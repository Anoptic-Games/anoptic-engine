/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <anoptic_log.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/scene_buffers.h"

//Init and cleanup functions

// Sole mint point for this file's scene buffers. in: create info, memory props; out: handle, backing
// allocation. Total out-params: every failure arm leaves VK_NULL_HANDLE + a zeroed allocation, so the
// cleanup pass's `if (buffer[i])` destroy gate never sees a driver-undefined or already-destroyed handle.
[[nodiscard]] static bool mintSceneBuffer(VulkanContext* ctx, const VkBufferCreateInfo* info,
                                          VkMemoryPropertyFlags props,
                                          VkBuffer* buffer, GpuAllocation* allocation) {
    *buffer = VK_NULL_HANDLE;
    *allocation = (GpuAllocation){0};

    // Minted into a local: a failed create leaves that undefined, never the slot.
    VkBuffer minted = VK_NULL_HANDLE;
    if (vkCreateBuffer(ctx->device, info, NULL, &minted) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device, minted, &memRequirements);

    GpuAllocation alloc = gpu_alloc(&gpuAllocator, memRequirements, props);
    if (alloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(ctx->device, minted, NULL);
        return false;
    }
    // A refused bind leaves the buffer created but unbacked while the suballocated mapping still
    // reads live: every mapped write would land outside what the buffer references. The arena span
    // is monotonic, so nothing is reclaimed here.
    if (vkBindBufferMemory(ctx->device, minted, alloc.memory, alloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, minted, NULL);
        return false;
    }

    *buffer = minted;
    *allocation = alloc;
    return true;
}

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
        
        if (!mintSceneBuffer(ctx, &bufferInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &state->materialBuffer.buffer[i], &state->materialBuffer.allocs[i])) {
            ano_log(ANO_FATAL, "Failed to create material buffer!");
            return false;
        }

        state->materialBuffer.mapped[i] = (MaterialData*)state->materialBuffer.allocs[i].mapped;
    }
    return true;
}

bool createLightBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxLights) {
    (void)ctx;
    // Light palette: ×1 device-local + delta staging.
    return slot_upload_create(&state->lightBuffer, maxLights, sizeof(LightData), SLOT_STAGING_INIT, true);
}


bool createMotionBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    (void)ctx;
    // Mover bookkeeping for the shadow cache: per-slot motion flags + swept-exposure mirrors.
    state->slotMotion   = (uint8_t*)calloc(maxEntities, 1u);
    state->slotBasePose = (mat4*)calloc(maxEntities, sizeof(mat4));
    state->slotMeshIdx  = (uint32_t*)malloc((size_t)maxEntities * sizeof(uint32_t));
    state->slotMoverIdx = (uint32_t*)malloc((size_t)maxEntities * sizeof(uint32_t));
    if (!state->slotMotion || !state->slotBasePose || !state->slotMeshIdx || !state->slotMoverIdx)
        return false;
    memset(state->slotMeshIdx,  0xFF, (size_t)maxEntities * sizeof(uint32_t)); // NO_MESH_INDEX
    memset(state->slotMoverIdx, 0xFF, (size_t)maxEntities * sizeof(uint32_t)); // ANO_RENDER_SLOT_UNMAPPED
    state->slotMotionCap = maxEntities;
    state->motionActiveCount = 0u;
    state->movers = NULL;
    state->moverCount = state->moverCap = 0u;
    state->moverUnboundedCount = 0u;
    // ×1 device-local + delta staging.
    return slot_upload_create(&state->motionBuffer, maxEntities, sizeof(AnoMotionDescriptor), SLOT_STAGING_INIT, false);
}

// Slot-indexed per-entity instance channel (tint/flags/scalars). ×1 device-local + delta staging.
bool createInstanceDataBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    (void)ctx;
    return slot_upload_create(&state->instanceDataBuffer, maxEntities, sizeof(AnoInstanceData), SLOT_STAGING_INIT, false);
}

// Per-frame host-visible storage buffer set for the streamed-transform lane.
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
        if (!mintSceneBuffer(ctx, &bi, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &outBufs[i], &outAllocs[i])) return false;
        outMapped[i] = outAllocs[i].mapped;
    }
    return true;
}

