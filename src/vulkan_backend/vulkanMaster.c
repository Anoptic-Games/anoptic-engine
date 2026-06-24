/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <vulkan/vulkan.h>
#include <mimalloc.h>
#include <mimalloc-override.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/gpu_alloc.h"

#define GLFW_INCLUDE_VULKAN

// Variables

static VulkanContext ctx;
PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT = NULL;
RendererState rendererState;
GpuAllocator gpuAllocator;
GpuAllocator stagingAllocator;
GpuAllocator swapchainAllocator;
GpuAllocator textureAllocator;
GpuAllocator rcAllocator;       // radiance-cascade volumes (RADIANCE_CASCADES.md)

// Entity (render-slot) buffer sizing. INITIAL_ENTITY_CAPACITY is just the
// starting slot count, NOT a ceiling: the slot-indexed GPU buffers grow on demand
// (see ensureEntityCapacity) in ENTITY_GROWTH_CHUNK-aligned, geometrically-doubling
// steps. PALETTE_CAPACITY sizes the material/light palettes, which are indexed by
// distinct material/light, not by entity, so they scale on their own axis.
#define INITIAL_ENTITY_CAPACITY 10000u
#define ENTITY_GROWTH_CHUNK      8192u
#define PALETTE_CAPACITY        10000u
#define STREAM_CAPACITY         16384u  // streamed-transform lane; separate axis, not grown in v1
#define SLOT_STAGING_INIT        1024u  // initial per-frame delta budget for a SlotUpload; grows on demand

struct VulkanGarbage vulkanGarbage = { NULL, NULL, NULL}; // THROW OUT WHEN YOU'RE DONE WITH IT

// --- Profiling harness (RADIANCE_CASCADES.md §8) -------------------------------------------
// Per-pass GPU timestamps as a fence-post: one timestamp (boundary enum in structs.h) at each
// section boundary, region time = consecutive delta * timestampPeriod. All boundaries are written
// unconditionally at top level (outside any render pass) so a skipped section yields a ~0 region,
// never an unwritten query. BOTTOM_OF_PIPE marks "all prior work retired", so a delta is that
// section's wall-clock including barrier stalls — coarse but apples-to-apples across lighting modes
// on the single graphics queue. Period (ns/tick) + valid bits live on rendererState (set at init).
static double   g_tsAccumMs[ANO_TS_COUNT - 1]; // accumulated per-region ms over the print window
static uint32_t g_tsFrames = 0;             // frames accumulated since the last print
#define ANO_PROFILE_PRINT_INTERVAL 120u     // print averaged stats every N frames

// Live VRAM use of a bump allocator: sum of each block's high-water offset (RADIANCE_CASCADES.md
// reports per-allocator resident so the shadow atlas can be broken out from the RC budget).
static VkDeviceSize allocator_used_bytes(const GpuAllocator* a) {
    VkDeviceSize used = 0;
    for (uint32_t i = 0; i < a->blockCount; i++) used += a->blocks[i].offset;
    return used;
}

// Stamp a section-boundary timestamp (BOTTOM_OF_PIPE = after all prior work). No-op when the queue
// has no timestamp support. The frame-begin boundary is stamped separately at TOP_OF_PIPE.
static inline void ano_ts(VkCommandBuffer cmd, uint32_t query) {
    if (rendererState.timestampValidBits)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            rendererState.frames[rendererState.frameIndex].timestampPool, query);
}

static GLFWwindow* window;

static Monitors monitors =
{
	.monitorInfos = NULL,	// Array of MonitorInfo for each monitor
	.monitorCount = 0		// Total number of monitors
};


// SlotUpload helpers (defined below, before ensureEntityCapacity) — forward-declared because
// recordCommandBuffer and the apply path use them earlier in the file.
static bool slot_upload_create(SlotUpload* b, uint32_t capacity, uint32_t stride, uint32_t stagingCap);
static void slot_upload_stage(SlotUpload* b, uint32_t f, uint32_t index, const void* value);
static void slot_upload_flush(VkCommandBuffer cmd, SlotUpload* b, uint32_t f);
static bool slot_upload_grow_device(SlotUpload* b, uint32_t newCap, uint32_t keep);

// Assorted utility functions

void unInitVulkan() // A celebration
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (rendererState.frames[i].frameSubmitted)
		{
			vkWaitForFences(ctx.device, 1, &(rendererState.frames[i].frameFence), VK_TRUE, UINT64_MAX);
			rendererState.frames[i].frameSubmitted = false; // reset the status
		}
    }

	// ECS<->render bridge teardown. CPU-only; safe on a zeroed state (early-init
	// failure) since the destroys guard NULL and the heap free is gated below.
	ano_render_bridge_destroy(&rendererState.bridge);
	render_slots_destroy(&rendererState.slots);
	if (rendererState.renderHeap)
	{
		mi_heap_destroy(rendererState.renderHeap);
		rendererState.renderHeap = NULL;
	}

	if (vulkanGarbage.ctx)
	{
		cleanupVulkan(vulkanGarbage.ctx);
	}
	
	if (vulkanGarbage.window)
	{
		glfwDestroyWindow(vulkanGarbage.window);
		glfwTerminate();
	}

	if (vulkanGarbage.monitors)
	{
		cleanupMonitors(vulkanGarbage.monitors);
	}

	#ifdef DEBUG_BUILD
	// Validation summary: g_ValidationErrors counts every WARNING+ message routed to
	// debugCallback over the run. 0 == validation-clean; nonzero is a regression to chase.
	printf("Validation messages (warning+) this run: %u\n", g_ValidationErrors);
	#endif
}

bool anoShouldClose()
{
	return glfwWindowShouldClose(window);
}

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle)
{
    uint32_t frameIdx = rendererState.frameIndex;
    DeletionQueue* q = &state->frames[frameIdx].deletionQueue;

    if (q->count >= q->capacity) {
        q->capacity = q->capacity == 0 ? 64 : q->capacity * 2;
        q->tasks = realloc(q->tasks, q->capacity * sizeof(DeletionTask));
    }

    q->tasks[q->count++] = (DeletionTask){ .type = type, .handle = handle };
}

void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    DeletionQueue* q = &state->frames[frameIndex].deletionQueue;

    for (uint32_t i = 0; i < q->count; i++) {
        DeletionTask task = q->tasks[i];
        switch (task.type) {
            case RESOURCE_TYPE_GEOMETRY_MESH:
                geometry_pool_free(&state->globalGeometryPool, task.handle);
                break;
            case RESOURCE_TYPE_BINDLESS_TEXTURE:
                // For now bindless_register_texture handles index. 
                // We'd add a bindless_free_texture(ctx, &state->bindlessTextures, task.handle) eventually
                break;
        }
    }
    q->count = 0; // Clear queue for next time we hit this frame
}

// Graphics operations

static const RenderPassDef g_framePasses[] = {
    // 0. GPU animation update
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_UPDATE,
        .dispatchX  = 0,
    },
    // 1. Streamed-transform scatter (overwrites ANO_MOTION_STREAMED slots with CPU data)
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_SCATTER,
        .dispatchX  = 0,  // computed from streamCount at runtime
    },
    // 2. Shadow-frustum setup: build each shadow map's light-space viewProj + planes from the
    //    light's live transform. Shared (not per view); must precede cull (which tests them).
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_SHADOWSETUP,
        .dispatchX  = 0,  // computed from shadow-frustum count at runtime
    },
    // 3. GPU culling (camera + shadow frustums, single pass)
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_CULL,
        .dispatchX  = 0,  // computed from entityCount at runtime
    },
    // 3. Clustered-forward light assignment (froxel light lists for the fragment passes).
    //    perView: each view bins lights against its own frustum into its own froxel lists.
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTCULL,
        .dispatchX  = 0,  // computed from cluster count at runtime
        .perView    = true,
    },
    // 4. Opaque geometry (perView: rendered once per view into that view's HDR target + depth)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // transmission pass loads this depth
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
    // 5. Transmissive geometry
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_TRANSMISSION,
        .implementationIndex    = 1,  // blended transmission variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE, // last pass; nothing reads depth after
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
};

// Whether a light type's direct occlusion is shadow-mapped (vs carried by the radiance cascade
// field) under the given AnoLightingMode. Mirrors lightUsesShadowMap() in flat.frag /
// transmission.frag — the shadow depth render (caster geometry) and the fragment PCF sample MUST
// agree, or a gated-off atlas layer gets sampled stale. HYBRID keeps directional + spot maps and
// routes point lights to RC; RC drops all shadow maps; SHADOWMAP keeps all (current behavior).
static inline bool lightTypeShadowMapped(uint32_t lightType, uint32_t mode)
{
    if (mode == ANO_LIGHTING_SHADOWMAP) return true;
    if (mode == ANO_LIGHTING_RC)        return false;
    return lightType != (uint32_t)LIGHT_TYPE_POINT; // ANO_LIGHTING_HYBRID
}

