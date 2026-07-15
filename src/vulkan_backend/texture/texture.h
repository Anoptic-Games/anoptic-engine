/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "vulkan_backend/instance/instanceInit.h"

/* Functions */

// Upload packed RGBA8 (sRGB/UNORM). genMips => blit chain in cmd. Decode is RM's job.
bool createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, const unsigned char* pixels, uint32_t width, uint32_t height, bool srgb, bool genMips, VkBuffer* outStagingBuffer);

// Creates an image view for an entity with an existing texture
bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanContext* ctx, RendererState* state);


uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler);

/* Helper Functions */

// Generic function for parametrized image creation
bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16);
// createImage with a queue-family share list, >= 2 distinct families selects CONCURRENT sharing
bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount);
// Transitions an image layout for use in rendering
bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

#endif
