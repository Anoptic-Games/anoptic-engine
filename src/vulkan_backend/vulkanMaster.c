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

static VulkanContext ctx;
PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT = NULL;
RendererState rendererState;
GpuAllocator gpuAllocator;
GpuAllocator stagingAllocator;
GpuAllocator swapchainAllocator;
GpuAllocator textureAllocator;

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
		if (rendererState.frames[i].frameSubmitted)
		{
			vkWaitForFences(ctx.device, 1, &(rendererState.frames[i].frameFence), VK_TRUE, UINT64_MAX);
			rendererState.frames[i].frameSubmitted = false; // reset the status
		}
    }

	if (vulkanGarbage.ctx)
	{
		cleanupVulkan(vulkanGarbage.ctx);
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
    uint32_t frameIdx = rendererState.frameIndex;
    DeletionQueue* q = &state->frames[frameIdx].deletionQueue;

    if (q->count >= q->capacity) {
        q->capacity = q->capacity == 0 ? 64 : q->capacity * 2;
        q->tasks = realloc(q->tasks, q->capacity * sizeof(DeletionTask));
    }

    q->tasks[q->count++] = (DeletionTask){ .type = type, .handle = handle };
}

void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    DeletionQueue* q = &state->frames[frameIndex].deletionQueue;

    for (uint32_t i = 0; i < q->count; i++) {
        DeletionTask task = q->tasks[i];
        switch (task.type) {
            case RESOURCE_TYPE_GEOMETRY_MESH:
                geometry_pool_free(&state->globalGeometryPool, task.handle);
                break;
            case RESOURCE_TYPE_BINDLESS_TEXTURE:
                // For now bindless_register_texture handles index. 
                // We'd add a bindless_free_texture(ctx, &state->bindlessTextures, task.handle) eventually
                break;
        }
    }
    q->count = 0; // Clear queue for next time we hit this frame
}

// Graphics operations

static const RenderPassDef g_framePasses[] = {
    // 0. GPU animation update
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_UPDATE,
        .dispatchX  = 0,
    },
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
    // 3. Transmissive geometry
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_TRANSMISSION,
        .implementationIndex    = 1,  // blended transmission variant
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
};