// Streamed-transform lane: per-frame resolved-slot buffers + one producer-written transform ring.
bool createStreamBuffers(VulkanContext* ctx, RendererState* state, uint32_t capacity) {
    TransformStreamBuffer* ts = &state->transformStream;
    ts->capacity    = capacity;
    ts->ringSlices  = (uint32_t)MAX_FRAMES_IN_FLIGHT + 2u;   // headroom over frames in flight
    ts->sliceStride = (VkDeviceSize)capacity * sizeof(mat4); // 16-byte aligned dynamic-offset unit
    ts->produceSeq  = 0;
    atomic_store_explicit(&ts->reclaimSeq, 0, memory_order_relaxed);
    ts->curSeq      = 0;
    ts->curCount    = 0;
    ts->resolveGen  = 1;                                     // first stage runs
    ts->idRing      = NULL;                                  // render-heap, set in initVulkan
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        ts->count[f]     = 0;
        ts->dynOffset[f] = 0;
        ts->frameSeq[f]  = 0;
        ts->stagedGen[f] = 0;
    }

    // Per-frame resolved-slot buffers (binding 0).
    if (!createMappedSsboSet(ctx, (VkDeviceSize)sizeof(uint32_t) * capacity,
                             ts->slotBuffer, ts->slotAllocs, (void**)ts->slotMapped)) {
        ano_log(ANO_FATAL, "Failed to create stream slot buffer!");
        return false;
    }

    // Single producer-written transform ring of ringSlices slices.
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = (VkDeviceSize)ts->ringSlices * ts->sliceStride,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!mintSceneBuffer(ctx, &bufferInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &ts->xformRing, &ts->xformRingAlloc)) {
        ano_log(ANO_FATAL, "Failed to create stream transform ring!");
        return false;
    }
    ts->xformRingMapped = (mat4*)ts->xformRingAlloc.mapped;
    return true;
}

// props selects backing (live transform / lightRuntime = DEVICE_LOCAL). initialTransform is SlotUpload, not this path.
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

        if (!mintSceneBuffer(ctx, &bufferInfo, props, &buf->buffer[i], &buf->allocs[i])) {
            ano_log(ANO_FATAL, "Failed to create transform buffer!");
            return false;
        }

        buf->mapped[i] = (mat4*)buf->allocs[i].mapped;
    }
    return true;
}

// Per-light fragment runtime record (4x vec4 = 64B). ×MAX_FRAMES_IN_FLIGHT DEVICE_LOCAL, written by lightsetup.comp.
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
        // Async light-cull reads on compute family.
        uint32_t fams[2];
        if (rendererState.asyncLc) buffer_share_async_compute(&bufferInfo, fams);

        if (!mintSceneBuffer(ctx, &bufferInfo, props, &buf->buffer[i], &buf->allocs[i])) {
            ano_log(ANO_FATAL, "Failed to create light pose buffer!");
            return false;
        }

        buf->mapped[i] = (mat4*)buf->allocs[i].mapped;
    }
    return true;
}

bool createIndirectDrawBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxDraws) {
    state->indirectBuffer.capacity = maxDraws;
    // Larger of the two command formats (mesh-tasks 12 B, indexed 20 B).
    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
    // Partitioned by ano_draw_partition_count(), maxDraws commands per partition.
    VkDeviceSize bufferSize = cmdStride * maxDraws * ano_draw_partition_count();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->indirectBuffer.drawCount[i] = 0;
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bufferSize,
            .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        // GPU-private, written by vkCmdFillBuffer + cull.comp, read by draw-indirect.
        if (!mintSceneBuffer(ctx, &bufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             &state->indirectBuffer.buffer[i], &state->indirectBuffer.allocs[i])) {
            ano_log(ANO_FATAL, "Failed to create indirect draw buffer!");
            return false;
        }

        state->indirectBuffer.mapped[i] = (VkDrawMeshTasksIndirectCommandEXT*)state->indirectBuffer.allocs[i].mapped;
    }

    return true;
}

// Clustered-forward froxel light lists. Fixed size (ANO_CLUSTER_COUNT), one-time DEVICE_LOCAL allocation.
bool createClusterBuffers(VulkanContext* ctx, RendererState* state) {
    VkDeviceSize countSize = (VkDeviceSize)sizeof(uint32_t) * ANO_CLUSTER_COUNT;
    VkDeviceSize indexSize = (VkDeviceSize)sizeof(uint32_t) * ANO_CLUSTER_COUNT * ANO_CLUSTER_MAX_LIGHTS;

    // Per view per frame.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &state->frames[i].views[v];
            VkBufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            // Async light-cull writes on compute family, fragment passes read on graphics.
            uint32_t fams[2];
            if (rendererState.asyncLc) buffer_share_async_compute(&info, fams);

            info.size = countSize;
            if (!mintSceneBuffer(ctx, &info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 &vr->clusterLightCountBuffer, &vr->clusterLightCountAlloc)) return false;

            info.size = indexSize;
            if (!mintSceneBuffer(ctx, &info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 &vr->clusterLightIndexBuffer, &vr->clusterLightIndexAlloc)) return false;
        }
    }
    return true;
}


