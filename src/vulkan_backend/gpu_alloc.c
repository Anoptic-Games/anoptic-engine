#include "gpu_alloc.h"
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_BLOCK_SIZE (256 * 1024 * 1024) // 256 MiB

static uint32_t findMemoryType(VkPhysicalDeviceMemoryProperties memProps, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    printf("Failed to find suitable memory type!\n");
    return UINT32_MAX;
}

GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    uint32_t memoryType = findMemoryType(alloc->memProps, reqs.memoryTypeBits, props);
    if (memoryType == UINT32_MAX) {
        GpuAllocation empty = {0};
        return empty;
    }
    
    // Find an existing block with enough space and matching type
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        GpuBlock* block = &alloc->blocks[i];
        if (block->memoryType == memoryType)
        {
            // Align offset
            VkDeviceSize alignedOffset = (block->offset + reqs.alignment - 1) & ~(reqs.alignment - 1);
            if (alignedOffset + reqs.size <= block->size)
            {
                block->offset = alignedOffset + reqs.size;
                
                GpuAllocation allocation = {
                    .memory = block->memory,
                    .offset = alignedOffset,
                    .size = reqs.size,
                    .mapped = block->mapped ? (void*)((char*)block->mapped + alignedOffset) : NULL
                };
                return allocation;
            }
        }
    }

    // Need a new block
    VkDeviceSize blockSize = reqs.size > DEFAULT_BLOCK_SIZE ? reqs.size : DEFAULT_BLOCK_SIZE;
    
    // Expand blocks array
    void* temp = realloc(alloc->blocks, (alloc->blockCount + 1) * sizeof(GpuBlock));
    if (!temp) {
        printf("Host OOM: Failed to allocate memory for GPU block tracking array!\n");
        return (GpuAllocation){0};
    }
    alloc->blocks = temp;
    GpuBlock* newBlock = &alloc->blocks[alloc->blockCount];
    alloc->blockCount++;

    newBlock->size = blockSize;
    newBlock->offset = 0;
    newBlock->memoryType = memoryType;
    newBlock->mapped = NULL;

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = blockSize,
        .memoryTypeIndex = memoryType
    };

    if (vkAllocateMemory(alloc->device, &allocInfo, NULL, &newBlock->memory) != VK_SUCCESS)
    {
        printf("Failed to allocate GPU block memory!\n");
        // Revert block count expansion
        alloc->blockCount--;
        GpuAllocation empty = {0};
        return empty;
    }

    if (alloc->memProps.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        vkMapMemory(alloc->device, newBlock->memory, 0, blockSize, 0, &newBlock->mapped);
    }

    VkDeviceSize alignedOffset = (newBlock->offset + reqs.alignment - 1) & ~(reqs.alignment - 1);
    newBlock->offset = alignedOffset + reqs.size;

    GpuAllocation allocation = {
        .memory = newBlock->memory,
        .offset = alignedOffset,
        .size = reqs.size,
        .mapped = newBlock->mapped ? (void*)((char*)newBlock->mapped + alignedOffset) : NULL
    };
    return allocation;
}

void gpu_alloc_reset(GpuAllocator* alloc)
{
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        alloc->blocks[i].offset = 0;
    }
}

void gpu_alloc_destroy(GpuAllocator* alloc)
{
    for (uint32_t i = 0; i < alloc->blockCount; i++)
    {
        if (alloc->blocks[i].mapped)
        {
            vkUnmapMemory(alloc->device, alloc->blocks[i].memory);
        }
        vkFreeMemory(alloc->device, alloc->blocks[i].memory, NULL);
    }
    free(alloc->blocks);
    alloc->blocks = NULL;
    alloc->blockCount = 0;
}
