/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <math.h>
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

// Light-palette rows [0, ANO_STATIC_LIGHT_COUNT) are the STATIC region the logic master fills with
// scene light-entities (create-with-light, static shadow budget); the runtime attach registry owns
// [ANO_STATIC_LIGHT_COUNT, PALETTE_CAPACITY). A fixed boundary (vs the old "base = live scene count")
// lets the logic master own the static light_index namespace independently of the runtime registry.
#define ANO_STATIC_LIGHT_COUNT     64u

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

// Runtime light registry teardown (audit 4.7 Phase 3); defined with the registry below but used by
// unInitVulkan above it.
static void light_registry_destroy(LightRegistry* r);

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

	// Async Hi-Z (review finding 2): the compute submits are not fence-tracked; drain the last
	// signaled ordinal before teardown destroys their pool/semaphores/images.
	if (rendererState.asyncHiz && rendererState.hizTimeline != VK_NULL_HANDLE
		&& ctx.device != VK_NULL_HANDLE && rendererState.timelineOrdinal > 0)
	{
		uint64_t last = rendererState.timelineOrdinal;
		VkSemaphoreWaitInfo waitInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1, .pSemaphores = &rendererState.hizTimeline, .pValues = &last };
		vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX);
	}

	// ECS<->render bridge teardown. CPU-only; safe on a zeroed state (early-init
	// failure) since the destroys guard NULL and the heap free is gated below.
	ano_render_bridge_destroy(&rendererState.bridge);
	render_slots_destroy(&rendererState.slots);
	light_registry_destroy(&rendererState.lightRegistry);
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
    // 1b. Per-light world-pose precompute: resolve each light's world position + forward from its (now
    //     final) driving transform ONCE per frame, so the fragment passes stop reloading the 64B mat4
    //     and re-deriving lightPos/lightForward per fragment. Shared (not per view); after update+scatter
    //     finalize transforms, before the geometry passes that read the pose (set 0 binding 12).
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTSETUP,
        .dispatchX  = 0,  // computed from light count at runtime
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
    // 4a. Depth pre-pass (perView): the opaque geometry rendered depth-only (fragment stage stripped,
    //     no color) to lay down the nearest depth. The opaque pass below then shades each visible pixel
    //     exactly once via an EQUAL test, removing overdraw of the heavy lighting shader (the frame's
    //     dominant cost). Same FLAT draw partition + geometry module -> bit-identical depth for EQUAL.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 2,  // depth-only variant
        .perView                = true,
        .colorAttachmentCount   = 0,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // opaque + transmission load this depth
    },
    // 4b. Depth pre-pass, two-sided lane (review finding 7): the opaque doubleSided partition laid
    //     down with cullMode NONE. LOADs 4a's depth (which CLEARed); the barrier orders 4a's writes
    //     under this pass's LESS test.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex    = 2,  // depth-only variant
        .perView                = true,
        .colorAttachmentCount   = 0,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,
        .depthBarrierBefore     = true,                             // wait on 4a's depth writes
    },
    // 4. Opaque geometry (perView: rendered once per view into that view's HDR target + depth)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 2,  // [0] HDR color, [1] R32_UINT picking id (audit 3.1)
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // EQUAL-test against the pre-pass depth
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // transmission pass loads this depth
        .depthBarrierBefore     = true,                             // wait on BOTH pre-passes' depth writes
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 4c. Opaque two-sided lane (review finding 7): same shading as 4 with cullMode NONE, LOADing
    //     4's color/id (4 CLEARed; its depth barrier already ordered both pre-passes, and EQUAL
    //     assigns each pixel to exactly one lane). Both opaque passes resolve the picking id; the
    //     LAST resolve — this one — is what ano_collect_pick reads, so both lanes stay pickable.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 2,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 5. Transmissive geometry (depth-sorted "over" lane)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_TRANSMISSION,
        .implementationIndex    = 1,  // blended transmission variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // additive pass below loads this depth
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 6. Additive glows (order-independent ONE/ONE). Drawn last so glows composite on top; depth-
    //    tested against opaque depth (hidden behind solids) but no depth write, so layers all add.
    //    As the LAST color pass it carries the view's ONLY MSAA->HDR resolve (review finding 5):
    //    the MSAA surface persists across opaque/transmission/additive (LOAD + STORE), so
    //    resolving in every pass just rewrote the same texels three times.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_ADDITIVE,
        .implementationIndex    = 0,  // single additive variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,     // Hi-Z reduce (review 4.9 step 3) reads this depth
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

// Hi-Z pyramid chain for one view (review 4.9 step 3): pyramid mips -> GENERAL, reduce mip 0 from
// the resolved (or MSAA) depth via the per-mip sets, MAX-downsample the chain, return the pyramid
// to SHADER_READ for the cull. Inputs: cmd = target CB, vr = the view's resources for the frame
// slot being recorded, viewExtent = the view's render extent (mip-0 source dims). Every barrier is
// compute-stage only, so the same recording serves the in-frame graphics path and the async
// compute CB (review finding 2). The pre-chain WAR (a prior frame's cull still sampling this slot)
// is carried by srcStage=COMPUTE submission order in-frame, and by the compute submit's gfxTimeline
// wait async — either way srcAccess 0: prior reads make nothing available.
static void record_hiz_pyramid_chain(VkCommandBuffer cmd, ViewResources* vr, VkExtent2D viewExtent)
{
    // pyramid (all mips) -> GENERAL for the storage writes. oldLayout is the resting SHADER_READ
    // (not UNDEFINED). The build overwrites every mip, so no contents are kept.
    VkImageMemoryBarrier pyrToGeneral = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    pyrToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pyrToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    pyrToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToGeneral.image = vr->hizImage;
    pyrToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pyrToGeneral.subresourceRange.levelCount = vr->hizMipCount;
    pyrToGeneral.subresourceRange.layerCount = 1;
    pyrToGeneral.srcAccessMask = 0;                        // WAR: execution-only (prior reads make nothing available)
    pyrToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &pyrToGeneral);

    // mip 0 = reduce (impl 0, resolved/MSAA depth); mip k = downsample (impl 1, mip k-1). One barrier
    // between mips so mip k-1's writes are visible to mip k's reads. PC matches hiz.comp's block.
    VkPipelineLayout hizLayout = rendererState.prototypes[PIPELINE_COMPUTE_HIZ].layout;
    for (uint32_t m = 0; m < vr->hizMipCount; m++) {
        uint32_t dstW = vr->hizWidth  >> m; if (dstW < 1u) dstW = 1u;
        uint32_t dstH = vr->hizHeight >> m; if (dstH < 1u) dstH = 1u;
        struct { int32_t srcMip, pad, dstW, dstH, srcW, srcH; } pc;
        pc.srcMip = (int32_t)m - 1; pc.pad = 0;
        pc.dstW = (int32_t)dstW; pc.dstH = (int32_t)dstH;
        if (m == 0u) {
            pc.srcW = (int32_t)viewExtent.width;
            pc.srcH = (int32_t)viewExtent.height;
        } else {
            uint32_t sw = vr->hizWidth  >> (m - 1u); if (sw < 1u) sw = 1u;
            uint32_t sh = vr->hizHeight >> (m - 1u); if (sh < 1u) sh = 1u;
            pc.srcW = (int32_t)sw; pc.srcH = (int32_t)sh;
        }
        uint32_t impl = (m == 0u) ? 0u : 1u;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hizLayout, 0, 1, &vr->hizSets[m], 0, NULL);
        vkCmdPushConstants(cmd, hizLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (dstW + 7u) / 8u, (dstH + 7u) / 8u, 1u);

        if (m + 1u < vr->hizMipCount) {
            VkMemoryBarrier mipBarrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &mipBarrier, 0, NULL, 0, NULL);
        }
    }

    // pyramid -> SHADER_READ (the cull samples it; step B).
    VkImageMemoryBarrier pyrToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    pyrToRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    pyrToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pyrToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToRead.image = vr->hizImage;
    pyrToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pyrToRead.subresourceRange.levelCount = vr->hizMipCount;
    pyrToRead.subresourceRange.layerCount = 1;
    pyrToRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    pyrToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &pyrToRead);
}