void recordCommandBuffer(uint32_t imageIndex)
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // re-recorded every frame, submitted once
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	VkCommandBuffer cmd = rendererState.frames[rendererState.frameIndex].commandBuffer;

	if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
	{
		printf("Failed to begin recording command buffer!\n");
	}

	// Profiling: reset this frame's query pool and stamp the frame-begin boundary (outside any
	// render pass). The five section boundaries below are stamped unconditionally at top level.
	if (rendererState.timestampValidBits) {
		vkCmdResetQueryPool(cmd, rendererState.frames[rendererState.frameIndex].timestampPool, 0, ANO_TS_COUNT);
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			rendererState.frames[rendererState.frameIndex].timestampPool, ANO_TS_FRAME_BEGIN);
	}

	// Transition swapchain image to color attachment optimal
	VkImageMemoryBarrier swapChainBarrier = {};
	swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapChainBarrier.image = rendererState.images[imageIndex];
	swapChainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	swapChainBarrier.subresourceRange.baseMipLevel = 0;
	swapChainBarrier.subresourceRange.levelCount = 1;
	swapChainBarrier.subresourceRange.baseArrayLayer = 0;
	swapChainBarrier.subresourceRange.layerCount = 1;
	swapChainBarrier.srcAccessMask = 0;
	swapChainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

    // Each view's HDR resolve target is moved to COLOR_ATTACHMENT inside the per-view loop below
    // (UNDEFINED -> COLOR: the geometry clear + resolve overwrite the whole render area).

    // Upload this frame's staged per-slot deltas into the DEVICE_LOCAL authoritative buffers
    // before any pass reads them. One vkCmdCopyBuffer per buffer, bracketed by a single
    // read->transfer / transfer->read barrier: the first scope reaches back across submission
    // order on this (single graphics) queue, so prior frames' shader reads of the shared buffers
    // complete before the copy writes; the second makes the writes visible to this frame's
    // update/cull/geometry reads. Skipped entirely when nothing changed this frame.
    {
        SlotUpload* ups[5] = {
            &rendererState.initialTransformBuffer, &rendererState.motionBuffer,
            &rendererState.instanceDataBuffer, &rendererState.lightBuffer,
            &rendererState.culling.entity,
        };
        uint32_t fi = rendererState.frameIndex;
        bool any = false;
        for (int u = 0; u < 5; u++) if (ups[u]->staged[fi]) { any = true; break; }
        if (any) {
            // These buffers are only ever read by the shader stages below: compute (update/cull/
            // lightcull), the geometry stage (entity buffer), and fragment (instance data + lights).
            // So both the WAR (pre) and the visibility (post) scopes are exactly that stage set, not
            // ALL_COMMANDS. The pre barrier's first scope still reaches prior submissions on this
            // single queue, so earlier frames' shader reads finish before the copy overwrites.
            VkPipelineStageFlags shaderStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                | (ctx.deviceCapabilities.meshShader ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkMemoryBarrier pre = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
            vkCmdPipelineBarrier(cmd, shaderStages, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &pre, 0, NULL, 0, NULL);
            for (int u = 0; u < 5; u++) slot_upload_flush(cmd, ups[u], fi);
            VkMemoryBarrier post = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, shaderStages,
                0, 1, &post, 0, NULL, 0, NULL);
        }
    }
    ano_ts(cmd, ANO_TS_AFTER_UPLOAD);

    uint32_t entityCount = rendererState.entityCount;

    // === Shared (view-independent) compute: update, scatter, cull ===
    // The cull dispatch is single-pass multi-frustum — it tests each entity against every view's
    // frustum and writes all views' partitions at once, so it runs here, not per view.
    if (entityCount > 0) {
        uint32_t streamCount = rendererState.transformStream.count[rendererState.frameIndex];
        for (int p = 0; p < (int)(sizeof(g_framePasses)/sizeof(g_framePasses[0])); p++) {
            const RenderPassDef* pass = &g_framePasses[p];
            if (pass->type != PASS_COMPUTE || pass->perView) continue;
            if (pass->prototype == PIPELINE_COMPUTE_SCATTER && streamCount == 0)
                continue; // nothing streamed this frame: skip the scatter pass entirely

            if (pass->prototype == PIPELINE_COMPUTE_CULL) {
                // Zero the entire indirect buffer (sized for the larger command format) so
                // unwritten partitions decode as no-op draws on either path, plus the draw count.
                VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
                    ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
                vkCmdFillBuffer(cmd, rendererState.indirectBuffer.buffer[rendererState.frameIndex], 0,
                    cmdStride * rendererState.indirectBuffer.capacity * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT, 0);
                vkCmdFillBuffer(cmd, rendererState.culling.drawCountBuffer[rendererState.frameIndex], 0,
                    sizeof(uint32_t) * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT, 0);

                VkMemoryBarrier fillBarrier = {};
                fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &fillBarrier, 0, NULL, 0, NULL);
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.prototypes[pass->prototype].implementations[0].pipeline);

            VkDescriptorSet set =
                pass->prototype == PIPELINE_COMPUTE_UPDATE      ? rendererState.frames[rendererState.frameIndex].updateSet :
                pass->prototype == PIPELINE_COMPUTE_SCATTER     ? rendererState.frames[rendererState.frameIndex].scatterSet :
                pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP ? rendererState.frames[rendererState.frameIndex].shadow.setupSet :
                                                                  rendererState.frames[rendererState.frameIndex].cullSet;

            // Scatter binding 1 (xform ring) is STORAGE_BUFFER_DYNAMIC: bind the
            // published slice by per-frame dynamic offset; other passes have none.
            uint32_t dynCount = pass->prototype == PIPELINE_COMPUTE_SCATTER ? 1u : 0u;
            const uint32_t* dynOff = pass->prototype == PIPELINE_COMPUTE_SCATTER
                ? &rendererState.transformStream.dynOffset[rendererState.frameIndex] : NULL;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &set, dynCount, dynOff);

            if (pass->prototype == PIPELINE_COMPUTE_UPDATE) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &entityCount);
            } else if (pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &streamCount);
            }

            uint32_t dispatchX;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                dispatchX = (ANO_SHADOW_FRUSTUM_COUNT + 63u) / 64u; // one invocation per shadow frustum
            } else {
                uint32_t workItems = pass->prototype == PIPELINE_COMPUTE_SCATTER ? streamCount : entityCount;
                dispatchX = pass->dispatchX == 0 ? (workItems + 255) / 256 : pass->dispatchX;
            }
            vkCmdDispatch(cmd, dispatchX, 1, 1);

            VkMemoryBarrier memoryBarrier = {};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                // Shadow frustums feed the cull (compute), the depth render (mesh/vertex), and the
                // fragment sampler — make the writes visible to all three.
                VkPipelineStageFlags geomStage = ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | geomStage | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else if (pass->prototype == PIPELINE_COMPUTE_UPDATE || pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                // update -> scatter is a WAW on streamed slots (scatter must win); both -> cull is a read.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else {
                // The cull pass feeds the indirect commands (DRAW_INDIRECT) and the
                // compacted/entity SSBOs read by the geometry stage (mesh or vertex).
                VkPipelineStageFlags geomStage = ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            }
        }
    }

    uint32_t drawSlotCount = ano_draw_pipeline_count();

    ano_ts(cmd, ANO_TS_AFTER_COMPUTE);

    // === Radiance cascades (RADIANCE_CASCADES.md M2): rasterized clipmap voxelization ===
    // Clear the scene voxel volumes, then rasterize the cull-compacted opaque geometry by 3 ortho
    // clipmap projections (one per dominant axis) into them via imageStore (voxelize.frag). Fills the
    // albedo+opacity + emission substrate M3 marches. View-independent (×1 shared). Gated on an RC
    // mode so SHADOWMAP keeps a clean baseline; nothing samples the volumes yet.
    bool rcActive = (rendererState.lightingMode != ANO_LIGHTING_SHADOWMAP);
    if (rcActive && entityCount > 0) {
        VkImage voxImgs[2] = { rendererState.rcVoxelAlbedo, rendererState.rcVoxelEmission };
        VkImageSubresourceRange full = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // UNDEFINED -> TRANSFER_DST, clear to zero (opacity 0 = empty), TRANSFER_DST -> GENERAL.
        VkImageMemoryBarrier toClear[2] = {}, toGeneral[2] = {};
        for (int k = 0; k < 2; k++) {
            toClear[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toClear[k].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toClear[k].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toClear[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toClear[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toClear[k].image = voxImgs[k]; toClear[k].subresourceRange = full;
            toClear[k].srcAccessMask = 0; toClear[k].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 2, toClear);
        VkClearColorValue zero = {0};
        for (int k = 0; k < 2; k++)
            vkCmdClearColorImage(cmd, voxImgs[k], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &full);
        for (int k = 0; k < 2; k++) {
            toGeneral[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGeneral[k].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toGeneral[k].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            toGeneral[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral[k].image = voxImgs[k]; toGeneral[k].subresourceRange = full;
            toGeneral[k].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; toGeneral[k].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 2, toGeneral);

        // Attachment-less render: the rasterizer grid is the voxel resolution; voxelize.frag derives
        // the third coord from worldPos and imageStores. Three draws of view 0's opaque partition,
        // one ortho axis each (push.shadowFrustumIndex selects the clipmap matrix in set 2).
        VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea.offset = (VkOffset2D){0, 0};
        ri.renderArea.extent = (VkExtent2D){ ANO_RC_VOXEL_DIM, ANO_RC_VOXEL_DIM };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 0;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.rcVoxelizePipeline);
        VkViewport vvp = { 0.0f, 0.0f, (float)ANO_RC_VOXEL_DIM, (float)ANO_RC_VOXEL_DIM, 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &vvp);
        VkRect2D vsc = { .offset = {0, 0}, .extent = { ANO_RC_VOXEL_DIM, ANO_RC_VOXEL_DIM } };
        vkCmdSetScissor(cmd, 0, 1, &vsc);

        bool useMeshV = ctx.deviceCapabilities.meshShader;
        VkShaderStageFlags pcStageV = useMeshV ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
        uint32_t drawSlotCountV = ano_draw_pipeline_count();
        uint32_t opaqueSlotV = ano_draw_slot_of(PIPELINE_FLAT);
        uint32_t partitionV = 0u * drawSlotCountV + opaqueSlotV; // view 0 opaque
        uint32_t maxDrawsV = rendererState.indirectBuffer.capacity;
        VkBuffer indirectBufV = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
        VkBuffer drawCountBufV = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
        VkDeviceSize countOffsetV = (VkDeviceSize)partitionV * sizeof(uint32_t);

        VkDescriptorSet vSet0 = rendererState.frames[rendererState.frameIndex].views[0].globalSet;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.rcVoxelizeLayout, 0, 1, &vSet0, 0, NULL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.rcVoxelizeLayout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.rcVoxelizeLayout, 2, 1, &rendererState.rcVoxelizeSet, 0, NULL);
        if (!useMeshV) vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (uint32_t axis = 0; axis < ANO_RC_VOXEL_AXES; axis++) {
            uint32_t pcVals[2] = { partitionV * rendererState.culling.maxEntities, axis }; // baseOffset, axis
            vkCmdPushConstants(cmd, rendererState.rcVoxelizeLayout, pcStageV, 0, sizeof(pcVals), pcVals);
            if (useMeshV) {
                VkDeviceSize io = (VkDeviceSize)partitionV * maxDrawsV * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                if (ctx.deviceCapabilities.drawIndirectCount)
                    pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBufV, io, drawCountBufV, countOffsetV, maxDrawsV, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                else
                    pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBufV, io, entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
            } else {
                VkDeviceSize io = (VkDeviceSize)partitionV * maxDrawsV * sizeof(VkDrawIndexedIndirectCommand);
                if (ctx.deviceCapabilities.drawIndirectCount)
                    vkCmdDrawIndexedIndirectCount(cmd, indirectBufV, io, drawCountBufV, countOffsetV, maxDrawsV, sizeof(VkDrawIndexedIndirectCommand));
                else
                    vkCmdDrawIndexedIndirect(cmd, indirectBufV, io, entityCount, sizeof(VkDrawIndexedIndirectCommand));
            }
        }
        vkCmdEndRendering(cmd);

        // GENERAL -> SHADER_READ for M3's trace (sampled). No consumer yet in M2.
        VkImageMemoryBarrier toRead[2] = {};
        for (int k = 0; k < 2; k++) {
            toRead[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toRead[k].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            toRead[k].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead[k].image = voxImgs[k]; toRead[k].subresourceRange = full;
            toRead[k].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toRead[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        // Voxel volumes visible to BOTH the trace (compute) below and flat.frag's debug sample (fragment).
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 2, toRead);

        // Cascade-0 trace (M3b): gather the voxel volumes into the irradiance field. irradiance
        // UNDEFINED -> GENERAL, dispatch, GENERAL -> SHADER_READ for the geometry stage's GI sample.
        VkImageSubresourceRange irrRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier irrToGeneral = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        irrToGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // regenerated each frame; discard
        irrToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        irrToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        irrToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        irrToGeneral.image = rendererState.rcIrradiance;
        irrToGeneral.subresourceRange = irrRange;
        irrToGeneral.srcAccessMask = 0; irrToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &irrToGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.rcTracePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.rcTraceLayout, 0, 1, &rendererState.rcTraceSet, 0, NULL);
        uint32_t traceGroups = (ANO_RC_IRRADIANCE_DIM + 3u) / 4u; // local_size 4^3
        vkCmdDispatch(cmd, traceGroups, traceGroups, traceGroups);

        VkImageMemoryBarrier irrToRead = irrToGeneral;
        irrToRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        irrToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irrToRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; irrToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &irrToRead);
    }
    ano_ts(cmd, ANO_TS_AFTER_RC);

    // === Shadow depth render: each shadow frustum's opaque casters into its atlas layer ===
    // Reads the cull's shadow partition (opaque draw slot only); writes depth into one array layer;
    // leaves the layer in SHADER_READ for the camera fragment passes to PCF-sample below.
    if (entityCount > 0) {
        ShadowResources* sh = &rendererState.frames[rendererState.frameIndex].shadow;
        uint32_t opaqueSlot = ano_draw_slot_of(PIPELINE_FLAT);
        bool useMeshS = ctx.deviceCapabilities.meshShader;
        uint32_t maxDrawsS = rendererState.indirectBuffer.capacity;
        VkShaderStageFlags pcStageS = useMeshS ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

        const ShadowFrustumConfig* shadowCfgs = (const ShadowFrustumConfig*)rendererState.shadowFrustumConfigMapped;
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            // Gate the depth render on the lighting mode: a light type carried by radiance cascades
            // this frame renders no caster geometry (the shadow-map bandwidth win under measurement).
            // Still drive its layer to SHADER_READ so the sampled atlas array stays uniformly
            // readable — the fragment is gated not to sample it, but the bound view spans all layers.
            if (!lightTypeShadowMapped(shadowCfgs[s].lightType, rendererState.lightingMode)) {
                VkImageMemoryBarrier toReadSkip = {};
                toReadSkip.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toReadSkip.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // contents unused; discard
                toReadSkip.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                toReadSkip.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toReadSkip.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toReadSkip.image = sh->atlasImage;
                toReadSkip.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
                toReadSkip.srcAccessMask = 0;
                toReadSkip.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &toReadSkip);
                continue;
            }
            // This atlas layer -> DEPTH_ATTACHMENT (UNDEFINED: cleared + fully written this frame).
            VkImageMemoryBarrier toDepth = {};
            toDepth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toDepth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            toDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDepth.image = sh->atlasImage;
            toDepth.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
            toDepth.srcAccessMask = 0;
            toDepth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0, 0, NULL, 0, NULL, 1, &toDepth);

            VkClearValue clearD = {}; clearD.depthStencil.depth = 1.0f;
            VkRenderingAttachmentInfo depthAtt = {};
            depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAtt.imageView = sh->layerView[s];
            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAtt.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAtt.clearValue = clearD;

            VkRenderingInfo ri = {};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.offset = (VkOffset2D){0, 0};
            ri.renderArea.extent = (VkExtent2D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM };
            ri.layerCount = 1;
            ri.colorAttachmentCount = 0;
            ri.pDepthAttachment = &depthAtt;
            vkCmdBeginRendering(cmd, &ri);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowPipeline);
            VkViewport vp = { 0.0f, 0.0f, (float)ANO_SHADOW_DIM, (float)ANO_SHADOW_DIM, 0.0f, 1.0f };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            VkRect2D sc = { .offset = {0, 0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } };
            vkCmdSetScissor(cmd, 0, 1, &sc);

            // Shadow pipeline reuses the FLAT layout (sets 0/1/2). Set 0 = view 0's global set
            // (shared transforms/compacted); set 2 = the shadow set (viewProjs). Bindless (set 1)
            // is unused by the depth shaders but bound for layout compatibility.
            VkPipelineLayout flatLayout = rendererState.prototypes[PIPELINE_FLAT].layout;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 0, 1,
                &rendererState.frames[rendererState.frameIndex].views[0].globalSet, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 1, 1,
                &rendererState.bindlessTextures.set, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 2, 1,
                &sh->geomSet, 0, NULL);

            uint32_t partition = (ANO_VIEW_COUNT + s) * drawSlotCount + opaqueSlot;
            uint32_t pcVals[2] = { partition * rendererState.culling.maxEntities, s }; // baseOffset, shadowFrustumIndex
            vkCmdPushConstants(cmd, flatLayout, pcStageS, 0, sizeof(pcVals), pcVals);

            VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
            VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
            VkDeviceSize countOffset = (VkDeviceSize)partition * sizeof(uint32_t);
            if (useMeshS) {
                VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                if (ctx.deviceCapabilities.drawIndirectCount)
                    pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                else
                    pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
            } else {
                vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawIndexedIndirectCommand);
                if (ctx.deviceCapabilities.drawIndirectCount)
                    vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawIndexedIndirectCommand));
                else
                    vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawIndexedIndirectCommand));
            }
            vkCmdEndRendering(cmd);

            // This atlas layer -> SHADER_READ for the camera fragment passes' PCF sampling.
            VkImageMemoryBarrier toRead = {};
            toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.image = sh->atlasImage;
            toRead.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
            toRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &toRead);
        }
    }

    ano_ts(cmd, ANO_TS_AFTER_SHADOW);

    // === Per view: light-cull (this view's froxel lists) then geometry into this view's
    // HDR target + depth, reading this view's cull partition. ===
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
        ViewResources* vr = &rendererState.frames[rendererState.frameIndex].views[v];

        // This view's HDR resolve target: UNDEFINED -> COLOR_ATTACHMENT (geometry clears + the
        // resolve overwrite the whole render area, so prior contents are not needed).
        {
            VkImageMemoryBarrier hdrToColor = {};
            hdrToColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            hdrToColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            hdrToColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrToColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToColor.image = vr->hdrColorImage;
            hdrToColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hdrToColor.subresourceRange.levelCount = 1;
            hdrToColor.subresourceRange.layerCount = 1;
            hdrToColor.srcAccessMask = 0;
            hdrToColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, NULL, 0, NULL, 1, &hdrToColor);
        }

        // Light-cull for this view: bins lights into this view's froxel grid using its frustum.
        if (entityCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout, 0, 1, &vr->lightcullSet, 0, NULL);
            uint32_t lightcullDispatch = (ANO_CLUSTER_COUNT + 63u) / 64u;
            vkCmdDispatch(cmd, lightcullDispatch, 1, 1);

            VkMemoryBarrier lcBarrier = {};
            lcBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            lcBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            lcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 1, &lcBarrier, 0, NULL, 0, NULL);
        }

        // The MSAA color target is shared and reused across views sequentially: order view v's
        // writes after view v-1's resolve read it.
        if (v > 0) {
            VkImageMemoryBarrier msaaReuse = {};
            msaaReuse.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            msaaReuse.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            msaaReuse.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            msaaReuse.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            msaaReuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            msaaReuse.image = rendererState.colorImage;
            msaaReuse.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            msaaReuse.subresourceRange.levelCount = 1;
            msaaReuse.subresourceRange.layerCount = 1;
            msaaReuse.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            msaaReuse.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, NULL, 0, NULL, 1, &msaaReuse);
        }

        for (int p = 0; p < (int)(sizeof(g_framePasses)/sizeof(g_framePasses[0])); p++) {
            const RenderPassDef* pass = &g_framePasses[p];
            if (pass->type != PASS_GRAPHICS) continue;

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            VkClearValue clearDepth = {};
            clearDepth.depthStencil.depth = 1.0f;
            clearDepth.depthStencil.stencil = 0;

            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = rendererState.colorView; // shared MSAA color
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.resolveMode = pass->resolveMode;
            colorAttachment.resolveImageView = vr->hdrColorView; // resolve into this view's HDR target
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = pass->colorLoadOp;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = clearColor;

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = vr->depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachment.loadOp = pass->depthLoadOp;
            depthAttachment.storeOp = pass->depthStoreOp;
            depthAttachment.clearValue = clearDepth;

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
            renderingInfo.renderArea.extent = rendererState.imageExtent;
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = pass->colorAttachmentCount;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;
            renderingInfo.pStencilAttachment = NULL;

            vkCmdBeginRendering(cmd, &renderingInfo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.prototypes[pass->prototype].implementations[pass->implementationIndex].pipeline);

            VkViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)(rendererState.imageExtent.width);
            viewport.height = (float)(rendererState.imageExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor = {};
            scissor.offset = (VkOffset2D){0, 0};
            scissor.extent = rendererState.imageExtent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // This view's global set selects its camera UBO + froxel light lists.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &vr->globalSet, 0, NULL);
            // Set 2: shadow frustums + atlas + per-light info (fragment PCF-samples shadows).
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 2, 1,
                &rendererState.frames[rendererState.frameIndex].shadow.geomSet, 0, NULL);

            if (entityCount > 0) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    rendererState.prototypes[pass->prototype].layout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);

                // Compacted draws live in this (view, draw slot) partition, matching the partition
                // cull.comp wrote them into: partition = view*drawSlotCount + slot.
                uint32_t slot = ano_draw_slot_of(pass->prototype);
                uint32_t partition = v * drawSlotCount + slot;
                uint32_t baseOffset = partition * rendererState.culling.maxEntities;
                bool useMesh = ctx.deviceCapabilities.meshShader;
                VkShaderStageFlags pcStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, pcStage, 0, sizeof(uint32_t), &baseOffset);

                VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
                VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
                VkDeviceSize countOffset = (VkDeviceSize)partition * sizeof(uint32_t);
                uint32_t maxDraws = rendererState.indirectBuffer.capacity;

                if (useMesh) {
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDraws * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    } else {
                        pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    }
                } else {
                    vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDraws * sizeof(VkDrawIndexedIndirectCommand);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawIndexedIndirectCommand));
                    } else {
                        vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }
            }

            vkCmdEndRendering(cmd);
        }

        // This view's HDR target -> SHADER_READ for the composite below.
        {
            VkImageMemoryBarrier hdrToRead = {};
            hdrToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            hdrToRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            hdrToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToRead.image = vr->hdrColorImage;
            hdrToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hdrToRead.subresourceRange.levelCount = 1;
            hdrToRead.subresourceRange.layerCount = 1;
            hdrToRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            hdrToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &hdrToRead);
        }
    }

    ano_ts(cmd, ANO_TS_AFTER_LIGHTING);

    // --- Composite: tonemap each view's HDR target onto the swapchain ---
    // View 0 fills the screen; auxiliary views composite as picture-in-picture insets along the
    // bottom-right. Each draw is the same ACES tonemap fullscreen triangle, scoped to its
    // destination rect by viewport+scissor, sampling that view's HDR target via its tonemap set.
    // Each view's HDR target was already moved to SHADER_READ at the end of its geometry pass.
    {
        VkRenderingAttachmentInfo tmColor = {};
        tmColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        tmColor.imageView = rendererState.views[imageIndex];
        tmColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        tmColor.resolveMode = VK_RESOLVE_MODE_NONE;
        tmColor.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // view 0 covers every pixel
        tmColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo tmInfo = {};
        tmInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        tmInfo.renderArea.offset = (VkOffset2D){0, 0};
        tmInfo.renderArea.extent = rendererState.imageExtent;
        tmInfo.layerCount = 1;
        tmInfo.colorAttachmentCount = 1;
        tmInfo.pColorAttachments = &tmColor;

        vkCmdBeginRendering(cmd, &tmInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.tonemapPipeline);

        uint32_t W = rendererState.imageExtent.width, H = rendererState.imageExtent.height;
        uint32_t insetW = W / 3u, insetH = H / 3u, margin = 16u;

        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &rendererState.frames[rendererState.frameIndex].views[v];

            VkViewport tmViewport = {};
            VkRect2D tmScissor = {};
            if (v == 0) {
                // Main view: full screen.
                tmViewport.x = 0.0f; tmViewport.y = 0.0f;
                tmViewport.width = (float)W; tmViewport.height = (float)H;
                tmScissor.offset = (VkOffset2D){0, 0};
                tmScissor.extent = rendererState.imageExtent;
            } else {
                // Inset: stack auxiliary views up the right edge from the bottom corner.
                uint32_t idx = v - 1u;
                int32_t x = (int32_t)(W - insetW - margin);
                int32_t y = (int32_t)(H - margin - (insetH + margin) * (idx + 1u) + margin);
                tmViewport.x = (float)x; tmViewport.y = (float)y;
                tmViewport.width = (float)insetW; tmViewport.height = (float)insetH;
                tmScissor.offset = (VkOffset2D){x, y};
                tmScissor.extent = (VkExtent2D){insetW, insetH};
            }
            tmViewport.minDepth = 0.0f; tmViewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &tmViewport);
            vkCmdSetScissor(cmd, 0, 1, &tmScissor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.tonemapLayout,
                0, 1, &vr->tonemapSet, 0, NULL);
            vkCmdDraw(cmd, 3, 1, 0, 0); // fullscreen triangle, scoped by viewport+scissor
        }
        vkCmdEndRendering(cmd);
    }
    ano_ts(cmd, ANO_TS_AFTER_COMPOSITE);

	// Transition swapchain image to present
	swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	swapChainBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	swapChainBarrier.dstAccessMask = 0;

	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

	if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
	{
		printf("Failed to record command buffer!\n");
	}
}



