/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Slot-indexed / streamed / cull GPU buffer types. Domain fragment of structs.h, included after its base defines.

#ifndef ANO_BUFFER_TYPES_H
#define ANO_BUFFER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/gpu_alloc.h"

typedef struct TransformBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    mat4*           mapped[MAX_FRAMES_IN_FLIGHT];  // host ptr when HOST_VISIBLE; else NULL
    uint32_t        capacity;   // element ceiling
    uint32_t        count;      // live element count (transforms: mirrored from slotHighWater)
} TransformBuffer;

// Unused host-mapped FIF shape. Live motion descriptors: SlotUpload (update.comp).
typedef struct MotionBuffer
{
    VkBuffer             buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation        allocs[MAX_FRAMES_IN_FLIGHT];
    AnoMotionDescriptor* mapped[MAX_FRAMES_IN_FLIGHT];  // 48 B/slot, AnoMotionType + p0/p1
    uint32_t             capacity;
    uint32_t             count;
} MotionBuffer;

// Unused host-mapped FIF shape. Live instance channel: SlotUpload (fragment stage).
typedef struct InstanceDataBuffer
{
    VkBuffer         buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation    allocs[MAX_FRAMES_IN_FLIGHT];
    AnoInstanceData* mapped[MAX_FRAMES_IN_FLIGHT];  // packed[0]=tint, packed[1]=flags, ...
    uint32_t         capacity;
    uint32_t         count;
} InstanceDataBuffer;

// Sentinel slot for a streamed entry whose render_id failed to resolve; scatter.comp skips it.
#define STREAM_SLOT_SKIP 0xFFFFFFFFu

// Streamed-transform lane: SPSC mapped ring. Producer publishes {seq,count}; scatter reads via dyn offset.
// Re-resolve render_id -> slot when resolveGen bumps.
typedef struct TransformStreamBuffer
{
    // Resolved target slots, render-written per frame (scatter binding 0). Unresolved -> STREAM_SLOT_SKIP.
    VkBuffer      slotBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation slotAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*     slotMapped[MAX_FRAMES_IN_FLIGHT];   // [capacity] resolved render slots

    // Producer-written transform ring (scatter binding 1, STORAGE_BUFFER_DYNAMIC); ringSlices slices of capacity mat4s.
    VkBuffer      xformRing;
    GpuAllocation xformRingAlloc;
    mat4*         xformRingMapped;                     // [ringSlices * capacity]

    // Producer-written render_id ring, parallel to xformRing (CPU-only, render-heap).
    uint32_t*     idRing;                              // [ringSlices * capacity]

    uint32_t      capacity;                            // STREAM_CAPACITY, entries per slice
    uint32_t      ringSlices;                          // R = MAX_FRAMES_IN_FLIGHT + 2
    VkDeviceSize  sliceStride;                         // capacity * sizeof(mat4), xform dynamic-offset unit

    uint32_t      count[MAX_FRAMES_IN_FLIGHT];         // scatter dispatch count per frame
    uint32_t      dynOffset[MAX_FRAMES_IN_FLIGHT];     // xformRing dynamic offset (bytes) per frame

    // Lock-free SPSC lifetime control. Seqs <= reclaimSeq are GPU-done and reusable.
    uint64_t          produceSeq;                      // producer thread only
    _Atomic uint64_t  reclaimSeq;                      // consumer -> producer
    uint64_t          curSeq;                          // render side: latest published seq (0 = none)
    uint32_t          curCount;                        // render side: entries in curSeq's slice
    uint64_t          frameSeq[MAX_FRAMES_IN_FLIGHT];  // seq each in-flight frame submitted

    // Resolve gen-tracking; frame re-resolves idRing -> slotMapped when its stagedGen lags.
    uint32_t      resolveGen;
    uint32_t      stagedGen[MAX_FRAMES_IN_FLIGHT];
} TransformStreamBuffer;

// Per-slot DEVICE_LOCAL + per-frame host-visible delta staging. render_apply_commands packs staging[f]; recordCommandBuffer uploads staging[f]->device under barrier. Growth copies device-side under idle.
typedef struct SlotUpload
{
    VkBuffer        device;                            // ×1 DEVICE_LOCAL authoritative (GPU reads this)
    GpuAllocation   deviceAlloc;
    uint32_t        capacity;                          // device elements
    uint32_t        count;                             // live element count (0/unused when slot-indexed)
    uint32_t        stride;                            // bytes per element

    VkBuffer        staging[MAX_FRAMES_IN_FLIGHT];     // host-visible delta source, per frame
    GpuAllocation   stagingAllocs[MAX_FRAMES_IN_FLIGHT];
    void*           stagingMapped[MAX_FRAMES_IN_FLIGHT];
    VkBufferCopy*   regions[MAX_FRAMES_IN_FLIGHT];     // [stagingCap] dst regions queued this frame
    uint32_t        staged[MAX_FRAMES_IN_FLIGHT];      // entries queued for frame f (reset after flush)
    uint32_t        stagingCap;                        // entries per staging buffer (grows on demand)
    bool            computeShared;                     // device buffer CONCURRENT gfx+compute
} SlotUpload;


