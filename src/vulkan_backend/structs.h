/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include "gpu_alloc.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"
#include "vulkan_backend/render_slots.h"
#include "render_bridge/render_bridge.h" // private transport; completes AnoRenderBridge + protocol

#define MAX_FRAMES_IN_FLIGHT 3

// HDR linear render target. The geometry passes render into this float format (MSAA),
// resolve to a single-sample HDR image, and a fullscreen tonemap pass encodes to the
// swapchain. R16G16B16A16_SFLOAT is in Vulkan's mandatory color-attachment + sampled +
// blend set, so it needs no runtime format-support query. See LIGHTING_SCALE.md.
#define ANO_HDR_COLOR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

// Clustered-forward froxel grid. Fixed dimensions (independent of entity/light count), so the
// cluster buffers are a fixed allocation, not on the entity growth path. ANO_CLUSTER_MAX_LIGHTS
// caps lights per froxel; overflow drops extras (logged-in-design; see LIGHTING_SCALE.md).
#define ANO_CLUSTER_X            16u
#define ANO_CLUSTER_Y            9u
#define ANO_CLUSTER_Z            24u
#define ANO_CLUSTER_COUNT        (ANO_CLUSTER_X * ANO_CLUSTER_Y * ANO_CLUSTER_Z) // 3456
#define ANO_CLUSTER_MAX_LIGHTS   128u

// Render views per frame (audit 4.8). A view is "one frustum to cull against": a camera, a
// shadow map, a reflection/portal, an inset feed. The cull pass tests every entity against all
// ANO_VIEW_COUNT frustums in one dispatch (single-pass multi-frustum) and writes a per-view
// partition of the indirect/draw-count/compacted buffers; each view owns its own depth + HDR
// target + froxel light lists + camera UBO + descriptor sets. View 0 is the main swapchain view;
// the rest composite on top (here: a picture-in-picture inset). Bump this to add views (e.g. a
// shadow pass); every per-view buffer/target/set array and the CullUBO frustum array size to it.
#define ANO_VIEW_COUNT           2u

// Dynamic shadow pass (audit 4.7 follow-on, built on the 4.8 multi-frustum cull). Each shadow map
// is one more frustum to cull against: a directional ortho map, a spot perspective map, and 6
// perspective faces per point light. All shadow maps are layers of one D32 2D array — point lights
// pick a face by the dominant axis of (frag - light) in the fragment (no samplerCube). The cull
// frustum set is the camera views followed by the shadow frustums. Sized to the demo's light rig;
// a dynamic "any light casts" registry is a follow-on.
// Built incrementally: directional first (this increment), then spot, then point cubes — bump
// the spot/point counts to add them once the directional path is hardware-verified.
#define ANO_SHADOW_DIR_COUNT     1u    // directional casters (one ortho map each)
#define ANO_SHADOW_SPOT_COUNT    1u    // spot casters (one perspective map each)
#define ANO_SHADOW_POINT_COUNT   4u    // point casters (6 cube-face maps each)
#define ANO_SHADOW_CUBE_FACES    6u
#define ANO_SHADOW_FRUSTUM_COUNT (ANO_SHADOW_DIR_COUNT + ANO_SHADOW_SPOT_COUNT + ANO_SHADOW_POINT_COUNT * ANO_SHADOW_CUBE_FACES) // currently 26 (DIR=1, SPOT=1, POINT=4)
#define ANO_FRUSTUM_COUNT        (ANO_VIEW_COUNT + ANO_SHADOW_FRUSTUM_COUNT)  // camera + shadow frustums = currently 28
#define ANO_SHADOW_DIM           1024u                  // per-layer shadow map resolution
#define ANO_SHADOW_DEPTH_FORMAT  VK_FORMAT_D32_SFLOAT   // sampled as a depth-compare (sampler2DArrayShadow)
#define ANO_SHADOW_ORTHO_EXTENT  8.0f                   // half-size of the directional ortho world box (covers the demo scene)

