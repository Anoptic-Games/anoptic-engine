#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/gpu_alloc.h"

extern struct VulkanGarbage vulkanGarbage;

int main() {
    printf("Starting Vulkan Memory Allocation test...\n");

    if (!initVulkan()) {
        printf("Failed to init Vulkan!\n");
        return 1;
    }
    VulkanContext* ctx = vulkanGarbage.ctx;

    // 1. Create a dummy buffer manually
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 1024;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer testBuffer;
    if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &testBuffer) != VK_SUCCESS) {
        printf("Failed to create test buffer!\n");
        unInitVulkan();
        return 1;
    }

    // 2. Query memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device, testBuffer, &memRequirements);

    if (memRequirements.size < 1024) {
        printf("Error: memory requirement size (%zu) is less than requested size (1024)!\n", (size_t)memRequirements.size);
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 3. Test gpu_alloc with device local memory properties
    // We can use the globally defined gpuAllocator
    GpuAllocation allocation = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (allocation.memory == VK_NULL_HANDLE) {
        printf("Error: gpu_alloc returned null memory handle!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 4. Bind the allocated memory to our test buffer
    if (vkBindBufferMemory(ctx->device, testBuffer, allocation.memory, allocation.offset) != VK_SUCCESS) {
        printf("Error: failed to bind buffer memory!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 5. Cleanup local test buffer (allocations from gpuAllocator are freed during allocator teardown in unInitVulkan)
    vkDestroyBuffer(ctx->device, testBuffer, NULL);

    unInitVulkan();
    printf("Memory Allocation test passed successfully!\n");
    return 0;
}