// One frustum to cull against. std140: mat4 (64) + vec4[6] (96) = 160, packs with no padding.
// Member offsets are 16-multiples as std140 needs, but the struct's own 16-byte alignment is
// unenforced (_Alignof(mat4) == _Alignof(Vector4) == 4) and holds only because every instance
// lives in mapped device memory 〜 docs/BUGS.md anoptic_math.h:21.
typedef struct CullView
{
    mat4    viewProj;
    Vector4 frustumPlanes[6];
} CullView;

typedef struct CullUBO
{
    // Per-view frustums. cull.comp writes survivors of views[v] into partition (v * drawSlotCount + slot).
    CullView views[ANO_VIEW_COUNT];
    uint32_t viewCount;     // active views this frame (<= ANO_VIEW_COUNT)
    uint32_t entityCount;   // active entity slots in [0, slotHighWater)
    uint32_t maxEntities;   // per-partition capacity (== indirectBuffer.capacity)
    uint32_t drawSlotCount; // drawing-partition count, partition stride per view
    // PipelineType -> draw-partition index (ANO_NO_DRAW_SLOT if it never draws). std140: uint32_t[16] == uvec4[4].
    uint32_t drawSlotOf[16];
    // Special draw slots (ano_draw_slot_of): [0] additive, [1] transmission, [2] masked. std140 uvec4.
    uint32_t specialSlots[4];
    // Per-view screen-area cull knobs. One vec4/view, packed to mirror GLSL vec4[]. Per view:
    //   [0] screenAreaScale  = |proj[1][1]| * 0.5 * screenHeight; rpx = worldRadius * scale / dist.
    //   [1] pixelThresholdSq = (min drawn pixel radius)^2; drop iff rpx^2 < this. 0 disables.
    //   [2] lodThresholdPx   = rpx at which LOD level 1 begins. 0 disables LOD.
    //   [3] lodBias          = signed LOD-level bias as float; + coarser.
    float    viewCullParams[ANO_VIEW_COUNT][4];
    // Shadow LOD relative bias vs view 0 (+ = coarser).
    int32_t  shadowLodBias;
    int32_t  _hizPad[3];   // std140 pad to offset 464
    // Hi-Z (single-phase). prevViewProj reprojects bounds into last frame's screen.
    // hizParams={baseW,baseH,mipCount,pad} (0=off); hizProj={p00,p11,p22,p32}.
    mat4     prevViewProj[ANO_VIEW_COUNT];
    float    hizParams[ANO_VIEW_COUNT][4];
    float    hizProj[ANO_VIEW_COUNT][4];
    // taskParams[0]!=0 -> mesh-path indirect = ceil(meshletCount/32) TASK WGs. Mirrors RendererState.taskCull. std140 uvec4.
    uint32_t taskParams[4];
} CullUBO;

// std140 layout guards for the Hi-Z tail; pin C offsets to the SPIR-V offsets.
_Static_assert(offsetof(CullUBO, prevViewProj) == 464, "CullUBO.prevViewProj must be std140 offset 464");
_Static_assert(offsetof(CullUBO, hizParams)    == 592, "CullUBO.hizParams must be std140 offset 592");
_Static_assert(offsetof(CullUBO, hizProj)      == 624, "CullUBO.hizProj must be std140 offset 624");


typedef struct CullUboBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    CullUBO*        mapped[MAX_FRAMES_IN_FLIGHT];
} CullUboBuffer;

typedef struct IndirectDrawBuffer
{
    VkBuffer                            buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation                       allocs[MAX_FRAMES_IN_FLIGHT];
    VkDrawMeshTasksIndirectCommandEXT*  mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t                            capacity;
    uint32_t                            drawCount[MAX_FRAMES_IN_FLIGHT];
} IndirectDrawBuffer;

typedef enum DeletionResourceType {
    RESOURCE_TYPE_GEOMETRY_MESH,
    RESOURCE_TYPE_BINDLESS_TEXTURE
} DeletionResourceType;

typedef struct DeletionTask {
    DeletionResourceType type;
    uint32_t handle;
} DeletionTask;

typedef struct DeletionQueue {
    DeletionTask* tasks;
    uint32_t count;
    uint32_t capacity;
} DeletionQueue;

typedef struct CullingBuffers {
    CullUboBuffer           ubo;

    // Per-slot mesh/material; ×1 device-local + delta staging.
    SlotUpload              entity;

    // MeshSSBO: 9 u32/mesh (meshlets + classic fallback + lodCount); host-visible, rewritten each frame
    VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];

    // MeshBoundsSSBO: vec4 sphere (xyz + radius) per mesh; host-visible, rewritten each frame
    VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written per-partition draw counts (DEVICE_LOCAL; cull atomics)
    VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written compacted entity indices per partition (DEVICE_LOCAL)
    VkBuffer                compactedEntityIndicesBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           compactedEntityIndicesAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               compactedEntityIndicesMapped[MAX_FRAMES_IN_FLIGHT];

    // Per-draw depth keys for the transparency sort. GPU-private, ANO_VIEW_COUNT*maxEntities floats.
    VkBuffer                sortKeysBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           sortKeysAllocs[MAX_FRAMES_IN_FLIGHT];

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;

    // Capacity tracking
    uint32_t                maxEntities;
} CullingBuffers;

#endif // ANO_BUFFER_TYPES_H
