/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <anoptic_log.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/slot_upload.h"

// A replacement buffer, constructed but not yet installed. buf == VK_NULL_HANDLE => holds nothing.
typedef struct { VkBuffer buf; GpuAllocation alloc; } GrownBuffer;

// Discharge a replacement that was built but never installed. Total; safe on an empty slot.
// gpu_alloc is an arena with no per-allocation free, so only the handle comes back.
static void grown_discard(GrownBuffer* g)
{
    vkDestroyBuffer(ctx.device, g->buf, NULL);
    g->buf = VK_NULL_HANDLE;
}

// Build one per-frame replacement set of newBytes each. False leaves the built prefix in out[]
// for the caller to discard; nothing already live is touched either way.
// A refused bind is a refusal: the buffer exists but is unbacked, and recording against it is UB.
static bool growBufferSetBuild(GrownBuffer out[MAX_FRAMES_IN_FLIGHT],
                               VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkDeviceSize newBytes)
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bi = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = newBytes, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(ctx.device, &bi, NULL, &out[i].buf) != VK_SUCCESS) {
            out[i].buf = VK_NULL_HANDLE; // out-param indeterminate on failure
            return false;
        }
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(ctx.device, out[i].buf, &mr);
        out[i].alloc = gpu_alloc(&gpuAllocator, mr, props);
        if (out[i].alloc.memory == VK_NULL_HANDLE) return false;
        if (vkBindBufferMemory(ctx.device, out[i].buf, out[i].alloc.memory, out[i].alloc.offset) != VK_SUCCESS) return false;
    }
    return true;
}

// Install a fully built per-frame set and discharge the handles it replaces. Total: cannot fail.
static void growBufferSetCommit(VkBuffer bufs[MAX_FRAMES_IN_FLIGHT],
                                GpuAllocation allocs[MAX_FRAMES_IN_FLIGHT],
                                GrownBuffer grown[MAX_FRAMES_IN_FLIGHT])
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(ctx.device, bufs[i], NULL); // handle only
        bufs[i]   = grown[i].buf;
        allocs[i] = grown[i].alloc;
    }
}

// SlotUpload: x1 DEVICE_LOCAL per-slot buffer fed by a per-frame host-visible
// delta staging ring. Render-thread only.

// Applies gfx+compute CONCURRENT sharing to a buffer the async light-cull touches across queue families.
// fams must outlive the vkCreateBuffer call.
void buffer_share_async_compute(VkBufferCreateInfo* bi, uint32_t fams[2])
{
    fams[0] = ctx.queueFamilyIndices.graphicsFamily;
    fams[1] = ctx.queueFamilyIndices.computeFamily;
    bi->sharingMode = VK_SHARING_MODE_CONCURRENT;
    bi->queueFamilyIndexCount = 2;
    bi->pQueueFamilyIndices = fams;
}

// Create SlotUpload: DEVICE_LOCAL + per-frame staging. computeShared => CONCURRENT with async light-cull compute.
// False on any refused create, allocation or bind; the caller owns the partial *b either way.
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
    if (vkBindBufferMemory(ctx.device, b->device, b->deviceAlloc.memory, b->deviceAlloc.offset) != VK_SUCCESS) return false;

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
        if (vkBindBufferMemory(ctx.device, b->staging[i], b->stagingAllocs[i].memory, b->stagingAllocs[i].offset) != VK_SUCCESS) return false;
        b->stagingMapped[i] = b->stagingAllocs[i].mapped;
        b->regions[i] = (VkBufferCopy*)malloc((size_t)b->stagingCap * sizeof(VkBufferCopy));
        if (!b->regions[i]) return false;
        b->staged[i] = 0;
    }
    return true;
}

// Grows every staging buffer + region list to hold >= need delta entries. Caller holds vkDeviceWaitIdle.
// Preserves the current frame's already-staged span.
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
        if (vkBindBufferMemory(ctx.device, nb, na.memory, na.offset) != VK_SUCCESS) { vkDestroyBuffer(ctx.device, nb, NULL); return false; }
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
// Best-effort: a host-OOM growth silently drops the write.
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

// Build the device buffer's replacement at newCap and GPU-copy [0, keep) forward from the live one.
// Caller holds vkDeviceWaitIdle. b is left untouched on both arms; false leaves *out for the caller to discard.
// true means [0, keep) actually landed: a refused mint, bind or submit/wait answers false.
static bool slot_upload_build_device(const SlotUpload* b, uint32_t newCap, uint32_t keep, GrownBuffer* out)
{
    VkBufferCreateInfo di = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)b->stride * newCap,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    uint32_t fams[2];
    if (b->computeShared) buffer_share_async_compute(&di, fams);
    if (vkCreateBuffer(ctx.device, &di, NULL, &out->buf) != VK_SUCCESS) {
        out->buf = VK_NULL_HANDLE; // out-param indeterminate on failure
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device, out->buf, &mr);
    out->alloc = gpu_alloc(&gpuAllocator, mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (out->alloc.memory == VK_NULL_HANDLE) return false;
    if (vkBindBufferMemory(ctx.device, out->buf, out->alloc.memory, out->alloc.offset) != VK_SUCCESS) return false;
    if (keep) {
        VkCommandBuffer c = beginSingleTimeCommands(&ctx);
        if (c == VK_NULL_HANDLE) return false; // no transient CB: [0,keep) cannot be preserved, so refuse
        VkBufferCopy region = { .srcOffset = 0, .dstOffset = 0, .size = (VkDeviceSize)b->stride * keep };
        vkCmdCopyBuffer(c, b->device, out->buf, 1, &region);
        // A dropped status here would publish a replacement whose preserved span is garbage.
        if (!endSingleTimeCommandsChecked(&ctx, c)) return false;
    }
    return true;
}

