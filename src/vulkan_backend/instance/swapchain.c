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
#include <anoptic_log.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"


struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface) // Available swap chains and capabilities
{
	struct SwapChainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, *surface, &details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &details.formatCount, NULL);

	details.formats = (VkSurfaceFormatKHR*) calloc(1, details.formatCount * sizeof(VkSurfaceFormatKHR));
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &details.formatCount, details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &details.presentModesCount, NULL);

	details.presentModes = (VkPresentModeKHR*) calloc(1, details.presentModesCount * sizeof(VkPresentModeKHR));
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &details.presentModesCount, details.presentModes);
		
	return details;
}


VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *availableFormats, uint32_t formatCount) 
{
    for (uint32_t i = 0; i < formatCount; i++) 
    {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB && availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
        {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *availablePresentModes, uint32_t presentModesCount, uint32_t preferredMode) 
{
    for (uint32_t i = 0; i < presentModesCount; i++) 
    {
        if (availablePresentModes[i] == preferredMode) 
        {
            return availablePresentModes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window) 
{ // Central init component, also used during resizes
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}
		else
		{
			int width, height;
			if (getChosenBorderless())
			{
				// Borderless: primary monitor resolution
				GLFWmonitor* primary = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(primary);
				width = mode->width;
				height = mode->height;
			} else
			{
				// Larger of window size or primary monitor resolution
				GLFWmonitor* primary = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(primary);
				glfwGetWindowSize(window, &width, &height);
				width = (width > mode->width) ? width : mode->width;
				height = (height > mode->height) ? height : mode->height;
			}
			VkExtent2D actualExtent =
			{
				(uint32_t)(width),
				(uint32_t)(height)
			};
			// Clamp render dimensions to min/max
			actualExtent.width = (actualExtent.width < capabilities.minImageExtent.width) ? capabilities.minImageExtent.width : actualExtent.width;
			actualExtent.width = (actualExtent.width > capabilities.maxImageExtent.width) ? capabilities.maxImageExtent.width : actualExtent.width;
			actualExtent.height = (actualExtent.height < capabilities.minImageExtent.height) ? capabilities.minImageExtent.height : actualExtent.height;
			actualExtent.height = (actualExtent.height > capabilities.maxImageExtent.height) ? capabilities.maxImageExtent.height : actualExtent.height;
			return actualExtent;
		}
}

bool initSwapChain(VulkanContext* ctx, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state) // Selects and initializes a swap chain
{
    struct SwapChainSupportDetails details = querySwapChainSupport(ctx->physicalDevice, &(ctx->surface));

    VkSurfaceFormatKHR chosenFormat = chooseSwapSurfaceFormat(details.formats, details.formatCount);
    VkPresentModeKHR chosenPresentMode = chooseSwapPresentMode(details.presentModes, details.presentModesCount, preferredMode);
    VkExtent2D chosenExtent = chooseSwapExtent(details.capabilities, window);

    uint32_t imageCount = details.capabilities.minImageCount + 1; // Request one more than minimum
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) 
    { // maxImageCount 0 means no upper limit
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx->surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = chosenExtent;
    createInfo.imageArrayLayers = 1; // Always 1 unless developing stereoscopic 3D
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = ctx->queueFamilyIndices;
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

    if (indices.graphicsFamily != indices.presentFamily) 
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT; // Concurrent when graphics != present queue
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } 
    else 
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // Exclusive when queues match
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = NULL; // Optional
    }

    createInfo.preTransform = details.capabilities.currentTransform; // No pre-transform
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Ignore alpha channel
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE; // Discard obscured pixels
    createInfo.oldSwapchain = oldSwapChain; // Old swapchain aids resource transfer on recreate

    VkSwapchainKHR swapChain;
    if (vkCreateSwapchainKHR(ctx->device, &createInfo, NULL, &swapChain) != VK_SUCCESS) 
    {
        return false;
    }

    // Swap chain images
    vkGetSwapchainImagesKHR(ctx->device, swapChain, &imageCount, NULL);
    VkImage* swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, swapChain, &imageCount, swapChainImages);

    state->swapChain = swapChain;
    state->imageFormat = chosenFormat.format;
    state->imageExtent = chosenExtent;
    // Per-view render extents: view 0 fills swapchain, aux views inset at W/3 x H/3
    state->viewExtent[0] = chosenExtent;
    for (uint32_t v = 1; v < ANO_VIEW_COUNT; v++) {
        uint32_t iw = chosenExtent.width / 3u;  if (iw < 1u) iw = 1u;
        uint32_t ih = chosenExtent.height / 3u; if (ih < 1u) ih = 1u;
        state->viewExtent[v] = (VkExtent2D){ iw, ih };
    }
    state->imageCount = imageCount;
    state->images = swapChainImages;

    return true;
}