void printUniformTransferState()
{
	// Swap Chain Components
	printf("\n=== Swap Chain Components ===\n");
	printf("Image count: %d\n", rendererState.imageCount);
	printf("Image extent: width = %d, height = %d\n", rendererState.imageExtent.width, rendererState.imageExtent.height);
	
	// Buffer Components
	printf("\n=== Buffer Components ===\n");
	printf("Mesh index: %u\n", rendererState.entities[0].meshIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Uniform buffer %d (view 0): %p\n", i, (void*)rendererState.frames[i].views[0].uniformBuffer);
		printf("Uniform alloc %d (view 0): %p\n", i, (void*)rendererState.frames[i].views[0].uniformAlloc.memory);
		printf("Uniform buffer mapping %d (view 0): %p\n", i, rendererState.frames[i].views[0].uniformMapped);
	}

	// Synchronization Components
	printf("\n=== Synchronization Components ===\n");
	printf("Current frame index: %d\n", rendererState.frameIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Frame %d submitted: %d\n", i, rendererState.frames[i].frameSubmitted);
	}
	printf("\n======================================\n");
}

void updateTransformBuffer(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
	// Deprecated: Transforms are now updated via GPU compute shader.
	// Initial transforms are set directly in instantiate_node().
	state->transformBuffer.count = state->entityCount;
}

void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    // The render slot authority is the cull/dispatch bound; dead slots within
    // [0, slotHighWater) self-skip in the shaders (meshIndex == NO_MESH_INDEX).
    state->entityCount = state->slots.slotHighWater;
    uint32_t entityCount = state->entityCount;

    // Draw count is DEVICE_LOCAL now: zeroed on the GPU timeline by the vkCmdFillBuffer
    // in recordCommandBuffer (before the cull pass), not by a CPU memset.

    // Update CullUBO. One frustum per view: derive viewProj + planes from each view's camera
    // (updateUniformBuffer already wrote each view's matrices into its mapped uniform). The cull
    // dispatch tests every entity against all of them in one pass.
    CullUBO* ubo = state->culling.ubo.mapped[frameIndex];
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; ++v) {
        GlobalUBO* viewUbo = state->frames[frameIndex].views[v].uniformMapped;
        multiplyMat4(ubo->views[v].viewProj, viewUbo->proj, viewUbo->view);
        extractFrustumPlanes(ubo->views[v].frustumPlanes, ubo->views[v].viewProj);
        // Publish the active light count to each view's fragment stage.
        viewUbo->lightCount = state->lightBuffer.count;
        // Publish the runtime lighting mode + debug selector (RADIANCE_CASCADES.md). The fragment
        // stage gates per-light shadow sampling on this; the shadow depth render is gated to match.
        viewUbo->lightingMode = state->lightingMode;
        viewUbo->debugView = state->debugView;
    }

    ubo->viewCount = ANO_VIEW_COUNT;
    ubo->entityCount = entityCount;
    ubo->maxEntities = state->culling.maxEntities;
    ubo->drawSlotCount = ano_draw_pipeline_count();

    // Publish the PipelineType -> draw-slot map cull.comp compacts by. Frame-invariant, but
    // rewritten here so each frame's UBO (incl. a freshly grown one) is always populated.
    for (uint32_t t = 0; t < 16u; ++t)
        ubo->drawSlotOf[t] = (t < PIPELINE_TYPE_COUNT) ? ano_draw_slot_of((PipelineType)t) : ANO_NO_DRAW_SLOT;

    // The EntitySSBO (mesh/material per slot) is seeded once at init and mutated
    // sparsely through the command bridge (render_apply_commands) — no per-frame
    // O(N) rewrite. MeshSSBO/MeshBoundsSSBO below are per-mesh (bounded by
    // meshCount, not entity count) and refreshed so geometry-pool changes apply.

    // Update MeshSSBO and MeshBoundsSSBO
    uint32_t meshCount = state->globalGeometryPool.meshCount;
    uint32_t* meshData = (uint32_t*)state->culling.meshDataMapped[frameIndex];
    float* meshBounds = (float*)state->culling.meshBoundsMapped[frameIndex];
    
    for(uint32_t i=0; i < meshCount; i++) {
        MeshRegion* mesh = &state->globalGeometryPool.meshes[i];
        
        meshData[i*8 + 0] = mesh->meshletCount;
        meshData[i*8 + 1] = mesh->meshletOffset;
        meshData[i*8 + 2] = mesh->uniqueVerticesOffset;
        meshData[i*8 + 3] = mesh->trianglesOffset;
        meshData[i*8 + 4] = mesh->vertexOffset;
        meshData[i*8 + 5] = mesh->classicIndexCount;       // fallback: VkDrawIndexedIndirectCommand.indexCount
        meshData[i*8 + 6] = mesh->classicIndexOffset / 4;  // fallback: firstIndex (u32 index units)
        meshData[i*8 + 7] = mesh->boundsOffset;            // byte offset of per-meshlet bounds (sphere+cone) in metadata buffer; consumed by flat.mesh cone cull

        meshBounds[i*4 + 0] = mesh->boundingSphereCenter[0];
        meshBounds[i*4 + 1] = mesh->boundingSphereCenter[1];
        meshBounds[i*4 + 2] = mesh->boundingSphereCenter[2];
        meshBounds[i*4 + 3] = mesh->boundingSphereRadius;
    }
}

// ---------------------------------------------------------------------------
// ECS <-> render bridge consumer (VK_BACKEND_INTEROP.md S5-S7, S9).
//
// Drains discrete state-transition commands from the logic thread and applies
// them to the mapped GPU buffers by render slot, propagating each across all
// MAX_FRAMES_IN_FLIGHT copies, then advances the slot quarantine and reports
// retired render_ids back. Cost is O(pending changes), never O(entities).
//
// NOTE: the slot authority is wired and live, but the legacy entities[] +
// per-frame updateCullingBuffers() path is still authoritative for the existing
// scene. With no command producer running yet this consumer drains an empty ring
// (a no-op beyond advancing the frame counter), so it is behavior-neutral. The
// cutover — making slots/slotHighWater authoritative and deleting the O(N)
// rewrite — is gated on on-hardware verification.
// ---------------------------------------------------------------------------

// Applies one resolved command's flagged fields to a single frame's buffers.
// Teleports target initialTransform (the GPU animation pass derives the live
// transform from it); mesh/material land in the cull entity SSBO; light params
// translate into GPU LightData with the driving slot as transformIndex.
// Stages a resolved CREATE/UPDATE/DESTROY command's flagged fields into THIS frame's delta
// staging (the device-local copies are uploaded by the flush in recordCommandBuffer). DESTROY
// dead-marks the entity slot so the cull pass skips it. Mirrors the field set of the former
// direct mapped writes; one upload per frame is enough since the device buffers are shared.
static void stage_command_fields(RendererState* s, const RenderCommand* c, uint32_t slot, uint32_t f)
{
    if (c->kind == RCMD_DESTROY) {
        uint32_t dead[2] = { NO_MESH_INDEX, 0u }; // dead-mark: meshIndex == NO_MESH_INDEX
        slot_upload_stage(&s->culling.entity, f, slot, dead);
        return;
    }

    uint32_t fields = (c->kind == RCMD_CREATE)
        ? (RFIELD_TRANSFORM | RFIELD_MESH_MAT | RFIELD_ANIM | RFIELD_USERDATA |
           (c->light_index != ANO_RENDER_NO_LIGHT ? RFIELD_LIGHT : 0u))
        : c->fields;

    if (fields & RFIELD_TRANSFORM)
        slot_upload_stage(&s->initialTransformBuffer, f, slot, &c->transform);
    if (fields & RFIELD_ANIM)
        slot_upload_stage(&s->motionBuffer, f, slot, &c->motion);
    if (fields & RFIELD_USERDATA)
        slot_upload_stage(&s->instanceDataBuffer, f, slot, &c->instance_data);
    if (fields & RFIELD_MESH_MAT) {
        uint32_t ent[2] = { c->mesh_index, c->material_index };
        slot_upload_stage(&s->culling.entity, f, slot, ent);
    }
    if ((fields & RFIELD_LIGHT) && c->light_index != ANO_RENDER_NO_LIGHT) {
        LightData L = {0};
        L.color[0]       = c->light.color[0];
        L.color[1]       = c->light.color[1];
        L.color[2]       = c->light.color[2];
        L.intensity      = c->light.intensity;
        L.range          = c->light.range;
        L.innerConeCos   = c->light.innerConeCos;
        L.outerConeCos   = c->light.outerConeCos;
        L.type           = (uint32_t)c->light.type;
        L.transformIndex = slot; // world pos/dir derived from this slot's live transform
        L.enabled        = 1u;
        slot_upload_stage(&s->lightBuffer, f, c->light_index, &L);
    }
}

// Recreates one set of per-frame host-visible buffers at `newBytes`, preserving
// the leading `copyBytes` of each (0 == discard: the buffer is GPU-regenerated
// every frame, so its old contents are worthless). The old VkBuffer handles are
// destroyed; their arena memory is NOT reclaimed (the GPU allocator is a bump
// arena — see gpu_alloc.h), but geometric growth bounds the waste to ~the final
// size and a teardown reset reclaims it. Writes the new handle/allocation back
// into bufs[]/allocs[]; the caller re-derives typed mapped pointers from allocs.
// in:  bufs/allocs [MAX_FRAMES_IN_FLIGHT], usage, newBytes (>0), copyBytes (<= old size)
// out: true on success; false leaves already-grown frames valid but the set partial
static bool growBufferSet(VkBuffer bufs[MAX_FRAMES_IN_FLIGHT],
                          GpuAllocation allocs[MAX_FRAMES_IN_FLIGHT],
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                          VkDeviceSize newBytes, VkDeviceSize copyBytes)
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = newBytes, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VkBuffer nb = VK_NULL_HANDLE;
        if (vkCreateBuffer(ctx.device, &bi, NULL, &nb) != VK_SUCCESS)
            return false;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(ctx.device, nb, &mr);
        GpuAllocation na = gpu_alloc(&gpuAllocator, mr, props);
        if (na.memory == VK_NULL_HANDLE) { vkDestroyBuffer(ctx.device, nb, NULL); return false; }
        vkBindBufferMemory(ctx.device, nb, na.memory, na.offset);
        if (copyBytes && allocs[i].mapped && na.mapped)
            memcpy(na.mapped, allocs[i].mapped, (size_t)copyBytes);
        vkDestroyBuffer(ctx.device, bufs[i], NULL); // handle only; arena keeps the memory
        bufs[i] = nb;
        allocs[i] = na;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SlotUpload: ×1 DEVICE_LOCAL per-slot buffer fed by a per-frame host-visible delta
// staging ring (docs/artifacts/DEVICE_LOCAL_SLOTS.md). Replaces the former ×3
// host-visible mapped buffers for the command-written per-slot data. Render-thread only.
// Uses the file-global ctx/gpuAllocator like growBufferSet.
// ---------------------------------------------------------------------------

// in:  b, capacity (device elements), stride (bytes/elem), stagingCap (initial delta budget)
// out: device DEVICE_LOCAL buffer + N host-visible staging buffers + region arrays; false on failure
static bool slot_upload_create(SlotUpload* b, uint32_t capacity, uint32_t stride, uint32_t stagingCap)
{
    memset(b, 0, sizeof(*b));
    b->capacity   = capacity;
    b->stride     = stride;
    b->stagingCap = stagingCap ? stagingCap : 1u;

    VkBufferCreateInfo di = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)stride * capacity,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(ctx.device, &di, NULL, &b->device) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, b->device, &mr);
    b->deviceAlloc = gpu_alloc(&gpuAllocator, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (b->deviceAlloc.memory == VK_NULL_HANDLE) return false;
    vkBindBufferMemory(ctx.device, b->device, b->deviceAlloc.memory, b->deviceAlloc.offset);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo si = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = (VkDeviceSize)stride * b->stagingCap,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(ctx.device, &si, NULL, &b->staging[i]) != VK_SUCCESS) return false;
        VkMemoryRequirements smr;
        vkGetBufferMemoryRequirements(ctx.device, b->staging[i], &smr);
        b->stagingAllocs[i] = gpu_alloc(&gpuAllocator, smr,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (b->stagingAllocs[i].memory == VK_NULL_HANDLE) return false;
        vkBindBufferMemory(ctx.device, b->staging[i], b->stagingAllocs[i].memory, b->stagingAllocs[i].offset);
        b->stagingMapped[i] = b->stagingAllocs[i].mapped;
        b->regions[i] = (VkBufferCopy*)malloc((size_t)b->stagingCap * sizeof(VkBufferCopy));
        if (!b->regions[i]) return false;
        b->staged[i] = 0;
    }
    return true;
}

// Grows every staging buffer + region list to hold >= need delta entries. Recreates the
// staging buffers, so the caller must hold vkDeviceWaitIdle (init is already idle; at runtime
// only a single-tick mass bulk trips it). Preserves the current frame's already-staged span.
static bool slot_upload_grow_staging(SlotUpload* b, uint32_t need)
{
    if (need <= b->stagingCap) return true;
    uint32_t newCap = b->stagingCap;
    while (newCap < need) newCap *= 2u;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo si = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = (VkDeviceSize)b->stride * newCap,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VkBuffer nb = VK_NULL_HANDLE;
        if (vkCreateBuffer(ctx.device, &si, NULL, &nb) != VK_SUCCESS) return false;
        VkMemoryRequirements smr;
        vkGetBufferMemoryRequirements(ctx.device, nb, &smr);
        GpuAllocation na = gpu_alloc(&gpuAllocator, smr,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (na.memory == VK_NULL_HANDLE) { vkDestroyBuffer(ctx.device, nb, NULL); return false; }
        vkBindBufferMemory(ctx.device, nb, na.memory, na.offset);
        if (b->staged[i] && b->stagingMapped[i] && na.mapped)
            memcpy(na.mapped, b->stagingMapped[i], (size_t)b->staged[i] * b->stride);
        vkDestroyBuffer(ctx.device, b->staging[i], NULL);
        b->staging[i]       = nb;
        b->stagingAllocs[i] = na;
        b->stagingMapped[i] = na.mapped;
        VkBufferCopy* nr = (VkBufferCopy*)realloc(b->regions[i], (size_t)newCap * sizeof(VkBufferCopy));
        if (!nr) return false;
        b->regions[i] = nr;
    }
    b->stagingCap = newCap;
    return true;
}

// Queues element `index` <- value into frame f's delta staging, growing staging on overflow.
// Best-effort: a host-OOM growth silently drops the write (matches the drop-on-OOM policy
// elsewhere in the apply path).
static void slot_upload_stage(SlotUpload* b, uint32_t f, uint32_t index, const void* value)
{
    if (b->staged[f] >= b->stagingCap) {
        vkDeviceWaitIdle(ctx.device);
        if (!slot_upload_grow_staging(b, b->staged[f] + 1u)) return;
    }
    uint32_t s = b->staged[f];
    memcpy((char*)b->stagingMapped[f] + (size_t)s * b->stride, value, b->stride);
    b->regions[f][s] = (VkBufferCopy){
        .srcOffset = (VkDeviceSize)s * b->stride,
        .dstOffset = (VkDeviceSize)index * b->stride,
        .size = b->stride,
    };
    b->staged[f] = s + 1u;
}

// Records frame f's queued copies (staging[f] -> device) into cmd, then clears the queue.
// Caller brackets all SlotUploads' flushes with one read->transfer / transfer->read barrier.
static void slot_upload_flush(VkCommandBuffer cmd, SlotUpload* b, uint32_t f)
{
    if (b->staged[f] == 0u) return;
    vkCmdCopyBuffer(cmd, b->staging[f], b->device, b->staged[f], b->regions[f]);
    b->staged[f] = 0u;
}

