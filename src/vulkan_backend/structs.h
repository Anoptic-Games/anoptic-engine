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
#include <anoptic_text.h>                // AnoFontBake + AnoGlyphInstance (text overlay, FONT_RENDER.md)

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

// Hi-Z (hierarchical depth) occlusion pyramid (review 4.9 step 3). Each camera view owns a half-res
// R32F depth pyramid built each frame from its MSAA depth; the cull samples it (next frame, single-
// phase) to reject occluded entities. Mip cap covers a half-res 4K base (2048 -> 12 mips) with slack.
#define ANO_MAX_HIZ_MIPS         16u

// Default per-view screen-area cull threshold, in pixels of projected bounding-sphere radius
// (review 4.9 step 1). An in-frustum entity smaller than this on screen emits no draw. ~1.5 px
// drops genuinely sub-pixel geometry while staying conservative; tune per view at runtime via
// ano_render_set_view_cull_threshold (0 disables the test for that view).
#define ANO_CULL_PIXEL_THRESHOLD_DEFAULT  1.5f

// Default per-view LOD threshold, in pixels of projected bounding-sphere radius (review 4.9 step 2):
// the rpx at which an entity drops from level 0 to level 1; each halving of rpx below it drops one
// more level. Inert until meshes carry LOD chains (lodCount>1). Tune per view at runtime via
// ano_render_set_view_lod_threshold (0 disables LOD selection -> always level 0 for that view).
#define ANO_LOD_PIXEL_THRESHOLD_DEFAULT   128.0f

// Default shadow-caster LOD offset (review 4.9 step 2, revised). Shadows now select the primary camera
// view's (view 0) LOD so a caster's shadow silhouette tracks its visible geometry; this default is the
// RELATIVE offset added on top. 0 == exact match with the visible mesh (removes the old permanent bias
// that made close casters throw coarse shadows); + = coarser shadow for perf. Clamped per-entity to the
// real chain length (level 0 for non-LOD meshes). Runtime-set via ano_render_set_shadow_lod_bias.
#define ANO_SHADOW_LOD_BIAS_DEFAULT       0

// Dynamic shadow pass (audit 4.7 follow-on, built on the 4.8 multi-frustum cull). Each shadow map
// is one more frustum to cull against: a directional ortho map, a spot perspective map, and 6
// perspective faces per point light. All shadow maps are layers of one 2D array — point lights
// pick a face by the dominant axis of (frag - light) in the fragment (no samplerCube). The cull
// frustum set is the camera views followed by the shadow frustums. Sized to the demo's light rig;
// a dynamic "any light casts" registry is a follow-on.
//
// Storage is moment shadow maps (Peters & Klein 2015), NOT binary depth-compare: each layer is a
// filterable RGBA16_UNORM color image holding the optimized 4-power-moment encoding of the nearest
// occluder depth (a transient depth buffer still selects the nearest occluder during the render).
// The fragment reconstructs a CDF lower bound (Hamburger 4MSM) instead of PCF-ing a depth-compare.
// A separable Gaussian prefilter blurs the moments (filtering = soft shadows), which is why the map
// resolution dropped to 512: filtered moments don't alias the way a depth-compare map does.
#define ANO_SHADOW_DIR_COUNT     1u    // static directional casters (one ortho map each)
#define ANO_SHADOW_SPOT_COUNT    1u    // static spot casters (one perspective map each)
#define ANO_SHADOW_POINT_COUNT   4u    // static point casters (6 cube-face maps each)
#define ANO_SHADOW_CUBE_FACES    6u
#define ANO_SHADOW_STATIC_FRUSTUM_COUNT (ANO_SHADOW_DIR_COUNT + ANO_SHADOW_SPOT_COUNT + ANO_SHADOW_POINT_COUNT * ANO_SHADOW_CUBE_FACES) // 26: the init rig
// Runtime shadow-caster headroom (audit 4.7): frustums above the static rig, allocated/freed at
// runtime as attached lights opt into casting (ano_render_light_attach castsShadow). Two pools, no
// fragmentation: single = dir/spot (1 frustum), point = 6 contiguous cube faces. Each frustum is a
// 512^2 RGBA16 atlas+temp layer (~12.6 MiB x3 frames), so this headroom is a real VRAM budget.
#define ANO_SHADOW_RT_SINGLE_COUNT 4u  // runtime dir/spot casters (1 frustum each)
#define ANO_SHADOW_RT_POINT_COUNT  2u  // runtime point casters (6 frustums each)
#define ANO_SHADOW_RT_FRUSTUM_COUNT (ANO_SHADOW_RT_SINGLE_COUNT + ANO_SHADOW_RT_POINT_COUNT * ANO_SHADOW_CUBE_FACES) // 16
#define ANO_SHADOW_FRUSTUM_COUNT (ANO_SHADOW_STATIC_FRUSTUM_COUNT + ANO_SHADOW_RT_FRUSTUM_COUNT) // 42 (static 26 + runtime 16)
_Static_assert(ANO_SHADOW_FRUSTUM_COUNT <= 64u, "MoverBound.exposeMask is a u64 frustum bitmask");
#define ANO_SHADOW_RT_SINGLE_BASE ANO_SHADOW_STATIC_FRUSTUM_COUNT                          // single-pool first slot (26)
#define ANO_SHADOW_RT_POINT_BASE  (ANO_SHADOW_RT_SINGLE_BASE + ANO_SHADOW_RT_SINGLE_COUNT) // point-pool first slot (30)
#define ANO_SHADOW_NONE          0xFFFFFFFFu // "no shadow frustum" sentinel (ShadowLightInfo.baseFrustum / rowShadowBase)
#define ANO_FRUSTUM_COUNT        (ANO_VIEW_COUNT + ANO_SHADOW_FRUSTUM_COUNT)  // camera + shadow frustums = currently 28
#define ANO_SHADOW_DIM           512u                   // per-layer shadow map resolution (CDF filtering lets this drop from 1024)
#define ANO_SHADOW_STATS_FORMAT  VK_FORMAT_R16G16B16A16_UNORM // layered Power CDF per-layer (coverage,M) pairs, filterable (sampler2DArray)
#define ANO_SHADOW_TRANSIENT_DEPTH_FORMAT VK_FORMAT_D32_SFLOAT // transient nearest-occluder select; not sampled
// Layered Power CDF (LVSM-style): partition light-space depth into CDF_LAYERS bands. Each texel stores,
// per band, (coverage, coverage*meanDepth) — both linearly filterable — so a receiver's occlusion is the
// cumulative coverage of nearer bands. Two (coverage,M) pairs pack into one RGBA16 texel, so the atlas
// holds ATLAS_SUBLAYERS array layers per frustum (rendered together via MRT). Keep in sync with the
// splits + packing in shadow_cdf.glsl.
#define ANO_SHADOW_CDF_LAYERS      4u                          // depth partitions
#define ANO_SHADOW_ATLAS_SUBLAYERS (ANO_SHADOW_CDF_LAYERS / 2u) // RGBA16 texels/frustum (2 pairs each)
#define ANO_SHADOW_ATLAS_LAYERS    (ANO_SHADOW_FRUSTUM_COUNT * ANO_SHADOW_ATLAS_SUBLAYERS) // 84
#define ANO_SHADOW_ORTHO_EXTENT  20.0f                  // half-size of the single directional ortho box; sized to enclose the ~30m Sponza atrium (mirror of shadowsetup.comp; non-cascaded, so larger = coarser texels)