void cleanupSwapChain(VulkanContext* ctx, RendererState* state)
{
    // Destroy swapchain image views
    for (size_t i = 0; i < state->viewCount; i++) 
    {
        vkDestroyImageView(ctx->device, state->views[i], NULL);
    }
    free(state->views);
    state->views = NULL;
    state->viewCount = 0;

    // Destroy per-view depth views + images
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            if (vr->depthView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->depthView, NULL);
                vr->depthView = VK_NULL_HANDLE;
            }
            if (vr->depthImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->depthImage, NULL);
                vr->depthImage = VK_NULL_HANDLE;
            }

            // Single-sample depth-resolve target
            if (vr->depthResolveView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->depthResolveView, NULL);
                vr->depthResolveView = VK_NULL_HANDLE;
            }
            if (vr->depthResolveImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->depthResolveImage, NULL);
                vr->depthResolveImage = VK_NULL_HANDLE;
            }

            // Hi-Z pyramid: destroy per-mip + sampled views and image
            for (uint32_t m = 0; m < vr->hizMipCount; m++)
            {
                if (vr->hizMipViews[m] != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(ctx->device, vr->hizMipViews[m], NULL);
                    vr->hizMipViews[m] = VK_NULL_HANDLE;
                }
            }
            if (vr->hizSampledView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->hizSampledView, NULL);
                vr->hizSampledView = VK_NULL_HANDLE;
            }
            if (vr->hizImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->hizImage, NULL);
                vr->hizImage = VK_NULL_HANDLE;
            }
            vr->hizMipCount = 0;
        }
    }


    // Per-view MSAA color + picking-id attachments
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
    {
        if (state->colorView[v])
        {
            vkDestroyImageView(ctx->device, state->colorView[v], NULL);
            state->colorView[v] = VK_NULL_HANDLE;
        }
        if (state->colorImage[v] != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx->device, state->colorImage[v], NULL);
            state->colorImage[v] = VK_NULL_HANDLE;
        }
        state->colorImageAlloc[v].memory = VK_NULL_HANDLE;

        if (state->pickIdView[v] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx->device, state->pickIdView[v], NULL);
            state->pickIdView[v] = VK_NULL_HANDLE;
        }
        if (state->pickIdImage[v] != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx->device, state->pickIdImage[v], NULL);
            state->pickIdImage[v] = VK_NULL_HANDLE;
        }
        state->pickIdImageAlloc[v].memory = VK_NULL_HANDLE;
    }

    // Per-view HDR resolve targets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            if (vr->hdrColorView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->hdrColorView, NULL);
                vr->hdrColorView = VK_NULL_HANDLE;
            }
            if (vr->hdrColorImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->hdrColorImage, NULL);
                vr->hdrColorImage = VK_NULL_HANDLE;
            }
            vr->hdrColorAlloc.memory = VK_NULL_HANDLE;

            // Picking-id resolve target (view 0 only)
            if (vr->pickIdResolveView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->pickIdResolveView, NULL);
                vr->pickIdResolveView = VK_NULL_HANDLE;
            }
            if (vr->pickIdResolveImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->pickIdResolveImage, NULL);
                vr->pickIdResolveImage = VK_NULL_HANDLE;
            }
            vr->pickIdResolveAlloc.memory = VK_NULL_HANDLE;
        }
    }

    // Text overlay raster targets
    ano_vk_text_destroy_overlay(ctx, state);

    // Swapchain kept for recreateSwapChain oldSwapChain
    if (state->images != NULL) {
        free(state->images);
        state->images = NULL;
    }


    gpu_alloc_reset(&swapchainAllocator);
}