// Async Hi-Z build CB (review finding 2): both views' pyramid chains for this frame slot, recorded
// fresh each frame (drawFrame host-waits hizTimeline before reuse). Submitted to ctx.computeQueue
// after the graphics submit — waits gfxTimeline == this frame's ordinal (depth resolves done, prior
// culls of the slots being rewritten retired), signals hizTimeline == the same ordinal (waited by
// the ordinal+2 graphics submit). No depth barriers here: the resolved depth was flipped to
// SHADER_READ on the graphics timeline before its signal, and semaphores carry the memory
// dependency both ways.
static void recordHiZCompute(uint32_t frameIndex)
{
    VkCommandBuffer cmd = rendererState.frames[frameIndex].computeCommandBuffer;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &beginInfo);
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
        ViewResources* vr = &rendererState.frames[frameIndex].views[v];
        record_hiz_pyramid_chain(cmd, vr, rendererState.viewExtent[v]);
    }
    vkEndCommandBuffer(cmd);
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
        SlotUpload* ups[7] = {
            &rendererState.initialTransformBuffer, &rendererState.motionBuffer,
            &rendererState.instanceDataBuffer, &rendererState.lightBuffer,
            &rendererState.culling.entity,
            &rendererState.shadowConfig, &rendererState.shadowInfo, // runtime shadow caster lifecycle
        };
        uint32_t fi = rendererState.frameIndex;
        bool any = false;
        for (int u = 0; u < 7; u++) if (ups[u]->staged[fi]) { any = true; break; }
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
            for (int u = 0; u < 7; u++) slot_upload_flush(cmd, ups[u], fi);
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
        uint32_t lightCount = rendererState.lightBuffer.count; // active light rows (lightsetup dispatch size)
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
                    cmdStride * rendererState.indirectBuffer.capacity * ano_draw_partition_count(), 0);
                vkCmdFillBuffer(cmd, rendererState.culling.drawCountBuffer[rendererState.frameIndex], 0,
                    sizeof(uint32_t) * ano_draw_partition_count(), 0);

                VkMemoryBarrier fillBarrier = {};
                fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &fillBarrier, 0, NULL, 0, NULL);

                // Hi-Z occlusion (review 4.9 step 3): the cull samples binding 11 = the PREVIOUS frame-
                // in-flight slot's pyramids (built last frame). Order that build's writes before this
                // cull reads them (no layout change — they rest in SHADER_READ). First frame: the prev
                // slot was never built but is seeded to SHADER_READ, so the barrier is a harmless no-op.
                // Async build (review finding 2): the writes happened on the compute queue; this
                // submit's hizTimeline wait already made them visible, so no barrier is recorded.
                if (!rendererState.asyncHiz) {
                    uint32_t hizPrevSlot = (rendererState.frameIndex + MAX_FRAMES_IN_FLIGHT - 1u) % MAX_FRAMES_IN_FLIGHT;
                    VkImageMemoryBarrier hizRead[ANO_VIEW_COUNT] = {};
                    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
                        hizRead[v].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        hizRead[v].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        hizRead[v].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        hizRead[v].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        hizRead[v].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        hizRead[v].image = rendererState.frames[hizPrevSlot].views[v].hizImage;
                        hizRead[v].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        hizRead[v].subresourceRange.levelCount = rendererState.frames[hizPrevSlot].views[v].hizMipCount;
                        hizRead[v].subresourceRange.layerCount = 1;
                        hizRead[v].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        hizRead[v].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    }
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, NULL, 0, NULL, ANO_VIEW_COUNT, hizRead);
                }
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.prototypes[pass->prototype].implementations[0].pipeline);

            VkDescriptorSet set =
                pass->prototype == PIPELINE_COMPUTE_UPDATE      ? rendererState.frames[rendererState.frameIndex].updateSet :
                pass->prototype == PIPELINE_COMPUTE_SCATTER     ? rendererState.frames[rendererState.frameIndex].scatterSet :
                pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP ? rendererState.frames[rendererState.frameIndex].shadow.setupSet :
                pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP  ? rendererState.frames[rendererState.frameIndex].lightsetupSet :
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
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &lightCount);
            }

            uint32_t dispatchX;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                dispatchX = (ANO_SHADOW_FRUSTUM_COUNT + 63u) / 64u; // one invocation per shadow frustum
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                dispatchX = (lightCount + 63u) / 64u; // one invocation per light (local_size_x = 64)
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
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                // Per-light world pose is read by the fragment passes (flat/transmission set 0 binding 12).
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else if (pass->prototype == PIPELINE_COMPUTE_UPDATE || pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                // update -> scatter is a WAW on streamed slots (scatter must win); both -> cull is a read.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else {
                // The cull pass feeds the indirect commands (DRAW_INDIRECT) and the compacted/entity
                // SSBOs read by the geometry stage (mesh or vertex) AND the transparency-sort compute
                // pass (tpsort), which reads the compacted draws + depth keys and rewrites the
                // transmission partition. So the barrier reaches COMPUTE as well as DRAW_INDIRECT|geom,
                // and dst includes SHADER_WRITE (tpsort overwrites cull's writes — WAW).
                VkPipelineStageFlags geomStage = ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            }
        }
    }

    uint32_t drawSlotCount = ano_draw_pipeline_count();

    ano_ts(cmd, ANO_TS_AFTER_COMPUTE);

    // === Layered Power CDF shadow render + separable prefilter ===
    // Three phases: (1) render each active frustum's nearest occluder as a one-hot (coverage=1, M=z)
    // in its depth band, MRT into the frustum's two atlas sublayers, depth-tested against the
    // frustum's own transient depth slice; (2) box blur-X atlas -> temp; (3) blur-Y temp -> atlas.
    // The lighting frags reconstruct occlusion as cumulative per-band coverage. Per frustum s the two
    // sublayers are the contiguous atlas layers [s*SUBLAYERS, s*SUBLAYERS+SUBLAYERS). (audit 4.7)
    //
    // Synchronization is per PHASE, not per layer: a barrier's execution scope is stage-global (not
    // scoped to the image it names), so the old per-layer barriers serialized all ~130 micro-passes
    // end-to-end. Passes within a phase touch disjoint subresources (own sublayers, own depth slice)
    // and need no mutual ordering; four whole-array barriers fence the phase boundaries. Command-
    // buffer state (pipeline, viewport, sets) persists across rendering instances: bound once per
    // phase, not per pass.
    if (entityCount > 0) {
        ShadowResources* sh = &rendererState.frames[rendererState.frameIndex].shadow;
        bool useMeshS = ctx.deviceCapabilities.meshShader;
        uint32_t maxDrawsS = rendererState.indirectBuffer.capacity;
        VkShaderStageFlags pcStageS = useMeshS ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
        const ShadowFrustumConfig* shadowCfgs = rendererState.shadowCfgMirror; // render-thread mirror (device copy not host-readable)

        VkViewport shVp = { 0.0f, 0.0f, (float)ANO_SHADOW_DIM, (float)ANO_SHADOW_DIM, 0.0f, 1.0f };
        VkRect2D   shSc = { .offset = {0, 0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } };
        VkClearValue clearStats = {}; // all bands empty: coverage 0 (no occluder in this texel) -> lit
        VkClearValue clearDepth = {}; clearDepth.depthStencil.depth = 1.0f;
        VkClearValue clearMRT[2] = { clearStats, clearStats }; // both sublayers

        // Frustums rendered this frame (review finding 8): active (spare/runtime-freed slots skip),
        // shadow-mapped under the lighting mode (a layer carried by radiance cascades renders no
        // caster), and DIRTY — the shared atlas persists per-frustum content, so a clean frustum
        // skips its depth render + blur and its layers just ride the whole-array transitions
        // (content-preserving). Dirty = layer invalid (never built, or its light attached/detached/
        // changed — scoped hooks) or a conservative epoch: scene mutation staged this frame,
        // streamed transforms, or any live parametric mover (GPU-side animation moves casters and
        // lights the CPU cannot see). A clean layer stays consistent with this frame's frustumBuffer
        // because its light is unchanged: shadowsetup rewrote the identical viewProj. Cached layers
        // keep their render-time LOD (camera-driven LOD drift does not invalidate; bounded
        // silhouette staleness). Freeze mode renders only never-built layers (the static-scene
        // ceiling); force-dirty pins the pre-cache behavior. When nothing renders, the whole region
        // — all four phase barriers included — is skipped: the atlas rests in SHADER_READ.
        // maxSub bounds the layered blur's layerCount to the rendered prefix.
        bool epochDirty = rendererState.shadowCacheMode == 1u
                       || (rendererState.shadowCacheMode == 0u
                           && (rendererState.shadowGlobalDirty
                               || rendererState.motionActiveCount > 0u
                               || rendererState.transformStream.count[rendererState.frameIndex] > 0u));
        if (epochDirty)
            for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) rendererState.shadowLayerValid[s] = false;
        rendererState.shadowGlobalDirty = false;

        bool renderS[ANO_SHADOW_FRUSTUM_COUNT];
        uint32_t renderCount = 0u, maxSub = 0u;
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            bool active = shadowCfgs[s].active && lightTypeShadowMapped(shadowCfgs[s].lightType, rendererState.lightingMode);
            renderS[s] = active && !rendererState.shadowLayerValid[s];
            if (renderS[s]) { renderCount++; maxSub = (s + 1u) * ANO_SHADOW_ATLAS_SUBLAYERS; }
        }

        // Phase barrier 1: atlas SHADER_READ -> COLOR (whole array, CONTENT PRESERVED — clean
        // frustums keep their layers; dirty ones re-render with loadOp CLEAR), temp UNDEFINED ->
        // COLOR (blur intermediate, content never crosses frames), transient depth UNDEFINED ->
        // DEPTH_ATTACHMENT (whole array). All three are shared across frames in flight: the
        // FRAGMENT source scope orders prior in-flight frames' atlas/temp reads (lighting frags,
        // blur) and EARLY|LATE the prior depth-slice use — the cross-frame WARs the per-frame
        // fence doesn't cover (same pattern as the Hi-Z pyramid rewrite below).
        if (renderCount > 0u) {
            VkImageMemoryBarrier pre[3];
            pre[0] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowAtlasImage, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            pre[1] = pre[0];
            pre[1].image = rendererState.shadowTempImage;
            pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // discard: repopulated by blur-X each use
            pre[2] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowDepthImage,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, ANO_SHADOW_FRUSTUM_COUNT } };
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, NULL, 0, NULL, 3, pre);
        }

        // --- Phase 1: per-frustum depth render (MRT into the frustum's two sublayers) ---
        // Disjoint targets across frustums: no inter-pass barriers, the renders may overlap.
        if (renderCount > 0u) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowPipeline);
            vkCmdSetViewport(cmd, 0, 1, &shVp);
            vkCmdSetScissor(cmd, 0, 1, &shSc);

            // Shadow pipeline reuses the FLAT layout (sets 0/1/2). Set 0 = view 0's global set; set 2 =
            // the shadow set (viewProjs). Bindless (set 1) is unused here but bound for compatibility.
            VkPipelineLayout flatLayout = rendererState.prototypes[PIPELINE_FLAT].layout;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 0, 1,
                &rendererState.frames[rendererState.frameIndex].views[0].globalSet, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 1, 1,
                &rendererState.bindlessTextures.set, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 2, 1,
                &sh->geomSet, 0, NULL);

            VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
            VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
            if (!useMeshS)
                vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                if (!renderS[s]) continue;
                uint32_t subBase = s * ANO_SHADOW_ATLAS_SUBLAYERS; // first of the frustum's 2 contiguous sublayers

                VkRenderingAttachmentInfo colorAtt[ANO_SHADOW_ATLAS_SUBLAYERS];
                for (uint32_t a = 0; a < ANO_SHADOW_ATLAS_SUBLAYERS; a++)
                    colorAtt[a] = (VkRenderingAttachmentInfo){ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = rendererState.shadowAtlasLayerView[subBase + a], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clearMRT[a] };
                VkRenderingAttachmentInfo depthAtt = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = rendererState.shadowDepthSliceView[s], .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .clearValue = clearDepth };
                VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                    .layerCount = 1, .colorAttachmentCount = ANO_SHADOW_ATLAS_SUBLAYERS, .pColorAttachments = colorAtt, .pDepthAttachment = &depthAtt };
                vkCmdBeginRendering(cmd, &ri);

                // Shadow partitions are slot-0-only; the partition index is base + s, not by draw slot.
                uint32_t partition = ANO_VIEW_COUNT * drawSlotCount + s;
                uint32_t pcVals[2] = { partition * rendererState.culling.maxEntities, s }; // baseOffset, shadowFrustumIndex
                vkCmdPushConstants(cmd, flatLayout, pcStageS, 0, sizeof(pcVals), pcVals);

                VkDeviceSize countOffset = (VkDeviceSize)partition * sizeof(uint32_t);
                if (useMeshS) {
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    if (ctx.deviceCapabilities.drawIndirectCount)
                        pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    else
                        pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                } else {
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawIndexedIndirectCommand);
                    if (ctx.deviceCapabilities.drawIndirectCount)
                        vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawIndexedIndirectCommand));
                    else
                        vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawIndexedIndirectCommand));
                }
                vkCmdEndRendering(cmd);
            }
        }

        // Phase barrier 2: whole atlas COLOR -> SHADER_READ for the blur-X sample — the only
        // ordering the depth renders need. Clean/inactive layers ride along (content preserved) so
        // the sampled array view sees one uniform layout.
        if (renderCount > 0u) {
            VkImageMemoryBarrier toRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &toRead);
        }

        // --- Phases 2 & 3: separable box blur (atlas -> temp -> atlas) over active sublayers ---
        // Every channel (per-band coverage / coverage*mean) is linearly filterable, so the 2D
        // footprint reduces to two 1D box passes exactly; the coverage gradient across a silhouette
        // is the penumbra. One layered render pass per direction when the device has vertex-stage
        // gl_Layer (shadowblur.vert routes the push-constant layer); else one single-layer pass per
        // active sublayer. Both paths are barrier-free within a direction: target layers are
        // disjoint and the common source is read-only.
        bool layeredBlur = ctx.deviceCapabilities.shaderOutputLayer;
        struct { float dir[2]; int32_t layer; int32_t pad; } bpc = {0};
        float invDim = 1.0f / (float)ANO_SHADOW_DIM;
        // The push spans both stages: shadowblur.vert reads layer, shadowblur.frag reads dir+layer
        // (the fallback vertex stage reads nothing; the range still covers it).
        const VkShaderStageFlags blurPcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        for (int pass = 0; renderCount > 0u && pass < 2; pass++) {
            VkImageView     dstArr = pass == 0 ? rendererState.shadowTempArrayView : rendererState.shadowAtlasArrayView;
            VkImageView*    dstLyr = pass == 0 ? rendererState.shadowTempLayerView : rendererState.shadowAtlasLayerView;
            VkDescriptorSet srcSet = pass == 0 ? sh->blurAtlasSet : sh->blurTempSet; // src = atlas (X) / temp (Y)
            bpc.dir[0] = pass == 0 ? invDim : 0.0f;
            bpc.dir[1] = pass == 0 ? 0.0f   : invDim;

            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowBlurPipeline);
                vkCmdSetViewport(cmd, 0, 1, &shVp);
                vkCmdSetScissor(cmd, 0, 1, &shSc);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowBlurLayout,
                    0, 1, &srcSet, 0, NULL);

                if (layeredBlur) {
                    // One pass over the array prefix [0, maxSub); each draw routes to its sublayer
                    // via gl_Layer. Undrawn (inactive) layers load DONT_CARE and are never sampled.
                    VkRenderingAttachmentInfo bColor = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = dstArr, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
                    VkRenderingInfo bri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                        .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                        .layerCount = maxSub, .colorAttachmentCount = 1, .pColorAttachments = &bColor };
                    vkCmdBeginRendering(cmd, &bri);
                    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                        if (!renderS[s]) continue;
                        for (uint32_t sub = 0; sub < ANO_SHADOW_ATLAS_SUBLAYERS; sub++) {
                            bpc.layer = (int32_t)(s * ANO_SHADOW_ATLAS_SUBLAYERS + sub);
                            vkCmdPushConstants(cmd, rendererState.shadowBlurLayout, blurPcStages, 0, sizeof(bpc), &bpc);
                            vkCmdDraw(cmd, 3, 1, 0, 0);
                        }
                    }
                    vkCmdEndRendering(cmd);
                } else {
                    // Fallback: one single-layer pass per rendered sublayer, still no interleaved
                    // barriers (the win was never the pass count; it was the stage-global drains).
                    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                        if (!renderS[s]) continue;
                        for (uint32_t sub = 0; sub < ANO_SHADOW_ATLAS_SUBLAYERS; sub++) {
                            uint32_t ss = s * ANO_SHADOW_ATLAS_SUBLAYERS + sub;
                            VkRenderingAttachmentInfo bColor = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                .imageView = dstLyr[ss], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
                            VkRenderingInfo bri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                                .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &bColor };
                            vkCmdBeginRendering(cmd, &bri);
                            bpc.layer = (int32_t)ss;
                            vkCmdPushConstants(cmd, rendererState.shadowBlurLayout, blurPcStages, 0, sizeof(bpc), &bpc);
                            vkCmdDraw(cmd, 3, 1, 0, 0);
                            vkCmdEndRendering(cmd);
                        }
                    }
                }
            }

            if (pass == 0) {
                // Phase barrier 3: temp COLOR -> SHADER_READ (blur-Y samples it) and atlas
                // SHADER_READ -> COLOR (blur-Y overwrites it; WAR against blur-X's reads, so the
                // execution dependency FRAGMENT -> COLOR is what matters). One call, both images.
                VkImageMemoryBarrier xy[2];
                xy[0] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowTempImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                xy[1] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0, 0, NULL, 0, NULL, 2, xy);
            } else {
                // Phase barrier 4: atlas COLOR -> SHADER_READ (its rest state) for the lighting frags.
                VkImageMemoryBarrier fin = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &fin);
            }
        }

        // Rendered layers are now faithful to their light + this frame's scene.
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
            if (renderS[s]) rendererState.shadowLayerValid[s] = true;
    }

    ano_ts(cmd, ANO_TS_AFTER_SHADOW);

    // === Transparency sort: reorder each camera view's transmission partition back-to-front ===
    // tpsort.comp reads cull's compacted draws + per-draw depth keys and rewrites the transmission
    // partition in place so the "over" blend in the per-view transmission pass composites farthest-
    // first instead of in cull's arbitrary atomic-append order. One workgroup per camera view. Runs
    // after cull (whose post-barrier now reaches COMPUTE) and before the per-view geometry passes.
    if (entityCount > 0 && ano_draw_slot_of(PIPELINE_TRANSMISSION) != ANO_NO_DRAW_SLOT) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].layout, 0, 1,
            &rendererState.frames[rendererState.frameIndex].cullSet, 0, NULL);
        vkCmdDispatch(cmd, ANO_VIEW_COUNT, 1, 1); // one workgroup per camera view

        // Sort writes (compacted indices + indirect commands) -> the geometry stage's indirect +
        // SSBO reads in the per-view transmission pass below.
        VkPipelineStageFlags geomStage = ctx.deviceCapabilities.meshShader
            ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        VkMemoryBarrier sortBarrier = {};
        sortBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        sortBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sortBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage,
            0, 1, &sortBarrier, 0, NULL, 0, NULL);
    }

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

        // MSAA color + id targets are PER VIEW (review finding 6): no attachment is shared across
        // views, so no inter-view reuse barrier — the views' raster is free to overlap.

        for (int p = 0; p < (int)(sizeof(g_framePasses)/sizeof(g_framePasses[0])); p++) {
            const RenderPassDef* pass = &g_framePasses[p];
            if (pass->type != PASS_GRAPHICS) continue;

            // Depth write->read hazard: the opaque pass EQUAL-tests the depth the pre-pass just wrote
            // into this same image. Order the pre-pass's writes before this pass's reads/tests.
            if (pass->depthBarrierBefore) {
                VkImageMemoryBarrier depthWaw = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = vr->depthImage, .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, NULL, 0, NULL, 1, &depthWaw);
            }

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            VkClearValue clearDepth = {};
            clearDepth.depthStencil.depth = 1.0f;
            clearDepth.depthStencil.stencil = 0;

            // color[0] = HDR; color[1] = R32_UINT picking id (only the opaque pass declares 2).
            VkRenderingAttachmentInfo color[2] = {};
            color[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color[0].imageView = rendererState.colorView[v]; // this view's MSAA color
            color[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color[0].resolveMode = pass->resolveMode;
            if (pass->resolveMode != VK_RESOLVE_MODE_NONE) {
                color[0].resolveImageView = vr->hdrColorView; // resolve into this view's HDR target
                color[0].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            color[0].loadOp = pass->colorLoadOp;
            color[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color[0].clearValue = clearColor;

            if (pass->colorAttachmentCount == 2) {
                // Picking id: render the slot into the shared MSAA id image; clear to the no-hit
                // sentinel. Integer formats MUST resolve SAMPLE_ZERO (never AVERAGE). Only view 0
                // resolves to a readable single-sample target; other views render then discard it.
                color[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                color[1].imageView = rendererState.pickIdView[v];
                color[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                // CLEAR only with the first opaque pass; the two-sided lane LOADs its ids (finding 7).
                color[1].loadOp = pass->colorLoadOp;
                color[1].clearValue.color.uint32[0] = 0xFFFFFFFFu;
                if (v == 0) {
                    color[1].resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                    color[1].resolveImageView = vr->pickIdResolveView;
                    color[1].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                } else {
                    color[1].resolveMode = VK_RESOLVE_MODE_NONE;
                    color[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }
            }

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
            renderingInfo.renderArea.extent = rendererState.viewExtent[v];
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = pass->colorAttachmentCount;
            renderingInfo.pColorAttachments = color;
            renderingInfo.pDepthAttachment = &depthAttachment;
            renderingInfo.pStencilAttachment = NULL;

            vkCmdBeginRendering(cmd, &renderingInfo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.prototypes[pass->prototype].implementations[pass->implementationIndex].pipeline);

            VkViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)(rendererState.viewExtent[v].width);
            viewport.height = (float)(rendererState.viewExtent[v].height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor = {};
            scissor.offset = (VkOffset2D){0, 0};
            scissor.extent = rendererState.viewExtent[v];
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

        // Picking (audit 3.1): copy the cursor texel from view 0's resolved id image into this
        // frame's readback buffer; ano_collect_pick reads it after this slot's fence next time round.
        // Skip on a degenerate extent (minimized).
        if (v == 0 && rendererState.imageExtent.width > 0 && rendererState.imageExtent.height > 0) {
            VkImageMemoryBarrier toSrc = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vr->pickIdResolveImage,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &toSrc);

            float fx = rendererState.cursorX < 0.0f ? 0.0f : rendererState.cursorX;
            float fy = rendererState.cursorY < 0.0f ? 0.0f : rendererState.cursorY;
            uint32_t cx = (uint32_t)fx, cy = (uint32_t)fy;
            if (cx >= rendererState.imageExtent.width)  cx = rendererState.imageExtent.width - 1u;
            if (cy >= rendererState.imageExtent.height) cy = rendererState.imageExtent.height - 1u;
            VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageOffset = { (int32_t)cx, (int32_t)cy, 0 }, .imageExtent = { 1, 1, 1 } };
            vkCmdCopyImageToBuffer(cmd, vr->pickIdResolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                rendererState.frames[rendererState.frameIndex].pickReadback, 1, &region);

            VkImageMemoryBarrier toColor = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vr->pickIdResolveImage,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, NULL, 0, NULL, 1, &toColor);
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

        // Hi-Z occlusion pyramid build (review 4.9 step 3). Reduce this view's MSAA depth into mip 0,
        // then MAX-downsample the chain (the cull samples it next frame; single-phase). Standard ZO
        // depth -> MAX reduction (farthest occluder). Depth -> SHADER_READ for the reduce and restored
        // to DEPTH_ATTACHMENT after, so next frame's geometry pass finds it in the expected layout.
        {
            // Reduce source setup. Avenue 1 (depthMaxResolve): fixed-function MAX-resolves this view's
            // MSAA depth into the single-sample depthResolveImage (farthest sample = conservative
            // occluder), which the reduce reads as a sampler2D (one fetch/texel). Fallback: transition the
            // MSAA depth to SHADER_READ and read it per-sample (sampler2DMS).
            if (ctx.deviceCapabilities.depthMaxResolve) {
                // (A) order the geometry passes' depth writes before the resolve reads the MSAA depth
                // (both use vr->depthImage as a depth attachment across separate rendering instances).
                // Layout stays DEPTH_ATTACHMENT: the resolve reads it in place; the reduce never touches it.
                VkImageMemoryBarrier depWaw = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = vr->depthImage, .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    0, 0, NULL, 0, NULL, 1, &depWaw);

                // Async build (review finding 2): the resolve target rests in SHADER_READ (the
                // compute-queue reduce consumes it there), so flip it to DEPTH_ATTACHMENT for this
                // frame's resolve write. srcStage EARLY_FRAGMENT_TESTS (srcAccess 0) chains this
                // transition after the submit's hizTimeline wait — the same stage is in its
                // pWaitDstStageMask — which carries the cross-queue WAR against the compute reduce
                // that last read this slot's image. Sync path: it already rests in DEPTH_ATTACHMENT.
                if (rendererState.asyncHiz) {
                    VkImageMemoryBarrier resPre = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    resPre.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    resPre.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    resPre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resPre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resPre.image = vr->depthResolveImage;
                    resPre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    resPre.subresourceRange.levelCount = 1;
                    resPre.subresourceRange.layerCount = 1;
                    resPre.srcAccessMask = 0;              // WAR: execution-only
                    resPre.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &resPre);
                }

                // Dedicated depth-resolve pass (no color, no draws): the store/resolve phase writes
                // depthResolveImage. It rests in DEPTH_ATTACHMENT (sync: seeded at creation, restored
                // after each build; async: flipped above), so no further pre-transition is needed.
                VkRenderingAttachmentInfo rDepth = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                rDepth.imageView = vr->depthView;                       // MSAA source (in DEPTH_ATTACHMENT)
                rDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                rDepth.resolveMode = VK_RESOLVE_MODE_MAX_BIT;
                rDepth.resolveImageView = vr->depthResolveView;
                rDepth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                rDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                rDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;      // MSAA depth not needed after resolve
                VkRenderingInfo rInfo = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
                rInfo.renderArea.offset = (VkOffset2D){0, 0};
                rInfo.renderArea.extent = rendererState.viewExtent[v];
                rInfo.layerCount = 1;
                rInfo.colorAttachmentCount = 0;
                rInfo.pDepthAttachment = &rDepth;
                vkCmdBeginRendering(cmd, &rInfo);
                vkCmdEndRendering(cmd);

                // (C) resolved depth DEPTH_ATTACHMENT -> SHADER_READ for the reduce.
                VkImageMemoryBarrier resToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                resToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                resToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                resToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                resToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                resToRead.image = vr->depthResolveImage;
                resToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                resToRead.subresourceRange.levelCount = 1;
                resToRead.subresourceRange.layerCount = 1;
                resToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                resToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &resToRead);
            } else {
                // depth DEPTH_ATTACHMENT -> SHADER_READ (the reduce reads it as a sampler2DMS)
                VkImageMemoryBarrier depToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                depToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depToRead.image = vr->depthImage;
                depToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                depToRead.subresourceRange.levelCount = 1;
                depToRead.subresourceRange.layerCount = 1;
                depToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &depToRead);
            }

            if (rendererState.asyncHiz) {
                // Async build (review finding 2): the pyramid chain (record_hiz_pyramid_chain) is
                // recorded into this frame's compute CB instead and runs on ctx.computeQueue once
                // this submit signals gfxTimeline — overlapping the next frame's graphics rather
                // than serializing between views here. The resolved depth stays SHADER_READ (its
                // async rest state) for the compute reduce; the pre-resolve flip above returns it
                // to DEPTH_ATTACHMENT when this slot comes round again.
            } else {
                // In-frame build. The pre-chain WAR (srcStage=COMPUTE inside the helper) waits, in
                // submission order, on a prior frame's cull that may still be reading this slot via
                // binding 11 (read one frame after it is built, rewritten two frames later — a
                // cross-frame WAR the per-frame fence doesn't cover, review 4.9 step 3).
                record_hiz_pyramid_chain(cmd, vr, rendererState.viewExtent[v]);

                // Restore depth to DEPTH_ATTACHMENT for next frame's geometry/resolve pass.
                if (ctx.deviceCapabilities.depthMaxResolve) {
                    // Avenue 1: the MSAA depthImage was never moved (it stayed a depth attachment); restore
                    // the resolved depth SHADER_READ -> DEPTH_ATTACHMENT instead, for next frame's resolve.
                    VkImageMemoryBarrier resRestore = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    resRestore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    resRestore.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    resRestore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resRestore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resRestore.image = vr->depthResolveImage;
                    resRestore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    resRestore.subresourceRange.levelCount = 1;
                    resRestore.subresourceRange.layerCount = 1;
                    resRestore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    resRestore.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &resRestore);
                } else {
                    VkImageMemoryBarrier depRestore = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    depRestore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    depRestore.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depRestore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    depRestore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    depRestore.image = vr->depthImage;
                    depRestore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    depRestore.subresourceRange.levelCount = 1;
                    depRestore.subresourceRange.layerCount = 1;
                    depRestore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    depRestore.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &depRestore);
                }
            }
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
	printf("Live render slots: %u\n", rendererState.slots.slotHighWater); // scene is logic-composed; entities[] is gone
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
	// Deprecated: transforms are now device-local. A renderable's base pose arrives per-slot via
	// RCMD_CREATE -> stage_command_fields -> initialTransformBuffer; update.comp derives the live one.
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
        // Screen-area cull knobs (review 4.9 step 1). scale = cot(fovY/2) * half viewport height in
        // px: |proj[1][1]| is cot(fovY/2) (negative under the Vulkan Y-flip), so rpx = worldRadius *
        // scale / dist. Threshold is squared here so the shader compares without a sqrt; 0 disables.
        float screenAreaScale = fabsf(viewUbo->proj[1][1]) * 0.5f * viewUbo->screenHeight;
        float threshold = state->cullPixelThreshold[v];
        ubo->viewCullParams[v][0] = screenAreaScale;
        ubo->viewCullParams[v][1] = threshold * threshold;
        ubo->viewCullParams[v][2] = state->lodPixelThreshold[v]; // LOD level-1 onset (review 4.9 step 2)
        ubo->viewCullParams[v][3] = (float)state->lodBias;       // global LOD bias (debug/tuning), replicated per view
        // Hi-Z occlusion (review 4.9 step 3): publish the viewProj the sampled pyramid was rendered
        // with, for reprojection — the slot `lag` frames back (1 = in-frame build, 2 = async build:
        // review finding 2) — then save this frame's into its own slot. hizParams.z = mipCount only
        // when this view's test is enabled AND past the post-(re)create warmup (hizValidOrdinal =
        // every sampled slot rebuilt at the current resolution); else 0 = test off (the default).
        // hizProj = the projection terms the cull needs (screen radius from proj00/proj11, ZO
        // nearest-depth from proj22/proj32).
        uint32_t hizLag = state->asyncHiz ? 2u : 1u;
        uint32_t histSlot = (frameIndex + MAX_FRAMES_IN_FLIGHT - hizLag) % MAX_FRAMES_IN_FLIGHT;
        bool hizWarm = state->timelineOrdinal + 1u >= state->hizValidOrdinal;
        memcpy(ubo->prevViewProj[v], state->viewProjHist[histSlot][v], sizeof(mat4));
        memcpy(state->viewProjHist[frameIndex][v], ubo->views[v].viewProj, sizeof(mat4));
        ubo->hizParams[v][0] = (float)state->frames[frameIndex].views[v].hizWidth;
        ubo->hizParams[v][1] = (float)state->frames[frameIndex].views[v].hizHeight;
        ubo->hizParams[v][2] = (state->hizEnable[v] && hizWarm) ? (float)state->frames[frameIndex].views[v].hizMipCount : 0.0f;
        ubo->hizParams[v][3] = 0.0f;
        ubo->hizProj[v][0] = viewUbo->proj[0][0];
        ubo->hizProj[v][1] = viewUbo->proj[1][1];
        ubo->hizProj[v][2] = viewUbo->proj[2][2];
        ubo->hizProj[v][3] = viewUbo->proj[3][2];
        // Publish the active light count to each view's fragment stage.
        viewUbo->lightCount = state->lightBuffer.count;
        // Publish the runtime lighting mode + debug selector (RADIANCE_CASCADES.md). The fragment
        // stage gates per-light shadow sampling on this; the shadow depth render is gated to match.
        viewUbo->lightingMode = state->lightingMode;
        viewUbo->debugView = state->debugView;
    }

    // Publish the live view-0 camera to the logic master (audit 4.11 / 7.1): the gameplay camera +
    // viewport it needs for picking rays and attention-driven LOD. Done here because this is where
    // this frame's view-0 viewProj + frustum planes are resolved; invViewProj unprojects a cursor
    // texel to a world ray. Latest-wins, lock-free (logic never laps the once-per-frame producer).
    {
        RenderSnapshot snap;
        memcpy(snap.viewProj, ubo->views[0].viewProj, sizeof(mat4));
        if (!invertMat4(snap.invViewProj, ubo->views[0].viewProj))
            memcpy(snap.invViewProj, ubo->views[0].viewProj, sizeof(mat4)); // singular (degenerate camera): publish a harmless placeholder
        memcpy(snap.frustum, ubo->views[0].frustumPlanes, sizeof snap.frustum);
        snap.vpWidth  = state->imageExtent.width;
        snap.vpHeight = state->imageExtent.height;
        snap.frameId  = state->globalFrame;
        ano_render_publish_snapshot(&state->bridge, &snap);
    }

    ubo->viewCount = ANO_VIEW_COUNT;
    ubo->entityCount = entityCount;
    ubo->maxEntities = state->culling.maxEntities;
    ubo->drawSlotCount = ano_draw_pipeline_count();
    ubo->shadowLodBias = state->shadowLodBias; // shadow LOD offset (review 4.9 step 2, revised): relative to the view-0 LOD

    // Publish the PipelineType -> draw-slot map cull.comp compacts by. Frame-invariant, but
    // rewritten here so each frame's UBO (incl. a freshly grown one) is always populated.
    for (uint32_t t = 0; t < 16u; ++t)
        ubo->drawSlotOf[t] = (t < PIPELINE_TYPE_COUNT) ? ano_draw_slot_of((PipelineType)t) : ANO_NO_DRAW_SLOT;

    // Special lanes the cull pass branches on: additive (slot x) emits no shadow caster, transmission
    // (slot y) is the depth-sorted "over" lane (tpsort.comp). ANO_NO_DRAW_SLOT when a lane is absent.
    ubo->specialSlots[0] = ano_draw_slot_of(PIPELINE_ADDITIVE);
    ubo->specialSlots[1] = ano_draw_slot_of(PIPELINE_TRANSMISSION);
    ubo->specialSlots[2] = ANO_NO_DRAW_SLOT;
    ubo->specialSlots[3] = ANO_NO_DRAW_SLOT;

    // The EntitySSBO (mesh/material per slot) is seeded once at init and mutated
    // sparsely through the command bridge (render_apply_commands) — no per-frame
    // O(N) rewrite. MeshSSBO/MeshBoundsSSBO below are per-mesh (bounded by
    // meshCount, not entity count) and refreshed so geometry-pool changes apply.

    // Update MeshSSBO and MeshBoundsSSBO. meshCount can't exceed ANO_MAX_MESHES (the geometry pool
    // refuses to register past it), but clamp defensively: these buffers are fixed-size mapped device
    // memory, so a future cap divergence must never let this loop write past their last slot.
    uint32_t meshCount = state->globalGeometryPool.meshCount;
    if (meshCount > ANO_MAX_MESHES) meshCount = ANO_MAX_MESHES;
    uint32_t* meshData = (uint32_t*)state->culling.meshDataMapped[frameIndex];
    float* meshBounds = (float*)state->culling.meshBoundsMapped[frameIndex];
    
    for(uint32_t i=0; i < meshCount; i++) {
        MeshRegion* mesh = &state->globalGeometryPool.meshes[i];
        
        meshData[i*9 + 0] = mesh->meshletCount;
        meshData[i*9 + 1] = mesh->meshletOffset;
        meshData[i*9 + 2] = mesh->uniqueVerticesOffset;
        meshData[i*9 + 3] = mesh->trianglesOffset;
        meshData[i*9 + 4] = mesh->vertexOffset;
        meshData[i*9 + 5] = mesh->classicIndexCount;       // fallback: VkDrawIndexedIndirectCommand.indexCount
        meshData[i*9 + 6] = mesh->classicIndexOffset / 4;  // fallback: firstIndex (u32 index units)
        meshData[i*9 + 7] = mesh->boundsOffset;            // byte offset of per-meshlet bounds (sphere+cone) in metadata buffer; consumed by flat.mesh cone cull
        meshData[i*9 + 8] = mesh->lodCount;                // review 4.9 step 2: contiguous LOD count (cull reads off the base mesh)

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
    // A create/update light-entity writes the STATIC palette region only; runtime lights take the
    // RCMD_LIGHT_* path. Bound the index so a stray static-region command can't clobber a runtime row.
    if ((fields & RFIELD_LIGHT) && c->light_index < ANO_STATIC_LIGHT_COUNT) {
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
                      (VkDeviceSize)sizeof(uint32_t) * newCap * ano_draw_partition_count(), 0) &&
        growBufferSet(state->indirectBuffer.buffer, state->indirectBuffer.allocs,
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, devProps,
                      cmdStride * newCap * ano_draw_partition_count(), 0) &&
        growBufferSet(state->culling.sortKeysBuffer, state->culling.sortKeysAllocs, ssbo, devProps,
                      (VkDeviceSize)sizeof(float) * (VkDeviceSize)ANO_VIEW_COUNT * newCap, 0);
    // Mover bookkeeping must track every slot (review finding 8): grow in lockstep or fail the create.
    if (ok) {
        uint8_t* nm = (uint8_t*)realloc(state->slotMotion, newCap);
        if (nm) {
            memset(nm + state->slotMotionCap, 0, newCap - state->slotMotionCap);
            state->slotMotion = nm;
            state->slotMotionCap = newCap;
        } else {
            ok = false;
        }
    }
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

// ---------------------------------------------------------------------------
// Runtime light registry (audit 4.7 Phase 3). Render-thread only; no locks. Owns the dynamic light
// palette region [base, base+capacity). See LightRegistry in structs.h.
// ---------------------------------------------------------------------------

static void light_registry_init(LightRegistry* r, uint32_t base, uint32_t capacity, uint32_t framesInFlight) {
    memset(r, 0, sizeof(*r));
    r->base = base;
    r->capacity = capacity;
    r->framesInFlight = framesInFlight;
}

static void light_registry_destroy(LightRegistry* r) {
    free(r->idToRow); free(r->rowState); free(r->rowParent); free(r->rowLightId); free(r->rowMirror);
    free(r->rowShadowBase); free(r->freeRows); free(r->quarantine);
    memset(r, 0, sizeof(*r));
}

// Grow the per-row arrays (state/parent/lightId share rowsCapacity), zero/UNMAPPED-filling the tail.
static bool lr_reserve_rows(LightRegistry* r, uint32_t need) {
    if (need <= r->rowsCapacity) return true;
    uint32_t nc = r->rowsCapacity ? r->rowsCapacity : 16u;
    while (nc < need) nc *= 2u;
    uint8_t*  s  = realloc(r->rowState,   nc);
    uint32_t* pp = realloc(r->rowParent,  (size_t)nc * sizeof(uint32_t));
    uint32_t* li = realloc(r->rowLightId, (size_t)nc * sizeof(uint32_t));
    LightData* mi = realloc(r->rowMirror, (size_t)nc * sizeof(LightData));
    uint32_t* sb = realloc(r->rowShadowBase, (size_t)nc * sizeof(uint32_t));
    if (s)  r->rowState   = s;
    if (pp) r->rowParent  = pp;
    if (li) r->rowLightId = li;
    if (mi) r->rowMirror  = mi;
    if (sb) r->rowShadowBase = sb;
    if (!s || !pp || !li || !mi || !sb) return false;
    for (uint32_t i = r->rowsCapacity; i < nc; i++) {
        s[i] = LIGHT_ROW_FREE; pp[i] = ANO_RENDER_SLOT_UNMAPPED; li[i] = ANO_RENDER_SLOT_UNMAPPED;
        sb[i] = ANO_SHADOW_NONE;
    }
    r->rowsCapacity = nc;
    return true;
}

static bool lr_reserve_ids(LightRegistry* r, uint32_t need) {
    if (need <= r->idCapacity) return true;
    uint32_t nc = r->idCapacity ? r->idCapacity : 16u;
    while (nc < need) {
        if (nc > (0xFFFFFFFFu / 2u)) { nc = need; break; } // cap the doubling so nc cannot wrap to 0
        nc *= 2u;
    }
    uint32_t* p = realloc(r->idToRow, (size_t)nc * sizeof(uint32_t));
    if (!p) return false;
    for (uint32_t i = r->idCapacity; i < nc; i++) p[i] = ANO_RENDER_SLOT_UNMAPPED;
    r->idToRow = p; r->idCapacity = nc;
    return true;
}

static void lr_push_free(LightRegistry* r, uint32_t row) {
    if (r->freeCount >= r->freeCapacity) {
        uint32_t nc = r->freeCapacity ? r->freeCapacity * 2u : 16u;
        uint32_t* p = realloc(r->freeRows, (size_t)nc * sizeof(uint32_t));
        if (!p) return; // OOM: row leaks (stays unused), no corruption
        r->freeRows = p; r->freeCapacity = nc;
    }
    r->freeRows[r->freeCount++] = row;
}

static void lr_push_quarantine(LightRegistry* r, uint32_t row, uint64_t safeFrame) {
    if (r->quarantineCount >= r->quarantineCapacity) {
        uint32_t nc = r->quarantineCapacity ? r->quarantineCapacity * 2u : 16u;
        LightRowQuarantine* p = realloc(r->quarantine, (size_t)nc * sizeof(LightRowQuarantine));
        if (!p) return; // OOM: row stays QUARANTINED forever (never reused), no corruption
        r->quarantine = p; r->quarantineCapacity = nc;
    }
    r->quarantine[r->quarantineCount++] = (LightRowQuarantine){ .row = row, .safeFrame = safeFrame };
}

// Attach: map light_id to a fresh row driven by parentRid. Returns the ABSOLUTE palette row, or
// ANO_RENDER_SLOT_UNMAPPED on capacity exhaustion / OOM / already-mapped id (producer error -> drop).
static uint32_t light_registry_alloc(LightRegistry* r, uint32_t light_id, uint32_t parentRid) {
    if (light_id == ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED; // sentinel reserved; also guards light_id+1u wrap
    if (!lr_reserve_ids(r, light_id + 1u)) return ANO_RENDER_SLOT_UNMAPPED;
    if (r->idToRow[light_id] != ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED; // double-attach
    uint32_t row;
    if (r->freeCount > 0u) {
        row = r->freeRows[--r->freeCount];           // reuse a quarantine-expired hole
    } else {
        if (r->highWater >= r->capacity) return ANO_RENDER_SLOT_UNMAPPED; // palette full
        if (!lr_reserve_rows(r, r->highWater + 1u)) return ANO_RENDER_SLOT_UNMAPPED;
        row = r->highWater++;
    }
    r->rowState[row]     = LIGHT_ROW_LIVE;
    r->rowParent[row]    = parentRid;
    r->rowLightId[row]   = light_id;
    r->idToRow[light_id] = row;
    return r->base + row;
}

static uint32_t light_registry_resolve(const LightRegistry* r, uint32_t light_id) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    return (row == ANO_RENDER_SLOT_UNMAPPED) ? ANO_RENDER_SLOT_UNMAPPED : r->base + row;
}

static uint32_t light_registry_parent_of(const LightRegistry* r, uint32_t light_id) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    return (row == ANO_RENDER_SLOT_UNMAPPED) ? ANO_RENDER_SLOT_UNMAPPED : r->rowParent[row];
}

// Detach one light_id: quarantine its row and unmap the id. Returns the ABSOLUTE row to disable
// (caller stages enabled=0), or ANO_RENDER_SLOT_UNMAPPED if the id was not mapped.
static uint32_t light_registry_detach(LightRegistry* r, uint32_t light_id, uint64_t currentFrame) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    if (row == ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED;
    r->idToRow[light_id] = ANO_RENDER_SLOT_UNMAPPED;
    r->rowState[row]     = LIGHT_ROW_QUARANTINED;
    r->rowLightId[row]   = ANO_RENDER_SLOT_UNMAPPED;
    lr_push_quarantine(r, row, currentFrame + r->framesInFlight);
    return r->base + row;
}

// Detach every light attached to parentRid (parent-DESTROY cascade). Writes up to `max` ABSOLUTE
// rows to disable into out_rows; returns the count. Call in a loop while it returns `max`. O(highWater)
// per call — a sibling-list index is the scale upgrade; runtime light churn is bounded for now.
static uint32_t light_registry_detach_children(LightRegistry* r, uint32_t parentRid,
                                               uint64_t currentFrame, uint32_t* out_rows, uint32_t max) {
    uint32_t n = 0;
    for (uint32_t row = 0; row < r->highWater && n < max; row++) {
        if (r->rowState[row] != LIGHT_ROW_LIVE || r->rowParent[row] != parentRid) continue;
        uint32_t lid = r->rowLightId[row];
        if (lid != ANO_RENDER_SLOT_UNMAPPED && lid < r->idCapacity) r->idToRow[lid] = ANO_RENDER_SLOT_UNMAPPED;
        r->rowState[row]   = LIGHT_ROW_QUARANTINED;
        r->rowLightId[row] = ANO_RENDER_SLOT_UNMAPPED;
        lr_push_quarantine(r, row, currentFrame + r->framesInFlight);
        out_rows[n++] = r->base + row;
    }
    return n;
}

// Return quarantined rows whose safeFrame has elapsed to the free-list for reuse.
static void light_registry_collect(LightRegistry* r, uint64_t currentFrame) {
    uint32_t w = 0;
    for (uint32_t i = 0; i < r->quarantineCount; i++) {
        LightRowQuarantine q = r->quarantine[i];
        if (q.safeFrame <= currentFrame) {
            r->rowState[q.row] = LIGHT_ROW_FREE;
            lr_push_free(r, q.row);
        } else {
            r->quarantine[w++] = q; // not yet safe: keep
        }
    }
    r->quarantineCount = w;
}

// Ascending compare for the free-row sort below. Subtraction would overflow on uint32_t.
static int lr_cmp_u32_asc(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

// High-water compaction: peel the trailing contiguous run of FREE rows off the top so the published
// cull light count (base + highWater) shrinks after a permanent drop in live lights, instead of
// staying pinned at the historical peak. Mirrors render_slots_compact (audit 4.5). Only FREE rows
// peel — QUARANTINED rows still inside their framesInFlight reuse window stay counted (an in-flight
// frame may still read them, and a peel-then-regrow would reuse the index too early). Call AFTER
// collect (so newly-expired rows are FREE) and BEFORE publishing the count. Returns rows reclaimed.
static uint32_t light_registry_compact(LightRegistry* r) {
    if (r->freeCount == 0u) return 0u;
    // Sort ascending so the trailing free run is a suffix; non-trailing holes stay a valid prefix
    // free-list (alloc pops from the end — order is irrelevant to correctness).
    qsort(r->freeRows, r->freeCount, sizeof(uint32_t), lr_cmp_u32_asc);
    uint32_t before = r->highWater;
    // freeCount>0 short-circuits before the highWater-1u read, so highWater==0 can't underflow.
    while (r->freeCount > 0u && r->freeRows[r->freeCount - 1u] == r->highWater - 1u) {
        r->freeCount--;
        r->highWater--;
    }
    return before - r->highWater;
}

// Spot/dir aim into LightData.localDir, defaulting a zero vector to model -Z (reproduces the prior
// -lx[2] forward). The shader normalizes after rotating by the parent, so a non-unit dir is fine.
static void light_set_dir(LightData* L, const float d[3]) {
    bool zero = (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f);
    L->localDir[0] = zero ? 0.0f : d[0];
    L->localDir[1] = zero ? 0.0f : d[1];
    L->localDir[2] = zero ? -1.0f : d[2];
}

// Build a GPU LightData from bridge params + a resolved parent slot (transformIndex) + local offset.
static LightData light_data_from_params(const RenderLightParams* p, uint32_t transformIndex, const float off[3]) {
    LightData L = {0};
    L.color[0] = p->color[0]; L.color[1] = p->color[1]; L.color[2] = p->color[2];
    L.intensity = p->intensity; L.range = p->range;
    L.innerConeCos = p->innerConeCos; L.outerConeCos = p->outerConeCos;
    L.type = (uint32_t)p->type;
    L.transformIndex = transformIndex;
    L.localOffset[0] = off[0]; L.localOffset[1] = off[1]; L.localOffset[2] = off[2];
    light_set_dir(&L, p->localDir);
    L.enabled = 1u;
    return L;
}

// Partial RCMD_LIGHT_UPDATE: merge only the producer fields named in `fields` (ANO_LIGHT_FIELD_*) into
// an existing mirror LightData; unnamed fields keep their current value. transformIndex/enabled are
// render-derived and refreshed by the caller, not here.
static void light_apply_fields(LightData* dst, const RenderLightParams* p, const float off[3], uint32_t fields) {
    if (fields & ANO_LIGHT_FIELD_COLOR)     { dst->color[0] = p->color[0]; dst->color[1] = p->color[1]; dst->color[2] = p->color[2]; }
    if (fields & ANO_LIGHT_FIELD_INTENSITY) dst->intensity = p->intensity;
    if (fields & ANO_LIGHT_FIELD_RANGE)     dst->range = p->range;
    if (fields & ANO_LIGHT_FIELD_CONE)      { dst->innerConeCos = p->innerConeCos; dst->outerConeCos = p->outerConeCos; }
    if (fields & ANO_LIGHT_FIELD_TYPE)      dst->type = (uint32_t)p->type;
    if (fields & ANO_LIGHT_FIELD_OFFSET)    { dst->localOffset[0] = off[0]; dst->localOffset[1] = off[1]; dst->localOffset[2] = off[2]; }
    if (fields & ANO_LIGHT_FIELD_DIRECTION) light_set_dir(dst, p->localDir);
}

// --- Runtime shadow-frustum pools (audit 4.7 budget expansion) ------------------------------------
// Allocate a runtime frustum block base for a casting light (single = 1 frustum for dir/spot, point =
// 6 contiguous cube faces). Returns ANO_SHADOW_NONE when the type's pool is exhausted (light stays
// shadowless — no error). The pool is inferred from the block base on free (slot-range partition).
static uint32_t shadow_frustum_alloc(RendererState* st, uint32_t lightType) {
    if (lightType == LIGHT_TYPE_POINT)
        return st->rtPointFreeCount  ? st->rtPointFree[--st->rtPointFreeCount]   : ANO_SHADOW_NONE;
    return     st->rtSingleFreeCount ? st->rtSingleFree[--st->rtSingleFreeCount] : ANO_SHADOW_NONE;
}
static void shadow_frustum_free(RendererState* st, uint32_t base) {
    if (base == ANO_SHADOW_NONE) return;
    // Bounds-guard the push: alloc/free are balanced so the pool can never legitimately overflow, but
    // a guard turns any future double-free into a dropped no-op instead of a silent OOB write.
    if (base >= ANO_SHADOW_RT_POINT_BASE) {
        if (st->rtPointFreeCount  < ANO_SHADOW_RT_POINT_COUNT)  st->rtPointFree[st->rtPointFreeCount++]   = base;
    } else {
        if (st->rtSingleFreeCount < ANO_SHADOW_RT_SINGLE_COUNT) st->rtSingleFree[st->rtSingleFreeCount++] = base;
    }
}

// Attach a runtime shadow caster: allocate a frustum block, stage its per-frustum config (active=1)
// Invalidate a frustum block's cached atlas layers (review finding 8): its light was (re)attached,
// detached, or its fields changed, so the persistent content no longer matches. NONE/no-op safe.
static void shadow_layers_invalidate(RendererState* st, uint32_t base, uint32_t count) {
    if (base == ANO_SHADOW_NONE) return;
    for (uint32_t f = 0; f < count && base + f < ANO_SHADOW_FRUSTUM_COUNT; f++)
        st->shadowLayerValid[base + f] = false;
}

// Per-slot mover bookkeeping (review finding 8): while any live slot carries a non-static motion
// descriptor, GPU-side animation can move casters and lights the CPU never sees, so the shadow
// cache treats every frustum dirty each frame. Balanced across create/update/destroy/recycle
// (destroy untracks with ANO_MOTION_STATIC); slotMotionCap always matches the slot capacity
// (ensureEntityCapacity grows it or fails the create).
static void shadow_track_motion(RendererState* st, uint32_t slot, uint32_t motionType) {
    if (slot >= st->slotMotionCap) return;
    uint8_t on = motionType != (uint32_t)ANO_MOTION_STATIC ? 1u : 0u;
    if (st->slotMotion[slot] != on) {
        st->slotMotion[slot] = on;
        if (on) st->motionActiveCount++;
        else if (st->motionActiveCount) st->motionActiveCount--;
    }
}

// + this light's per-light info (castsShadow=1, base, count) through the SlotUploads + the CPU mirror,
// and record the base on the registry row so detach can free it. Past budget it stages NON-casting info
// (castsShadow=0) and stays shadowless — this is load-bearing: detach deliberately doesn't re-stage
// shadowInfo (it banks on the reuse re-staging it), so a budget-full attach onto a recycled palette row
// MUST clear the stale {castsShadow=1, freed base} the prior caster left, or the frag samples it.
// lightPalIdx = absolute light-palette row; regRow = relative registry row.
static void shadow_caster_attach(RendererState* st, uint32_t lightPalIdx, uint32_t regRow,
                                 uint32_t lightType, uint32_t frameIndex) {
    uint32_t base = shadow_frustum_alloc(st, lightType);
    st->lightRegistry.rowShadowBase[regRow] = base; // NONE if budget full
    if (base == ANO_SHADOW_NONE) {
        ShadowLightInfo si = {0}; // castsShadow == 0: clear any prior caster's info on this palette row
        slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
        return;
    }
    uint32_t blockSize = (lightType == LIGHT_TYPE_POINT) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightPalIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .active = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // recycled block: prior caster's cached layers are stale
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
}

// Free a registry row's runtime shadow caster (if any): mark its frustum block inactive (mirror +
// SlotUpload, so shadowsetup writes reject-all and the record loop skips it) and return it to the
// pool. The light itself is disabled (enabled=0) by the caller, so the fragment stops sampling it;
// shadowInfo need not be re-staged (a reuse re-stages it). rowShadowBase -> NONE.
static void shadow_caster_detach(RendererState* st, uint32_t regRow, uint32_t frameIndex) {
    uint32_t base = st->lightRegistry.rowShadowBase[regRow];
    if (base == ANO_SHADOW_NONE) return;
    uint32_t blockSize = (base >= ANO_SHADOW_RT_POINT_BASE) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = st->shadowCfgMirror[base + f];
        c.active = 0u;
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // freed block: content is the departed caster's
    shadow_frustum_free(st, base);
    st->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
}

// Cascade: disable + quarantine every runtime light attached to a renderable that is being destroyed.
// Stages enabled=0 into THIS frame for each child (correctness write, while the parent slot is still
// resolvable) and frees its runtime shadow frustum; the rows return to the free-list after quarantine.
static void cascade_detach_lights(RendererState* state, uint32_t parentRid, uint32_t frameIndex) {
    uint32_t rows[64]; uint32_t n;
    LightData off = {0}; // enabled == 0
    do {
        n = light_registry_detach_children(&state->lightRegistry, parentRid, state->globalFrame, rows, 64u);
        for (uint32_t k = 0; k < n; k++) {
            slot_upload_stage(&state->lightBuffer, frameIndex, rows[k], &off);
            shadow_caster_detach(state, rows[k] - state->lightRegistry.base, frameIndex);
        }
    } while (n == 64u);
}

// Defined below (near the old light-rig helpers); the create-with-light path calls it.
static void register_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t lightType, uint32_t frameIndex);

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
            stage_command_fields(state, &cmd, slot, frameIndex); // stages the light photometrics if present
            shadow_track_motion(state, slot, cmd.motion.type);
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
            // A create-with-light that casts gets a static-region shadow frustum (logic owns scene
            // lights now). Bounded to the static region, matching the light-staging guard above.
            if (cmd.light_index < ANO_STATIC_LIGHT_COUNT && cmd.light.castsShadow)
                register_static_shadow(state, cmd.light_index, (uint32_t)cmd.light.type, frameIndex);
            break;
        }

        case RCMD_UPDATE: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);
                if (cmd.fields & RFIELD_ANIM)
                    shadow_track_motion(state, slot, cmd.motion.type);
                state->shadowGlobalDirty = true; // transform/mesh/motion may move a caster (finding 8)
            }
            break;
        }

        case RCMD_DESTROY: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);     // dead-mark
                shadow_track_motion(state, slot, (uint32_t)ANO_MOTION_STATIC); // untrack before recycle
                state->shadowGlobalDirty = true; // caster set changed (review finding 8)
                cascade_detach_lights(state, cmd.render_id, frameIndex); // disable lights riding this slot
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
                shadow_track_motion(state, slot, b->motion[e].type);
                // Batch carries no instance data; clear it so a recycled slot drops the prior
                // occupant's tint/flags and renders inert.
                slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &inert);
                uint32_t ent[2] = { b->mesh[e], b->material[e] };
                slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
            }
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
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
                if (u->fields & RFIELD_ANIM) {
                    slot_upload_stage(&state->motionBuffer, frameIndex, slot, &u->motion[e]);
                    shadow_track_motion(state, slot, u->motion[e].type);
                }
                if (u->fields & RFIELD_USERDATA)
                    slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &u->instance_data[e]);
                if (u->fields & RFIELD_MESH_MAT) {
                    uint32_t ent[2] = { u->mesh[e], u->material[e] };
                    slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
                }
            }
            state->shadowGlobalDirty = true; // casters may have moved/changed (review finding 8)
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
                shadow_track_motion(state, slot, (uint32_t)ANO_MOTION_STATIC); // untrack before recycle
                cascade_detach_lights(state, rid, frameIndex); // disable lights riding this slot
                render_slots_retire(&state->slots, rid, state->globalFrame);
            }
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_LIGHT_ATTACH: {
            // Attach a runtime light to a renderable: it rides that slot's transform at light_offset.
            uint32_t parentSlot = render_slots_resolve(&state->slots, cmd.render_id);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) break; // parent not (yet) resolvable: drop
            uint32_t row = light_registry_alloc(&state->lightRegistry, cmd.light_id, cmd.render_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) break; // palette full / double-attach: drop
            uint32_t regRow = row - state->lightRegistry.base;
            LightData L = light_data_from_params(&cmd.light, parentSlot, cmd.light_offset);
            state->lightRegistry.rowMirror[regRow] = L; // seed the partial-update RMW base
            slot_upload_stage(&state->lightBuffer, frameIndex, row, &L);
            // Shadow caster: allocate a runtime frustum block if requested (and budget allows), else
            // stage non-casting info so a reused palette row never inherits a prior caster's info.
            if (cmd.light.castsShadow) {
                shadow_caster_attach(state, row, regRow, L.type, frameIndex);
            } else {
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
                state->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
            }
            break;
        }

        case RCMD_LIGHT_UPDATE: {
            uint32_t row = light_registry_resolve(&state->lightRegistry, cmd.light_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) break; // unknown light: drop
            uint32_t parentRid  = light_registry_parent_of(&state->lightRegistry, cmd.light_id);
            uint32_t parentSlot = render_slots_resolve(&state->slots, parentRid);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) break; // parent gone: drop (cascade clears it)
            // Read-modify-write the mirror: merge only the masked fields (0 == legacy full overwrite),
            // then refresh the render-derived transformIndex + enabled and re-stage the whole element.
            uint32_t fields = cmd.light_fields ? cmd.light_fields : ANO_LIGHT_FIELD_ALL;
            uint32_t regRow = row - state->lightRegistry.base;
            LightData* mir = &state->lightRegistry.rowMirror[regRow];
            uint32_t oldType = mir->type;
            light_apply_fields(mir, &cmd.light, cmd.light_offset, fields);
            mir->transformIndex = parentSlot;
            mir->enabled = 1u;
            slot_upload_stage(&state->lightBuffer, frameIndex, row, mir);
            // Shadow-caster transitions. castsShadow is preserved across a normal update; only an explicit
            // ANO_LIGHT_FIELD_CAST request (outside ALL) toggles it, so a full overwrite never silently
            // drops a caster. A live caster also re-allocates on a TYPE change (point = 6 frustums vs
            // single = 1; the stale frustumCount / cube-face fan would sample unallocated layers).
            bool isCasting   = state->lightRegistry.rowShadowBase[regRow] != ANO_SHADOW_NONE;
            bool wantCast    = (fields & ANO_LIGHT_FIELD_CAST) ? (cmd.light.castsShadow != 0u) : isCasting;
            bool typeChanged = mir->type != oldType;
            if (wantCast && (!isCasting || typeChanged)) {
                if (isCasting) shadow_caster_detach(state, regRow, frameIndex); // re-alloc for new type
                shadow_caster_attach(state, row, regRow, mir->type, frameIndex);
            } else if (!wantCast && isCasting) {
                // Toggle off while the light stays lit: free the frustum and re-stage non-casting info so
                // the fragment stops sampling the now-inactive block (detach alone leaves shadowInfo set).
                shadow_caster_detach(state, regRow, frameIndex);
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
            }
            // Changed fields on a staying caster (offset/direction/cone/range) stale its cached
            // layers (review finding 8); the attach/detach transitions above already invalidated.
            shadow_layers_invalidate(state, state->lightRegistry.rowShadowBase[regRow],
                mir->type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u);
            break;
        }

        case RCMD_LIGHT_DETACH: {
            uint32_t row = light_registry_detach(&state->lightRegistry, cmd.light_id, state->globalFrame);
            if (row != ANO_RENDER_SLOT_UNMAPPED) {
                LightData off = {0}; // enabled == 0
                slot_upload_stage(&state->lightBuffer, frameIndex, row, &off);
                shadow_caster_detach(state, row - state->lightRegistry.base, frameIndex); // free its frustum if casting
            }
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
            RenderEvent ev = { .kind = REVENT_SLOT_RETIRED, .u.render_id = retired[i] };
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

    // Runtime light lifecycle (audit 4.7 Phase 3): return quarantine-expired light rows to the
    // free-list, then publish the cull light count = base + dynamic high-water. updateCullingBuffers
    // ran before this drain (per-frame order), so a light attached this frame is binned next frame,
    // one frame after its row data was staged + flushed — acceptable for a discrete attach/detach.
    light_registry_collect(&state->lightRegistry, state->globalFrame);
    light_registry_compact(&state->lightRegistry); // peel trailing free rows -> shrink the cull light count
    state->lightBuffer.count = state->lightRegistry.base + state->lightRegistry.highWater;

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

// Loaded-asset registry (anoptic_render.h). initVulkan parses the glTF assets and records them here
// in load order; the logic master flattens them into renderable primitives to compose the scene.
// g_defaultMaterial is the first asset's first material (the procedural ground/markers borrow it).
#define ANO_MAX_LOADED_ASSETS 16u
static ModelAsset* g_assets[ANO_MAX_LOADED_ASSETS];
static uint32_t    g_assetCount;
static uint32_t    g_defaultMaterial;

uint32_t anoRenderAssetCount(void) { return g_assetCount; }

uint32_t anoRenderAssetPrimitives(uint32_t asset_id, const mat4 root, AnoRenderableDesc* out, uint32_t cap) {
    if (asset_id >= g_assetCount) return 0u;
    return model_flatten(g_assets[asset_id], root, out, cap);
}

uint32_t anoRenderFallbackMesh(void)    { return FALLBACK_MESH_INDEX; }
uint32_t anoRenderDefaultMaterial(void) { return g_defaultMaterial; }
uint32_t anoRenderStaticLightBase(void) { return ANO_STATIC_LIGHT_COUNT; }

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

// Per-view screen-area cull threshold (review 4.9 step 1). Stored on the render state and squared
// into CullUBO.viewCullParams[view][1] by updateCullingBuffers; takes effect from the next recorded
// frame. Pixels of projected bounding-sphere radius; 0 disables the test for that view, negative is
// clamped to 0. Out-of-range view index is ignored. Render-thread only (no atomics), like the
// lighting-mode setter.
void ano_render_set_view_cull_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.cullPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
}

