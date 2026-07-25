/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#define STB_IMAGE_IMPLEMENTATION

#include "vulkan_backend/texture/texture.h" 

#include <anoptic_log.h>

extern GpuAllocator textureAllocator;
extern GpuAllocator stagingAllocator;

// Functions

Texture8 readTexture8bit(char* fileName)
{
	Texture8 texture = {};
	texture.pixels = stbi_load(fileName, &texture.texWidth, &texture.texHeight, &texture.texChannels, STBI_rgb_alpha);

	return texture;
}

uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{ // out: the granted slot, or ANO_BINDLESS_NONE. A grant is < maxTextures, so it never spells refusal.
	if (bta->textureCount >= bta->maxTextures) {
		ano_log(ANO_ERROR, "ERROR: Bindless texture array full!");
		return ANO_BINDLESS_NONE;
	}

	uint32_t index = bta->textureCount;
	bta->textureCount++;

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = view;
	imageInfo.sampler = sampler;

	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = bta->set;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = index;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(ctx->device, 1, &descriptorWrite, 0, NULL);

	return index;
}

bool transitionImageLayout(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
{ // in: ctx, a borrowed cmd or VK_NULL_HANDLE to mint one. out: false if nothing was recorded.
	// A refused mint answers VK_NULL_HANDLE; the barrier below never runs on it.
	VkCommandBuffer commandBuffer = cmd == VK_NULL_HANDLE ? beginSingleTimeCommands(ctx) : cmd;
	if (commandBuffer == VK_NULL_HANDLE) return false;

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
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = 0;

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
	} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		// Transition to fragment-sampled read layout.
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else
	{
		ano_log(ANO_ERROR, "Unsupported layout transition!");
		return false;
	}

	// Aspect follows the format.
	if (format == VK_FORMAT_D32_SFLOAT || hasStencilComponent(format))
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

	if (cmd == VK_NULL_HANDLE) endSingleTimeCommands(ctx, commandBuffer);
	return true;
}