void recreateSwapChain(VulkanContext* ctx, GLFWwindow* window)
{
	// Wait for device idle
	vkDeviceWaitIdle(ctx->device);

	// This is completely unecessary and introduces a bug on reinit.
	// for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
	// 	vkDestroySemaphore(ctx->device, rendererState.frames[i].imageAvailable, NULL);
	// 	vkDestroySemaphore(ctx->device, rendererState.frames[i].renderFinished, NULL);
		
	// 	VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	// 	vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &rendererState.frames[i].imageAvailable);
	// 	vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &rendererState.frames[i].renderFinished);
	// }

    // Clean up previous swapchain
	cleanupSwapChain(ctx, &rendererState);
    
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0)
	{
		ano_debug_log(ANO_INFO, "Sleeping!");
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}
    // Update extents for recreation
    rendererState.imageExtent.width = width;
    rendererState.imageExtent.height = height;
	// Save outdated swapchain
	VkSwapchainKHR oldSwapChain = rendererState.swapChain;
	rendererState.swapChain = VK_NULL_HANDLE;

	initSwapChain(ctx, window, getChosenPresentMode(), oldSwapChain, &rendererState);

	// Destroy old swapchain
	vkDestroySwapchainKHR(ctx->device, oldSwapChain, NULL);

	if(rendererState.swapChain == VK_NULL_HANDLE)
	{
		ano_log(ANO_FATAL, "Swap chain re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}
	createImageViews(ctx, &rendererState);
	if (rendererState.views == NULL)
	{
		ano_log(ANO_FATAL, "View group re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}

	createColorResources(ctx);

	createDepthResources(ctx, &rendererState);
	if (rendererState.frames[0].views[0].depthView == NULL)
	{
		ano_log(ANO_FATAL, "Depth resources re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}

	// Hi-Z pyramid: recreate at new resolution, rebind per-mip sets
	if (!createHiZResources(ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Hi-Z resources re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}
	updateHiZDescriptorSets(ctx, &rendererState);

	// Rebind tonemap sets to recreated HDR resolve views
	updateTonemapDescriptorSets(ctx, &rendererState);

	// Rebind text overlay sets
	ano_vk_text_update_sets(ctx, &rendererState);

	vkResetCommandPool(ctx->device, rendererState.commandPool, 0);
	if (rendererState.computeCommandPool != VK_NULL_HANDLE)
		vkResetCommandPool(ctx->device, rendererState.computeCommandPool, 0); // async Hi-Z build CBs
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) // Clear fences before resuming render
	{
		vkResetFences(ctx->device, 1, &(rendererState.frames[i].frameFence));
        rendererState.frames[i].frameSubmitted = false;
	}

	rendererState.framebufferResized = false;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{ // Central init component
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	if (vkCreateImageView(device, &viewInfo, NULL, &imageView) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to create image view!");
	}

	return imageView;
}


bool createImageViews(VulkanContext* ctx, RendererState* state)
{
    state->views = (VkImageView*)malloc(state->imageCount * sizeof(VkImageView));
    state->viewCount = state->imageCount;

    for (uint32_t i = 0; i < state->imageCount; i++) 
    {
        state->views[i] = createImageView(ctx->device, state->images[i], state->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
    return true;
}

