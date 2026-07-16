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



bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool)
{ // Central init component
	struct QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, &surface);
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	
	if (vkCreateCommandPool(device, &poolInfo, NULL, commandPool) != VK_SUCCESS) 
	{
		ano_log(ANO_FATAL, "Failed to create command pool!");
		return false;
	}
	return true;
}

bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, buffer) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to create data buffer!");
		return false;
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(ctx->device, *buffer, &memRequirements);

	*allocation = gpu_alloc(allocator, memRequirements, properties);
	if (allocation->memory == VK_NULL_HANDLE) {
		vkDestroyBuffer(ctx->device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE; // avoid dangling handle
		return false;
	}
	vkBindBufferMemory(ctx->device, *buffer, allocation->memory, allocation->offset);

	return true;
}



bool createUniformBuffers(VulkanContext* ctx, RendererState* state)
{ // Central to init, perspective uniforms
	VkDeviceSize bufferSize = sizeof(GlobalUBO);

	// One camera UBO per view per frame.
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			GpuAllocation alloc;
			if (!createDataBuffer(ctx, &gpuAllocator, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &(rendererState.frames[i].views[v].uniformBuffer), &alloc))
			{
				ano_log(ANO_FATAL, "Failed to create uniform buffer!");
				return false;
			}

			rendererState.frames[i].views[v].uniformMapped = alloc.mapped;
		}
	}

	return true;
}

void printMatrix(float mat[4][4])
{ // Debug matrix dump
	ano_debug_log(ANO_INFO, "Matrix:");
	for (int i = 0; i < 4; i++)
	{
		ano_debug_log(ANO_INFO, "%f %f %f %f", mat[i][0], mat[i][1], mat[i][2], mat[i][3]);
	}
}