void recordCommandBuffer(uint32_t imageIndex) 
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0; // Optional
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	VkCommandBuffer cmd = rendererState.frames[rendererState.frameIndex].commandBuffer;

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
	swapChainBarrier.image = rendererState.images[imageIndex];
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

    uint32_t entityCount = rendererState.entityCount;

    for (int p = 0; p < sizeof(g_framePasses)/sizeof(g_framePasses[0]); p++) {
        const RenderPassDef* pass = &g_framePasses[p];

        if (pass->type == PASS_COMPUTE) {
            if (entityCount > 0) {
                if (pass->prototype == PIPELINE_COMPUTE_CULL) {
                    // Zero the entire indirect buffer (sized for the larger command format)
                    // so unwritten slots decode as no-op draws on either path, plus the draw count.
                    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
                        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    vkCmdFillBuffer(cmd, rendererState.indirectBuffer.buffer[rendererState.frameIndex], 0,
                        cmdStride * rendererState.indirectBuffer.capacity * PIPELINE_TYPE_COUNT, 0);
                    vkCmdFillBuffer(cmd, rendererState.culling.drawCountBuffer[rendererState.frameIndex], 0,
                        sizeof(uint32_t) * PIPELINE_TYPE_COUNT, 0);

                    // Add a barrier to make sure the fill completes before the compute shader writes to it
                    VkMemoryBarrier fillBarrier = {};
                    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &fillBarrier, 0, NULL, 0, NULL);
                }

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.prototypes[pass->prototype].implementations[0].pipeline);
                
                VkDescriptorSet set = pass->prototype == PIPELINE_COMPUTE_UPDATE ? 
                    rendererState.frames[rendererState.frameIndex].updateSet : 
                    rendererState.frames[rendererState.frameIndex].cullSet;
                    
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    rendererState.prototypes[pass->prototype].layout, 0, 1, &set, 0, NULL);
                    
                if (pass->prototype == PIPELINE_COMPUTE_UPDATE) {
                    vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &entityCount);
                }
                
                // If dispatchX is 0, we compute it from entityCount
                uint32_t dispatchX = pass->dispatchX == 0 ? (entityCount + 255) / 256 : pass->dispatchX;
                vkCmdDispatch(cmd, dispatchX, 1, 1);

                VkMemoryBarrier memoryBarrier = {};
                memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                
                if (pass->prototype == PIPELINE_COMPUTE_UPDATE) {
                    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &memoryBarrier, 0, NULL, 0, NULL);
                } else {
                    // The cull pass feeds the indirect commands (DRAW_INDIRECT) and the
                    // compacted/entity SSBOs read by the geometry stage (mesh or vertex).
                    VkPipelineStageFlags geomStage = ctx.deviceCapabilities.meshShader
                        ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                    memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage,
                        0, 1, &memoryBarrier, 0, NULL, 0, NULL);
                }
            }
        } else if (pass->type == PASS_GRAPHICS) {
            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            VkClearValue clearDepth = {};
            clearDepth.depthStencil.depth = 1.0f;
            clearDepth.depthStencil.stencil = 0;

            VkRenderingAttachmentInfo colorAttachment = {};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = rendererState.colorView; // MSAA color
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.resolveMode = pass->resolveMode;
            colorAttachment.resolveImageView = rendererState.views[imageIndex];
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = pass->colorLoadOp;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue = clearColor;

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = rendererState.frames[rendererState.frameIndex].depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachment.loadOp = pass->depthLoadOp;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue = clearDepth;

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
            renderingInfo.renderArea.extent = rendererState.imageExtent;
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
            viewport.width = (float)(rendererState.imageExtent.width);
            viewport.height = (float)(rendererState.imageExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            
            VkRect2D scissor = {};
            // Must match the viewport's units: the swapchain image is sized in physical
            // pixels (imageExtent), whereas glfwGetWindowSize returns logical points.
            // On a Retina/HiDPI display those differ by the backing scale, so using the
            // window size here clips rendering to a sub-rectangle of the surface.
            scissor.offset = (VkOffset2D){0, 0};
            scissor.extent = rendererState.imageExtent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &(rendererState.frames[rendererState.frameIndex].globalSet), 0, NULL);

            if (entityCount > 0) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    rendererState.prototypes[pass->prototype].layout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);

                uint32_t pipelineType = pass->prototype;
                uint32_t baseOffset = pipelineType * rendererState.culling.maxEntities;
                bool useMesh = ctx.deviceCapabilities.meshShader;
                VkShaderStageFlags pcStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, pcStage, 0, sizeof(uint32_t), &baseOffset);

                VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
                VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
                VkDeviceSize countOffset = (VkDeviceSize)pipelineType * sizeof(uint32_t);
                uint32_t maxDraws = rendererState.indirectBuffer.capacity;

                if (useMesh) {
                    VkDeviceSize indirectOffset = (VkDeviceSize)pipelineType * maxDraws * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    } else {
                        pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    }
                } else {
                    // Fallback: classic indexed indirect over the shared geometry index buffer.
                    // The cull pass wrote mesh-local firstIndex/indexCount + per-mesh vertexOffset.
                    vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    VkDeviceSize indirectOffset = (VkDeviceSize)pipelineType * maxDraws * sizeof(VkDrawIndexedIndirectCommand);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawIndexedIndirectCommand));
                    } else {
                        vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }
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
	printf("Image count: %d\n", rendererState.imageCount);
	printf("Image extent: width = %d, height = %d\n", rendererState.imageExtent.width, rendererState.imageExtent.height);
	
	// Buffer Components
	printf("\n=== Buffer Components ===\n");
	printf("Mesh index: %u\n", rendererState.entities[0].meshIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Uniform buffer %d: %p\n", i, (void*)rendererState.frames[i].uniformBuffer);
		printf("Uniform alloc %d: %p\n", i, (void*)rendererState.frames[i].uniformAlloc.memory);
		printf("Uniform buffer mapping %d: %p\n", i, rendererState.frames[i].uniformMapped);
	}

	// Synchronization Components
	printf("\n=== Synchronization Components ===\n");
	printf("Current frame index: %d\n", rendererState.frameIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		printf("Frame %d submitted: %d\n", i, rendererState.frames[i].frameSubmitted);
	}
	printf("\n======================================\n");
}

void updateTransformBuffer(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
	// Deprecated: Transforms are now updated via GPU compute shader.
	// Initial transforms are set directly in instantiate_node().
	state->transformBuffer.count = state->entityCount;
}

