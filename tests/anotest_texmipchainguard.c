/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the discarded mip-generation status inside createTextureImage. generateMipmaps'
// only failure arm 〜 the driver reporting no VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
// (texture.c:239-243) 〜 returns false before recording a single barrier or blit, the call site
// at texture.c:437 drops the bool, and the fallback whole-chain TRANSFER_DST->SHADER_READ
// transition beneath it is commented out (:439-443), so the upload returns true with the image
// parked in TRANSFER_DST_OPTIMAL and mips 1..N-1 never written while the caller binds the
// full-chain view as SHADER_READ_ONLY_OPTIMAL (bindless_register_texture pins that layout at
// :36) (docs/BUGS.md, Render / Vulkan backend / Implementation). Harness: compiles the REAL
// texture.c TU against link-seam vk/helper stubs that shadow the tracked image's per-mip layout
// and written state from every recorded barrier, copy and blit 〜 no GPU device, no loader.
// Real stbi decodes a real 8x8 TGA this test writes in its CWD. Failure injection is
// contract-faithful: format properties are driver-reported data and the stub simply reports the
// feature absent (spec-optional for the 16-bit formats the flag16 TODO at :397 will bring;
// non-conformant-driver territory for today's RGBA8 pair). Controls prove the filterable file
// path publishes a fully written SHADER_READ chain and the sibling single-mip pixel path does
// the same through its explicit checked transition (:379), so a reject-everything fix cannot
// pass. Fix-agnostic trigger invariant: a true return implies every created mip is written and
// in SHADER_READ_ONLY_OPTIMAL 〜 failing the load, clamping the chain to one mip, or
// software-generating mips all satisfy it; merely uncommenting :439-443 does not, because the
// mip tail would still be sampled unwritten. Exit 0 == pass.

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


/* Shadow 〜 per-mip layout and written state of the most recently created image */

#define MAX_MIPS 16
static VkImage       g_trackedImage;
static uint32_t      g_imgMips;              // from vkCreateImage's pCreateInfo->mipLevels
static VkImageLayout g_mipLayout[MAX_MIPS];
static bool          g_mipWritten[MAX_MIPS];
static uint32_t      g_layoutFaults; // barrier/copy/blit against a mismatched current layout 〜 whole-run invariant
static uint32_t      g_undefReads;   // blit sourcing a never-written mip 〜 whole-run invariant
static uint32_t      g_imageMintCount;

/* Failure injection */
static bool g_noFilterLinear; // driver reports no SAMPLED_IMAGE_FILTER_LINEAR 〜 generateMipmaps' :242 refusal arm

// True when every created mip is written and parked in SHADER_READ_ONLY_OPTIMAL.
static bool chain_sampleable(void)
{
    if (g_imgMips == 0) return false;
    for (uint32_t m = 0; m < g_imgMips; m++)
        if (!g_mipWritten[m] || g_mipLayout[m] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
    return true;
}

// Prints the shadowed chain state, one line per mip.
static void print_chain(void)
{
    for (uint32_t m = 0; m < g_imgMips; m++)
        printf("  mip %" PRIu32 ": layout %d, %s\n", m, (int)g_mipLayout[m], g_mipWritten[m] ? "written" : "NEVER WRITTEN");
}


/* Link seams 〜 texture.c externs (the real definitions live in vulkanMaster.c / instance/,
   which this executable deliberately does not link) */

GpuAllocator textureAllocator;
GpuAllocator stagingAllocator;

#define MAX_SHADOWS 8
static void    *g_mapShadow[MAX_SHADOWS];
static uint32_t g_shadowCount;

// in: size; out: buffer handle + host-backed allocation.
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
    (void)ctx; (void)allocator; (void)usage; (void)properties;
    if (g_shadowCount >= MAX_SHADOWS) { printf("FAIL: shadow overflow\n"); failures++; return false; }
    void *shadow = malloc((size_t)size);
    g_mapShadow[g_shadowCount++] = shadow;
    allocation->memory = (VkDeviceMemory)(uintptr_t)0x51;
    allocation->offset = 0;
    allocation->size   = size;
    allocation->mapped = shadow;
    *buffer = (VkBuffer)(uintptr_t)(0x1000u + g_shadowCount);
    return shadow != NULL;
}

// Satisfies every request 〜 allocation failure is not this test's arm.
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
{
    (void)device; (void)pAllocator;
    *pImage = (VkImage)(uintptr_t)(0x600u + g_imageMintCount++);
    g_trackedImage = *pImage;
    g_imgMips = pCreateInfo->mipLevels > MAX_MIPS ? MAX_MIPS : pCreateInfo->mipLevels;
    for (uint32_t m = 0; m < MAX_MIPS; m++) { g_mipLayout[m] = VK_IMAGE_LAYOUT_UNDEFINED; g_mipWritten[m] = false; }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{ (void)device; (void)image; pMemoryRequirements->size = 1u << 20; pMemoryRequirements->alignment = 256; pMemoryRequirements->memoryTypeBits = 1; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)image; (void)memory; (void)memoryOffset; return VK_SUCCESS; }