// Per-pass GPU timestamp boundaries (RADIANCE_CASCADES.md §8). Fence-post model: one timestamp at
// each section boundary, region time = consecutive delta. Shared by the record path (vulkanMaster)
// and the per-frame query-pool sizing (instanceInit). Insert RC boundaries here as those passes land.
enum {
    ANO_TS_FRAME_BEGIN = 0, // top of the command buffer
    ANO_TS_AFTER_UPLOAD,    // delta staging copies done
    ANO_TS_AFTER_COMPUTE,   // update/scatter/cull done
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
	bool taskShader;            // VK_EXT_mesh_shader taskShader feature usable (implies meshShader); enables the per-meshlet task cull
	bool depthMaxResolve;       // VK_RESOLVE_MODE_MAX_BIT in supportedDepthResolveModes: enables the Hi-Z single-sample depth-resolve path (else per-sample MSAA reduce)
	bool shaderOutputLayer;     // vk1.2 shaderOutputLayer(+ViewportIndex): vertex-stage gl_Layer, enables the single-pass layered shadow blur (else per-layer passes)
	bool timelineSemaphore;     // vk1.2 timelineSemaphore: cross-queue ordering for the async Hi-Z build (review finding 2)
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
	uint32_t preferredMsaa;	// MSAA sample count (2/4/8), clamped to device support; review finding 5
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
    bool            computeShared;                     // device buffer CONCURRENT gfx+compute (async light-cull reads it); grow preserves
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
// LightData is laid out as 5 x vec4 (80 bytes) for std430. Each leading vec3 +
// scalar packs into one 16-byte row (standard std430 vec3+scalar packing); the C
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
    // row 3 (audit 4.7 multi-light). Local offset in the driving entity's MODEL space: world pos =
    // transforms[transformIndex] * vec4(localOffset, 1). Lets many lights share ONE parent slot at
    // distinct positions (running lights / engine / cockpit) with no entity slot each; zero ==
    // driver origin (reproduces the pre-offset behaviour). Applied at ALL FOUR world-pos sites in
    // lockstep — flat.frag, transmission.frag, lightcull.comp (froxel binning), shadowsetup.comp
    // (shadow eye, so offset lights also cast correctly). Edit those five mirrors together.
    float       localOffset[3];
    uint32_t    pad2;
    // row 4 (audit 4.7 fanned spots). Light direction in the driving entity's MODEL space: world
    // forward = normalize(mat3(transforms[transformIndex]) * localDir). Lets spots/directionals on a
    // shared parent slot aim independently instead of all following the parent's -Z. (0,0,-1)
    // reproduces the prior -lx[2] behaviour (the C layer defaults a zero vector to it). Decoded at
    // the three direction sites in lockstep: flat.frag, transmission.frag, shadowsetup.comp (spot
    // shadow eye). lightcull.comp bins by position+range only, so it ignores this field (but its
    // struct mirror MUST still carry the row for a matching stride).
    float       localDir[3];
    uint32_t    pad3;
} LightData; // 80 bytes
_Static_assert(sizeof(LightData) == 80, "LightData must be 80B (5x vec4) to match the GLSL std430 mirrors");

// Runtime light lifecycle (audit 4.7 Phase 3). Render-side authority over the DYNAMIC region of the
// light palette: rows [base, base+capacity); rows [0, base) are the permanent init-rig lights. Maps a
// producer-owned light_id -> palette row, records each row's parent render_id for the parent-DESTROY
// cascade, and frame-quarantines a detached row before reuse (mirrors RenderSlotTable's quarantine).
// highWater is the dynamic peak; the cull light count is base + highWater. light_registry_compact
// peels the trailing free run each drain to lower highWater after a permanent drop, so the cull bound
// reclaims past the concurrent peak. Render-thread only; no synchronization. ANO_RENDER_SLOT_UNMAPPED == unmapped.
enum { LIGHT_ROW_FREE = 0u, LIGHT_ROW_LIVE = 1u, LIGHT_ROW_QUARANTINED = 2u };

