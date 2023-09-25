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




// Variables


struct VulkanGarbage vulkanGarbage = { NULL, NULL}; // THROW OUT WHEN YOU'RE DONE WITH IT

// Function Prototypes

VkResult createInstance(VkInstance *instance, VkDebugUtilsMessengerEXT *debugMessenger);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window);
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);


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
	return;
}

// Graphics operations

void recordCommandBuffer(VulkanComponents* components, uint32_t imageIndex) 
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0; // Optional
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	if (vkBeginCommandBuffer(components->commandBuffer[components->frameIndex], &beginInfo) != VK_SUCCESS) 
	{
	    printf("Failed to begin recording command buffer!\n");
	}

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = components->renderPass;
	renderPassInfo.framebuffer = components->framebufferGroup.buffers[imageIndex];
	renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
	renderPassInfo.renderArea.extent = components->swapChainGroup.imageExtent;

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(components->commandBuffer[components->frameIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(components->commandBuffer[components->frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, components->graphicsPipeline);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)(components->swapChainGroup.imageExtent.width);
	viewport.height = (float)(components->swapChainGroup.imageExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(components->commandBuffer[components->frameIndex], 0, 1, &viewport);
	
	VkRect2D scissor = {};
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = components->swapChainGroup.imageExtent;
	vkCmdSetScissor(components->commandBuffer[components->frameIndex], 0, 1, &scissor);

	vkCmdDraw(components->commandBuffer[components->frameIndex], 3, 1, 0, 0);

	vkCmdEndRenderPass(components->commandBuffer[components->frameIndex]);

	if (vkEndCommandBuffer(components->commandBuffer[components->frameIndex]) != VK_SUCCESS) {
	    printf("Failed to record command buffer!\n");
	}
}

void drawFrame(VulkanComponents* components, GLFWwindow* window) 
{
	if (!components->skipCheck)
	{
		vkWaitForFences(components->device, 1, &(components->inFlightFence[components->frameIndex]), VK_TRUE, UINT64_MAX);
	} else
	{
		components->skipCheck -= 1; // Simple way to skip semaphore waits for a given number of frames
	}


	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(components->device, components->swapChainGroup.swapChain, UINT64_MAX, components->imageAvailableSemaphore[components->frameIndex], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || components->framebufferResized) 
	{
		
		printf("Recreating swap chain!\n");
	    recreateSwapChain(components, window);
	    return;
	} else if (result != VK_SUCCESS) 
	{
	    printf("Failed to acquire swap chain image!\n");
	}

	vkResetCommandBuffer(components->commandBuffer[components->frameIndex], 0);
	recordCommandBuffer(components, imageIndex);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = {components->imageAvailableSemaphore[components->frameIndex]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(components->commandBuffer[components->frameIndex]);
	VkSemaphore signalSemaphores[] = {components->renderFinishedSemaphore[components->frameIndex]};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(components->device, 1, &(components->inFlightFence[components->frameIndex])); // this goes here because multi-threading
	if (vkQueueSubmit(components->graphicsQueue, 1, &submitInfo, components->inFlightFence[components->frameIndex]) != VK_SUCCESS) 
	{
	    printf("Failed to submit draw command buffer!\n");
	    return;
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = {components->swapChainGroup.swapChain};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = NULL; // Optional
	vkQueuePresentKHR(components->presentQueue, &presentInfo);

	components->frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (components->frameIndex == 3)
	{
		components->frameIndex = 0;
	}

	return;
}

//Init and cleanup functions

VulkanComponents* initVulkan(GLFWwindow* window, VulkanComponents* components) // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{
	memset(components, 0, sizeof(VulkanComponents)); // Just in case there's garbage making our unitialized parts non-NULL
    if(components == NULL) 
    {
        fprintf(stderr, "Failed to allocate memory for Vulkan components!\n");
        return NULL;
    }

	components->enableValidationLayers = true;
	components->frameIndex = 0; // Tracks which frame is being processed

    // Initialize Vulkan
    if (createInstance(&(components->instance), &(components->debugMessenger)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        free(components);
        return NULL;
    }
    vulkanGarbage.components = components;

    // Create a window surface
    if (createSurface(components->instance, window, &(components->surface)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create window surface!\n");
        unInitVulkan();
        return NULL;
    }
    vulkanGarbage.components = components;

    // Pick physical device
    DeviceCapabilities capabilities;
    components->physicalDevice = VK_NULL_HANDLE;
    struct QueueFamilyIndices indices;
    if (!pickPhysicalDevice(components->instance, &(components->physicalDevice), &(components->surface), &capabilities, &indices))
    {
    	fprintf(stderr, "Quitting init: physical device failure!\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;
    
    // Create logical device
    if (createLogicalDevice(components->physicalDevice, &(components->device), &(components->graphicsQueue), &(components->computeQueue), &(components->transferQueue), &(components->presentQueue), &indices) != VK_SUCCESS)
    {
        fprintf(stderr, "Quitting init: logical device failure!\n");
        unInitVulkan();
        return NULL;
    }
    vulkanGarbage.components = components;

    components->swapChainGroup = initSwapChain(components->physicalDevice, components->device, &(components->surface), window); // Initialize a swap chain
    if (components->swapChainGroup.swapChain == NULL)
    {
    	printf("Quitting init: swap chain failure.\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;
    
    components->viewGroup = createImageViews(components->device, components->swapChainGroup);
    if (components->viewGroup.views == NULL)
    {
    	printf("Quitting init: image view failure.\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;

	if (createRenderPass(components->device, components->swapChainGroup.imageFormat, &(components->renderPass)) != true)
	{
		printf("Quitting init: render pass failure\n");
		unInitVulkan();
		return NULL;
	}
	vulkanGarbage.components = components;

	components->graphicsPipeline = createGraphicsPipeline(components->device, components->swapChainGroup.imageExtent, &(components->pipelineLayout), components->renderPass);
	if (components->graphicsPipeline == NULL)
	{
		printf("Quitting init: pipeline failure!\n");
		unInitVulkan();
		return NULL;
	}
	vulkanGarbage.components = components;

	if (createFramebuffers(components->device, &(components->framebufferGroup), components->viewGroup, components-> swapChainGroup, components->renderPass) != true)
	{
		printf("Quitting init: framebuffer failure!\n");
		unInitVulkan();
		return NULL;	
	}
	vulkanGarbage.components = components;

	if (createCommandPool(components->device, components->physicalDevice, components->surface, &(components->commandPool)) != true)
	{
		printf("Quitting init: command pool failure!\n");
		unInitVulkan();
		return NULL;
	}
	vulkanGarbage.components = components;

	if (createCommandBuffer(components) != true)
	{
		printf("Quitting init: command buffer failure!\n");
		unInitVulkan();
		return NULL;
	}
	vulkanGarbage.components = components;

	if (createSyncObjects(components) != true)
	{
		printf("Quitting init: sync failure!\n");
		unInitVulkan();
		return NULL;
	}

    
    return components;
}
