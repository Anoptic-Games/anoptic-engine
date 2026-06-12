/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>


bool allocateBuffer(VulkanComponents* components, VkBuffer buffer, VkMemoryPropertyFlags properties, VkDeviceMemory* bufferMemory)
{ // Can be used for sub-allocation scheme as-is
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(components->deviceQueueComp.device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(components, memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(components->deviceQueueComp.device, &allocInfo, NULL, bufferMemory) != VK_SUCCESS)
	{
		printf("Failed to allocate buffer memory!");
		return false;
	}

	vkBindBufferMemory(components->deviceQueueComp.device, buffer, *bufferMemory, 0);
	return true;
}

bool createDataBuffer(VulkanComponents* components, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, buffer) != VK_SUCCESS)
	{
		printf("Failed to create data buffer!");
		return false;
	}

	if (!allocateBuffer(components, *buffer, properties, bufferMemory))
	{
		// Clean up the created buffer before returning
		vkDestroyBuffer(components->deviceQueueComp.device, *buffer, NULL);
		return false;
	}

	return true;
}

bool createVertexBuffer(VulkanComponents* components, uint32_t vertexCount, VkBuffer* vertex, VkDeviceMemory* vertexMemory)
{ // Not core to init, mostly meant for actual rendering
	VkDeviceSize bufferSize = sizeof(Vertex) * vertexCount;

	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, properties, vertex, vertexMemory)) 
	{
		printf("Failed to create vertex buffer!");
		return false;
	}
	
	return true;
}

bool createIndexBuffer(VulkanComponents* components, uint32_t indexCount, VkBuffer* index, VkDeviceMemory* indexMemory)
{ // Ditto
	VkDeviceSize bufferSize = sizeof(uint16_t) * indexCount;
	

	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, properties, index, indexMemory)) 
	{
		printf("Failed to create index buffer!");
		return false;
	}
	
	return true;

}

bool createUniformBuffers(VulkanComponents* components)
{ // Central to init, specific to perspective uniforms (world translation, rotation and projection)
	VkDeviceSize bufferSize = sizeof(UniformComponents);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{

		if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			 &(components->renderComp.buffers.uniform[i]), &(components->renderComp.buffers.uniformMemory[i]))) 
		{
			printf("Failed to create uniform buffer!");
			return false;
		}
		printf("!!UBO!! UBO buffer: %p\n", components->renderComp.buffers.uniform[i]);

		vkMapMemory(components->deviceQueueComp.device, components->renderComp.buffers.uniformMemory[i], 0, bufferSize, 0, &(components->renderComp.buffers.uniformMapped[i]));
		printf("!!UBO!! Created mapped UBO buffer at: %p\n", components->renderComp.buffers.uniformMapped[i]);
	}

	return true;
}

bool createTransformBuffers(VulkanComponents* components)
{ // !TODO transition this for dynamic mesh instantiation
	VkDeviceSize bufferSize = sizeof(ModelTransforms);

	for (size_t i = 0; i < components->renderComp.buffers.entityCount; i++)
	{

		if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			 &(components->renderComp.buffers.entities[i].transform), &(components->renderComp.buffers.entities[i].transformMemory))) 
		{
			printf("Failed to create uniform buffer!");
			return false;
		}
		printf("!!MESHTRANSFORM!! Transform uniform buffer: %p\n", components->renderComp.buffers.entities[i].transform);

		vkMapMemory(components->deviceQueueComp.device, components->renderComp.buffers.entities[i].transformMemory, 0, bufferSize, 0, &(components->renderComp.buffers.entities[i].transformMapped));
		printf("!!MESHTRANSFORM!! Created mapped memory buffer at %p\n", components->renderComp.buffers.entities[i].transformMapped);
	}
	
	return true;
}

