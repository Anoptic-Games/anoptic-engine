/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/vulkanMaster.h"

#define GLFW_INCLUDE_VULKAN

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
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (components.syncComp.frameSubmitted[i])
		{
			vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[i]), VK_TRUE, UINT64_MAX);
			components.syncComp.frameSubmitted[i] = false; // reset the status
		}
    }

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

	VkClearValue clearValues[2] = {};
	clearValues[0].color.float32[0] = 0.0f;
	clearValues[0].color.float32[1] = 0.0f;
	clearValues[0].color.float32[2] = 0.0f;
	clearValues[0].color.float32[3] = 0.0f;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;
	renderPassInfo.clearValueCount = sizeof(clearValues) / sizeof(VkClearValue);
	renderPassInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass(components.cmdComp.commandBuffer[components.syncComp.frameIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Create loop for all extant pipelines once multiple ones are supported, loop through them then through the meshes they apply to
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
	int windowWidth, windowHeight;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = (VkExtent2D){(uint32_t)windowWidth, (uint32_t)windowHeight};
	vkCmdSetScissor(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0, 1, &scissor);

	// Loop through meshes of the current pipeline, bind the associated vertex and index buffers.
	VkBuffer* vertexBuffer;
	VkDeviceSize offsets[] = {0};

	vkCmdBindDescriptorSets(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
		components.renderComp.pipelineLayout, 0, 1, &(components.renderComp.descriptorSets[components.syncComp.frameIndex]), 0, NULL);

	for (uint32_t i = 0; i < components.renderComp.buffers.entityCount; i++) // Iterate through all render packages and issue indexed draw commands
	{
		vertexBuffer = &components.renderComp.buffers.entities[i].vertex;
		vkCmdBindVertexBuffers(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0, 1, vertexBuffer, offsets);
		vkCmdBindIndexBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex], components.renderComp.buffers.entities[i].index, 0, VK_INDEX_TYPE_UINT16);

		vkCmdBindDescriptorSets(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
			components.renderComp.pipelineLayout, 1, 1, &(components.renderComp.buffers.entities[i].meshDescriptorSet), 0, NULL);

		vkCmdDrawIndexed(components.cmdComp.commandBuffer[components.syncComp.frameIndex], components.renderComp.buffers.entities[i].indexCount, 1, 0, 0, 0);
	}
	

	vkCmdEndRenderPass(components.cmdComp.commandBuffer[components.syncComp.frameIndex]);

	if (vkEndCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex]) != VK_SUCCESS)
	{
		printf("Failed to record command buffer!\n");
	}
}

void clearSemaphores()
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(components.deviceQueueComp.device, components.syncComp.imageAvailableSemaphore[i], NULL);
		vkDestroySemaphore(components.deviceQueueComp.device, components.syncComp.renderFinishedSemaphore[i], NULL);

		if (vkCreateSemaphore(components.deviceQueueComp.device, &semaphoreInfo, NULL, &components.syncComp.imageAvailableSemaphore[i]) != VK_SUCCESS ||
			vkCreateSemaphore(components.deviceQueueComp.device, &semaphoreInfo, NULL, &components.syncComp.renderFinishedSemaphore[i]) != VK_SUCCESS)
		{
			printf("Failed to recreate semaphores!\n");
		}
	}
}

void printUniformTransferState()
{
	// Swap Chain Components
	printf("\n=== Swap Chain Components ===\n");
	printf("Image count: %d\n", components.swapChainComp.swapChainGroup.imageCount);
	printf("Image extent: width = %d, height = %d\n", components.swapChainComp.swapChainGroup.imageExtent.width, components.swapChainComp.swapChainGroup.imageExtent.height);
	
	// Buffer Components
	printf("\n=== Buffer Components ===\n");
	printf("Vertex buffer: %p\n", (void*)components.renderComp.buffers.entities[0].vertex);
	printf("Index buffer: %p\n", (void*)components.renderComp.buffers.entities[0].index);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Uniform buffer %d: %p\n", i, (void*)components.renderComp.buffers.uniform[i]);
		printf("Uniform memory %d: %p\n", i, (void*)components.renderComp.buffers.uniformMemory[i]);
		printf("Uniform buffer mapping %d: %p\n", i, components.renderComp.buffers.uniformMapped[i]);
	}

	// Synchronization Components
	printf("\n=== Synchronization Components ===\n");
	printf("Current frame index: %d\n", components.syncComp.frameIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Frame %d submitted: %d\n", i, components.syncComp.frameSubmitted[i]);
	}
	
	printf("\n======================================\n");
}

