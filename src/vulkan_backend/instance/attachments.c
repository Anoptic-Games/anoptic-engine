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
{ // Returns a device-supported format from a list of candidates, currently only used in pipeline images but should be used every time an image is created
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

	// One depth target per view per frame: each view's frustum has its own visibility/depth.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			ViewResources* vr = &state->frames[i].views[v];
			// SAMPLED_BIT (review 4.9 step 3): the Hi-Z reduce pass reads this MSAA depth via a
			// sampler2DMS to build the occlusion pyramid mip 0. Sized to the VIEW's extent.
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

			// Avenue 1 (review 4.9 step 3): single-sample MAX-resolve target for the Hi-Z reduce, only
			// when supported. Same format/aspect as depthImage, one sample. Sync build: rests in
			// DEPTH_ATTACHMENT (the build resolves into it, flips it to SHADER_READ for the reduce,
			// back). Async build (review finding 2): the reduce runs on the compute queue, so it rests
			// in SHADER_READ instead (graphics flips it to DEPTH_ATTACHMENT for its resolve write and
			// back before signaling) and is CONCURRENT-shared with the compute family.
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

// Hi-Z occlusion pyramid resources (review 4.9 step 3). One half-res R32F mip chain per view per
// frame-in-flight, built each frame from that view's MSAA depth (recordCommandBuffer) and sampled by
// the cull next frame. STORAGE (imageStore per mip) + SAMPLED (downsample reads mip k-1; cull reads
// the pyramid). Each image gets an all-mip sampled view + one single-mip storage view per level. Base
// dims are half the swapchain extent; recreated with the swapchain. Per-mip descriptor sets are
// allocated/written later (they need the layout + pool). No initial layout transition: the build
// transitions UNDEFINED->GENERAL each frame (it fully rewrites every mip).
bool createHiZResources(VulkanContext* ctx, RendererState* state)
{
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			// Per-view pyramid: half the VIEW's render extent (views render at their own
			// resolution, review finding 6), so mip counts differ per view.
			uint32_t w = (state->viewExtent[v].width  + 1u) / 2u; if (w < 1u) w = 1u;
			uint32_t h = (state->viewExtent[v].height + 1u) / 2u; if (h < 1u) h = 1u;
			uint32_t mips = 1u;
			for (uint32_t m = (w > h) ? w : h; m > 1u; m >>= 1) ++mips;
			if (mips > ANO_MAX_HIZ_MIPS) mips = ANO_MAX_HIZ_MIPS;

			ViewResources* vr = &state->frames[i].views[v];
			vr->hizWidth = w;
			vr->hizHeight = h;
			vr->hizMipCount = mips;
			// Async build (review finding 2): written by the compute queue, sampled by the graphics
			// queue's cull -> CONCURRENT between the two families (no ownership-transfer pairs).
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

			// Seed every pyramid to SHADER_READ so the first frames' cull (which samples not-yet-built
			// previous-slot pyramids, review 4.9 step 3) reads a valid layout. The per-frame build re-
			// transitions UNDEFINED->GENERAL (discarding), so this initial state is harmless.
			if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->hizImage, VK_FORMAT_R32_SFLOAT,
									   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mips))
			{
				ano_log(ANO_FATAL, "Failed to transition Hi-Z image for frame %u view %u!", i, v);
				return false;
			}
		}
	}

	// Warmup gate (review finding 2): the cull samples the pyramid built `lag` submits earlier, so
	// after any (re)creation the first `lag` frames would sample seeded garbage. updateCullingBuffers
	// forces hizParams.z = 0 (test off) for ordinals below this.
	state->hizValidOrdinal = state->timelineOrdinal + 1u + (state->asyncHiz ? 2u : 1u);
	return true;
}

void createColorResources(VulkanContext* ctx)
{
	// Geometry renders into an HDR float MSAA target (was the swapchain LDR format), so
	// many-light dynamic range is preserved; a fullscreen tonemap pass encodes to the
	// swapchain afterwards. The MSAA targets stay transient (resolved, never stored) and are
	// PER VIEW (review finding 6): each is sized to its view's extent, and the views stop
	// sharing an attachment — no inter-view reuse barrier, so their raster may overlap.
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

		// MSAA picking-id attachment (audit 3.1): mirrors the MSAA color above (transient,
		// resolved for view 0, discarded elsewhere). Per view like the color target: the opaque
		// pipeline always declares both attachments, so every view needs one at its own extent.
		createImage(ctx, &swapchainAllocator, rendererState.viewExtent[v].width, rendererState.viewExtent[v].height,
			1, ctx->msaaSamples, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &rendererState.pickIdImage[v], &rendererState.pickIdImageAlloc[v], false);
		rendererState.pickIdView[v] = createImageView(ctx->device, rendererState.pickIdImage[v], VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		if (!transitionImageLayout(ctx, VK_NULL_HANDLE, rendererState.pickIdImage[v], VK_FORMAT_R32_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1))
		{
			ano_log(ANO_ERROR, "Failed to transition picking id image layout (view %u)!", v);
		}
	}

	// Single-sample HDR resolve target, one per view per frame: resolve destination
	// (COLOR_ATTACHMENT) for that view's MSAA HDR pass, and composite source (SAMPLED). Not
	// transient — the composite reads it back. Seeded to SHADER_READ_ONLY; recordCommandBuffer
	// re-transitions per frame. Sized to the view's extent (the composite samples it 1:1).
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

			// Picking id resolve target: view 0 only (the gameplay view). Single-sample R32_UINT,
			// the SAMPLE_ZERO resolve destination of the shared MSAA id image, then a TRANSFER_SRC
			// for the cursor-texel copy. Resting layout COLOR_ATTACHMENT_OPTIMAL (the resolve layout).
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

	// Text overlay raster targets: per-frame, swapchain-sized.
	ano_vk_text_create_overlay(ctx, &rendererState);
}