// Radiance cascades (RADIANCE_CASCADES.md). Bounded camera-following clipmap; M1 stands up the 3D
// storage-image resource class with the scene voxel volume + a placeholder write pass. 16 m / 64^3
// cascade-0 is the confirmed validation target; the scene voxel grid is finer (128^3) so geometry
// detail outresolves the probe grid. RGBA16F is a mandatory storage-image format (no support query).
#define ANO_RC_VOXEL_DIM     128u
#define ANO_RC_VOXEL_FORMAT  VK_FORMAT_R16G16B16A16_SFLOAT

// Per-pass GPU timestamp boundaries (RADIANCE_CASCADES.md §8). Fence-post model: one timestamp at
// each section boundary, region time = consecutive delta. Shared by the record path (vulkanMaster)
// and the per-frame query-pool sizing (instanceInit). Insert RC boundaries here as those passes land.
enum {
    ANO_TS_FRAME_BEGIN = 0, // top of the command buffer
    ANO_TS_AFTER_UPLOAD,    // delta staging copies done
    ANO_TS_AFTER_COMPUTE,   // update/scatter/cull done
    ANO_TS_AFTER_RC,        // radiance-cascade passes done (voxelize/trace/merge; M1: probe write)
    ANO_TS_AFTER_SHADOW,    // shadow depth render done
    ANO_TS_AFTER_LIGHTING,  // per-view light-cull + geometry done
    ANO_TS_AFTER_COMPOSITE, // tonemap composite done
    ANO_TS_COUNT
};

// FALLBACK_MESH_INDEX is the public renderer contract (anoptic_render.h), pulled
// in transitively via render_bridge.h above.
#define FALLBACK_TEXTURE_INDEX 0

// Sentinel values for optional entity components.
// NO_MESH_INDEX marks a transform-only entity (e.g. a pure light) that the
// culling pass must skip so it draws no geometry. NO_LIGHT_INDEX marks an
// entity that carries no light. Both match the 0xFFFFFFFF "absent" convention
// already used for optional bindless textures in MaterialData.
#define NO_MESH_INDEX  0xFFFFFFFFu
#define NO_LIGHT_INDEX 0xFFFFFFFFu

// Structs

// New structs for streamlined state resource management






typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
	bool drawIndirectCount;
	bool meshShader;            // VK_EXT_mesh_shader present and meshShader feature usable; false selects the vertex-shader fallback path
} DeviceCapabilities;

typedef struct QueueFamilyIndices // Stores whether different queue families exist, and which queue has been selected for each
{
	bool graphicsPresent;
    uint32_t graphicsFamily;
    bool computePresent;
    uint32_t computeFamily;
    bool transferPresent;
    uint32_t transferFamily;
    bool presentPresent;
    uint32_t presentFamily;
} QueueFamilyIndices;



typedef struct RenderEntity
{ // To be extended with animation data
    uint32_t meshIndex;       // index into geometry pool, or NO_MESH_INDEX for transform-only entities
    uint32_t materialIndex;   // index into MaterialSSBO
    uint32_t lightIndex;      // index into the light SSBO, or NO_LIGHT_INDEX if this entity is not a light
    mat4 transform;
} RenderEntity;

typedef struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formatCount;
    VkSurfaceFormatKHR *formats;
    uint32_t presentModesCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef struct VulkanContext
{
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    bool                     enableValidationLayers;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         physicalDevice;
    uint32_t                 deviceCount;
    char**                   availableDevices;
    DeviceCapabilities       deviceCapabilities;
    QueueFamilyIndices       queueFamilyIndices;
    VkSampleCountFlagBits    msaaSamples;
    VkDevice                 device;
    VkQueue                  graphicsQueue;
    VkQueue                  computeQueue;
    VkQueue                  transferQueue;
    VkQueue                  presentQueue;
} VulkanContext;

