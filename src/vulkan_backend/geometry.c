/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include "vulkan_backend/geometry.h"
#include <string.h>
#include <stdio.h>

bool ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device, uint32_t graphicsFamily, uint32_t transferFamily)
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
    
    pool->vertexCapacity = vertexPoolSize;
    pool->indexCapacity = indexPoolSize;

    uint32_t queueFamilyIndices[] = {graphicsFamily, transferFamily};
    bool concurrent = (graphicsFamily != transferFamily);

    VkBufferCreateInfo vInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertexPoolSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? 2 : 0,
        .pQueueFamilyIndices = concurrent ? queueFamilyIndices : NULL
    };
    vkCreateBuffer(device, &vInfo, NULL, &pool->vertexBuffer);
    VkMemoryRequirements vReqs;
    vkGetBufferMemoryRequirements(device, pool->vertexBuffer, &vReqs);
    pool->vertexAlloc = gpu_alloc(alloc, vReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (pool->vertexAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE; // atomic rollback: null handle (cleanup guards on it) + free meshes
        free(pool->meshes);
        pool->meshes = NULL;
        return false;
    }
    vkBindBufferMemory(device, pool->vertexBuffer, pool->vertexAlloc.memory, pool->vertexAlloc.offset);

    VkBufferCreateInfo iInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = indexPoolSize,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? 2 : 0,
        .pQueueFamilyIndices = concurrent ? queueFamilyIndices : NULL
    };
    vkCreateBuffer(device, &iInfo, NULL, &pool->indexBuffer);
    VkMemoryRequirements iReqs;
    vkGetBufferMemoryRequirements(device, pool->indexBuffer, &iReqs);
    pool->indexAlloc = gpu_alloc(alloc, iReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (pool->indexAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pool->vertexBuffer, NULL);
        vkDestroyBuffer(device, pool->indexBuffer, NULL);
        pool->vertexBuffer = VK_NULL_HANDLE; // atomic rollback: null both handles + free meshes
        pool->indexBuffer = VK_NULL_HANDLE;
        free(pool->meshes);
        pool->meshes = NULL;
        return false;
    }
    vkBindBufferMemory(device, pool->indexBuffer, pool->indexAlloc.memory, pool->indexAlloc.offset);

    return true;
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
                              uint32_t transferFamily, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint32_t* indices, uint32_t indexCount)
{
    // Wait for any in-flight draws to complete before mutating shared device-local buffers
    vkDeviceWaitIdle(device);

    // Build meshlets and calculate bounds on the host
    size_t max_meshlets = ano_build_meshlets_bound(indexCount, 64, 126);
    if (max_meshlets == 0) return -1;

    ano_meshlet_t* meshlets = (ano_meshlet_t*)malloc(max_meshlets * sizeof(ano_meshlet_t));
    uint32_t* meshlet_vertices = (uint32_t*)malloc(max_meshlets * 64 * sizeof(uint32_t));
    uint8_t* meshlet_triangles = (uint8_t*)malloc(max_meshlets * 126 * 3 * sizeof(uint8_t));
    ano_meshlet_bounds_gpu_t* bounds = (ano_meshlet_bounds_gpu_t*)malloc(max_meshlets * sizeof(ano_meshlet_bounds_gpu_t));

    size_t meshlet_count = ano_build_meshlets(
        meshlets,
        meshlet_vertices,
        meshlet_triangles,
        indices,
        indexCount,
        64,
        126
    );

    if (meshlet_count == 0) {
        free(meshlets);
        free(meshlet_vertices);
        free(meshlet_triangles);
        free(bounds);
        return -1;
    }

    size_t unique_vertex_count = meshlets[meshlet_count - 1].vertex_offset + meshlets[meshlet_count - 1].vertex_count;
    size_t local_indices_count = meshlets[meshlet_count - 1].triangle_offset + meshlets[meshlet_count - 1].triangle_count * 3;

    for (size_t p = 0; p < meshlet_count; ++p) {
        bounds[p] = ano_compute_meshlet_bounds(
            meshlet_vertices + meshlets[p].vertex_offset,
            meshlet_triangles + meshlets[p].triangle_offset,
            meshlets[p].triangle_count,
            (const float*)vertices,
            vertexCount,
            sizeof(Vertex)
        );
    }

    VkDeviceSize meshlets_size = meshlet_count * sizeof(ano_meshlet_t);
    VkDeviceSize unique_vertices_size = unique_vertex_count * sizeof(uint32_t);
    VkDeviceSize local_triangles_size = (local_indices_count * sizeof(uint8_t) + 3) & ~3;
    VkDeviceSize bounds_size = meshlet_count * sizeof(ano_meshlet_bounds_gpu_t);

    VkDeviceSize total_metadata_size = meshlets_size + unique_vertices_size + local_triangles_size + bounds_size;

    VkDeviceSize vertexSize = sizeof(Vertex) * vertexCount;
    VkDeviceSize totalSize = vertexSize + total_metadata_size;

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkBuffer stagingBuffer;
    if (vkCreateBuffer(device, &stagingInfo, NULL, &stagingBuffer) != VK_SUCCESS) {
        free(meshlets);
        free(meshlet_vertices);
        free(meshlet_triangles);
        free(bounds);
        return -1;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

    GpuAllocation stagingAlloc = gpu_alloc(alloc, memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingBuffer, NULL);
        free(meshlets);
        free(meshlet_vertices);
        free(meshlet_triangles);
        free(bounds);
        return -1;
    }
    vkBindBufferMemory(device, stagingBuffer, stagingAlloc.memory, stagingAlloc.offset);

    // Copy vertex and metadata data into the staging buffer
    char* mapped = (char*)stagingAlloc.mapped;
    memcpy(mapped, vertices, vertexSize);
    
    char* meta_ptr = mapped + vertexSize;
    memcpy(meta_ptr, meshlets, meshlets_size);
    memcpy(meta_ptr + meshlets_size, meshlet_vertices, unique_vertices_size);
    memcpy(meta_ptr + meshlets_size + unique_vertices_size, meshlet_triangles, local_indices_count);
    if (local_triangles_size > local_indices_count) {
        memset(meta_ptr + meshlets_size + unique_vertices_size + local_indices_count, 0, local_triangles_size - local_indices_count);
    }
    memcpy(meta_ptr + meshlets_size + unique_vertices_size + local_triangles_size, bounds, bounds_size);

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = transferFamily
    };
    VkCommandPool transientPool;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &transientPool) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, NULL);
        free(meshlets);
        free(meshlet_vertices);
        free(meshlet_triangles);
        free(bounds);
        return -1;
    }

    // Record commands
    VkCommandBufferAllocateInfo allocCmdInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = transientPool,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocCmdInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Plan allocations
    uint32_t finalVertexOffset = (uint32_t)-1;
    int vertexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->vertexFreeCount; i++) {
        if (pool->vertexFreeBlocks[i].size >= vertexSize) {
            finalVertexOffset = pool->vertexFreeBlocks[i].offset;
            vertexFreeIdx = (int)i;
            break;
        }
    }
    if (finalVertexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->vertexWriteOffset + vertexSize > pool->vertexCapacity) {
            printf("Error: Geometry mega-buffer vertex pool exhausted! Requested %llu, Capacity %llu\n",
                   (unsigned long long)(pool->vertexWriteOffset + vertexSize), (unsigned long long)pool->vertexCapacity);
            vkDestroyBuffer(device, stagingBuffer, NULL);
            vkDestroyCommandPool(device, transientPool, NULL);
            free(meshlets);
            free(meshlet_vertices);
            free(meshlet_triangles);
            free(bounds);
            return 0; // Return fallback mesh
        }
        finalVertexOffset = pool->vertexWriteOffset;
    }

    uint32_t finalIndexOffset = (uint32_t)-1;
    int indexFreeIdx = -1;
    for (uint32_t i = 0; i < pool->indexFreeCount; i++) {
        if (pool->indexFreeBlocks[i].size >= total_metadata_size) {
            finalIndexOffset = pool->indexFreeBlocks[i].offset;
            indexFreeIdx = (int)i;
            break;
        }
    }
    if (finalIndexOffset == (uint32_t)-1) {
        if ((VkDeviceSize)pool->indexWriteOffset + total_metadata_size > pool->indexCapacity) {
            printf("Error: Geometry mega-buffer metadata pool exhausted! Requested %llu, Capacity %llu\n",
                   (unsigned long long)(pool->indexWriteOffset + total_metadata_size), (unsigned long long)pool->indexCapacity);
            vkDestroyBuffer(device, stagingBuffer, NULL);
            vkDestroyCommandPool(device, transientPool, NULL);
            free(meshlets);
            free(meshlet_vertices);
            free(meshlet_triangles);
            free(bounds);
            return 0; // Return fallback mesh
        }
        finalIndexOffset = pool->indexWriteOffset;
    }

    // Both fit — now commit the reservations.
    if (vertexFreeIdx >= 0) {
        pool->vertexFreeBlocks[vertexFreeIdx].offset += vertexSize;
        pool->vertexFreeBlocks[vertexFreeIdx].size -= vertexSize;
        if (pool->vertexFreeBlocks[vertexFreeIdx].size == 0) {
            pool->vertexFreeBlocks[vertexFreeIdx] = pool->vertexFreeBlocks[--pool->vertexFreeCount];
        }
    } else {
        pool->vertexWriteOffset += vertexSize;
    }
    if (indexFreeIdx >= 0) {
        pool->indexFreeBlocks[indexFreeIdx].offset += total_metadata_size;
        pool->indexFreeBlocks[indexFreeIdx].size -= total_metadata_size;
        if (pool->indexFreeBlocks[indexFreeIdx].size == 0) {
            pool->indexFreeBlocks[indexFreeIdx] = pool->indexFreeBlocks[--pool->indexFreeCount];
        }
    } else {
        pool->indexWriteOffset += total_metadata_size;
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
        .size = total_metadata_size
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, pool->indexBuffer, 1, &indexCopyRegion); // Copy metadata

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };
    
    VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(device, &fenceInfo, NULL, &fence);

    vkQueueSubmit(transferQueue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, NULL);
    vkFreeCommandBuffers(device, transientPool, 1, &cmd);
    vkDestroyCommandPool(device, transientPool, NULL);

    // Cleanup staging buffer and host arrays
    vkDestroyBuffer(device, stagingBuffer, NULL);
    free(meshlets);
    free(meshlet_vertices);
    free(meshlet_triangles);
    free(bounds);

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
    mesh->vertexCount = vertexCount;
    mesh->indexOffset = finalIndexOffset;
    mesh->indexCount = (uint32_t)total_metadata_size;
    mesh->meshletOffset = finalIndexOffset;
    mesh->meshletCount = (uint32_t)meshlet_count;
    mesh->uniqueVerticesOffset = finalIndexOffset + (uint32_t)meshlets_size;
    mesh->trianglesOffset = finalIndexOffset + (uint32_t)(meshlets_size + unique_vertices_size);
    mesh->boundsOffset = finalIndexOffset + (uint32_t)(meshlets_size + unique_vertices_size + local_triangles_size);

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
            .size = mesh->indexCount
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
