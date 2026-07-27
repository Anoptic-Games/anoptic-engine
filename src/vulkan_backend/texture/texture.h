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

// Texture construction outcomes.
// SOURCE: this asset. DEVICE: whole load (gpu_alloc monotonic). INVALID: contract violation.
ANO_RESULT_TYPE(AnoTextureResult,
    ANO_TEXTURE_BUILT = 0,   // *pkg complete; caller owns every handle
    ANO_TEXTURE_SOURCE,      // decode refused or outside domain
    ANO_TEXTURE_DEVICE,      // device or texture arena refused
    ANO_TEXTURE_INVALID      // no usage bits, or null destination
);

// Sample interpretations for one image. Both bits -> one mutable-format image, one allocation.
typedef enum TextureUsageBits {
	TEXTURE_USE_NONE  = 0,
	TEXTURE_USE_COLOR = 1u << 0,   // sRGB view
	TEXTURE_USE_DATA  = 1u << 1,   // UNORM view
} TextureUsageBits;
typedef uint32_t TextureUsageFlags;   // PbrFeatureFlags idiom, components.h:157

// One constructed texture.
// srgbView non-null iff COLOR built; unormView iff DATA; BUILT carries >= 1 view.
// staging live and caller-owned when keepStaging, else VK_NULL_HANDLE.
typedef struct TexturePackage {
	VkImage       image;
	GpuAllocation alloc;
	VkImageView   srgbView;    // iff COLOR built
	VkImageView   unormView;   // iff DATA built
	VkBuffer      staging;     // caller-owned through batch, or VK_NULL_HANDLE
	uint32_t      mipLevels, width, height;
} TexturePackage;

// in: BUILT package. out: registry teardown record over the same handles.
// inv: sole converter; field orders cannot drift.
static inline TextureData ano_texture_record(const TexturePackage* pkg)
{
	return (TextureData){
		.textureImage = pkg->image,
		.textureImageAlloc = pkg->alloc,
		.srgbView = pkg->srgbView,
		.unormView = pkg->unormView,
	};
}

// Functions

// Reads an image from storage and returns Vulkan-compatible 8-bit binary texture data
Texture8 readTexture8bit(const char* fileName);

// Borrowed-cmd contract (createTextureImage, createTextureImageFromPixels, transitionImageLayout):
// in: cmd borrowed (caller submits), or VK_NULL_HANDLE for a transient per-op mint/submit/retire.
// out: refused per-op mint -> false, nothing recorded.
// inv: deliberate no-borrow and failed caller mint are indistinguishable; both take the per-op path.

// Load file texels into a Vulkan image.
// in: usage (COLOR -> sRGB, DATA -> UNORM, both -> mutable dual-view); keepStaging publishes pkg->staging.
// out: BUILT transfers *pkg; else *pkg inert, no live handles.
// inv: usage with neither bit, or borrowed cmd with keepStaging false -> INVALID.
AnoTextureResult createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                    const char* fileName, bool flag16,
                                    TextureUsageFlags usage, bool keepStaging);

// Same from raw pixels.
AnoTextureResult createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                              const unsigned char* pixels, uint32_t width, uint32_t height,
                                              TextureUsageFlags usage, bool keepStaging);

// in: existing image. out: *textureImageView, or VK_NULL_HANDLE on false.
[[nodiscard]] bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanContext* ctx, RendererState* state);


// Absent bindless slot. Same sentinel MaterialData / flat.frag already read as "no texture".
#define ANO_BINDLESS_NONE 0xFFFFFFFFu

// out: slot for (view, sampler), or ANO_BINDLESS_NONE if full or either handle absent.
// inv: granted slot is never ANO_BINDLESS_NONE (refusal outside the index domain).
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
// Transition image layout. Borrowed-cmd contract above.
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
