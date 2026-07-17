/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the staging-buffer discharge on createTextureImage's failure paths. The staging
// buffer is acquired at texture.c:415 and discharged only in the success epilogue (:445 hands
// it to outStagingBuffer or destroys it), so the failure returns at :426 (image creation) and
// :432 (layout transition) orphan the live VkBuffer (docs/BUGS.md, Render / Vulkan backend /
// Implementation). *outStagingBuffer is written only at :445, so the glTF caller's calloc'd
// slot stays VK_NULL_HANDLE while stagingCount++ has already consumed it (ano_GltfParser.c:274)
// and the destroy loop at :296 no-ops on the hole 〜 one buffer object bound into the shared
// staging arena orphans per failed texture load. The sibling createTextureImageFromPixels
// orphans identically on its :368/:374/:382 arms (reached with NULL out from scene_buffers.c:479).
// Harness: compiles the REAL texture.c TU and satisfies its link seams with stubs that keep a
// mint/destroy ledger of every staging handle createDataBuffer issues 〜 no GPU device, no
// loader. Real stbi_load decodes a real TGA this test writes beside its CWD. Failure injection
// is contract-faithful: vkCreateImage refusing with OOM (:426) and gpu_alloc returning the empty
// allocation (:368), both real-world arms. Controls prove both success shapes balance the ledger
// (inline destroy on NULL out, hand-out plus caller destroy otherwise), so a reject-everything
// fix cannot pass. Fix-agnostic trigger invariant: after a failed call plus a caller-faithful
// discharge of any handle that came back, zero staging buffers remain live 〜 an internal
// destroy-before-return fix and a hand-out-on-failure fix both satisfy it. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/texture/texture.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledger 〜 every staging VkBuffer minted by createDataBuffer, and its live state */

#define MAX_BUFS 16
static VkBuffer g_minted[MAX_BUFS];
static bool     g_liveFlag[MAX_BUFS];
static uint32_t g_mintCount;
static void    *g_mapShadow[MAX_BUFS];
static uint32_t g_doubleDestroys;  // never reset 〜 whole-run invariant
static uint32_t g_unknownDestroys; // never reset 〜 whole-run invariant

/* Failure injection */
static bool g_failCreateImage; // vkCreateImage refuses with OOM 〜 texture.c:426 arm
static bool g_failGpuAlloc;    // gpu_alloc returns empty 〜 createImageShared :217 -> :368 arm

// Counts ledger entries still live.
static uint32_t live_buffers(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_mintCount; i++) if (g_liveFlag[i]) n++;
    return n;
}

// Clears the mint ledger and releases the staging shadows; run invariants persist.
static void reset_ledger(void)
{
    for (uint32_t i = 0; i < g_mintCount; i++) { free(g_mapShadow[i]); g_mapShadow[i] = NULL; }
    memset(g_minted, 0, sizeof g_minted);
    memset(g_liveFlag, 0, sizeof g_liveFlag);
    g_mintCount = 0;
}


/* Link seams 〜 texture.c externs (the real definitions live in vulkanMaster.c / instance/,
   which this executable deliberately does not link) */

GpuAllocator textureAllocator;
GpuAllocator stagingAllocator;

// in: size; out: buffer handle + host-backed allocation. Mints a unique ledgered handle.
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
    (void)ctx; (void)allocator; (void)usage; (void)properties;
    if (g_mintCount >= MAX_BUFS) { printf("FAIL: ledger overflow\n"); failures++; return false; }
    void *shadow = malloc((size_t)size);
    VkBuffer handle = (VkBuffer)(uintptr_t)(0x1000u + g_mintCount);
    g_minted[g_mintCount] = handle;
    g_liveFlag[g_mintCount] = true;
    g_mapShadow[g_mintCount] = shadow;
    g_mintCount++;
    allocation->memory = (VkDeviceMemory)(uintptr_t)0x51;
    allocation->offset = 0;
    allocation->size   = size;
    allocation->mapped = shadow;
    *buffer = handle;
    return shadow != NULL;
}

// Satisfies the request unless the exhaustion arm is injected.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)alloc; (void)props;
    if (g_failGpuAlloc) return (GpuAllocation){0};
    return (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x53, .offset = 0, .size = reqs.size, .mapped = NULL };
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx) { (void)ctx; return (VkCommandBuffer)(uintptr_t)0x54; }
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer) { (void)ctx; (void)commandBuffer; }
bool hasStencilComponent(VkFormat format) { return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT; }
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{ (void)device; (void)image; (void)format; (void)aspectFlags; (void)mipLevels; return (VkImageView)(uintptr_t)0x55; }


