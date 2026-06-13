/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <vulkan/vulkan.h>
#include <mimalloc.h>
#include <mimalloc-override.h>

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

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle)
{
    uint32_t frameIdx = state->frameIndex;
    DeletionQueue* q = &state->deletionQueues[frameIdx];

    if (q->count >= q->capacity) {
        q->capacity = q->capacity == 0 ? 64 : q->capacity * 2;
        q->tasks = realloc(q->tasks, q->capacity * sizeof(DeletionTask));
    }

    q->tasks[q->count++] = (DeletionTask){ .type = type, .handle = handle };
}

void flush_deletion_queue(VulkanComponents* components, RendererState* state, uint32_t frameIndex)
{
    DeletionQueue* q = &state->deletionQueues[frameIndex];

    for (uint32_t i = 0; i < q->count; i++) {
        DeletionTask task = q->tasks[i];
        switch (task.type) {
            case RESOURCE_TYPE_GEOMETRY_MESH:
                geometry_pool_free(&state->globalGeometryPool, task.handle);
                break;
            case RESOURCE_TYPE_BINDLESS_TEXTURE:
                // For now bindless_register_texture handles index. 
                // We'd add a bindless_free_texture(components, &state->bindlessTextures, task.handle) eventually
                break;
        }
    }
    q->count = 0; // Clear queue for next time we hit this frame
}

// Graphics operations

static const RenderPassDef g_framePasses[] = {
    // 1. GPU culling
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_CULL,
        .dispatchX  = 0,  // computed from entityCount at runtime
    },
    // 2. Opaque geometry
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
};

void recordCommandBuffer(uint32_t imageIndex) 
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0; // Optional
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	VkCommandBuffer cmd = components.cmdComp.commandBuffer[components.syncComp.frameIndex];

	if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) 
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
		cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

    uint32_t entityCount = components.renderComp.buffers.entityCount;

    for (int p = 0; p < sizeof(g_framePasses)/sizeof(g_framePasses[0]); p++) {
        const RenderPassDef* pass = &g_framePasses[p];

        if (pass->type == PASS_COMPUTE) {
            if (entityCount > 0) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.prototypes[pass->prototype].implementations[0].pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    rendererState.prototypes[pass->prototype].layout, 0, 1, &(rendererState.cullSets[components.syncComp.frameIndex]), 0, NULL);
                
                // If dispatchX is 0, we compute it from entityCount
                uint32_t dispatchX = pass->dispatchX == 0 ? (entityCount + 255) / 256 : pass->dispatchX;
                vkCmdDispatch(cmd, dispatchX, 1, 1);

                VkMemoryBarrier memoryBarrier = {};
                memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

                vkCmdPipelineBarrier(
                    cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                    0,
                    1, &memoryBarrier,
                    0, NULL,
                    0, NULL
                );
            }
        } else if (pass->type == PASS_GRAPHICS) {
            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            VkClearValue clearDepth = {};
            clearDepth.depthStencil.depth = 1.0f;
            clearDepth.depthStencil.stencil = 0;

            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = components.swapChainComp.viewGroup.colorView; // MSAA color
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.resolveMode = pass->resolveMode;
            colorAttachment.resolveImageView = components.swapChainComp.viewGroup.views[imageIndex];
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = pass->colorLoadOp;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = clearColor;

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = components.renderComp.buffers.depthView[components.syncComp.frameIndex];
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachment.loadOp = pass->depthLoadOp;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue = clearDepth;

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
            renderingInfo.renderArea.extent = components.swapChainComp.swapChainGroup.imageExtent;
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = pass->colorAttachmentCount;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;
            renderingInfo.pStencilAttachment = NULL;

            vkCmdBeginRendering(cmd, &renderingInfo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.prototypes[pass->prototype].implementations[pass->implementationIndex].pipeline);

            VkViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)(components.swapChainComp.swapChainGroup.imageExtent.width);
            viewport.height = (float)(components.swapChainComp.swapChainGroup.imageExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            
            VkRect2D scissor = {};
            int windowWidth, windowHeight;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            scissor.offset = (VkOffset2D){0, 0};
            scissor.extent = (VkExtent2D){(uint32_t)windowWidth, (uint32_t)windowHeight};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Bind monolithic vertex and index buffers once per frame
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, &rendererState.globalGeometryPool.vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &(rendererState.globalSets[components.syncComp.frameIndex]), 0, NULL);

            if (entityCount > 0) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    rendererState.prototypes[pass->prototype].layout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);

                uint32_t baseOffset = 0; // base offset is 0 because firstInstance inherently handles the offset
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &baseOffset);

                vkCmdDrawIndexedIndirectCount(
                    cmd,
                    rendererState.indirectBuffer.buffer[components.syncComp.frameIndex],
                    0,
                    rendererState.drawCountBuffer[components.syncComp.frameIndex],
                    0,
                    rendererState.indirectBuffer.capacity,
                    sizeof(VkDrawIndexedIndirectCommand));
            }
            
            vkCmdEndRendering(cmd);
        }
    }

	// Transition swapchain image to present
	swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	swapChainBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	swapChainBarrier.dstAccessMask = 0;

	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &swapChainBarrier
	);

	if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
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
		printf("Uniform alloc %d: %p\n", i, (void*)components.renderComp.buffers.uniformAlloc[i].memory);
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

