/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"


VkFormat findSupportedFormat(VulkanContext* ctx, const VkFormat* candidates, uint32_t candidateCount, VkImageTiling tiling, VkFormatFeatureFlags features)
{ // Returns a device-supported format from a candidate list
	for (int i = 0; i < candidateCount; i++)
	{
		VkFormat format = candidates[i];
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
		{
			return format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
		{
			return format;
		}
	}
	ano_log(ANO_ERROR, "Failed to find a suitable format!");
	return VK_FORMAT_UNDEFINED;
}


// !TODO move this to a bottom-level library
VkFormat findDepthFormat(VulkanContext* ctx)
{
	VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
	return(findSupportedFormat(ctx, &candidates[0], sizeof(candidates)/sizeof(VkFormat),
								VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
}

bool hasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


bool createDepthResources(VulkanContext* ctx, RendererState* state)
{
	VkFormat depthFormat = findDepthFormat(ctx);
	if (depthFormat == VK_FORMAT_UNDEFINED)
	{
		ano_log(ANO_FATAL, "No compatible depth formats detected!");
		return false;
	}
	state->depthFormat = depthFormat;

	// One depth target per view per frame.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			ViewResources* vr = &state->frames[i].views[v];
			// MSAA depth target, view-sized, sampled by the Hi-Z reduce.
			if (!createImage(ctx, &swapchainAllocator, state->viewExtent[v].width,
				state->viewExtent[v].height, 1, ctx->msaaSamples, state->depthFormat,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							 &vr->depthImage, &vr->depthAlloc, false))
			{
				ano_log(ANO_FATAL, "Failed to create depth resource for frame %d view %u!", i, v);
				return false;
			}

			vr->depthView = createImageView(ctx->device, vr->depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

			if(!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->depthImage, depthFormat,
									  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1))
			{
				ano_log(ANO_FATAL, "Failed to transition depth buffer layout for frame %d view %u!", i, v);
				return false;
			}

			// Single-sample MAX-resolve target for the Hi-Z reduce, when supported.
			if (ctx->deviceCapabilities.depthMaxResolve)
			{
				uint32_t shareFamilies[2] = { ctx->queueFamilyIndices.graphicsFamily, ctx->queueFamilyIndices.computeFamily };
				if (!createImageShared(ctx, &swapchainAllocator, state->viewExtent[v].width,
					state->viewExtent[v].height, 1, VK_SAMPLE_COUNT_1_BIT, state->depthFormat,
					VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
								 &vr->depthResolveImage, &vr->depthResolveAlloc, false,
								 shareFamilies, state->asyncHiz ? 2u : 0u))
				{
					ano_log(ANO_FATAL, "Failed to create depth-resolve resource for frame %d view %u!", i, v);
					return false;
				}
				vr->depthResolveView = createImageView(ctx->device, vr->depthResolveImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
				if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->depthResolveImage, depthFormat,
										  VK_IMAGE_LAYOUT_UNDEFINED,
										  state->asyncHiz ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
										                  : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1))
				{
					ano_log(ANO_FATAL, "Failed to transition depth-resolve layout for frame %d view %u!", i, v);
					return false;
				}
			}
		}
	}
	return true;
}

