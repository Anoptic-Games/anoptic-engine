#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vulkan_backend/vulkanMaster.h"

extern uint32_t g_ValidationErrors;
extern struct VulkanGarbage vulkanGarbage;
// rendererState is declared extern in vulkanMaster.h

int main() {
    printf("Starting Vulkan Synchronization Primitives test...\n");
    g_ValidationErrors = 0;

    if (!initVulkan()) {
        printf("Failed to init Vulkan!\n");
        return 1;
    }
    VulkanContext* ctx = vulkanGarbage.ctx;

    if (g_ValidationErrors > 0) {
        printf("Error: Validation errors occurred during initVulkan!\n");
        unInitVulkan();
        return 1;
    }

    // 1. Test Single Time Command submission which inherently tests vkQueueSubmit and vkQueueWaitIdle
    printf("Testing synchronous command buffer submission...\n");
    VkCommandBuffer cmd = beginSingleTimeCommands(ctx);

    // Perform a dummy operation (e.g., pipeline barrier without any actual memory transition just to have a command)
    VkMemoryBarrier memBarrier = {};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = 0;
    memBarrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        1, &memBarrier,
        0, NULL,
        0, NULL
    );

    endSingleTimeCommands(ctx, cmd);

    if (g_ValidationErrors > 0) {
        printf("Error: Validation errors occurred during synchronous command submission!\n");
        unInitVulkan();
        return 1;
    }

    // 2. Test proper asynchronous submission with Fences and Semaphores
    printf("Testing asynchronous submission with Fences and Semaphores...\n");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // We won't actually wait on the semaphore because there's no swapchain image acquisition in this headless test,
    // which would cause a deadlock or timeout. Instead, we'll just test the Fence.

    // Use the first frame's pre-allocated command buffer and fence.
    VkCommandBuffer cmdAsync = rendererState.frames[0].commandBuffer;
    VkFence frameFence = rendererState.frames[0].frameFence;

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmdAsync, &beginInfo) != VK_SUCCESS) {
        printf("Error: Failed to begin command buffer!\n");
        unInitVulkan();
        return 1;
    }

    // End the command buffer to put it in the executable state
    if (vkEndCommandBuffer(cmdAsync) != VK_SUCCESS) {
        printf("Error: Failed to end command buffer!\n");
        unInitVulkan();
        return 1;
    }

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdAsync;

    // Ensure the fence is reset before submission
    vkResetFences(ctx->device, 1, &frameFence);

    if (vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, frameFence) != VK_SUCCESS) {
        printf("Error: Failed to submit draw command buffer!\n");
        unInitVulkan();
        return 1;
    }

    // Wait for the queue to finish executing using the Fence
    vkWaitForFences(ctx->device, 1, &frameFence, VK_TRUE, UINT64_MAX);

    if (g_ValidationErrors > 0) {
        printf("Error: Validation errors occurred during asynchronous submission!\n");
        unInitVulkan();
        return 1;
    }

    // 3. Intentionally trigger a synchronization validation error (Invalid Fence Create Flags)
    printf("Triggering intentional validation error (invalid fence creation)...\n");
    uint32_t currentErrors = g_ValidationErrors;

    VkFenceCreateInfo badFenceInfo = {};
    badFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    badFenceInfo.flags = 0xFFFFFFFF; // Invalid flags

    VkFence badFence = VK_NULL_HANDLE;
    vkCreateFence(ctx->device, &badFenceInfo, NULL, &badFence);
    // Some drivers create the object anyway.
    if (badFence != VK_NULL_HANDLE)
        vkDestroyFence(ctx->device, badFence, NULL);

    unInitVulkan();

    if (g_ValidationErrors > currentErrors) {
        printf("Success: Sync test completed and caught intentional validation violations!\n");
        return 0; // Success!
    } else {
        printf("Error: Strict validation failed to catch the invalid sync handle/flags!\n");
        return 1; // Failure!
    }
}