// Applies each image barrier to the shadow: oldLayout must match (or be UNDEFINED, which
// additionally discards content per spec), newLayout becomes current for the mip range.
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    for (uint32_t b = 0; b < imageMemoryBarrierCount; b++) {
        const VkImageMemoryBarrier* bar = &pImageMemoryBarriers[b];
        if (bar->image != g_trackedImage) continue;
        uint32_t base = bar->subresourceRange.baseMipLevel;
        uint32_t cnt  = bar->subresourceRange.levelCount;
        if (cnt == VK_REMAINING_MIP_LEVELS) cnt = base < g_imgMips ? g_imgMips - base : 0;
        for (uint32_t m = base; m < base + cnt && m < g_imgMips; m++) {
            if (bar->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) g_mipWritten[m] = false;
            else if (bar->oldLayout != g_mipLayout[m]) g_layoutFaults++;
            g_mipLayout[m] = bar->newLayout;
        }
    }
}

// Marks each copied dst mip written; the declared layout must match the shadow.
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    (void)commandBuffer; (void)srcBuffer;
    if (dstImage != g_trackedImage) return;
    for (uint32_t i = 0; i < regionCount; i++) {
        uint32_t m = pRegions[i].imageSubresource.mipLevel;
        if (m >= g_imgMips) continue;
        if (dstImageLayout != g_mipLayout[m]) g_layoutFaults++;
        g_mipWritten[m] = true;
    }
}

// Propagates written state src->dst; both declared layouts must match the shadow.
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{
    (void)commandBuffer; (void)filter;
    for (uint32_t i = 0; i < regionCount; i++) {
        uint32_t sm = pRegions[i].srcSubresource.mipLevel;
        uint32_t dm = pRegions[i].dstSubresource.mipLevel;
        if (srcImage == g_trackedImage && sm < g_imgMips) {
            if (srcImageLayout != g_mipLayout[sm]) g_layoutFaults++;
            if (!g_mipWritten[sm]) g_undefReads++;
        }
        if (dstImage == g_trackedImage && dm < g_imgMips) {
            if (dstImageLayout != g_mipLayout[dm]) g_layoutFaults++;
            g_mipWritten[dm] = true;
        }
    }
}

// Driver-reported format properties 〜 the injection point, spec-legal either way.
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{
    (void)physicalDevice; (void)format;
    memset(pFormatProperties, 0, sizeof *pFormatProperties);
    pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if (!g_noFilterLinear) pFormatProperties->optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)buffer; (void)pAllocator; }

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

    char sq[] = "anotest_texmipchain_8x8.tga";
    CHECK(write_tga(sq, 8, 8), "square TGA written");

    // control: filterable format 〜 the mip loop writes every level and parks the chain in SHADER_READ
    staging = VK_NULL_HANDLE;
    bool ok = createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging);
    CHECK(ok, "filterable file upload succeeds");
    CHECK(g_imgMips == 4, "8x8 file creates a 4-mip chain");
    CHECK(chain_sampleable(), "filterable upload publishes a fully written SHADER_READ chain");
    if (ok && !chain_sampleable()) print_chain();
    vkDestroyBuffer(ctx.device, staging, NULL); // caller epilogue

    // control: the sibling pixel path proves the intended no-mipgen shape 〜 one mip, explicit checked transition (:379)
    static unsigned char px[4 * 4 * 4];
    ok = createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 4, NULL);
    CHECK(ok, "sibling single-mip upload succeeds");
    CHECK(g_imgMips == 1 && chain_sampleable(), "sibling publishes its one mip written and in SHADER_READ");

    // trigger: the driver reports no FILTER_LINEAR 〜 generateMipmaps refuses at :242 before recording
    // anything, :437 drops the bool, the :439-443 fallback transition is commented out
    printf("trigger: unfilterable-format upload on the file path\n");
    g_noFilterLinear = true;
    staging = VK_NULL_HANDLE;
    ok = createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, false, &staging);
    g_noFilterLinear = false;
    vkDestroyBuffer(ctx.device, staging, NULL); // caller-faithful discharge of anything handed back
    if (ok && !chain_sampleable()) {
        printf("  (createTextureImage returned true with the chain below 〜 texture.c:437 discarded generateMipmaps' refusal)\n");
        print_chain();
    }
    CHECK(!ok || chain_sampleable(), "a true return implies a sampleable image 〜 every created mip written and in SHADER_READ_ONLY_OPTIMAL");

    // whole-run recording invariants 〜 a fix must not blit unwritten mips or record mismatched layouts
    CHECK(g_layoutFaults == 0, "no barrier/copy/blit recorded against a mismatched current layout");
    CHECK(g_undefReads == 0, "no blit sourced a never-written mip");

    for (uint32_t i = 0; i < g_shadowCount; i++) free(g_mapShadow[i]);
    remove(sq);

    if (failures) {
        printf("anotest_texmipchainguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_texmipchainguard: all passed\n");
    return 0;
}