typedef struct LightRowQuarantine { uint32_t row; uint64_t safeFrame; } LightRowQuarantine;

typedef struct LightRegistry
{
    uint32_t   base;            // dynamic rows start here in the palette (== init light count)
    uint32_t   capacity;        // dynamic row ceiling (palette capacity - base)
    uint32_t   framesInFlight;  // == MAX_FRAMES_IN_FLIGHT

    uint32_t  *idToRow;         // light_id -> relative row [0,highWater), or UNMAPPED. Grown on demand.
    uint32_t   idCapacity;

    uint8_t   *rowState;        // [rowsCapacity] LIGHT_ROW_*
    uint32_t  *rowParent;       // [rowsCapacity] parent render_id  (LIVE rows; drives the cascade scan)
    uint32_t  *rowLightId;      // [rowsCapacity] light_id in the row (LIVE rows; to unmap on cascade)
    LightData *rowMirror;       // [rowsCapacity] CPU mirror of each row's staged LightData; the
                                // read-modify-write base for partial RCMD_LIGHT_UPDATE (device copy
                                // is not host-readable). Written fully on attach/full-update.
    uint32_t  *rowShadowBase;   // [rowsCapacity] the runtime shadow frustum block base this row's light
                                // owns (ANO_SHADOW_NONE = non-casting); freed back to the pool on detach.
    uint32_t   rowsCapacity;

    uint32_t  *freeRows;        // stack of FREE relative rows (holes below highWater)
    uint32_t   freeCount, freeCapacity;

    uint32_t   highWater;       // peak relative rows used; cull light count = base + highWater

    LightRowQuarantine *quarantine;
    uint32_t   quarantineCount, quarantineCapacity;
} LightRegistry;

// Swept-bound motion exposure (review finding 8, deferred half). A parametric mover's trajectory is
// a closed form, so ONE world-space sphere bounds its mesh for ALL time; a shadow frustum needs
// re-rendering only while some mover's sphere reaches its light's influence volume (or its light
// itself rides a mover). Movers with no finite bound (LINEAR/STREAMED, degenerate params, or
// ANO_FORCE_NO_SWEPT) count into moverUnboundedCount, which restores the old blanket epoch. All
// bookkeeping is render-thread-only and command-driven — counts change when commands change motion/
// pose/mesh or the caster set, never per frame. See the mover_* / shadow_volume_* helpers.
typedef struct MoverBound
{
    uint32_t slot;              // owning entity slot
    uint32_t unbounded;         // 1 = no finite trajectory bound (counted in moverUnboundedCount)
    float    center[3];         // world sphere containing the slot's mesh over its WHOLE trajectory
    float    radius;
    uint64_t exposeMask;        // bit s = this mover exposes frustum s (kept in sync with shadowExposed)
    AnoMotionDescriptor motion; // retained so a teleport / mesh swap can recompute the bound
} MoverBound;

typedef struct ShadowCasterVolume
{
    uint32_t parentSlot;        // slot driving the light (ANO_RENDER_SLOT_UNMAPPED = no caster here)
    float    offset[3];         // light's localOffset in parent model space (recompute input)
    float    range;             // light range as attached; <= 0 = unbounded
    float    center[3];         // cached world influence sphere (parent BASE pose x offset)
    float    radius;            // < 0 = unbounded volume (directional / range <= 0): any mover exposes
} ShadowCasterVolume;

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
    // Special draw slots (ano_draw_slot_of): [0] = additive lane (emits no shadow caster), [1] =
    // transmission lane (the depth-sorted "over" lane). ANO_NO_DRAW_SLOT if that lane is absent.
    // std140 uvec4 (16 B contiguous); cull.comp reads specialSlots.x / .y.
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

// --- Dynamic shadows (audit 4.7 follow-on, on the 4.8 multi-frustum cull) -------------------
// Shadow frustums reuse CullView (viewProj + 6 planes): shadowsetup.comp writes them from each
// light's live GPU transform, cull.comp tests entities against the planes, and the depth render +
// fragment sampler use the viewProj. They occupy one slot-0 cull partition each, packed after the
// camera partitions: ANO_VIEW_COUNT*drawSlotCount + s (see ano_draw_partition_count, components.h).

// One per shadow frustum: which light + cube face it renders. Drives shadowsetup.comp's projection
// choice (ortho / perspective / cube face). `active` lets spare/runtime-freed frustums be skipped:
// shadowsetup writes reject-all planes for inactive slots (cull then emits nothing) and the record
// loop skips them, so the runtime headroom slots don't spuriously render light 0. Written through a
// SlotUpload (race-free runtime mutation) + a render-thread CPU mirror for the record-loop gating.
// std430: 4 x u32 = 16 B.
typedef struct ShadowFrustumConfig {
    uint32_t lightIndex;   // index into the light buffer
    uint32_t lightType;    // LightType
    uint32_t faceIndex;    // cube face [0,6) for point lights; 0 otherwise
    uint32_t active;       // 0 = inactive (skip render, reject-all cull), 1 = live
} ShadowFrustumConfig;

// CPU-authored, indexed by light index: where this light's shadow frustums (= array layers) live,
// so the fragment shader knows whether/where to sample. std430: 4 x u32 = 16 B.
typedef struct ShadowLightInfo {
    uint32_t castsShadow;  // 0 = no shadow (skip sampling)
    uint32_t baseFrustum;  // first shadow-frustum index / array layer for this light
    uint32_t frustumCount; // 1 (dir/spot) or 6 (point)
    uint32_t pad;
} ShadowLightInfo;

