/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: async light-cull cross-queue sharing vs the camera UBO. createUniformBuffers
// (commands.c:82) creates each view's per-frame camera UBO EXCLUSIVE with no asyncLc arm, while
// updateClusterDescriptorSets binds that very buffer into the light-cull set at binding 0
// (descriptors.c:308/:330) and the light-cull dispatch runs on the dedicated compute queue family
// when async light-cull is on (hiz.c:93-:111 records vr->lightcullSet, submit.c:113 submits to
// ctx.computeQueue); lightcull.comp reads view/proj/near/far/clusterDims from it for every froxel
// (lightcull.comp:78-:118). An EXCLUSIVE resource accessed by a second queue family with no
// ownership transfer 〜 every barrier in the tree passes VK_QUEUE_FAMILY_IGNORED 〜 yields
// spec-undefined contents for that family's reads, and the module's own rule covers exactly this:
// buffer_share_async_compute (slot_upload.c:46, "a buffer the async light-cull touches across
// queue families") is applied to lightRuntimeBuffer (scene_buffers.c:208), both cluster buffers
// (:284) and the light SSBO via computeShared (:56) 〜 every sibling binding in the same set 〜
// only binding 0 misses the treatment (docs/BUGS.md, Render / Vulkan backend / Implementation,
// commands.c:82).
// Harness: compiles the REAL commands.c, descriptors.c, scene_buffers.c and slot_upload.c TUs 〜
// no GPU device, no loader. vkCreateBuffer keeps a sharing-mode ledger (mode + captured family
// list per minted handle); vkUpdateDescriptorSets records every buffer write aimed at a
// light-cull set. The sequence mirrors initVulkan: the scene-creator slice (vulkanMaster.c:585),
// createUniformBuffers (:637), updateClusterDescriptorSets (:665).
// CONTROL 1: asyncLc off 〜 the full sequence succeeds and all five bindings resolve to ledgered
// buffers, so the harness plumbing itself is proven and no sharing is demanded where none is due.
// CONTROL 2: asyncLc on 〜 bindings 1-4 are CONCURRENT holding both families (the harness detects
// the applied treatment; a reject-everything check cannot pass).
// TRIGGER: asyncLc on 〜 the binding-0 buffer must also be gfx+compute CONCURRENT; fails until
// every buffer the async light-cull consumes is compute-shared (a creation-side share arm or a
// shared replacement bound into the set both satisfy it).
// A crash is a valid failure signal. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h" // createUniformBuffers / updateClusterDescriptorSets
#include "vulkan_backend/scene_buffers.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/backend.h"               // extern VulkanContext ctx
#include "vulkan_backend/vulkanMaster.h"          // extern RendererState rendererState

// scene_buffers.c internal creators 〜 no public header; signatures mirror scene_buffers.c:53/:194/:270.
bool createLightBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxLights);
bool createLightRuntimeBuffer(VulkanContext* ctx, TransformBuffer* buf, uint32_t maxLights, VkMemoryPropertyFlags props);
bool createClusterBuffers(VulkanContext* ctx, RendererState* state);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// The two distinct queue families the phase runner installs (asyncHiz/asyncLc require distinct, vulkanMaster.c:379).
#define GFX_FAM 1u
#define CMP_FAM 5u


/* Sharing-mode ledger 〜 every vkCreateBuffer in the process, mode + captured family list per handle */

typedef struct { VkBuffer h; VkSharingMode mode; uint32_t famCount; uint32_t fams[8]; } BufRec;
static BufRec   g_buf[4096];
static uint32_t g_bufCount, g_bufMint;

// in: minted handle; out: its create record, or NULL.
static const BufRec* buf_find(VkBuffer h)
{
    for (uint32_t i = 0; i < g_bufCount; i++) if (g_buf[i].h == h) return &g_buf[i];
    return NULL;
}

// out: true iff the record is CONCURRENT and its family list holds both GFX_FAM and CMP_FAM.
static bool buf_shared_gfx_compute(const BufRec* r)
{
    if (r == NULL || r->mode != VK_SHARING_MODE_CONCURRENT) return false;
    bool g = false, c = false;
    for (uint32_t i = 0; i < r->famCount; i++) {
        if (r->fams[i] == GFX_FAM) g = true;
        if (r->fams[i] == CMP_FAM) c = true;
    }
    return g && c;
}