// Hi-Z occlusion pyramid: one half-res R32F mip chain per view per frame-in-flight.
bool createHiZResources(VulkanContext* ctx, RendererState* state)
{
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			// Per-view pyramid, half the view's render extent.
			uint32_t w = (state->viewExtent[v].width  + 1u) / 2u; if (w < 1u) w = 1u;
			uint32_t h = (state->viewExtent[v].height + 1u) / 2u; if (h < 1u) h = 1u;
			uint32_t mips = 1u;
			for (uint32_t m = (w > h) ? w : h; m > 1u; m >>= 1) ++mips;
			if (mips > ANO_MAX_HIZ_MIPS) mips = ANO_MAX_HIZ_MIPS;

			ViewResources* vr = &state->frames[i].views[v];
			vr->hizWidth = w;
			vr->hizHeight = h;
			vr->hizMipCount = mips;
			// Async build: CONCURRENT between the graphics and compute families.
			uint32_t shareFamilies[2] = { ctx->queueFamilyIndices.graphicsFamily, ctx->queueFamilyIndices.computeFamily };
			if (!createImageShared(ctx, &swapchainAllocator, w, h, mips, VK_SAMPLE_COUNT_1_BIT,
				VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&vr->hizImage, &vr->hizAlloc, false,
				shareFamilies, state->asyncHiz ? 2u : 0u))
			{
				ano_log(ANO_FATAL, "Failed to create Hi-Z image for frame %u view %u!", i, v);
				return false;
			}

			vr->hizSampledView = createImageView(ctx->device, vr->hizImage, VK_FORMAT_R32_SFLOAT,
												 VK_IMAGE_ASPECT_COLOR_BIT, mips);

			for (uint32_t m = 0; m < mips; m++)
			{
				VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				iv.image = vr->hizImage;
				iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
				iv.format = VK_FORMAT_R32_SFLOAT;
				iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				iv.subresourceRange.baseMipLevel = m;
				iv.subresourceRange.levelCount = 1;
				iv.subresourceRange.baseArrayLayer = 0;
				iv.subresourceRange.layerCount = 1;
				if (vkCreateImageView(ctx->device, &iv, NULL, &vr->hizMipViews[m]) != VK_SUCCESS)
				{
					ano_log(ANO_FATAL, "Failed to create Hi-Z mip view %u (frame %u view %u)!", m, i, v);
					return false;
				}
			}

			// Seed every pyramid to SHADER_READ for the first frames' cull.
			if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->hizImage, VK_FORMAT_R32_SFLOAT,
									   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mips))
			{
				ano_log(ANO_FATAL, "Failed to transition Hi-Z image for frame %u view %u!", i, v);
				return false;
			}
		}
	}

	// Warmup gate: updateCullingBuffers disables the Hi-Z test for ordinals below this.
	state->hizValidOrdinal = state->timelineOrdinal + 1u + (state->asyncHiz ? 2u : 1u);
	return true;
}

void createColorResources(VulkanContext* ctx)
{
	// Transient HDR float MSAA target, per view, resolved by a later tonemap pass.
	VkFormat colorFormat = ANO_HDR_COLOR_FORMAT;

	for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
	{
		createImage(ctx, &swapchainAllocator, rendererState.viewExtent[v].width, rendererState.viewExtent[v].height,
			1, ctx->msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &rendererState.colorImage[v], &rendererState.colorImageAlloc[v], false);
		rendererState.colorView[v] = createImageView(ctx->device, rendererState.colorImage[v], colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

		if (!transitionImageLayout(ctx, VK_NULL_HANDLE, rendererState.colorImage[v], colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1))
		{
			ano_log(ANO_ERROR, "Failed to transition color image layout (view %u)!", v);
		}

		// MSAA picking-id attachment, per view, mirrors the MSAA color target.
		createImage(ctx, &swapchainAllocator, rendererState.viewExtent[v].width, rendererState.viewExtent[v].height,
			1, ctx->msaaSamples, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &rendererState.pickIdImage[v], &rendererState.pickIdImageAlloc[v], false);
		rendererState.pickIdView[v] = createImageView(ctx->device, rendererState.pickIdImage[v], VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		if (!transitionImageLayout(ctx, VK_NULL_HANDLE, rendererState.pickIdImage[v], VK_FORMAT_R32_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1))
		{
			ano_log(ANO_ERROR, "Failed to transition picking id image layout (view %u)!", v);
		}
	}

	// Single-sample HDR resolve target, one per view per frame.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			ViewResources* vr = &rendererState.frames[i].views[v];
			createImage(ctx, &swapchainAllocator, rendererState.viewExtent[v].width, rendererState.viewExtent[v].height,
				1, VK_SAMPLE_COUNT_1_BIT, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vr->hdrColorImage, &vr->hdrColorAlloc, false);
			vr->hdrColorView = createImageView(ctx->device, vr->hdrColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
			if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->hdrColorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1))
			{
				ano_log(ANO_ERROR, "Failed to transition HDR resolve image layout!");
			}

			// Picking id resolve target, view 0 only.
			if (v == 0)
			{
				createImage(ctx, &swapchainAllocator, rendererState.imageExtent.width, rendererState.imageExtent.height,
					1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vr->pickIdResolveImage, &vr->pickIdResolveAlloc, false);
				vr->pickIdResolveView = createImageView(ctx->device, vr->pickIdResolveImage, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
				if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->pickIdResolveImage, VK_FORMAT_R32_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1))
				{
					ano_log(ANO_ERROR, "Failed to transition picking id resolve image layout!");
				}
			}
		}
	}

	// Text overlay raster targets, per-frame, swapchain-sized.
	ano_vk_text_create_overlay(ctx, &rendererState);
}