[[nodiscard]] static bool copyBufferToImage(VulkanContext* ctx, VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{ // in: ctx, a borrowed cmd or VK_NULL_HANDLE to mint one. out: false if nothing was recorded.
	// A refused mint answers VK_NULL_HANDLE; the copy below never runs on it.
	VkCommandBuffer commandBuffer = cmd == VK_NULL_HANDLE ? beginSingleTimeCommands(ctx) : cmd;
	if (commandBuffer == VK_NULL_HANDLE) return false;

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


	if (cmd == VK_NULL_HANDLE) endSingleTimeCommands(ctx, commandBuffer);
	return true;
}

bool createImageShared(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16,
				const uint32_t* shareFamilies, uint32_t shareFamilyCount)
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
	// Two+ distinct families -> CONCURRENT sharing, otherwise EXCLUSIVE.
	if (shareFamilies != NULL && shareFamilyCount >= 2)
	{
		imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
		imageInfo.queueFamilyIndexCount = shareFamilyCount;
		imageInfo.pQueueFamilyIndices = shareFamilies;
	}
	imageInfo.samples = numSamples;
	imageInfo.flags = 0; // optional

	// Every refusal below is total: *image VK_NULL_HANDLE, *imageAlloc the empty allocation.
	if (vkCreateImage(ctx->device, &imageInfo, NULL, image) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to create image!");
		*image = VK_NULL_HANDLE; // driver leaves it undefined on error
		*imageAlloc = (GpuAllocation){0};
		return false;
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(ctx->device, *image, &memRequirements);

	*imageAlloc = gpu_alloc(allocator, memRequirements, properties);
	if (imageAlloc->memory == VK_NULL_HANDLE) {
		vkDestroyImage(ctx->device, *image, NULL);
		*image = VK_NULL_HANDLE; // clear dangling handle
		return false;
	}

	// A refused bind leaves the image created but unbacked; the first record against it would
	// read unbacked memory. The arena span is monotonic, so nothing is reclaimed here.
	if (vkBindImageMemory(ctx->device, *image, imageAlloc->memory, imageAlloc->offset) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to bind image memory!");
		vkDestroyImage(ctx->device, *image, NULL);
		*image = VK_NULL_HANDLE;
		*imageAlloc = (GpuAllocation){0};
		return false;
	}

	return true;
}

bool createImage(VulkanContext* ctx, GpuAllocator* allocator, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
				VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, GpuAllocation* imageAlloc, bool flag16)
{
	return createImageShared(ctx, allocator, width, height, mipLevels, numSamples, format,
							 tiling, usage, properties, image, imageAlloc, flag16, NULL, 0);
}

// in: format. out: true iff the driver can linear-filter optimal-tiled blits of it.
// Sole decode point for "can a mip chain be generated for this format".
static bool formatFiltersLinear(VulkanContext* ctx, VkFormat format)
{
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice, format, &formatProperties);
	return (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

[[nodiscard]] static bool generateMipmaps(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{ // true: every level of the chain is written and parked in SHADER_READ_ONLY_OPTIMAL
	// Only the downsample blits need filtering; a one-level chain needs none.
	if (mipLevels > 1 && !formatFiltersLinear(ctx, imageFormat))
	{ // TODO software-generation fallback
		ano_log(ANO_ERROR, "Texture image does not support bilinear filtering!");
		return false;
	}

	// A refused mint answers VK_NULL_HANDLE; the blits below never run on it.
	VkCommandBuffer commandBuffer = cmd == VK_NULL_HANDLE ? beginSingleTimeCommands(ctx) : cmd;
	if (commandBuffer == VK_NULL_HANDLE) return false;

	VkImageMemoryBarrier barrier =
	{// Zero-initialize
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
		ano_debug_log(ANO_INFO, "Mip dimensions at level %d: %d,	%d", i, mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1);
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

	if (cmd == VK_NULL_HANDLE) endSingleTimeCommands(ctx, commandBuffer);
	return true;
}

bool createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, const unsigned char* pixels, uint32_t width, uint32_t height, VkBuffer* outStagingBuffer)
{
	// This face's extents come from the caller, not a decoder, so it owns its own domain: accept-form,
	// so a NULL source, a zero (or wrapped-negative) dim, or a pixel count whose RGBA byte count would
	// overflow the widened math refuses here rather than reaching a zero-extent vkCreateImage or a
	// mip derivation. Nothing acquired: total the face.
	if (pixels == NULL || !(width >= 1) || !(height >= 1) ||
	    !((VkDeviceSize)width * height <= UINT64_MAX / 4u))
	{
		ano_log(ANO_ERROR, "Pixel upload outside the domain: %ux%u", width, height);
		*textureImage = VK_NULL_HANDLE;
		*textureImageAlloc = (GpuAllocation){0};
		*textureImageView = VK_NULL_HANDLE;
		return false;
	}

	// Widen before multiplying: the uint32_t product wraps and would undersize the staging buffer.
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;
	uint32_t mipLevels = 1;

	VkBuffer stagingBuffer;
	GpuAllocation stagingAlloc;
	// Refused before anything is acquired: the mapping below is only reachable on the arm that wrote it.
	if (!createDataBuffer(ctx, &stagingAllocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingAlloc))
	{
		ano_log(ANO_ERROR, "Staging buffer creation failure!");
		// Nothing acquired: total the face so the caller's render state never keeps prior contents.
		*textureImage = VK_NULL_HANDLE;
		*textureImageAlloc = (GpuAllocation){0};
		*textureImageView = VK_NULL_HANDLE;
		return false;
	}

	void* data = stagingAlloc.mapped;
	memcpy(data, pixels, (size_t)(imageSize));

	if(!createImage(ctx, &textureAllocator, width, height, mipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageAlloc, false))
	{
		ano_log(ANO_ERROR, "Image creation failure!");
		return false;
	}

	if(!transitionImageLayout(ctx, cmd, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels))
	{
		ano_olog(ANO_ERROR, "Layout transition failure!");
		return false;
	}

	if(!copyBufferToImage(ctx, cmd, stagingBuffer, *textureImage, width, height))
	{
		ano_log(ANO_ERROR, "Pixel upload failure!");
		return false;
	}

	if(!transitionImageLayout(ctx, cmd, *textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels))
	{
		ano_olog(ANO_ERROR, "Layout transition failure!");
		return false;
	}

	if (outStagingBuffer) *outStagingBuffer = stagingBuffer; else vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
	
	if(!createTextureImageView(ctx, *textureImage, textureImageView, VK_FORMAT_R8G8B8A8_SRGB, mipLevels))
	{
		ano_log(ANO_ERROR, "Image view creation failure!");
		return false;
	}

	return true;
}
bool createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, char* fileName, bool flag16, bool srgb, VkBuffer* outStagingBuffer)
{
	//!TODO Add logic for 16-bit images
	// The decode seam owns the extent domain: accept-form, so a zero or negative dim refuses here
	// rather than reaching log2 in the mip derivation below. Freeing NULL is a no-op.
	Texture8 texture = readTexture8bit(fileName);
	if (!texture.pixels || !(texture.texWidth >= 1) || !(texture.texHeight >= 1))
	{
		ano_log(ANO_ERROR, "Failed to load texture image: %s", fileName);
		stbi_image_free(texture.pixels);
		*textureImage = VK_NULL_HANDLE;
		*textureImageAlloc = (GpuAllocation){0};
		*textureImageView = VK_NULL_HANDLE;
		return false;
	}

	// sRGB for color textures, linear UNORM for data textures.
	VkFormat texFormat = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

	// Widen before multiplying: stbi's int32 dims overflow the int product above ~23170 square.
	VkDeviceSize imageSize = (VkDeviceSize)texture.texWidth * texture.texHeight * 4;
	texture.mipLevels = (uint32_t)(floor(log2(texture.texWidth > texture.texHeight ? texture.texWidth : texture.texHeight)) + 1); // dynamic mip levels; the log2 operand is >= 1 by the decode gate
	// The chain is only as long as the format can blit: no linear filter, no tail to leave unwritten.
	if (texture.mipLevels > 1 && !formatFiltersLinear(ctx, texFormat))
	{
		ano_log(ANO_WARN, "Format lacks linear filtering; loading %s with a single mip.", fileName);
		texture.mipLevels = 1;
	}

	ano_debug_log(ANO_INFO, "Texture mip levels: %d", texture.mipLevels);

	VkBuffer stagingBuffer;
	GpuAllocation stagingAlloc;
	// Refused before anything is acquired: the mapping below is only reachable on the arm that wrote it.
	if (!createDataBuffer(ctx, &stagingAllocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingAlloc))
	{
		ano_log(ANO_ERROR, "Staging buffer creation failure: %s", fileName);
		stbi_image_free(texture.pixels);
		// Nothing acquired: total the face so the caller's render state never keeps prior contents.
		*textureImage = VK_NULL_HANDLE;
		*textureImageAlloc = (GpuAllocation){0};
		*textureImageView = VK_NULL_HANDLE;
		return false;
	}

	void* data = stagingAlloc.mapped;
	memcpy(data, texture.pixels, (size_t)(imageSize));

	stbi_image_free(texture.pixels);

	if (!createImage(ctx, &textureAllocator, texture.texWidth, texture.texHeight, texture.mipLevels, VK_SAMPLE_COUNT_1_BIT, texFormat, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageAlloc, false))
	{
		ano_log(ANO_ERROR, "Image creation failure: %s", fileName);
		return false;
	}

	if(!transitionImageLayout(ctx, cmd, *textureImage, texFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Layout transition failure: %s", fileName);
		return false;
	}

	if (!copyBufferToImage(ctx, cmd, stagingBuffer, *textureImage, (uint32_t) texture.texWidth, (uint32_t) texture.texHeight))
	{
		ano_log(ANO_ERROR, "Pixel upload failure: %s", fileName);
		return false;
	}

	// Publishes the whole chain in SHADER_READ_ONLY_OPTIMAL, which is the layout the bindless descriptor pins.
	if (!generateMipmaps(ctx, cmd, *textureImage, texFormat, texture.texWidth, texture.texHeight, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Mip chain generation failure: %s", fileName);
		return false;
	}

	if (outStagingBuffer) *outStagingBuffer = stagingBuffer; else vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
	if(!createTextureImageView(ctx, *textureImage, textureImageView, texFormat, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Image view creation failure: %s", fileName);
		return false;
	}
	
	return true;
}

bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels)
{ // createImageView answers VK_NULL_HANDLE on failure, which is what false means here
	*textureImageView = createImageView(ctx->device, textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT, miplevels);

	return *textureImageView != VK_NULL_HANDLE;
}


bool createTextureSampler(VulkanContext* ctx, RendererState* state)
{ // One shared sampler suffices.
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	//TODO Cache physical device properties once.
	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &properties);
	
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
	samplerInfo.maxLod = 20.0f; // 524K texture cap
	
	if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &state->textureSampler) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create texture sampler!");
		return false;
	}
	return true;
}
