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

bool transitionImageLayout(VulkanComponents* components, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
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
	barrier.subresourceRange.levelCount = mipLevels;
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

bool createImage(VulkanComponents* components, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory, bool flag16)
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = (uint32_t)(width);
	imageInfo.extent.height = (uint32_t)(height);
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;

	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = numSamples;
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

	VkResult result = vkAllocateMemory(components->deviceQueueComp.device, &allocInfo, NULL, imageMemory);
	if (result != VK_SUCCESS)
	{
		printf("Failed to allocate image memory! Return code: %d\n", result);
		return false;
	}
	
	vkBindImageMemory(components->deviceQueueComp.device, *image, *imageMemory, 0);

	return true;
}

bool generateMipmaps(VulkanComponents* components, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{
	// Check that we can actually do linear filtering first..
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(components->physicalDeviceComp.physicalDevice, imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
	{ // Change this to an alt case using software generation
		printf("Texture image does not support bilinear filtering!\n");
		return false;
	}

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(components);

	VkImageMemoryBarrier barrier =
	{// Zero-initialization to ensure no garbage is carried over
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = 0,
		.dstAccessMask = 0,
		.oldLayout = 0,
		.newLayout = 0,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = VK_NULL_HANDLE,
		.subresourceRange =
		{
			.aspectMask = 0,
			.baseMipLevel = 0,
			.levelCount = 0,
			.baseArrayLayer = 0,
			.layerCount = 0
		}
	};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	for (uint32_t i = 1; i < mipLevels; i++)
	{
		printf("Mip dimensions at level %d: %d,	%d\n", i, mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1);
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, NULL,
			0, NULL,
			1, &barrier);

		VkImageBlit blit = {};
		blit.srcOffsets[0].x = 0;
		blit.srcOffsets[0].y = 0;
		blit.srcOffsets[0].z = 0;
		blit.srcOffsets[1].x = mipWidth;
		blit.srcOffsets[1].y = mipHeight;
		blit.srcOffsets[1].z = 1;
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0].x = 0;
		blit.dstOffsets[0].y = 0;
		blit.dstOffsets[0].z = 0;
		blit.dstOffsets[1].x = mipWidth > 1 ? mipWidth / 2 : 1;
		blit.dstOffsets[1].y = mipHeight > 1 ? mipHeight / 2 : 1;
		blit.dstOffsets[1].z = 1;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(commandBuffer,
			image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, NULL,
			0, NULL,
			1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, NULL,
		0, NULL,
		1, &barrier);

	endSingleTimeCommands(components, commandBuffer);
	return true;
}

bool createTextureImage(VulkanComponents* components, VkImage* textureImage, VkDeviceMemory* textureImageMemory, VkImageView* textureImageView, char* fileName, bool flag16)
{
	//!TODO Add logic for 16-bit images
	Texture8 texture = readTexture8bit(fileName);
	if (!texture.pixels)
	{
		printf("Failed to load texture image: %s\n", fileName);
		return false;
	}

	VkDeviceSize imageSize = texture.texWidth * texture.texHeight * 4;
	texture.mipLevels = (uint32_t)(floor(log2(texture.texWidth > texture.texHeight ? texture.texWidth : texture.texHeight)) + 1); // Mipmap levels determined dynamically 

	printf("Texture mip levels: %d\n", texture.mipLevels);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createDataBuffer(components, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

	void* data;
	vkMapMemory(components->deviceQueueComp.device, stagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, texture.pixels, (size_t)(imageSize));
	vkUnmapMemory(components->deviceQueueComp.device, stagingBufferMemory);

	stbi_image_free(texture.pixels);

	if (!createImage(components, texture.texWidth, texture.texHeight, texture.mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory, false))
	{
		printf("Image creation failure: %s\n", fileName);
		return false;
	}

	// TODO: Figure out if this case ever occurs
	if(!transitionImageLayout(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels))
	{
		printf("Layout transition failure: %s\n", fileName);
		return false;
	}

	copyBufferToImage(components, stagingBuffer, *textureImage, (uint32_t) texture.texWidth, (uint32_t) texture.texWidth);

	generateMipmaps(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, texture.texWidth, texture.texHeight, texture.mipLevels);

	// TODO: Figure out if this case ever occurs
	/*if(!transitionImageLayout(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture.mipLevels))
	{
		printf("Layout transition failure: %s\n", fileName);
		return false;
	}*/

	vkDestroyBuffer(components->deviceQueueComp.device, stagingBuffer, NULL);
	vkFreeMemory(components->deviceQueueComp.device, stagingBufferMemory, NULL);
	
	if(!createTextureImageView(components, *textureImage, textureImageView, VK_FORMAT_R8G8B8A8_SRGB, texture.mipLevels))
	{
		printf("Image view creation failure: %s\n", fileName);
		return false;
	}
	
	return true;
}

bool createTextureImageFromCPUMemory(VulkanComponents* components, VkImage* textureImage, VkDeviceMemory* textureImageMemory, VkImageView* textureImageView, Texture8 texture, VkFormat format, bool flag16)
{
	//!TODO Add logic for 16-bit images
	if (!texture.pixels)
	{
		printf("Invalid Texture was given to pass onto the GPU!\n");
		return false;
	}

	VkDeviceSize imageSize = texture.texWidth * texture.texHeight * 4;
	texture.mipLevels = (uint32_t)(floor(log2(texture.texWidth > texture.texHeight ? texture.texWidth : texture.texHeight)) + 1); // Mipmap levels determined dynamically 

	printf("Texture mip levels from CPU Memory: %d\n", texture.mipLevels);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createDataBuffer(components, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

	void* data;
	vkMapMemory(components->deviceQueueComp.device, stagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, texture.pixels, (size_t)(imageSize));
	vkUnmapMemory(components->deviceQueueComp.device, stagingBufferMemory);

	stbi_image_free(texture.pixels);

	if (!createImage(components, texture.texWidth, texture.texHeight, texture.mipLevels, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory, false))
	{
		printf("Image creation failure from CPU Memory!\n");
		return false;
	}

	// TODO: Figure out if this case ever occurs
	if(!transitionImageLayout(components, *textureImage, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels))
	{
		printf("Layout transition failure from CPU Memory!\n");
		return false;
	}

	copyBufferToImage(components, stagingBuffer, *textureImage, (uint32_t) texture.texWidth, (uint32_t) texture.texWidth);

	generateMipmaps(components, *textureImage, format, texture.texWidth, texture.texHeight, texture.mipLevels);

	// TODO: Figure out if this case ever occurs
	/*if(!transitionImageLayout(components, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture.mipLevels))
	{
		printf("Layout transition failure: %s\n", fileName);
		return false;
	}*/

	vkDestroyBuffer(components->deviceQueueComp.device, stagingBuffer, NULL);
	vkFreeMemory(components->deviceQueueComp.device, stagingBufferMemory, NULL);
	
	if(!createTextureImageView(components, *textureImage, textureImageView, format, texture.mipLevels))
	{
		printf("Image view creation failure from CPU Memory!\n");
		return false;
	}
	
	return true;
}

bool createTextureImageView(VulkanComponents* components, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels)
{
	*textureImageView = createImageView(components->deviceQueueComp.device, textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT, miplevels);

	return true;
}


bool createTextureSampler(VulkanComponents* components)
{ // DONE? Turns out maxLod is ignored if the texture in question doesn't have that many levels. I'm sure we'll need more samplers at some point, but we don't need one per texture
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
	samplerInfo.maxLod = 20.0f; // This would technically be a 524K texture, I'm sure it's large enough
	
	if (vkCreateSampler(components->deviceQueueComp.device, &samplerInfo, NULL, &components->renderComp.textureSampler) != VK_SUCCESS)
	{
		printf("Failed to create texture sampler!\n");
		return false;
	}
	return true;
}