void updateCullingBuffers(VulkanComponents* components, RendererState* state, uint32_t frameIndex)
{
    uint32_t entityCount = components->renderComp.buffers.entityCount;
    RenderEntity* entities = components->renderComp.buffers.entities;

    // Reset draw count
    *state->drawCountMapped[frameIndex] = 0;

    // Update CullUBO
    CullUBO* ubo = state->cullUboBuffer.mapped[frameIndex];
    GlobalUBO* globalUbo = components->renderComp.buffers.uniformMapped[frameIndex];
    
    // Calculate viewProj matrix
    multiplyMat4(ubo->viewProj, globalUbo->proj, globalUbo->view);
    
    // Extract frustum planes
    extractFrustumPlanes(ubo->frustumPlanes, ubo->viewProj);

    ubo->entityCount = entityCount;

    // Update EntitySSBO
    uint32_t* entityBuffer = (uint32_t*)state->entityMapped[frameIndex];
    for(uint32_t i=0; i < entityCount; i++) {
        entityBuffer[i*2] = entities[i].meshIndex;
        entityBuffer[i*2+1] = entities[i].materialIndex;
    }

    // Update MeshSSBO and MeshBoundsSSBO
    uint32_t meshCount = state->globalGeometryPool.meshCount;
    uint32_t* meshData = (uint32_t*)state->meshDataMapped[frameIndex];
    float* meshBounds = (float*)state->meshBoundsMapped[frameIndex];
    
    for(uint32_t i=0; i < meshCount; i++) {
        MeshRegion* mesh = &state->globalGeometryPool.meshes[i];
        
        meshData[i*4 + 0] = mesh->indexCount;
        meshData[i*4 + 1] = mesh->indexOffset / sizeof(uint16_t);
        meshData[i*4 + 2] = mesh->baseVertex;
        meshData[i*4 + 3] = 0; // padding

        meshBounds[i*4 + 0] = mesh->boundingSphereCenter[0];
        meshBounds[i*4 + 1] = mesh->boundingSphereCenter[1];
        meshBounds[i*4 + 2] = mesh->boundingSphereCenter[2];
        meshBounds[i*4 + 3] = mesh->boundingSphereRadius;
    }
}

#include "anoptic_time.h"

