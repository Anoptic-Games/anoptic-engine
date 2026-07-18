/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ensureEntityCapacity's descriptor re-point after entity-buffer growth. Growth
// recreates the per-frame live-transform SSBOs (growBufferSet destroys each old VkBuffer,
// slot_upload.c:36) but re-runs ONLY updateUboDescriptorSets (slot_upload.c:277). The
// shadowsetup descriptor set's binding 1 is that same transform SSBO (descriptors.c:357/:376)
// and its sole writer, updateShadowDescriptorSets, runs once at init (vulkanMaster.c:666) 〜 so
// after the first growth every frame's PIPELINE_COMPUTE_SHADOWSETUP dispatch (record.c:133,
// passes.c pass 2) reads world transforms through a descriptor that references a destroyed
// buffer (docs/BUGS.md, Render / Vulkan backend). This drives the REAL ensureEntityCapacity /
// updateUboDescriptorSets / updateShadowDescriptorSets over stubbed Vulkan entry points that
// record descriptor writes and destroyed handles: no device, no window. A control pins the
// cull set's binding 1 (which IS re-pointed) so a harness fault cannot mask the guard.
// Fails until growth re-runs updateShadowDescriptorSets (or re-points setupSet binding 1).
// Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/render_slots.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/instance/instanceInit.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------
// Link-time environment: the renderer globals plus Vulkan entry-point stubs.
// Handles are minted counters; descriptor writes and buffer destroys are
// recorded so the test can ask "what does this set's binding point at now?".
// ---------------------------------------------------------------------------

VulkanContext ctx;
RendererState rendererState;
GpuAllocator  gpuAllocator;

static uintptr_t g_nextHandle = 0x1000;
static void* mint(void) { return (void*)(g_nextHandle += 0x10); }

// Destroyed-buffer log.
#define MAX_DESTROYED 4096
static VkBuffer g_destroyed[MAX_DESTROYED];
static uint32_t g_destroyedCount;

static bool is_destroyed(VkBuffer b)
{
    for (uint32_t i = 0; i < g_destroyedCount; i++)
        if (g_destroyed[i] == b) return true;
    return false;
}

// Descriptor shadow state: latest buffer written per (set, binding).
typedef struct { VkDescriptorSet set; uint32_t binding; VkBuffer buf; } DescEntry;
#define MAX_DESC 4096
static DescEntry g_desc[MAX_DESC];
static uint32_t  g_descCount;

static void desc_put(VkDescriptorSet set, uint32_t binding, VkBuffer buf)
{
    for (uint32_t i = 0; i < g_descCount; i++)
        if (g_desc[i].set == set && g_desc[i].binding == binding) { g_desc[i].buf = buf; return; }
    if (g_descCount < MAX_DESC)
        g_desc[g_descCount++] = (DescEntry){ set, binding, buf };
}