void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    uint32_t entityCount = state->entityCount;
    RenderEntity* entities = state->entities;

    // Reset draw count
    memset(state->culling.drawCountMapped[frameIndex], 0, sizeof(uint32_t) * PIPELINE_TYPE_COUNT);

    // Update CullUBO
    CullUBO* ubo = state->culling.ubo.mapped[frameIndex];
    GlobalUBO* globalUbo = state->frames[frameIndex].uniformMapped;
    
    // Calculate viewProj matrix
    multiplyMat4(ubo->viewProj, globalUbo->proj, globalUbo->view);

    // Extract frustum planes
    extractFrustumPlanes(ubo->frustumPlanes, ubo->viewProj);

    // Publish the active light count to the fragment stage.
    globalUbo->lightCount = state->lightBuffer.count;

    ubo->entityCount = entityCount;
    ubo->maxEntities = state->culling.maxEntities;

    // Update EntitySSBO
    uint32_t* entityBuffer = (uint32_t*)state->culling.entityMapped[frameIndex];
    for(uint32_t i=0; i < entityCount; i++) {
        entityBuffer[i*2] = entities[i].meshIndex;
        entityBuffer[i*2+1] = entities[i].materialIndex;
    }

    // Update MeshSSBO and MeshBoundsSSBO
    uint32_t meshCount = state->globalGeometryPool.meshCount;
    uint32_t* meshData = (uint32_t*)state->culling.meshDataMapped[frameIndex];
    float* meshBounds = (float*)state->culling.meshBoundsMapped[frameIndex];
    
    for(uint32_t i=0; i < meshCount; i++) {
        MeshRegion* mesh = &state->globalGeometryPool.meshes[i];
        
        meshData[i*8 + 0] = mesh->meshletCount;
        meshData[i*8 + 1] = mesh->meshletOffset;
        meshData[i*8 + 2] = mesh->uniqueVerticesOffset;
        meshData[i*8 + 3] = mesh->trianglesOffset;
        meshData[i*8 + 4] = mesh->vertexOffset;
        meshData[i*8 + 5] = mesh->classicIndexCount;       // fallback: VkDrawIndexedIndirectCommand.indexCount
        meshData[i*8 + 6] = mesh->classicIndexOffset / 4;  // fallback: firstIndex (u32 index units)
        meshData[i*8 + 7] = 0;

        meshBounds[i*4 + 0] = mesh->boundingSphereCenter[0];
        meshBounds[i*4 + 1] = mesh->boundingSphereCenter[1];
        meshBounds[i*4 + 2] = mesh->boundingSphereCenter[2];
        meshBounds[i*4 + 3] = mesh->boundingSphereRadius;
    }
}

#include "anoptic_time.h"

void testAssetUnloadReload(VulkanContext* ctx, RendererState* state) {
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
            originalMeshIndex = state->entities[0].meshIndex;
            state->entities[0].meshIndex = FALLBACK_MESH_INDEX;
            state->entities[0].materialIndex = 0; // Use fallback material if possible

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
            const uint32_t triIndices[] = { 0, 1, 2 };

            uint32_t newMeshIdx = geometry_pool_upload(&state->globalGeometryPool, &stagingAllocator,
                                                       ctx->device,
                                                       ctx->queueFamilyIndices.transferFamily,
                                                       ctx->transferQueue,
                                                       triVertices, 3, triIndices, 3);
            
            printf("--- TEST: New mesh assigned index %u (Expected %u) ---\n", newMeshIdx, originalMeshIndex);
            
            // Assign to entity
            state->entities[0].meshIndex = newMeshIdx;
            
            phase = 2; // Done
            //glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
}

void drawFrame() 
{
	if (rendererState.framebufferResized)
	{
		rendererState.framebufferResized = false;
		recreateSwapChain(&ctx, window);
		return;
	}

    testAssetUnloadReload(&ctx, &rendererState);

    if (rendererState.frames[rendererState.frameIndex].frameSubmitted == true)
    {
        vkWaitForFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence), VK_TRUE, UINT64_MAX);
    }

    // Process any deferred deletions that were waiting for this frame's previous commands to finish
    flush_deletion_queue(&ctx, &rendererState, rendererState.frameIndex);
    
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(ctx.device, rendererState.swapChain, UINT64_MAX, rendererState.frames[rendererState.frameIndex].imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) 
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) 
	{
		printf("Failed to acquire swap chain image!\n");
		return;
	}

	updateUniformBuffer(&ctx, &rendererState);

	// Update entity transforms
	// float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
	// for (uint32_t i = 0; i < rendererState.entityCount && i < 3; i++) {
		//updateMeshTransforms(&ctx, &rendererState.entities[i], moveOffsets[i]);
	// }

	updateTransformBuffer(&ctx, &rendererState, rendererState.frameIndex);
	updateCullingBuffers(&ctx, &rendererState, rendererState.frameIndex);

	vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].commandBuffer, 0);
	recordCommandBuffer(imageIndex);

	//updateUniformBuffer(&ctx, &rendererState);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = {rendererState.frames[rendererState.frameIndex].imageAvailable};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].commandBuffer);
	VkSemaphore signalSemaphores[] = {rendererState.frames[rendererState.frameIndex].renderFinished};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkResetFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence)); // this goes here because multi-threading
	if (vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, rendererState.frames[rendererState.frameIndex].frameFence) != VK_SUCCESS) 
	{
		printf("Failed to submit draw command buffer!\n");
		return;
	}

    // Presentation should happen *before* submitting commands for a new frame, so we're actually taking advantage of buffering

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = {rendererState.swapChain};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = NULL; // Optional

	VkResult presentResult = vkQueuePresentKHR(ctx.presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (presentResult != VK_SUCCESS)
	{
		printf("Failed to present swap chain image!\n");
		return;
	}

	rendererState.frames[rendererState.frameIndex].frameSubmitted = true;

	//printUniformTransferState();

	rendererState.frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (rendererState.frameIndex == MAX_FRAMES_IN_FLIGHT)
	{
		rendererState.frameIndex = 0;
	}
}

