/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the FATAL-log-instead-of-return family in the scene buffer creators. createMaterialBuffer's
// vkCreateBuffer failure arm at scene_buffers.c:34-36 logs ANO_FATAL and falls through 〜 ano_log(ANO_FATAL)
// is ano_log_write, which formats a record and returns 0; nothing in src/log aborts 〜 so :39 feeds
// vkGetBufferMemoryRequirements the failed call's out-param (undefined on error per the Vulkan spec, an
// invalid-handle VUID breach), gpu_alloc sizes from garbage requirements, :46 binds the garbage handle, and
// the creator returns true. createTransformBuffer repeats the shape at :174-176/:179/:186 on the engine's
// transform lane, and createCullingBuffers discards all six of its vkCreateBuffer results outright
// (:343/:359/:375/:392/:409/:424); the same file's siblings prove the intended contract by returning false
// on the identical check (createLightRuntimeBuffer :212, createIndirectDrawBuffer :249, createClusterBuffers
// :288/:295). Boot reach: ano_vk_create_scene_resources :499/:505/:510 (docs/BUGS.md, Render / Vulkan
// backend / Implementation, scene_buffers.c:35).
// Harness: compiles the REAL scene_buffers.c TU and satisfies its link seams with stubs 〜 no GPU device,
// no loader. The vk stubs keep a mint ledger keyed by handle; the failing vkCreateBuffer arm is
// contract-faithful (returns VK_ERROR_OUT_OF_DEVICE_MEMORY and leaves *pBuffer untouched, since output
// params have undefined contents on error), so the test pre-poisons each handle slot and any stub fed a
// handle the ledger never minted counts a wild consumption deterministically 〜 no crash needed. Controls
// prove the good path mints/binds/maps every frame's buffer and that the gpu_alloc failure arm the creators
// DO check reports false and balances the ledger, so a reject-everything fix cannot pass. Fix-agnostic
// invariants: a failed create returns false, feeds no unminted handle to any vk entry point, and never
// destroys a handle it did not mint. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"    // RendererState, rendererState extern, texture.h
#include "vulkan_backend/backend.h"         // ctx extern
#include "vulkan_backend/gpu_alloc.h"       // gpu_alloc, allocator globals
#include "vulkan_backend/slot_upload.h"     // slot_upload_create, buffer_share_async_compute
#include "vulkan_backend/components.h"      // ano_draw_partition_count
#include "vulkan_backend/geometry.h"        // geometry_pool_upload
#include "vulkan_backend/shadow/shadow.h"   // createShadowResources

// TU-internal creators under test (external linkage, no header declares them).
bool createMaterialBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities);
bool createTransformBuffer(VulkanContext* ctx, TransformBuffer* buf, uint32_t maxEntities, VkMemoryPropertyFlags props);
bool createCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t maxEntities);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define POISON(k) ((VkBuffer)(uintptr_t)(0xB00F0000u + (k)))


/* Ledger 〜 every VkBuffer this harness mints, its create size, and its live state */

#define MAX_BUFS 64
static VkBuffer     g_minted[MAX_BUFS];
static VkDeviceSize g_mintSize[MAX_BUFS];
static bool         g_liveFlag[MAX_BUFS];
static uint32_t     g_mintCount;
static void        *g_shadow[MAX_BUFS];   // gpu_alloc host shadows, freed on reset
static uint32_t     g_shadowCount;
static uint32_t     g_wildReqs;           // vkGetBufferMemoryRequirements on a handle never minted
static uint32_t     g_wildBinds;          // vkBindBufferMemory on a handle never minted
static uint32_t     g_doubleDestroys;     // never reset 〜 whole-run invariant
static uint32_t     g_unknownDestroys;    // never reset 〜 whole-run invariant

/* Failure injection */
static bool g_failCreate;      // every vkCreateBuffer fails, out-param untouched
static bool g_failAllocOnce;   // next gpu_alloc returns the empty allocation

// Finds a minted handle's ledger index, or -1.
static int mint_index(VkBuffer h)
{
    for (uint32_t i = 0; i < g_mintCount; i++) if (g_minted[i] == h) return (int)i;
    return -1;
}

// Counts ledger entries still live.
static uint32_t live_buffers(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_mintCount; i++) if (g_liveFlag[i]) n++;
    return n;
}