// Grows the device buffer to newCap elements, preserving [0, keep) via a one-shot GPU copy.
// Caller holds vkDeviceWaitIdle. Old handle destroyed; arena memory not reclaimed (bump arena),
// bounded by geometric growth like growBufferSet.
static bool slot_upload_grow_device(SlotUpload* b, uint32_t newCap, uint32_t keep)
{
    VkBufferCreateInfo di = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)b->stride * newCap,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer nb = VK_NULL_HANDLE;
    if (vkCreateBuffer(ctx.device, &di, NULL, &nb) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, nb, &mr);
    GpuAllocation na = gpu_alloc(&gpuAllocator, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (na.memory == VK_NULL_HANDLE) { vkDestroyBuffer(ctx.device, nb, NULL); return false; }
    vkBindBufferMemory(ctx.device, nb, na.memory, na.offset);
    if (keep) {
        VkCommandBuffer c = beginSingleTimeCommands(&ctx);
        VkBufferCopy region = { .srcOffset = 0, .dstOffset = 0, .size = (VkDeviceSize)b->stride * keep };
        vkCmdCopyBuffer(c, b->device, nb, 1, &region);
        endSingleTimeCommands(&ctx, c);
    }
    vkDestroyBuffer(ctx.device, b->device, NULL);
    b->device      = nb;
    b->deviceAlloc = na;
    b->capacity    = newCap;
    return true;
}

// Ensures the slot-indexed GPU buffers can hold at least `required` slots, growing
// them (and the slot table's ceiling) if not. No-op when already large enough.
//
// Growth recreates every entity-scaled buffer larger and re-points all descriptor
// sets at the new handles. Both are only legal while no in-flight frame references
// the old buffers/descriptors, so a full vkDeviceWaitIdle precedes the work. Growth
// fires only when a spawn crosses a chunk boundary (rare), so the stall is fine.
//
// Persistent slot data (initialTransform, motion, instanceData, entity mesh/material) is a
// ×1 device-local SlotUpload, grown with a GPU copy of the live span; transform /
// compactedIndices / indirect are GPU-regenerated each frame, so they are resized without a copy.
//
// in:  state, required (slot count needed), frameIndex (frame being recorded)
// out: true if capacity >= required afterward; false on OOM (caller drops the spawn)
static bool ensureEntityCapacity(RendererState* state, uint32_t required, uint32_t frameIndex)
{
    uint32_t oldCap = state->slots.slotCapacity;
    if (required <= oldCap) return true;

    uint32_t newCap = oldCap ? oldCap * 2u : ENTITY_GROWTH_CHUNK;
    if (newCap < required) newCap = required;
    newCap = ((newCap + ENTITY_GROWTH_CHUNK - 1u) / ENTITY_GROWTH_CHUNK) * ENTITY_GROWTH_CHUNK;
    if (newCap < required) { // round-up overflow
        printf("Fatal: entity capacity request %u exceeds addressable range.\n", required);
        return false;
    }

    vkDeviceWaitIdle(ctx.device);

    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags devProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    bool ok =
        // ×1 device-local authoritative per-slot data: GPU-copy the live [0,oldCap) span forward
        slot_upload_grow_device(&state->initialTransformBuffer, newCap, oldCap) &&
        slot_upload_grow_device(&state->motionBuffer, newCap, oldCap) &&
        slot_upload_grow_device(&state->instanceDataBuffer, newCap, oldCap) &&
        slot_upload_grow_device(&state->culling.entity, newCap, oldCap) &&
        // GPU-private, regenerated each frame: DEVICE_LOCAL, resize only (no copy)
        growBufferSet(state->transformBuffer.buffer, state->transformBuffer.allocs, ssbo, devProps,
                      (VkDeviceSize)sizeof(mat4) * newCap, 0) &&
        growBufferSet(state->culling.compactedEntityIndicesBuffer, state->culling.compactedEntityIndicesAllocs, ssbo, devProps,
                      (VkDeviceSize)sizeof(uint32_t) * newCap * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT, 0) &&
        growBufferSet(state->indirectBuffer.buffer, state->indirectBuffer.allocs,
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, devProps,
                      cmdStride * newCap * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT, 0);
    if (!ok) {
        printf("Fatal: entity capacity growth %u -> %u failed (GPU out of memory?).\n", oldCap, newCap);
        return false;
    }

    // Re-derive mapped pointers for the GPU-private buffers (NULL now they are DEVICE_LOCAL;
    // never CPU-dereferenced). The four SlotUpload device buffers + their capacities were
    // updated inside slot_upload_grow_device above; nothing to re-point here.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->transformBuffer.mapped[i]               = (mat4*)state->transformBuffer.allocs[i].mapped;
        state->culling.compactedEntityIndicesMapped[i] = (uint32_t*)state->culling.compactedEntityIndicesAllocs[i].mapped;
        state->indirectBuffer.mapped[i]                = (VkDrawMeshTasksIndirectCommandEXT*)state->indirectBuffer.allocs[i].mapped;
    }
    state->transformBuffer.capacity = newCap;
    state->indirectBuffer.capacity  = newCap;
    state->culling.maxEntities      = newCap;
    render_slots_set_capacity(&state->slots, newCap);

    // updateCullingBuffers already wrote this frame's CullUBO with the OLD
    // maxEntities; the compacted-index partition stride (cull.comp) and the
    // draw-time base offset both key off it, so realign it to newCap now or the
    // growth frame reads pipeline-type partitions at the wrong stride.
    state->culling.ubo.mapped[frameIndex]->maxEntities = newCap;

    // Re-point every descriptor at the new handles/ranges. Safe only because the
    // device is idle. Before the descriptor sets exist (init-time growth) this is
    // skipped; the init updateUboDescriptorSets call then binds the final buffers.
    if (state->frames[0].views[0].globalSet != VK_NULL_HANDLE)
        updateUboDescriptorSets(&ctx, state);

    printf("Entity capacity grown: %u -> %u slots.\n", oldCap, newCap);
    return true;
}

// Stages the held streamed slice into this frame's scatter lane. The transform payload is
// NOT copied — scatter reads the producer-written ring slice directly via dynamic offset;
// only the cheap render_id -> slot resolve lands in slotMapped, and only when resolveGen
// moved (a new publish or a slot retirement). Otherwise the prior resolution, count,
// dynamic offset and frameSeq still hold, so a frame with no fresh publish re-binds the
// same slice for free (hold-last-value). curCount == 0 yields count 0 and scatter
// self-skips. frameSeq[frameIndex] records the seq this frame submits, for the reclaim.
// in:  state, frameIndex
// out: slotMapped[frameIndex], count[frameIndex], dynOffset[frameIndex], frameSeq[frameIndex]
static void stage_stream_frame(RendererState* state, uint32_t frameIndex)
{
    TransformStreamBuffer* ts = &state->transformStream;
    if (ts->stagedGen[frameIndex] == ts->resolveGen)
        return; // slot/count/offset/seq for this frame already current
    ts->stagedGen[frameIndex] = ts->resolveGen;
    ts->frameSeq[frameIndex]  = ts->curSeq;

    if (ts->curCount == 0) { ts->count[frameIndex] = 0; return; }

    uint32_t slice = (uint32_t)((ts->curSeq - 1u) % ts->ringSlices);
    const uint32_t* ids = ts->idRing + (size_t)slice * ts->capacity;
    uint32_t* slots = ts->slotMapped[frameIndex];
    for (uint32_t i = 0; i < ts->curCount; i++) {
        uint32_t slot = render_slots_resolve(&state->slots, ids[i]);
        slots[i] = (slot == ANO_RENDER_SLOT_UNMAPPED) ? STREAM_SLOT_SKIP : slot;
    }
    ts->count[frameIndex]     = ts->curCount;
    ts->dynOffset[frameIndex] = (uint32_t)((VkDeviceSize)slice * ts->sliceStride);
}

// Releases a bulk command's render-owned batch block (the single mi-heap allocation the
// bulk submit helpers pack the struct + arrays into). No-op for non-owned commands (e.g.
// init's renderHeap-resident BULK_CREATE). Render-thread only.
static void free_owned_bulk(const RenderCommand* c)
{
    if (!c->bulk_owned) return;
    void* blk = c->kind == RCMD_BULK_UPDATE  ? (void*)c->update
              : c->kind == RCMD_BULK_DESTROY ? (void*)c->destroy
              :                                (void*)c->batch;
    if (blk) mi_free(blk);
}

static void render_apply_commands(RendererState* state, uint32_t frameIndex)
{
    // Drain the bridge and stage each command's changed per-slot fields into THIS frame's
    // delta staging (uploaded to the shared DEVICE_LOCAL buffers by recordCommandBuffer).
    // There is no cross-frame propagation list anymore: the authoritative per-slot buffers
    // are single copies shared by all frames, so one upload suffices. A DESTROY dead-marks
    // its slot and retires it immediately; the safeFrame = globalFrame + framesInFlight
    // quarantine then keeps the slot out of reuse until every frame that could still read the
    // old occupant has drained (see docs/artifacts/DEVICE_LOCAL_SLOTS.md).
    RenderCommand cmd;
    while (ano_render_next_command(&state->bridge, &cmd)) {
        switch (cmd.kind) {
        case RCMD_STREAM_TRANSFORMS:
            // Adopt the published slice as the held snapshot; bump resolveGen so every frame
            // re-resolves it. The mapped writes preceded the producer's submit, so the slice
            // contents are visible after this drain's acquire.
            state->transformStream.curSeq   = cmd.stream_seq;
            state->transformStream.curCount = cmd.stream_count;
            state->transformStream.resolveGen++;
            break;

        case RCMD_CREATE: {
            // Grow if no recycled hole is available and the high-water is at the ceiling.
            if (state->slots.freeCount == 0u && state->slots.slotHighWater >= state->slots.slotCapacity &&
                !ensureEntityCapacity(state, state->slots.slotHighWater + 1u, frameIndex))
                break; // growth failed: drop the spawn
            uint32_t slot = render_slots_alloc(&state->slots, cmd.render_id);
            if (slot == ANO_RENDER_SLOT_UNMAPPED) break; // unexpected: drop rather than corrupt
            stage_command_fields(state, &cmd, slot, frameIndex);
            break;
        }

        case RCMD_UPDATE: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED)
                stage_command_fields(state, &cmd, slot, frameIndex);
            break;
        }

        case RCMD_DESTROY: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex); // dead-mark
                render_slots_retire(&state->slots, cmd.render_id, state->globalFrame);
            }
            break;
        }

        case RCMD_BULK_CREATE: {
            const RenderCreateBatch* b = cmd.batch;
            if (!b) break;
            // alloc_range needs a contiguous run from the high-water mark.
            if (!ensureEntityCapacity(state, state->slots.slotHighWater + b->count, frameIndex)) {
                free_owned_bulk(&cmd); break; // growth failed: drop the batch
            }
            render_slots_alloc_range(&state->slots, b->render_ids, b->count);
            AnoInstanceData inert = {0};
            for (uint32_t e = 0; e < b->count; e++) {
                uint32_t slot = render_slots_resolve(&state->slots, b->render_ids[e]);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                slot_upload_stage(&state->initialTransformBuffer, frameIndex, slot, &b->transforms[e]);
                slot_upload_stage(&state->motionBuffer, frameIndex, slot, &b->motion[e]);
                // Batch carries no instance data; clear it so a recycled slot drops the prior
                // occupant's tint/flags and renders inert.
                slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &inert);
                uint32_t ent[2] = { b->mesh[e], b->material[e] };
                slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
            }
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_BULK_UPDATE: {
            // Apply the shared field mask to each resolvable target (unresolved ids dropped).
            const RenderUpdateBatch* u = cmd.update;
            if (!u) break;
            for (uint32_t e = 0; e < u->count; e++) {
                uint32_t slot = render_slots_resolve(&state->slots, u->render_ids[e]);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                if (u->fields & RFIELD_TRANSFORM)
                    slot_upload_stage(&state->initialTransformBuffer, frameIndex, slot, &u->transforms[e]);
                if (u->fields & RFIELD_ANIM)
                    slot_upload_stage(&state->motionBuffer, frameIndex, slot, &u->motion[e]);
                if (u->fields & RFIELD_USERDATA)
                    slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &u->instance_data[e]);
                if (u->fields & RFIELD_MESH_MAT) {
                    uint32_t ent[2] = { u->mesh[e], u->material[e] };
                    slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
                }
            }
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_BULK_DESTROY: {
            const RenderDestroyBatch* d = cmd.destroy;
            if (!d) break;
            uint32_t dead[2] = { NO_MESH_INDEX, 0u };
            for (uint32_t e = 0; e < d->count; e++) {
                uint32_t rid  = d->render_ids[e];
                uint32_t slot = render_slots_resolve(&state->slots, rid);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                slot_upload_stage(&state->culling.entity, frameIndex, slot, dead);
                render_slots_retire(&state->slots, rid, state->globalFrame);
            }
            free_owned_bulk(&cmd);
            break;
        }

        default:
            break;
        }
    }

    // Free + report slots whose quarantine has elapsed (every referencing frame retired).
    uint32_t retired[64];
    uint32_t n;
    bool anyRetired = false;
    do {
        n = render_slots_collect_retired(&state->slots, state->globalFrame, retired, 64u);
        if (n) anyRetired = true;
        for (uint32_t i = 0; i < n; i++) {
            RenderEvent ev = { .kind = REVENT_SLOT_RETIRED, .render_id = retired[i] };
            (void)ano_render_emit_event(&state->bridge, &ev);
        }
    } while (n == 64u);
    if (anyRetired) {
        state->transformStream.resolveGen++; // a freed/recycled slot invalidates cached resolves

        // Reclaim the per-frame compute-dispatch bound: if this frame's retirements left a
        // trailing run of free slots at the top, drop slotHighWater past them so cull/update
        // stop dispatching over dead tail slots. Only fires when the free-list just changed
        // (alloc never lowers the high-water). No live slot moves; no VRAM is returned (the
        // buffers stay grown — see SLOT_RECLAIM.md for the deferred per-region VRAM path).
        render_slots_compact(&state->slots);
    }

    // Stage the held streamed slice into this frame against the now-settled slot map.
    // Re-resolves only on a resolveGen bump (new publish or retirement); otherwise it
    // re-binds the same slice for free, so a streamed pose holds across ticks.
    stage_stream_frame(state, frameIndex);
}

#include "anoptic_time.h"

// ---------------------------------------------------------------------------
// Render world ownership. GLFW pins all window and event handling to the process
// main thread (a hard requirement on macOS/Cocoa: glfwInit, glfwCreateWindow,
// glfwPollEvents and surface creation must issue from the thread that runs
// main()). So the render world — all of Vulkan AND GLFW: init, the frame loop,
// glfwPollEvents, swapchain recreation, teardown — runs directly on the main
// thread (see main()). The logic/ECS master runs on its OWN thread now and is the
// sole render-command PRODUCER, coordinating only through the lock-free bridge
// (engine policy: no mutexes outside the pre-existing Vulkan internals).
//
// Init is synchronous: main() calls initVulkan() (which creates the bridge)
// BEFORE spawning the logic thread, so no readiness handshake is needed. Shutdown
// is ordered so the producer always quiesces BEFORE the bridge dies: the render
// loop exits on window close, main stops the logic thread and joins it, and only
// then runs unInitVulkan() to destroy the bridge.
// ---------------------------------------------------------------------------

// Producer endpoint. Valid once initVulkan() has returned (the bridge is created
// there and destroyed in unInitVulkan).
AnoRenderBridge* anoRenderBridge(void) { return &rendererState.bridge; }

// render_id 0's original geometry index, for the stand-in producer. Safe to read
// after init: the render loop does not mutate entities[] after init.
uint32_t anoRenderEntity0Mesh(void) {
    return rendererState.entities ? rendererState.entities[0].meshIndex : NO_MESH_INDEX;
}

// Copies render_id's seeded base pose — the initialTransform update.comp derives the
// live transform from, with the scene's nudge already folded in — into `out`. Stand-in
// stream-producer helper so a fabricated stream stays in the same world space as the
// normal path (a bare identity would teleport/mirror the entity). Valid after init; the
// seeded base is not mutated for static entities. Returns false (out untouched) if the
// id is unmapped.
// in:  render_id; out: mat4 out
bool anoRenderEntityBaseTransform(uint32_t render_id, mat4 out) {
    // initialTransform is DEVICE_LOCAL now (no host readback); the host-side entities[] holds
    // the seeded base pose with the init spawn nudge folded in. render_id == entity index for
    // the init scene (identity slot range), which is all the stand-in producer needs.
    if (!rendererState.entities || render_id >= rendererState.entityCount) return false;
    memcpy(out, &rendererState.entities[render_id].transform, sizeof(mat4));
    return true;
}