/* Descriptor-write ledger 〜 every buffer write, matched later against the light-cull set handles */

typedef struct { VkDescriptorSet set; uint32_t binding; VkBuffer buf; } WriteRec;
static WriteRec g_write[1024];
static uint32_t g_writeCount;

// Distinct fake handles per frame/view set so global-set writes cannot alias light-cull ones.
static VkDescriptorSet lc_set(uint32_t i, uint32_t v) { return (VkDescriptorSet)(uintptr_t)(0x1C000010u + i * 16u + v); }
static VkDescriptorSet gl_set(uint32_t i, uint32_t v) { return (VkDescriptorSet)(uintptr_t)(0x61000010u + i * 16u + v); }

// in: frame/view/binding; out: the create record of the buffer bound there, *found says the write exists.
static const BufRec* bound_buf(uint32_t i, uint32_t v, uint32_t binding, bool* found)
{
    for (uint32_t w = 0; w < g_writeCount; w++) {
        if (g_write[w].set == lc_set(i, v) && g_write[w].binding == binding) {
            *found = true;
            return buf_find(g_write[w].buf);
        }
    }
    *found = false;
    return NULL;
}


/* Link seams 〜 globals the real TUs reference (defined in vulkanMaster.c / gpu_alloc.c in production) */

VulkanContext ctx;
RendererState rendererState;
GpuAllocator  gpuAllocator;
GpuAllocator  stagingAllocator;

uint32_t ano_draw_partition_count(void) { return 1u; }

struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR* surface)
{ (void)device; (void)surface; struct QueueFamilyIndices q = {0}; return q; }

// Bump-arena stand-in: hands back a distinct memory handle and a real mapped pointer.
static _Alignas(64) uint8_t g_mapArena[1u << 16];
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)alloc; (void)props;
    GpuAllocation a = { .memory = (VkDeviceMemory)(uintptr_t)0x3E30001u, .offset = 0, .size = reqs.size, .mapped = g_mapArena };
    return a;
}

// commands.c / slot_upload.c TU link fodder (updateMeshTransforms, ensureEntityCapacity) 〜 never called here.
void translate(float mat[4][4], float x, float y, float z) { (void)mat; (void)x; (void)y; (void)z; }
void rotateMatrix(float mat[4][4], char axis, float angle) { (void)mat; (void)axis; (void)angle; }
void render_slots_set_capacity(RenderSlotTable* table, uint32_t newCapacity) { (void)table; (void)newCapacity; }

