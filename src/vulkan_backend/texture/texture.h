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

#include "vulkan_backend/instance/instanceInit.h"

// Structs

typedef struct Texture8
{
	int32_t texWidth;
	int32_t texHeight;
	int32_t texChannels;
	stbi_uc* pixels;
} Texture8;

// Functions

// Reads an image from storage and returns Vulkan-compatible 8-bit binary texture data
Texture8 readTexture8bit(char* fileName);

// Takes binary texture data and loads it into a Vulkan image object
bool createTextureImage(VulkanComponents* components, EntityBuffer* entity, char* fileName, bool flag16);

// Creates an image view for an entity with an existing texture
bool createTextureImageView(VulkanComponents* components, EntityBuffer* entity);

// Creates a sampler definition for use in shaders
bool createTextureSampler(VulkanComponents* components);


// Helper functions

// Generic function for parametrized image creation
bool createImage(VulkanComponents* components, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
				VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory, bool flag16);
// Transitions an image layout for use in rendering
bool transitionImageLayout(VulkanComponents* components, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

#endif