// Producer endpoint — reserve the next free transform-ring slice. Returns false (out
// untouched) when that slice is still in flight on the GPU (reclaimSeq has not caught
// up): the caller drops the tick and the render side holds the last published slice.
// Single-producer; does NOT advance produceSeq (commit does), so repeated begins without
// a commit return the same slice.
// in:  out (AnoStreamRegion*); out: filled on success; false if no free slice
bool ano_render_stream_begin(AnoStreamRegion* out) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    uint64_t seq = ts->produceSeq + 1u;
    if (seq > ts->ringSlices) {
        uint64_t prior = seq - ts->ringSlices; // seq that last used this slice
        if (atomic_load_explicit(&ts->reclaimSeq, memory_order_acquire) < prior)
            return false; // slice not yet GPU-reclaimed
    }
    uint32_t slice = (uint32_t)((seq - 1u) % ts->ringSlices);
    out->ids      = ts->idRing + (size_t)slice * ts->capacity;
    out->xforms   = ts->xformRingMapped + (size_t)slice * ts->capacity;
    out->capacity = ts->capacity;
    out->token    = seq;
    return true;
}

// Producer endpoint — publish a filled region as one {seq,count} control command. The
// region's mapped writes precede this submit's release, so the render consumer sees them
// after its acquire and the queue submit makes them GPU-visible (coherent memory).
// Advances produceSeq only on a successful enqueue. false if the command ring is full
// (slice left unpublished; it is reclaimed normally on the next cycle).
// in:  region, count (clamped to capacity); out: true on enqueue
bool ano_render_stream_commit(const AnoStreamRegion* region, uint32_t count) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    if (count > ts->capacity) count = ts->capacity;
    RenderCommand cmd = { .kind = RCMD_STREAM_TRANSFORMS,
                          .stream_seq = region->token, .stream_count = count };
    if (!ano_render_submit(&rendererState.bridge, &cmd))
        return false;
    ts->produceSeq = region->token;
    return true;
}

// Producer endpoint — mass field change. Packs the batch + every flagged field array into
// ONE render-owned block (mi heap, freed render-side after the change reaches all frames),
// so the caller's arrays need only live until this returns. Backpressure: ring full ->
// free the copy and return false (retry next tick), never drop.
// in:  bridge, batch (count, shared fields mask, parallel arrays); out: true on enqueue
bool ano_render_submit_bulk_update(AnoRenderBridge* bridge, const RenderUpdateBatch* batch) {
    if (!batch || batch->count == 0) return true;
    uint32_t count = batch->count, fields = batch->fields;
    size_t bytes = sizeof(RenderUpdateBatch) + (size_t)count * sizeof(uint32_t); // struct + ids
    if (fields & RFIELD_TRANSFORM) bytes += (size_t)count * sizeof(mat4);
    if (fields & RFIELD_ANIM)      bytes += (size_t)count * sizeof(AnoMotionDescriptor);
    if (fields & RFIELD_MESH_MAT)  bytes += (size_t)count * sizeof(uint32_t) * 2u;
    if (fields & RFIELD_USERDATA)  bytes += (size_t)count * sizeof(AnoInstanceData);
    char* blk = mi_malloc(bytes);
    if (!blk) return false;
    RenderUpdateBatch* b = (RenderUpdateBatch*)blk;
    *b = (RenderUpdateBatch){ .count = count, .fields = fields };
    char* cur = blk + sizeof(RenderUpdateBatch);
    b->render_ids = (uint32_t*)cur;
    memcpy(cur, batch->render_ids, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    if (fields & RFIELD_TRANSFORM) {
        b->transforms = (mat4*)cur;
        memcpy(cur, batch->transforms, (size_t)count * sizeof(mat4)); cur += (size_t)count * sizeof(mat4);
    }
    if (fields & RFIELD_ANIM) {
        b->motion = (AnoMotionDescriptor*)cur;
        memcpy(cur, batch->motion, (size_t)count * sizeof(AnoMotionDescriptor)); cur += (size_t)count * sizeof(AnoMotionDescriptor);
    }
    if (fields & RFIELD_MESH_MAT) {
        b->mesh = (uint32_t*)cur;
        memcpy(cur, batch->mesh, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
        b->material = (uint32_t*)cur;
        memcpy(cur, batch->material, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    }
    if (fields & RFIELD_USERDATA) {
        b->instance_data = (AnoInstanceData*)cur;
        memcpy(cur, batch->instance_data, (size_t)count * sizeof(AnoInstanceData)); cur += (size_t)count * sizeof(AnoInstanceData);
    }
    RenderCommand cmd = { .kind = RCMD_BULK_UPDATE, .update = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) { mi_free(blk); return false; }
    return true;
}

// Producer endpoint — mass despawn. Copies the render_id array into one render-owned block
// (freed render-side after the dead-mark reaches all frames). Same backpressure contract.
// in:  bridge, render_ids, count; out: true on enqueue
bool ano_render_submit_bulk_destroy(AnoRenderBridge* bridge, const uint32_t* render_ids, uint32_t count) {
    if (count == 0) return true;
    size_t bytes = sizeof(RenderDestroyBatch) + (size_t)count * sizeof(uint32_t);
    char* blk = mi_malloc(bytes);
    if (!blk) return false;
    RenderDestroyBatch* b = (RenderDestroyBatch*)blk;
    uint32_t* ids = (uint32_t*)(blk + sizeof(RenderDestroyBatch));
    memcpy(ids, render_ids, (size_t)count * sizeof(uint32_t));
    b->count = count;
    b->render_ids = ids;
    RenderCommand cmd = { .kind = RCMD_BULK_DESTROY, .destroy = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) { mi_free(blk); return false; }
    return true;
}

// Lighting-mode control (RADIANCE_CASCADES.md). Stored on the render state and published into the
// GlobalUBO tail by updateCullingBuffers; takes effect from the next recorded frame. Mutated only
// from the render thread (frame record + L-key callback are both main-thread), so no atomics.
void ano_render_set_lighting_mode(AnoLightingMode mode) {
    if ((uint32_t)mode >= (uint32_t)ANO_LIGHTING_MODE_COUNT) return;
    if (rendererState.lightingMode != (uint32_t)mode) {
        rendererState.lightingMode = (uint32_t)mode;
        // Discard the in-progress timing window so the next printed average is pure for the new mode.
        for (int r = 0; r < ANO_TS_COUNT - 1; r++) g_tsAccumMs[r] = 0.0;
        g_tsFrames = 0;
    }
}

AnoLightingMode ano_render_get_lighting_mode(void) {
    return (AnoLightingMode)rendererState.lightingMode;
}

// Print the averaged per-pass GPU times + per-allocator resident VRAM for the active lighting mode
// (RADIANCE_CASCADES.md §8). shadowAtlas is the always-resident D32 atlas (ANO_SHADOW_FRUSTUM_COUNT
// layers x ANO_SHADOW_DIM^2 x 4 B x MAX_FRAMES_IN_FLIGHT), reported separately so RC-only VRAM is
// not charged for the idle-but-resident atlas — the fairness break-out the harness requires.
static void ano_print_profiling(void) {
    static const char* const modeNames[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RC" };
    uint32_t m = rendererState.lightingMode;
    const char* mn = (m < (uint32_t)ANO_LIGHTING_MODE_COUNT) ? modeNames[m] : "?";
    double inv = g_tsFrames ? 1.0 / (double)g_tsFrames : 0.0;
    double up   = g_tsAccumMs[ANO_TS_FRAME_BEGIN]   * inv; // FRAME_BEGIN -> AFTER_UPLOAD
    double cp   = g_tsAccumMs[ANO_TS_AFTER_UPLOAD]  * inv; // -> AFTER_COMPUTE
    double rcms = g_tsAccumMs[ANO_TS_AFTER_COMPUTE] * inv; // -> AFTER_RC
    double sh   = g_tsAccumMs[ANO_TS_AFTER_RC]      * inv; // -> AFTER_SHADOW
    double li   = g_tsAccumMs[ANO_TS_AFTER_SHADOW]  * inv; // -> AFTER_LIGHTING
    double co   = g_tsAccumMs[ANO_TS_AFTER_LIGHTING]* inv; // -> AFTER_COMPOSITE
    double total = up + cp + rcms + sh + li + co;

    const double MiB = 1024.0 * 1024.0;
    double gpu  = (double)allocator_used_bytes(&gpuAllocator)       / MiB;
    double tex  = (double)allocator_used_bytes(&textureAllocator)   / MiB;
    double swap = (double)allocator_used_bytes(&swapchainAllocator) / MiB;
    double stg  = (double)allocator_used_bytes(&stagingAllocator)   / MiB;
    double rc   = (double)allocator_used_bytes(&rcAllocator)        / MiB;
    double atlas = (double)((VkDeviceSize)ANO_SHADOW_FRUSTUM_COUNT * ANO_SHADOW_DIM * ANO_SHADOW_DIM
                            * 4u * MAX_FRAMES_IN_FLIGHT) / MiB;

    printf("[profile mode=%s] GPU ms: upload=%.3f compute=%.3f rc=%.3f shadow=%.3f lighting=%.3f composite=%.3f total=%.3f"
           " | VRAM MiB: gpu=%.1f tex=%.1f swap=%.1f staging=%.1f rc=%.1f | shadowAtlas(resident)=%.1f\n",
           mn, up, cp, rcms, sh, li, co, total, gpu, tex, swap, stg, rc, atlas);
}

// Read this frame slot's timestamps (its prior submission is fence-complete) and fold the per-pass
// deltas into the running average; print every ANO_PROFILE_PRINT_INTERVAL frames. Masks to the
// queue's valid bits and handles counter wraparound. No-op when timestamps are unsupported.
static void ano_collect_frame_stats(uint32_t frameIndex) {
    if (!rendererState.timestampValidBits) return;
    VkQueryPool pool = rendererState.frames[frameIndex].timestampPool;
    if (pool == VK_NULL_HANDLE) return;
    uint64_t ts[ANO_TS_COUNT];
    if (vkGetQueryPoolResults(ctx.device, pool, 0, ANO_TS_COUNT, sizeof(ts), ts,
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    uint64_t mask = (rendererState.timestampValidBits >= 64) ? ~0ull : ((1ull << rendererState.timestampValidBits) - 1ull);
    for (int r = 0; r < ANO_TS_COUNT - 1; r++) {
        uint64_t a = ts[r] & mask, b = ts[r + 1] & mask;
        uint64_t d = (b >= a) ? (b - a) : (mask - a + b + 1u); // wrap-safe
        g_tsAccumMs[r] += (double)d * (double)rendererState.timestampPeriodNs * 1e-6;
    }
    if (++g_tsFrames >= ANO_PROFILE_PRINT_INTERVAL) {
        ano_print_profiling();
        for (int r = 0; r < ANO_TS_COUNT - 1; r++) g_tsAccumMs[r] = 0.0;
        g_tsFrames = 0;
    }
}

void drawFrame()
{
	if (rendererState.framebufferResized)
	{
		rendererState.framebufferResized = false;
		recreateSwapChain(&ctx, window);
		return;
	}

    // Discrete ECS->render transitions arrive via the bridge from the logic
    // thread; they are drained in render_apply_commands (below). No producer runs
    // on this thread anymore.

    if (rendererState.frames[rendererState.frameIndex].frameSubmitted == true)
    {
        vkWaitForFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence), VK_TRUE, UINT64_MAX);

        // This slot's prior submission is complete: its per-pass timestamps are ready to read.
        ano_collect_frame_stats(rendererState.frameIndex);

        // Reclaim streamed-transform ring slices the GPU is finished reading. Hold-last-
        // value means several in-flight frames can share one seq (one slice), so this
        // slot completing does NOT free its slice — a sibling may still read it. The safe
        // bound is the OLDEST still-in-flight frame's seq: after we wait slot frameIndex,
        // that is slot (frameIndex+1) % N (the next to be reused). Everything strictly
        // below its seq is unreferenced.
        uint32_t oldest = (rendererState.frameIndex + 1u) % MAX_FRAMES_IN_FLIGHT;
        uint64_t qmin = rendererState.transformStream.frameSeq[oldest];
        atomic_store_explicit(&rendererState.transformStream.reclaimSeq,
                              qmin ? qmin - 1u : 0u, memory_order_release);
    }

    // Process any deferred deletions that were waiting for this frame's previous commands to finish
    flush_deletion_queue(&ctx, &rendererState, rendererState.frameIndex);
    
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(ctx.device, rendererState.swapChain, UINT64_MAX, rendererState.frames[rendererState.frameIndex].imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) 
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) 
	{
		printf("Failed to acquire swap chain image!\n");
		return;
	}

	updateUniformBuffer(&ctx, &rendererState);

	// Update entity transforms
	// float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
	// for (uint32_t i = 0; i < rendererState.entityCount && i < 3; i++) {
		//updateMeshTransforms(&ctx, &rendererState.entities[i], moveOffsets[i]);
	// }

	updateTransformBuffer(&ctx, &rendererState, rendererState.frameIndex);
	updateCullingBuffers(&ctx, &rendererState, rendererState.frameIndex);

	// Ingest discrete ECS->render state transitions for this frame slot.
	render_apply_commands(&rendererState, rendererState.frameIndex);

	vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].commandBuffer, 0);
	recordCommandBuffer(imageIndex);

	//updateUniformBuffer(&ctx, &rendererState);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = {rendererState.frames[rendererState.frameIndex].imageAvailable};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].commandBuffer);
	VkSemaphore signalSemaphores[] = {rendererState.frames[rendererState.frameIndex].renderFinished};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence)); // this goes here because multi-threading
	if (vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, rendererState.frames[rendererState.frameIndex].frameFence) != VK_SUCCESS) 
	{
		printf("Failed to submit draw command buffer!\n");
		return;
	}

    // Presentation should happen *before* submitting commands for a new frame, so we're actually taking advantage of buffering

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = {rendererState.swapChain};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = NULL; // Optional

	VkResult presentResult = vkQueuePresentKHR(ctx.presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (presentResult != VK_SUCCESS)
	{
		printf("Failed to present swap chain image!\n");
		return;
	}

	rendererState.frames[rendererState.frameIndex].frameSubmitted = true;

	//printUniformTransferState();

	rendererState.frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (rendererState.frameIndex == MAX_FRAMES_IN_FLIGHT)
	{
		rendererState.frameIndex = 0;
	}
	rendererState.globalFrame += 1; // monotonic; gates slot-quarantine retirement
}

//Init and cleanup functions

bool createMaterialBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->materialBuffer.capacity = maxEntities;
    state->materialBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(MaterialData) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->materialBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create material buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, state->materialBuffer.buffer[i], &memRequirements);
        
        state->materialBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->materialBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->materialBuffer.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->materialBuffer.buffer[i], state->materialBuffer.allocs[i].memory, state->materialBuffer.allocs[i].offset);
        
        state->materialBuffer.mapped[i] = (MaterialData*)state->materialBuffer.allocs[i].mapped;
    }
    return true;
}

bool createLightBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxLights) {
    (void)ctx;
    // Light palette: ×1 device-local + delta staging (count tracks live lights; addLightEntity
    // and RFIELD_LIGHT commands stage into it). slot_upload_create zeroes count.
    return slot_upload_create(&state->lightBuffer, maxLights, sizeof(LightData), SLOT_STAGING_INIT);
}

// Appends a transform-only light entity (no geometry) to the scene and registers
// its LightData in the light buffer. `light` supplies the photometric parameters;
// this fills in transformIndex (the new entity) and marks it enabled, then writes
// the entry into every frame's light buffer. Must be called before the per-frame
// transform upload loop so the light's transform reaches the transform buffers.
// Returns the new entity index.
static uint32_t addLightEntity(LightData light, mat4 transform) {
    uint32_t entIdx = rendererState.entityCount;
    rendererState.entityCount += 1;
    rendererState.entities = realloc(rendererState.entities, rendererState.entityCount * sizeof(RenderEntity));

    uint32_t lightIdx = rendererState.lightBuffer.count++;

    rendererState.entities[entIdx].meshIndex = NO_MESH_INDEX;   // skipped by culling -> draws nothing
    rendererState.entities[entIdx].materialIndex = 0;
    rendererState.entities[entIdx].lightIndex = lightIdx;
    memcpy(&rendererState.entities[entIdx].transform, transform, sizeof(mat4));

    light.transformIndex = entIdx;
    light.enabled = 1;
    // Stage into frame 0's light delta; init's one-shot flush uploads it to the device buffer
    // (shared by all frames in flight). Called during scene setup, before the first draw.
    slot_upload_stage(&rendererState.lightBuffer, 0, lightIdx, &light);

    // Register a shadow caster for this light, within its type's budget (ANO_SHADOW_*_COUNT). Point
    // lights claim 6 contiguous cube-face frustums, dir/spot one each. The frustum block lays out
    // cfg[base..] and the light's info points at it; shadowsetup builds each, the fragment samples.
    uint32_t budget = light.type == LIGHT_TYPE_DIRECTIONAL ? ANO_SHADOW_DIR_COUNT
                    : light.type == LIGHT_TYPE_POINT       ? ANO_SHADOW_POINT_COUNT
                                                           : ANO_SHADOW_SPOT_COUNT;
    uint32_t blockSize = light.type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
    if (rendererState.shadowTypeUsed[light.type] < budget &&
        rendererState.shadowFrustumNext + blockSize <= ANO_SHADOW_FRUSTUM_COUNT) {
        uint32_t base = rendererState.shadowFrustumNext;
        ShadowFrustumConfig* cfg = (ShadowFrustumConfig*)rendererState.shadowFrustumConfigMapped;
        ShadowLightInfo* info = (ShadowLightInfo*)rendererState.shadowLightInfoMapped;
        for (uint32_t f = 0; f < blockSize; f++)
            cfg[base + f] = (ShadowFrustumConfig){ .lightIndex = lightIdx, .lightType = light.type,
                .faceIndex = (light.type == LIGHT_TYPE_POINT ? f : 0u), .pad = 0u };
        info[lightIdx] = (ShadowLightInfo){ .castsShadow = 1u, .baseFrustum = base,
            .frustumCount = blockSize, .pad = 0u };
        rendererState.shadowFrustumNext += blockSize;
        rendererState.shadowTypeUsed[light.type] += 1u;
    }
    return entIdx;
}