float ano_render_get_view_cull_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.cullPixelThreshold[view];
}

// Per-view LOD threshold (review 4.9 step 2). Copied into CullUBO.viewCullParams[view][2] by
// updateCullingBuffers; takes effect from the next recorded frame. Pixels of projected radius at
// which level 1 begins; 0 disables LOD selection (always level 0), negative clamps to 0. Inert
// until meshes carry LOD chains. Out-of-range view ignored. Render-thread only, like the others.
void ano_render_set_view_lod_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.lodPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    if (view == 0u) rendererState.shadowGlobalDirty = true; // shadow LOD tracks view 0 (finding 8)
}

float ano_render_get_view_lod_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.lodPixelThreshold[view];
}

// Global LOD bias (review 4.9 step 2): added to every entity's auto-selected level in cull.comp,
// then clamped to the mesh's chain range. + = coarser, - = finer; published into viewCullParams[v][3]
// by updateCullingBuffers; takes effect next recorded frame. Clamped to +/- ANO_MAX_LOD (the shader
// clamps to the real chain length per entity). Render-thread only, like the other knobs.
void ano_render_set_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.lodBias = bias < -lim ? -lim : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD (finding 8)
}

int32_t ano_render_get_lod_bias(void) {
    return rendererState.lodBias;
}

