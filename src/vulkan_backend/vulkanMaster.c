/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>



#define GLFW_INCLUDE_VULKAN

#include "vulkan_backend/instanceInit.h"

#include "vulkan_backend/structs.h"

#include "vulkan_backend/pipeline.h"

#include "vulkan_backend/vulkanConfig.h"



// Variables

static VulkanComponents components;

struct VulkanGarbage vulkanGarbage = { NULL, NULL, NULL}; // THROW OUT WHEN YOU'RE DONE WITH IT

static GLFWwindow* window;

static Monitors monitors =
{
	.monitorInfos = NULL,	// Array of MonitorInfo for each monitor
	.monitorCount = 0		// Total number of monitors
};


// Assorted utility functions

void unInitVulkan() // A celebration
{

	if (vulkanGarbage.components)
	{
		cleanupVulkan(vulkanGarbage.components);
	}
	
	if (vulkanGarbage.window)
	{
		glfwDestroyWindow(vulkanGarbage.window);
		glfwTerminate();
	}

	if (vulkanGarbage.monitors)
	{
		cleanupMonitors(vulkanGarbage.monitors);
	}
	return;
}

bool anoShouldClose()
{
	return glfwWindowShouldClose(window);
}

// Graphics operations

void recordCommandBuffer(uint32_t imageIndex) 
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0; // Optional
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	if (vkBeginCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex], &beginInfo) != VK_SUCCESS) 
	{
	    printf("Failed to begin recording command buffer!\n");
	}

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = components.renderComp.renderPass;
	renderPassInfo.framebuffer = components.swapChainComp.framebufferGroup.buffers[imageIndex];
	renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
	renderPassInfo.renderArea.extent = components.swapChainComp.swapChainGroup.imageExtent;

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(components.cmdComp.commandBuffer[components.syncComp.frameIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, components.renderComp.graphicsPipeline);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)(components.swapChainComp.swapChainGroup.imageExtent.width);
	viewport.height = (float)(components.swapChainComp.swapChainGroup.imageExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0, 1, &viewport);
	
	VkRect2D scissor = {};
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = components.swapChainComp.swapChainGroup.imageExtent;
	vkCmdSetScissor(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0, 1, &scissor);

	vkCmdDraw(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 3, 1, 0, 0);

	vkCmdEndRenderPass(components.cmdComp.commandBuffer[components.syncComp.frameIndex]);

	if (vkEndCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex]) != VK_SUCCESS) {
	    printf("Failed to record command buffer!\n");
	}
}

void drawFrame() 
{
	if (!components.syncComp.skipCheck)
	{
		vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[components.syncComp.frameIndex]), VK_TRUE, UINT64_MAX);
	} else
	{
		components.syncComp.skipCheck -= 1; // Simple way to skip semaphore waits for a given number of frames
	}


	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.swapChain, UINT64_MAX, components.syncComp.imageAvailableSemaphore[components.syncComp.frameIndex], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || components.syncComp.framebufferResized) 
	{
		
		printf("Recreating swap chain!\n");
	    recreateSwapChain(&components, window);
	    return;
	} else if (result != VK_SUCCESS) 
	{
	    printf("Failed to acquire swap chain image!\n");
	}

	vkResetCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0);
	recordCommandBuffer(imageIndex);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = {components.syncComp.imageAvailableSemaphore[components.syncComp.frameIndex]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(components.cmdComp.commandBuffer[components.syncComp.frameIndex]);
	VkSemaphore signalSemaphores[] = {components.syncComp.renderFinishedSemaphore[components.syncComp.frameIndex]};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[components.syncComp.frameIndex])); // this goes here because multi-threading
	if (vkQueueSubmit(components.deviceQueueComp.graphicsQueue, 1, &submitInfo, components.syncComp.inFlightFence[components.syncComp.frameIndex]) != VK_SUCCESS) 
	{
	    printf("Failed to submit draw command buffer!\n");
	    return;
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = {components.swapChainComp.swapChainGroup.swapChain};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = NULL; // Optional
	vkQueuePresentKHR(components.deviceQueueComp.presentQueue, &presentInfo);

	components.syncComp.frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (components.syncComp.frameIndex == 3)
	{
		components.syncComp.frameIndex = 0;
	}

	return;
}

//Init and cleanup functions

