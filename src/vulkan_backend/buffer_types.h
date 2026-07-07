/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Slot-indexed / streamed / cull GPU buffer types. A domain fragment of structs.h: it is included
// by structs.h AFTER the base includes + MAX_FRAMES_IN_FLIGHT / ANO_VIEW_COUNT defines, and relies
// on them (mat4, AnoMotionDescriptor, AnoInstanceData, GpuAllocation) — not a standalone header.

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
    mat4*           mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} TransformBuffer;

// Per-slot GPU motion descriptors (type + params) consumed by update.comp to derive
// the live transform from global time. Persistent, slot-indexed, copy-forward.
typedef struct MotionBuffer
{
    VkBuffer             buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation        allocs[MAX_FRAMES_IN_FLIGHT];
    AnoMotionDescriptor* mapped[MAX_FRAMES_IN_FLIGHT];  // 48 B/slot; AnoMotionType + p0/p1
    uint32_t             capacity;
    uint32_t             count;
} MotionBuffer;

// Persistent, slot-indexed, copy-forward-on-grow — same lifecycle class as
// initialTransform/motion. Carries the open-ended per-entity instance
// channel (AnoInstanceData) read by the fragment stage; zero is the inert default.
typedef struct InstanceDataBuffer
{
    VkBuffer         buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation    allocs[MAX_FRAMES_IN_FLIGHT];
    AnoInstanceData* mapped[MAX_FRAMES_IN_FLIGHT];  // packed[0]=tint, packed[1]=flags, ...
    uint32_t         capacity;
    uint32_t         count;
} InstanceDataBuffer;

// Streamed-transform lane (Path B). Two SoA
// staging buffers on their OWN capacity axis (the streamed set is a minority, not the
// entity axis): per-tick render slots + their CPU transforms, scattered into the live
// transform buffer by scatter.comp. Per-frame, host-visible, ephemeral (overwritten
// each tick). count[f] is the number of valid entries staged for frame f.
// Sentinel slot for a streamed entry whose render_id failed to resolve (retired/unknown);
// scatter.comp skips it. The xform ring is producer-laid-out and cannot be compacted, so
// dropped entries stay in place with this slot and are no-ops on the GPU.
#define STREAM_SLOT_SKIP 0xFFFFFFFFu

// Streamed-transform lane (Path B v2 — zero-copy mapped ring). The producer writes its
// render_ids + live transforms straight into a free ring slice and publishes one tiny
// {seq,count} command; scatter reads the slice IN PLACE via a dynamic descriptor offset,
// so the mat4 payload is never copied render-side. Slice lifetime is a lock-free SPSC
// handshake: the producer reserves the next slice only when the consumer's per-frame
// fence has reclaimed the seq that last used it. The render side holds the latest
// published slice (hold-last-value) and re-resolves render_id -> slot into slotMapped
// only when a new publish or a slot retirement bumps resolveGen.
typedef struct TransformStreamBuffer
{
    // Resolved target slots, render-written per frame (scatter binding 0). Unresolved
    // render_ids become STREAM_SLOT_SKIP.
    VkBuffer      slotBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation slotAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*     slotMapped[MAX_FRAMES_IN_FLIGHT];   // [capacity] resolved render slots

    // Producer-written transform ring (scatter binding 1, STORAGE_BUFFER_DYNAMIC): one
    // device buffer of `ringSlices` slices, each `capacity` mat4s; scatter binds the
    // published slice by dynamic offset.
    VkBuffer      xformRing;
    GpuAllocation xformRingAlloc;
    mat4*         xformRingMapped;                     // [ringSlices * capacity]

    // Producer-written render_id ring, parallel to xformRing (CPU-only, render-heap; the
    // render side resolves it into slotMapped — no descriptor).
    uint32_t*     idRing;                              // [ringSlices * capacity]

    uint32_t      capacity;                            // STREAM_CAPACITY: entries per slice
    uint32_t      ringSlices;                          // R = MAX_FRAMES_IN_FLIGHT + 2
    VkDeviceSize  sliceStride;                         // capacity * sizeof(mat4): xform dynamic-offset unit

    uint32_t      count[MAX_FRAMES_IN_FLIGHT];         // scatter dispatch count per frame
    uint32_t      dynOffset[MAX_FRAMES_IN_FLIGHT];     // xformRing dynamic offset (bytes) per frame

    // Lock-free SPSC lifetime control. produceSeq is producer-private (monotonic publish
    // count; slice = (seq-1) % ringSlices). reclaimSeq is consumer-published: all seqs
    // <= reclaimSeq are GPU-done (advanced off the per-frame fence), so the producer may
    // overwrite their slices. curSeq/curCount are the latest published slice the render
    // holds; frameSeq[f] is the seq frame f last submitted (drives reclaim).
    uint64_t          produceSeq;                      // producer thread only
    _Atomic uint64_t  reclaimSeq;                      // consumer -> producer
    uint64_t          curSeq;                          // render side: latest published seq (0 = none)
    uint32_t          curCount;                        // render side: entries in curSeq's slice
    uint64_t          frameSeq[MAX_FRAMES_IN_FLIGHT];  // seq each in-flight frame submitted

    // Resolve gen-tracking: bumped on a new publish or any slot retirement; a frame
    // re-resolves idRing -> slotMapped only when its stagedGen lags.
    uint32_t      resolveGen;
    uint32_t      stagedGen[MAX_FRAMES_IN_FLIGHT];
} TransformStreamBuffer;

