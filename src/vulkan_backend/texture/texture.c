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

Texture8 readTexture8bit(const char* fileName)
{
	Texture8 texture = {};
	texture.pixels = stbi_load(fileName, &texture.texWidth, &texture.texHeight, &texture.texChannels, STBI_rgb_alpha);

	return texture;
}

uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{ // out: granted slot, or ANO_BINDLESS_NONE.
	// Null view/sampler: refuse before bump.
	if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
		ano_log(ANO_ERROR, "ERROR: Bindless registration refused an absent view or sampler!");
		return ANO_BINDLESS_NONE;
	}
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
				const uint32_t* shareFamilies, uint32_t shareFamilyCount,
				const VkFormat* viewFormats, uint32_t viewFormatCount)
{
	VkImageFormatListCreateInfo formatList = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO };
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
	// Two+ view formats -> MUTABLE_FORMAT plus explicit list.
	if (viewFormats != NULL && viewFormatCount >= 2)
	{
		formatList.viewFormatCount = viewFormatCount;
		formatList.pViewFormats = viewFormats;
		formatList.pNext = imageInfo.pNext;
		imageInfo.pNext = &formatList;
		imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

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

	// Bind fail: destroy image; arena not reclaimed.
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
							 tiling, usage, properties, image, imageAlloc, flag16, NULL, 0, NULL, 0);
}

// in: format. out: true iff the driver can linear-filter optimal-tiled blits of it.
static bool formatFiltersLinear(VulkanContext* ctx, VkFormat format)
{
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice, format, &formatProperties);
	return (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

[[nodiscard]] static bool generateMipmaps(VulkanContext* ctx, VkCommandBuffer cmd, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{ // true: every level of the chain is written and parked in SHADER_READ_ONLY_OPTIMAL
	// Filtering only when mipLevels > 1.
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

// Mutable view list order: SRGB, UNORM.
static const VkFormat textureViewFormats[2] = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM };

// in: usage. out: create-time image/blit format. COLOR -> SRGB, else UNORM.
static VkFormat textureBaseFormat(TextureUsageFlags usage)
{
	return (usage & TEXTURE_USE_COLOR) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

// in: usage. out: 2 iff COLOR|DATA (MUTABLE_FORMAT), else 0.
static uint32_t textureViewFormatCount(TextureUsageFlags usage)
{
	return ((usage & TEXTURE_USE_COLOR) && (usage & TEXTURE_USE_DATA)) ? 2u : 0u;
}

AnoTextureResult createTextureImageFromPixels(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
				const unsigned char* pixels, uint32_t width, uint32_t height,
				TextureUsageFlags usage, bool keepStaging)
{ // in: ctx, cmd or VK_NULL_HANDLE, RGBA8 pixels, usage. out: BUILT fills *pkg; else *pkg inert.
	if (pkg == NULL) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);
	*pkg = (TexturePackage){0}; // total before first acquisition
	// Refuse unknown/empty usage; refuse borrowed cmd without keepStaging.
	if ((usage & ~(TextureUsageFlags)(TEXTURE_USE_COLOR | TEXTURE_USE_DATA)) != 0u
		|| (usage & (TEXTURE_USE_COLOR | TEXTURE_USE_DATA)) == 0u
		|| (cmd != VK_NULL_HANDLE && !keepStaging))
		return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);

	VkImage image = VK_NULL_HANDLE; GpuAllocation alloc = (GpuAllocation){0};
	VkBuffer staging = VK_NULL_HANDLE; GpuAllocation stagingAlloc = (GpuAllocation){0};
	VkImageView srgbView = VK_NULL_HANDLE, unormView = VK_NULL_HANDLE;
	const VkFormat texFormat = textureBaseFormat(usage);
	const uint32_t mipLevels = 1;
	AnoTextureResultCode code = ANO_TEXTURE_DEVICE; // default; SOURCE overrides

	// NULL pixels, zero dim, or RGBA byte-count overflow.
	if (pixels == NULL || !(width >= 1) || !(height >= 1) ||
		!((VkDeviceSize)width * height <= UINT64_MAX / 4u))
	{
		ano_log(ANO_ERROR, "Pixel upload outside the domain: %ux%u", width, height);
		code = ANO_TEXTURE_SOURCE;
		goto fail;
	}

	// Widen before multiply.
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

	if (!createDataBuffer(ctx, &stagingAllocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, &stagingAlloc))
	{
		ano_log(ANO_ERROR, "Staging buffer creation failure!");
		goto fail;
	}

	memcpy(stagingAlloc.mapped, pixels, (size_t)(imageSize));

	if (!createImageShared(ctx, &textureAllocator, width, height, mipLevels, VK_SAMPLE_COUNT_1_BIT, texFormat, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&image, &alloc, false, NULL, 0, textureViewFormats, textureViewFormatCount(usage)))
	{
		ano_log(ANO_ERROR, "Image creation failure!");
		goto fail;
	}

	// Views before any vkCmd*.
	if ((usage & TEXTURE_USE_COLOR) && !createTextureImageView(ctx, image, &srgbView, VK_FORMAT_R8G8B8A8_SRGB, mipLevels))
	{
		ano_log(ANO_ERROR, "Colour image view creation failure!");
		goto fail;
	}
	if ((usage & TEXTURE_USE_DATA) && !createTextureImageView(ctx, image, &unormView, VK_FORMAT_R8G8B8A8_UNORM, mipLevels))
	{
		ano_log(ANO_ERROR, "Data image view creation failure!");
		goto fail;
	}

	if (!transitionImageLayout(ctx, cmd, image, texFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels))
	{
		ano_olog(ANO_ERROR, "Layout transition failure!");
		goto fail;
	}

	if (!copyBufferToImage(ctx, cmd, staging, image, width, height))
	{
		ano_log(ANO_ERROR, "Pixel upload failure!");
		goto fail;
	}

	if (!transitionImageLayout(ctx, cmd, image, texFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels))
	{
		ano_olog(ANO_ERROR, "Layout transition failure!");
		goto fail;
	}

	// Publish: caller owns from here.
	if (keepStaging) pkg->staging = staging; else vkDestroyBuffer(ctx->device, staging, NULL);
	pkg->image = image; pkg->alloc = alloc;
	pkg->srgbView = srgbView; pkg->unormView = unormView;
	pkg->mipLevels = mipLevels; pkg->width = width; pkg->height = height;
	return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_BUILT);

fail:
	// Inert handles are no-ops; arena span behind alloc is not reclaimed.
	vkDestroyImageView(ctx->device, unormView, NULL);
	vkDestroyImageView(ctx->device, srgbView, NULL);
	vkDestroyImage(ctx->device, image, NULL);
	vkDestroyBuffer(ctx->device, staging, NULL);
	return ANO_RESULT(AnoTextureResult, code);
}

