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

// Functions

// Reads an image from storage and returns Vulkan-compatible 8-bit binary texture data
Texture8 readTexture8bit(char* fileName);

// Borrowed-cmd contract, binding on createTextureImage, createTextureImageFromPixels and
// transitionImageLayout:
// cmd: a command buffer borrowed from the caller (the caller submits it), or VK_NULL_HANDLE to
// have this call mint, submit and retire one transient CB per operation. The callee does not
// distinguish a deliberate no-borrow from a caller whose own mint failed; both take the per-op
// path, and a refused per-op mint answers false with nothing recorded.
// Deferred: a distinguishable absent-CB spelling (ANO_CMD_NONE or a BorrowedCmd carrier) would let a caller refuse a whole phase on a failed batch mint instead of degrading to per-op submits; it re-plumbs beginSingleTimeCommands/endSingleTimeCommands (commands.c, instanceInit.h) and every borrow site (attachments.c:93/:119/:190/:229/:250/:277/:300, text_raster.c:806) and belongs to the texture-module owner.

// Loads binary texture data into a Vulkan image. srgb -> R8G8B8A8_SRGB, false -> UNORM
bool createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, char* fileName, bool flag16, bool srgb, VkBuffer* outStagingBuffer);

bool createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, const unsigned char* pixels, uint32_t width, uint32_t height, VkBuffer* outStagingBuffer);

// Creates an image view for an entity with an existing texture.
// False leaves *textureImageView VK_NULL_HANDLE, which is what createImageView answers on failure.
bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanContext* ctx, RendererState* state);


// "no bindless slot" answer: the same word MaterialData's texture fields and every fragment
// shader already read as "no texture" (components.c:139ff, flat.frag:215), so a refusal baked
// into the material SSBO reads as absent rather than as a real slot.
#define ANO_BINDLESS_NONE 0xFFFFFFFFu

// out: the slot the view now occupies, or ANO_BINDLESS_NONE when the array is full.
// A granted slot is bta->textureCount < bta->maxTextures <= UINT32_MAX, so it is never
// ANO_BINDLESS_NONE: refusal is outside the index domain, not aliased onto slot 0.
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler);

// Helper functions

// Generic function for parametrized image creation
bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16);
// createImage with a queue-family share list, >= 2 distinct families selects CONCURRENT sharing
bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount);
// Transitions an image layout for use in rendering.
// cmd: a command buffer borrowed from the caller (the caller submits it), or VK_NULL_HANDLE to
// have this call mint, submit and retire one transient CB per operation. The callee does not
// distinguish a deliberate no-borrow from a caller whose own mint failed; both take the per-op
// path, and a refused per-op mint answers false with nothing recorded.
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