// scene_buffers.c TU link fodder (createFallbackResources / ano_vk_create_scene_resources) 〜 never called here.
uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                              uint32_t transferFamily, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint32_t* indices, uint32_t indexCount)
{ (void)pool; (void)alloc; (void)device; (void)transferFamily; (void)transferQueue; (void)vertices; (void)vertexCount; (void)indices; (void)indexCount; return 0u; }
bool createTextureImageFromPixels(VulkanContext* c, VkCommandBuffer cmd, VkImage* img, GpuAllocation* alloc, VkImageView* view, const unsigned char* pixels, uint32_t w, uint32_t h, VkBuffer* outStaging)
{ (void)c; (void)cmd; (void)img; (void)alloc; (void)view; (void)pixels; (void)w; (void)h; (void)outStaging; return true; }
uint32_t bindless_register_texture(VulkanContext* c, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{ (void)c; (void)bta; (void)view; (void)sampler; return 0u; }
bool createShadowResources(VulkanContext* c, RendererState* state) { (void)c; (void)state; return true; }


/* Link seams 〜 the vk* entry points the four TUs call (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    (void)device; (void)pAllocator;
    if (g_bufCount >= sizeof g_buf / sizeof g_buf[0]) { printf("FAIL: buffer ledger overflow\n"); exit(1); }
    BufRec* r = &g_buf[g_bufCount++];
    r->h = (VkBuffer)(uintptr_t)(0xB0F00000u + ++g_bufMint);
    r->mode = pCreateInfo->sharingMode;
    r->famCount = 0;
    if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT && pCreateInfo->pQueueFamilyIndices != NULL) {
        r->famCount = pCreateInfo->queueFamilyIndexCount > 8u ? 8u : pCreateInfo->queueFamilyIndexCount;
        for (uint32_t i = 0; i < r->famCount; i++) r->fams[i] = pCreateInfo->pQueueFamilyIndices[i];
    }
    *pBuffer = r->h;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    (void)device; (void)buffer;
    pMemoryRequirements->size = 1024;
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = ~0u;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)buffer; (void)memory; (void)memoryOffset; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)buffer; (void)pAllocator; }

// Records every buffer-typed write; image writes pass through untouched.
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{
    (void)device; (void)descriptorCopyCount; (void)pDescriptorCopies;
    for (uint32_t i = 0; i < descriptorWriteCount; i++) {
        const VkWriteDescriptorSet* w = &pDescriptorWrites[i];
        bool bufferType = w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                       || w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                       || w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                       || w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        if (!bufferType || w->pBufferInfo == NULL) continue;
        for (uint32_t d = 0; d < w->descriptorCount; d++) {
            if (g_writeCount >= sizeof g_write / sizeof g_write[0]) { printf("FAIL: write ledger overflow\n"); exit(1); }
            g_write[g_writeCount++] = (WriteRec){ w->dstSet, w->dstBinding + d, w->pBufferInfo[d].buffer };
        }
    }
}

// Handle mints / no-ops for the rest of the compiled TUs' surface. Never exercised by this test's path.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pDescriptorPool = (VkDescriptorPool)(uintptr_t)0xD00Cu; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
{
    (void)device;
    static uint32_t mint = 0;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)(0x5E70000u + ++mint);
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pCommandPool = (VkCommandPool)(uintptr_t)0xC001u; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
{
    (void)device;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) pCommandBuffers[i] = (VkCommandBuffer)(uintptr_t)(0xCB000010u + i);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{ (void)device; (void)commandPool; (void)commandBufferCount; (void)pCommandBuffers; }
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{ (void)commandBuffer; (void)pBeginInfo; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{ (void)commandBuffer; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
{ (void)commandBuffer; (void)srcBuffer; (void)dstBuffer; (void)regionCount; (void)pRegions; }
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{ (void)commandBuffer; (void)dstBuffer; (void)dstOffset; (void)size; (void)data; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pFence = (VkFence)(uintptr_t)0xFE7Cu; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)fence; (void)pAllocator; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pSemaphore = (VkSemaphore)(uintptr_t)0x5E4Au; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pQueryPool = (VkQueryPool)(uintptr_t)0x9D01u; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{ (void)queue; (void)submitCount; (void)pSubmits; (void)fence; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{ (void)device; (void)fenceCount; (void)pFences; (void)waitAll; (void)timeout; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{ (void)device; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{ (void)physicalDevice; memset(pProperties, 0, sizeof *pProperties); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
{
    (void)physicalDevice;
    if (pQueueFamilyProperties == NULL) { *pQueueFamilyPropertyCount = 1; return; }
    memset(pQueueFamilyProperties, 0, sizeof *pQueueFamilyProperties);
    pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{ (void)physicalDevice; memset(pMemoryProperties, 0, sizeof *pMemoryProperties); }


/* Phase runner 〜 the real init slice (vulkanMaster.c:585 creators, :637 UBOs, :665 cluster wiring) */

// in: asyncLc; out: true iff every real creator succeeded. Ledgers and globals reset per phase.
static bool run_phase(bool asyncLc)
{
    memset(&ctx, 0, sizeof ctx);
    memset(&rendererState, 0, sizeof rendererState);
    memset(&gpuAllocator, 0, sizeof gpuAllocator);
    memset(&stagingAllocator, 0, sizeof stagingAllocator);
    g_bufCount = g_bufMint = g_writeCount = 0;

    ctx.device = (VkDevice)(uintptr_t)0xD7;
    ctx.queueFamilyIndices.graphicsPresent = true;
    ctx.queueFamilyIndices.graphicsFamily  = GFX_FAM;
    ctx.queueFamilyIndices.computePresent  = true;
    ctx.queueFamilyIndices.computeFamily   = CMP_FAM;
    rendererState.asyncLc = asyncLc;

    // Set handles the wiring step targets (production mints them in createDescriptorSets).
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            rendererState.frames[i].views[v].lightcullSet = lc_set(i, v);
            rendererState.frames[i].views[v].globalSet    = gl_set(i, v);
        }
    }

    bool ok = true;
    ok = ok && createLightBuffer(&ctx, &rendererState, 64u);                                                        // scene_buffers.c:506 slice
    ok = ok && createLightRuntimeBuffer(&ctx, &rendererState.lightRuntimeBuffer, 64u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // :507
    ok = ok && createClusterBuffers(&ctx, &rendererState);                                                          // :511
    ok = ok && createUniformBuffers(&ctx, &rendererState);                                                          // vulkanMaster.c:637
    if (ok) updateClusterDescriptorSets(&ctx, &rendererState);                                                      // vulkanMaster.c:665
    return ok;
}

