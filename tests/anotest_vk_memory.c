#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/memory/memory.h"

extern struct VulkanGarbage vulkanGarbage;

int main() {
    printf("Starting Vulkan Memory Allocation test...\n");

    if (!initVulkan()) {
        printf("Failed to init Vulkan!\n");
        return 1;
    }
    VulkanComponents* comps = vulkanGarbage.components;

    // 1. Create a dummy buffer manually
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 1024;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer testBuffer;
    if (vkCreateBuffer(comps->deviceQueueComp.device, &bufferInfo, NULL, &testBuffer) != VK_SUCCESS) {
        printf("Failed to create test buffer!\n");
        unInitVulkan();
        return 1;
    }

    // 2. Query memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(comps->deviceQueueComp.device, testBuffer, &memRequirements);

    if (memRequirements.size < 1024) {
        printf("Error: memory requirement size (%zu) is less than requested size (1024)!\n", memRequirements.size);
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (memRequirements.alignment == 0) {
        printf("Error: memory requirement alignment is 0!\n");
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    if (memRequirements.memoryTypeBits == 0) {
        printf("Error: memory requirement type bits is 0!\n");
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 3. Test findMemoryType with valid requirements
    uint32_t validMemType = findMemoryType(comps, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (validMemType == UINT32_MAX) {
        printf("Error: failed to find valid memory type!\n");
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 4. Test findMemoryType with impossible typeFilter (0)
    uint32_t invalidMemType = findMemoryType(comps, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (invalidMemType != UINT32_MAX) {
        printf("Error: findMemoryType succeeded with impossible type filter 0!\n");
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // 5. Test allocateBuffer logic (it internally calls vkBindBufferMemory)
    VkDeviceMemory testMemory;
    if (!allocateBuffer(comps, testBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &testMemory)) {
        printf("Error: allocateBuffer failed!\n");
        vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
        unInitVulkan();
        return 1;
    }

    // Cleanup
    vkDestroyBuffer(comps->deviceQueueComp.device, testBuffer, NULL);
    vkFreeMemory(comps->deviceQueueComp.device, testMemory, NULL);

    unInitVulkan();
    printf("Memory Allocation test passed successfully!\n");
    return 0;
}