void testAssetUnloadReload(VulkanComponents* comps, RendererState* state) {
    static uint64_t lastTime = 0;
    static int phase = 0;
    static uint32_t originalMeshIndex = 0;

    uint64_t now = ano_timestamp_us();
    if (lastTime == 0) lastTime = now;

    if (now - lastTime > 1000000) { // 1 second
        lastTime = now;
        
        if (phase == 0) {
            printf("--- TEST: Unloading original mesh ---\n");
            // Save original and set fallback
            originalMeshIndex = comps->renderComp.buffers.entities[0].meshIndex;
            comps->renderComp.buffers.entities[0].meshIndex = FALLBACK_MESH_INDEX;
            comps->renderComp.buffers.entities[0].materialIndex = 0; // Use fallback material if possible

            // Defer deletion
            deferred_delete_resource(state, RESOURCE_TYPE_GEOMETRY_MESH, originalMeshIndex);
            
            phase = 1;
        } else if (phase == 1) {
            printf("--- TEST: Uploading new mesh to reused memory ---\n");
            
            // Upload a simple triangle
            const Vertex triVertices[] = {
                {{ 0.0f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
                {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
            };
            const uint16_t triIndices[] = { 0, 1, 2 };

            uint32_t newMeshIdx = geometry_pool_upload(&state->globalGeometryPool, &gpuAllocator,
                                                       comps->deviceQueueComp.device,
                                                       comps->cmdComp.commandPool,
                                                       comps->deviceQueueComp.transferQueue,
                                                       triVertices, 3, triIndices, 3);
            
            printf("--- TEST: New mesh assigned index %u (Expected %u) ---\n", newMeshIdx, originalMeshIndex);
            
            // Assign to entity
            comps->renderComp.buffers.entities[0].meshIndex = newMeshIdx;
            
            phase = 2; // Done
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
}

void drawFrame() 
{
	if (components.syncComp.framebufferResized)
	{
		components.syncComp.framebufferResized = false;
		recreateSwapChain(&components, window);
		return;
	}

    testAssetUnloadReload(&components, &rendererState);

    if (components.syncComp.frameSubmitted[components.syncComp.frameIndex] == true)
    {
        vkWaitForFences(components.deviceQueueComp.device, 1, &(components.syncComp.inFlightFence[components.syncComp.frameIndex]), VK_TRUE, UINT64_MAX);
    }

    // Process any deferred deletions that were waiting for this frame's previous commands to finish
    flush_deletion_queue(&components, &rendererState, components.syncComp.frameIndex);
    
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
	updateCullingBuffers(&components, &rendererState, components.syncComp.frameIndex);

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
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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

void createCullingBuffers(VulkanComponents* components, RendererState* state, uint32_t maxEntities) {
    state->entityCount = maxEntities;
    uint32_t maxMeshes = 1024;
    
    VkDeviceSize entityBufferSize = sizeof(uint32_t) * 2 * maxEntities; // meshIndex, materialIndex
    VkDeviceSize meshDataSize = sizeof(uint32_t) * 4 * maxMeshes; // uvec4
    VkDeviceSize meshBoundsSize = sizeof(float) * 4 * maxMeshes; // vec4
    VkDeviceSize drawCountSize = sizeof(uint32_t);
    VkDeviceSize uboSize = sizeof(CullUBO);
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Entity Buffer
        VkBufferCreateInfo entityInfo = {};
        entityInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        entityInfo.size = entityBufferSize;
        entityInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        entityInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(components->deviceQueueComp.device, &entityInfo, NULL, &state->entityBuffer[i]);
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->entityBuffer[i], &memReqs);
        state->entityAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->entityBuffer[i], state->entityAllocs[i].memory, state->entityAllocs[i].offset);
        state->entityMapped[i] = state->entityAllocs[i].mapped;

        // Mesh Data Buffer
        VkBufferCreateInfo meshInfo = {};
        meshInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        meshInfo.size = meshDataSize;
        meshInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meshInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(components->deviceQueueComp.device, &meshInfo, NULL, &state->meshDataBuffer[i]);
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->meshDataBuffer[i], &memReqs);
        state->meshDataAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->meshDataBuffer[i], state->meshDataAllocs[i].memory, state->meshDataAllocs[i].offset);
        state->meshDataMapped[i] = state->meshDataAllocs[i].mapped;

        // Mesh Bounds Buffer
        VkBufferCreateInfo boundsInfo = {};
        boundsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        boundsInfo.size = meshBoundsSize;
        boundsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        boundsInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(components->deviceQueueComp.device, &boundsInfo, NULL, &state->meshBoundsBuffer[i]);
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->meshBoundsBuffer[i], &memReqs);
        state->meshBoundsAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->meshBoundsBuffer[i], state->meshBoundsAllocs[i].memory, state->meshBoundsAllocs[i].offset);
        state->meshBoundsMapped[i] = state->meshBoundsAllocs[i].mapped;

        // Draw Count Buffer
        VkBufferCreateInfo countInfo = {};
        countInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        countInfo.size = drawCountSize;
        countInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        countInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(components->deviceQueueComp.device, &countInfo, NULL, &state->drawCountBuffer[i]);
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->drawCountBuffer[i], &memReqs);
        state->drawCountAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->drawCountBuffer[i], state->drawCountAllocs[i].memory, state->drawCountAllocs[i].offset);
        state->drawCountMapped[i] = (uint32_t*)state->drawCountAllocs[i].mapped;

        // Cull UBO
        VkBufferCreateInfo uboInfo = {};
        uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uboInfo.size = uboSize;
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(components->deviceQueueComp.device, &uboInfo, NULL, &state->cullUboBuffer.buffer[i]);
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->cullUboBuffer.buffer[i], &memReqs);
        state->cullUboBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->cullUboBuffer.buffer[i], state->cullUboBuffer.allocs[i].memory, state->cullUboBuffer.allocs[i].offset);
        state->cullUboBuffer.mapped[i] = (CullUBO*)state->cullUboBuffer.allocs[i].mapped;
    }
}