bool updateUniformBuffer(VulkanComponents* components)
{ // Changes the perspective (camera) parameters applied to the world-space for a given frame. Should be generalized with its parameters exposed via an interface
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	for(int i = 0; i < 4; i++)
		for(int j = 0; j < 4; j++)
			components->renderComp.uniform.model[i][j] = (i == j) ? 1.0f : 0.0f;

	rotateMatrix(components->renderComp.uniform.model, 'Y', angle);

	float eye[] = {0.0f, 0.9f, 3.5f};  // Move camera up and back
	float center[] = {0.0f, 0.15f, 0.0f}; // Camera looks at the origin
	float up[] = {0.0f, -1.0f, 0.0f};  // World is flipped // TODO: Maybe unflip the world

	lookAt(components->renderComp.uniform.view, eye, center, up);

	float fov = 45.0f; // Field of View in degrees
	float aspect = (float)components->swapChainComp.swapChainGroup.imageExtent.width / (float)components->swapChainComp.swapChainGroup.imageExtent.height;
	float near = 0.1f;
	float far = 100.0f;
	perspective(components->renderComp.uniform.proj, fov, aspect, near, far);
	
	//printf("!!UBO!! Mapped UBO buffer on value update: %p\n", components->renderComp.buffers.uniformMapped[components->syncComp.frameIndex]);
	memcpy(components->renderComp.buffers.uniformMapped[components->syncComp.frameIndex], &(components->renderComp.uniform), sizeof(components->renderComp.uniform));

	angle += ((float)(time-oldTime)) * 0.000000f;
	if(angle > 2.0f * pi)
	{
		angle = 0.0f;	
	}
	oldTime = time;

	return true;
}

bool updateMeshTransforms(VulkanComponents* components, EntityBuffer* entity, float move)
{
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	// Initialize ModelTransforms structure
	ModelTransforms meshTransforms;

	// Initialize translation, rotation, and scale matrices to identity
	for(int i = 0; i < 4; i++)
	{
		for(int j = 0; j < 4; j++)
		{
			meshTransforms.translation[i][j] = (i == j) ? 1.0f : 0.0f;
			meshTransforms.rotation[i][j] = (i == j) ? 1.0f : 0.0f;
			meshTransforms.scale[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	translate(meshTransforms.translation, move, 0.0f, 0.0f);

	// Apply rotation to the mesh's transform
	rotateMatrix(meshTransforms.rotation, 'Y', angle);

	// TODO: Apply translation and scaling if needed
	// Example: translateMatrix(meshTransforms.translation, x, y, z);
	// Example: scaleMatrix(meshTransforms.scale, scaleX, scaleY, scaleZ);

	// Map the buffer and copy the transformation data
	//printf("!!MESHTRANSFORM!! Mapped transform buffer on value update: %p\n", entity->transformMapped);
	memcpy(entity->transformMapped, &meshTransforms, sizeof(meshTransforms));

	// Update angle for next frame
	angle += ((float)(time - oldTime)) * 0.000001f;
	if (angle > 2.0f * pi)
	{
		angle = 0.0f;
	}
	oldTime = time;

	return true;
}

VkFormat findSupportedFormat(VulkanComponents* components, const VkFormat* candidates, uint32_t candidateCount, VkImageTiling tiling, VkFormatFeatureFlags features)
{ // Returns a device-supported format from a list of candidates, currently only used in pipeline images but should be used every time an image is created
	for (int i = 0; i < candidateCount; i++)
	{
		VkFormat format = candidates[i];
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(components->physicalDeviceComp.physicalDevice, format, &props);

		// TODO: Figure out if both cases are really necessary (currently identical results)
		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
		{
			return format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
		{
			return format;
		}
	}
	printf("Failed to find a suitable format!\n");
	return VK_FORMAT_UNDEFINED;
}


// !TODO move this to a bottom-level library
VkFormat findDepthFormat(VulkanComponents* components)
{
	VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
	return(findSupportedFormat(components, &candidates[0], sizeof(candidates)/sizeof(VkFormat),
								VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
}

bool hasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}