// Clears the mint ledger, shadows, and per-phase wild counters; run invariants persist.
static void reset_ledger(void)
{
    for (uint32_t i = 0; i < g_shadowCount; i++) { free(g_shadow[i]); g_shadow[i] = NULL; }
    g_shadowCount = 0;
    memset(g_minted, 0, sizeof g_minted);
    memset(g_mintSize, 0, sizeof g_mintSize);
    memset(g_liveFlag, 0, sizeof g_liveFlag);
    g_mintCount = 0;
    g_wildReqs = g_wildBinds = 0;
}


/* Link seams 〜 globals scene_buffers.c references (real definitions live in vulkanMaster.c /
   gpu_alloc.c, which this executable deliberately does not link) */

VulkanContext ctx;
RendererState rendererState;
GpuAllocator  gpuAllocator;
GpuAllocator  stagingAllocator;


/* Link seams 〜 helper functions scene_buffers.c calls */

// in: requirements from the vk stub; out: fake device memory + host shadow as the mapping,
// or the empty allocation when injected (memory VK_NULL_HANDLE 〜 the arm the creators DO check).
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)alloc; (void)props;
    if (g_failAllocOnce) { g_failAllocOnce = false; return (GpuAllocation){0}; }
    void *shadow = NULL;
    if (g_shadowCount < MAX_BUFS) {
        shadow = calloc(1, (size_t)reqs.size);
        g_shadow[g_shadowCount++] = shadow;
    }
    return (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x51, .offset = 0, .size = reqs.size, .mapped = shadow };
}

bool slot_upload_create(SlotUpload* b, uint32_t capacity, uint32_t stride, uint32_t stagingCap, bool computeShared)
{ (void)capacity; (void)stride; (void)stagingCap; (void)computeShared; *b = (SlotUpload){0}; return true; }

void buffer_share_async_compute(VkBufferCreateInfo* bi, uint32_t fams[2]) { (void)bi; (void)fams; }

uint32_t ano_draw_partition_count(void) { return 1u; }

uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                              uint32_t transferFamily, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint32_t* indices, uint32_t indexCount)
{ (void)pool; (void)alloc; (void)device; (void)transferFamily; (void)transferQueue; (void)vertices; (void)vertexCount; (void)indices; (void)indexCount; return 0u; }

bool createTextureImageFromPixels(VulkanContext* c, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, const unsigned char* pixels, uint32_t width, uint32_t height, VkBuffer* outStagingBuffer)
{ (void)c; (void)cmd; (void)textureImage; (void)textureImageAlloc; (void)textureImageView; (void)pixels; (void)width; (void)height; (void)outStagingBuffer; return true; }

