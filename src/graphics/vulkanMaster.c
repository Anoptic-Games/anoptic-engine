//========================================================================
// Panopticon 0.01
//------------------------------------------------------------------------
// Copyright (c) 2023 Matei Anghel
// Copyright (c) 2023 Cristian Necsoiu
//
// This file is part of 'The Anopticon Game Engine'.
// 
// 'The Anopticon Game Engine' is free software: you can redistribute it
// and/or modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation, version 3 of the License.
//
// 'The Anopticon Game Engine' is distributed WITHOUT ANY WARRANTY, without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
// See the GNU General Public License for more details.
//
// This notice may not be removed or altered from any source distribution.
//
// You should have received a copy of the GNU General Public License along with this software. 
// If not, see <https://www.gnu.org/licenses/>.
//
//========================================================================



#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include<unistd.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "./instanceInit.c"

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
	
	if (vkBeginCommandBuffer(components->commandBuffer, &beginInfo) != VK_SUCCESS) 
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

	vkCmdBeginRenderPass(components->commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(components->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, components->graphicsPipeline);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)(components->swapChainGroup.imageExtent.width);
	viewport.height = (float)(components->swapChainGroup.imageExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(components->commandBuffer, 0, 1, &viewport);
	
	VkRect2D scissor = {};
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = components->swapChainGroup.imageExtent;
	vkCmdSetScissor(components->commandBuffer, 0, 1, &scissor);

	vkCmdDraw(components->commandBuffer, 3, 1, 0, 0);

	if (vkEndCommandBuffer(components->commandBuffer) != VK_SUCCESS) {
	    printf("Failed to record command buffer!\n");
	}
}

void drawFrame(VulkanComponents* components, GLFWwindow* window) 
{
	vkWaitForFences(components->device, 1, &(components->inFlightFence), VK_TRUE, UINT64_MAX);
	
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(components->device, components->swapChainGroup.swapChain, UINT64_MAX, components->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || components->framebufferResized) 
	{
		printf("Recreating swap chain!\n");
	    recreateSwapChain(components, window);
	    return;
	} else if (result != VK_SUCCESS) 
	{
	    printf("Failed to acquire swap chain image!\n");
	}

	vkResetCommandBuffer(components->commandBuffer, 0);
	recordCommandBuffer(components, imageIndex);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = {components->imageAvailableSemaphore};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(components->commandBuffer);
	VkSemaphore signalSemaphores[] = {components->renderFinishedSemaphore};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	if (vkQueueSubmit(components->graphicsQueue, 1, &submitInfo, components->inFlightFence) != VK_SUCCESS) 
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
	vkResetFences(components->device, 1, &(components->inFlightFence));
	vkQueuePresentKHR(components->presentQueue, &presentInfo);

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

	components->graphicsPipeline = createGraphicsPipeline(components->device, components->swapChainGroup.imageExtent, components->pipelineLayout, components->renderPass);
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