//Init and cleanup functions

bool createMaterialBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->materialBuffer.capacity = maxEntities;
    state->materialBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(MaterialData) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->materialBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create material buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, state->materialBuffer.buffer[i], &memRequirements);
        
        state->materialBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->materialBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->materialBuffer.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->materialBuffer.buffer[i], state->materialBuffer.allocs[i].memory, state->materialBuffer.allocs[i].offset);
        
        state->materialBuffer.mapped[i] = (MaterialData*)state->materialBuffer.allocs[i].mapped;
    }
    return true;
}

bool createLightBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxLights) {
    state->lightBuffer.capacity = maxLights;
    state->lightBuffer.count = 0;

    VkDeviceSize bufferSize = sizeof(LightData) * maxLights;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->lightBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create light buffer!\n");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, state->lightBuffer.buffer[i], &memRequirements);

        state->lightBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->lightBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->lightBuffer.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->lightBuffer.buffer[i], state->lightBuffer.allocs[i].memory, state->lightBuffer.allocs[i].offset);

        state->lightBuffer.mapped[i] = (LightData*)state->lightBuffer.allocs[i].mapped;
    }
    return true;
}

// Appends a transform-only light entity (no geometry) to the scene and registers
// its LightData in the light buffer. `light` supplies the photometric parameters;
// this fills in transformIndex (the new entity) and marks it enabled, then writes
// the entry into every frame's light buffer. Must be called before the per-frame
// transform upload loop so the light's transform reaches the transform buffers.
// Returns the new entity index.
static uint32_t addLightEntity(LightData light, mat4 transform) {
    uint32_t entIdx = rendererState.entityCount;
    rendererState.entityCount += 1;
    rendererState.entities = realloc(rendererState.entities, rendererState.entityCount * sizeof(RenderEntity));

    uint32_t lightIdx = rendererState.lightBuffer.count++;

    rendererState.entities[entIdx].meshIndex = NO_MESH_INDEX;   // skipped by culling -> draws nothing
    rendererState.entities[entIdx].materialIndex = 0;
    rendererState.entities[entIdx].lightIndex = lightIdx;
    memcpy(&rendererState.entities[entIdx].transform, transform, sizeof(mat4));

    light.transformIndex = entIdx;
    light.enabled = 1;
    for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        rendererState.lightBuffer.mapped[frame][lightIdx] = light;
    }
    return entIdx;
}

bool createAngularVelocityBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->angularVelocityBuffer.capacity = maxEntities;
    state->angularVelocityBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(Vector4) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->angularVelocityBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create angular velocity buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, state->angularVelocityBuffer.buffer[i], &memRequirements);
        
        state->angularVelocityBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->angularVelocityBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->angularVelocityBuffer.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->angularVelocityBuffer.buffer[i], state->angularVelocityBuffer.allocs[i].memory, state->angularVelocityBuffer.allocs[i].offset);
        
        state->angularVelocityBuffer.mapped[i] = (Vector4*)state->angularVelocityBuffer.allocs[i].mapped;
    }
    return true;
}

bool createTransformBuffer(VulkanContext* ctx, TransformBuffer* buf, uint32_t maxEntities) {
    buf->capacity = maxEntities;
    buf->count = 0;
    
    VkDeviceSize bufferSize = sizeof(mat4) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &buf->buffer[i]) != VK_SUCCESS) {
            printf("Failed to create transform buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx->device, buf->buffer[i], &memRequirements);
        
        buf->allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (buf->allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, buf->buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, buf->buffer[i], buf->allocs[i].memory, buf->allocs[i].offset);
        
        buf->mapped[i] = (mat4*)buf->allocs[i].mapped;
    }
    return true;
}

bool createIndirectDrawBuffer(VulkanContext* ctx, RendererState* state, uint32_t maxDraws) {
    state->indirectBuffer.capacity = maxDraws;
    // Size for the larger of the two command formats so one allocation serves both
    // paths: VkDrawMeshTasksIndirectCommandEXT (12 B) or VkDrawIndexedIndirectCommand (20 B).
    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
    VkDeviceSize bufferSize = cmdStride * maxDraws * PIPELINE_TYPE_COUNT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->indirectBuffer.drawCount[i] = 0;
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bufferSize,
            .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &state->indirectBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create indirect draw buffer!\n");
            return false;
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(ctx->device, state->indirectBuffer.buffer[i], &memReqs);

        state->indirectBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->indirectBuffer.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->indirectBuffer.buffer[i], NULL);
            return false;
        }

        vkBindBufferMemory(ctx->device, state->indirectBuffer.buffer[i], state->indirectBuffer.allocs[i].memory, state->indirectBuffer.allocs[i].offset);
        state->indirectBuffer.mapped[i] = (VkDrawMeshTasksIndirectCommandEXT*)state->indirectBuffer.allocs[i].mapped;
    }

    return true;
}

