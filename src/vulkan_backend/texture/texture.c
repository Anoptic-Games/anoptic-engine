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
    texture.pixels = stbi_load(fileName, &texture.texWidth, &texture.texHeight, &texture.texChannels, STBI_rgb_alpha);

	return texture;
}

bool transitionImageLayout(VulkanComponents* components, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(components);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0; // TODO
	barrier.dstAccessMask = 0; // TODO

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;
	
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
	    barrier.srcAccessMask = 0;
	    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	
	    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
	    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	
	    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	} else
	{
	    printf("Unsupported layout transition!\n");
	    return false;
	}

	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	{
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (hasStencilComponent(format))
		{
		    barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	} else
	{
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	
	vkCmdPipelineBarrier(
	    commandBuffer,
	    sourceStage, destinationStage,
	    0,
	    0, NULL,
	    0, NULL,
	    1, &barrier
	);

	endSingleTimeCommands(components, commandBuffer);
	return true;
}

void copyBufferToImage(VulkanComponents* components, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(components);
    
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	
	region.imageOffset.x = 0;
	region.imageOffset.y = 0;
	region.imageOffset.z = 0;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(
	    commandBuffer,
	    buffer,
	    image,
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    1,
	    &region
	);
	
	
    endSingleTimeCommands(components, commandBuffer);
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

	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.flags = 0; // Optional
	
	if (vkCreateImage(components->deviceQueueComp.device, &imageInfo, NULL, image) != VK_SUCCESS)
	{
	    printf("Failed to create image!\n");
	    return false;
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(components->deviceQueueComp.device, *image, &memRequirements);
	
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(components, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	if (vkAllocateMemory(components->deviceQueueComp.device, &allocInfo, NULL, imageMemory) != VK_SUCCESS)
	{
	    printf("Failed to allocate image memory!\n");
	    return false;
	}
	
	vkBindImageMemory(components->deviceQueueComp.device, *image, *imageMemory, 0);

	return true;
}

bool createTextureImage(VulkanComponents* components, VkImage* textureImage, VkDeviceMemory* textureImageMemory, char* fileName, bool flag16)
{
	//!TODO Add logic for 16-bit images
	Texture8 texture = readTexture8bit(fileName);
	if (!texture.pixels)
	{
        printf("Failed to load texture image: %s\n", fileName);
		return false;
    }

	VkDeviceSize imageSize = texture.texWidth * texture.texHeight * 4;	

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createDataBuffer(components, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

	void* data;
	vkMapMemory(components->deviceQueueComp.device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, texture.pixels, (size_t)(imageSize));
	vkUnmapMemory(components->deviceQueueComp.device, stagingBufferMemory);

	stbi_image_free(texture.pixels);

	if (!createImage(components, texture.texWidth, texture.texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory, false))
	{
		printf("Image creation failure: %s\n", fileName);
		return false;
	}

    // TODO: Figure out if this case ever occurs
    if(!transitionImageLayout(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
	{
		printf("Layout transition failure: %s\n", fileName);
		return false;
	}

	copyBufferToImage(components, stagingBuffer, *textureImage, (uint32_t) texture.texWidth, (uint32_t) texture.texWidth);

    // TODO: Figure out if this case ever occurs
	if(!transitionImageLayout(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
	{
		printf("Layout transition failure: %s\n", fileName);
		return false;
	}


	vkDestroyBuffer(components->deviceQueueComp.device, stagingBuffer, NULL);
	vkFreeMemory(components->deviceQueueComp.device, stagingBufferMemory, NULL);
	
	
	return true;
}

bool createTextureImageView(VulkanComponents* components, VkImage textureImage, VkImageView* textureImageView)
{
	*textureImageView = createImageView(components->deviceQueueComp.device, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

	return true;
}


bool createTextureSampler(VulkanComponents* components)
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	//TODO Physical device properties should be sampled once in the program's lifetime and cached
	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(components->physicalDeviceComp.physicalDevice, &properties);
	
	//TODO Add an anisotropic filter setting to vulkanConfig
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	
    if (vkCreateSampler(components->deviceQueueComp.device, &samplerInfo, NULL, &components->renderComp.textureSampler) != VK_SUCCESS)
    {
        printf("Failed to create texture sampler!\n");
        return false;
    }
    return true;
}
