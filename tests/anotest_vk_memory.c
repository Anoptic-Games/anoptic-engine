#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/gpu_alloc.h"

extern struct VulkanGarbage vulkanGarbage;
extern bool g_AnoVkNoSuitableGpu;

int main() {
    printf("Starting Vulkan Memory Allocation test...\n");

    if (!initVulkan()) {
        if (g_AnoVkNoSuitableGpu) {
            printf("SKIP: no Vulkan device here can run the renderer.\n");
            return 77; // ctest SKIP_RETURN_CODE
        }
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
        printf("Error: memory requirement size (%zu) is less than requested size (1024)!\n", memRequirements.size);
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (memRequirements.alignment == 0) {
        printf("Error: memory requirement alignment is 0!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (memRequirements.memoryTypeBits == 0) {
        printf("Error: memory requirement type bits is 0!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 3. Test findMemoryType with valid requirements
    uint32_t validMemType = findMemoryType(ctx, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (validMemType == UINT32_MAX) {
        printf("Error: failed to find valid memory type!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 4. Test findMemoryType with impossible typeFilter (0)
    uint32_t invalidMemType = findMemoryType(ctx, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (invalidMemType != UINT32_MAX) {
        printf("Error: findMemoryType succeeded with impossible type filter 0!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 5. Arena gpu_alloc + bind
    GpuAllocation testAlloc = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (testAlloc.memory == VK_NULL_HANDLE || testAlloc.size < memRequirements.size) {
        printf("Error: gpu_alloc failed to return a valid allocation!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (testAlloc.offset % memRequirements.alignment != 0) {
        printf("Error: gpu_alloc offset (%zu) violates required alignment (%zu)!\n", testAlloc.offset, memRequirements.alignment);
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (vkBindBufferMemory(ctx->device, testBuffer, testAlloc.memory, testAlloc.offset) != VK_SUCCESS) {
        printf("Error: failed to bind buffer to arena allocation!\n");
        vkDestroyBuffer(ctx->device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // Cleanup. gpu_alloc has arena semantics 〜 no individual free; the allocator
    // is torn down by unInitVulkan.
    vkDestroyBuffer(ctx->device, testBuffer, NULL);

    unInitVulkan();
    printf("Memory Allocation test passed successfully!\n");
    return 0;
}