bool createMotionBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    (void)ctx;
    // ×1 device-local + delta staging. Fresh slots are written by their CREATE before being
    // read (a slot is < slotHighWater only after allocation), so no host-side zero-fill is needed.
    return slot_upload_create(&state->motionBuffer, maxEntities, sizeof(AnoMotionDescriptor), SLOT_STAGING_INIT);
}

// Slot-indexed per-entity instance channel (tint/flags/scalars). ×1 device-local + delta
// staging; a CREATE always writes the slot (inert {0} for a recycled hole) before it is read.
// in:  ctx, state, maxEntities (initial slot count)
// out: true on success; false on buffer/alloc failure
bool createInstanceDataBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    (void)ctx;
    return slot_upload_create(&state->instanceDataBuffer, maxEntities, sizeof(AnoInstanceData), SLOT_STAGING_INIT);
}

// Creates one host-visible storage buffer per frame and writes its handle/alloc/mapped
// pointer back through the out params. Helper for the streamed-transform lane.
static bool createMappedSsboSet(VulkanContext* ctx, VkDeviceSize bytes,
                                VkBuffer outBufs[MAX_FRAMES_IN_FLIGHT],
                                GpuAllocation outAllocs[MAX_FRAMES_IN_FLIGHT],
                                void* outMapped[MAX_FRAMES_IN_FLIGHT]) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bytes, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(ctx->device, &bi, NULL, &outBufs[i]) != VK_SUCCESS) return false;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(ctx->device, outBufs[i], &mr);
        outAllocs[i] = gpu_alloc(&gpuAllocator, mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (outAllocs[i].memory == VK_NULL_HANDLE) { vkDestroyBuffer(ctx->device, outBufs[i], NULL); return false; }
        vkBindBufferMemory(ctx->device, outBufs[i], outAllocs[i].memory, outAllocs[i].offset);
        outMapped[i] = outAllocs[i].mapped;
    }
    return true;
}

// Streamed-transform lane (Path B v2 — zero-copy mapped ring). Per-frame resolved-slot
// buffers (render-written, scatter binding 0) plus ONE producer-written transform ring
// of ringSlices slices (scatter binding 1 via dynamic offset). The parallel render_id
// ring (idRing) is CPU-only and allocated from renderHeap in initVulkan once it exists.
// in:  ctx, state, capacity (STREAM_CAPACITY)
// out: true on success; false on buffer/alloc failure
bool createStreamBuffers(VulkanContext* ctx, RendererState* state, uint32_t capacity) {
    TransformStreamBuffer* ts = &state->transformStream;
    ts->capacity    = capacity;
    ts->ringSlices  = (uint32_t)MAX_FRAMES_IN_FLIGHT + 2u;   // headroom over frames in flight
    ts->sliceStride = (VkDeviceSize)capacity * sizeof(mat4); // 16-byte aligned; dynamic-offset unit
    ts->produceSeq  = 0;
    atomic_store_explicit(&ts->reclaimSeq, 0, memory_order_relaxed);
    ts->curSeq      = 0;
    ts->curCount    = 0;
    ts->resolveGen  = 1;                                     // != stagedGen[*] (0) -> first stage runs
    ts->idRing      = NULL;                                  // render-heap; set in initVulkan
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        ts->count[f]     = 0;
        ts->dynOffset[f] = 0;
        ts->frameSeq[f]  = 0;
        ts->stagedGen[f] = 0;
    }

    // Per-frame resolved-slot buffers (binding 0).
    if (!createMappedSsboSet(ctx, (VkDeviceSize)sizeof(uint32_t) * capacity,
                             ts->slotBuffer, ts->slotAllocs, (void**)ts->slotMapped)) {
        printf("Failed to create stream slot buffer!\n");
        return false;
    }

    // Single producer-written transform ring: ringSlices slices of `capacity` mat4s.
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = (VkDeviceSize)ts->ringSlices * ts->sliceStride,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &ts->xformRing) != VK_SUCCESS) {
        printf("Failed to create stream transform ring!\n");
        return false;
    }
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(ctx->device, ts->xformRing, &memReq);
    ts->xformRingAlloc = gpu_alloc(&gpuAllocator, memReq,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ts->xformRingAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(ctx->device, ts->xformRing, NULL);
        ts->xformRing = VK_NULL_HANDLE;
        printf("Failed to allocate stream transform ring!\n");
        return false;
    }
    vkBindBufferMemory(ctx->device, ts->xformRing, ts->xformRingAlloc.memory, ts->xformRingAlloc.offset);
    ts->xformRingMapped = (mat4*)ts->xformRingAlloc.mapped;
    return true;
}

// props selects the backing memory: the live transform buffer is GPU-private
// (DEVICE_LOCAL, regenerated by update.comp every frame, never CPU-touched), whereas
// initialTransform stays HOST_VISIBLE until the Slice-2 staging migration owns it.
bool createTransformBuffer(VulkanContext* ctx, TransformBuffer* buf, uint32_t maxEntities, VkMemoryPropertyFlags props) {
    buf->capacity = maxEntities;
    buf->count = 0;

    VkDeviceSize bufferSize = sizeof(mat4) * maxEntities;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &buf->buffer[i]) != VK_SUCCESS) {
            printf("Failed to create transform buffer!\n");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, buf->buffer[i], &memRequirements);

        buf->allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, props);
        if (buf->allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, buf->buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, buf->buffer[i], buf->allocs[i].memory, buf->allocs[i].offset);
        
        buf->mapped[i] = (mat4*)buf->allocs[i].mapped;
    }
    return true;
}

bool createIndirectDrawBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxDraws) {
    state->indirectBuffer.capacity = maxDraws;
    // Size for the larger of the two command formats so one allocation serves both
    // paths: VkDrawMeshTasksIndirectCommandEXT (12 B) or VkDrawIndexedIndirectCommand (20 B).
    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
    // Partitioned by (frustum, draw slot): ANO_FRUSTUM_COUNT * ano_draw_pipeline_count() partitions,
    // each holding up to maxDraws commands. cull.comp writes (view*drawSlotCount + slot).
    VkDeviceSize bufferSize = cmdStride * maxDraws * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->indirectBuffer.drawCount[i] = 0;
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bufferSize,
            .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->indirectBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create indirect draw buffer!\n");
            return false;
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(ctx->device, state->indirectBuffer.buffer[i], &memReqs);

        // GPU-private: vkCmdFillBuffer + cull.comp write it, draw-indirect reads it; never CPU-touched.
        state->indirectBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->indirectBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->indirectBuffer.buffer[i], NULL);
            return false;
        }

        vkBindBufferMemory(ctx->device, state->indirectBuffer.buffer[i], state->indirectBuffer.allocs[i].memory, state->indirectBuffer.allocs[i].offset);
        state->indirectBuffer.mapped[i] = (VkDrawMeshTasksIndirectCommandEXT*)state->indirectBuffer.allocs[i].mapped;
    }

    return true;
}

// Clustered-forward froxel light lists. Fixed size (ANO_CLUSTER_COUNT froxels), independent of
// entity/light count, so these are a one-time DEVICE_LOCAL allocation off the growth path. Per
// frame: the light-cull compute writes them and the same frame's fragment passes read them.
// in:  ctx, state
// out: true on success; populates frames[i].clusterLight{Count,Index}Buffer/Alloc
bool createClusterBuffers(VulkanContext* ctx, RendererState* state) {
    VkDeviceSize countSize = (VkDeviceSize)sizeof(uint32_t) * ANO_CLUSTER_COUNT;
    VkDeviceSize indexSize = (VkDeviceSize)sizeof(uint32_t) * ANO_CLUSTER_COUNT * ANO_CLUSTER_MAX_LIGHTS;

    // Per view per frame: each view's light-cull bins lights against its own frustum.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &state->frames[i].views[v];
            VkBufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkMemoryRequirements memReqs;

            info.size = countSize;
            if (vkCreateBuffer(ctx->device, &info, NULL, &vr->clusterLightCountBuffer) != VK_SUCCESS) return false;
            vkGetBufferMemoryRequirements(ctx->device, vr->clusterLightCountBuffer, &memReqs);
            vr->clusterLightCountAlloc = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vr->clusterLightCountAlloc.memory == VK_NULL_HANDLE) return false;
            vkBindBufferMemory(ctx->device, vr->clusterLightCountBuffer, vr->clusterLightCountAlloc.memory, vr->clusterLightCountAlloc.offset);

            info.size = indexSize;
            if (vkCreateBuffer(ctx->device, &info, NULL, &vr->clusterLightIndexBuffer) != VK_SUCCESS) return false;
            vkGetBufferMemoryRequirements(ctx->device, vr->clusterLightIndexBuffer, &memReqs);
            vr->clusterLightIndexAlloc = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vr->clusterLightIndexAlloc.memory == VK_NULL_HANDLE) return false;
            vkBindBufferMemory(ctx->device, vr->clusterLightIndexBuffer, vr->clusterLightIndexAlloc.memory, vr->clusterLightIndexAlloc.offset);
        }
    }
    return true;
}

// Dynamic shadow resources (audit 4.7). Per frame: the GPU-written shadow frustum buffer and the
// D32 depth atlas array (one layer per shadow frustum) with per-layer render views + a single
// array sample view. Static (once): the CPU shadow config (which light/face per frustum) and the
// per-light shadow info, both host-visible, filled here for the demo's directional caster (light 0).
// in:  ctx, state (lightBuffer.capacity known)
// out: true on success; populates frames[].shadow.* and state->shadow{FrustumConfig,LightInfo}*
// One ×1 shared 3D voxel image (RGBA16F): STORAGE (voxelize writes) + SAMPLED (M3 trace reads) +
// TRANSFER_DST (per-frame clear). Hand-rolled — createImage() is 2D/depth=1 only — like the shadow
// atlas, with a VK_IMAGE_VIEW_TYPE_3D view. in: ctx; out: img/alloc/view. Returns false on failure.
static bool rc_create_voxel_image(VulkanContext* ctx, uint32_t dim, VkImage* img, GpuAllocation* alloc, VkImageView* view) {
    VkImageCreateInfo iinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    iinfo.imageType = VK_IMAGE_TYPE_3D;
    iinfo.format = ANO_RC_VOXEL_FORMAT;
    iinfo.extent = (VkExtent3D){ dim, dim, dim };
    iinfo.mipLevels = 1;
    iinfo.arrayLayers = 1;
    iinfo.samples = VK_SAMPLE_COUNT_1_BIT;
    iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    iinfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(ctx->device, &iinfo, NULL, img) != VK_SUCCESS) return false;
    VkMemoryRequirements imr; vkGetImageMemoryRequirements(ctx->device, *img, &imr);
    *alloc = gpu_alloc(&rcAllocator, imr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc->memory == VK_NULL_HANDLE) return false;
    vkBindImageMemory(ctx->device, *img, alloc->memory, alloc->offset);

    VkImageViewCreateInfo vinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vinfo.image = *img;
    vinfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vinfo.format = ANO_RC_VOXEL_FORMAT;
    vinfo.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return vkCreateImageView(ctx->device, &vinfo, NULL, view) == VK_SUCCESS;
}

// Axis-aligned orthographic clipmap viewProj projecting `depthAxis` (0=X,1=Y,2=Z) as depth: the two
// other world axes map to ndc.xy in [-1,1], depthAxis maps to ndc.z in [0,1], all over [-H,H].
// Column-major mat4 (m[col][row]), matching the GLSL CullView.viewProj the geometry stage reads.
static void rc_build_clipmap_ortho(mat4 out, uint32_t depthAxis) {
    float H = ANO_RC_CLIP_HALF_EXTENT;
    float s = 1.0f / H;            // [-H,H] -> [-1,1]
    float ds = 1.0f / (2.0f * H);  // [-H,H] -> [0,1]
    memset(out, 0, sizeof(mat4));
    uint32_t sx = (depthAxis + 1u) % 3u; // first screen axis  -> ndc.x
    uint32_t sy = (depthAxis + 2u) % 3u; // second screen axis -> ndc.y
    out[sx][0] = s;          // ndc.x from world[sx]
    out[sy][1] = s;          // ndc.y from world[sy]
    out[depthAxis][2] = ds;  // ndc.z from world[depthAxis]
    out[3][2] = 0.5f;        // depth offset to [0,1]
    out[3][3] = 1.0f;
}