bool createCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t maxEntities) {
    state->culling.maxEntities = maxEntities;
    uint32_t maxMeshes = 1024;
    
    VkDeviceSize entityBufferSize = sizeof(uint32_t) * 2 * maxEntities; // meshIndex, materialIndex
    VkDeviceSize meshDataSize = sizeof(uint32_t) * 8 * maxMeshes; // uvec8
    VkDeviceSize meshBoundsSize = sizeof(float) * 4 * maxMeshes; // vec4
    VkDeviceSize drawCountSize = sizeof(uint32_t) * PIPELINE_TYPE_COUNT;
    VkDeviceSize compactedEntityIndicesSize = sizeof(uint32_t) * maxEntities * PIPELINE_TYPE_COUNT;
    VkDeviceSize uboSize = sizeof(CullUBO);
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Entity Buffer
        VkBufferCreateInfo entityInfo = {};
        entityInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        entityInfo.size = entityBufferSize;
        entityInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        entityInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &entityInfo, NULL, &state->culling.entityBuffer[i]);
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(ctx->device, state->culling.entityBuffer[i], &memReqs);
        state->culling.entityAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.entityAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.entityBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.entityBuffer[i], state->culling.entityAllocs[i].memory, state->culling.entityAllocs[i].offset);
        state->culling.entityMapped[i] = state->culling.entityAllocs[i].mapped;

        // Mesh Data Buffer
        VkBufferCreateInfo meshInfo = {};
        meshInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        meshInfo.size = meshDataSize;
        meshInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        meshInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &meshInfo, NULL, &state->culling.meshDataBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.meshDataBuffer[i], &memReqs);
        state->culling.meshDataAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.meshDataAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.meshDataBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.meshDataBuffer[i], state->culling.meshDataAllocs[i].memory, state->culling.meshDataAllocs[i].offset);
        state->culling.meshDataMapped[i] = state->culling.meshDataAllocs[i].mapped;

        // Mesh Bounds Buffer
        VkBufferCreateInfo boundsInfo = {};
        boundsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        boundsInfo.size = meshBoundsSize;
        boundsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        boundsInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &boundsInfo, NULL, &state->culling.meshBoundsBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.meshBoundsBuffer[i], &memReqs);
        state->culling.meshBoundsAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.meshBoundsAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.meshBoundsBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.meshBoundsBuffer[i], state->culling.meshBoundsAllocs[i].memory, state->culling.meshBoundsAllocs[i].offset);
        state->culling.meshBoundsMapped[i] = state->culling.meshBoundsAllocs[i].mapped;

        // Draw Count Buffer
        VkBufferCreateInfo countInfo = {};
        countInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        countInfo.size = drawCountSize;
        countInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        countInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &countInfo, NULL, &state->culling.drawCountBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.drawCountBuffer[i], &memReqs);
        state->culling.drawCountAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.drawCountAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.drawCountBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.drawCountBuffer[i], state->culling.drawCountAllocs[i].memory, state->culling.drawCountAllocs[i].offset);
        state->culling.drawCountMapped[i] = (uint32_t*)state->culling.drawCountAllocs[i].mapped;

        // Compacted Entity Indices Buffer
        VkBufferCreateInfo compactedInfo = {};
        compactedInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        compactedInfo.size = compactedEntityIndicesSize;
        compactedInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        compactedInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &compactedInfo, NULL, &state->culling.compactedEntityIndicesBuffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.compactedEntityIndicesBuffer[i], &memReqs);
        state->culling.compactedEntityIndicesAllocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.compactedEntityIndicesAllocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.compactedEntityIndicesBuffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.compactedEntityIndicesBuffer[i], state->culling.compactedEntityIndicesAllocs[i].memory, state->culling.compactedEntityIndicesAllocs[i].offset);
        state->culling.compactedEntityIndicesMapped[i] = (uint32_t*)state->culling.compactedEntityIndicesAllocs[i].mapped;

        // Cull UBO
        VkBufferCreateInfo uboInfo = {};
        uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uboInfo.size = uboSize;
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx->device, &uboInfo, NULL, &state->culling.ubo.buffer[i]);
        vkGetBufferMemoryRequirements(ctx->device, state->culling.ubo.buffer[i], &memReqs);
        state->culling.ubo.allocs[i] = gpu_alloc(&gpuAllocator, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (state->culling.ubo.allocs[i].memory == VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, state->culling.ubo.buffer[i], NULL);
            return false;
        }
        vkBindBufferMemory(ctx->device, state->culling.ubo.buffer[i], state->culling.ubo.allocs[i].memory, state->culling.ubo.allocs[i].offset);
        state->culling.ubo.mapped[i] = (CullUBO*)state->culling.ubo.allocs[i].mapped;
    }
    return true;
}

