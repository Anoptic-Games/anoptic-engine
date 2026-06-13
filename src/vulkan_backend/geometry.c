/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include "vulkan_backend/geometry.h"
#include <string.h>
#include <stdio.h>

void ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device)
{
    pool->meshCount = 0;
    pool->meshCapacity = 100;
    pool->meshes = calloc(pool->meshCapacity, sizeof(MeshRegion));
    pool->vertexWriteOffset = 0;
    pool->indexWriteOffset = 0;
    
    pool->vertexFreeBlocks = NULL;
    pool->vertexFreeCount = 0;
    pool->vertexFreeCapacity = 0;
    
    pool->indexFreeBlocks = NULL;
    pool->indexFreeCount = 0;
    pool->indexFreeCapacity = 0;
    
    pool->freeMeshIndices = NULL;
    pool->freeMeshIndexCount = 0;
    pool->freeMeshIndexCapacity = 0;

    VkDeviceSize vertexPoolSize = 64 * 1024 * 1024; // 64 MB
    VkDeviceSize indexPoolSize = 16 * 1024 * 1024;  // 16 MB

    VkBufferCreateInfo vInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertexPoolSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(device, &vInfo, NULL, &pool->vertexBuffer);
    VkMemoryRequirements vReqs;
    vkGetBufferMemoryRequirements(device, pool->vertexBuffer, &vReqs);
    pool->vertexAlloc = gpu_alloc(alloc, vReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(device, pool->vertexBuffer, pool->vertexAlloc.memory, pool->vertexAlloc.offset);

    VkBufferCreateInfo iInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = indexPoolSize,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(device, &iInfo, NULL, &pool->indexBuffer);
    VkMemoryRequirements iReqs;
    vkGetBufferMemoryRequirements(device, pool->indexBuffer, &iReqs);
    pool->indexAlloc = gpu_alloc(alloc, iReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindBufferMemory(device, pool->indexBuffer, pool->indexAlloc.memory, pool->indexAlloc.offset);
}

void ano_vk_cleanup_geometry_pool(GeometryPool* pool, VkDevice device)
{
    if (pool->vertexBuffer) vkDestroyBuffer(device, pool->vertexBuffer, NULL);
    if (pool->indexBuffer) vkDestroyBuffer(device, pool->indexBuffer, NULL);
    if (pool->meshes) free(pool->meshes);
    pool->meshes = NULL;
    pool->meshCount = 0;
    pool->meshCapacity = 0;
    
    if (pool->vertexFreeBlocks) free(pool->vertexFreeBlocks);
    pool->vertexFreeBlocks = NULL;
    
    if (pool->indexFreeBlocks) free(pool->indexFreeBlocks);
    pool->indexFreeBlocks = NULL;
    
    if (pool->freeMeshIndices) free(pool->freeMeshIndices);
    pool->freeMeshIndices = NULL;
}

uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                              VkCommandPool cmdPool, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint16_t* indices, uint32_t indexCount)
{
    // Need a staging buffer
    VkDeviceSize vertexSize = sizeof(Vertex) * vertexCount;
    VkDeviceSize indexSize = sizeof(uint16_t) * indexCount;
    VkDeviceSize totalSize = vertexSize + indexSize;

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer stagingBuffer;
    if (vkCreateBuffer(device, &stagingInfo, NULL, &stagingBuffer) != VK_SUCCESS) return -1;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

    GpuAllocation stagingAlloc = gpu_alloc(alloc, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(device, stagingBuffer, stagingAlloc.memory, stagingAlloc.offset);

    // Copy data
    char* mapped = (char*)stagingAlloc.mapped;
    memcpy(mapped, vertices, vertexSize);
    memcpy(mapped + vertexSize, indices, indexSize);

    // Record commands
    VkCommandBufferAllocateInfo allocCmdInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = cmdPool,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocCmdInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Find free blocks or use write offset
    uint32_t finalVertexOffset = (uint32_t)-1;
    for (uint32_t i = 0; i < pool->vertexFreeCount; i++) {
        if (pool->vertexFreeBlocks[i].size >= vertexSize) {
            finalVertexOffset = pool->vertexFreeBlocks[i].offset;
            pool->vertexFreeBlocks[i].offset += vertexSize;
            pool->vertexFreeBlocks[i].size -= vertexSize;
            if (pool->vertexFreeBlocks[i].size == 0) {
                pool->vertexFreeBlocks[i] = pool->vertexFreeBlocks[--pool->vertexFreeCount];
            }
            break;
        }
    }
    if (finalVertexOffset == (uint32_t)-1) {
        finalVertexOffset = pool->vertexWriteOffset;
        pool->vertexWriteOffset += vertexSize;
    }

    uint32_t finalIndexOffset = (uint32_t)-1;
    for (uint32_t i = 0; i < pool->indexFreeCount; i++) {
        if (pool->indexFreeBlocks[i].size >= indexSize) {
            finalIndexOffset = pool->indexFreeBlocks[i].offset;
            pool->indexFreeBlocks[i].offset += indexSize;
            pool->indexFreeBlocks[i].size -= indexSize;
            if (pool->indexFreeBlocks[i].size == 0) {
                pool->indexFreeBlocks[i] = pool->indexFreeBlocks[--pool->indexFreeCount];
            }
            break;
        }
    }
    if (finalIndexOffset == (uint32_t)-1) {
        finalIndexOffset = pool->indexWriteOffset;
        pool->indexWriteOffset += indexSize;
    }

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = finalVertexOffset,
        .size = vertexSize
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, pool->vertexBuffer, 1, &copyRegion);

    VkBufferCopy indexCopyRegion = {
        .srcOffset = vertexSize,
        .dstOffset = finalIndexOffset,
        .size = indexSize
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, pool->indexBuffer, 1, &indexCopyRegion);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };
    
    // In a real optimized system we use a Fence, but for simple init a WaitIdle is fine. 
    // The plan specifies replacing waitIdle with a Fence, let's do it!
    VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(device, &fenceInfo, NULL, &fence);

    vkQueueSubmit(transferQueue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, NULL);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);

    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, NULL);
    // Note: gpu_alloc doesn't have an individual free. It's an arena. 
    // This is a slight leak in the host visible block, but we will fix that by using a dedicated resetable block or ring buffer later.

    // Register mesh
    uint32_t finalMeshIndex = (uint32_t)-1;
    if (pool->freeMeshIndexCount > 0) {
        finalMeshIndex = pool->freeMeshIndices[--pool->freeMeshIndexCount];
    } else {
        if (pool->meshCount >= pool->meshCapacity) {
            pool->meshCapacity = pool->meshCapacity == 0 ? 100 : pool->meshCapacity * 2;
            pool->meshes = realloc(pool->meshes, pool->meshCapacity * sizeof(MeshRegion));
        }
        finalMeshIndex = pool->meshCount++;
    }

    MeshRegion* mesh = &pool->meshes[finalMeshIndex];
    mesh->vertexOffset = finalVertexOffset;
    mesh->indexOffset = finalIndexOffset;
    mesh->indexCount = indexCount;
    mesh->vertexCount = vertexCount;
    mesh->baseVertex = finalVertexOffset / sizeof(Vertex);

    // Calculate bounding sphere
    Vector3 minBounds = vertices[0].position;
    Vector3 maxBounds = vertices[0].position;
    for (uint32_t i = 1; i < vertexCount; i++) {
        if (vertices[i].position.v[0] < minBounds.v[0]) minBounds.v[0] = vertices[i].position.v[0];
        if (vertices[i].position.v[1] < minBounds.v[1]) minBounds.v[1] = vertices[i].position.v[1];
        if (vertices[i].position.v[2] < minBounds.v[2]) minBounds.v[2] = vertices[i].position.v[2];
        if (vertices[i].position.v[0] > maxBounds.v[0]) maxBounds.v[0] = vertices[i].position.v[0];
        if (vertices[i].position.v[1] > maxBounds.v[1]) maxBounds.v[1] = vertices[i].position.v[1];
        if (vertices[i].position.v[2] > maxBounds.v[2]) maxBounds.v[2] = vertices[i].position.v[2];
    }
    
    mesh->boundingSphereCenter[0] = (minBounds.v[0] + maxBounds.v[0]) * 0.5f;
    mesh->boundingSphereCenter[1] = (minBounds.v[1] + maxBounds.v[1]) * 0.5f;
    mesh->boundingSphereCenter[2] = (minBounds.v[2] + maxBounds.v[2]) * 0.5f;

    float maxDistSq = 0.0f;
    for (uint32_t i = 0; i < vertexCount; i++) {
        float dx = vertices[i].position.v[0] - mesh->boundingSphereCenter[0];
        float dy = vertices[i].position.v[1] - mesh->boundingSphereCenter[1];
        float dz = vertices[i].position.v[2] - mesh->boundingSphereCenter[2];
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > maxDistSq) maxDistSq = distSq;
    }
    mesh->boundingSphereRadius = sqrtf(maxDistSq);

    return finalMeshIndex;
}