/* Link seams 〜 the vk* entry points texture.c calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (g_failCreateImage) return VK_ERROR_OUT_OF_DEVICE_MEMORY; // out-param undefined on error per spec
    *pImage = (VkImage)(uintptr_t)0x60;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{ (void)device; (void)image; pMemoryRequirements->size = 1u << 20; pMemoryRequirements->alignment = 256; pMemoryRequirements->memoryTypeBits = 1; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)image; (void)memory; (void)memoryOffset; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{ (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers; (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{ (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{ (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; (void)filter; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{ (void)physicalDevice; (void)format; memset(pFormatProperties, 0, sizeof *pFormatProperties); pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer == VK_NULL_HANDLE) return; // spec no-op, the caller's calloc'd-hole case
    for (uint32_t i = 0; i < g_mintCount; i++) {
        if (g_minted[i] == buffer) {
            if (!g_liveFlag[i]) g_doubleDestroys++;
            g_liveFlag[i] = false;
            return;
        }
    }
    g_unknownDestroys++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)image; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{ (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites; (void)descriptorCopyCount; (void)pDescriptorCopies; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pSampler = (VkSampler)(uintptr_t)0x61; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{ (void)physicalDevice; memset(pProperties, 0, sizeof *pProperties); }


/* Fixture */

// Writes a w x h uncompressed 32-bit true-color TGA (top-left origin) for the real stbi_load.
static bool write_tga(const char *path, int w, int h)
{
    unsigned char hdr[18] = {0};
    hdr[2]  = 2; // uncompressed true-color
    hdr[12] = (unsigned char)(w & 0xFF); hdr[13] = (unsigned char)((w >> 8) & 0xFF);
    hdr[14] = (unsigned char)(h & 0xFF); hdr[15] = (unsigned char)((h >> 8) & 0xFF);
    hdr[16] = 32;   // bits per pixel
    hdr[17] = 0x28; // top-left origin, 8 alpha bits
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(hdr, 1, sizeof hdr, f);
    for (int i = 0; i < w * h; i++) {
        unsigned char px[4] = { (unsigned char)i, 0x40, 0x80, 0xFF };
        fwrite(px, 1, sizeof px, f);
    }
    fclose(f);
    return true;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static VulkanContext ctx; // zeroed; every seam stub ignores its handles
    VkCommandBuffer cmd = (VkCommandBuffer)(uintptr_t)0x70;
    VkImage img; GpuAllocation alloc; VkImageView view; VkBuffer staging;

    char sq[] = "anotest_texstaging_4x4.tga";
    CHECK(write_tga(sq, 4, 4), "square TGA written");

    // control: the sibling pixel path with NULL out destroys its staging buffer inline (:385)
    static unsigned char px[4 * 4 * 4];
    reset_ledger();
    CHECK(createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 4, NULL), "sibling upload (NULL out) succeeds");
    CHECK(g_mintCount == 1, "sibling path mints exactly one staging buffer");
    CHECK(live_buffers() == 0, "success with NULL out destroys the staging buffer inline");

    // control: the file path hands the handle out (:445) and the caller epilogue balances the
    // ledger (ano_GltfParser.c:297), so a reject-everything or destroy-everything fix cannot pass
    reset_ledger();
    staging = VK_NULL_HANDLE;
    CHECK(createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging), "square file upload succeeds");
    CHECK(g_mintCount == 1 && staging == g_minted[0], "staging handle handed to the caller on success");
    CHECK(live_buffers() == 1, "handed-out staging buffer stays live until the caller destroys it");
    vkDestroyBuffer(ctx.device, staging, NULL); // caller epilogue
    CHECK(live_buffers() == 0, "caller destroy balances the ledger on success");

    // trigger: image creation fails (device OOM) 〜 the :426 return skips the :445 discharge and
    // never writes *outStagingBuffer, so the caller's calloc'd slot no-ops its destroy loop
    printf("trigger: image-creation failure on the file path\n");
    reset_ledger();
    g_failCreateImage = true;
    staging = VK_NULL_HANDLE; // the caller's calloc'd slot (ano_GltfParser.c:248)
    bool ok = createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging);
    g_failCreateImage = false;
    CHECK(!ok, "image-creation failure is reported");
    CHECK(g_mintCount == 1, "failed upload minted its staging buffer before the refusal");
    if (staging != VK_NULL_HANDLE) vkDestroyBuffer(ctx.device, staging, NULL); // caller-faithful discharge of anything handed back
    if (live_buffers() != 0)
        printf("  (staging buffer minted at texture.c:415 still live after the :426 failure return; *outStagingBuffer %s written)\n",
               staging == VK_NULL_HANDLE ? "never" : "was");
    CHECK(live_buffers() == 0, "failure return discharges or hands out the staging buffer");

    // trigger: the sibling arm under arena exhaustion 〜 gpu_alloc returns empty, createImage
    // fails, and the :368 return orphans the staging buffer the same way
    printf("trigger: arena exhaustion on the pixel path\n");
    reset_ledger();
    g_failGpuAlloc = true;
    ok = createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 4, NULL);
    g_failGpuAlloc = false;
    CHECK(!ok, "allocation failure is reported");
    if (live_buffers() != 0)
        printf("  (%" PRIu32 " staging buffer(s) still live after the sibling :368 failure return)\n", live_buffers());
    CHECK(live_buffers() == 0, "sibling failure return discharges the staging buffer");

    // whole-run ledger invariants 〜 a fix must not double-destroy or invent handles
    CHECK(g_doubleDestroys == 0, "no staging buffer destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unknown buffer handle destroyed");

    reset_ledger();
    remove(sq);

    if (failures) {
        printf("anotest_texstagingguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_texstagingguard: all passed\n");
    return 0;
}
