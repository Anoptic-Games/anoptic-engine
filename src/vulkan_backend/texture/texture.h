/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdbool.h>
#include <stb_image.h>
#include <math.h>

#include <anoptic_results.h>

#include "vulkan_backend/components.h"
#include "vulkan_backend/instance/instanceInit.h"

// Structs

typedef struct Texture8
{
	int32_t texWidth;
	int32_t texHeight;
	int32_t texChannels;
	uint32_t mipLevels;
	stbi_uc* pixels;
} Texture8;

// Texture construction outcomes. The loader reacts differently to each: a bad source file is this
// asset's problem and the next texture may still load, while a device or arena refusal is the whole
// load's problem and further construction only deepens it 〜 gpu_alloc is monotonic, so every
// attempt past the first refusal burns another span that never comes back.
ANO_RESULT_TYPE(AnoTextureResult,
    ANO_TEXTURE_BUILT = 0,   // *pkg carries a complete texture; the caller owns every handle in it
    ANO_TEXTURE_SOURCE,      // the source image is unusable: decode refused, or outside the domain
    ANO_TEXTURE_DEVICE,      // the device or the texture arena refused an acquisition
    ANO_TEXTURE_INVALID      // the call violates the contract: no usage bits set, null destination
);

// Which interpretations one image is sampled through. Both bits is one mutable-format image
// carrying both views over one allocation, never two uploads of the same texels.
typedef enum TextureUsageBits {
	TEXTURE_USE_NONE  = 0,
	TEXTURE_USE_COLOR = 1u << 0,   // sampled through the sRGB view
	TEXTURE_USE_DATA  = 1u << 1,   // sampled through the UNORM view
} TextureUsageBits;
typedef uint32_t TextureUsageFlags;   // the PbrFeatureFlags idiom, components.h:157

// One constructed texture, whole. srgbView is non-null iff COLOR was asked for and built, unormView
// iff DATA was, and a BUILT package carries at least one of them. staging is live and caller-owned
// through the caller's upload batch when keepStaging was asked for, VK_NULL_HANDLE otherwise.
typedef struct TexturePackage {
	VkImage       image;
	GpuAllocation alloc;
	VkImageView   srgbView;    // non-null iff COLOR was requested and built
	VkImageView   unormView;   // non-null iff DATA was requested and built
	VkBuffer      staging;     // live and caller-owned through the caller's batch, or VK_NULL_HANDLE
	uint32_t      mipLevels, width, height;
} TexturePackage;

// in: a BUILT package. out: the registry's teardown record over the same handles.
// The one converter, so the two field orders cannot drift apart.
static inline TextureData ano_texture_record(const TexturePackage* pkg)
{ return (TextureData){ .textureImage = pkg->image, .textureImageAlloc = pkg->alloc,
                        .srgbView = pkg->srgbView, .unormView = pkg->unormView }; }

// Functions

// Reads an image from storage and returns Vulkan-compatible 8-bit binary texture data
Texture8 readTexture8bit(const char* fileName);

// Borrowed-cmd contract, binding on createTextureImage, createTextureImageFromPixels and
// transitionImageLayout:
// cmd: a command buffer borrowed from the caller (the caller submits it), or VK_NULL_HANDLE to
// have this call mint, submit and retire one transient CB per operation. The callee does not
// distinguish a deliberate no-borrow from a caller whose own mint failed; both take the per-op
// path, and a refused per-op mint answers false with nothing recorded.
// Deferred: a distinguishable absent-CB spelling (ANO_CMD_NONE or a BorrowedCmd carrier) would let a caller refuse a whole phase on a failed batch mint instead of degrading to per-op submits; it re-plumbs beginSingleTimeCommands/endSingleTimeCommands (commands.c, instanceInit.h) and every borrow site (attachments.c:93/:119/:190/:229/:250/:277/:300, text_raster.c:806) and belongs to the texture-module owner.

// Loads binary texture data into a Vulkan image. usage picks the views: COLOR -> sRGB, DATA ->
// UNORM, both -> one mutable-format image carrying both; a mask naming neither is INVALID, so a
// BUILT package always carries at least one view. keepStaging publishes the staging buffer into
// pkg->staging for the caller's batch to retire; a borrowed cmd with keepStaging false is INVALID
// rather than a documented obligation, since the copy that CB carries reads that buffer until the
// caller submits and discharging it here would be a use-after-free.
// BUILT transfers the whole package; every other code leaves *pkg inert with no handle left live.
AnoTextureResult createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                    const char* fileName, bool flag16,
                                    TextureUsageFlags usage, bool keepStaging);

AnoTextureResult createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                              const unsigned char* pixels, uint32_t width, uint32_t height,
                                              TextureUsageFlags usage, bool keepStaging);

// Creates an image view for an entity with an existing texture.
// False leaves *textureImageView VK_NULL_HANDLE, which is what createImageView answers on failure.
[[nodiscard]] bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanContext* ctx, RendererState* state);


// "no bindless slot" answer: the same word MaterialData's texture fields and every fragment
// shader already read as "no texture" (components.c:139ff, flat.frag:215), so a refusal baked
// into the material SSBO reads as absent rather than as a real slot.
#define ANO_BINDLESS_NONE 0xFFFFFFFFu

// out: the slot the (view, sampler) pair now occupies, or ANO_BINDLESS_NONE when the array is
// full or either handle is absent. A granted slot is bta->textureCount < bta->maxTextures <=
// UINT32_MAX, so it is never ANO_BINDLESS_NONE: refusal is outside the index domain, not aliased
// onto slot 0. The descriptor is the pair, not the view.
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler);

// Helper functions

// Generic function for parametrized image creation
bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16);
// createImage with a queue-family share list, >= 2 distinct families selects CONCURRENT sharing.
// viewFormats mirrors it: >= 2 listed formats selects MUTABLE_FORMAT with that explicit list.
[[nodiscard]] bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount,
				const VkFormat* viewFormats, uint32_t viewFormatCount);
// Transitions an image layout for use in rendering.
// cmd: a command buffer borrowed from the caller (the caller submits it), or VK_NULL_HANDLE to
// have this call mint, submit and retire one transient CB per operation. The callee does not
// distinguish a deliberate no-borrow from a caller whose own mint failed; both take the per-op
// path, and a refused per-op mint answers false with nothing recorded.
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
