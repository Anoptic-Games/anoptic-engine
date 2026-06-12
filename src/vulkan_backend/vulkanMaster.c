/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/gpu_alloc.h"

#define GLFW_INCLUDE_VULKAN

// Variables

static VulkanComponents components;
RendererState rendererState;
GpuAllocator gpuAllocator;
GpuAllocator swapchainAllocator;

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

	// Transition swapchain image to color attachment optimal
	VkImageMemoryBarrier swapChainBarrier = {};
	swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapChainBarrier.image = components.swapChainComp.swapChainGroup.images[imageIndex];
	swapChainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	swapChainBarrier.subresourceRange.baseMipLevel = 0;
	swapChainBarrier.subresourceRange.levelCount = 1;
	swapChainBarrier.subresourceRange.baseArrayLayer = 0;
	swapChainBarrier.subresourceRange.layerCount = 1;
	swapChainBarrier.srcAccessMask = 0;
	swapChainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	vkCmdPipelineBarrier(
		components.cmdComp.commandBuffer[components.syncComp.frameIndex],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
	VkClearValue clearDepth = {};
	clearDepth.depthStencil.depth = 1.0f;
	clearDepth.depthStencil.stencil = 0;

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = components.swapChainComp.viewGroup.colorView; // MSAA color
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
	colorAttachment.resolveImageView = components.swapChainComp.viewGroup.views[imageIndex];
	colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue = clearColor;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = components.renderComp.buffers.depthView[components.syncComp.frameIndex];
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.clearValue = clearDepth;

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
	renderingInfo.renderArea.extent = components.swapChainComp.swapChainGroup.imageExtent;
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;
	renderingInfo.pStencilAttachment = NULL;

	vkCmdBeginRendering(components.cmdComp.commandBuffer[components.syncComp.frameIndex], &renderingInfo);

	// Create loop for all extant pipelines once multiple ones are supported, loop through them then through the meshes they apply to
	vkCmdBindPipeline(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.prototypes[PIPELINE_FLAT].implementations[0].pipeline);

    // Should probably only do this if the viewport's actually changed
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

	// Bind monolithic vertex and index buffers once per frame
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(components.cmdComp.commandBuffer[components.syncComp.frameIndex], 0, 1, &rendererState.globalGeometryPool.vertexBuffer, offsets);
	vkCmdBindIndexBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex], rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
		rendererState.prototypes[PIPELINE_FLAT].layout, 0, 1, &(rendererState.globalSets[components.syncComp.frameIndex]), 0, NULL);

	uint32_t entityCount = components.renderComp.buffers.entityCount;
	if (entityCount > 0) {
		vkCmdBindDescriptorSets(components.cmdComp.commandBuffer[components.syncComp.frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
			rendererState.prototypes[PIPELINE_FLAT].layout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);

		uint32_t baseOffset = 0; // base offset is 0 because firstInstance inherently handles the offset
		vkCmdPushConstants(components.cmdComp.commandBuffer[components.syncComp.frameIndex], rendererState.prototypes[PIPELINE_FLAT].layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &baseOffset);

		vkCmdDrawIndexedIndirect(
			components.cmdComp.commandBuffer[components.syncComp.frameIndex],
			rendererState.indirectBuffer.buffer[components.syncComp.frameIndex],
			0,
			entityCount,
			sizeof(VkDrawIndexedIndirectCommand));
	}
	
	vkCmdEndRendering(components.cmdComp.commandBuffer[components.syncComp.frameIndex]);

	// Transition swapchain image to present
	swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	swapChainBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	swapChainBarrier.dstAccessMask = 0;

	vkCmdPipelineBarrier(
		components.cmdComp.commandBuffer[components.syncComp.frameIndex],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

	if (vkEndCommandBuffer(components.cmdComp.commandBuffer[components.syncComp.frameIndex]) != VK_SUCCESS)
	{
		printf("Failed to record command buffer!\n");
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
	printf("Mesh index: %u\n", components.renderComp.buffers.entities[0].meshIndex);
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

void updateTransformBuffer(VulkanComponents* components, RendererState* state, uint32_t frameIndex)
{
	uint32_t entityCount = components->renderComp.buffers.entityCount;
	RenderEntity* entities = components->renderComp.buffers.entities;
	mat4* transforms = state->transformBuffer.mapped[frameIndex];

	for (uint32_t i = 0; i < entityCount; i++) {
		memcpy(&transforms[i], &entities[i].transform, sizeof(mat4));
	}
	state->transformBuffer.count = entityCount;
}

void buildIndirectCommands(VulkanComponents* components, RendererState* state, uint32_t frameIndex)
{
	VkDrawIndexedIndirectCommand* cmds = state->indirectBuffer.mapped[frameIndex];
	uint32_t entityCount = components->renderComp.buffers.entityCount;
	RenderEntity* entities = components->renderComp.buffers.entities;
	uint32_t drawCount = 0;

	for (uint32_t i = 0; i < entityCount; i++) {
		MeshRegion* mesh = &state->globalGeometryPool.meshes[entities[i].meshIndex];

		cmds[drawCount] = (VkDrawIndexedIndirectCommand){
			.indexCount    = mesh->indexCount,
			.instanceCount = 1,
			.firstIndex    = mesh->indexOffset / sizeof(uint16_t),
			.vertexOffset  = mesh->baseVertex,
			.firstInstance = i,
		};
		drawCount++;
	}

	state->indirectBuffer.drawCount[frameIndex] = drawCount;
}

void drawFrame() 
{
	if (components.syncComp.framebufferResized)
	{
		components.syncComp.framebufferResized = false;
		recreateSwapChain(&components, window);
		return;
	}

    if (components.syncComp.frameSubmitted[components.syncComp.frameIndex] == true)
    {
        vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[components.syncComp.frameIndex]), VK_TRUE, UINT64_MAX);
    }
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(components.deviceQueueComp.device, components.swapChainComp.swapChainGroup.swapChain, UINT64_MAX, components.syncComp.imageAvailableSemaphore[components.syncComp.frameIndex], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) 
	{
		recreateSwapChain(&components, window);
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) 
	{
		printf("Failed to acquire swap chain image!\n");
		return;
	}

	updateUniformBuffer(&components);

	// Update entity transforms
	float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
	for (uint32_t i = 0; i < components.renderComp.buffers.entityCount && i < 3; i++) {
		updateMeshTransforms(&components, &components.renderComp.buffers.entities[i], moveOffsets[i]);
	}

	updateTransformBuffer(&components, &rendererState, components.syncComp.frameIndex);
	buildIndirectCommands(&components, &rendererState, components.syncComp.frameIndex);

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

    // Presentation should happen *before* submitting commands for a new frame, so we're actually taking advantage of buffering

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

void createMaterialBuffer(VulkanComponents* components, RendererState* state, uint32_t maxEntities) {
    state->materialBuffer.capacity = maxEntities;
    state->materialBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(MaterialData) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, &state->materialBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create material buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->materialBuffer.buffer[i], &memRequirements);
        
        state->materialBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->materialBuffer.buffer[i], state->materialBuffer.allocs[i].memory, state->materialBuffer.allocs[i].offset);
        
        state->materialBuffer.mapped[i] = (MaterialData*)state->materialBuffer.allocs[i].mapped;
    }
}