typedef struct Dimensions2D
{
	uint32_t width;
	uint32_t height;
} Dimensions2D;

typedef struct WindowParameters
{
    uint32_t width;
    uint32_t height;
    uint32_t monitorIndex;        // Desired monitor index for fullscreen, -1 for windowed
    bool borderless;         // True for borderless, false otherwise
    // ... other parameters
} WindowParameters;

typedef struct VulkanSettings
{
	char* preferredDevice; // Physical GPU to use for rendering
	uint32_t preferredMode;	// Frame present mode
} VulkanSettings;

typedef struct MonitorInfo 
{
    const GLFWvidmode* modes;    // Video modes supported by the monitor
    int modeCount;               // Number of video modes supported
} MonitorInfo;

typedef struct Monitors 
{
    MonitorInfo* monitorInfos;   // Array of MonitorInfo for each monitor
    int monitorCount;           // Total number of monitors
} Monitors;


struct VulkanGarbage //All the various stuff that needs to be thrown out
{
	struct VulkanContext *ctx;
	GLFWwindow *window;
	Monitors *monitors;
};

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

// Streamed-transform lane (Path B, docs/artifacts/STREAMED_TRANSFORMS.md). Two SoA
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
// old->new device-side under vkDeviceWaitIdle. See docs/artifacts/DEVICE_LOCAL_SLOTS.md.
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
} SlotUpload;

typedef struct MaterialData
{
    // Feature flags identifying which features are active in this material
    uint32_t    features;           // PbrFeatureFlags bitmask
    uint32_t    baseColorTexture;   // Index in bindless array (0 = fallback)
    uint32_t    pad0[2];            // Align baseColorFactor to 16 bytes

    // 1. pbrMetallicRoughness
    float       baseColorFactor[4];
    uint32_t    metallicRoughnessTexture;
    float       metallicFactor;
    float       roughnessFactor;

    // 2. Core Material Properties
    uint32_t    normalTexture;
    float       normalScale;
    uint32_t    occlusionTexture;
    float       occlusionStrength;
    uint32_t    emissiveTexture;
    
    // 3. Emissive Factor (aligned to 16 bytes since offset is 64)
    float       emissiveFactor[4];  // RGB + 1 padding element
    uint32_t    alphaMode;          // 0 = OPAQUE, 1 = MASK, 2 = BLEND
    float       alphaCutoff;
    uint32_t    doubleSided;        // 0 = false, 1 = true

    // 4. KHR_materials_clearcoat
    uint32_t    clearcoatTexture;
    uint32_t    clearcoatRoughnessTexture;
    uint32_t    clearcoatNormalTexture;
    float       clearcoatFactor;
    float       clearcoatRoughnessFactor;

    // 5. KHR_materials_transmission
    uint32_t    transmissionTexture;
    float       transmissionFactor;

    // 6. KHR_materials_volume
    uint32_t    thicknessTexture;
    float       thicknessFactor;
    float       attenuationDistance;
    uint32_t    pad1[3];            // Align attenuationColor to 16 bytes

    float       attenuationColor[4]; // RGB + 1 padding element

    // 7. KHR_materials_ior
    float       ior;

    // 8. KHR_materials_specular
    uint32_t    specularTexture;
    uint32_t    specularColorTexture;
    float       specularFactor;

    float       specularColorFactor[4]; // RGB + 1 padding element

    // 9. KHR_materials_sheen
    uint32_t    sheenColorTexture;
    uint32_t    sheenRoughnessTexture;
    uint32_t    pad2[2];            // Align sheenColorFactor to 16 bytes

    float       sheenColorFactor[4]; // RGB + 1 padding element
    float       sheenRoughnessFactor;

    // 10. KHR_materials_iridescence
    uint32_t    iridescenceTexture;
    uint32_t    iridescenceThicknessTexture;
    float       iridescenceFactor;
    float       iridescenceIor;
    float       iridescenceThicknessMinimum;
    float       iridescenceThicknessMaximum;

    // 11. KHR_materials_anisotropy
    uint32_t    anisotropyTexture;
    float       anisotropyStrength;
    float       anisotropyRotation;

    // 12. KHR_materials_dispersion
    float       dispersion;

    // 13. KHR_materials_diffuse_transmission
    uint32_t    diffuseTransmissionTexture;
    uint32_t    diffuseTransmissionColorTexture;
    float       diffuseTransmissionFactor;
    uint32_t    pad3[2];            // Align diffuseTransmissionColorFactor to 16 bytes

    float       diffuseTransmissionColorFactor[4]; // RGB + 1 padding element

    // 14. KHR_materials_emissive_strength
    float       emissiveStrength;

    uint32_t    pipelineType;

    // Final padding to align structure size to a multiple of 16 (total size = 320 bytes)
    uint32_t    padding[2];
} MaterialData;

