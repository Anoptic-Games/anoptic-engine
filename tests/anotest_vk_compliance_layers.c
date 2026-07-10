#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vulkan_backend/vulkanMaster.h"

extern uint32_t g_ValidationErrors;
extern struct VulkanGarbage vulkanGarbage;

int main() {
    printf("Starting Vulkan Compliance Layer test...\n");
    g_ValidationErrors = 0;

    if (!initVulkan()) {
        printf("Failed to init Vulkan!\n");
        return 1;
    }
    VulkanContext* ctx = vulkanGarbage.ctx;

    // The initialization itself should not have caused validation errors
    if (g_ValidationErrors > 0) {
        printf("Error: Validation errors occurred during initVulkan!\n");
        unInitVulkan();
        return 1;
    }

    // Intentionally trigger a validation error by passing invalid creation parameters
    printf("Triggering intentional validation error (invalid buffer creation)...\n");
    VkBufferCreateInfo badInfo = {};
    badInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    badInfo.size = 0; // Invalid size
    badInfo.usage = 0; // Invalid usage
    VkBuffer badBuffer = VK_NULL_HANDLE;
    vkCreateBuffer(ctx->device, &badInfo, NULL, &badBuffer);
    // Some drivers create the object anyway.
    if (badBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(ctx->device, badBuffer, NULL);

    unInitVulkan();

    // The test PASSES if we successfully intercepted at least one validation error
    if (g_ValidationErrors > 0) {
        printf("Success: Intercepted %u validation errors!\n", g_ValidationErrors);
        return 0; // Success!
    } else {
        printf("Error: Strict validation failed to catch the invalid handle!\n");
        return 1; // Failure!
    }
}