// Install a fully built device replacement at newCap and discharge the handle it replaces. Total.
static void slot_upload_commit_device(SlotUpload* b, GrownBuffer* grown, uint32_t newCap)
{
    vkDestroyBuffer(ctx.device, b->device, NULL); // handle only
    b->device      = grown->buf;
    b->deviceAlloc = grown->alloc;
    b->capacity    = newCap;
}

// in: state, required slot count, frame index; out: true once every entity-scaled buffer holds >= required.
// Idle + build all replacements + install + rebind descriptors. Commit-last: false leaves every buffer,
// capacity and descriptor set exactly as it found them, so a refused growth is safe to keep rendering through.
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

    // Commit-last: construct every replacement first, install and destroy only once all of them exist,
    // so a mid-chain failure can never leave a descriptor set naming a destroyed VkBuffer.
    GrownBuffer devNew[4] = {};
    GrownBuffer setNew[4][MAX_FRAMES_IN_FLIGHT] = {};

    bool ok =
        // x1 device-local per-slot data: GPU-copy the live [0,oldCap) span forward
        slot_upload_build_device(&state->initialTransformBuffer, newCap, oldCap, &devNew[0]) &&
        slot_upload_build_device(&state->motionBuffer, newCap, oldCap, &devNew[1]) &&
        slot_upload_build_device(&state->instanceDataBuffer, newCap, oldCap, &devNew[2]) &&
        slot_upload_build_device(&state->culling.entity, newCap, oldCap, &devNew[3]) &&
        // GPU-private, regenerated each frame: DEVICE_LOCAL, resize only (no copy)
        growBufferSetBuild(setNew[0], ssbo, devProps, (VkDeviceSize)sizeof(mat4) * newCap) &&
        growBufferSetBuild(setNew[1], ssbo, devProps,
                           (VkDeviceSize)sizeof(uint32_t) * newCap * ano_draw_partition_count()) &&
        growBufferSetBuild(setNew[2],
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           devProps, cmdStride * newCap * ano_draw_partition_count()) &&
        growBufferSetBuild(setNew[3], ssbo, devProps,
                           (VkDeviceSize)sizeof(float) * (VkDeviceSize)ANO_VIEW_COUNT * newCap);
    // Mover bookkeeping tracks every slot: grow in lockstep or fail the create.
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
        // Nothing was installed: discharge the replacements and leave every live descriptor's handle intact.
        for (int i = 0; i < 4; i++) grown_discard(&devNew[i]);
        for (int i = 0; i < 4; i++)
            for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) grown_discard(&setNew[i][f]);
        ano_log(ANO_FATAL, "Fatal: entity capacity growth %u -> %u failed (GPU out of memory?).", oldCap, newCap);
        return false;
    }

    // Every replacement exists: install them, then discharge the handles they replace.
    slot_upload_commit_device(&state->initialTransformBuffer, &devNew[0], newCap);
    slot_upload_commit_device(&state->motionBuffer,           &devNew[1], newCap);
    slot_upload_commit_device(&state->instanceDataBuffer,     &devNew[2], newCap);
    slot_upload_commit_device(&state->culling.entity,         &devNew[3], newCap);
    growBufferSetCommit(state->transformBuffer.buffer, state->transformBuffer.allocs, setNew[0]);
    growBufferSetCommit(state->culling.compactedEntityIndicesBuffer, state->culling.compactedEntityIndicesAllocs, setNew[1]);
    growBufferSetCommit(state->indirectBuffer.buffer, state->indirectBuffer.allocs, setNew[2]);
    growBufferSetCommit(state->culling.sortKeysBuffer, state->culling.sortKeysAllocs, setNew[3]);

    // Re-derive mapped pointers for the GPU-private buffers.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->transformBuffer.mapped[i]               = (mat4*)state->transformBuffer.allocs[i].mapped;
        state->culling.compactedEntityIndicesMapped[i] = (uint32_t*)state->culling.compactedEntityIndicesAllocs[i].mapped;
        state->indirectBuffer.mapped[i]                = (VkDrawMeshTasksIndirectCommandEXT*)state->indirectBuffer.allocs[i].mapped;
    }
    state->transformBuffer.capacity = newCap;
    state->indirectBuffer.capacity  = newCap;
    state->culling.maxEntities      = newCap;
    render_slots_set_capacity(&state->slots, newCap);

    // Realign this frame's CullUBO maxEntities to newCap.
    state->culling.ubo.mapped[frameIndex]->maxEntities = newCap;

    // Re-point every descriptor at the new handles/ranges. Skipped before the descriptor sets exist.
    if (state->frames[0].views[0].globalSet != VK_NULL_HANDLE)
        updateUboDescriptorSets(&ctx, state);

    ano_log(ANO_INFO, "Entity capacity grown: %u -> %u slots.", oldCap, newCap);
    return true;
}