bool createFallbackResources(VulkanComponents* components, RendererState* state)
{
    // 1. Fallback Mesh (Simple Cube)
    const Vertex cubeVertices[] = {
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };
    
    const uint16_t cubeIndices[] = {
        0, 1, 2, 2, 3, 0, // front
        1, 5, 6, 6, 2, 1, // right
        5, 4, 7, 7, 6, 5, // back
        4, 0, 3, 3, 7, 4, // left
        3, 2, 6, 6, 7, 3, // top
        4, 5, 1, 1, 0, 4  // bottom
    };

    uint32_t fallbackMeshIdx = geometry_pool_upload(&state->globalGeometryPool, &gpuAllocator,
                                                    components->deviceQueueComp.device,
                                                    components->cmdComp.commandPool,
                                                    components->deviceQueueComp.transferQueue,
                                                    cubeVertices, 8, cubeIndices, 36);

    if (fallbackMeshIdx != FALLBACK_MESH_INDEX) {
        printf("Warning: Fallback mesh was assigned index %u instead of %u!\n", fallbackMeshIdx, FALLBACK_MESH_INDEX);
    }

    // 2. Fallback Texture (2x2 Magenta/Black Checkerboard)
    unsigned char fallbackPixels[16] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255
    };

    VkDeviceMemory fallbackImageMemory; // Memory managed by gpu_allocator

    if (!createTextureImageFromPixels(components, &state->fallbackImage, &fallbackImageMemory, &state->fallbackImageView, fallbackPixels, 2, 2)) {
        printf("Warning: Failed to create fallback texture!\n");
        return false;
    }

    // 3. Register Fallback Texture
    uint32_t fallbackTexIdx = bindless_register_texture(components, &state->bindlessTextures, state->fallbackImageView, components->renderComp.textureSampler);
    
    if (fallbackTexIdx != FALLBACK_TEXTURE_INDEX) {
        printf("Warning: Fallback texture was assigned index %u instead of %u!\n", fallbackTexIdx, FALLBACK_TEXTURE_INDEX);
    }

    return true;
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
	if (!ano_vk_init_cull_layout(&components, &rendererState))
	{
		printf("Quitting init: cull layout failure!\n");
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

	if (!createFallbackResources(&components, &rendererState))
	{
		printf("Quitting init: fallback resources failure.\n");
		unInitVulkan();
		return false;
	}

	components.renderComp.buffers.entityCount = 1;

	// In a real application, maxEntities would be dynamic or configured.
	uint32_t maxEntities = 1000;
	createTransformBuffer(&components, &rendererState, maxEntities);
	createMaterialBuffer(&components, &rendererState, maxEntities);
	createIndirectDrawBuffer(&components, &rendererState, maxEntities);
    createCullingBuffers(&components, &rendererState, maxEntities);

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