bool createFallbackResources(VulkanContext* ctx, RendererState* state)
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
    
    const uint32_t cubeIndices[] = {
        0, 1, 2, 2, 3, 0, // front
        1, 5, 6, 6, 2, 1, // right
        5, 4, 7, 7, 6, 5, // back
        4, 0, 3, 3, 7, 4, // left
        3, 2, 6, 6, 7, 3, // top
        4, 5, 1, 1, 0, 4  // bottom
    };

    uint32_t fallbackMeshIdx = geometry_pool_upload(&state->globalGeometryPool, &stagingAllocator,
                                                    ctx->device,
                                                    ctx->queueFamilyIndices.transferFamily,
                                                    ctx->transferQueue,
                                                    cubeVertices, 8, cubeIndices, 36);

    if (fallbackMeshIdx != FALLBACK_MESH_INDEX) {
        printf("Warning: Fallback mesh was assigned index %u instead of %u!\n", fallbackMeshIdx, FALLBACK_MESH_INDEX);
    }

    // 2. Fallback Texture (2x2 Magenta/Black Checkerboard)
    unsigned char fallbackPixels[16] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255
    };

    GpuAllocation fallbackImageAlloc; // Memory managed by gpu_allocator

    if (!createTextureImageFromPixels(ctx, VK_NULL_HANDLE, &state->fallbackImage, &fallbackImageAlloc, &state->fallbackImageView, fallbackPixels, 2, 2, NULL)) {
        printf("Warning: Failed to create fallback texture!\n");
        return false;
    }

    // 3. Register Fallback Texture
    uint32_t fallbackTexIdx = bindless_register_texture(ctx, &state->bindlessTextures, state->fallbackImageView, state->textureSampler);
    
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

	window = initWindow(&ctx, &monitors);

	if (window == NULL)
	{
		// Handle error
		printf("Window initialization failed.\n");
		unInitVulkan();
		return 0;
	}

	requestPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);

	ctx.enableValidationLayers = true;
	rendererState.frameIndex = 0; // Tracks which frame is being processed

	// Initialize Vulkan
	if (createInstance(&ctx) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create Vulkan instance!\n");
		unInitVulkan();
		return false;
	}
	vulkanGarbage.ctx = &ctx;

	// Create a window surface
	if (createSurface(ctx.instance, window, &(ctx.surface)) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create window surface!\n");
		unInitVulkan();
		return false;
	}

	// Pick physical device
	DeviceCapabilities capabilities;
	ctx.physicalDevice = VK_NULL_HANDLE;

	//!TODO replace empty char array with preffered device from VulkanSettings   
	char* preferredDevice = getChosenDevice();
	if (!pickPhysicalDevice(&ctx, &capabilities, &(ctx.queueFamilyIndices), preferredDevice))
	{
		fprintf(stderr, "Quitting init: physical device failure!\n");
		unInitVulkan();
		return false;
	}
	

	if (createLogicalDevice(ctx.physicalDevice, &(ctx.device), &(ctx.graphicsQueue), &(ctx.computeQueue), &(ctx.transferQueue), &(ctx.presentQueue), &(ctx.queueFamilyIndices)) != VK_SUCCESS)
	{
		fprintf(stderr, "Quitting init: logical device failure!\n");
		unInitVulkan();
		return false;
	}

    // Mesh-shader entry points only exist on the mesh path. The fallback path draws
    // via core vkCmdDrawIndexedIndirect[Count] and needs none of these.
    if (ctx.deviceCapabilities.meshShader) {
        pfnVkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksEXT");
        pfnVkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectEXT");
        pfnVkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectCountEXT");

        if (!pfnVkCmdDrawMeshTasksEXT || !pfnVkCmdDrawMeshTasksIndirectEXT || !pfnVkCmdDrawMeshTasksIndirectCountEXT) {
            fprintf(stderr, "Failed to load mesh shader extension function pointers!\n");
            unInitVulkan();
            return false;
        }
    }

	gpuAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &gpuAllocator.memProps);
	gpuAllocator.blocks = NULL;
	gpuAllocator.blockCount = 0;
	stagingAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &stagingAllocator.memProps);
	stagingAllocator.blocks = NULL;
	stagingAllocator.blockCount = 0;

	swapchainAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &swapchainAllocator.memProps);
	swapchainAllocator.blocks = NULL;
	swapchainAllocator.blockCount = 0;

	textureAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &textureAllocator.memProps);
	textureAllocator.blocks = NULL;
	textureAllocator.blockCount = 0;

	if (!ano_vk_init_geometry_pool(&rendererState.globalGeometryPool, &gpuAllocator, ctx.device, ctx.queueFamilyIndices.graphicsFamily, ctx.queueFamilyIndices.transferFamily)) {
		printf("Quitting init: geometry pool creation failure!\n");
		unInitVulkan();
		return false;
	}




	initSwapChain(&ctx, window, getChosenPresentMode(), VK_NULL_HANDLE, &rendererState); // Initialize a swap chain
	if (rendererState.swapChain == NULL)
	{
		printf("Quitting init: swap chain failure.\n");
		unInitVulkan();
		return false;
	}
	
	createImageViews(&ctx, &rendererState);
	if (rendererState.views == NULL)
	{
		printf("Quitting init: image view failure.\n");
		unInitVulkan();
		return false;
	}

	if (!createCommandPool(ctx.device, ctx.physicalDevice,
						   ctx.surface, &(rendererState.commandPool)))
	{
		printf("Quitting init: command pool failure!\n");
		unInitVulkan();
		return false;
	}

	createColorResources(&ctx); // Make this a bool and add check

	if(!createDepthResources(&ctx, &rendererState))
	{
		printf("Quitting init: depth resource creation failure!\n");
	}



	if (!ano_vk_init_global_layout(&ctx, &rendererState))
	{
		printf("Quitting init: global layout failure!\n");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_cull_layout(&ctx, &rendererState))
	{
		printf("Quitting init: cull layout failure!\n");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_material_layouts(&ctx, &rendererState))
	{
		printf("Quitting init: material layouts failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createBindlessTextureArray(&ctx, &rendererState))
	{
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_pipelines(&ctx, &rendererState))
	{
		printf("Quitting init: pipeline failure!\n");
		unInitVulkan();
		return false;
	}


	/*if(!createTextureImage(&ctx, &rendererState.entities[0], "texture.jpg", false))
	{
		printf("Quitting init: texture read failure!\n");
		unInitVulkan();
		return false;
	}

	if(!createTextureImageView(&ctx, &rendererState.entities[0]))
	{
		printf("Quitting init: texture image view failure!\n");
		unInitVulkan();
		return false;
	}*/

	if(!createTextureSampler(&ctx, &rendererState))
	{
		printf("Quitting init: texture sampler failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createFallbackResources(&ctx, &rendererState))
	{
		printf("Quitting init: fallback resources failure.\n");
		unInitVulkan();
		return false;
	}

	rendererState.entityCount = 0;

	// In a real application, maxEntities would be dynamic or configured.
	uint32_t maxEntities = 10000;
	if (!createTransformBuffer(&ctx, &rendererState.transformBuffer, maxEntities) ||
	    !createTransformBuffer(&ctx, &rendererState.initialTransformBuffer, maxEntities) ||
	    !createAngularVelocityBuffer(&ctx, &rendererState, maxEntities) ||
	    !createMaterialBuffer(&ctx, &rendererState, maxEntities) ||
	    !createLightBuffer(&ctx, &rendererState, maxEntities) ||
	    !createIndirectDrawBuffer(&ctx, &rendererState, maxEntities) ||
	    !createCullingBuffers(&ctx, &rendererState, maxEntities))
	{
		printf("Quitting init: buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	ModelAsset* vikingRoomAsset = parseGltf(&ctx, "viking_room.gltf");
	if(!vikingRoomAsset)
	{
		printf("Failed to parse glTF file!\n");
		unInitVulkan();
		return false;
	}
	
	mat4 identity = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};
	rotateMatrix(identity, 'X', -3.14159f / 2.0f);
	instantiate_model(vikingRoomAsset, identity);

	uint32_t vikingRoomEntityCount = rendererState.entityCount;

	ModelAsset* candleHolderAsset = parseGltf(&ctx, "GlassHurricaneCandleHolder.gltf");
	if(!candleHolderAsset)
	{
		printf("Failed to parse GlassHurricaneCandleHolder glTF file!\n");
		unInitVulkan();
		return false;
	}
	
	mat4 candleTransform = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};
	candleTransform[3][0] = 2.0f; // Orbit radius of 5 units on X
	instantiate_model(candleHolderAsset, candleTransform);

	// -------------------------------------------------------------
	// Scene lights (generalized light format, replacing the values
	// formerly hard-coded in the fragment shaders). Each light is a
	// transform-only entity: it carries no geometry (culling skips it)
	// and its world position/direction are derived from its transform,
	// so it participates in the GPU transform/animation system.
	//
	// Convention: a light's "forward" is the entity local -Z axis, i.e.
	// -transform[2] (column 2). For directional/spot lights set column 2
	// to the negated travel direction; translation lives in column 3.
	// -------------------------------------------------------------
	uint32_t firstLightEntity = rendererState.entityCount;

	// Directional key light (warm white), shining from up/front (0.5,1,0.3).
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[2][0] = 0.4319f; xform[2][1] = 0.8638f; xform[2][2] = 0.2591f; // normalized (0.5,1,0.3)
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.96f; l.color[2] = 0.9f;
		l.intensity = 2.5f;
		l.range = 0.0f; // directional: unbounded
		l.type = LIGHT_TYPE_DIRECTIONAL;
		addLightEntity(l, xform);
	}

	// Warm point light (orbits the origin to exercise the animation path).
	uint32_t warmLightEntity;
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = 1.5f; xform[3][2] = 1.2f;
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.95f; l.color[2] = 0.8f;
		l.intensity = 5.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		warmLightEntity = addLightEntity(l, xform);
	}

	// Cool blue fill from the opposite side.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = -2.0f; xform[3][1] = 2.0f; xform[3][2] = -1.0f;
		LightData l = {0};
		l.color[0] = 0.4f; l.color[1] = 0.6f; l.color[2] = 1.0f;
		l.intensity = 4.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Red rim/accent light.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 2.0f; xform[3][1] = 0.5f; xform[3][2] = 0.0f;
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 0.3f; l.color[2] = 0.3f;
		l.intensity = 3.5f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Greenish/cyan fill from below.
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = -1.0f; xform[3][2] = 1.0f;
		LightData l = {0};
		l.color[0] = 0.3f; l.color[1] = 1.0f; l.color[2] = 0.8f;
		l.intensity = 2.0f; l.range = 10.0f; l.type = LIGHT_TYPE_POINT;
		addLightEntity(l, xform);
	}

	// Overhead spotlight aimed straight down (demonstrates the spot type).
	{
		mat4 xform = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		xform[3][0] = 0.0f; xform[3][1] = 4.0f; xform[3][2] = 0.0f;
		// forward = -column2 = (0,-1,0): aim down. column2 stays identity (0,0,1)? No:
		xform[2][0] = 0.0f; xform[2][1] = 1.0f; xform[2][2] = 0.0f; // -column2 = (0,-1,0)
		LightData l = {0};
		l.color[0] = 1.0f; l.color[1] = 1.0f; l.color[2] = 1.0f;
		l.intensity = 20.0f; l.range = 12.0f; l.type = LIGHT_TYPE_SPOT;
		l.innerConeCos = 0.966f; // ~15 degrees
		l.outerConeCos = 0.906f; // ~25 degrees
		addLightEntity(l, xform);
	}

	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
		float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
		for (int i = 0; i < rendererState.entityCount; i++) {
			bool isLight  = (uint32_t)i >= firstLightEntity;
			bool isCandle = !isLight && (uint32_t)i >= vikingRoomEntityCount;
			bool orbit    = isCandle || (uint32_t)i == warmLightEntity;

			rendererState.angularVelocityBuffer.mapped[frame][i].v[0] = 0.0f;
			rendererState.angularVelocityBuffer.mapped[frame][i].v[1] = orbit ? 0.5f : (isLight ? 0.0f : 1.0f);
			rendererState.angularVelocityBuffer.mapped[frame][i].v[2] = 0.0f;
			rendererState.angularVelocityBuffer.mapped[frame][i].v[3] = orbit ? 1.0f : 0.0f; // orbit flag

			memcpy(&rendererState.transformBuffer.mapped[frame][i], &rendererState.entities[i].transform, sizeof(mat4));
			memcpy(&rendererState.initialTransformBuffer.mapped[frame][i], &rendererState.entities[i].transform, sizeof(mat4));

			if (!isLight && i < 3) {
			    rendererState.transformBuffer.mapped[frame][i][3][0] += moveOffsets[i];
			    rendererState.initialTransformBuffer.mapped[frame][i][3][0] += moveOffsets[i];
			}
		}
	}
	/*if (!createVertexBuffer(&ctx, vertices, 8, &rendererState.entities[0]))
	{
		printf("Quitting init: vertex buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	// Fill the vertex buffer
	if (!stagingTransfer(&ctx, vertices, rendererState.entities[0].vertex, sizeof(vertices)))
	{
		printf("Quitting init: staging buffer population failure!\n");
		unInitVulkan();
		return false;
	}

	if (!createIndexBuffer(&ctx, vertexIndices, 12, &rendererState.entities[0]))
	{
		printf("Quitting init: vertex buffer creation failure!\n");
		unInitVulkan();
		return false;
	}

	if (!stagingTransfer(&ctx, vertexIndices, rendererState.entities[0].index, sizeof(vertexIndices)))
	{
		printf("Quitting init: staging buffer population failure!\n");
		unInitVulkan();
		return false;
	}*/

	if (!createUniformBuffers(&ctx, &rendererState))
	{
		printf("Quitting init: uniform buffer creation failure!\n");
		unInitVulkan();
		return false;
	}



	// HERE
	if (!createDescriptorPool(&ctx, &rendererState))
	{
		printf("Quitting init: UBO descriptor pool creation failure!\n");
		unInitVulkan();
		return false;
	}


	if (!createDescriptorSets(&ctx, &rendererState))
	{
		printf("Quitting init: UBO descriptor sets creation failure!\n");
		unInitVulkan();
		return false;
	}


	updateUboDescriptorSets(&ctx, &rendererState);


	if (!createCommandBuffer(&ctx, &rendererState))
	{
		printf("Quitting init: command buffer failure!\n");
		unInitVulkan();
		return false;
	}
	

	if (!createSyncObjects(&ctx, &rendererState))
	{
		printf("Quitting init: sync failure!\n");
		unInitVulkan();
		return false;
	}

	printf("Instance creation complete!\n");

	return true;
}

