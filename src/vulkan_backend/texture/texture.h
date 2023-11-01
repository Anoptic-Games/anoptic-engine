/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#ifndef TEXTURE_H
#define TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdbool.h>
#include "stb_image.h"

#include "vulkan_backend/instance/instanceInit.h"

// Structs

typedef struct Texture8
{
	uint32_t texWidth;
	uint32_t texHeight;
	uint32_t texChannels;
	stbi_uc* pixels;
} Texture8;

// Functions

// Reads an image from storage and returns Vulkan-compatible 8-bit binary texture data
Texture8 readTexture8bit(char* fileName);

// Takes binary texture data and loads it into a Vulkan image object
bool createTextureImage(VulkanComponents* components, EntityBuffer* entity, char* fileName, bool flag16);



#endif