typedef struct MaterialBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    MaterialData*   mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} MaterialBuffer;

// ---------------------------------------------------------------------------
// Lighting
//
// Punctual light sources following the glTF KHR_lights_punctual model, mirroring
// the engine's glTF-based MaterialData convention. A light is an entity
// component: its world-space position and direction are NOT stored here, they
// are derived in the fragment shader from the driving entity's live transform
// (transforms[transformIndex]) so GPU animation (orbit/spin) applies for free.
// Only photometric parameters and the transform link live in this struct.
//
// LightData is laid out as 3 x vec4 (48 bytes) for std430. The leading vec3 +
// float pack into one 16-byte row (standard std430 vec3+scalar packing); the C
// layout below matches byte-for-byte.
// ---------------------------------------------------------------------------
typedef enum LightType
{
    LIGHT_TYPE_DIRECTIONAL = 0, // infinitely distant, uses direction only
    LIGHT_TYPE_POINT       = 1, // omnidirectional, uses position + range
    LIGHT_TYPE_SPOT        = 2, // cone, uses position + direction + range + cones
} LightType;

typedef struct LightData
{
    // row 0
    float       color[3];       // linear RGB, normalized (intensity carries magnitude)
    float       intensity;      // brightness multiplier (candela-like for point/spot, lux-like for directional)
    // row 1
    float       range;          // attenuation cutoff distance; <= 0 means unbounded (ignored for directional)
    float       innerConeCos;   // cosine of spot inner cone half-angle (full intensity within)
    float       outerConeCos;   // cosine of spot outer cone half-angle (zero intensity beyond)
    uint32_t    type;           // LightType
    // row 2
    uint32_t    transformIndex; // entity/transform index that drives world position + direction
    uint32_t    enabled;        // 0 = ignored, 1 = active
    uint32_t    pad0;
    uint32_t    pad1;
} LightData; // 48 bytes

typedef struct LightBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    LightData*      mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max lights
    uint32_t        count;      // current light count
} LightBuffer;

// ===========================================================================
// SKELETONS — data shapes only. None of the structs below are allocated, bound,
// or drawn yet; they document the planned decal and skinned-mesh subsystems so
// the architecture is fixed before the implementation lands. Activating a pass:
//   1. create its PipelinePrototype in pipeline.c (mirror PIPELINE_FLAT / the
//      compute passes),  2. add its shaders to the CMake shader manifest,
//   3. add a draw entry to g_framePasses (decal/skinned) or a dispatch (pose).
// ===========================================================================

