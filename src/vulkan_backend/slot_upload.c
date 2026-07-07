/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <anoptic_logging.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/slot_upload.h"

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
// staging ring. Replaces the former ×3
// host-visible mapped buffers for the command-written per-slot data. Render-thread only.
// Uses the file-global ctx/gpuAllocator like growBufferSet.
// ---------------------------------------------------------------------------

// Applies gfx+compute CONCURRENT sharing to a buffer the async light-cull touches across queue
// families (an EXCLUSIVE buffer's contents are undefined to the other family without ownership-
// transfer barriers; CONCURRENT trades that bookkeeping for a negligible access cost). fams must
// outlive the vkCreateBuffer call.
void buffer_share_async_compute(VkBufferCreateInfo* bi, uint32_t fams[2])
{
    fams[0] = ctx.queueFamilyIndices.graphicsFamily;
    fams[1] = ctx.queueFamilyIndices.computeFamily;
    bi->sharingMode = VK_SHARING_MODE_CONCURRENT;
    bi->queueFamilyIndexCount = 2;
    bi->pQueueFamilyIndices = fams;
}

// in:  b, capacity (device elements), stride (bytes/elem), stagingCap (initial delta budget),
//      computeShared (device buffer read by the async light-cull's compute family)
// out: device DEVICE_LOCAL buffer + N host-visible staging buffers + region arrays; false on failure
bool slot_upload_create(SlotUpload* b, uint32_t capacity, uint32_t stride, uint32_t stagingCap, bool computeShared)
{
    memset(b, 0, sizeof(*b));
    b->capacity      = capacity;
    b->stride        = stride;
    b->stagingCap    = stagingCap ? stagingCap : 1u;
    b->computeShared = computeShared && rendererState.asyncLc;

    VkBufferCreateInfo di = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)stride * capacity,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    uint32_t fams[2];
    if (b->computeShared) buffer_share_async_compute(&di, fams);
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
void slot_upload_stage(SlotUpload* b, uint32_t f, uint32_t index, const void* value)
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
void slot_upload_flush(VkCommandBuffer cmd, SlotUpload* b, uint32_t f)
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
    uint32_t fams[2];
    if (b->computeShared) buffer_share_async_compute(&di, fams); // sharing survives growth
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
bool ensureEntityCapacity(RendererState* state, uint32_t required, uint32_t frameIndex)
{
    uint32_t oldCap = state->slots.slotCapacity;
    if (required <= oldCap) return true;

    uint32_t newCap = oldCap ? oldCap * 2u : ENTITY_GROWTH_CHUNK;
    if (newCap < required) newCap = required;
    newCap = ((newCap + ENTITY_GROWTH_CHUNK - 1u) / ENTITY_GROWTH_CHUNK) * ENTITY_GROWTH_CHUNK;
    if (newCap < required) { // round-up overflow
        ano_log(ANO_FATAL, "Fatal: entity capacity request %u exceeds addressable range.", required);
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
    // Mover bookkeeping must track every slot (review finding 8): grow in lockstep or fail the
    // create. The swept-exposure mirrors (pose/mesh/mover map) ride the same capacity.
    if (ok) {
        uint32_t oldMc = state->slotMotionCap;
        uint8_t*  nm = (uint8_t*)realloc(state->slotMotion, newCap);
        mat4*     np = (mat4*)realloc(state->slotBasePose, (size_t)newCap * sizeof(mat4));
        uint32_t* nx = (uint32_t*)realloc(state->slotMeshIdx, (size_t)newCap * sizeof(uint32_t));
        uint32_t* nv = (uint32_t*)realloc(state->slotMoverIdx, (size_t)newCap * sizeof(uint32_t));
        if (nm) state->slotMotion = nm;
        if (np) state->slotBasePose = np;
        if (nx) state->slotMeshIdx = nx;
        if (nv) state->slotMoverIdx = nv;
        if (nm && np && nx && nv) {
            memset(nm + oldMc, 0, newCap - oldMc);
            memset(np + oldMc, 0, (size_t)(newCap - oldMc) * sizeof(mat4));
            memset(nx + oldMc, 0xFF, (size_t)(newCap - oldMc) * sizeof(uint32_t)); // NO_MESH_INDEX
            memset(nv + oldMc, 0xFF, (size_t)(newCap - oldMc) * sizeof(uint32_t)); // UNMAPPED
            state->slotMotionCap = newCap;
        } else {
            ok = false;
        }
    }
    if (!ok) {
        ano_log(ANO_FATAL, "Fatal: entity capacity growth %u -> %u failed (GPU out of memory?).", oldCap, newCap);
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

    ano_log(ANO_INFO, "Entity capacity grown: %u -> %u slots.", oldCap, newCap);
    return true;
}