bool initVulkan() // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{

	// Window initialization
	WindowParameters parameters =
	{
		.width = 800,
    	.height = 600,
    	.monitorIndex = -1,        // Desired monitor index for fullscreen, -1 for windowed
    	.borderless = 0
	};

	vulkanGarbage.monitors = &monitors;
	cleanupMonitors(&monitors);
	printf("Here");
	enumerateMonitors(&monitors);
	window = initWindow(&components, parameters, &monitors);

	if (window == NULL)
	{
	    // Handle error
	    printf("Window initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	components.instanceDebug.enableValidationLayers = true;
	components.syncComp.frameIndex = 0; // Tracks which frame is being processed

    // Initialize Vulkan
    if (createInstance(&components) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        unInitVulkan();
        return false;
    }
    vulkanGarbage.components = &components;

    // Create a window surface
    if (createSurface(components.instanceDebug.instance, window, &(components.surface)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create window surface!\n");
        unInitVulkan();
        return false;
    }

    // Pick physical device
    DeviceCapabilities capabilities;
    components.physicalDeviceComp.physicalDevice = VK_NULL_HANDLE;
    struct QueueFamilyIndices indices;
	//!TODO replace empty char array with preffered device from VulkanSettings   
	char* preferredDevice = getChosenDevice();
	if (!pickPhysicalDevice(&(components), &capabilities, &(components.physicalDeviceComp.queueFamilyIndices), preferredDevice))
    {
    	fprintf(stderr, "Quitting init: physical device failure!\n");
    	unInitVulkan();
    	return false;
    }
    
    // Create logical device
    if (createLogicalDevice(components.physicalDeviceComp.physicalDevice, &(components.deviceQueueComp.device), &(components.deviceQueueComp.graphicsQueue), &(components.deviceQueueComp.computeQueue), &(components.deviceQueueComp.transferQueue), &(components.deviceQueueComp.presentQueue), &(components.physicalDeviceComp.queueFamilyIndices)) != VK_SUCCESS)
    {
        fprintf(stderr, "Quitting init: logical device failure!\n");
        unInitVulkan();
        return false;
    }

    components.swapChainComp.swapChainGroup = initSwapChain(components.physicalDeviceComp.physicalDevice, components.deviceQueueComp.device, &(components.surface), window, getChosenPresentMode()); // Initialize a swap chain
    if (components.swapChainComp.swapChainGroup.swapChain == NULL)
    {
    	printf("Quitting init: swap chain failure.\n");
    	unInitVulkan();
    	return false;
    }
    
    components.swapChainComp.viewGroup = createImageViews(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup);
    if (components.swapChainComp.viewGroup.views == NULL)
    {
    	printf("Quitting init: image view failure.\n");
    	unInitVulkan();
    	return false;
    }

	if (createRenderPass(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.imageFormat, &(components.renderComp.renderPass)) != true)
	{
		printf("Quitting init: render pass failure\n");
		unInitVulkan();
		return false;
	}

	components.renderComp.graphicsPipeline = createGraphicsPipeline(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.imageExtent, &(components.renderComp.pipelineLayout), components.renderComp.renderPass);
	if (components.renderComp.graphicsPipeline == NULL)
	{
		printf("Quitting init: pipeline failure!\n");
		unInitVulkan();
		return false;
	}
	printf("Framebuffers\n");

	if (createFramebuffers(components.deviceQueueComp.device, &(components.swapChainComp.framebufferGroup), components.swapChainComp.viewGroup, components.swapChainComp.swapChainGroup, components.renderComp.renderPass) != true)
	{
		printf("Quitting init: framebuffer failure!\n");
		unInitVulkan();
		return false;	
	}

	printf("Command pool\n");

	if (createCommandPool(components.deviceQueueComp.device, components.physicalDeviceComp.physicalDevice, components.surface, &(components.cmdComp.commandPool)) != true)
	{
		printf("Quitting init: command pool failure!\n");
		unInitVulkan();
		return false;
	}

	printf("Command buffer\n");

	if (createCommandBuffer(&components) != true)
	{
		printf("Quitting init: command buffer failure!\n");
		unInitVulkan();
		return false;
	}
	
	printf("Sync objects\n");

	if (createSyncObjects(&components) != true)
	{
		printf("Quitting init: sync failure!\n");
		unInitVulkan();
		return false;
	}

    
    return true;
}
