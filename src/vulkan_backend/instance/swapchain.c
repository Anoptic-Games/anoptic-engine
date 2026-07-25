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


// Surface enumeration caps. Real surfaces report far fewer; a longer list is clamped, never overrun.
enum { MAX_SURFACE_FORMATS = 64, MAX_PRESENT_MODES = 16 };

// Everything a swapchain needs from its surface. Values only, no owned storage.
typedef struct SwapChainSupport
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR       format;
	VkPresentModeKHR         presentMode;
} SwapChainSupport;


// Preferred format if the surface offers it, else the first one reported.
// availableFormats holds formatCount entries; formatCount is non-zero.
static VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *availableFormats, uint32_t formatCount)
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

// preferredMode if the surface offers it, else FIFO, which the spec always guarantees.
// availablePresentModes holds presentModesCount entries.
static VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *availablePresentModes, uint32_t presentModesCount, uint32_t preferredMode)
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

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window)
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

// Surface capabilities plus the format and present mode selected from them.
// Inputs: device (VkPhysicalDevice), surface (VkSurfaceKHR), preferredMode (uint32_t).
// Output: bool; *out is a total out-param, zeroed on every false. False means unreadable
// capabilities or a zero-format surface, so *out.format after true is one the surface listed.
static bool querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t preferredMode, SwapChainSupport* out)
{
    *out = (SwapChainSupport){0};
    SwapChainSupport support = {0};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities) != VK_SUCCESS)
        return false;

    VkSurfaceFormatKHR formats[MAX_SURFACE_FORMATS];
    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, NULL) != VK_SUCCESS || formatCount == 0)
        return false;
    if (formatCount > MAX_SURFACE_FORMATS)
        formatCount = MAX_SURFACE_FORMATS; // VK_INCOMPLETE, not an error

    VkResult formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, formats);
    if ((formatResult != VK_SUCCESS && formatResult != VK_INCOMPLETE) || formatCount == 0)
        return false;
    support.format = chooseSwapSurfaceFormat(formats, formatCount);

    VkPresentModeKHR modes[MAX_PRESENT_MODES];
    uint32_t modeCount = 0;
    support.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, NULL) == VK_SUCCESS && modeCount > 0)
    {
        if (modeCount > MAX_PRESENT_MODES)
            modeCount = MAX_PRESENT_MODES;
        VkResult modeResult = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, modes);
        if ((modeResult == VK_SUCCESS || modeResult == VK_INCOMPLETE) && modeCount > 0)
            support.presentMode = chooseSwapPresentMode(modes, modeCount, preferredMode);
    }

    *out = support;
    return true;
}

bool initSwapChain(VulkanContext* ctx, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state) // Selects and initializes a swap chain
{
    SwapChainSupport support;
    if (!querySwapChainSupport(ctx->physicalDevice, ctx->surface, preferredMode, &support))
    {
        ano_log(ANO_ERROR, "Failed to query swap chain support!");
        return false;
    }

    VkExtent2D chosenExtent = chooseSwapExtent(support.capabilities, window);

    uint32_t imageCount = support.capabilities.minImageCount + 1; // Request one more than minimum
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    { // maxImageCount 0 means no upper limit
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx->surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = support.format.format;
    createInfo.imageColorSpace = support.format.colorSpace;
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

    createInfo.preTransform = support.capabilities.currentTransform; // No pre-transform
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Ignore alpha channel
    createInfo.presentMode = support.presentMode;
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
    state->imageFormat = support.format.format;
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

	// Do not recreate FIF semaphores on reinit (bugs).
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
	if (!createImageViews(ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "View group re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}

	if (!createColorResourcesChecked(ctx))
	{
		ano_log(ANO_FATAL, "Colour target re-creation error, exiting!");
		cleanupVulkan(ctx);
		exit(1);
	}

	// Status, not a sniff: the builder fills frame 0 / view 0 first, so a later frame's refusal
	// leaves that field live.
	if (!createDepthResources(ctx, &rendererState))
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

// Central init component. VK_NULL_HANDLE on failure: a failed vkCreateImageView leaves *pView
// undefined, so the driver's out-param never reaches a caller.
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
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

	VkImageView imageView = VK_NULL_HANDLE;
	if (vkCreateImageView(device, &viewInfo, NULL, &imageView) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to create image view!");
		return VK_NULL_HANDLE;
	}

	return imageView;
}


// Swapchain colour views, one per presentable image. false leaves views NULL and viewCount 0.
// The pair is published only once the array exists and every slot holds a live view, so a
// non-zero viewCount always indexes that many live handles.
bool createImageViews(VulkanContext* ctx, RendererState* state)
{
    state->views = NULL;
    state->viewCount = 0;

    VkImageView* views = (VkImageView*)malloc(state->imageCount * sizeof(VkImageView));
    if (views == NULL)
    {
        ano_log(ANO_ERROR, "Failed to allocate %u swapchain image views!", state->imageCount);
        return false;
    }

    for (uint32_t i = 0; i < state->imageCount; i++)
    {
        views[i] = createImageView(ctx->device, state->images[i], state->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        if (views[i] == VK_NULL_HANDLE)
        { // Unwind the prefix: teardown never walks a partial set
            for (uint32_t j = 0; j < i; j++)
                vkDestroyImageView(ctx->device, views[j], NULL);
            free(views);
            return false;
        }
    }

    state->views = views;
    state->viewCount = state->imageCount;
    return true;
}