bool updateMeshTransforms(VulkanContext* ctx, RenderEntity* entity, float move)
{
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	// Identity transform
	for(int i = 0; i < 4; i++)
	{
		for(int j = 0; j < 4; j++)
		{
			entity->transform[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	translate(entity->transform, move, 0.0f, 0.0f);
	rotateMatrix(entity->transform, 'Y', angle);

	// Update angle for next frame
	angle += ((float)(time - oldTime)) * 0.000001f;
	if (angle > 2.0f * pi)
	{
		angle = 0.0f;
	}
	oldTime = time;

	return true;
}




/*bool createDescriptorPool(VulkanContext* ctx)
{ // Central to init
	VkDescriptorPoolSize poolSizes[2] = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(VkDescriptorPoolSize));
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &(ctx->renderComp.descriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}*/





uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{ // Central to init, also used externally post-init
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	
	ano_log(ANO_ERROR, "Failed to find suitable memory type!");
	return UINT32_MAX;
}


bool stagingTransfer(VulkanContext* ctx, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize)
{ // Not central to init
	VkBuffer stagingBuffer;
	GpuAllocation stagingAlloc;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (!createDataBuffer(ctx, &stagingAllocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, properties, &stagingBuffer, &stagingAlloc)) 
	{
		ano_log(ANO_ERROR, "Failed to create staging buffer!");
		return false;
	}

	// Map and copy
	void* mappedMemory = stagingAlloc.mapped;
	memcpy(mappedMemory, data, bufferSize);

	// Copy to destination
	if (!copyBuffer(ctx, stagingBuffer, dstBuffer, bufferSize))
	{
		ano_log(ANO_ERROR, "Failed to copy buffers!");
		return false;
	}

	// Cleanup staging buffer
	vkDestroyBuffer(ctx->device, stagingBuffer, NULL);

	return true;
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx)
{ // Used in init, also external
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = rendererState.commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(ctx->device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer)
{ // Used in init, also external
	vkEndCommandBuffer(commandBuffer);

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	vkCreateFence(ctx->device, &fenceInfo, NULL, &fence);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, fence);
	vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
	vkDestroyFence(ctx->device, fence, NULL);

	vkFreeCommandBuffers(ctx->device, rendererState.commandPool, 1, &commandBuffer);
}


bool copyBuffer(VulkanContext* ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{ // Used in init, also external
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(ctx);
	
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(ctx, commandBuffer);

	return true;
}

bool createCommandBuffer(VulkanContext* ctx, RendererState* state)
{ // Central to init
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = rendererState.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	for (uint32_t i =0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkAllocateCommandBuffers(ctx->device, &allocInfo, &(rendererState.frames[i].commandBuffer)) != VK_SUCCESS)
		{
			ano_log(ANO_FATAL, "Failed to allocate command buffers!");
			return false;
		}
		// Async light-cull: uploads + shared compute prelude in their own CB, submitted ahead of main.
		if (state->asyncLc
			&& vkAllocateCommandBuffers(ctx->device, &allocInfo, &(rendererState.frames[i].preludeCommandBuffer)) != VK_SUCCESS)
		{
			ano_log(ANO_FATAL, "Failed to allocate prelude command buffers!");
			return false;
		}
	}

	return true;
}

bool createSyncObjects(VulkanContext* ctx, RendererState* state) 
{ // Central to init
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;  

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		if (vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &(rendererState.frames[i].imageAvailable)) != VK_SUCCESS ||
			vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &(rendererState.frames[i].renderFinished)) != VK_SUCCESS ||
			vkCreateFence(ctx->device, &fenceInfo, NULL, &(rendererState.frames[i].frameFence)) != VK_SUCCESS)
			{
			ano_log(ANO_FATAL, "Failed to create semaphores!");
			return false;
		}
	}

	// Async Hi-Z timelines: gfxTimeline counts graphics submits, hizTimeline counts builds.
	if (state->asyncHiz)
	{
		VkSemaphoreTypeCreateInfo timelineInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0 };
		VkSemaphoreCreateInfo timelineSem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineInfo };
		if (vkCreateSemaphore(ctx->device, &timelineSem, NULL, &state->gfxTimeline) != VK_SUCCESS ||
			vkCreateSemaphore(ctx->device, &timelineSem, NULL, &state->hizTimeline) != VK_SUCCESS)
		{
			ano_log(ANO_FATAL, "Failed to create async Hi-Z timeline semaphores!");
			return false;
		}
		// Async light-cull: preludeTimeline counts prelude submits, lcTimeline counts light-culls.
		if (state->asyncLc &&
			(vkCreateSemaphore(ctx->device, &timelineSem, NULL, &state->preludeTimeline) != VK_SUCCESS ||
			 vkCreateSemaphore(ctx->device, &timelineSem, NULL, &state->lcTimeline) != VK_SUCCESS))
		{
			ano_log(ANO_FATAL, "Failed to create async light-cull timeline semaphores!");
			return false;
		}
	}

	// GPU timestamp profiling. One ANO_TS_COUNT query pool per frame in flight.
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(ctx->physicalDevice, &props);
		state->timestampPeriodNs = props.limits.timestampPeriod;

		uint32_t qfCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qfCount, NULL);
		VkQueueFamilyProperties qfProps[qfCount];
		vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qfCount, qfProps);
		uint32_t gf = ctx->queueFamilyIndices.graphicsFamily;
		state->timestampValidBits = (gf < qfCount) ? qfProps[gf].timestampValidBits : 0u;
		if (state->timestampPeriodNs <= 0.0f) state->timestampValidBits = 0u;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			state->frames[i].timestampPool = VK_NULL_HANDLE;
			if (state->timestampValidBits) {
				VkQueryPoolCreateInfo qpi = {};
				qpi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
				qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
				qpi.queryCount = ANO_TS_COUNT;
				if (vkCreateQueryPool(ctx->device, &qpi, NULL, &state->frames[i].timestampPool) != VK_SUCCESS) {
					ano_log(ANO_WARN, "Failed to create timestamp query pool; profiling disabled.");
					state->timestampValidBits = 0u; // disable profiling, keep rendering
				}
			}
		}
	}

	// Picking readback buffers. One host-visible|coherent buffer per frame in flight, persistently mapped.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		if (!createDataBuffer(ctx, &gpuAllocator, sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&state->frames[i].pickReadback, &state->frames[i].pickReadbackAlloc)) {
			ano_log(ANO_FATAL, "Failed to create picking readback buffer!");
			return false;
		}
		state->frames[i].pickReadbackMapped = state->frames[i].pickReadbackAlloc.mapped;
		*state->frames[i].pickReadbackMapped = 0xFFFFFFFFu;
	}
	state->lastPickRenderId = ANO_RENDER_NO_PICK;

	return true;
}