// Per-frame shadow state: the GPU-written frustum buffer + descriptor sets. The CDF-stats atlas
// itself is a SINGLE shared instance on RendererState (review finding 8): per-frustum content
// persists across frames so clean frustums skip their render+blur; the per-frame sets just bind
// the shared views. frustumBuffer stays per-frame (shadowsetup rewrites it from that frame's light
// data — identical for unchanged lights, so cached layers stay consistent with it).
typedef struct ShadowResources {
    VkBuffer        frustumBuffer;   // CullView[ANO_SHADOW_FRUSTUM_COUNT], written by shadowsetup.comp
    GpuAllocation   frustumAlloc;
    VkDescriptorSet setupSet;        // shadowsetup.comp inputs/outputs
    VkDescriptorSet geomSet;         // moment render (flat.mesh / flat.vert) + frag sampling
    VkDescriptorSet blurAtlasSet;    // blur src = atlas array (X pass: atlas -> temp)
    VkDescriptorSet blurTempSet;     // blur src = temp array  (Y pass: temp -> atlas)
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

    // Single-sample MAX-resolved depth (avenue 1, review 4.9 step 3): populated only when
    // deviceCapabilities.depthMaxResolve. A fixed-function VK_RESOLVE_MODE_MAX_BIT resolve of depthImage
    // (farthest sample = conservative occluder) so the Hi-Z reduce reads one sample/texel instead of the
    // per-sample sampler2DMS loop. NULL handles when unsupported (the reduce falls back to depthImage).
    VkImage             depthResolveImage;
    GpuAllocation       depthResolveAlloc;
    VkImageView         depthResolveView;

    // HDR resolve target: this view's MSAA HDR color resolves here (single-sample), then the
    // composite/tonemap pass samples it to write the swapchain. Per view (the composite reads
    // each view's image) and per frame (an in-flight frame's read never races the next resolve).
    VkImage             hdrColorImage;
    GpuAllocation       hdrColorAlloc;
    VkImageView         hdrColorView;

    // Picking id resolve target (R32_UINT, single-sample): SAMPLE_ZERO resolve of the shared MSAA
    // id attachment, then copied to a readback buffer. ONLY frames[*].views[0] is populated (the
    // gameplay view); other views render the id attachment but discard it (resolveMode=NONE).
    VkImage             pickIdResolveImage;
    GpuAllocation       pickIdResolveAlloc;
    VkImageView         pickIdResolveView;

    // Hi-Z occlusion pyramid (review 4.9 step 3): half-res R32F mip chain built from depthImage each
    // frame. hizSampledView covers all mips (downsample reads mip k-1; the cull samples it next frame);
    // hizMipViews[k] is a single-mip storage view the build writes via imageStore. hizSets[k] is the
    // per-mip compute descriptor set (single-image binding avoids storage-image-array dynamic indexing,
    // an unenabled feature). hizMipCount mips are live; dims are per view (half this view's extent —
    // views render at their own resolution, review finding 6).
    VkImage             hizImage;
    GpuAllocation       hizAlloc;
    VkImageView         hizSampledView;
    VkImageView         hizMipViews[ANO_MAX_HIZ_MIPS];
    VkDescriptorSet     hizSets[ANO_MAX_HIZ_MIPS];
    uint32_t            hizMipCount;
    uint32_t            hizWidth;
    uint32_t            hizHeight;

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

    // Command recording. computeCommandBuffer holds the async Hi-Z pyramid build (review finding
    // 2): allocated from computeCommandPool (dedicated compute family), submitted to
    // ctx.computeQueue after the graphics submit, NULL when asyncHiz is off. Async light-cull
    // (finding 2 remainder) splits the graphics frame: preludeCommandBuffer carries the uploads +
    // shared compute prelude (submitted first, signals preludeTimeline), commandBuffer the rest;
    // lightcullCommandBuffer holds both views' light-cull dispatches for ctx.computeQueue. Both
    // NULL when asyncLc is off (single-CB frame, in-frame light-cull).
    VkCommandBuffer     commandBuffer;
    VkCommandBuffer     computeCommandBuffer;
    VkCommandBuffer     preludeCommandBuffer;
    VkCommandBuffer     lightcullCommandBuffer;

    // Per-pass GPU timestamps (RADIANCE_CASCADES.md profiling harness). One pool per frame in
    // flight: reset + written during record, read back after this slot's fence next time round.
    VkQueryPool         timestampPool;

    // Picking readback: the view-0 id texel under the cursor, copied here each frame and read after
    // this slot's fence (the timestamp-collect pattern). Host-visible|coherent, persistently mapped.
    VkBuffer            pickReadback;
    GpuAllocation       pickReadbackAlloc;
    uint32_t*           pickReadbackMapped;

    // Per-view resources (camera UBO, depth, HDR target, froxel lists, descriptor sets).
    ViewResources       views[ANO_VIEW_COUNT];

    // Dynamic shadow state: GPU-written shadow frustums + the depth atlas array (audit 4.7).
    ShadowResources     shadow;

    // View-independent descriptor sets: a single cull dispatch tests all views at once, and
    // update/scatter operate on the shared transform pool — none of these are per-view.
    VkDescriptorSet     cullSet;
    VkDescriptorSet     updateSet;
    VkDescriptorSet     scatterSet;
    VkDescriptorSet     lightsetupSet;  // per-light runtime precompute (transforms+lights in, LightRuntime out)

    // Text overlay (FONT_RENDER.md step 5). The overlay image is the glyph raster target
    // (compute-written GENERAL, composite-sampled SHADER_READ), swapchain-sized, recreated
    // with the swapchain. textFrameBuffer is host-visible/mapped frame data (glyph
    // instances now, tile lists later), rewritten wholesale when the text changes.
    VkImage             textOverlayImage;
    GpuAllocation       textOverlayAlloc;
    VkImageView         textOverlayView;
    VkBuffer            textFrameBuffer;
    GpuAllocation       textFrameAlloc;
    void*               textFrameMapped;
    VkDescriptorSet     textRasterSet;   // curves + directory + frame data + storage image
    VkDescriptorSet     textOverlaySet;  // tonemapSetLayout-shaped: sampled overlay for composite
    VkCommandBuffer     textCommandBuffer; // async text raster CB (computeCommandPool); NULL when asyncText off
    uint32_t            textSlotVersion;   // textVersion this slot's frame buffer last copied

    // Deferred resource deletion
    DeletionQueue       deletionQueue;
} PerFrameResources;

typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;

    // Latest cursor position in framebuffer pixels (origin top-left), updated by the GLFW cursor
    // callback on the render thread; the picking readback samples the id-buffer texel here. Read +
    // written only on the render thread, so plain floats (no atomics).
    float                   cursorX, cursorY;

    // GPU id-buffer picking (audit 3.1): per-view MSAA R32_UINT id attachment (mirrors colorImage),
    // resolved per frame for view 0. lastPickRenderId de-dupes REVENT_PICK_RESULT (emit on change).
    VkImage                 pickIdImage[ANO_VIEW_COUNT];
    GpuAllocation           pickIdImageAlloc[ANO_VIEW_COUNT];
    VkImageView             pickIdView[ANO_VIEW_COUNT];
    uint32_t                lastPickRenderId;

    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    // Per-view render extent (review finding 6): view 0 renders at the swapchain extent; auxiliary
    // views render at their composite inset size (W/3 x H/3, matching the tonemap placement) so a
    // 1/9-area inset stops paying full-resolution pixel cost. Filled wherever imageExtent is set.
    VkExtent2D              viewExtent[ANO_VIEW_COUNT];
    uint32_t                imageCount;
    VkImage*                images;
    // Per-view MSAA color target (transient, resolved into that view's hdrColorImage). Per view —
    // not shared — so views need no inter-view reuse barrier and can size to their own extent.
    VkImage                 colorImage[ANO_VIEW_COUNT];
    GpuAllocation           colorImageAlloc[ANO_VIEW_COUNT];
    VkImageView             colorView[ANO_VIEW_COUNT];
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

    // Text overlay (FONT_RENDER.md). Glyph curves bake once (CPU blobs on textHeap, the
    // shaping source) and upload once to device-local buffers. The raster pass lives in
    // prototypes[PIPELINE_COMPUTE_TEXTRASTER]; the composite blend draw is bespoke and
    // shares the tonemap set/pipeline layout (one sampled image). textOverlay gates all
    // of it: env ANO_FORCE_NO_TEXT, or a font/bake init failure (non-fatal), turn it off.
    bool                    textOverlay;
    VkDescriptorSetLayout   textRasterSetLayout;
    VkPipeline              textOverlayPipeline;
    VkBuffer                textCurveBuffer;
    GpuAllocation           textCurveAlloc;
    VkBuffer                textGlyphBuffer;
    GpuAllocation           textGlyphAlloc;
    AnoFontBake             textBake;
    mi_heap_t*              textHeap;
    uint32_t                textInstanceCount; // instances in the CURRENT slot's frame buffer
    uint32_t                textFlags;         // TextRasterPush.flags (bit 0 = opaque self-test)
    // Pending on-screen text (FONT_RENDER.md step 8): ano_vk_text_set shapes into this
    // canonical array (textHeap) and bumps textVersion; each frame slot copies it into
    // its own mapped frame buffer after its fence wait (ano_vk_text_frame_refresh), so
    // in-flight GPU readers are never overwritten. Render thread only.
    AnoGlyphInstance*       textPending;
    uint32_t                textPendingCount;
    uint32_t                textVersion;
    // Async text lane (FONT_RENDER.md step 7): lag-0, rides asyncHiz's infrastructure. The
    // per-frame raster CB submits to ctx.computeQueue with NO waits (inputs are CPU-written,
    // slot reuse is frame-fence ordered) and signals textTimeline == ordinal; the frame's
    // main submit waits it at FRAGMENT_SHADER (the composite sample is the only consumer).
    // Gate: asyncText (asyncHiz && textOverlay && !ANO_FORCE_NO_ASYNC_TEXT, downgraded
    // non-fatally if the lane's objects fail); off falls back to the in-frame record.
    bool                    asyncText;
    VkSemaphore             textTimeline;
    // World-space text lane (the paper's pixel-shader variant): a bespoke spinning quad
    // drawn inside each view's additive pass (the last MSAA color pass, so it resolves
    // with the scene). Same glyph buffers and instance ABI; instances live in the upper
    // region of the per-frame text buffers (index ANO_TEXT_WORLD_FIRST on), shaped once
    // at init. Gate: textWorld (textOverlay && !ANO_FORCE_NO_TEXT_WORLD).
    bool                    textWorld;
    VkPipeline              textWorldPipeline;
    VkPipelineLayout        textWorldLayout;
    uint32_t                textWorldCount;

    // Hi-Z occlusion pyramid build (review 4.9 step 3). The pipeline lives in prototypes[
    // PIPELINE_COMPUTE_HIZ] (two implementations: [0]=reduce MSAA depth->mip0, [1]=downsample mip->mip,
    // selected by the isReduce spec constant). hizSetLayout is the shared per-mip compute set layout
    // (sampler2D pyramid, r32f storage mip, sampler2DMS depth). Pyramid dims/mips are per view
    // (ViewResources.hizWidth/hizHeight/hizMipCount — half that view's extent). Recreated with the
    // swapchain.
    VkDescriptorSetLayout   hizSetLayout;

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
    TransformBuffer         lightRuntimeBuffer;     // ×3 DEVICE_LOCAL, lightsetup.comp writes the per-light fragment runtime record (64B: pose + color*intensity + range/cone/type) each frame
    IndirectDrawBuffer      indirectBuffer;
    BindlessTextureArray    bindlessTextures;

    // Skeletons (see structs above): declared, not yet allocated/bound/drawn.
    DecalPool               decalPool;      // global anchored decal pool (PIPELINE_DECAL)
    BonePalettePool         bonePalette;    // visible-compacted bone matrices (PIPELINE_SKINNED)

    VkDescriptorSetLayout   updateSetLayout;
    VkDescriptorSetLayout   scatterSetLayout;
    VkDescriptorSetLayout   lightcullSetLayout; // clustered-forward light assignment pass
    VkDescriptorSetLayout   shadowSetupSetLayout; // shadowsetup compute pass (per-shadow-frustum viewProj build)
    VkDescriptorSetLayout   lightsetupSetLayout; // per-light world-pose precompute pass

    // Dynamic shadow pass (audit 4.7; moment shadow maps). Renders the optimized 4-moment encoding
    // (color) of the nearest occluder into the shadow atlas array. Standalone pipeline (not a
    // PipelineType): reuses the cull-compacted draw lists. A separable Gaussian prefilter (the
    // shadowBlur* fullscreen pipeline) then softens the moments before the lighting frags sample.
    VkPipeline              shadowPipeline;
    VkPipelineLayout        shadowLayout;
    VkDescriptorSetLayout   shadowGeomSetLayout; // moment render: shadow frustums + transforms + compacted indices + geometry
    VkPipelineCache         shadowCache;
    VkSampler               shadowSampler;       // plain linear/clamp sampler for sampler2DArray (moments + blur taps)
    // Moment prefilter: fullscreen separable box over the atlas. Vertex stage is shadowblur.vert
    // (gl_Layer from push constant, layered single-pass blur) when shaderOutputLayer is available,
    // else tonemap.vert with per-layer render passes.
    VkPipeline              shadowBlurPipeline;
    VkPipelineLayout        shadowBlurLayout;
    VkDescriptorSetLayout   shadowBlurSetLayout; // 1 combined-image-sampler (blur source array)

    // Transient nearest-occluder depth for the shadow atlas render: one slice per frustum so the
    // per-frustum depth renders carry no cross-frustum dependency (no WAW barrier chain). Single
    // instance across frames in flight — contents are frame-transient (loadOp CLEAR, never
    // sampled); each frame's UNDEFINED->DEPTH transition orders the cross-frame WAR by naming
    // EARLY|LATE_FRAGMENT_TESTS in its source scope (single in-order queue; same pattern as the
    // Hi-Z pyramid cross-frame WAR).
    VkImage                 shadowDepthImage;
    GpuAllocation           shadowDepthAlloc;
    VkImageView             shadowDepthSliceView[ANO_SHADOW_FRUSTUM_COUNT];

    // CDF-stats shadow atlas + separable-blur temp: ONE instance across frames in flight (review
    // finding 8 — the ×3 copies were rebuilt from UNDEFINED every frame, structurally blocking any
    // temporal reuse). RGBA16_UNORM 2D arrays, ANO_SHADOW_ATLAS_LAYERS layers (2 sublayers/frustum).
    // The atlas rests in SHADER_READ between frames with per-frustum content persisting; a frame
    // with dirty frustums flips the whole array COLOR<->SHADER_READ (transitions preserve content)
    // and re-renders only the dirty ones. Cross-frame WARs (a prior in-flight frame's lighting/blur
    // reads) ride the pre-barrier's FRAGMENT_SHADER source scope on the single in-order queue. The
    // temp's content never crosses frames (still UNDEFINED-discarded per use).
    VkImage                 shadowAtlasImage;
    GpuAllocation           shadowAtlasAlloc;
    VkImageView             shadowAtlasArrayView;                        // sampled by lighting frags + blur-X
    VkImageView             shadowAtlasLayerView[ANO_SHADOW_ATLAS_LAYERS]; // per-sublayer render targets
    VkImage                 shadowTempImage;
    GpuAllocation           shadowTempAlloc;
    VkImageView             shadowTempArrayView;                         // blur-Y source
    VkImageView             shadowTempLayerView[ANO_SHADOW_ATLAS_LAYERS]; // blur-X render targets

    // Dirty-frustum cache state (review finding 8). A frustum re-renders when its layer is invalid
    // (never built, or its light attached/detached/changed), a conservative global epoch fires
    // (any entity-mutating command, streamed transforms this frame, or a mover with no finite
    // trajectory bound), or swept motion exposure holds it dirty: a parametric mover whose whole-
    // trajectory sphere reaches its light volume, or a light riding a mover (shadowExposed /
    // shadowVolume below). Camera-driven LOD drift is deliberately NOT an invalidation source:
    // cached layers keep their render-time LOD until something else dirties them (bounded,
    // silhouette-only staleness). shadowCacheMode: 0 = normal, 1 = every frame dirty
    // (ANO_FORCE_NO_SHADOW_CACHE, the pre-cache behavior), 2 = freeze (ANO_SHADOW_CACHE_FREEZE:
    // only never-built layers render — the steady-state ceiling a fully static scene would reach).
    uint32_t                shadowCacheMode;
    bool                    shadowLayerValid[ANO_SHADOW_FRUSTUM_COUNT];
    bool                    shadowGlobalDirty;  // set by apply-path scene mutations; consumed each record
    uint8_t*                slotMotion;         // per-slot: non-static motion descriptor installed
    uint32_t                slotMotionCap;
    uint32_t                motionActiveCount;  // live slots with non-static motion

    // Swept-bound motion exposure (finding 8, deferred half — see MoverBound above). The SlotUpload
    // device copies are not host-readable, so base pose + mesh index are mirrored CPU-side at every
    // staging site; mover/caster records derive from the mirrors at command time. All arrays are
    // [slotMotionCap], grown in lockstep by ensureEntityCapacity.
    mat4*                   slotBasePose;       // CPU mirror of staged base poses
    uint32_t*               slotMeshIdx;        // CPU mirror of staged mesh indices (NO_MESH_INDEX default)
    uint32_t*               slotMoverIdx;       // slot -> movers[] row, ANO_RENDER_SLOT_UNMAPPED if none
    MoverBound*             movers;             // compact live movers
    uint32_t                moverCount;
    uint32_t                moverCap;
    uint32_t                moverUnboundedCount;// movers with no finite bound -> blanket epoch fallback
    bool                    sweptExposure;      // ANO_FORCE_NO_SWEPT clears: every mover counts unbounded
    bool                    sweptPoisoned;      // mover-array growth failed: permanent blanket fallback
    ShadowCasterVolume      shadowVolume[ANO_SHADOW_FRUSTUM_COUNT];
    uint32_t                shadowExposed[ANO_SHADOW_FRUSTUM_COUNT]; // movers whose bound reaches the volume

    // Re-render budget (finding 8, last deferred half). Caps CONTENT-dirty renders per frame
    // (mover exposure / scene-mutation epochs — the light unchanged, so this frame's rewritten
    // frustum matrix is identical to the cached content's: deferral costs only temporal lag,
    // never matrix/content tearing). MATRIX-dirty frustums (light attached/changed/teleported/
    // riding a mover) are exempt and always render: shadowsetup rewrites their viewProj from live
    // state each frame, so deferring them would sample old content with a new matrix. Deferral is
    // naturally persistent (valid flag stays false; exposure counts remain), and command-driven
    // exposure changes always ride an epoch, so stale content never strands. 0 = unlimited.
    bool                    shadowMatrixDirty[ANO_SHADOW_FRUSTUM_COUNT]; // light changed: not deferrable
    uint64_t                shadowLastRendered[ANO_SHADOW_FRUSTUM_COUNT]; // globalFrame stamp: oldest-first fairness
    uint32_t                shadowRenderBudget; // max content-dirty renders/frame (ANO_SHADOW_BUDGET)

    // Shadow config: per-frustum (which light/face/active) + per-light (where its maps live). Both
    // are SlotUpload (×1 device + delta staging) so the runtime caster lifecycle can mutate them
    // race-free, exactly like lightBuffer; the static rig seeds them at init. shadowCfgMirror is the
    // render-thread CPU copy of the config the record loop reads for per-frustum gating (the device
    // copy is not host-readable). shadowInfo is GPU-only (fragment), so it needs no mirror.
    SlotUpload              shadowConfig;           // ShadowFrustumConfig[ANO_SHADOW_FRUSTUM_COUNT]
    SlotUpload              shadowInfo;             // ShadowLightInfo[lightBuffer.capacity]
    ShadowFrustumConfig*    shadowCfgMirror;        // [ANO_SHADOW_FRUSTUM_COUNT] render-thread mirror

    // Static rig frustum allocator (monotonic, init only): the rig fills [0, shadowFrustumNext).
    // shadowTypeUsed indexed by LightType (0 dir / 1 point / 2 spot); bounds each type to its budget.
    uint32_t                shadowFrustumNext;
    uint32_t                shadowTypeUsed[3];

    // Runtime frustum pools (audit 4.7): free-lists over the headroom above the static rig. Single =
    // dir/spot (1 frustum) in [RT_SINGLE_BASE, RT_POINT_BASE); point = 6-block bases in
    // [RT_POINT_BASE, FRUSTUM_COUNT). Stacks of free slot/block-base indices; no quarantine needed
    // (the atlas is per-frame and the config SlotUpload WAR barrier serializes reuse).
    uint32_t                rtSingleFree[ANO_SHADOW_RT_SINGLE_COUNT];
    uint32_t                rtSingleFreeCount;
    uint32_t                rtPointFree[ANO_SHADOW_RT_POINT_COUNT];
    uint32_t                rtPointFreeCount;

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
    LightRegistry           lightRegistry;  // runtime light attach/detach lifecycle (audit 4.7 Phase 3)

    // Runtime render config (RADIANCE_CASCADES.md). lightingMode is an AnoLightingMode, stored
    // as u32 so it copies straight into the GlobalUBO tail; debugView selects a visualization
    // (0 = off) added with the radiance-cascade passes. Process-arena lifetime, mutated only
    // from the render thread (L-key callback / ano_render_set_lighting_mode).
    uint32_t                lightingMode;   // AnoLightingMode; default ANO_LIGHTING_SHADOWMAP (0)
    uint32_t                debugView;      // RC debug visualization selector (0 = off)
    // Per-view screen-area cull threshold in pixels (review 4.9 step 1): an in-frustum entity whose
    // projected bounding-sphere radius falls below this is dropped before it emits a draw. Held per
    // view (a peripheral main view can cull harder than a zoomed scope inset) and runtime-set via
    // ano_render_set_view_cull_threshold; 0 disables the test for that view. updateCullingBuffers
    // squares it into CullUBO.viewCullParams[v][1]. Defaulted in createCullingBuffers.
    float                   cullPixelThreshold[ANO_VIEW_COUNT];
    // Per-view LOD threshold in pixels (review 4.9 step 2): the projected radius at which level 1
    // begins (each further halving drops a level). Runtime-set via ano_render_set_view_lod_threshold,
    // copied into CullUBO.viewCullParams[v][2]; 0 disables LOD (always level 0). Inert until meshes
    // carry LOD chains. Defaulted in createCullingBuffers.
    float                   lodPixelThreshold[ANO_VIEW_COUNT];
    // Global LOD-level bias for debug/inspection + tuning (review 4.9 step 2): added to every entity's
    // auto-selected level (cull clamps to the chain range). + = coarser, - = finer; a large +bias pins
    // the scene to its coarsest level. Replicated into CullUBO.viewCullParams[v][3]. Runtime-set via
    // ano_render_set_lod_bias (the test scene's [ and ] keys); default 0 (no bias).
    int32_t                 lodBias;
    // Shadow LOD offset (review 4.9 step 2, revised): shadows select the primary camera view's (view 0)
    // LOD so a caster's shadow tracks its visible geometry; this is a RELATIVE offset on top. + = coarser
    // shadow than the visible mesh. Published into CullUBO.shadowLodBias. Runtime-set via
    // ano_render_set_shadow_lod_bias (the test scene's ; and ' keys); default ANO_SHADOW_LOD_BIAS_DEFAULT
    // (0 = exact match). Only affects meshes with LOD chains.
    int32_t                 shadowLodBias;
    // Hi-Z occlusion (review 4.9 step 3). viewProjHist[slot] holds the viewProj each frame slot was
    // recorded with; updateCullingBuffers publishes the slot matching the pyramid the cull samples
    // (lag 1 sync, lag 2 async — review finding 2) into CullUBO.prevViewProj for reprojection.
    // hizEnable is the per-view runtime toggle (0 = off, the default): updateCullingBuffers publishes
    // mipCount only when enabled, so the occlusion test is inert until ano_render_set_view_hiz_enable.
    mat4                    viewProjHist[MAX_FRAMES_IN_FLIGHT][ANO_VIEW_COUNT];
    uint32_t                hizEnable[ANO_VIEW_COUNT];

    // Async Hi-Z build (review finding 2): the pyramid reduce/downsample chain runs on the dedicated
    // compute queue, overlapping the next frame's graphics; the cull consumes a lag-2 pyramid.
    // Ordering is two timelines — gfxTimeline counts graphics submits (waited by that frame's build:
    // depth resolves done), hizTimeline counts builds (waited by the ordinal+2 graphics submit before
    // its cull/depth stages, host-waited before this slot's compute CB is re-recorded). Values are the
    // 1-based submit ordinal, monotonic across swapchain recreates (frame fences/slots are not).
    // hizValidOrdinal gates the occlusion test off until every sampled pyramid slot has been built
    // at the current resolution (set by createHiZResources). Gate: asyncHiz (set once at init).
    bool                    asyncHiz;
    VkSemaphore             gfxTimeline;
    VkSemaphore             hizTimeline;
    uint64_t                timelineOrdinal;    // last submitted ordinal; the frame being recorded is +1
    uint64_t                hizValidOrdinal;    // first ordinal whose cull may trust the sampled pyramids
    VkCommandPool           computeCommandPool; // compute-family pool for the per-frame build CBs

    // Async light-cull (review finding 2 remainder): the per-view froxel binning runs on the same
    // dedicated compute queue DURING this frame's shadow region. The graphics frame splits in two —
    // the prelude submit (uploads + shared compute) signals preludeTimeline = ordinal, the light-cull
    // submit waits it and signals lcTimeline = ordinal, and the main submit waits lcTimeline at
    // FRAGMENT_SHADER (first consumer). The prelude also waits lcTimeline >= ordinal-1 at TRANSFER:
    // the shared lightBuffer upload must not overwrite the PRIOR frame's compute-queue read (the
    // in-queue reach-back barrier no longer covers it). lightBuffer/lightRuntime/cluster buffers are
    // created CONCURRENT-shared between the two families. Gate: asyncLc (requires asyncHiz's infra).
    bool                    asyncLc;
    VkSemaphore             preludeTimeline;
    VkSemaphore             lcTimeline;

    // Task-shader meshlet cull (review priority 10): every mesh-drawing pipeline (flat both lanes
    // + prepass impls, transmission, additive, shadow) carries a flat.task stage that frustum/
    // cone/Hi-Z-culls 32 meshlets per workgroup and launches mesh workgroups for survivors only;
    // cull.comp sizes indirect commands to match (CullUBO.taskParams). Decided once at init
    // (meshShader && taskShader && !ANO_FORCE_NO_TASK) — layouts, pipelines, barriers, push-
    // constant stage flags, and the cull UBO flag all key off it together.
    bool                    taskCull;

    // GPU timestamp profiling (RADIANCE_CASCADES.md §8). Queried once at init from the device
    // limits + graphics queue family; validBits == 0 disables the per-pass timing path.
    float                   timestampPeriodNs;  // ns per timestamp tick (limits.timestampPeriod)
    uint32_t                timestampValidBits;  // graphics-queue timestampValidBits (0 = unsupported)
} RendererState;


#endif