// Radiance-cascade resources (RADIANCE_CASCADES.md M2). The ×1 shared scene voxel substrate
// (albedo+opacity and emission 3D images) + a host-written CullView[3] holding the 3 ortho clipmap
// viewProjs (static origin clipmap, written once) + one descriptor set (set 2: frustums + the two
// storage images). Prereqs: rcAllocator + rcVoxelizeSetLayout + the global pool all created.
bool createRcResources(VulkanContext* ctx, RendererState* state) {
    if (!rc_create_voxel_image(ctx, ANO_RC_VOXEL_DIM, &state->rcVoxelAlbedo, &state->rcVoxelAlbedoAlloc, &state->rcVoxelAlbedoView)) return false;
    if (!rc_create_voxel_image(ctx, ANO_RC_VOXEL_DIM, &state->rcVoxelEmission, &state->rcVoxelEmissionAlloc, &state->rcVoxelEmissionView)) return false;
    if (!rc_create_voxel_image(ctx, ANO_RC_IRRADIANCE_DIM, &state->rcIrradiance, &state->rcIrradianceAlloc, &state->rcIrradianceView)) return false;

    // Linear, clamp-to-edge sampler for trilinear reads of the 3D voxel/irradiance volumes (the
    // debug voxel view samples albedo through it; M3's trace + the GI ambient term reuse it).
    VkSamplerCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(ctx->device, &si, NULL, &state->rcSampler) != VK_SUCCESS) return false;

    // Ortho frustum buffer: CullView[ANO_RC_VOXEL_AXES], host-visible, viewProj filled per axis
    // (planes unused — the geometry stage only reads viewProj). Static clipmap, so written once.
    VkDeviceSize frustumSize = (VkDeviceSize)sizeof(CullView) * ANO_RC_VOXEL_AXES;
    if (!createDataBuffer(ctx, &gpuAllocator, frustumSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &state->rcVoxelizeFrustumBuffer, &state->rcVoxelizeFrustumAlloc)) return false;
    CullView* frustums = (CullView*)state->rcVoxelizeFrustumAlloc.mapped;
    memset(frustums, 0, (size_t)frustumSize);
    for (uint32_t a = 0; a < ANO_RC_VOXEL_AXES; a++)
        rc_build_clipmap_ortho(frustums[a].viewProj, a);

    // One shared set (the volumes are ×1). Binding 0 = frustums (SSBO), 1/2 = storage images (GENERAL).
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dai.descriptorPool = state->globalDescriptorPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &state->rcVoxelizeSetLayout;
    if (vkAllocateDescriptorSets(ctx->device, &dai, &state->rcVoxelizeSet) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo frustumInfo = { state->rcVoxelizeFrustumBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorImageInfo albedoInfo   = { .imageView = state->rcVoxelAlbedoView,   .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo emissionInfo = { .imageView = state->rcVoxelEmissionView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = state->rcVoxelizeSet; writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &frustumInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = state->rcVoxelizeSet; writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &albedoInfo;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = state->rcVoxelizeSet; writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &emissionInfo;
    vkUpdateDescriptorSets(ctx->device, 3, writes, 0, NULL);

    // Trace set (M3b): 0/1 = voxel albedo+emission sampled (SHADER_READ after voxelize), 2 = the
    // irradiance field as a storage image (GENERAL, written by rc_trace.comp).
    VkDescriptorSetAllocateInfo tdai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    tdai.descriptorPool = state->globalDescriptorPool;
    tdai.descriptorSetCount = 1;
    tdai.pSetLayouts = &state->rcTraceSetLayout;
    if (vkAllocateDescriptorSets(ctx->device, &tdai, &state->rcTraceSet) != VK_SUCCESS) return false;

    VkDescriptorImageInfo tAlbedo   = { state->rcSampler, state->rcVoxelAlbedoView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo tEmission = { state->rcSampler, state->rcVoxelEmissionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo tIrr      = { .imageView = state->rcIrradianceView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet tw[3] = {};
    tw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; tw[0].dstSet = state->rcTraceSet; tw[0].dstBinding = 0;
    tw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[0].descriptorCount = 1; tw[0].pImageInfo = &tAlbedo;
    tw[1] = tw[0]; tw[1].dstBinding = 1; tw[1].pImageInfo = &tEmission;
    tw[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; tw[2].dstSet = state->rcTraceSet; tw[2].dstBinding = 2;
    tw[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; tw[2].descriptorCount = 1; tw[2].pImageInfo = &tIrr;
    vkUpdateDescriptorSets(ctx->device, 3, tw, 0, NULL);

    // One-time UNDEFINED -> SHADER_READ so the binding-12/13 samplers always reference a valid layout,
    // even before the first voxelize/trace (default SHADOWMAP mode never runs them). HYBRID/RC
    // re-transition from UNDEFINED each frame (discarding), so this is the resting state for unused frames.
    VkCommandBuffer init = beginSingleTimeCommands(ctx);
    VkImage imgs[3] = { state->rcVoxelAlbedo, state->rcVoxelEmission, state->rcIrradiance };
    VkImageMemoryBarrier toRead[3] = {};
    for (int k = 0; k < 3; k++) {
        toRead[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead[k].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toRead[k].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead[k].image = imgs[k];
        toRead[k].subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toRead[k].srcAccessMask = 0; toRead[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(init, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 3, toRead);
    endSingleTimeCommands(ctx, init);

    return true;
}

bool createShadowResources(VulkanContext* ctx, RendererState* state) {
    VkDeviceSize frustumSize = (VkDeviceSize)sizeof(CullView) * ANO_SHADOW_FRUSTUM_COUNT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ShadowResources* sh = &state->frames[i].shadow;

        // GPU-written shadow frustum buffer (viewProj + planes per frustum).
        VkBufferCreateInfo binfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = frustumSize, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        if (vkCreateBuffer(ctx->device, &binfo, NULL, &sh->frustumBuffer) != VK_SUCCESS) return false;
        VkMemoryRequirements bmr; vkGetBufferMemoryRequirements(ctx->device, sh->frustumBuffer, &bmr);
        sh->frustumAlloc = gpu_alloc(&gpuAllocator, bmr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (sh->frustumAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindBufferMemory(ctx->device, sh->frustumBuffer, sh->frustumAlloc.memory, sh->frustumAlloc.offset);

        // Depth atlas: D32 2D array, ANO_SHADOW_FRUSTUM_COUNT layers, single-sample, sampled + rendered.
        VkImageCreateInfo iinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        iinfo.imageType = VK_IMAGE_TYPE_2D;
        iinfo.format = ANO_SHADOW_DEPTH_FORMAT;
        iinfo.extent = (VkExtent3D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM, 1 };
        iinfo.mipLevels = 1;
        iinfo.arrayLayers = ANO_SHADOW_FRUSTUM_COUNT;
        iinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        iinfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(ctx->device, &iinfo, NULL, &sh->atlasImage) != VK_SUCCESS) return false;
        VkMemoryRequirements imr; vkGetImageMemoryRequirements(ctx->device, sh->atlasImage, &imr);
        sh->atlasAlloc = gpu_alloc(&gpuAllocator, imr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (sh->atlasAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindImageMemory(ctx->device, sh->atlasImage, sh->atlasAlloc.memory, sh->atlasAlloc.offset);

        // Array sample view (all layers) + per-layer render views.
        VkImageViewCreateInfo vinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vinfo.image = sh->atlasImage;
        vinfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vinfo.format = ANO_SHADOW_DEPTH_FORMAT;
        vinfo.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, ANO_SHADOW_FRUSTUM_COUNT };
        if (vkCreateImageView(ctx->device, &vinfo, NULL, &sh->arrayView) != VK_SUCCESS) return false;

        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            VkImageViewCreateInfo lv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            lv.image = sh->atlasImage;
            lv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            lv.format = ANO_SHADOW_DEPTH_FORMAT;
            lv.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
            if (vkCreateImageView(ctx->device, &lv, NULL, &sh->layerView[s]) != VK_SUCCESS) return false;
        }
        // Layout handled per-frame in recordCommandBuffer (UNDEFINED->DEPTH->SHADER_READ).
    }

    // Static CPU config (once), host-visible. Demo rig: light 0 is the directional caster.
    GpuAllocation cfgAlloc, infoAlloc;
    VkDeviceSize cfgSize = (VkDeviceSize)sizeof(ShadowFrustumConfig) * ANO_SHADOW_FRUSTUM_COUNT;
    VkDeviceSize infoSize = (VkDeviceSize)sizeof(ShadowLightInfo) * state->lightBuffer.capacity;
    if (!createDataBuffer(ctx, &gpuAllocator, cfgSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &state->shadowFrustumConfigBuffer, &cfgAlloc)) return false;
    if (!createDataBuffer(ctx, &gpuAllocator, infoSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &state->shadowLightInfoBuffer, &infoAlloc)) return false;
    state->shadowFrustumConfigAlloc = cfgAlloc; state->shadowFrustumConfigMapped = cfgAlloc.mapped;
    state->shadowLightInfoAlloc = infoAlloc;   state->shadowLightInfoMapped = infoAlloc.mapped;

    // Config starts empty: addLightEntity registers each caster (per-type budget) as the scene rig
    // is built, advancing state->shadowFrustumNext. Frustums never claimed by a caster stay zeroed,
    // which shadowsetup reads as light 0's directional map — harmless, since no light's info points
    // at them. info[] starts all-non-casting; the rig sets the casters' baseFrustum/frustumCount.
    memset(state->shadowFrustumConfigMapped, 0, (size_t)cfgSize);
    ShadowLightInfo* info = (ShadowLightInfo*)state->shadowLightInfoMapped;
    for (uint32_t l = 0; l < state->lightBuffer.capacity; l++)
        info[l] = (ShadowLightInfo){ .castsShadow = 0u, .baseFrustum = 0u, .frustumCount = 0u, .pad = 0u };
    state->shadowFrustumNext = 0u;
    state->shadowTypeUsed[0] = state->shadowTypeUsed[1] = state->shadowTypeUsed[2] = 0u;

    return true;
}

bool createCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->culling.maxEntities = maxEntities;
    uint32_t maxMeshes = 1024;
    
    VkDeviceSize meshDataSize = sizeof(uint32_t) * 8 * maxMeshes; // uvec8
    VkDeviceSize meshBoundsSize = sizeof(float) * 4 * maxMeshes; // vec4
    VkDeviceSize drawCountSize = sizeof(uint32_t) * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;
    VkDeviceSize compactedEntityIndicesSize = sizeof(uint32_t) * maxEntities * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;
    VkDeviceSize uboSize = sizeof(CullUBO);
    
    // Per-slot mesh/material (meshIndex, materialIndex): ×1 device-local + delta staging,
    // replacing the former per-frame host-visible entityBuffer.
    if (!slot_upload_create(&state->culling.entity, maxEntities, sizeof(uint32_t) * 2u, SLOT_STAGING_INIT))
        return false;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkMemoryRequirements memReqs;

        // Mesh Data Buffer
        VkBufferCreateInfo meshInfo = {};
        meshInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        meshInfo.size = meshDataSize;
        meshInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meshInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &meshInfo, NULL, &state->culling.meshDataBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.meshDataBuffer[i], &memReqs);
        state->culling.meshDataAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.meshDataAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.meshDataBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.meshDataBuffer[i], state->culling.meshDataAllocs[i].memory, state->culling.meshDataAllocs[i].offset);
        state->culling.meshDataMapped[i] = state->culling.meshDataAllocs[i].mapped;

        // Mesh Bounds Buffer
        VkBufferCreateInfo boundsInfo = {};
        boundsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        boundsInfo.size = meshBoundsSize;
        boundsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        boundsInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &boundsInfo, NULL, &state->culling.meshBoundsBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.meshBoundsBuffer[i], &memReqs);
        state->culling.meshBoundsAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.meshBoundsAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.meshBoundsBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.meshBoundsBuffer[i], state->culling.meshBoundsAllocs[i].memory, state->culling.meshBoundsAllocs[i].offset);
        state->culling.meshBoundsMapped[i] = state->culling.meshBoundsAllocs[i].mapped;

        // Draw Count Buffer
        VkBufferCreateInfo countInfo = {};
        countInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        countInfo.size = drawCountSize;
        countInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        countInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &countInfo, NULL, &state->culling.drawCountBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.drawCountBuffer[i], &memReqs);
        // GPU-private: zeroed by vkCmdFillBuffer, atomic-incremented by cull.comp, read by draw-indirect-count.
        state->culling.drawCountAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->culling.drawCountAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.drawCountBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.drawCountBuffer[i], state->culling.drawCountAllocs[i].memory, state->culling.drawCountAllocs[i].offset);
        state->culling.drawCountMapped[i] = (uint32_t*)state->culling.drawCountAllocs[i].mapped;

        // Compacted Entity Indices Buffer
        VkBufferCreateInfo compactedInfo = {};
        compactedInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        compactedInfo.size = compactedEntityIndicesSize;
        compactedInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        compactedInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &compactedInfo, NULL, &state->culling.compactedEntityIndicesBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.compactedEntityIndicesBuffer[i], &memReqs);
        // GPU-private: written by cull.comp (compaction), read by the geometry stage.
        state->culling.compactedEntityIndicesAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->culling.compactedEntityIndicesAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.compactedEntityIndicesBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.compactedEntityIndicesBuffer[i], state->culling.compactedEntityIndicesAllocs[i].memory, state->culling.compactedEntityIndicesAllocs[i].offset);
        state->culling.compactedEntityIndicesMapped[i] = (uint32_t*)state->culling.compactedEntityIndicesAllocs[i].mapped;

        // Cull UBO
        VkBufferCreateInfo uboInfo = {};
        uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uboInfo.size = uboSize;
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &uboInfo, NULL, &state->culling.ubo.buffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.ubo.buffer[i], &memReqs);
        state->culling.ubo.allocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.ubo.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.ubo.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.ubo.buffer[i], state->culling.ubo.allocs[i].memory, state->culling.ubo.allocs[i].offset);
        state->culling.ubo.mapped[i] = (CullUBO*)state->culling.ubo.allocs[i].mapped;
    }
    return true;
}

bool createFallbackResources(VulkanContext* ctx, RendererState* state)
{
    // 1. Fallback Mesh (Simple Cube)
    const Vertex cubeVertices[] = {
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };
    
    // Winding matches the glTF convention (CCW front under frontFace=CCW + the Y-flip projection),
    // so the fallback cube agrees with loaded meshes and needs no per-instance mirror. (The original
    // order was the reverse, which left the ground box's top face back-facing — see
    // docs/math_conventions.md, winding.) Per triangle (a,b,c) -> (a,c,b).
    const uint32_t cubeIndices[] = {
        0, 2, 1, 2, 0, 3, // front
        1, 6, 5, 6, 1, 2, // right
        5, 7, 4, 7, 5, 6, // back
        4, 3, 0, 3, 4, 7, // left
        3, 6, 2, 6, 3, 7, // top
        4, 1, 5, 1, 4, 0  // bottom
    };

    uint32_t fallbackMeshIdx = geometry_pool_upload(&state->globalGeometryPool, &stagingAllocator,
                                                    ctx->device,
                                                    ctx->queueFamilyIndices.transferFamily,
                                                    ctx->transferQueue,
                                                    cubeVertices, 8, cubeIndices, 36);

    if (fallbackMeshIdx != FALLBACK_MESH_INDEX) {
        printf("Warning: Fallback mesh was assigned index %u instead of %u!\n", fallbackMeshIdx, FALLBACK_MESH_INDEX);
    }

    // 2. Fallback Texture (2x2 Magenta/Black Checkerboard)
    unsigned char fallbackPixels[16] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255
    };

    GpuAllocation fallbackImageAlloc; // Memory managed by gpu_allocator

    if (!createTextureImageFromPixels(ctx, VK_NULL_HANDLE, &state->fallbackImage, &fallbackImageAlloc, &state->fallbackImageView, fallbackPixels, 2, 2, NULL)) {
        printf("Warning: Failed to create fallback texture!\n");
        return false;
    }

    // 3. Register Fallback Texture
    uint32_t fallbackTexIdx = bindless_register_texture(ctx, &state->bindlessTextures, state->fallbackImageView, state->textureSampler);
    
    if (fallbackTexIdx != FALLBACK_TEXTURE_INDEX) {
        printf("Warning: Fallback texture was assigned index %u instead of %u!\n", fallbackTexIdx, FALLBACK_TEXTURE_INDEX);
    }

    return true;
}