// Shadow LOD offset (review 4.9 step 2, revised): shadow casters now select the primary camera view's
// (view 0) LOD so a caster's shadow silhouette tracks its visible geometry; this bias is an extra
// RELATIVE offset added on top (0 = exact match, + = coarser shadow). Published into CullUBO.shadowLodBias
// by updateCullingBuffers; takes effect next recorded frame. cull.comp clamps per-entity to the real
// chain length, so non-LOD meshes always cast level 0. Clamped to [0, ANO_MAX_LOD]; negative (a shadow
// finer than the visible mesh) is pointless and clamps to 0. Render-thread only, like the other knobs.
void ano_render_set_shadow_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.shadowLodBias = bias < 0 ? 0 : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD (finding 8)
}

int32_t ano_render_get_shadow_lod_bias(void) {
    return rendererState.shadowLodBias;
}

// Per-view Hi-Z occlusion toggle (review 4.9 step 3). When enabled, the cull rejects entities fully
// behind LAST frame's depth pyramid for that view (single-phase; ~1-frame disocclusion latency).
// Published into CullUBO.hizParams[view].z (mipCount when on, 0 when off) by updateCullingBuffers;
// takes effect next recorded frame. Default off. Render-thread only; out-of-range view ignored.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.hizEnable[view] = enable ? 1u : 0u;
}