bool createCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->culling.maxEntities = maxEntities;
    uint32_t maxMeshes = ANO_MAX_MESHES; // must match the descriptor ranges in instanceInit.c

    // Per-view screen-area cull + LOD thresholds, runtime-overridable per view.
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; ++v) {
        state->cullPixelThreshold[v] = ANO_CULL_PIXEL_THRESHOLD_DEFAULT;
        state->lodPixelThreshold[v]  = ANO_LOD_PIXEL_THRESHOLD_DEFAULT;
        state->hizEnable[v] = 0u;                          // Hi-Z occlusion off by default
        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
            memset(state->viewProjHist[f][v], 0, sizeof(mat4)); // zero matrix -> reprojection skipped
    }
    // Test hook, enables view 0 occlusion from startup (same as the H key).
    if (getenv("ANO_HIZ_ON")) state->hizEnable[0] = 1u;
    state->shadowLodBias = ANO_SHADOW_LOD_BIAS_DEFAULT; // shadow LOD offset relative to view-0
    
    VkDeviceSize meshDataSize = sizeof(uint32_t) * 9 * maxMeshes; // MeshData 9 u32 (8 + lodCount)
    VkDeviceSize meshBoundsSize = sizeof(float) * 4 * maxMeshes; // vec4
    VkDeviceSize drawCountSize = sizeof(uint32_t) * ano_draw_partition_count();
    VkDeviceSize compactedEntityIndicesSize = sizeof(uint32_t) * maxEntities * ano_draw_partition_count();
    // Transparency sort keys, one float per camera draw slot.
    VkDeviceSize sortKeysSize = sizeof(float) * (VkDeviceSize)ANO_VIEW_COUNT * maxEntities;
    VkDeviceSize uboSize = sizeof(CullUBO);
    
    // Per-slot mesh/material, ×1 device-local + delta staging.
    if (!slot_upload_create(&state->culling.entity, maxEntities, sizeof(uint32_t) * 2u, SLOT_STAGING_INIT, false))
        return false;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Mesh Data Buffer
        VkBufferCreateInfo meshInfo = {};
        meshInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        meshInfo.size = meshDataSize;
        meshInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meshInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!mintSceneBuffer(ctx, &meshInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &state->culling.meshDataBuffer[i], &state->culling.meshDataAllocs[i])) {
            ano_log(ANO_FATAL, "Failed to create mesh data buffer!");
            return false;
        }
        state->culling.meshDataMapped[i] = state->culling.meshDataAllocs[i].mapped;

        // Mesh Bounds Buffer
        VkBufferCreateInfo boundsInfo = {};
        boundsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        boundsInfo.size = meshBoundsSize;
        boundsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        boundsInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!mintSceneBuffer(ctx, &boundsInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &state->culling.meshBoundsBuffer[i], &state->culling.meshBoundsAllocs[i])) {
            ano_log(ANO_FATAL, "Failed to create mesh bounds buffer!");
            return false;
        }
        state->culling.meshBoundsMapped[i] = state->culling.meshBoundsAllocs[i].mapped;

        // Draw Count Buffer
        VkBufferCreateInfo countInfo = {};
        countInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        countInfo.size = drawCountSize;
        countInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        countInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // GPU-private, zeroed by vkCmdFillBuffer, incremented by cull.comp, read by draw-indirect-count.
        if (!mintSceneBuffer(ctx, &countInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             &state->culling.drawCountBuffer[i], &state->culling.drawCountAllocs[i])) {
            ano_log(ANO_FATAL, "Failed to create draw count buffer!");
            return false;
        }
        state->culling.drawCountMapped[i] = (uint32_t*)state->culling.drawCountAllocs[i].mapped;

        // Compacted Entity Indices Buffer
        VkBufferCreateInfo compactedInfo = {};
        compactedInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        compactedInfo.size = compactedEntityIndicesSize;
        compactedInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        compactedInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // GPU-private, written by cull.comp, read by the geometry stage.
        if (!mintSceneBuffer(ctx, &compactedInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             &state->culling.compactedEntityIndicesBuffer[i], &state->culling.compactedEntityIndicesAllocs[i])) {
            ano_log(ANO_FATAL, "Failed to create compacted entity index buffer!");
            return false;
        }
        state->culling.compactedEntityIndicesMapped[i] = (uint32_t*)state->culling.compactedEntityIndicesAllocs[i].mapped;

        // Sort Keys Buffer, GPU-private, written by cull.comp, read by tpsort.comp.
        VkBufferCreateInfo sortKeysInfo = {};
        sortKeysInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sortKeysInfo.size = sortKeysSize;
        sortKeysInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        sortKeysInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!mintSceneBuffer(ctx, &sortKeysInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             &state->culling.sortKeysBuffer[i], &state->culling.sortKeysAllocs[i])) {
            ano_log(ANO_FATAL, "Failed to create sort keys buffer!");
            return false;
        }

        // Cull UBO
        VkBufferCreateInfo uboInfo = {};
        uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uboInfo.size = uboSize;
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!mintSceneBuffer(ctx, &uboInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &state->culling.ubo.buffer[i], &state->culling.ubo.allocs[i])) {
            ano_log(ANO_FATAL, "Failed to create cull UBO buffer!");
            return false;
        }
        state->culling.ubo.mapped[i] = (CullUBO*)state->culling.ubo.allocs[i].mapped;
    }
    return true;
}