// Decals are NOT a per-entity attribute: ownership is inverted. A decal is an
// element of one global, budget-bounded pool that anchors BACK to its host by
// render slot, so the million entities with none pay nothing. The pool recycles
// (ring/LRU): "unbounded over time" becomes a fixed allocation with eviction.
// The decal pass reads transforms[anchorSlot] to ride a moving host.
typedef struct DecalRecord
{
    mat4     localTransform;   // placement in the host slot's local space (projection box)
    uint32_t anchorSlot;       // host render slot; transforms[anchorSlot] gives live world pose
    uint32_t textureLayer;     // index into the decal texture array (scorch/blood/crack/...)
    float    fade;             // 0..1 lifetime fade; drives LRU eviction when it expires
    uint32_t flags;            // projection vs UV-overlay, blend mode, etc.
} DecalRecord; // 80 bytes

typedef struct DecalPool
{
    VkBuffer      buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation allocs[MAX_FRAMES_IN_FLIGHT];
    DecalRecord*  mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t      capacity;    // global decal budget (hard cap; recycling, never grow-per-entity)
    uint32_t      count;       // live decals
    uint32_t      recycleHead; // ring cursor for eviction of the oldest record
} DecalPool;

// Skinned-mesh state. Looping clips ARE continuous GPU-parameterized motion: send
// {rigId, clip(s), startPhase} once and the pose pre-pass derives the bone palette
// from global time every frame — only discrete clip transitions cross the bridge.
// Sparse: only skinned slots carry this, keyed by slot, never widening the rigid path.
typedef struct SkinInstanceState
{
    uint32_t rigId;            // index into the (static) rig asset table: skeleton + inverse-bind
    uint32_t clipA;            // active clip
    uint32_t clipB;            // cross-fade target (== clipA when not blending)
    float    blendStart;       // global-time stamp the A->B cross-fade began
    float    blendDuration;    // cross-fade length; 0 == no blend
    float    clipStartPhase;   // global-time stamp clipA started (loop phase origin)
} SkinInstanceState;

// Per-instance bone matrices, sized to the VISIBLE skinned set and compacted by the
// cull pass (like compactedEntityIndices), GPU-regenerated each frame by the pose
// pre-pass — an off-screen skinned instance costs zero palette memory. Variable
// width (boneCount per instance) lives here, never in the per-slot hot buffers.
typedef struct BonePalettePool
{
    VkBuffer      buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation allocs[MAX_FRAMES_IN_FLIGHT];
    mat4*         mapped[MAX_FRAMES_IN_FLIGHT]; // skinning matrices, packed per visible instance
    uint32_t      matrixCapacity;              // total bone-matrix slots across the visible set
} BonePalettePool;

typedef struct BindlessTextureArray
{
    VkDescriptorPool        pool;
    VkDescriptorSetLayout   layout;
    VkDescriptorSet         set;            // ONE set, holds ALL textures
    uint32_t                maxTextures;    // upper bound (e.g. 4096)
    uint32_t                textureCount;   // current count
    VkSampler               defaultSampler; // shared linear/repeat sampler
} BindlessTextureArray;

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
} CullUBO;

// --- Dynamic shadows (audit 4.7 follow-on, on the 4.8 multi-frustum cull) -------------------
// Shadow frustums reuse CullView (viewProj + 6 planes): shadowsetup.comp writes them from each
// light's live GPU transform, cull.comp tests entities against the planes, and the depth render +
// fragment sampler use the viewProj. They occupy cull partitions [ANO_VIEW_COUNT, ANO_FRUSTUM_COUNT).

// CPU-authored, one per shadow frustum: which light + cube face it renders. Drives shadowsetup.comp's
// projection choice (ortho / perspective / cube face). std430: 4 x u32 = 16 B.
typedef struct ShadowFrustumConfig {
    uint32_t lightIndex;   // index into the light buffer
    uint32_t lightType;    // LightType
    uint32_t faceIndex;    // cube face [0,6) for point lights; 0 otherwise
    uint32_t pad;
} ShadowFrustumConfig;

