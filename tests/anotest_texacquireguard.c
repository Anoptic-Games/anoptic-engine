/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the staging-buffer ACQUISITION failure in createTextureImage. The createDataBuffer
// call at texture.c:415 has its bool status discarded, and :417-418 consume the out-params
// regardless: `void* data = stagingAlloc.mapped; memcpy(data, texture.pixels, imageSize)`
// (docs/BUGS.md, Render / Vulkan backend / Implementation, texture.c:415). The real callee's
// arena-exhaustion arm (commands.c:59-64) returns false with *allocation zeroed 〜 mapped NULL,
// a deterministic NULL write of texWidth*texHeight*4 bytes 〜 and its vkCreateBuffer arm
// (commands.c:50-54) returns false with *allocation never written at all, so the uninitialized
// stack stagingAlloc at :414 feeds the memcpy a wild pointer. The sibling
// createTextureImageFromPixels has the identical shape at :360-363. Distinct from the tallied
// :426 entry: that one orphans the buffer a SUCCESSFUL acquire minted; this one is the FAILED
// acquire never being noticed, the exact staging-arena pressure a loading spree produces.
// Harness: compiles the REAL texture.c TU and satisfies its link seams with stubs 〜 no GPU
// device, no loader. Real stbi_load decodes a real TGA this test writes beside its CWD. The
// createDataBuffer stub's failure arm is contract-faithful to the real exhaustion arm:
// *buffer = VK_NULL_HANDLE, *allocation = {0}, return false. Controls prove both upload paths
// deliver the decoded pixels into the staging shadow on a good acquire, so a reject-everything
// fix cannot pass (printed + flushed before the crashing trigger). Unfixed, the trigger dies in
// the :418 memcpy through NULL 〜 a crash IS the failure signal. Fix-agnostic invariant: a failed
// acquire returns false with no write through mapped and no unknown/double buffer destroy.
// Exit 0 == pass.

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
static bool g_failAcquire; // createDataBuffer's arena-exhaustion arm 〜 commands.c:59-64

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

// in: size; out: buffer handle + host-backed allocation, or the real callee's exhaustion-arm
// contract when injected (*buffer VK_NULL_HANDLE, *allocation zeroed, false 〜 commands.c:59-64).
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
    (void)ctx; (void)allocator; (void)usage; (void)properties;
    if (g_failAcquire) {
        *buffer = VK_NULL_HANDLE;
        *allocation = (GpuAllocation){0}; // mapped == NULL, faithful to gpu_alloc's empty return
        return false;
    }
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

// Satisfies the image-side requests; the staging seam under test is createDataBuffer.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)alloc; (void)props;
    return (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x53, .offset = 0, .size = reqs.size, .mapped = NULL };
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx) { (void)ctx; return (VkCommandBuffer)(uintptr_t)0x54; }
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer) { (void)ctx; (void)commandBuffer; }
bool hasStencilComponent(VkFormat format) { return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT; }
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{ (void)device; (void)image; (void)format; (void)aspectFlags; (void)mipLevels; return (VkImageView)(uintptr_t)0x55; }


/* Link seams 〜 the vk* entry points texture.c calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pImage = (VkImage)(uintptr_t)0x60; return VK_SUCCESS; }

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
    if (buffer == VK_NULL_HANDLE) return; // spec no-op
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
        unsigned char px[4] = { (unsigned char)i, 0x40, 0x80, 0xFF }; // BGRA in-file
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

    char sq[] = "anotest_texacquire_4x4.tga";
    CHECK(write_tga(sq, 4, 4), "square TGA written");

    // control: a good acquire on the file path lands the decoded pixels in the staging shadow
    // (TGA stores BGRA; stbi returns RGBA, so pixel 0 is {0x80, 0x40, 0x00, 0xFF})
    reset_ledger();
    staging = VK_NULL_HANDLE;
    CHECK(createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging), "file upload succeeds on a good acquire");
    CHECK(g_mintCount == 1, "file path mints exactly one staging buffer");
    if (g_mintCount == 1 && g_mapShadow[0]) {
        const unsigned char *sh = (const unsigned char *)g_mapShadow[0];
        CHECK(sh[0] == 0x80 && sh[1] == 0x40 && sh[2] == 0x00 && sh[3] == 0xFF, "decoded pixels reached the staging mapping");
    }
    vkDestroyBuffer(ctx.device, staging, NULL); // caller epilogue
    CHECK(live_buffers() == 0, "control ledger balances");

    // control: the sibling pixel path delivers its bytes the same way
    static unsigned char px[4 * 4 * 4];
    for (uint32_t i = 0; i < sizeof px; i++) px[i] = (unsigned char)(0xA0 ^ i);
    reset_ledger();
    CHECK(createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 4, NULL), "sibling upload succeeds on a good acquire");
    CHECK(g_mintCount == 1, "sibling path mints exactly one staging buffer");
    if (g_mintCount == 1 && g_mapShadow[0])
        CHECK(memcmp(g_mapShadow[0], px, sizeof px) == 0, "caller pixels reached the staging mapping");
    CHECK(live_buffers() == 0, "sibling control ledger balances");

    // trigger: the acquire fails with the real callee's exhaustion-arm contract 〜 unfixed,
    // texture.c:415 discards the false and :417-418 memcpy the decoded image through
    // stagingAlloc.mapped == NULL (the vkCreateBuffer arm would make it a wild stack pointer
    // instead); the crash below IS the bug firing
    printf("trigger: staging acquisition failure on the file path 〜 expect a NULL write in the texture.c:418 memcpy\n");
    fflush(stdout);
    reset_ledger();
    g_failAcquire = true;
    staging = VK_NULL_HANDLE;
    bool ok = createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging);
    g_failAcquire = false;
    CHECK(!ok, "failed staging acquire is reported by the file path");
    CHECK(g_mintCount == 0, "no staging buffer exists after a failed acquire");

    // trigger: the sibling pixel path, identical shape at texture.c:360-363
    printf("trigger: staging acquisition failure on the pixel path 〜 expect a NULL write in the texture.c:363 memcpy\n");
    fflush(stdout);
    reset_ledger();
    g_failAcquire = true;
    ok = createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 4, NULL);
    g_failAcquire = false;
    CHECK(!ok, "failed staging acquire is reported by the pixel path");
    CHECK(g_mintCount == 0, "no staging buffer exists after a failed sibling acquire");

    // whole-run ledger invariants 〜 a fix must not double-destroy or invent handles
    CHECK(g_doubleDestroys == 0, "no staging buffer destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unknown buffer handle destroyed");

    reset_ledger();
    remove(sq);

    if (failures) {
        printf("anotest_texacquireguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_texacquireguard: all passed\n");
    return 0;
}