// Report + verdict for one binding across all frame/view sets. share demanded only when asyncLc.
// out: number of sets whose bound buffer breaks the demand (missing write / unledgered / unshared).
static uint32_t audit_binding(uint32_t binding, bool demandShare)
{
    uint32_t bad = 0;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            bool found = false;
            const BufRec* r = bound_buf(i, v, binding, &found);
            if (!found || r == NULL) { bad++; continue; }
            if (demandShare && !buf_shared_gfx_compute(r)) bad++;
        }
    }
    bool f0 = false;
    const BufRec* r0 = bound_buf(0, 0, binding, &f0);
    printf("  binding %u: %s, mode=%s fams=%u 〜 %u/%u sets breaking\n", binding,
           f0 && r0 ? "bound+ledgered" : "MISSING",
           r0 == NULL ? "?" : (r0->mode == VK_SHARING_MODE_CONCURRENT ? "CONCURRENT" : "EXCLUSIVE"),
           r0 ? r0->famCount : 0u, bad, (uint32_t)(MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT));
    return bad;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control 1: asyncLc off 〜 sequence succeeds, all five bindings bound to ledgered buffers,
    // nothing demands CONCURRENT, so the harness plumbing itself is proven non-vacuous
    printf("control: asyncLc=off\n");
    bool ok = run_phase(false);
    CHECK(ok, "control: creator + UBO sequence succeeds with asyncLc off");
    uint32_t missing = 0;
    for (uint32_t b = 0; b <= 4; b++) missing += audit_binding(b, false);
    CHECK(missing == 0, "control: all five light-cull bindings are bound to ledgered buffers");

    // trigger: asyncLc on 〜 the module's own rule (slot_upload.c:46) demands gfx+compute
    // CONCURRENT for every buffer the async light-cull touches across queue families
    printf("trigger: asyncLc=on 〜 every light-cull-consumed buffer must be gfx+compute CONCURRENT\n");
    ok = run_phase(true);
    CHECK(ok, "trigger: creator + UBO sequence succeeds with asyncLc on");

    // control 2: the treated siblings 〜 pose (1), lights (2), cluster count/index (3/4) 〜 prove
    // the harness detects the applied treatment, so a reject-everything check cannot pass
    CHECK(audit_binding(1, true) == 0, "control: binding 1 (lightRuntimeBuffer, scene_buffers.c:208) is gfx+compute CONCURRENT");
    CHECK(audit_binding(2, true) == 0, "control: binding 2 (light SSBO, computeShared scene_buffers.c:56) is gfx+compute CONCURRENT");
    CHECK(audit_binding(3, true) == 0, "control: binding 3 (cluster count, scene_buffers.c:284) is gfx+compute CONCURRENT");
    CHECK(audit_binding(4, true) == 0, "control: binding 4 (cluster index, scene_buffers.c:284) is gfx+compute CONCURRENT");

    // the bug: binding 0 〜 the camera UBO createUniformBuffers mints EXCLUSIVE (commands.c:82)
    // rides into the compute-family dispatch with no ownership transfer anywhere in the tree
    CHECK(audit_binding(0, true) == 0, "trigger: binding 0 (camera UBO) is gfx+compute CONCURRENT when asyncLc is on (commands.c:82 misses the buffer_share_async_compute arm)");

    if (failures) {
        printf("anotest_uboshareguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uboshareguard: all passed\n");
    return 0;
}