bool ano_render_get_view_hiz_enable(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return false;
    return rendererState.hizEnable[view] != 0u;
}

// Print the averaged per-pass GPU times + per-allocator resident VRAM for the active lighting mode
// (RADIANCE_CASCADES.md §8). shadowAtlas is the always-resident CDF-stats atlas (ANO_SHADOW_ATLAS_LAYERS
// RGBA16 layers x ANO_SHADOW_DIM^2 x MAX_FRAMES_IN_FLIGHT), reported separately so RC-only VRAM is
// not charged for the idle-but-resident atlas — the fairness break-out the harness requires.
static void ano_print_profiling(void) {
    static const char* const modeNames[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RC" };
    uint32_t m = rendererState.lightingMode;
    const char* mn = (m < (uint32_t)ANO_LIGHTING_MODE_COUNT) ? modeNames[m] : "?";
    double inv = g_tsFrames ? 1.0 / (double)g_tsFrames : 0.0;
    double up = g_tsAccumMs[ANO_TS_FRAME_BEGIN]    * inv;
    double cp = g_tsAccumMs[ANO_TS_AFTER_UPLOAD]   * inv;
    double sh = g_tsAccumMs[ANO_TS_AFTER_COMPUTE]  * inv;
    double li = g_tsAccumMs[ANO_TS_AFTER_SHADOW]   * inv;
    double co = g_tsAccumMs[ANO_TS_AFTER_LIGHTING] * inv;
    double total = up + cp + sh + li + co;

    const double MiB = 1024.0 * 1024.0;
    double gpu  = (double)allocator_used_bytes(&gpuAllocator)       / MiB;
    double tex  = (double)allocator_used_bytes(&textureAllocator)   / MiB;
    double swap = (double)allocator_used_bytes(&swapchainAllocator) / MiB;
    double stg  = (double)allocator_used_bytes(&stagingAllocator)   / MiB;
    // CDF stats atlas + blur temp (both RGBA16_UNORM = 8 B/texel, ATLAS_LAYERS = 2 sublayers/frustum)
    // + the per-frustum transient nearest-occluder depth array (D32 = 4 B/texel). ONE shared instance
    // of each across frames in flight (review finding 8).
    double atlas = (double)((VkDeviceSize)ANO_SHADOW_ATLAS_LAYERS * ANO_SHADOW_DIM * ANO_SHADOW_DIM * 8u * 2u
                            + (VkDeviceSize)ANO_SHADOW_FRUSTUM_COUNT * ANO_SHADOW_DIM * ANO_SHADOW_DIM * 4u) / MiB;

    printf("[profile mode=%s] GPU ms: upload=%.3f compute=%.3f shadow=%.3f lighting=%.3f composite=%.3f total=%.3f"
           " | VRAM MiB: gpu=%.1f tex=%.1f swap=%.1f staging=%.1f | shadowAtlas(resident)=%.1f\n",
           mn, up, cp, sh, li, co, total, gpu, tex, swap, stg, atlas);
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

// Read this frame slot's picking readback (fence-complete, like the timestamps): map the sampled
// slot back to a render_id and emit REVENT_PICK_RESULT to the logic master when the hit changes
// (audit 3.1). A cleared/unmapped slot collapses to ANO_RENDER_NO_PICK. On a full event ring, skip
// latching so the change re-emits next frame (backpressure-safe).
static void ano_collect_pick(uint32_t frameIndex) {
    uint32_t slot = *rendererState.frames[frameIndex].pickReadbackMapped;
    uint32_t rid  = (slot == 0xFFFFFFFFu) ? ANO_RENDER_NO_PICK
                                          : render_slots_render_id_of(&rendererState.slots, slot);
    if (rid == ANO_RENDER_SLOT_UNMAPPED) rid = ANO_RENDER_NO_PICK; // slot retired between draw and read
    if (rid != rendererState.lastPickRenderId) {
        RenderEvent ev = { .kind = REVENT_PICK_RESULT, .u.pick_render_id = rid };
        if (ano_render_emit_event(&rendererState.bridge, &ev))
            rendererState.lastPickRenderId = rid;
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

        // Same fence-gated readback for picking: this slot's id-texel copy has retired.
        ano_collect_pick(rendererState.frameIndex);

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

	// Async Hi-Z (review finding 2): 1-based ordinal of THIS submit. Monotonic across swapchain
	// recreates (unlike frame slots/fences), so timeline signal values never repeat.
	uint64_t ordinal = rendererState.timelineOrdinal + 1u;
	if (rendererState.asyncHiz)
	{
		// This slot's compute CB trails its graphics submit and is not covered by frameFence:
		// host-wait its own prior signal (MAX_FRAMES_IN_FLIGHT ordinals back) before re-recording.
		if (ordinal > (uint64_t)MAX_FRAMES_IN_FLIGHT)
		{
			uint64_t prior = ordinal - (uint64_t)MAX_FRAMES_IN_FLIGHT;
			VkSemaphoreWaitInfo waitInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1, .pSemaphores = &rendererState.hizTimeline, .pValues = &prior };
			vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX);
		}
		recordHiZCompute(rendererState.frameIndex);
	}

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	// Graphics submit. Async Hi-Z adds a second wait — hizTimeline >= ordinal-2 at the cull's
	// COMPUTE stage (the lag-2 pyramid it samples) | EARLY_FRAGMENT_TESTS (chain anchor for the
	// depth-resolve WAR flip in recordCommandBuffer) — and a second signal, gfxTimeline = ordinal
	// (waited by this frame's compute build). Values on binary semaphores are ignored per spec.
	VkSemaphore waitSemaphores[2] = {rendererState.frames[rendererState.frameIndex].imageAvailable, rendererState.hizTimeline};
	VkPipelineStageFlags waitStages[2] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT};
	uint64_t waitValues[2] = {0, ordinal > 2u ? ordinal - 2u : 0u};
	VkSemaphore signalSemaphores[2] = {rendererState.frames[rendererState.frameIndex].renderFinished, rendererState.gfxTimeline};
	uint64_t signalValues[2] = {0, ordinal};
	VkTimelineSemaphoreSubmitInfo timelineValues = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.waitSemaphoreValueCount = 2, .pWaitSemaphoreValues = waitValues,
		.signalSemaphoreValueCount = 2, .pSignalSemaphoreValues = signalValues };
	if (rendererState.asyncHiz)
		submitInfo.pNext = &timelineValues;
	submitInfo.waitSemaphoreCount = rendererState.asyncHiz ? 2 : 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].commandBuffer);
	submitInfo.signalSemaphoreCount = rendererState.asyncHiz ? 2 : 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence)); // this goes here because multi-threading
	if (vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, rendererState.frames[rendererState.frameIndex].frameFence) != VK_SUCCESS)
	{
		printf("Failed to submit draw command buffer!\n");
		return;
	}
	rendererState.timelineOrdinal = ordinal;

	// Async Hi-Z compute submit (review finding 2): waits this frame's graphics (gfxTimeline ==
	// ordinal: depth resolves done, prior readers of the slots being rewritten retired), signals
	// hizTimeline == ordinal for the ordinal+2 graphics submit. Executes during the NEXT frame's
	// graphics on the dedicated queue — off the critical path.
	if (rendererState.asyncHiz)
	{
		VkPipelineStageFlags hizWaitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		VkTimelineSemaphoreSubmitInfo hizValues = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &ordinal,
			.signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &ordinal };
		VkSubmitInfo hizSubmit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &hizValues,
			.waitSemaphoreCount = 1, .pWaitSemaphores = &rendererState.gfxTimeline, .pWaitDstStageMask = &hizWaitStage,
			.commandBufferCount = 1, .pCommandBuffers = &rendererState.frames[rendererState.frameIndex].computeCommandBuffer,
			.signalSemaphoreCount = 1, .pSignalSemaphores = &rendererState.hizTimeline };
		if (vkQueueSubmit(ctx.computeQueue, 1, &hizSubmit, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			// Keep the timeline monotonic so the ordinal+2 graphics wait cannot deadlock; a failed
			// submit here is device-loss territory anyway.
			printf("Failed to submit async Hi-Z command buffer!\n");
			VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
				.semaphore = rendererState.hizTimeline, .value = ordinal };
			vkSignalSemaphore(ctx.device, &signalInfo);
		}
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
    // Light palette: ×1 device-local + delta staging (count tracks live lights; create-with-light
    // and RCMD_LIGHT_* commands stage into it). slot_upload_create zeroes count.
    return slot_upload_create(&state->lightBuffer, maxLights, sizeof(LightData), SLOT_STAGING_INIT);
}