AnoTextureResult createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
				const char* fileName, bool flag16,
				TextureUsageFlags usage, bool keepStaging)
{ // in: ctx, cmd or VK_NULL_HANDLE, path, usage. out: BUILT fills *pkg; else *pkg inert.
	//!TODO Add logic for 16-bit images
	(void)flag16;
	if (pkg == NULL) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);
	*pkg = (TexturePackage){0}; // total before first acquisition
	// Refuse unknown/empty usage; refuse borrowed cmd without keepStaging.
	if ((usage & ~(TextureUsageFlags)(TEXTURE_USE_COLOR | TEXTURE_USE_DATA)) != 0u
		|| (usage & (TEXTURE_USE_COLOR | TEXTURE_USE_DATA)) == 0u
		|| (cmd != VK_NULL_HANDLE && !keepStaging))
		return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);

	VkImage image = VK_NULL_HANDLE; GpuAllocation alloc = (GpuAllocation){0};
	VkBuffer staging = VK_NULL_HANDLE; GpuAllocation stagingAlloc = (GpuAllocation){0};
	VkImageView srgbView = VK_NULL_HANDLE, unormView = VK_NULL_HANDLE;
	Texture8 texture = {0};
	const VkFormat texFormat = textureBaseFormat(usage);
	AnoTextureResultCode code = ANO_TEXTURE_DEVICE; // default; SOURCE overrides

	// Decode gate: refuse zero/negative dims before log2.
	texture = readTexture8bit(fileName);
	if (!texture.pixels || !(texture.texWidth >= 1) || !(texture.texHeight >= 1))
	{
		ano_log(ANO_ERROR, "Failed to load texture image: %s", fileName);
		code = ANO_TEXTURE_SOURCE;
		goto fail;
	}

	// Widen before multiply.
	VkDeviceSize imageSize = (VkDeviceSize)texture.texWidth * texture.texHeight * 4;
	texture.mipLevels = (uint32_t)(floor(log2(texture.texWidth > texture.texHeight ? texture.texWidth : texture.texHeight)) + 1); // dynamic mip levels; the log2 operand is >= 1 by the decode gate
	// Dual-view: single mip.
	if (textureViewFormatCount(usage) == 2) texture.mipLevels = 1;
	// No linear filter -> single mip.
	if (texture.mipLevels > 1 && !formatFiltersLinear(ctx, texFormat))
	{
		ano_log(ANO_WARN, "Format lacks linear filtering; loading %s with a single mip.", fileName);
		texture.mipLevels = 1;
	}

	ano_debug_log(ANO_INFO, "Texture mip levels: %d", texture.mipLevels);

	if (!createDataBuffer(ctx, &stagingAllocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, &stagingAlloc))
	{
		ano_log(ANO_ERROR, "Staging buffer creation failure: %s", fileName);
		goto fail;
	}

	memcpy(stagingAlloc.mapped, texture.pixels, (size_t)(imageSize));

	// Drop decoded pixels before GPU work.
	stbi_image_free(texture.pixels); texture.pixels = NULL;

	if (!createImageShared(ctx, &textureAllocator, texture.texWidth, texture.texHeight, texture.mipLevels, VK_SAMPLE_COUNT_1_BIT, texFormat, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&image, &alloc, false, NULL, 0, textureViewFormats, textureViewFormatCount(usage)))
	{
		ano_log(ANO_ERROR, "Image creation failure: %s", fileName);
		goto fail;
	}

	// Views before any vkCmd*.
	if ((usage & TEXTURE_USE_COLOR) && !createTextureImageView(ctx, image, &srgbView, VK_FORMAT_R8G8B8A8_SRGB, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Colour image view creation failure: %s", fileName);
		goto fail;
	}
	if ((usage & TEXTURE_USE_DATA) && !createTextureImageView(ctx, image, &unormView, VK_FORMAT_R8G8B8A8_UNORM, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Data image view creation failure: %s", fileName);
		goto fail;
	}

	if (!transitionImageLayout(ctx, cmd, image, texFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Layout transition failure: %s", fileName);
		goto fail;
	}

	if (!copyBufferToImage(ctx, cmd, staging, image, (uint32_t) texture.texWidth, (uint32_t) texture.texHeight))
	{
		ano_log(ANO_ERROR, "Pixel upload failure: %s", fileName);
		goto fail;
	}

	// Leaves chain in SHADER_READ_ONLY_OPTIMAL.
	if (!generateMipmaps(ctx, cmd, image, texFormat, texture.texWidth, texture.texHeight, texture.mipLevels))
	{
		ano_log(ANO_ERROR, "Mip chain generation failure: %s", fileName);
		goto fail;
	}

	// Publish: caller owns from here.
	if (keepStaging) pkg->staging = staging; else vkDestroyBuffer(ctx->device, staging, NULL);
	pkg->image = image; pkg->alloc = alloc;
	pkg->srgbView = srgbView; pkg->unormView = unormView;
	pkg->mipLevels = texture.mipLevels;
	pkg->width = (uint32_t)texture.texWidth; pkg->height = (uint32_t)texture.texHeight;
	return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_BUILT);

fail:
	// Inert handles are no-ops; arena span behind alloc is not reclaimed.
	vkDestroyImageView(ctx->device, unormView, NULL);
	vkDestroyImageView(ctx->device, srgbView, NULL);
	vkDestroyImage(ctx->device, image, NULL);
	vkDestroyBuffer(ctx->device, staging, NULL);
	stbi_image_free(texture.pixels);
	return ANO_RESULT(AnoTextureResult, code);
}

bool createTextureImageView(VulkanContext* ctx, VkImage textureImage, VkImageView* textureImageView, VkFormat format, uint32_t miplevels)
{ // false iff createImageView returned VK_NULL_HANDLE
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
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; // 2D only: W is never sampled

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
