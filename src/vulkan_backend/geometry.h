/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <vulkan/vulkan.h>
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/vertex/vertex.h"

typedef struct MeshRegion
{
    uint32_t    vertexOffset;   // byte offset into the vertex mega-buffer
    uint32_t    indexOffset;    // byte offset into the index mega-buffer
    uint32_t    indexCount;     // number of indices
    uint32_t    vertexCount;    // number of vertices (for non-indexed draws)
    int32_t     baseVertex;     // added to each index value before fetching
    float       boundingSphereCenter[3];
    float       boundingSphereRadius;
} MeshRegion;

typedef struct GeometryPool
{
    GpuAllocation   vertexAlloc;    // sub-allocation from GpuAllocator
    GpuAllocation   indexAlloc;

    VkBuffer        vertexBuffer;   // one buffer, bound to vertexAlloc.memory at vertexAlloc.offset
    VkBuffer        indexBuffer;    // one buffer, bound to indexAlloc.memory at indexAlloc.offset

    uint32_t        vertexWriteOffset;  // current write head (bytes)
    uint32_t        indexWriteOffset;

    uint32_t        meshCount;
    MeshRegion*     meshes;             // registered mesh regions
} GeometryPool;

void ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device);
void ano_vk_cleanup_geometry_pool(GeometryPool* pool, VkDevice device);

// Upload mesh data, return a MeshRegion handle (index into meshes[]).
// Data is staged through a staging buffer -> device-local transfer.
uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                              VkCommandPool cmdPool, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint16_t* indices, uint32_t indexCount);

#endif