void drawFrame() 
{
    if (components.syncComp.frameSubmitted[components.syncComp.frameIndex] == true)
    {
        vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[components.syncComp.frameIndex]), VK_TRUE, UINT64_MAX);
    }
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.swapChain, UINT64_MAX, components.syncComp.imageAvailableSemaphore[components.syncComp.frameIndex], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || components.syncComp.framebufferResized) 
	{
		vkDeviceWaitIdle(components.deviceQueueComp.device);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (components.syncComp.frameSubmitted[i])
			{
				vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[i]), VK_TRUE, UINT64_MAX);
				components.syncComp.frameSubmitted[i] = false; // reset the status
			}
		}
		//printf("Recreating swap chain!\n");
		clearSemaphores();
        vkDeviceWaitIdle(components.deviceQueueComp.device);
		recreateSwapChain(&components, window);
		return;
	} else if (result != VK_SUCCESS) 
	{
		printf("Failed to acquire swap chain image!\n");
        vkDeviceWaitIdle(components.deviceQueueComp.device);
		recreateSwapChain(&components, window);
        return;
	}

	updateUniformBuffer(&components);
	updateMeshTransforms(&components, &components.renderComp.buffers.entities[0], 2.0f);
	updateMeshTransforms(&components, &components.renderComp.buffers.entities[1], -2.0f);
	updateMeshTransforms(&components, &components.renderComp.buffers.entities[2], 0.0f);

	vkResetCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0);
	recordCommandBuffer(imageIndex);

	//updateUniformBuffer(&components);
	
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

	VkResult presentResult = vkQueuePresentKHR(components.deviceQueueComp.presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		// Handle swap chain recreation
		vkDeviceWaitIdle(components.deviceQueueComp.device);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (components.syncComp.frameSubmitted[i])
			{
				vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[i]), VK_TRUE, UINT64_MAX);
				components.syncComp.frameSubmitted[i] = false; // reset the status
			}
		}
		clearSemaphores();
		recreateSwapChain(&components, window);
		return;
	} else if (presentResult != VK_SUCCESS)
	{
		printf("Failed to present swap chain image!\n");
		return;
	}

	components.syncComp.frameSubmitted[components.syncComp.frameIndex] = true;

	//printUniformTransferState();

	components.syncComp.frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (components.syncComp.frameIndex == MAX_FRAMES_IN_FLIGHT)
	{
		components.syncComp.frameIndex = 0;
	}
}

//Init and cleanup functions