// Register a STATIC-region shadow caster for a light-palette row whose photometric data was already
// staged (by stage_command_fields on a create-with-light). Allocates the light type's frustum block
// monotonically within the static region (point = 6 cube faces, dir/spot = 1), stages each frustum's
// config (active=1) + the light's info (castsShadow=1, base, count) to `frameIndex`. Past the type's
// budget or the static region it stays shadowless (info default castsShadow=0) — no error. Called by
// the RCMD_CREATE apply path so the LOGIC master spawns the scene's casting lights (audit: logic owns
// the scene), on the same static budget the old render-side rig used.
static void register_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t lightType, uint32_t frameIndex) {
    uint32_t budget = lightType == LIGHT_TYPE_DIRECTIONAL ? ANO_SHADOW_DIR_COUNT
                    : lightType == LIGHT_TYPE_POINT       ? ANO_SHADOW_POINT_COUNT
                                                          : ANO_SHADOW_SPOT_COUNT;
    uint32_t blockSize = lightType == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
    if (st->shadowTypeUsed[lightType] >= budget ||
        st->shadowFrustumNext + blockSize > ANO_SHADOW_STATIC_FRUSTUM_COUNT)
        return; // budget/region full: light stays shadowless
    uint32_t base = st->shadowFrustumNext;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .active = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // fresh static block: render before first sample
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightIdx, &si);
    st->shadowFrustumNext += blockSize;
    st->shadowTypeUsed[lightType] += 1u;
}

