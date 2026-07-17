/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: mid-walk publication in ensureEntityCapacity's growth chain. Each && arm at
// slot_upload.c:219-:234 publishes as it succeeds 〜 slot_upload_grow_device destroys the old device
// buffer and swaps in the new one per arm (:191-:193) 〜 while updateUboDescriptorSets is gated on full
// success (:276-:277), so a later arm's OOM failure returns false at :258 with the already-swapped
// prefix arms' old VkBuffers destroyed and the live descriptor sets still referencing them
// (descriptors.c:550/:743/:748 bind SlotUpload.device by handle). ano_log(ANO_FATAL) at :257 is plain
// ano_log_write and never aborts, both callers drop the spawn and keep rendering (apply.c:123-:124 /
// :164-:165), so the next recorded frame binds destroyed buffers 〜 GPU use-after-free / device-lost
// under exactly the device-memory pressure that made growth fail (docs/BUGS.md, Render / Vulkan
// backend / Implementation, slot_upload.c:221).
// Harness: compiles the REAL slot_upload.c TU and satisfies its link seams with stubs 〜 no GPU device,
// no loader. The vk stubs keep a mint/destroy ledger keyed by handle; the updateUboDescriptorSets stub
// snapshots every buffer handle a descriptor write would bind out of RendererState (the four SlotUpload
// device buffers + the three per-frame grow sets + sort keys), exactly the set descriptors.c reads.
// Controls prove full growth succeeds, re-points the sets, and discharges every old device handle, and
// that a FIRST-arm failure reports false with descriptors intact 〜 so a reject-everything fix cannot
// pass. Fix-agnostic invariant: whenever ensureEntityCapacity returns, every handle the descriptor
// snapshot references is live 〜 satisfied by deferring the destroys, unwinding the prefix, or
// re-pointing the sets on the failure path. A crash is a valid failure signal. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"    // RendererState, instanceInit.h seam signatures
#include "vulkan_backend/backend.h"         // ctx extern, ENTITY_GROWTH_CHUNK
#include "vulkan_backend/gpu_alloc.h"       // gpu_alloc, GpuAllocation
#include "vulkan_backend/slot_upload.h"     // slot_upload_create, ensureEntityCapacity
#include "vulkan_backend/components.h"      // ano_draw_partition_count seam signature
#include "vulkan_backend/render_slots.h"    // render_slots_set_capacity seam signature

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledger 〜 every VkBuffer this harness mints and its live state */

#define MAX_BUFS 256
static VkBuffer     g_minted[MAX_BUFS];
static VkDeviceSize g_mintSize[MAX_BUFS];
static bool         g_liveFlag[MAX_BUFS];
static uint32_t     g_mintCount;
static void        *g_shadow[MAX_BUFS];   // gpu_alloc host shadows, freed at exit
static uint32_t     g_shadowCount;
static uint32_t     g_wildReqs;           // vkGetBufferMemoryRequirements on a handle never minted
static uint32_t     g_wildBinds;          // vkBindBufferMemory on a handle never minted
static uint32_t     g_wildCopies;         // vkCmdCopyBuffer src/dst not minted or not live
static uint32_t     g_doubleDestroys;     // whole-run invariant
static uint32_t     g_unknownDestroys;    // whole-run invariant

/* Failure injection: the Nth vkCreateBuffer after arming fails, out-param untouched */
static int g_failCreateAt;

// Finds a minted handle's ledger index, or -1.
static int mint_index(VkBuffer h)
{
    for (uint32_t i = 0; i < g_mintCount; i++) if (g_minted[i] == h) return (int)i;
    return -1;
}

// True iff handle is minted and not yet destroyed.
static bool handle_live(VkBuffer h)
{
    int idx = mint_index(h);
    return idx >= 0 && g_liveFlag[idx];
}


/* Descriptor snapshot 〜 what the live descriptor sets reference, per the last update call */

#define DESC_MAX 32
static VkBuffer g_descRefs[DESC_MAX];
static uint32_t g_descRefCount;
static uint32_t g_descUpdateCalls;

// True iff every buffer handle the descriptor sets reference is still live.
static bool desc_refs_all_live(void)
{
    for (uint32_t i = 0; i < g_descRefCount; i++)
        if (!handle_live(g_descRefs[i])) return false;
    return true;
}

// Prints each stale (destroyed-but-still-referenced) descriptor handle.
static void desc_report_stale(void)
{
    for (uint32_t i = 0; i < g_descRefCount; i++)
        if (!handle_live(g_descRefs[i]))
            printf("  stale descriptor ref: buffer %p destroyed with the sets never re-pointed\n", (void*)g_descRefs[i]);
}


/* Link seams 〜 globals slot_upload.c references (real definitions live in vulkanMaster.c / gpu_alloc.c) */