bool initVulkan() // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{

	// Window initialization
	Dimensions2D initDimensions = {800, 600};
	setResolution(initDimensions);
	setMonitor(-1);
	setBorderless(0);

	vulkanGarbage.monitors = &monitors;
	cleanupMonitors(&monitors);
	printf("Here");
	enumerateMonitors(&monitors);

	window = initWindow(&components, &monitors);

	if (window == NULL)
	{
		// Handle error
		printf("Window initialization failed.\n");
		unInitVulkan();
		return 0;
	}

	requestPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);

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

	components.swapChainComp.swapChainGroup = initSwapChain(&components, window, getChosenPresentMode(), VK_NULL_HANDLE); // Initialize a swap chain
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

	if (!createCommandPool(components.deviceQueueComp.device, components.physicalDeviceComp.physicalDevice,
						   components.surface, &(components.cmdComp.commandPool)))
	{
		printf("Quitting init: command pool failure!\n");
		unInitVulkan();
		return false;
	}

	createColorResources(&components); // Make this a bool and add check

	if(!createDepthResources(&components))
	{
		printf("Quitting init: depth resource creation failure!\n");
	}

	if (!createRenderPass(&components, components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.imageFormat,
						  &(components.renderComp.renderPass)))
	{
		printf("Quitting init: render pass failure\n");
		unInitVulkan();
		return false;
	}

	components.renderComp.graphicsPipeline = createGraphicsPipeline(&components);
	if (components.renderComp.graphicsPipeline == NULL)
	{
		printf("Quitting init: pipeline failure!\n");
		unInitVulkan();
		return false;
	}
	printf("Framebuffers\n");

	if (!createFramebuffers(&components))
	{
		printf("Quitting init: framebuffer failure!\n");
		unInitVulkan();
		return false;	
	}

	/*if(!createTextureImage(&components, &components.renderComp.buffers.entities[0], "texture.jpg", false))
	{
		printf("Quitting init: texture read failure!\n");
		unInitVulkan();
		return false;
	}

	if(!createTextureImageView(&components, &components.renderComp.buffers.entities[0]))
	{
		printf("Quitting init: texture image view failure!\n");
		unInitVulkan();
		return false;
	}*/

	if(!createTextureSampler(&components))
	{
		printf("Quitting init: texture sampler failure!\n");
		unInitVulkan();
		return false;
	}

	components.renderComp.buffers.entityCount = 1;

	if(!parseGltf(&components, "viking_room.gltf"))
	{
		printf("Failed to parse glTF file!\n");
		unInitVulkan();
		return false;
	}
	
	/*if (!createVertexBuffer(&components, vertices, 8, &components.renderComp.buffers.entities[0]))
	{
		printf("Quitting init: vertex buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	// Fill the vertex buffer
	if (!stagingTransfer(&components, vertices, components.renderComp.buffers.entities[0].vertex, sizeof(vertices)))
	{
		printf("Quitting init: staging buffer population failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createIndexBuffer(&components, vertexIndices, 12, &components.renderComp.buffers.entities[0]))
	{
		printf("Quitting init: vertex buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!stagingTransfer(&components, vertexIndices, components.renderComp.buffers.entities[0].index, sizeof(vertexIndices)))
	{
		printf("Quitting init: staging buffer population failure!\n");
		unInitVulkan();
		return false;
	}*/

	if (!createUniformBuffers(&components))
	{
		printf("Quitting init: uniform buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createTransformBuffers(&components))
	{
		printf("Quitting init: transform buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	// HERE
	if (!createDescriptorPool(&components))
	{
		printf("Quitting init: UBO descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createMeshDescriptorPool(&components))
	{
		printf("Quitting init: mesh descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createDescriptorSets(&components))
	{
		printf("Quitting init: UBO descriptor sets creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createMeshDescriptorSets(&components))
	{
		printf("Quitting init: mesh descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}

	updateUboDescriptorSets(&components);
	updateMeshDescriptorSets(&components);

	if (!createCommandBuffer(&components))
	{
		printf("Quitting init: command buffer failure!\n");
		unInitVulkan();
		return false;
	}
	

	if (!createSyncObjects(&components))
	{
		printf("Quitting init: sync failure!\n");
		unInitVulkan();
		return false;
	}

	printf("Instance creation complete!\n");

	return true;
}

void drawChar(FT_ULong glyph_number)
{
	Texture8 texture = {};
    FT_Bitmap* bitmap = ft_get_glyph_bitmap(glyph_number);
    texture.mipLevels = 0;
    texture.texChannels = 1;
    texture.pixels = bitmap->buffer;
    texture.texWidth = bitmap->width;
    texture.texHeight = bitmap->rows;
	GlyphTexture glyphTexture = {};

    createTextureImageFromCPUMemory(&components, &(glyphTexture.textureImage), &(glyphTexture.textureImageMemory), &(glyphTexture.textureImageView), texture, VK_FORMAT_R8_SRGB, false);
}