// Per-slot GPU data whose AUTHORITATIVE copy is a single DEVICE_LOCAL buffer the GPU reads
// every frame (no triplication, no host-visible hot reads), fed by sparse CPU writes through
// a per-frame host-visible DELTA staging ring. render_apply_commands packs frame f's changed
// elements into staging[f] + a parallel copy-region list; recordCommandBuffer uploads
// staging[f] -> device with one vkCmdCopyBuffer, barrier-ordered after prior frames' shader
// reads (single graphics queue, cross-submission) and before this frame's reads. ONE upload
// suffices: the device buffer is shared by every frame in flight. Growth copies the live span
// old->new device-side under vkDeviceWaitIdle.
typedef struct SlotUpload
{
    VkBuffer        device;                            // ×1 DEVICE_LOCAL authoritative (GPU reads this)
    GpuAllocation   deviceAlloc;
    uint32_t        capacity;                          // device elements
    uint32_t        count;                             // live element count (light palette; 0/unused when slot-indexed)
    uint32_t        stride;                            // bytes per element

    VkBuffer        staging[MAX_FRAMES_IN_FLIGHT];     // host-visible delta source, per frame
    GpuAllocation   stagingAllocs[MAX_FRAMES_IN_FLIGHT];
    void*           stagingMapped[MAX_FRAMES_IN_FLIGHT];
    VkBufferCopy*   regions[MAX_FRAMES_IN_FLIGHT];     // [stagingCap] dst regions queued this frame
    uint32_t        staged[MAX_FRAMES_IN_FLIGHT];      // entries queued for frame f (reset after flush)
    uint32_t        stagingCap;                        // entries per staging buffer (grows on demand)
    bool            computeShared;                     // device buffer CONCURRENT gfx+compute (async light-cull reads it); grow preserves
} SlotUpload;


// One frustum to cull against. std140: mat4 (64) + vec4[6] (96) = 160, 16-aligned, so an
// array of these packs with no padding between elements (matches cull.comp's CullView[]).
typedef struct CullView
{
    mat4    viewProj;
    Vector4 frustumPlanes[6];
} CullView;