VulkanContext ctx;
RendererState rendererState;
GpuAllocator  gpuAllocator;


/* Link seams 〜 helper functions slot_upload.c calls */

// in: requirements from the vk stub; out: fake device memory + host shadow as the mapping.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)alloc; (void)props;
    void *shadow = NULL;
    if (g_shadowCount < MAX_BUFS) {
        shadow = calloc(1, (size_t)reqs.size);
        g_shadow[g_shadowCount++] = shadow;
    }
    return (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x51, .offset = 0, .size = reqs.size, .mapped = shadow };
}

uint32_t ano_draw_partition_count(void) { return 1u; }

// Mirrors the real never-shrinks ceiling raise; records the call for the controls.
static uint32_t g_lastSetCapacity;
void render_slots_set_capacity(RenderSlotTable* table, uint32_t newCapacity)
{
    if (newCapacity > table->slotCapacity) table->slotCapacity = newCapacity;
    g_lastSetCapacity = newCapacity;
}

// Descriptor seam under audit: snapshots every buffer handle descriptors.c would bind from state.
void updateUboDescriptorSets(VulkanContext* c, RendererState* state)
{
    (void)c;
    g_descUpdateCalls++;
    g_descRefCount = 0;
    VkBuffer devs[4] = { state->initialTransformBuffer.device, state->motionBuffer.device,
                         state->instanceDataBuffer.device, state->culling.entity.device };
    for (int i = 0; i < 4; i++)
        if (devs[i] != VK_NULL_HANDLE && g_descRefCount < DESC_MAX) g_descRefs[g_descRefCount++] = devs[i];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBuffer per[4] = { state->transformBuffer.buffer[i], state->culling.compactedEntityIndicesBuffer[i],
                            state->indirectBuffer.buffer[i], state->culling.sortKeysBuffer[i] };
        for (int k = 0; k < 4; k++)
            if (per[k] != VK_NULL_HANDLE && g_descRefCount < DESC_MAX) g_descRefs[g_descRefCount++] = per[k];
    }
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* c) { (void)c; return (VkCommandBuffer)(uintptr_t)0xC0FFEE; }
void endSingleTimeCommands(VulkanContext* c, VkCommandBuffer commandBuffer) { (void)c; (void)commandBuffer; }


/* Link seams 〜 the vk* entry points slot_upload.c calls (loader not linked) */

// Failure arm is contract-faithful: error status, *pBuffer untouched (undefined contents on error).
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    (void)device; (void)pAllocator;
    if (g_failCreateAt > 0 && --g_failCreateAt == 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (g_mintCount >= MAX_BUFS) { printf("FAIL: ledger overflow\n"); failures++; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    VkBuffer handle = (VkBuffer)(uintptr_t)(0x1000u + g_mintCount);
    g_minted[g_mintCount] = handle;
    g_mintSize[g_mintCount] = pCreateInfo->size;
    g_liveFlag[g_mintCount] = true;
    g_mintCount++;
    *pBuffer = handle;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    (void)device;
    int idx = mint_index(buffer);
    if (idx < 0) g_wildReqs++;
    pMemoryRequirements->size = idx >= 0 ? g_mintSize[idx] : 4096;
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    (void)device; (void)memory; (void)memoryOffset;
    if (mint_index(buffer) < 0) g_wildBinds++;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer == VK_NULL_HANDLE) return; // spec no-op
    int idx = mint_index(buffer);
    if (idx < 0) { g_unknownDestroys++; return; }
    if (!g_liveFlag[idx]) g_doubleDestroys++;
    g_liveFlag[idx] = false;
}