static VkBuffer desc_get(VkDescriptorSet set, uint32_t binding)
{
    for (uint32_t i = 0; i < g_descCount; i++)
        if (g_desc[i].set == set && g_desc[i].binding == binding) return g_desc[i].buf;
    return VK_NULL_HANDLE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    *pBuffer = (VkBuffer)mint();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer != VK_NULL_HANDLE && g_destroyedCount < MAX_DESTROYED)
        g_destroyed[g_destroyedCount++] = buffer;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    (void)device; (void)buffer;
    // Sizes are irrelevant to the logic under test; keep the bump allocator tiny.
    pMemoryRequirements->size = 256;
    pMemoryRequirements->alignment = 16;
    pMemoryRequirements->memoryTypeBits = 1u;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    (void)device; (void)buffer; (void)memory; (void)memoryOffset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                                           uint32_t regionCount, const VkBufferCopy* pRegions)
{
    (void)commandBuffer; (void)srcBuffer; (void)dstBuffer; (void)regionCount; (void)pRegions;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                                const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    (void)device; (void)pAllocateInfo; (void)pAllocator;
    *pMemory = (VkDeviceMemory)mint();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)memory; (void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                           VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
{
    (void)device; (void)memory; (void)offset; (void)flags;
    // Nothing under test writes through staging here; a small backing block suffices.
    (void)size;
    *ppData = calloc(1u, 1u << 20);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { (void)device; (void)memory; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    *pDescriptorPool = (VkDescriptorPool)mint();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                                        VkDescriptorSet* pDescriptorSets)
{
    (void)device;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
        pDescriptorSets[i] = (VkDescriptorSet)mint();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                                  const VkWriteDescriptorSet* pDescriptorWrites,
                                                  uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{
    (void)device; (void)descriptorCopyCount; (void)pDescriptorCopies;
    for (uint32_t w = 0; w < descriptorWriteCount; w++) {
        const VkWriteDescriptorSet* dw = &pDescriptorWrites[w];
        bool buffered = dw->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                     || dw->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                     || dw->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        if (!buffered || dw->pBufferInfo == NULL)
            continue;
        for (uint32_t j = 0; j < dw->descriptorCount; j++)
            desc_put(dw->dstSet, dw->dstBinding + j, dw->pBufferInfo[j].buffer);
    }
}

// Transient-command stubs (slot_upload_grow_device's device-side preserve copy).
VkCommandBuffer beginSingleTimeCommands(VulkanContext* c) { (void)c; return (VkCommandBuffer)mint(); }
void endSingleTimeCommands(VulkanContext* c, VkCommandBuffer commandBuffer) { (void)c; (void)commandBuffer; }

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------

int main(void)
{
    memset(&ctx, 0, sizeof ctx);
    memset(&rendererState, 0, sizeof rendererState);
    memset(&gpuAllocator, 0, sizeof gpuAllocator);

    ctx.device = (VkDevice)mint();
    gpuAllocator.device = ctx.device;
    gpuAllocator.memProps.memoryTypeCount = 1;
    gpuAllocator.memProps.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Slot table at a small ceiling so a one-entity overshoot forces growth.
    mi_heap_t* heap = mi_heap_new();
    const uint32_t initialCap = 8u;
    CHECK(render_slots_init(&rendererState.slots, heap, initialCap, MAX_FRAMES_IN_FLIGHT), "slot table init");

    // Entity-scaled buffers exactly as initVulkan leaves them.
    CHECK(slot_upload_create(&rendererState.initialTransformBuffer, initialCap, sizeof(mat4), 4, false), "initialTransform SlotUpload");
    CHECK(slot_upload_create(&rendererState.motionBuffer, initialCap, sizeof(AnoMotionDescriptor), 4, false), "motion SlotUpload");
    CHECK(slot_upload_create(&rendererState.instanceDataBuffer, initialCap, sizeof(AnoInstanceData), 4, false), "instanceData SlotUpload");
    CHECK(slot_upload_create(&rendererState.culling.entity, initialCap, sizeof(uint32_t) * 2u, 4, false), "entity SlotUpload");
    rendererState.culling.maxEntities = initialCap;

    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 256 };
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateBuffer(ctx.device, &bi, NULL, &rendererState.transformBuffer.buffer[i]);
        vkCreateBuffer(ctx.device, &bi, NULL, &rendererState.culling.compactedEntityIndicesBuffer[i]);
        vkCreateBuffer(ctx.device, &bi, NULL, &rendererState.indirectBuffer.buffer[i]);
        vkCreateBuffer(ctx.device, &bi, NULL, &rendererState.culling.sortKeysBuffer[i]);
        rendererState.culling.ubo.mapped[i] = calloc(1, sizeof(CullUBO));
    }
    rendererState.transformBuffer.capacity = initialCap;
    rendererState.indirectBuffer.capacity = initialCap;

    // Descriptor sets as init allocates them (fake handles; the recorder keys on them).
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
            rendererState.frames[i].views[v].globalSet = (VkDescriptorSet)mint();
        rendererState.frames[i].cullSet          = (VkDescriptorSet)mint();
        rendererState.frames[i].updateSet        = (VkDescriptorSet)mint();
        rendererState.frames[i].scatterSet       = (VkDescriptorSet)mint();
        rendererState.frames[i].lightsetupSet    = (VkDescriptorSet)mint();
        rendererState.frames[i].shadow.setupSet  = (VkDescriptorSet)mint();
        rendererState.frames[i].shadow.geomSet   = (VkDescriptorSet)mint();
        rendererState.frames[i].shadow.blurAtlasSet = (VkDescriptorSet)mint();
        rendererState.frames[i].shadow.blurTempSet  = (VkDescriptorSet)mint();
    }

    // Init-time descriptor binding, in initVulkan order.
    updateUboDescriptorSets(&ctx, &rendererState);
    updateShadowDescriptorSets(&ctx, &rendererState);

    // Precondition: shadowsetup binding 1 is the live transform SSBO.
    VkBuffer oldTransforms[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        oldTransforms[i] = rendererState.transformBuffer.buffer[i];
        CHECK(desc_get(rendererState.frames[i].shadow.setupSet, 1) == oldTransforms[i],
              "precondition: setupSet binding 1 == transform SSBO");
    }

    // Grow past the ceiling, as the bridge's CREATE path does at capacity.
    CHECK(ensureEntityCapacity(&rendererState, initialCap + 1u, 0), "ensureEntityCapacity grows");

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBuffer neu = rendererState.transformBuffer.buffer[i];
        CHECK(neu != oldTransforms[i], "growth recreated the transform SSBO");
        CHECK(is_destroyed(oldTransforms[i]), "growth destroyed the old transform SSBO");

        // Control: the cull set's transform binding IS re-pointed (harness + re-point path work).
        CHECK(desc_get(rendererState.frames[i].cullSet, 1) == neu,
              "control: cullSet binding 1 re-pointed at the regrown transform SSBO");

        // Guard: the shadowsetup set must not keep referencing the destroyed buffer.
        VkBuffer bound = desc_get(rendererState.frames[i].shadow.setupSet, 1);
        CHECK(bound == neu,
              "setupSet binding 1 re-pointed at the regrown transform SSBO (shadowsetup.comp input)");
        CHECK(!is_destroyed(bound),
              "setupSet binding 1 does not reference a destroyed VkBuffer");
    }

    if (failures) {
        printf("anotest_vkguard: %d failure(s)\n", failures);
        return 1;
    }
    printf("anotest_vkguard: all checks passed\n");
    return 0;
}
