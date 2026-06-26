/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <vulkan/vulkan.h>
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/vertex/vertex.h"
#include "mesh/ano_meshoptimizer.h"

typedef struct MeshRegion
{
    uint32_t    vertexOffset;   // byte offset into the vertex mega-buffer
    uint32_t    vertexCount;    // number of vertices
    uint32_t    indexOffset;    // byte offset of metadata block in index buffer
    uint32_t    indexCount;     // total metadata size in bytes (used for freeing)
    uint32_t    meshletOffset;  // byte offset of meshlets in index buffer
    uint32_t    meshletCount;   // number of meshlets
    uint32_t    uniqueVerticesOffset; // byte offset of unique vertices in index buffer
    uint32_t    trianglesOffset; // byte offset of local triangles in index buffer
    uint32_t    boundsOffset;   // byte offset of bounds in index buffer
    uint32_t    classicIndexOffset; // byte offset of the plain u32 triangle-list index region (fallback path)
    uint32_t    classicIndexCount;  // number of u32 indices in the classic region (fallback path)
    float       boundingSphereCenter[3];
    float       boundingSphereRadius;
    uint32_t    lodCount;           // # contiguous LOD meshes from this base (1 == standalone); cull reads it off the base
} MeshRegion;

typedef struct GeoFreeBlock
{
    uint32_t offset;
    uint32_t size;
} GeoFreeBlock;

typedef struct GeometryPool
{
    GpuAllocation   vertexAlloc;    // sub-allocation from GpuAllocator
    GpuAllocation   indexAlloc;

    VkBuffer        vertexBuffer;   // one buffer, bound to vertexAlloc.memory at vertexAlloc.offset
    VkBuffer        indexBuffer;    // one buffer, bound to indexAlloc.memory at indexAlloc.offset

    uint32_t        vertexWriteOffset;  // current write head (bytes)
    uint32_t        indexWriteOffset;
    
    VkDeviceSize    vertexCapacity;
    VkDeviceSize    indexCapacity;

    GeoFreeBlock*   vertexFreeBlocks;
    uint32_t        vertexFreeCount;
    uint32_t        vertexFreeCapacity;

    GeoFreeBlock*   indexFreeBlocks;
    uint32_t        indexFreeCount;
    uint32_t        indexFreeCapacity;

    uint32_t        meshCount;
    uint32_t        meshCapacity;
    MeshRegion*     meshes;             // registered mesh regions

    uint32_t*       freeMeshIndices;
    uint32_t        freeMeshIndexCount;
    uint32_t        freeMeshIndexCapacity;
} GeometryPool;

bool ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device, uint32_t graphicsFamily, uint32_t transferFamily);
void ano_vk_cleanup_geometry_pool(GeometryPool* pool, VkDevice device);

// Upload mesh data, return a MeshRegion handle (index into meshes[]).
// Data is staged through a staging buffer -> device-local transfer.
uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                              uint32_t transferFamily, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint32_t* indices, uint32_t indexCount);

#define ANO_MAX_LOD 8u

// Per-mesh GPU buffer capacity (MeshSSBO / MeshBoundsSSBO slots), fixed at buffer creation. The host
// geometry pool grows its meshes[] array on demand but must never register past this, or
// updateCullingBuffers would write past the mapped device buffers — the upload paths refuse once the
// pool would exceed it (returning the fallback mesh 0). The cull-set/graphics-set descriptor RANGES
// and the createCullingBuffers buffer sizes must all use this value; keep them in lockstep. With 4x
// LOD chains (ANO_DEFAULT_LOD_COUNT) this covers ~2048 distinct source meshes; ~156 B/slot of VRAM.
#define ANO_MAX_MESHES 8192u

// Default LOD levels glTF uploads request. 1 == today's behavior (a single full-detail mesh, no
// decimation). Raise it (<= ANO_MAX_LOD) to turn LOD chains on engine-wide once decimation quality
// is verified on real assets.
#define ANO_DEFAULT_LOD_COUNT 4u

// LOD chain production config (review 4.9 step 2). lodCount levels are emitted as a contiguous run
// of mesh indices: level 0 is the full mesh, level i is the source decimated to ratios[i] of the
// source index count under targetError. ratios[0] is conventionally 1.0 (level 0 == full).
typedef struct AnoLodConfig
{
    uint32_t lodCount;             // levels to emit (>=1, clamped to ANO_MAX_LOD)
    float    ratios[ANO_MAX_LOD];  // per-level target index fraction of the source (level 0 == 1.0)
    float    targetError;          // ano_simplify relative error budget (fraction of bbox extent)
} AnoLodConfig;

// A sensible default chain: ratios 1, 1/2, 1/4, ... and a 5%-of-extent error budget.
AnoLodConfig ano_lod_config_default(uint32_t lodCount);

// Upload a mesh as a contiguous LOD chain. Produces config->lodCount adjacent mesh regions sharing
// the same vertex data (level i = ano_simplify of the source to ratios[i]); returns the base mesh
// index and writes the count actually produced to *out_lodCount (the chain truncates if a level's
// simplify stalls or a pool is exhausted). Bounds are LOD-invariant (full mesh sphere on every
// level). Returns 0 (fallback) with *out_lodCount == 0 if nothing is produced. out_* may be NULL.
uint32_t geometry_pool_upload_chain(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                                    uint32_t transferFamily, VkQueue transferQueue,
                                    const Vertex* vertices, uint32_t vertexCount,
                                    const uint32_t* indices, uint32_t indexCount,
                                    const AnoLodConfig* config,
                                    uint32_t* out_lodBase, uint32_t* out_lodCount);

// Free a mesh region, adding its memory and index to the free lists
void geometry_pool_free(GeometryPool* pool, uint32_t meshIndex);

// Free a contiguous LOD chain (each level returned to the free lists). Symmetric with upload_chain.
void geometry_pool_free_chain(GeometryPool* pool, uint32_t lodBase, uint32_t lodCount);

#endif