// The grow keep-copy must read and write live handles only.
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
{
    (void)commandBuffer; (void)regionCount; (void)pRegions;
    if (!handle_live(srcBuffer)) g_wildCopies++;
    if (!handle_live(dstBuffer)) g_wildCopies++;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { (void)device; return VK_SUCCESS; }


/* State setup 〜 the entity-scaled slice of RendererState ensureEntityCapacity walks */

static CullUBO g_cullUbo;

// in: state to populate, starting slot ceiling
// inv: four real SlotUploads minted through the TU under test; grow-set arms start empty (resize-only);
//      globalSet non-null so the :276 descriptor gate is armed; CullUBO mapped for the :273 realign.
static void setup_state(RendererState* rs, uint32_t oldCap)
{
    memset(rs, 0, sizeof *rs);
    rs->slots.slotCapacity = oldCap;
    bool ok = slot_upload_create(&rs->initialTransformBuffer, oldCap, 64u, 4u, false)
           && slot_upload_create(&rs->motionBuffer,           oldCap, 48u, 4u, false)
           && slot_upload_create(&rs->instanceDataBuffer,     oldCap, 16u, 4u, false)
           && slot_upload_create(&rs->culling.entity,         oldCap,  8u, 4u, false);
    CHECK(ok, "setup: slot_upload_create x4 succeeds");
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) rs->culling.ubo.mapped[i] = &g_cullUbo;
    rs->frames[0].views[0].globalSet = (VkDescriptorSet)(uintptr_t)0xD5;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ctx.device = (VkDevice)(uintptr_t)0x77; // every seam stub ignores it
    static RendererState rsGood, rsFirst, rsMid;
    const uint32_t oldCap = 8u;

    // control: full growth succeeds, discharges every old device buffer, and re-points the sets
    setup_state(&rsGood, oldCap);
    updateUboDescriptorSets(&ctx, &rsGood); // boot-time descriptor write
    VkBuffer oldDevs[4] = { rsGood.initialTransformBuffer.device, rsGood.motionBuffer.device,
                            rsGood.instanceDataBuffer.device, rsGood.culling.entity.device };
    uint32_t updatesBefore = g_descUpdateCalls;
    CHECK(ensureEntityCapacity(&rsGood, oldCap + 1u, 0), "control growth succeeds");
    CHECK(g_descUpdateCalls == updatesBefore + 1u, "successful growth re-points the descriptor sets");
    CHECK(desc_refs_all_live(), "after successful growth every descriptor ref is live");
    for (int i = 0; i < 4; i++) CHECK(!handle_live(oldDevs[i]), "successful growth discharges the old device buffer");
    CHECK(rsGood.slots.slotCapacity == ENTITY_GROWTH_CHUNK && g_lastSetCapacity == ENTITY_GROWTH_CHUNK, "slot ceiling raised to the grown capacity");
    CHECK(g_cullUbo.maxEntities == ENTITY_GROWTH_CHUNK, "CullUBO maxEntities realigned");
    CHECK(g_wildReqs == 0 && g_wildBinds == 0 && g_wildCopies == 0, "control growth consumes only live minted handles");

    // control: a FIRST-arm failure reports false with nothing yet published 〜 descriptors stay intact,
    // so failure reporting itself works and a reject-everything fix cannot pass
    setup_state(&rsFirst, oldCap);
    updateUboDescriptorSets(&ctx, &rsFirst);
    g_failCreateAt = 1; // arm 1's vkCreateBuffer refuses
    CHECK(!ensureEntityCapacity(&rsFirst, oldCap + 1u, 0), "first-arm failure reports false");
    CHECK(g_failCreateAt == 0, "first-arm injection consumed");
    CHECK(desc_refs_all_live(), "first-arm failure leaves every descriptor ref live");

    // trigger: arm 1 (initialTransformBuffer) grows and publishes 〜 old device buffer destroyed at
    // slot_upload.c:191, swap at :192-:193 〜 then arm 2 (motionBuffer) hits the injected GPU OOM at
    // :179 and the chain returns false at :258 without reaching updateUboDescriptorSets (:276)
    printf("trigger: second-arm GPU OOM 〜 expect the prefix arm's destroyed device buffer still descriptor-referenced\n");
    fflush(stdout);
    setup_state(&rsMid, oldCap);
    updateUboDescriptorSets(&ctx, &rsMid);
    VkBuffer prefixOld = rsMid.initialTransformBuffer.device;
    uint32_t updatesBeforeTrigger = g_descUpdateCalls;
    g_failCreateAt = 2; // create #1 = arm 1's new device buffer, create #2 = arm 2's, injected to fail
    bool ok = ensureEntityCapacity(&rsMid, oldCap + 1u, 0);
    CHECK(!ok, "mid-walk failure reports false");
    CHECK(g_failCreateAt == 0, "mid-walk injection consumed");
    printf("step: arm 1 swapped (device %p -> %p), old handle %s; arm 2 refused; descriptor updates during failed growth: %u\n",
           (void*)prefixOld, (void*)rsMid.initialTransformBuffer.device,
           handle_live(prefixOld) ? "LIVE" : "DESTROYED", g_descUpdateCalls - updatesBeforeTrigger);
    if (!desc_refs_all_live()) desc_report_stale();
    CHECK(desc_refs_all_live(), "no live descriptor set references a destroyed VkBuffer after failed growth (slot_upload.c:191 published, :276 never re-pointed)");

    // whole-run ledger invariants 〜 a fix must not destroy handles it never minted or destroy twice
    CHECK(g_doubleDestroys == 0, "no buffer destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unminted buffer handle destroyed");
    CHECK(g_wildReqs == 0 && g_wildBinds == 0 && g_wildCopies == 0, "no wild handle consumption anywhere");

    for (uint32_t i = 0; i < g_shadowCount; i++) free(g_shadow[i]);

    if (failures) {
        printf("anotest_entitygrowguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_entitygrowguard: all passed\n");
    return 0;
}
