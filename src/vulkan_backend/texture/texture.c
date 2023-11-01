/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#define STB_IMAGE_IMPLEMENTATION

#include "vulkan_backend/texture/texture.h" 

// Functions

Texture8 readTexture8bit(char* fileName)
{
    Texture8 texture = {};
    stbi_uc* pixels = stbi_load(fileName, &texture.texWidth, &texture.texHeight, &texture.texChannels, STBI_rgb_alpha);

	return texture;
}

bool createImage(VulkanComponents* components, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
				VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory, bool flag16)
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = (uint32_t)(width);
	imageInfo.extent.height = (uint32_t)(height);
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;

	imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = 0; // Optional
	
	if (vkCreateImage(components->deviceQueueComp.device, &imageInfo, NULL, image) != VK_SUCCESS) {
	    printf("Failed to create image!\n");
	    return false;
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(components->deviceQueueComp.device, *image, &memRequirements);
	
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(components, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	if (vkAllocateMemory(components->deviceQueueComp.device, &allocInfo, NULL, imageMemory) != VK_SUCCESS) {
	    printf("Failed to allocate image memory!\n");
	    return false;
	}
	
	vkBindImageMemory(components->deviceQueueComp.device, image, imageMemory, 0);

	return true;
}

bool createTextureImage(VulkanComponents* components, EntityBuffer* entity, char* fileName, bool flag16)
{
	//!TODO Add logic for 16-bit images
	Texture8 texture = readTexture8bit("%s/resources/stextures/texture.jpg");
	if (!texture.pixels)
	{
        printf("Failed to load texture image: %s\n", fileName);
		return false;
    }

	VkDeviceSize imageSize = texture.texWidth * texture.texHeight * 4;	

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createDataBuffer(components, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(components->deviceQueueComp.device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, texture.pixels, (size_t)(imageSize));
	vkUnmapMemory(components->deviceQueueComp.device, stagingBufferMemory);

	stbi_image_free(texture.pixels);

	if (!createImage(components, texture.texWidth, texture.texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, entity->textureImage, entity->textureImageMemory, false))
	{
		printf("Image creation failure: %s\n", fileName);
	}
	
	return true;
}