// CPU-authored, indexed by light index: where this light's shadow frustums (= array layers) live,
// so the fragment shader knows whether/where to sample. std430: 4 x u32 = 16 B.
typedef struct ShadowLightInfo {
    uint32_t castsShadow;  // 0 = no shadow (skip sampling)
    uint32_t baseFrustum;  // first shadow-frustum index / array layer for this light
    uint32_t frustumCount; // 1 (dir/spot) or 6 (point)
    uint32_t pad;
} ShadowLightInfo;

// Per-frame shadow state: the GPU-written frustum buffer and the depth atlas array. The atlas is a
// D32 2D array (one layer per shadow frustum); layerView[] are per-layer render targets, arrayView
// is the single sampled view (depth-compare via shadowSampler in the fragment shader).
typedef struct ShadowResources {
    VkBuffer        frustumBuffer;   // CullView[ANO_SHADOW_FRUSTUM_COUNT], written by shadowsetup.comp
    GpuAllocation   frustumAlloc;
    VkImage         atlasImage;      // D32 2D array, ANO_SHADOW_FRUSTUM_COUNT layers
    GpuAllocation   atlasAlloc;
    VkImageView     arrayView;       // sample view (2D array, depth aspect)
    VkImageView     layerView[ANO_SHADOW_FRUSTUM_COUNT]; // per-layer depth render targets
    VkDescriptorSet setupSet;        // shadowsetup.comp inputs/outputs
    VkDescriptorSet geomSet;         // depth render (shadow.mesh / shadow.vert)
} ShadowResources;

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

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;


    // Capacity tracking
    uint32_t                maxEntities;
} CullingBuffers;

// Everything a single render view owns within a frame-in-flight (audit 4.8). One per view per
// frame; view 0 is the main camera, the rest are auxiliary (inset/shadow/reflection). All sized
// to the swapchain extent: an auxiliary view renders full-res into its own HDR target and the
// composite samples it into its destination rectangle.
typedef struct ViewResources
{
    // Per-view camera UBO (view/proj/cameraPos/near/far/cluster params). cull.comp reads it
    // (via the mapped copy) to derive this view's frustum; the geometry/lighting shaders read
    // it through globalSet. Distinct per view so each frustum and froxel grid is independent.
    VkBuffer            uniformBuffer;
    GpuAllocation       uniformAlloc;
    void*               uniformMapped;

    // Depth attachment (MSAA). Per view: each frustum has its own visibility/depth.
    VkImage             depthImage;
    GpuAllocation       depthAlloc;
    VkImageView         depthView;

    // HDR resolve target: this view's MSAA HDR color resolves here (single-sample), then the
    // composite/tonemap pass samples it to write the swapchain. Per view (the composite reads
    // each view's image) and per frame (an in-flight frame's read never races the next resolve).
    VkImage             hdrColorImage;
    GpuAllocation       hdrColorAlloc;
    VkImageView         hdrColorView;

    // Clustered-forward froxel light lists for this view (device-local, written by this view's
    // light-cull dispatch, read by its fragment passes). Per view: each frustum bins lights
    // differently. Fixed size: clusterLightCount = uint per froxel; clusterLightIndices =
    // ANO_CLUSTER_MAX_LIGHTS per froxel (offset implicit = clusterIdx * ANO_CLUSTER_MAX_LIGHTS).
    VkBuffer            clusterLightCountBuffer;
    GpuAllocation       clusterLightCountAlloc;
    VkBuffer            clusterLightIndexBuffer;
    GpuAllocation       clusterLightIndexAlloc;

    // Per-view descriptor sets. globalSet binds this view's uniform + cluster lists (+ shared
    // SSBOs) for the geometry/fragment passes; lightcullSet binds this view's uniform + cluster
    // outputs for its light-cull dispatch; tonemapSet samples this view's hdrColorView in the
    // composite. The shaders are view-agnostic — binding the per-view set selects the view.
    VkDescriptorSet     globalSet;
    VkDescriptorSet     lightcullSet;
    VkDescriptorSet     tonemapSet;
} ViewResources;