typedef struct CullUBO
{
    // Per-view frustums. cull.comp loops v in [0, viewCount) and writes survivors of views[v]
    // into partition (v * drawSlotCount + slot) of the indirect/drawCount/compacted buffers.
    CullView views[ANO_VIEW_COUNT];
    uint32_t viewCount;     // active views this frame (<= ANO_VIEW_COUNT)
    uint32_t entityCount;   // active entity slots in [0, slotHighWater)
    uint32_t maxEntities;   // per-partition capacity (== indirectBuffer.capacity)
    uint32_t drawSlotCount; // drawing-partition count (ano_draw_pipeline_count()); partition stride per view
    // PipelineType -> draw-partition index (ANO_NO_DRAW_SLOT if it never draws). cull.comp
    // compacts visible draws by slot rather than by raw enum value, so the indirect/drawCount/
    // compacted buffers hold only the drawing partitions. Indexed by material pipelineType, so
    // it spans the enum; padded to 16 (uvec4[4] in cull.comp). std140: uint32_t[16] == uvec4[4].
    uint32_t drawSlotOf[16];
    // Special draw slots (ano_draw_slot_of): [0] = additive lane (emits no shadow caster), [1] =
    // transmission lane (the depth-sorted "over" lane), [2] = masked lane (casters emit into the
    // per-frustum MASKED shadow partition at base + FRUSTUM_COUNT + s). ANO_NO_DRAW_SLOT if that
    // lane is absent. std140 uvec4 (16 B contiguous); cull.comp reads specialSlots.x / .y / .z.
    uint32_t specialSlots[4];
    // Per-view screen-area cull knobs (review 4.9 step 1). One vec4 per view, tightly packed like
    // drawSlotOf so C float[ANO_VIEW_COUNT][4] mirrors GLSL vec4[ANO_VIEW_COUNT] with no std140
    // scalar-array padding (a bare float[] pads each element to 16 B and desyncs). Per view:
    //   [v][0] screenAreaScale  = |proj[1][1]| * 0.5 * screenHeight  (cot(fovY/2) * half-height px),
    //                             so projected pixel radius rpx = worldRadius * scale / dist.
    //   [v][1] pixelThresholdSq = (min drawn pixel radius)^2; a draw is dropped iff rpx^2 < this.
    //                             0 disables the test for that view (pre-step-1 behavior).
    //   [v][2] lodThresholdPx   = rpx at which LOD level 1 begins (step 2); 0 disables LOD selection.
    //   [v][3] lodBias          = signed LOD-level bias (debug/tuning), stored as float; + coarser.
    float    viewCullParams[ANO_VIEW_COUNT][4];
    // Shadow LOD offset (review 4.9 step 2, revised): shadow casters select the primary camera view's
    // (view 0) LOD so a caster's shadow silhouette tracks its visible geometry; cull.comp recomputes
    // view 0's screen-area metric per caster and feeds this as an extra RELATIVE bias (0 = exact match,
    // + = coarser), clamped per-entity to its chain length. Trails viewCullParams at a 16-aligned offset;
    // a bare scalar is std140-safe there. Shaders that declare only a CullUBO prefix (tpsort.comp) are
    // unaffected by this tail field.
    int32_t  shadowLodBias;
    int32_t  _hizPad[3];   // std140: align the 16-aligned arrays below to offset 464 (matches cull.comp)
    // Hi-Z occlusion (review 4.9 step 3, single-phase). Per camera view: prevViewProj reprojects this
    // frame's bounds into LAST frame's screen (the pyramid the cull samples was built last frame).
    // hizParams = {baseW, baseH, mipCount, pad}; mipCount==0 disables the test (default off). hizProj =
    // {proj00, proj11, proj22, proj32} of the current projection for screen radius + the ZO nearest-depth.
    mat4     prevViewProj[ANO_VIEW_COUNT];
    float    hizParams[ANO_VIEW_COUNT][4];
    float    hizProj[ANO_VIEW_COUNT][4];
    // Task-shader meshlet cull (review priority 10): [0] != 0 sizes mesh-path indirect commands as
    // ceil(meshletCount/32) TASK workgroups (flat.task launches survivors) instead of one mesh
    // workgroup per meshlet. Mirrors RendererState.taskCull — the pipelines' stage set and this
    // flag must flip together. std140 uvec4; [1..3] reserved.
    uint32_t taskParams[4];
} CullUBO;

// std140 layout guards for the Hi-Z tail (review 4.9 step 3): a scalar before 16-aligned arrays is the
// classic std140 trap (see cull.comp). These pin the C offsets to the SPIR-V offsets (spirv-dis-verified).
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

    // Per-slot mesh/material (meshIndex, materialIndex); ×1 device-local + delta staging.
    SlotUpload              entity;

    // Mesh draw parameters (firstIndex, indexCount, vertexOffset per mesh)
    VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];

    // Bounding volumes for frustum testing
    VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written draw count (atomic counter output from cull shader)
    VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written compacted entity indices (1-to-1 mapping for visible elements)
    VkBuffer                compactedEntityIndicesBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           compactedEntityIndicesAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               compactedEntityIndicesMapped[MAX_FRAMES_IN_FLIGHT];

    // Per-draw depth keys for the transparency sort (audit 4.7). cull writes one float per camera
    // transmission draw at [view*maxEntities + writeIdx]; tpsort.comp reads them to reorder the
    // partition back-to-front. GPU-private (DEVICE_LOCAL), sized ANO_VIEW_COUNT*maxEntities floats.
    VkBuffer                sortKeysBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           sortKeysAllocs[MAX_FRAMES_IN_FLIGHT];

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;


    // Capacity tracking
    uint32_t                maxEntities;
} CullingBuffers;

#endif // ANO_BUFFER_TYPES_H