bool createMotionBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    (void)ctx;
    // Mover bookkeeping for the shadow cache (review finding 8): per-slot non-static-motion flags,
    // grown alongside the slot table by ensureEntityCapacity.
    state->slotMotion = (uint8_t*)calloc(maxEntities, 1u);
    if (!state->slotMotion) return false;
    state->slotMotionCap = maxEntities;
    state->motionActiveCount = 0u;
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

// Per-light fragment runtime record (LightRuntime = 4x vec4 = 64B/light: pose + color*intensity +
// range/cone/type). ×MAX_FRAMES_IN_FLIGHT DEVICE_LOCAL, written by lightsetup.comp each frame and read by
// the fragment passes — same storage class as the transform buffer, so it reuses TransformBuffer (mapped[]
// stays NULL for device-local). Fixed at the light palette capacity (never grown, like lightBuffer).
bool createLightRuntimeBuffer(VulkanContext* ctx, TransformBuffer* buf, uint32_t maxLights, VkMemoryPropertyFlags props) {
    buf->capacity = maxLights;
    buf->count = 0;

    VkDeviceSize bufferSize = (VkDeviceSize)(sizeof(float) * 16u) * maxLights; // 4x vec4 LightRuntime = 64B

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &buf->buffer[i]) != VK_SUCCESS) {
            printf("Failed to create light pose buffer!\n");
            return false;
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
    // Partitioned: each camera view owns every draw slot, each shadow frustum owns one slot-0
    // partition (ano_draw_partition_count(), see components.h), each holding up to maxDraws commands.
    VkDeviceSize bufferSize = cmdStride * maxDraws * ano_draw_partition_count();

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
// RGBA16 CDF-stats atlas + blur temp (2D arrays, per-sublayer render views + array sample views).
// Shared (once): the transient caster-depth array (one slice per frustum) and the CPU shadow
// config / per-light shadow info SlotUploads.
// in:  ctx, state (lightBuffer.capacity known)
// out: true on success; populates frames[].shadow.*, state->shadowDepth*, state->shadow{Config,Info}
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
    }

    // CDF-stats atlas + blur-temp: both RGBA16_UNORM 2D arrays, ANO_SHADOW_ATLAS_LAYERS layers (2 MRT
    // sublayers per frustum = 4 depth bands), single-sample, color-rendered + sampled. The atlas holds
    // the final (blurred) per-band (coverage,M); temp is the separable-blur intermediate. ONE instance
    // across frames in flight (review finding 8): content persists so clean frustums skip re-render.
    // Both seeded to SHADER_READ — the atlas's rest state — so a first frame with nothing dirty (or
    // the whole-array preserve transition) never sees UNDEFINED.
    {
        VkImage* momentImgs[2]     = { &state->shadowAtlasImage, &state->shadowTempImage };
        GpuAllocation* momentAl[2] = { &state->shadowAtlasAlloc, &state->shadowTempAlloc };
        VkImageView* momentArr[2]  = { &state->shadowAtlasArrayView, &state->shadowTempArrayView };
        VkImageView* momentLyr[2]  = { state->shadowAtlasLayerView, state->shadowTempLayerView };
        for (int m = 0; m < 2; m++) {
            VkImageCreateInfo iinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            iinfo.imageType = VK_IMAGE_TYPE_2D;
            iinfo.format = ANO_SHADOW_STATS_FORMAT;
            iinfo.extent = (VkExtent3D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM, 1 };
            iinfo.mipLevels = 1;
            iinfo.arrayLayers = ANO_SHADOW_ATLAS_LAYERS;
            iinfo.samples = VK_SAMPLE_COUNT_1_BIT;
            iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            iinfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(ctx->device, &iinfo, NULL, momentImgs[m]) != VK_SUCCESS) return false;
            VkMemoryRequirements imr; vkGetImageMemoryRequirements(ctx->device, *momentImgs[m], &imr);
            *momentAl[m] = gpu_alloc(&gpuAllocator, imr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (momentAl[m]->memory == VK_NULL_HANDLE) return false;
            vkBindImageMemory(ctx->device, *momentImgs[m], momentAl[m]->memory, momentAl[m]->offset);

            VkImageViewCreateInfo vinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vinfo.image = *momentImgs[m];
            vinfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            vinfo.format = ANO_SHADOW_STATS_FORMAT;
            vinfo.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS };
            if (vkCreateImageView(ctx->device, &vinfo, NULL, momentArr[m]) != VK_SUCCESS) return false;

            for (uint32_t s = 0; s < ANO_SHADOW_ATLAS_LAYERS; s++) {
                VkImageViewCreateInfo lv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                lv.image = *momentImgs[m];
                lv.viewType = VK_IMAGE_VIEW_TYPE_2D;
                lv.format = ANO_SHADOW_STATS_FORMAT;
                lv.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, s, 1 };
                if (vkCreateImageView(ctx->device, &lv, NULL, &momentLyr[m][s]) != VK_SUCCESS) return false;
            }

            // Seed ALL layers to SHADER_READ (transitionImageLayout spans only layer 0).
            VkCommandBuffer seedCmd = beginSingleTimeCommands(ctx);
            VkImageMemoryBarrier seed = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *momentImgs[m], .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            vkCmdPipelineBarrier(seedCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &seed);
            endSingleTimeCommands(ctx, seedCmd);
        }
    }

    // Dirty-frustum cache state (review finding 8): every layer starts invalid (renders on first
    // activation); env hooks pin the pre-cache always-dirty behavior or freeze for the static-scene
    // ceiling. Mover bookkeeping (slotMotion) is allocated by createMotionBuffer.
    state->shadowCacheMode = getenv("ANO_FORCE_NO_SHADOW_CACHE") ? 1u
                           : getenv("ANO_SHADOW_CACHE_FREEZE")   ? 2u : 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) state->shadowLayerValid[s] = false;
    state->shadowGlobalDirty = false;
    if (state->shadowCacheMode)
        printf("Shadow cache: %s\n", state->shadowCacheMode == 1u ? "OFF (every frame dirty)" : "FREEZE");

    // Transient nearest-occluder depth (never sampled): ONE image shared across frames in flight,
    // one slice per shadow frustum so the per-frustum depth renders are mutually independent (no
    // WAW barrier chain through a reused layer). Contents are frame-transient (loadOp CLEAR each
    // render); the per-frame UNDEFINED->DEPTH transition carries the cross-frame WAR in its
    // EARLY|LATE_FRAGMENT_TESTS source scope.
    {
        VkImageCreateInfo dinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        dinfo.imageType = VK_IMAGE_TYPE_2D;
        dinfo.format = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT;
        dinfo.extent = (VkExtent3D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM, 1 };
        dinfo.mipLevels = 1;
        dinfo.arrayLayers = ANO_SHADOW_FRUSTUM_COUNT;
        dinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        dinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        dinfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(ctx->device, &dinfo, NULL, &state->shadowDepthImage) != VK_SUCCESS) return false;
        VkMemoryRequirements dmr; vkGetImageMemoryRequirements(ctx->device, state->shadowDepthImage, &dmr);
        state->shadowDepthAlloc = gpu_alloc(&gpuAllocator, dmr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->shadowDepthAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindImageMemory(ctx->device, state->shadowDepthImage, state->shadowDepthAlloc.memory, state->shadowDepthAlloc.offset);
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            VkImageViewCreateInfo dv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            dv.image = state->shadowDepthImage;
            dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dv.format = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT;
            dv.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
            if (vkCreateImageView(ctx->device, &dv, NULL, &state->shadowDepthSliceView[s]) != VK_SUCCESS) return false;
        }
    }

    // Shadow config + per-light info as SlotUploads (×1 device + delta staging) so the runtime caster
    // lifecycle mutates them race-free, like lightBuffer. The init zero-fills the device (spare slots:
    // active=0 / castsShadow=0); a casting create stages its frustum block via register_static_shadow.
    // shadowCfgMirror is the render-thread CPU copy the record loop gates on.
    if (!slot_upload_create(&state->shadowConfig, ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig), SLOT_STAGING_INIT)) return false;
    if (!slot_upload_create(&state->shadowInfo, state->lightBuffer.capacity, sizeof(ShadowLightInfo), SLOT_STAGING_INIT)) return false;
    state->shadowCfgMirror = (ShadowFrustumConfig*)calloc(ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig)); // active=0
    if (!state->shadowCfgMirror) return false;

    // Static rig allocator (monotonic, fills [0, shadowFrustumNext) within the static region).
    state->shadowFrustumNext = 0u;
    state->shadowTypeUsed[0] = state->shadowTypeUsed[1] = state->shadowTypeUsed[2] = 0u;
    // Runtime pools: push every free single slot + point-block base in the headroom region.
    state->rtSingleFreeCount = 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_RT_SINGLE_COUNT; s++)
        state->rtSingleFree[state->rtSingleFreeCount++] = ANO_SHADOW_RT_SINGLE_BASE + s;
    state->rtPointFreeCount = 0u;
    for (uint32_t b = 0; b < ANO_SHADOW_RT_POINT_COUNT; b++)
        state->rtPointFree[state->rtPointFreeCount++] = ANO_SHADOW_RT_POINT_BASE + b * ANO_SHADOW_CUBE_FACES;

    return true;
}