void createTransformBuffer(VulkanComponents* components, RendererState* state, uint32_t maxEntities) {
    state->transformBuffer.capacity = maxEntities;
    state->transformBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(mat4) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, &state->transformBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create transform buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->transformBuffer.buffer[i], &memRequirements);
        
        state->transformBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->transformBuffer.buffer[i], state->transformBuffer.allocs[i].memory, state->transformBuffer.allocs[i].offset);
        
        state->transformBuffer.mapped[i] = (mat4*)state->transformBuffer.allocs[i].mapped;
    }
}

void createIndirectDrawBuffer(VulkanComponents* components, RendererState* state, uint32_t maxDraws) {
    state->indirectBuffer.capacity = maxDraws;
    
    VkDeviceSize bufferSize = sizeof(VkDrawIndexedIndirectCommand) * maxDraws;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->indirectBuffer.drawCount[i] = 0;
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, &state->indirectBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create indirect draw buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->indirectBuffer.buffer[i], &memRequirements);
        
        state->indirectBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->indirectBuffer.buffer[i], state->indirectBuffer.allocs[i].memory, state->indirectBuffer.allocs[i].offset);
        
        state->indirectBuffer.mapped[i] = (VkDrawIndexedIndirectCommand*)state->indirectBuffer.allocs[i].mapped;
    }
}

bool initVulkan() // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{

	// Window initialization
	Dimensions2D initDimensions = {800, 600};
	setResolution(initDimensions);
	setMonitor(-1);
	setBorderless(0);

    vulkanGarbage.monitors = &monitors;
    cleanupMonitors(&monitors);
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
	

	if (createLogicalDevice(components.physicalDeviceComp.physicalDevice, &(components.deviceQueueComp.device), &(components.deviceQueueComp.graphicsQueue), &(components.deviceQueueComp.computeQueue), &(components.deviceQueueComp.transferQueue), &(components.deviceQueueComp.presentQueue), &(components.physicalDeviceComp.queueFamilyIndices)) != VK_SUCCESS)
	{
		fprintf(stderr, "Quitting init: logical device failure!\n");
		unInitVulkan();
		return false;
	}

	gpuAllocator.device = components.deviceQueueComp.device;
	vkGetPhysicalDeviceMemoryProperties(components.physicalDeviceComp.physicalDevice, &gpuAllocator.memProps);
	gpuAllocator.blocks = NULL;
	gpuAllocator.blockCount = 0;

	swapchainAllocator.device = components.deviceQueueComp.device;
	vkGetPhysicalDeviceMemoryProperties(components.physicalDeviceComp.physicalDevice, &swapchainAllocator.memProps);
	swapchainAllocator.blocks = NULL;
	swapchainAllocator.blockCount = 0;

	ano_vk_init_geometry_pool(&rendererState.globalGeometryPool, &gpuAllocator, components.deviceQueueComp.device);



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



	if (!ano_vk_init_global_layout(&components, &rendererState))
	{
		printf("Quitting init: global layout failure!\n");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_material_layouts(&components, &rendererState))
	{
		printf("Quitting init: material layouts failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createBindlessTextureArray(&components, &rendererState))
	{
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_pipelines(&components, &rendererState))
	{
		printf("Quitting init: pipeline failure!\n");
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

	// In a real application, maxEntities would be dynamic or configured.
	uint32_t maxEntities = 1000;
	createTransformBuffer(&components, &rendererState, maxEntities);
	createMaterialBuffer(&components, &rendererState, maxEntities);
	createIndirectDrawBuffer(&components, &rendererState, maxEntities);

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



	// HERE
	if (!createDescriptorPool(&components, &rendererState))
	{
		printf("Quitting init: UBO descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}


	if (!createDescriptorSets(&components, &rendererState))
	{
		printf("Quitting init: UBO descriptor sets creation failure!\n");
		unInitVulkan();
		return false;
	}


	updateUboDescriptorSets(&components, &rendererState);


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