uint32_t bindless_register_texture(VulkanContext* c, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{ (void)c; (void)bta; (void)view; (void)sampler; return 0u; }

bool createShadowResources(VulkanContext* c, RendererState* state) { (void)c; (void)state; return true; }


/* Link seams 〜 the vk* entry points scene_buffers.c calls (loader not linked) */

// Failure arm is contract-faithful: error status, *pBuffer untouched (undefined contents on error).
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    (void)device; (void)pAllocator;
    if (g_failCreate) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (g_mintCount >= MAX_BUFS) { printf("FAIL: ledger overflow\n"); failures++; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    VkBuffer handle = (VkBuffer)(uintptr_t)(0x1000u + g_mintCount);
    g_minted[g_mintCount] = handle;
    g_mintSize[g_mintCount] = pCreateInfo->size;
    g_liveFlag[g_mintCount] = true;
    g_mintCount++;
    *pBuffer = handle;
    return VK_SUCCESS;
}

// Unminted handles are counted as wild consumptions; the write stays deterministic either way.
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


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ctx.device = (VkDevice)(uintptr_t)0x77; // every seam stub ignores it
    static RendererState rsGood, rsBad, rsCull;

    // control: the transform-lane good path mints, binds, and maps one buffer per frame in flight
    reset_ledger();
    TransformBuffer tb = {0};
    CHECK(createTransformBuffer(&ctx, &tb, 8, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "transform create succeeds on a good path");
    CHECK(g_mintCount == MAX_FRAMES_IN_FLIGHT, "transform good path mints one buffer per frame");
    CHECK(g_wildReqs == 0 && g_wildBinds == 0, "transform good path consumes only minted handles");
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) CHECK(tb.mapped[i] != NULL, "transform mapping published");
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) vkDestroyBuffer(ctx.device, tb.buffer[i], NULL); // caller epilogue
    CHECK(live_buffers() == 0, "transform control ledger balances");

    // control: the material good path does the same through RendererState
    reset_ledger();
    CHECK(createMaterialBuffer(&ctx, &rsGood, 8), "material create succeeds on a good path");
    CHECK(g_mintCount == MAX_FRAMES_IN_FLIGHT, "material good path mints one buffer per frame");
    CHECK(g_wildReqs == 0 && g_wildBinds == 0, "material good path consumes only minted handles");
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) vkDestroyBuffer(ctx.device, rsGood.materialBuffer.buffer[i], NULL);
    CHECK(live_buffers() == 0, "material control ledger balances");

    // control: the failure channel the creators DO check 〜 gpu_alloc's empty return 〜 reports
    // false and destroys the minted buffer (scene_buffers.c:182-184), so failure reporting works
    reset_ledger();
    TransformBuffer tbAlloc = {0};
    g_failAllocOnce = true;
    CHECK(!createTransformBuffer(&ctx, &tbAlloc, 8, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "transform create reports the checked gpu_alloc failure");
    CHECK(live_buffers() == 0, "checked failure arm destroys its minted buffer");

    // trigger: every vkCreateBuffer fails with out-params untouched 〜 unfixed, scene_buffers.c:175
    // only logs FATAL (which returns), :179 consumes the poisoned handle slot as if written, :186
    // binds it, and the creator returns true
    printf("trigger: transform-lane vkCreateBuffer failure 〜 expect FATAL-log fall-through at scene_buffers.c:174-176\n");
    fflush(stdout);
    reset_ledger();
    TransformBuffer tbFail = {0};
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) tbFail.buffer[i] = POISON(i);
    g_failCreate = true;
    bool ok = createTransformBuffer(&ctx, &tbFail, 8, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    g_failCreate = false;
    CHECK(!ok, "transform create reports vkCreateBuffer failure (scene_buffers.c:174 falls through)");
    CHECK(g_wildReqs == 0, "no unminted handle fed to vkGetBufferMemoryRequirements (scene_buffers.c:179)");
    CHECK(g_wildBinds == 0, "no unminted handle bound (scene_buffers.c:186)");

    // trigger: the material twin, scene_buffers.c:34-36 / :39 / :46
    printf("trigger: material-palette vkCreateBuffer failure 〜 expect FATAL-log fall-through at scene_buffers.c:34-36\n");
    fflush(stdout);
    reset_ledger();
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) rsBad.materialBuffer.buffer[i] = POISON(16 + i);
    g_failCreate = true;
    ok = createMaterialBuffer(&ctx, &rsBad, 8);
    g_failCreate = false;
    CHECK(!ok, "material create reports vkCreateBuffer failure (scene_buffers.c:34 falls through)");
    CHECK(g_wildReqs == 0, "no unminted handle fed to vkGetBufferMemoryRequirements (scene_buffers.c:39)");
    CHECK(g_wildBinds == 0, "no unminted handle bound (scene_buffers.c:46)");

    // trigger: createCullingBuffers drops all six vkCreateBuffer results with no guard at all
    // (:343/:359/:375/:392/:409/:424) 〜 six wild consumptions per frame in flight
    printf("trigger: culling-set vkCreateBuffer failure 〜 expect six unguarded creates per frame (scene_buffers.c:343-:424)\n");
    fflush(stdout);
    reset_ledger();
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rsCull.culling.meshDataBuffer[i]               = POISON(32 + 6 * i + 0);
        rsCull.culling.meshBoundsBuffer[i]             = POISON(32 + 6 * i + 1);
        rsCull.culling.drawCountBuffer[i]              = POISON(32 + 6 * i + 2);
        rsCull.culling.compactedEntityIndicesBuffer[i] = POISON(32 + 6 * i + 3);
        rsCull.culling.sortKeysBuffer[i]               = POISON(32 + 6 * i + 4);
        rsCull.culling.ubo.buffer[i]                   = POISON(32 + 6 * i + 5);
    }
    g_failCreate = true;
    ok = createCullingBuffers(&ctx, &rsCull, 4);
    g_failCreate = false;
    CHECK(!ok, "culling create reports vkCreateBuffer failure (scene_buffers.c:343-:424 never check)");
    CHECK(g_wildReqs == 0, "no unminted culling handle fed to vkGetBufferMemoryRequirements");
    CHECK(g_wildBinds == 0, "no unminted culling handle bound");

    // whole-run ledger invariants 〜 a fix must not destroy handles it never minted or destroy twice
    CHECK(g_doubleDestroys == 0, "no buffer destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unminted buffer handle destroyed");

    reset_ledger();

    if (failures) {
        printf("anotest_scenebufferguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_scenebufferguard: all passed\n");
    return 0;
}
