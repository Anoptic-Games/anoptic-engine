/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the mip-0 upload extents of the file texture path. createTextureImage passes
// texture.texWidth as BOTH dimensions of the buffer->image copy (texture.c:435, docs/BUGS.md,
// Render / Vulkan backend / Implementation), while its sibling createTextureImageFromPixels at
// :377 passes (width, height) correctly. Every non-square file uploads wrong: landscape (w > h)
// submits a copy region w rows tall against an h-row image and reads w*(w-h)*4 bytes past the
// w*h*4 staging buffer 〜 a VUID breach on both the image bound and the buffer bound 〜 and
// portrait (w < h) uploads only w rows, leaving the rest of mip 0 undefined for generateMipmaps
// to smear down the chain. Reached from every glTF texture upload (ano_GltfParser.c:270).
// Harness: compiles the REAL texture.c TU into this executable and satisfies its link seams
// (instanceInit helpers, gpu_alloc, the vk* entry points) with stubs that capture the
// VkImageCreateInfo extent, the VkBufferImageCopy region, and the staging request 〜 no GPU
// device, no loader. Real stbi_load decodes real TGA files this test writes beside its CWD.
// Controls prove the sibling pixel path AND a square file ride the same seams with correct
// extents, so a fix that rejects everything or stubs the copy out cannot pass. Differential and
// fix-agnostic: any correct fix hands (texWidth, texHeight) to the copy and every CHECK passes.
// Exit 0 == pass.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/texture/texture.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Captures */

static VkExtent3D        g_imgExtent;    // last vkCreateImage extent
static VkBufferImageCopy g_copy;         // last buffer->image copy region
static uint32_t          g_copies;       // copy calls seen
static VkDeviceSize      g_stagingBytes; // last staging request
static void             *g_stagingMapped;

// Clears every capture and releases the previous staging shadow.
static void reset_captures(void)
{
    memset(&g_imgExtent, 0, sizeof g_imgExtent);
    memset(&g_copy, 0, sizeof g_copy);
    g_copies = 0;
    g_stagingBytes = 0;
    free(g_stagingMapped);
    g_stagingMapped = NULL;
}


/* Link seams 〜 texture.c externs (the real definitions live in vulkanMaster.c / instance/,
   which this executable deliberately does not link) */

GpuAllocator textureAllocator;
GpuAllocator stagingAllocator;

// in: size; out: buffer handle + host-backed allocation. Records the request.
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
    (void)ctx; (void)allocator; (void)usage; (void)properties;
    g_stagingBytes = size;
    g_stagingMapped = malloc((size_t)size);
    allocation->memory = (VkDeviceMemory)(uintptr_t)0x51;
    allocation->offset = 0;
    allocation->size   = size;
    allocation->mapped = g_stagingMapped;
    *buffer = (VkBuffer)(uintptr_t)0x52;
    return g_stagingMapped != NULL;
}

// Always satisfies the request so createImageShared proceeds.
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
{ (void)device; (void)pAllocator; g_imgExtent = pCreateInfo->extent; *pImage = (VkImage)(uintptr_t)0x60; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{ (void)device; (void)image; pMemoryRequirements->size = 1u << 20; pMemoryRequirements->alignment = 256; pMemoryRequirements->memoryTypeBits = 1; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)image; (void)memory; (void)memoryOffset; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{ (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers; (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{ (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; if (regionCount) { g_copy = pRegions[0]; g_copies++; } }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{ (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; (void)filter; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{ (void)physicalDevice; (void)format; memset(pFormatProperties, 0, sizeof *pFormatProperties); pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT; }

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

    char sq[]   = "anotest_texupload_4x4.tga";
    char land[] = "anotest_texupload_8x2.tga";
    char port[] = "anotest_texupload_2x8.tga";

    // control: the sibling pixel path carries (width, height) into the copy region
    static unsigned char px[4 * 2 * 4];
    reset_captures();
    CHECK(createTextureImageFromPixels(&ctx, cmd, &img, &alloc, &view, px, 4, 2, &staging), "sibling upload (4x2 pixels) succeeds");
    CHECK(g_copies == 1, "sibling path records exactly one copy");
    CHECK(g_copy.imageExtent.width == 4 && g_copy.imageExtent.height == 2, "sibling copy region is width x height");

    // control: a square file rides the whole file path (real stbi decode) with correct extents,
    // so a fix that rejects files or drops the copy wholesale cannot pass
    CHECK(write_tga(sq, 4, 4), "square TGA written");
    reset_captures();
    CHECK(createTextureImage(&ctx, cmd, &img, &alloc, &view, sq, false, true, &staging), "square file upload succeeds");
    CHECK(g_imgExtent.width == 4 && g_imgExtent.height == 4, "square image created 4x4");
    CHECK(g_copies == 1 && g_copy.imageExtent.width == 4 && g_copy.imageExtent.height == 4, "square copy region is 4x4");

    // trigger: landscape 8x2 〜 the copy must span texWidth x texHeight; the bug hands
    // texWidth twice, an 8-row region over a 2-row image fed from a 64-byte staging buffer
    printf("trigger: landscape 8x2 file upload\n");
    CHECK(write_tga(land, 8, 2), "landscape TGA written");
    reset_captures();
    CHECK(createTextureImage(&ctx, cmd, &img, &alloc, &view, land, false, true, &staging), "landscape file upload succeeds");
    CHECK(g_imgExtent.width == 8 && g_imgExtent.height == 2, "landscape image created 8x2");
    if (g_copy.imageExtent.height != 2)
        printf("  (copy region %ux%u against the 8x2 image; %" PRIu64 " bytes consumed from a %" PRIu64 "-byte staging buffer)\n",
               g_copy.imageExtent.width, g_copy.imageExtent.height,
               (uint64_t)g_copy.imageExtent.width * g_copy.imageExtent.height * 4u, (uint64_t)g_stagingBytes);
    CHECK(g_copy.imageExtent.height == 2, "landscape copy region height equals texHeight");
    CHECK((uint64_t)g_copy.imageExtent.width * g_copy.imageExtent.height * 4u <= (uint64_t)g_stagingBytes, "landscape copy region stays inside the staging buffer");

    // trigger: portrait 2x8 〜 the bug copies only texWidth rows and leaves mip 0 rows 2..7
    // undefined for the mip chain to inherit
    printf("trigger: portrait 2x8 file upload\n");
    CHECK(write_tga(port, 2, 8), "portrait TGA written");
    reset_captures();
    CHECK(createTextureImage(&ctx, cmd, &img, &alloc, &view, port, false, true, &staging), "portrait file upload succeeds");
    CHECK(g_imgExtent.width == 2 && g_imgExtent.height == 8, "portrait image created 2x8");
    if (g_copy.imageExtent.height != 8)
        printf("  (copy region %ux%u against the 2x8 image; rows %u..7 of mip 0 never uploaded)\n",
               g_copy.imageExtent.width, g_copy.imageExtent.height, g_copy.imageExtent.height);
    CHECK(g_copy.imageExtent.height == 8, "portrait copy region covers every image row");

    reset_captures();
    remove(sq); remove(land); remove(port);

    if (failures) {
        printf("anotest_texuploadguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_texuploadguard: all passed\n");
    return 0;
}