// The pool's refusal rides straight into the two absent-mesh lanes 〜 a chain base becomes
// AnoRenderableDesc.mesh_index, which becomes slotMeshIdx 〜 so it must be the word both already
// read as "no geometry", never a slot. The one seam where all three spellings are visible.
_Static_assert(ANO_MESH_NONE == NO_MESH_INDEX && ANO_MESH_NONE == ANO_RENDER_NO_MESH,
               "mesh refusal must spell the absent-mesh word its consuming lanes read");

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
    
    // glTF CCW winding. Per triangle (a,b,c) -> (a,c,b).
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

    // Every unaddressable mesh degrades to this slot, so init without it is a lie: unwind. Covers
    // both refusal spellings 〜 ANO_MESH_NONE (pool full or transfer failed) and a wrong grant.
    if (fallbackMeshIdx != FALLBACK_MESH_INDEX) {
        ano_log(ANO_ERROR, "Fallback mesh was assigned index %u instead of %u!", fallbackMeshIdx, FALLBACK_MESH_INDEX);
        return false;
    }

    // 2. Fallback Texture (2x2 Magenta/Black Checkerboard)
    unsigned char fallbackPixels[16] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255
    };

    TexturePackage fallbackPkg; // gpu_allocator

    // Colour role, no borrowed batch.
    if (createTextureImageFromPixels(ctx, VK_NULL_HANDLE, &fallbackPkg, fallbackPixels, 2, 2,
                                     TEXTURE_USE_COLOR, false).code != ANO_TEXTURE_BUILT) {
        ano_log(ANO_WARN, "Warning: Failed to create fallback texture!");
        return false;
    }
    state->fallbackImage = fallbackPkg.image;
    state->fallbackImageView = fallbackPkg.srgbView;

    // 3. Register Fallback Texture
    uint32_t fallbackTexIdx = bindless_register_texture(ctx, &state->bindlessTextures, state->fallbackImageView, state->textureSampler);
    
    // Every unaddressable texture degrades to this slot, so init without it is a lie: unwind.
    if (fallbackTexIdx != FALLBACK_TEXTURE_INDEX) {
        ano_log(ANO_ERROR, "Fallback texture was assigned index %u instead of %u!", fallbackTexIdx, FALLBACK_TEXTURE_INDEX);
        return false;
    }

    return true;
}

// Create every slot-indexed / palette scene buffer + shadow resources in one shot.
bool ano_vk_create_scene_resources(void)
{
	rendererState.entityCount = 0;
	uint32_t maxEntities = INITIAL_ENTITY_CAPACITY;
	if (!createTransformBuffer(&ctx, &rendererState.transformBuffer, maxEntities,
	                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
	    !slot_upload_create(&rendererState.initialTransformBuffer, maxEntities, sizeof(mat4), SLOT_STAGING_INIT, false) ||
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
		return false;
	return true;
}
