#ifndef GPU_ALLOC_H
#define GPU_ALLOC_H

#include <vulkan/vulkan.h>

typedef struct GpuBlock
{
    VkDeviceMemory  memory;
    VkDeviceSize    size;
    VkDeviceSize    offset;     // next free offset (bump allocator)
    uint32_t        memoryType;
    void*           mapped;     // persistently mapped if HOST_VISIBLE, else NULL
} GpuBlock;

typedef struct GpuAllocator
{
    VkDevice        device;
    GpuBlock*       blocks;
    uint32_t        blockCount;
    VkPhysicalDeviceMemoryProperties memProps;
} GpuAllocator;

typedef struct GpuAllocation
{
    VkDeviceMemory  memory;     // which block
    VkDeviceSize    offset;     // offset within block
    VkDeviceSize    size;       // allocation size
    void*           mapped;     // mapped pointer + offset, or NULL
} GpuAllocation;

// Allocate a region from the appropriate memory type.
// Alignment is handled internally. Creates a new block if needed.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs,
                        VkMemoryPropertyFlags props);

// Free is deferred — blocks are freed on allocator teardown or explicit reset.
// Individual allocations are not freed (arena semantics).
void gpu_alloc_reset(GpuAllocator* alloc);   // reset all blocks to offset=0
void gpu_alloc_destroy(GpuAllocator* alloc);  // free all VkDeviceMemory

extern GpuAllocator gpuAllocator;
extern GpuAllocator swapchainAllocator;
extern GpuAllocator stagingAllocator;
extern GpuAllocator textureAllocator;

#endif