bool initVulkan() // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{

	// Window initialization
	Dimensions2D initDimensions = {800, 600};
	setResolution(initDimensions);
	setMonitor(-1);
	setBorderless(0);

    vulkanGarbage.monitors = &monitors;
    cleanupMonitors(&monitors);
    enumerateMonitors(&monitors);

	window = initWindow(&ctx, &monitors);

	if (window == NULL)
	{
		// Handle error
		printf("Window initialization failed.\n");
		unInitVulkan();
		return 0;
	}

	requestPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);

	ctx.enableValidationLayers = true;
	rendererState.frameIndex = 0; // Tracks which frame is being processed

	// Initialize Vulkan
	if (createInstance(&ctx) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create Vulkan instance!\n");
		unInitVulkan();
		return false;
	}
	vulkanGarbage.ctx = &ctx;

	// Create a window surface
	if (createSurface(ctx.instance, window, &(ctx.surface)) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create window surface!\n");
		unInitVulkan();
		return false;
	}

	// Pick physical device
	DeviceCapabilities capabilities;
	ctx.physicalDevice = VK_NULL_HANDLE;

	//!TODO replace empty char array with preffered device from VulkanSettings   
	char* preferredDevice = getChosenDevice();
	if (!pickPhysicalDevice(&ctx, &capabilities, &(ctx.queueFamilyIndices), preferredDevice))
	{
		fprintf(stderr, "Quitting init: physical device failure!\n");
		unInitVulkan();
		return false;
	}
	

	if (createLogicalDevice(ctx.physicalDevice, &(ctx.device), &(ctx.graphicsQueue), &(ctx.computeQueue), &(ctx.transferQueue), &(ctx.presentQueue), &(ctx.queueFamilyIndices)) != VK_SUCCESS)
	{
		fprintf(stderr, "Quitting init: logical device failure!\n");
		unInitVulkan();
		return false;
	}

    // Mesh-shader entry points only exist on the mesh path. The fallback path draws
    // via core vkCmdDrawIndexedIndirect[Count] and needs none of these.
    if (ctx.deviceCapabilities.meshShader) {
        pfnVkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksEXT");
        pfnVkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectEXT");
        pfnVkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectCountEXT");

        if (!pfnVkCmdDrawMeshTasksEXT || !pfnVkCmdDrawMeshTasksIndirectEXT || !pfnVkCmdDrawMeshTasksIndirectCountEXT) {
            fprintf(stderr, "Failed to load mesh shader extension function pointers!\n");
            unInitVulkan();
            return false;
        }
    }

	gpuAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &gpuAllocator.memProps);
	gpuAllocator.blocks = NULL;
	gpuAllocator.blockCount = 0;
	stagingAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &stagingAllocator.memProps);
	stagingAllocator.blocks = NULL;
	stagingAllocator.blockCount = 0;

	swapchainAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &swapchainAllocator.memProps);
	swapchainAllocator.blocks = NULL;
	swapchainAllocator.blockCount = 0;

	textureAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &textureAllocator.memProps);
	textureAllocator.blocks = NULL;
	textureAllocator.blockCount = 0;

	rcAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &rcAllocator.memProps);
	rcAllocator.blocks = NULL;
	rcAllocator.blockCount = 0;

	if (!ano_vk_init_geometry_pool(&rendererState.globalGeometryPool, &gpuAllocator, ctx.device, ctx.queueFamilyIndices.graphicsFamily, ctx.queueFamilyIndices.transferFamily)) {
		printf("Quitting init: geometry pool creation failure!\n");
		unInitVulkan();
		return false;
	}

	initSwapChain(&ctx, window, getChosenPresentMode(), VK_NULL_HANDLE, &rendererState); // Initialize a swap chain
	if (rendererState.swapChain == NULL)
	{
		printf("Quitting init: swap chain failure.\n");
		unInitVulkan();
		return false;
	}
	
	createImageViews(&ctx, &rendererState);
	if (rendererState.views == NULL)
	{
		printf("Quitting init: image view failure.\n");
		unInitVulkan();
		return false;
	}

	if (!createCommandPool(ctx.device, ctx.physicalDevice,
						   ctx.surface, &(rendererState.commandPool)))
	{
		printf("Quitting init: command pool failure!\n");
		unInitVulkan();
		return false;
	}

	createColorResources(&ctx); // Make this a bool and add check

	if(!createDepthResources(&ctx, &rendererState))
	{
		printf("Quitting init: depth resource creation failure!\n");
	}



	if (!ano_vk_init_global_layout(&ctx, &rendererState))
	{
		printf("Quitting init: global layout failure!\n");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_cull_layout(&ctx, &rendererState))
	{
		printf("Quitting init: cull layout failure!\n");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_material_layouts(&ctx, &rendererState))
	{
		printf("Quitting init: material layouts failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createBindlessTextureArray(&ctx, &rendererState))
	{
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_pipelines(&ctx, &rendererState))
	{
		printf("Quitting init: pipeline failure!\n");
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_tonemap(&ctx, &rendererState))
	{
		printf("Quitting init: tonemap pipeline failure!\n");
		unInitVulkan();
		return false;
	}

	// Depth-only shadow pipeline + compare sampler (reuses the flat pipeline layout, so after pipelines).
	if (!ano_vk_init_shadow(&ctx, &rendererState))
	{
		printf("Quitting init: shadow pipeline failure!\n");
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_rc_voxelize(&ctx, &rendererState))
	{
		printf("Quitting init: radiance-cascade voxelize pipeline failure!\n");
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_rc_trace(&ctx, &rendererState))
	{
		printf("Quitting init: radiance-cascade trace pipeline failure!\n");
		unInitVulkan();
		return false;
	}

	if(!createTextureSampler(&ctx, &rendererState))
	{
		printf("Quitting init: texture sampler failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createFallbackResources(&ctx, &rendererState))
	{
		printf("Quitting init: fallback resources failure.\n");
		unInitVulkan();
		return false;
	}

	rendererState.entityCount = 0;

	// Slot-indexed buffers start at INITIAL_ENTITY_CAPACITY and grow on demand
	// (ensureEntityCapacity); material/light are distinct-element palettes on their
	// own capacity axis. maxEntities is the starting slot count, no longer a ceiling.
	uint32_t maxEntities = INITIAL_ENTITY_CAPACITY;
	if (!createTransformBuffer(&ctx, &rendererState.transformBuffer, maxEntities,
	                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
	    !slot_upload_create(&rendererState.initialTransformBuffer, maxEntities, sizeof(mat4), SLOT_STAGING_INIT) ||
	    !createMotionBuffer(&ctx, &rendererState, maxEntities) ||
	    !createInstanceDataBuffer(&ctx, &rendererState, maxEntities) ||
	    !createStreamBuffers(&ctx, &rendererState, STREAM_CAPACITY) ||
	    !createMaterialBuffer(&ctx, &rendererState, PALETTE_CAPACITY) ||
	    !createLightBuffer(&ctx, &rendererState, PALETTE_CAPACITY) ||
	    !createIndirectDrawBuffer(&ctx, &rendererState, maxEntities) ||
	    !createCullingBuffers(&ctx, &rendererState, maxEntities) ||
	    !createClusterBuffers(&ctx, &rendererState) ||
	    !createShadowResources(&ctx, &rendererState))
	{
		printf("Quitting init: buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	ModelAsset* vikingRoomAsset = parseGltf(&ctx, "viking_room.gltf");
	if(!vikingRoomAsset)
	{
		printf("Failed to parse glTF file!\n");
		unInitVulkan();
		return false;
	}
	
	mat4 identity = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};
	rotateMatrix(identity, 'X', -3.14159f / 2.0f);
	instantiate_model(vikingRoomAsset, identity);

	uint32_t vikingRoomEntityCount = rendererState.entityCount;

	ModelAsset* candleHolderAsset = parseGltf(&ctx, "GlassHurricaneCandleHolder.gltf");
	if(!candleHolderAsset)
	{
		printf("Failed to parse GlassHurricaneCandleHolder glTF file!\n");
		unInitVulkan();
		return false;
	}
	
	mat4 candleTransform = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};
	candleTransform[3][0] = 2.0f; // Orbit radius of 5 units on X
	instantiate_model(candleHolderAsset, candleTransform);

	// Ground plane (audit 4.7): a wide thin slab — the fallback cube scaled flat — placed below
	// the scene so the directional shadow is visible regardless of light orientation. Reuses the
	// viking room's opaque material (guaranteed to draw + cast). Forced static in the motion loop.
	uint32_t groundEntity = rendererState.entityCount;
	{
        // Thin ground box, all-positive scale. The fallback cube's winding now matches glTF, so its
        // top face is front-facing under frontFace=CCW (no mirror needed), and the constant (1,1,1)
        // vertex normal resolves to ~+Y here under the inverse-transpose normal matrix (the thin Y
        // axis dominates), so the top lights and receives shadow correctly. See docs/math_conventions.md.
        mat4 g = {{15.0f, 0,      0,     0},
                  {0,     0.05f,  0,     0},
                  {0,     0,      15.0f, 0},
                  {0,    -0.6f,   0,     1}};
		rendererState.entityCount += 1;
		rendererState.entities = realloc(rendererState.entities, rendererState.entityCount * sizeof(RenderEntity));
		rendererState.entities[groundEntity].meshIndex = FALLBACK_MESH_INDEX;
		rendererState.entities[groundEntity].materialIndex = rendererState.entities[0].materialIndex;
		rendererState.entities[groundEntity].lightIndex = ANO_RENDER_NO_LIGHT;
		memcpy(&rendererState.entities[groundEntity].transform, g, sizeof(mat4));
	}

	// Sun marker (debug, audit 4.7): a small cube at the directional light's source direction
	// (lightDir * 6, lightDir = normalized (0.5,1,0.3) — the vector shadowsetup derives as
	// -lightForward). Shadows must extend AWAY from it: an in-render check for the light's
	// orientation, since the renderer has no other directional-light gizmo.
	uint32_t sunMarkerEntity = rendererState.entityCount;
	{
		mat4 m = {{0.2f,0,0,0},{0,0.2f,0,0},{0,0,0.2f,0},{2.59f,5.18f,1.55f,1}};
		rendererState.entityCount += 1;
		rendererState.entities = realloc(rendererState.entities, rendererState.entityCount * sizeof(RenderEntity));
		rendererState.entities[sunMarkerEntity].meshIndex = FALLBACK_MESH_INDEX;
		rendererState.entities[sunMarkerEntity].materialIndex = rendererState.entities[0].materialIndex;
		rendererState.entities[sunMarkerEntity].lightIndex = ANO_RENDER_NO_LIGHT;
		memcpy(&rendererState.entities[sunMarkerEntity].transform, m, sizeof(mat4));
	}

	// -------------------------------------------------------------
	// Scene lights (generalized light format, replacing the values
	// formerly hard-coded in the fragment shaders). Each light is a
	// transform-only entity: it carries no geometry (culling skips it)
	// and its world position/direction are derived from its transform,
	// so it participates in the GPU transform/animation system.
	//
	// Convention: a light's "forward" is the entity local -Z axis, i.e.
	// -transform[2] (column 2). For directional/spot lights set column 2
	// to the negated travel direction; translation lives in column 3.
	// -------------------------------------------------------------
	uint32_t firstLightEntity = rendererState.entityCount;

	// Directional key light (warm white), shining from up/front (0.5,1,0.3).
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[2][0] = 0.4319f; xform[2][1] = 0.8638f; xform[2][2] = 0.2591f; // normalized (0.5,1,0.3)
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.96f; l.color[2] = 0.9f;
		l.intensity = 2.5f;
		l.range = 0.0f; // directional: unbounded
		l.type = LIGHT_TYPE_DIRECTIONAL;
		addLightEntity(l, xform);
	}

	// Warm point light (orbits the origin to exercise the animation path).
	uint32_t warmLightEntity;
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = 1.5f; xform[3][2] = 1.2f;
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.95f; l.color[2] = 0.8f;
		l.intensity = 5.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		warmLightEntity = addLightEntity(l, xform);
	}

	// Cool blue fill from the opposite side.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = -2.0f; xform[3][1] = 2.0f; xform[3][2] = -1.0f;
		LightData l = {0};
		l.color[0] = 0.4f; l.color[1] = 0.6f; l.color[2] = 1.0f;
		l.intensity = 4.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Red rim/accent light.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 2.0f; xform[3][1] = 0.5f; xform[3][2] = 0.0f;
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.3f; l.color[2] = 0.3f;
		l.intensity = 3.5f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Greenish/cyan fill from below.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = -1.0f; xform[3][2] = 1.0f;
		LightData l = {0};
		l.color[0] = 0.3f; l.color[1] = 1.0f; l.color[2] = 0.8f;
		l.intensity = 2.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Overhead spotlight aimed straight down (demonstrates the spot type).
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = 4.0f; xform[3][2] = 0.0f;
		// forward = -column2 = (0,-1,0): aim down. column2 stays identity (0,0,1)? No:
		xform[2][0] = 0.0f; xform[2][1] = 1.0f; xform[2][2] = 0.0f; // -column2 = (0,-1,0)
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 1.0f; l.color[2] = 1.0f;
		l.intensity = 20.0f; l.range = 12.0f; l.type = LIGHT_TYPE_SPOT;
		l.innerConeCos = 0.966f; // ~15 degrees
		l.outerConeCos = 0.906f; // ~25 degrees
		addLightEntity(l, xform);
	}

	// ECS <-> render bridge: render-owned slot authority + command/event rings
	// (VK_BACKEND_INTEROP.md). The whole init scene is now published as ONE
	// RCMD_BULK_CREATE — the same command path a runtime mass-spawn takes — instead
	// of writing the per-slot GPU buffers directly here. render_id == entity index
	// because init is append-only, so render_slots_alloc_range hands back a
	// contiguous identity slot range; this is the seam the logic-side ECS will own
	// once entities[] is retired.
	rendererState.renderHeap = mi_heap_new();
	if (!rendererState.renderHeap ||
	    !render_slots_init(&rendererState.slots, rendererState.renderHeap, maxEntities, MAX_FRAMES_IN_FLIGHT) ||
	    !ano_render_bridge_init(&rendererState.bridge, rendererState.renderHeap, 4096, 1024))
	{
		printf("Quitting init: render bridge / slot authority failure!\n");
		unInitVulkan();
		return false;
	}

	// Stream render_id ring now that renderHeap exists: CPU-only, parallel to xformRing,
	// producer-written and render-resolved. Freed wholesale on mi_heap_destroy at teardown.
	rendererState.transformStream.idRing = mi_heap_malloc(rendererState.renderHeap,
	    (size_t)rendererState.transformStream.ringSlices * STREAM_CAPACITY * sizeof(uint32_t));
	if (!rendererState.transformStream.idRing)
	{
		printf("Quitting init: stream id ring allocation failure!\n");
		unInitVulkan();
		return false;
	}
	rendererState.globalFrame = 0;

	// Build the initial-state batch from the CPU scene record. The base pose folds
	// in the legacy spawn nudge (first three meshes) and the continuous-motion
	// vocabulary (spin for meshes, orbit for the candle + warm light); these reach
	// the GPU once as initialTransform / animation params and are never restreamed.
	// The live transform is intentionally NOT written — update.comp derives it from
	// initialTransform every frame. Lights keep their photometric LightData in the
	// light palette (addLightEntity); the batch only carries their renderable
	// projection (mesh == NO_MESH_INDEX, so the cull pass skips them).
	uint32_t batchCount = rendererState.entityCount;
	uint32_t *batchIds      = mi_heap_malloc(rendererState.renderHeap, (size_t)batchCount * sizeof(uint32_t));
	mat4     *batchXforms   = mi_heap_malloc(rendererState.renderHeap, (size_t)batchCount * sizeof(mat4));
	AnoMotionDescriptor *batchMotion = mi_heap_malloc(rendererState.renderHeap, (size_t)batchCount * sizeof(AnoMotionDescriptor));
	uint32_t *batchMesh     = mi_heap_malloc(rendererState.renderHeap, (size_t)batchCount * sizeof(uint32_t));
	uint32_t *batchMaterial = mi_heap_malloc(rendererState.renderHeap, (size_t)batchCount * sizeof(uint32_t));
	if (!batchIds || !batchXforms || !batchMotion || !batchMesh || !batchMaterial)
	{
		printf("Quitting init: bulk-create batch allocation failure!\n");
		unInitVulkan();
		return false;
	}

	float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
	for (uint32_t i = 0; i < batchCount; i++) {
		bool isLight  = i >= firstLightEntity;
		bool isCandle = !isLight && i >= vikingRoomEntityCount;
		bool orbit    = isCandle || i == warmLightEntity;

		batchIds[i]      = i;
		batchMesh[i]     = rendererState.entities[i].meshIndex;
		batchMaterial[i] = rendererState.entities[i].materialIndex;

		// Preserve prior behavior: orbiters revolve about world +Y at 0.5 rad/s,
		// other non-lights spin in place about local +Y at 1.0 rad/s, lights static.
		AnoMotionDescriptor md = {0}; // ANO_MOTION_STATIC
		if (orbit) {
			md.type = ANO_MOTION_ORBIT;
			md.p0.v[1] = 0.5f;
		} else if (!isLight) {
			md.type = ANO_MOTION_SPIN;
			md.p0.v[1] = 1.0f;
		}
		// STAND-IN (Path B): the first two renderables are CPU-streamed (see
		// stream_stand_in); update.comp leaves them at base and scatter overwrites them.
		if (i < 2) { md = (AnoMotionDescriptor){0}; md.type = ANO_MOTION_STREAMED; }
		if (i == groundEntity || i == sunMarkerEntity) md = (AnoMotionDescriptor){0}; // ground + sun marker are static
		batchMotion[i] = md;

		// Fold the legacy spawn nudge into entities[] so it stays the authoritative host-side
		// base-pose record (anoRenderEntityBaseTransform reads it; initialTransform is device-local).
		if (!isLight && i < 3)
			rendererState.entities[i].transform[3][0] += moveOffsets[i];
		memcpy(&batchXforms[i], &rendererState.entities[i].transform, sizeof(mat4));
	}

	RenderCreateBatch initBatch = {
		.count      = batchCount,
		.render_ids = batchIds,
		.transforms = batchXforms,
		.motion     = batchMotion,
		.mesh       = batchMesh,
		.material   = batchMaterial,
	};
	RenderCommand bulk = { .kind = RCMD_BULK_CREATE, .batch = &initBatch };
	if (!ano_render_submit(&rendererState.bridge, &bulk))
	{
		printf("Quitting init: failed to submit bulk-create command!\n");
		unInitVulkan();
		return false;
	}

	// Seed every slot once: a single drain allocates the slot range and stages the batch
	// (plus the addLightEntity light writes) into frame 0's delta staging, then a one-shot
	// transfer uploads it into the DEVICE_LOCAL authoritative buffers. The device is idle here
	// (no per-frame command buffer recording yet); the device buffers are shared by every
	// frame in flight, so one upload seeds them all.
	render_apply_commands(&rendererState, 0);
	{
		VkCommandBuffer up = beginSingleTimeCommands(&ctx);
		slot_upload_flush(up, &rendererState.initialTransformBuffer, 0);
		slot_upload_flush(up, &rendererState.motionBuffer, 0);
		slot_upload_flush(up, &rendererState.instanceDataBuffer, 0);
		slot_upload_flush(up, &rendererState.lightBuffer, 0);
		slot_upload_flush(up, &rendererState.culling.entity, 0);
		endSingleTimeCommands(&ctx, up);
	}

	mi_free(batchIds);
	mi_free(batchXforms);
	mi_free(batchMotion);
	mi_free(batchMesh);
	mi_free(batchMaterial);

	if (!createUniformBuffers(&ctx, &rendererState))
	{
		printf("Quitting init: uniform buffer creation failure!\n");
		unInitVulkan();
		return false;
	}



	// HERE
	if (!createDescriptorPool(&ctx, &rendererState))
	{
		printf("Quitting init: UBO descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}

	// Radiance-cascade resources (RADIANCE_CASCADES.md M2): needs the pool (above) + rcVoxelizeSetLayout
	// (ano_vk_init_rc_voxelize) + rcAllocator (all created earlier in init).
	if (!createRcResources(&ctx, &rendererState))
	{
		printf("Quitting init: radiance-cascade resource creation failure!\n");
		unInitVulkan();
		return false;
	}


	if (!createDescriptorSets(&ctx, &rendererState))
	{
		printf("Quitting init: UBO descriptor sets creation failure!\n");
		unInitVulkan();
		return false;
	}


	updateUboDescriptorSets(&ctx, &rendererState);
	updateTonemapDescriptorSets(&ctx, &rendererState);
	updateClusterDescriptorSets(&ctx, &rendererState);
	updateShadowDescriptorSets(&ctx, &rendererState);


	if (!createCommandBuffer(&ctx, &rendererState))
	{
		printf("Quitting init: command buffer failure!\n");
		unInitVulkan();
		return false;
	}
	

	if (!createSyncObjects(&ctx, &rendererState))
	{
		printf("Quitting init: sync failure!\n");
		unInitVulkan();
		return false;
	}

	printf("Instance creation complete!\n");

	return true;
}