typedef struct PerFrameResources
{
    // Synchronization
    VkSemaphore         imageAvailable;
    VkSemaphore         renderFinished;
    VkFence             frameFence;
    bool                frameSubmitted;

    // Command recording
    VkCommandBuffer     commandBuffer;

    // Per-pass GPU timestamps (RADIANCE_CASCADES.md profiling harness). One pool per frame in
    // flight: reset + written during record, read back after this slot's fence next time round.
    VkQueryPool         timestampPool;

    // Per-view resources (camera UBO, depth, HDR target, froxel lists, descriptor sets).
    ViewResources       views[ANO_VIEW_COUNT];

    // Dynamic shadow state: GPU-written shadow frustums + the depth atlas array (audit 4.7).
    ShadowResources     shadow;

    // View-independent descriptor sets: a single cull dispatch tests all views at once, and
    // update/scatter operate on the shared transform pool — none of these are per-view.
    VkDescriptorSet     cullSet;
    VkDescriptorSet     updateSet;
    VkDescriptorSet     scatterSet;

    // Deferred resource deletion
    DeletionQueue       deletionQueue;
} PerFrameResources;

typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;

    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    uint32_t                imageCount;
    VkImage*                images;
    VkImage                 colorImage;
    GpuAllocation           colorImageAlloc;
    VkImageView             colorView;
    uint32_t                viewCount;
    VkImageView*            views;

    // Command pool
    VkCommandPool           commandPool;

    // Render data. Per-view CPU scratch: updateUniformBuffer builds each view's camera here,
    // then memcpys it into that view's mapped uniform buffer.
    GlobalUBO               uboData[ANO_VIEW_COUNT];
    VkSampler               textureSampler;
    RenderEntity*           entities;
    uint32_t                entityCount;
    VkFormat                depthFormat;

    // Pipeline system (Stage 0+)
    PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

    // Descriptor infrastructure (to be populated per-stage)
    VkDescriptorPool        globalDescriptorPool;
    VkDescriptorSetLayout   globalSetLayout;        // Set 0

    // Tonemap pass (bespoke fullscreen HDR->swapchain encode). Standalone, not a
    // PipelineType prototype: it does not go through cull/compaction or g_framePasses.
    VkPipeline              tonemapPipeline;
    VkPipelineLayout        tonemapLayout;
    VkDescriptorSetLayout   tonemapSetLayout;       // 1 combined-image-sampler (hdrColorView)
    VkPipelineCache         tonemapCache;

    // Geometry
    GeometryPool            globalGeometryPool;
    RenderPrimitives        primitives;

    TransformBuffer         transformBuffer;        // ×3 DEVICE_LOCAL, GPU-regenerated each frame
    SlotUpload              initialTransformBuffer; // ×1 device-local + delta staging
    SlotUpload              motionBuffer;           // ×1 device-local + delta staging
    SlotUpload              instanceDataBuffer;     // ×1 device-local + delta staging
    TransformStreamBuffer   transformStream;
    MaterialBuffer          materialBuffer;
    SlotUpload              lightBuffer;            // ×1 device-local + delta staging (palette)
    IndirectDrawBuffer      indirectBuffer;
    BindlessTextureArray    bindlessTextures;

    // Skeletons (see structs above): declared, not yet allocated/bound/drawn.
    DecalPool               decalPool;      // global anchored decal pool (PIPELINE_DECAL)
    BonePalettePool         bonePalette;    // visible-compacted bone matrices (PIPELINE_SKINNED)

    VkDescriptorSetLayout   updateSetLayout;
    VkDescriptorSetLayout   scatterSetLayout;
    VkDescriptorSetLayout   lightcullSetLayout; // clustered-forward light assignment pass
    VkDescriptorSetLayout   shadowSetupSetLayout; // shadowsetup compute pass (per-shadow-frustum viewProj build)

    // Dynamic shadow pass (audit 4.7). Depth-only render into the shadow atlas array. Standalone
    // pipeline (not a PipelineType): reuses the cull-compacted draw lists but writes depth only.
    VkPipeline              shadowPipeline;
    VkPipelineLayout        shadowLayout;
    VkDescriptorSetLayout   shadowGeomSetLayout; // depth render: shadow frustums + transforms + compacted indices + geometry
    VkPipelineCache         shadowCache;
    VkSampler               shadowSampler;       // depth-compare sampler (PCF) for sampler2DArrayShadow

    // CPU-authored shadow config: per-frustum (which light/face) + per-light (where its maps live).
    // Static (filled once at init), host-visible. The light-info buffer is indexed by light index.
    VkBuffer                shadowFrustumConfigBuffer;
    GpuAllocation           shadowFrustumConfigAlloc;
    void*                   shadowFrustumConfigMapped;
    VkBuffer                shadowLightInfoBuffer;
    GpuAllocation           shadowLightInfoAlloc;
    void*                   shadowLightInfoMapped;
    // Running shadow-frustum allocator, advanced by addLightEntity as casters register at init.
    // shadowTypeUsed indexed by LightType (0 dir / 1 point / 2 spot); bounds each type to its budget.
    uint32_t                shadowFrustumNext;
    uint32_t                shadowTypeUsed[3];

    // Fallback resources
    VkImage                 fallbackImage;
    VkImageView             fallbackImageView;



    // Culling system
    CullingBuffers          culling;

    // ECS <-> render bridge (VK_BACKEND_INTEROP.md). The render master owns the
    // slot authority and consumes discrete state-transition commands; per-entity
    // GPU layout is keyed off render_slots, never the logic-side entity index.
    mi_heap_t              *renderHeap;     // backs slot table + bridge rings
    RenderSlotTable         slots;          // logical render_id -> stable GPU slot
    AnoRenderBridge         bridge;         // logic->render commands, render->logic events
    uint64_t                globalFrame;    // monotonic frame counter for slot quarantine

    // Runtime render config (RADIANCE_CASCADES.md). lightingMode is an AnoLightingMode, stored
    // as u32 so it copies straight into the GlobalUBO tail; debugView selects a visualization
    // (0 = off) added with the radiance-cascade passes. Process-arena lifetime, mutated only
    // from the render thread (L-key callback / ano_render_set_lighting_mode).
    uint32_t                lightingMode;   // AnoLightingMode; default ANO_LIGHTING_SHADOWMAP (0)
    uint32_t                debugView;      // RC debug visualization selector (0 = off)

    // GPU timestamp profiling (RADIANCE_CASCADES.md §8). Queried once at init from the device
    // limits + graphics queue family; validBits == 0 disables the per-pass timing path.
    float                   timestampPeriodNs;  // ns per timestamp tick (limits.timestampPeriod)
    uint32_t                timestampValidBits;  // graphics-queue timestampValidBits (0 = unsupported)

    // Radiance cascades (RADIANCE_CASCADES.md, M1). Shared ×1 (not per-frame, not per-view): the
    // volumes are produced + consumed within a frame behind barriers, fence-serialized per slot.
    // M1 is the scene voxel volume + a placeholder compute that imageStores into it, proving the
    // 3D STORAGE_IMAGE + GENERAL-layout path. rcProbeSet is a single set (the image is ×1 shared).
    VkImage                 rcSceneVoxel;       // VK_IMAGE_TYPE_3D, ANO_RC_VOXEL_DIM^3, RGBA16F
    GpuAllocation           rcSceneVoxelAlloc;
    VkImageView             rcSceneVoxelView;   // VK_IMAGE_VIEW_TYPE_3D
    VkDescriptorSetLayout   rcProbeSetLayout;   // 1 binding: STORAGE_IMAGE (the voxel volume)
    VkDescriptorSet         rcProbeSet;
} RendererState;


#endif
