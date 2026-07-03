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

// Takes binary texture data and loads it into a Vulkan image object
bool createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, char* fileName, bool flag16, VkBuffer* outStagingBuffer);

bool createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, const unsigned char* pixels, uint32_t width, uint32_t height, VkBuffer* outStagingBuffer);

// Creates an image view for an entity with an existing texture
bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanContext* ctx, RendererState* state);


uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler);

// Helper functions

// Generic function for parametrized image creation
bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16);
// Ditto with a queue-family share list: >= 2 distinct families selects CONCURRENT sharing (cross-
// queue images for the async Hi-Z build, review finding 2); a NULL/short list behaves as createImage
bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount);
// Transitions an image layout for use in rendering
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