bool createCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->culling.maxEntities = maxEntities;
    uint32_t maxMeshes = ANO_MAX_MESHES; // must match the descriptor ranges in instanceInit.c

    // Per-view screen-area cull + LOD thresholds (review 4.9 steps 1-2); runtime-overridable per
    // view via ano_render_set_view_cull_threshold / ano_render_set_view_lod_threshold. Same default
    // for every view; tune at runtime.
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; ++v) {
        state->cullPixelThreshold[v] = ANO_CULL_PIXEL_THRESHOLD_DEFAULT;
        state->lodPixelThreshold[v]  = ANO_LOD_PIXEL_THRESHOLD_DEFAULT;
        state->hizEnable[v] = 0u;                          // Hi-Z occlusion off by default (review 4.9 step 3)
        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
            memset(state->viewProjHist[f][v], 0, sizeof(mat4)); // first frames: zero matrix -> reprojection skipped
    }
    // Test hook: enable view 0's occlusion test from startup (same effect as the H key) for
    // headless A/B of the Hi-Z consumption path (in-frame lag-1 vs async lag-2, review finding 2).
    if (getenv("ANO_HIZ_ON")) state->hizEnable[0] = 1u;
    state->shadowLodBias = ANO_SHADOW_LOD_BIAS_DEFAULT; // shadow LOD offset relative to view-0 LOD (0 = exact match)
    
    VkDeviceSize meshDataSize = sizeof(uint32_t) * 9 * maxMeshes; // MeshData: 9 u32 (8 + lodCount)
    VkDeviceSize meshBoundsSize = sizeof(float) * 4 * maxMeshes; // vec4
    VkDeviceSize drawCountSize = sizeof(uint32_t) * ano_draw_partition_count();
    VkDeviceSize compactedEntityIndicesSize = sizeof(uint32_t) * maxEntities * ano_draw_partition_count();
    // Transparency sort keys: one float per camera draw slot (only the transmission partition writes
    // them, but sizing per camera-view keeps the index = view*maxEntities + writeIdx trivially in range).
    VkDeviceSize sortKeysSize = sizeof(float) * (VkDeviceSize)ANO_VIEW_COUNT * maxEntities;
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

        // Sort Keys Buffer (transparency sort): GPU-private, written by cull.comp, read by tpsort.comp.
        VkBufferCreateInfo sortKeysInfo = {};
        sortKeysInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sortKeysInfo.size = sortKeysSize;
        sortKeysInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        sortKeysInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &sortKeysInfo, NULL, &state->culling.sortKeysBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.sortKeysBuffer[i], &memReqs);
        state->culling.sortKeysAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->culling.sortKeysAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.sortKeysBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.sortKeysBuffer[i], state->culling.sortKeysAllocs[i].memory, state->culling.sortKeysAllocs[i].offset);

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

	// Async Hi-Z gate (review finding 2). Needs a compute queue on a family DISTINCT from graphics
	// (findQueueFamilies prefers a dedicated one; same-family means the very same VkQueue — no
	// overlap possible), timeline semaphores for the cross-queue ordering, and the single-sample MAX
	// depth resolve (the raster resolve stays on graphics; only the pyramid reduce/downsample moves,
	// so the compute CB never needs graphics-only stages). ANO_FORCE_NO_ASYNC_HIZ pins the in-frame
	// build for A/B. Must be decided before depth/Hi-Z resources (sharing mode + rest layouts) and
	// createSyncObjects (timelines) run.
	rendererState.asyncHiz = ctx.deviceCapabilities.timelineSemaphore
	                      && ctx.deviceCapabilities.depthMaxResolve
	                      && ctx.queueFamilyIndices.computePresent
	                      && ctx.queueFamilyIndices.computeFamily != ctx.queueFamilyIndices.graphicsFamily
	                      && !getenv("ANO_FORCE_NO_ASYNC_HIZ");
	printf("Async Hi-Z build: %s\n", rendererState.asyncHiz ? "on (dedicated compute queue)" : "off (in-frame)");

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

	// Async Hi-Z build CBs (review finding 2): the graphics-family pool is invalid on the compute
	// queue, so the per-frame build CBs come from their own compute-family pool.
	if (rendererState.asyncHiz)
	{
		VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = ctx.queueFamilyIndices.computeFamily };
		if (vkCreateCommandPool(ctx.device, &cpi, NULL, &rendererState.computeCommandPool) != VK_SUCCESS)
		{
			printf("Quitting init: compute command pool failure!\n");
			unInitVulkan();
			return false;
		}
		VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = rendererState.computeCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkAllocateCommandBuffers(ctx.device, &cai, &rendererState.frames[i].computeCommandBuffer) != VK_SUCCESS)
			{
				printf("Quitting init: compute command buffer allocation failure!\n");
				unInitVulkan();
				return false;
			}
		}
	}

	createColorResources(&ctx); // Make this a bool and add check

	if(!createDepthResources(&ctx, &rendererState))
	{
		printf("Quitting init: depth resource creation failure!\n");
	}

	// Hi-Z occlusion pyramid images (review 4.9 step 3). Built each frame from depth; the per-mip
	// descriptor sets are allocated/written after the layout + pool exist (below).
	if(!createHiZResources(&ctx, &rendererState))
	{
		printf("Quitting init: Hi-Z resource creation failure!\n");
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
	    !createLightRuntimeBuffer(&ctx, &rendererState.lightRuntimeBuffer, PALETTE_CAPACITY,
	                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
	    !createIndirectDrawBuffer(&ctx, &rendererState, maxEntities) ||
	    !createCullingBuffers(&ctx, &rendererState, maxEntities) ||
	    !createClusterBuffers(&ctx, &rendererState) ||
	    !createShadowResources(&ctx, &rendererState))
	{
		printf("Quitting init: buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	// Load the scene's glTF assets into GPU memory (geometry pool + materials + textures). The render
	// world owns asset LOADING; the logic master COMPOSES scene instances from them via the public
	// anoRenderAsset* query API and emits the creates itself (audit: logic owns the scene). Asset load
	// order is the asset_id namespace (0 = viking room, 1 = candle holder).
	ModelAsset* vikingRoomAsset = parseGltf(&ctx, "viking_room.gltf");
	if (!vikingRoomAsset)
	{
		printf("Failed to parse glTF file!\n");
		unInitVulkan();
		return false;
	}
	ModelAsset* candleHolderAsset = parseGltf(&ctx, "GlassHurricaneCandleHolder.gltf");
	if (!candleHolderAsset)
	{
		printf("Failed to parse GlassHurricaneCandleHolder glTF file!\n");
		unInitVulkan();
		return false;
	}
	g_assets[g_assetCount++] = vikingRoomAsset;   // asset_id 0
	g_assets[g_assetCount++] = candleHolderAsset; // asset_id 1

	// Sponza: a large multi-material interior (103 primitives / 25 materials / 69 textures) parsed under
	// one node, dropped in as the scene environment to stress-test node-mesh placement and the renderer's
	// graceful fallback for unimplemented PBR attributes. Texture URIs resolve relative to the glTF's own
	// directory (see parseGltf). Non-fatal: a missing/failed Sponza just leaves asset_id 2 unregistered,
	// and the logic master's spawn degrades to a no-op for it (the base scene still comes up).
	ModelAsset* sponzaAsset = parseGltf(&ctx, "sponza/2.0/Sponza/glTF/Sponza.gltf");
	if (sponzaAsset)
		g_assets[g_assetCount++] = sponzaAsset;   // asset_id 2
	else
		printf("Warning: failed to parse Sponza glTF; continuing without it.\n");

	// Default material for procedural renderables (ground slab / debug markers the logic master builds
	// without an asset): the first asset's first primitive material, matching the old rig's choice.
	{
		mat4 ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		AnoRenderableDesc d0;
		if (model_flatten(vikingRoomAsset, ident, &d0, 1u) > 0u) g_defaultMaterial = d0.material_index;
	}


	// ECS <-> render bridge: render-owned slot authority + command/event rings (VK_BACKEND_INTEROP.md).
	// The logic master now composes the whole scene and emits its creates through this command ring —
	// the same path a runtime spawn takes — so nothing is written to the per-slot GPU buffers here.
	rendererState.renderHeap = mi_heap_new();
	if (!rendererState.renderHeap ||
	    !render_slots_init(&rendererState.slots, rendererState.renderHeap, maxEntities, MAX_FRAMES_IN_FLIGHT) ||
	    // Events ring widened to 4096: it now also carries forwarded input (key REPEAT bursts), and
	    // render must never block emitting it, so the ring absorbs the spikes (audit 4.11).
	    !ano_render_bridge_init(&rendererState.bridge, rendererState.renderHeap, 4096, 4096))
	{
		printf("Quitting init: render bridge / slot authority failure!\n");
		unInitVulkan();
		return false;
	}

	// Runtime light registry (audit 4.7 Phase 3). Palette rows [0, ANO_STATIC_LIGHT_COUNT) are the
	// STATIC region the logic master fills with scene light-entities (create-with-light, static shadow
	// budget); the registry owns the dynamic remainder [ANO_STATIC_LIGHT_COUNT, capacity) for runtime
	// attach/detach. A FIXED base (not the live scene-light count) lets logic own the static light_index
	// namespace independently of when/whether it has spawned the scene lights.
	light_registry_init(&rendererState.lightRegistry, ANO_STATIC_LIGHT_COUNT,
	                    rendererState.lightBuffer.capacity - ANO_STATIC_LIGHT_COUNT,
	                    MAX_FRAMES_IN_FLIGHT);

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

	// Zero the light palette + shadow config/info device buffers once (hardening): an unwritten row
	// then reads enabled=0 / active=0 / castsShadow=0 (inert) instead of uninitialized memory, which
	// the cull/fragment would treat as a phantom light/caster. The logic master spawns the scene
	// asynchronously; each create is staged + uploaded by the normal per-frame path (render_apply_commands
	// + recordCommandBuffer) as it arrives, so there is no init scene seed beyond this zero-fill.
	{
		VkCommandBuffer up = beginSingleTimeCommands(&ctx);
		vkCmdFillBuffer(up, rendererState.lightBuffer.device, 0,
		                (VkDeviceSize)sizeof(LightData) * rendererState.lightBuffer.capacity, 0u);
		vkCmdFillBuffer(up, rendererState.shadowConfig.device, 0,
		                (VkDeviceSize)sizeof(ShadowFrustumConfig) * rendererState.shadowConfig.capacity, 0u);
		vkCmdFillBuffer(up, rendererState.shadowInfo.device, 0,
		                (VkDeviceSize)sizeof(ShadowLightInfo) * rendererState.shadowInfo.capacity, 0u);
		endSingleTimeCommands(&ctx, up);
	}

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


	if (!createDescriptorSets(&ctx, &rendererState))
	{
		printf("Quitting init: UBO descriptor sets creation failure!\n");
		unInitVulkan();
		return false;
	}


	updateUboDescriptorSets(&ctx, &rendererState);
	updateTonemapDescriptorSets(&ctx, &rendererState);
	updateHiZDescriptorSets(&ctx, &rendererState);
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