void geometry_pool_free(GeometryPool* pool, uint32_t meshIndex)
{
    if (meshIndex >= pool->meshCount || meshIndex == 0) return; // Don't free fallback or out of bounds

    MeshRegion* mesh = &pool->meshes[meshIndex];
    if (mesh->vertexCount == 0) return; // Already freed

    // Add to vertex free list
    if (pool->vertexFreeCount >= pool->vertexFreeCapacity) {
        pool->vertexFreeCapacity = pool->vertexFreeCapacity == 0 ? 32 : pool->vertexFreeCapacity * 2;
        pool->vertexFreeBlocks = realloc(pool->vertexFreeBlocks, pool->vertexFreeCapacity * sizeof(GeoFreeBlock));
    }
    pool->vertexFreeBlocks[pool->vertexFreeCount++] = (GeoFreeBlock){
        .offset = mesh->vertexOffset,
        .size = mesh->vertexCount * sizeof(Vertex)
    };

    // Add to index free list
    if (mesh->indexCount > 0) {
        if (pool->indexFreeCount >= pool->indexFreeCapacity) {
            pool->indexFreeCapacity = pool->indexFreeCapacity == 0 ? 32 : pool->indexFreeCapacity * 2;
            pool->indexFreeBlocks = realloc(pool->indexFreeBlocks, pool->indexFreeCapacity * sizeof(GeoFreeBlock));
        }
        pool->indexFreeBlocks[pool->indexFreeCount++] = (GeoFreeBlock){
            .offset = mesh->indexOffset,
            .size = mesh->indexCount * sizeof(uint16_t)
        };
    }

    // Add mesh index to free list
    if (pool->freeMeshIndexCount >= pool->freeMeshIndexCapacity) {
        pool->freeMeshIndexCapacity = pool->freeMeshIndexCapacity == 0 ? 32 : pool->freeMeshIndexCapacity * 2;
        pool->freeMeshIndices = realloc(pool->freeMeshIndices, pool->freeMeshIndexCapacity * sizeof(uint32_t));
    }
    pool->freeMeshIndices[pool->freeMeshIndexCount++] = meshIndex;

    // Clear mesh
    memset(mesh, 0, sizeof(MeshRegion));
}
