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
#include <anoptic_resources.h>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"
#include "vulkan_backend/render_slots.h"
#include "render_bridge/render_bridge.h" // private transport; completes AnoRenderBridge + protocol
#include <anoptic_text.h>                // AnoFontBake + AnoGlyphInstance (text overlay)

#define MAX_FRAMES_IN_FLIGHT 3

// HDR linear render target: MSAA float, resolved then tonemapped to swapchain.
#define ANO_HDR_COLOR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

// Clustered-forward froxel grid (fixed dimensions). ANO_CLUSTER_MAX_LIGHTS caps lights per froxel.
#define ANO_CLUSTER_X            16u
#define ANO_CLUSTER_Y            9u
#define ANO_CLUSTER_Z            24u
#define ANO_CLUSTER_COUNT        (ANO_CLUSTER_X * ANO_CLUSTER_Y * ANO_CLUSTER_Z) // 3456
#define ANO_CLUSTER_MAX_LIGHTS   128u

// Render views per frame. View 0 is the main swapchain view; the rest composite on top.
#define ANO_VIEW_COUNT           2u

// Hi-Z occlusion pyramid mip cap: half-res R32F depth pyramid per camera view.
#define ANO_MAX_HIZ_MIPS         16u

// Max live logic-submitted screen-text blocks; excess dropped.
#define ANO_TEXT_MAX_BLOCKS      64u

// UI overlay lane table capacities (per-frame regions of uiFrameBuffer; ui-render.md §3.2).
#define ANO_UI_MAX_PRIMS         4096u
#define ANO_UI_MAX_CLIPS         256u
#define ANO_UI_MAX_PAINTS        256u
#define ANO_UI_MAX_STOPS         1024u
#define ANO_UI_MAX_CURVE_WORDS   16384u // packed path curve stream (binding 8), 64 KiB/slot
// Per-tile prim lists (binding 9 offsets, 10 entries). Offset words cover the max 8px
// grid (2560x1368 = 54720 tiles) + slack + the trailing total, 256-word aligned.
#define ANO_UI_TILE_OFFSET_WORDS 65792u
#define ANO_UI_MAX_TILE_ENTRIES  262144u
// UI glyph label region of the text frame buffer, above the world panel. Drawn only by
// UI_GLYPHS prims, z-interleaved.
#define ANO_UI_GLYPH_FIRST       16384u
#define ANO_UI_MAX_GLYPHS        5120u
// Max live logic-submitted UI blocks; excess dropped.
#define ANO_UI_MAX_BLOCKS        64u

// Default per-view screen-area cull threshold (px). Runtime ano_render_set_view_cull_threshold, 0 disables.
#define ANO_CULL_PIXEL_THRESHOLD_DEFAULT  1.5f

// Default per-view LOD threshold (px): level 0->1 drop. Runtime ano_render_set_view_lod_threshold, 0 disables LOD.
#define ANO_LOD_PIXEL_THRESHOLD_DEFAULT   128.0f

// Default shadow-caster LOD offset relative to view 0. Runtime ano_render_set_shadow_lod_bias.
#define ANO_SHADOW_LOD_BIAS_DEFAULT       0

// Dynamic shadow pass: each shadow map is one cull frustum. Moment maps (RGBA16_UNORM), reconstructed via CDF lower bound.
#define ANO_SHADOW_DIR_COUNT     1u    // static directional casters
#define ANO_SHADOW_SPOT_COUNT    1u    // static spot casters
#define ANO_SHADOW_POINT_COUNT   4u    // static point casters (6 faces each)
#define ANO_SHADOW_CUBE_FACES    6u
#define ANO_SHADOW_STATIC_FRUSTUM_COUNT (ANO_SHADOW_DIR_COUNT + ANO_SHADOW_SPOT_COUNT + ANO_SHADOW_POINT_COUNT * ANO_SHADOW_CUBE_FACES) // 26
// Runtime shadow-caster headroom: frustums above the static rig. Two pools: single (dir/spot), point (6 faces).
#define ANO_SHADOW_RT_SINGLE_COUNT 4u  // runtime dir/spot casters
#define ANO_SHADOW_RT_POINT_COUNT  2u  // runtime point casters (6 frustums each)
#define ANO_SHADOW_RT_FRUSTUM_COUNT (ANO_SHADOW_RT_SINGLE_COUNT + ANO_SHADOW_RT_POINT_COUNT * ANO_SHADOW_CUBE_FACES) // 16
#define ANO_SHADOW_FRUSTUM_COUNT (ANO_SHADOW_STATIC_FRUSTUM_COUNT + ANO_SHADOW_RT_FRUSTUM_COUNT) // 42
_Static_assert(ANO_SHADOW_FRUSTUM_COUNT <= 64u, "MoverBound.exposeMask is a u64 frustum bitmask; shadow_sample.glsl's viewProj UBO is mat4[64]");
// Fixed bound of the sampling-viewProj UBO array (shadow_sample.glsl declares mat4[64]).
#define ANO_SHADOW_SAMPLE_VP_CAP 64u
#define ANO_SHADOW_RT_SINGLE_BASE ANO_SHADOW_STATIC_FRUSTUM_COUNT                          // single-pool first slot
#define ANO_SHADOW_RT_POINT_BASE  (ANO_SHADOW_RT_SINGLE_BASE + ANO_SHADOW_RT_SINGLE_COUNT) // point-pool first slot
#define ANO_SHADOW_NONE          0xFFFFFFFFu // "no shadow frustum" sentinel
#define ANO_FRUSTUM_COUNT        (ANO_VIEW_COUNT + ANO_SHADOW_FRUSTUM_COUNT)  // camera + shadow frustums
#define ANO_SHADOW_DIM           512u                   // per-layer shadow map resolution
#define ANO_SHADOW_STATS_FORMAT  VK_FORMAT_R16G16B16A16_UNORM // layered Power CDF (coverage,M) pairs, filterable
#define ANO_SHADOW_TRANSIENT_DEPTH_FORMAT VK_FORMAT_D32_SFLOAT // transient nearest-occluder select
// Layered Power CDF (LVSM-style): partition light-space depth into CDF_LAYERS bands. Keep in sync with shadow_cdf.glsl.
#define ANO_SHADOW_CDF_LAYERS      4u                          // depth partitions
#define ANO_SHADOW_ATLAS_SUBLAYERS (ANO_SHADOW_CDF_LAYERS / 2u) // RGBA16 texels/frustum (2 pairs each)
#define ANO_SHADOW_ATLAS_LAYERS    (ANO_SHADOW_FRUSTUM_COUNT * ANO_SHADOW_ATLAS_SUBLAYERS) // 84
#define ANO_SHADOW_ORTHO_EXTENT  20.0f                  // half-size of the directional ortho box (mirror of shadowsetup.comp)

// Per-pass GPU timestamp boundaries (fence-post: one timestamp per section boundary).
enum {
    ANO_TS_FRAME_BEGIN = 0, // top of the command buffer
    ANO_TS_AFTER_UPLOAD,    // delta staging copies done
    ANO_TS_AFTER_COMPUTE,   // update/scatter/cull done
    ANO_TS_AFTER_SHADOW,    // shadow depth render done
    ANO_TS_AFTER_LIGHTING,  // per-view light-cull + geometry done
    ANO_TS_AFTER_COMPOSITE, // tonemap composite done
    ANO_TS_COUNT
};

// FALLBACK_MESH_INDEX: public renderer contract (anoptic_render.h).
#define FALLBACK_TEXTURE_INDEX 0

// Sentinel values for optional entity components.
// NO_MESH_INDEX: transform-only entity (culling skips it). NO_LIGHT_INDEX: no light.
#define NO_MESH_INDEX  0xFFFFFFFFu
#define NO_LIGHT_INDEX 0xFFFFFFFFu

// Structs

typedef struct DeviceCapabilities // Add queue families, device extensions etc as implemented
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
	bool drawIndirectCount;
	bool meshShader;            // VK_EXT_mesh_shader meshShader usable
	bool taskShader;            // VK_EXT_mesh_shader taskShader usable (per-meshlet task cull)
	bool depthMaxResolve;       // VK_RESOLVE_MODE_MAX_BIT: Hi-Z single-sample depth-resolve path
	bool shaderOutputLayer;     // vk1.2 shaderOutputLayer: single-pass layered shadow blur
	bool timelineSemaphore;     // vk1.2 timelineSemaphore: cross-queue ordering for async Hi-Z build
	bool shaderFloat16;         // vk1.2 shaderFloat16: selects the *_fp16.frag lighting variants
} DeviceCapabilities;

typedef struct QueueFamilyIndices // Which queue families exist and the selected queue for each
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
    uint32_t meshIndex;       // geometry pool index, or NO_MESH_INDEX
    uint32_t materialIndex;   // index into MaterialSSBO
    uint32_t lightIndex;      // light SSBO index, or NO_LIGHT_INDEX
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
    uint32_t monitorIndex;        // Fullscreen monitor index, -1 for windowed
    bool borderless;         // Borderless
    // ... other parameters
} WindowParameters;

typedef struct VulkanSettings
{
	char* preferredDevice; // Physical GPU to use for rendering
	uint32_t preferredMode;	// Frame present mode
	uint32_t preferredMsaa;	// MSAA sample count (2/4/8), clamped to device support
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


struct VulkanGarbage // Resources to destroy
{
	struct VulkanContext *ctx;
	GLFWwindow *window;
	Monitors *monitors;
};

// Domain type fragments split out of this file; depend on the defines above.
#include "vulkan_backend/buffer_types.h"
#include "vulkan_backend/light_types.h"
#include "vulkan_backend/shadow/shadow_types.h"


typedef struct MaterialData
{
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
    
    // 3. Emissive Factor (aligned to 16 bytes)
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

    // Final padding to 16-byte multiple (total 320 bytes)
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

// ===========================================================================
// SKELETONS — data shapes only. None of the structs below are allocated, bound,
// or drawn yet; they document the planned decal and skinned-mesh subsystems so
// the architecture is fixed before the implementation lands. Activating a pass:
//   1. create its PipelinePrototype in pipeline.c (mirror PIPELINE_FLAT / the
//      compute passes),  2. add its shaders to the CMake shader manifest,
//   3. add a draw entry to g_framePasses (decal/skinned) or a dispatch (pose).
// ===========================================================================

// Global budget-bounded decal pool; each decal anchors back to its host by render slot.
typedef struct DecalRecord
{
    mat4     localTransform;   // placement in the host slot's local space
    uint32_t anchorSlot;       // host render slot
    uint32_t textureLayer;     // index into the decal texture array
    float    fade;             // 0..1 lifetime fade
    uint32_t flags;            // projection vs UV-overlay, blend mode, etc.
} DecalRecord; // 80 bytes

typedef struct DecalPool
{
    VkBuffer      buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation allocs[MAX_FRAMES_IN_FLIGHT];
    DecalRecord*  mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t      capacity;    // global decal budget (hard cap)
    uint32_t      count;       // live decals
    uint32_t      recycleHead; // ring cursor for eviction
} DecalPool;

// Skinned-mesh state; only skinned slots carry it, keyed by slot.
typedef struct SkinInstanceState
{
    uint32_t rigId;            // index into the rig asset table
    uint32_t clipA;            // active clip
    uint32_t clipB;            // cross-fade target (== clipA when not blending)
    float    blendStart;       // global-time stamp the A->B cross-fade began
    float    blendDuration;    // cross-fade length, 0 == no blend
    float    clipStartPhase;   // global-time stamp clipA started
} SkinInstanceState;

// Per-instance bone matrices, sized to the visible skinned set, GPU-regenerated each frame.
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

// Everything a single render view owns within a frame-in-flight. View 0 is the main camera.
typedef struct ViewResources
{
    // Per-view camera UBO (view/proj/cameraPos/near/far/cluster params).
    VkBuffer            uniformBuffer;
    GpuAllocation       uniformAlloc;
    void*               uniformMapped;

    // Depth attachment (MSAA), per view.
    VkImage             depthImage;
    GpuAllocation       depthAlloc;
    VkImageView         depthView;

    // Single-sample MAX-resolved depth; populated only when deviceCapabilities.depthMaxResolve, else NULL.
    VkImage             depthResolveImage;
    GpuAllocation       depthResolveAlloc;
    VkImageView         depthResolveView;

    // HDR resolve target: this view's MSAA HDR color resolves here, sampled by the tonemap pass.
    VkImage             hdrColorImage;
    GpuAllocation       hdrColorAlloc;
    VkImageView         hdrColorView;

    // Picking id resolve target (R32_UINT): SAMPLE_ZERO resolve of the id attachment. Only views[0] populated.
    VkImage             pickIdResolveImage;
    GpuAllocation       pickIdResolveAlloc;
    VkImageView         pickIdResolveView;

    // Hi-Z occlusion pyramid: half-res R32F mip chain built from depthImage each frame.
    // hizSampledView covers all mips; hizMipViews[k] is a single-mip storage view; hizSets[k] the per-mip set.
    VkImage             hizImage;
    GpuAllocation       hizAlloc;
    VkImageView         hizSampledView;
    VkImageView         hizMipViews[ANO_MAX_HIZ_MIPS];
    VkDescriptorSet     hizSets[ANO_MAX_HIZ_MIPS];
    uint32_t            hizMipCount;
    uint32_t            hizWidth;
    uint32_t            hizHeight;

    // Clustered-forward froxel light lists for this view (device-local).
    VkBuffer            clusterLightCountBuffer;
    GpuAllocation       clusterLightCountAlloc;
    VkBuffer            clusterLightIndexBuffer;
    GpuAllocation       clusterLightIndexAlloc;

    // Per-view descriptor sets: globalSet (geometry/fragment), lightcullSet (light-cull), tonemapSet (composite).
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

    // Command recording. computeCommandBuffer: async Hi-Z build (NULL when asyncHiz off).
    // preludeCommandBuffer/lightcullCommandBuffer: async light-cull split (NULL when asyncLc off).
    VkCommandBuffer     commandBuffer;
    VkCommandBuffer     computeCommandBuffer;
    VkCommandBuffer     preludeCommandBuffer;
    VkCommandBuffer     lightcullCommandBuffer;

    // Per-pass GPU timestamps, one pool per frame in flight.
    VkQueryPool         timestampPool;

    // Picking readback: view-0 id texel under the cursor. Host-visible|coherent, persistently mapped.
    VkBuffer            pickReadback;
    GpuAllocation       pickReadbackAlloc;
    uint32_t*           pickReadbackMapped;

    // Per-view resources (camera UBO, depth, HDR target, froxel lists, descriptor sets).
    ViewResources       views[ANO_VIEW_COUNT];

    // Dynamic shadow state: GPU-written shadow frustums + the depth atlas array.
    ShadowResources     shadow;

    // View-independent descriptor sets (cull tests all views at once; update/scatter on the shared pool).
    VkDescriptorSet     cullSet;
    VkDescriptorSet     updateSet;
    VkDescriptorSet     scatterSet;
    VkDescriptorSet     lightsetupSet;  // per-light runtime precompute

    // Text overlay. Overlay image is the glyph raster target; textFrameBuffer is host-visible frame data.
    VkImage             textOverlayImage;
    GpuAllocation       textOverlayAlloc;
    VkImageView         textOverlayView;
    VkBuffer            textFrameBuffer;
    GpuAllocation       textFrameAlloc;
    void*               textFrameMapped;
    // UI overlay lane frame data: one host-visible buffer holding the table regions
    // (raster-set bindings 4-10). Created whenever textOverlay is up.
    VkBuffer            uiFrameBuffer;
    GpuAllocation       uiFrameAlloc;
    void*               uiFrameMapped;
    uint32_t            uiSlotVersion;   // uiVersion this slot's tables last copied
    // Per-tile prim lists (§3.7): built into this slot's tile regions in the record path,
    // keyed on (version, grid).
    uint32_t            uiTileVersion;   // 0 = never built / invalid
    int32_t             uiTileOx, uiTileOy;
    uint32_t            uiTileGx, uiTileGy;
    VkDescriptorSet     textRasterSet;   // curves + directory + frame data + storage image
    VkDescriptorSet     textOverlaySet;  // sampled overlay for composite
    VkCommandBuffer     textCommandBuffer; // async text raster CB, NULL when asyncText off
    uint32_t            textSlotVersion;   // textVersion this slot's frame buffer last copied

    // Deferred resource deletion
    DeletionQueue       deletionQueue;
} PerFrameResources;

typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;

    // Latest cursor position in framebuffer pixels (origin top-left). Render thread only.
    float                   cursorX, cursorY;

    // Overlay surface scale: logical units -> framebuffer px (0 until the window exists,
    // treated as 1). Written by window.c on the main thread, read by the compose fold
    // and the snapshot publish.
    float                   uiScale;

    // GPU id-buffer picking: per-view MSAA R32_UINT id attachment, resolved for view 0.
    VkImage                 pickIdImage[ANO_VIEW_COUNT];
    GpuAllocation           pickIdImageAlloc[ANO_VIEW_COUNT];
    VkImageView             pickIdView[ANO_VIEW_COUNT];
    uint32_t                lastPickRenderId;

    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    // Per-view render extent: view 0 at swapchain extent, auxiliary views at their inset size.
    VkExtent2D              viewExtent[ANO_VIEW_COUNT];
    uint32_t                imageCount;
    VkImage*                images;
    // Per-view MSAA color target (transient, resolved into that view's hdrColorImage).
    VkImage                 colorImage[ANO_VIEW_COUNT];
    GpuAllocation           colorImageAlloc[ANO_VIEW_COUNT];
    VkImageView             colorView[ANO_VIEW_COUNT];
    uint32_t                viewCount;
    VkImageView*            views;

    // Command pool
    VkCommandPool           commandPool;

    // Render data. Per-view CPU scratch for each view's camera UBO.
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

    // Tonemap pass (bespoke fullscreen HDR->swapchain encode). Standalone, not a PipelineType.
    VkPipeline              tonemapPipeline;
    VkPipelineLayout        tonemapLayout;
    VkDescriptorSetLayout   tonemapSetLayout;       // 1 combined-image-sampler (hdrColorView)
    VkPipelineCache         tonemapCache;

    // Text overlay. Glyph curves bake once to device-local buffers. Gate textOverlay: off on ANO_FORCE_NO_TEXT or bake init failure.
    bool                    textOverlay;
    VkDescriptorSetLayout   textRasterSetLayout;
    VkPipeline              textOverlayPipeline;
    VkBuffer                textCurveBuffer;
    GpuAllocation           textCurveAlloc;
    VkBuffer                textGlyphBuffer;
    GpuAllocation           textGlyphAlloc;
    AnoFontBake             textBake;
    mi_heap_t*              textHeap;
    ano_res_reader          textResourceReader;
    ano_res_read            textResourceRead;
    uint32_t                textInstanceCount; // instances in the CURRENT slot's frame buffer
    uint32_t                textFlags;         // TextRasterPush.flags (bit 0 = opaque self-test)
    // Pending on-screen text: ano_vk_text_set shapes into this canonical array and bumps textVersion.
    AnoGlyphInstance*       textPending;
    uint32_t                textPendingCount;
    uint32_t                textVersion;
    float                   textBounds[4];     // pending canvas px AABB (minXY, maxXY), inverted when blank
    // Async text lane (lag-0, rides asyncHiz infra). Gate: asyncText.
    bool                    asyncText;
    VkSemaphore             textTimeline;
    // World-space text lane: a quad drawn in each view's additive pass. Gate: textWorld.
    bool                    textWorld;
    VkPipeline              textWorldPipeline;
    VkPipelineLayout        textWorldLayout;
    uint32_t                textWorldCount;
    // Logic-submitted screen-text blocks (v0 bridge path); each entry adopts the ano_render_text_set allocation.
    struct { uint32_t id; const RenderTextBlock* blk; } textBlocks[ANO_TEXT_MAX_BLOCKS];
    uint32_t                textBlockCount;
    uint32_t                textOsdCount;      // instances the render-internal OSD text occupies

    // UI overlay lane (docs/ui/ui-render.md): prims raster via the text raster dispatch.
    // Gate uiOverlay rides textOverlay, off on ANO_FORCE_NO_UI or init failure (tables
    // stay resident + bound).
    bool                    uiOverlay;
    uint32_t                uiPrimCount; // prim count in the CURRENT slot's tables (push block)
    uint32_t                uiClipCount; // clip count, ditto (shader bound-check, fail closed)
    uint32_t                uiPaintCount; // paint count, ditto (gradient fail-closed bound)
    float                   uiBounds[4]; // current px AABB incl. shadow pads, inverted when blank
    // UI block registry (v0 bridge) + the composed pending tables (textHeap allocations),
    // copied per slot by ano_vk_ui_frame_refresh when uiVersion moves.
    struct { uint32_t id; const RenderUiBlock* blk; } uiBlocks[ANO_UI_MAX_BLOCKS];
    uint32_t                uiBlockCount;
    bool                    uiPinned;            // ANO_UI_DEMO/_OPAQUE: registry adopts, compose suppressed
    bool                    uiComposeDirty;      // block set/clear/rescale marks, frame refresh flushes once
    AnoUiPrim*              uiPendingPrims;
    AnoUiClip*              uiPendingClips;
    AnoUiPaint*             uiPendingPaints;
    AnoUiStop*              uiPendingStops;
    uint32_t*               uiPendingCurves;
    AnoGlyphInstance*       uiPendingGlyphs;
    uint32_t*               uiTileCursor;        // per-tile fill cursor scratch (tile build)
    uint32_t*               uiTileScratch;       // heap-side tile build target, memcpy'd to the slot's mapped regions
    bool                    uiTilesEnabled;      // !ANO_FORCE_NO_UI_TILES, resolved at init
    uint32_t                uiPendingPrimCount;
    uint32_t                uiPendingClipCount;
    uint32_t                uiPendingPaintCount;
    uint32_t                uiPendingStopCount;
    uint32_t                uiPendingCurveCount;
    uint32_t                uiPendingGlyphCount;
    float                   uiPendingBounds[4];
    uint32_t                uiVersion;

    // Hi-Z occlusion pyramid build. Pipeline in prototypes[PIPELINE_COMPUTE_HIZ]; hizSetLayout is the shared per-mip set layout.
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
    TransformBuffer         lightRuntimeBuffer;     // ×3 DEVICE_LOCAL, lightsetup.comp writes the per-light runtime record each frame
    IndirectDrawBuffer      indirectBuffer;
    BindlessTextureArray    bindlessTextures;

    // Skeletons (see structs above): declared, not yet allocated/bound/drawn.
    DecalPool               decalPool;      // global anchored decal pool (PIPELINE_DECAL)
    BonePalettePool         bonePalette;    // visible-compacted bone matrices (PIPELINE_SKINNED)

    VkDescriptorSetLayout   updateSetLayout;
    VkDescriptorSetLayout   scatterSetLayout;
    VkDescriptorSetLayout   lightcullSetLayout; // clustered-forward light assignment pass
    VkDescriptorSetLayout   shadowSetupSetLayout; // shadowsetup compute pass (per-frustum viewProj build)
    VkDescriptorSetLayout   lightsetupSetLayout; // per-light world-pose precompute pass

    // Dynamic shadow pass (moment shadow maps). Standalone pipeline reusing the cull-compacted draw lists.
    VkPipeline              shadowPipeline;
    VkPipeline              shadowPipelineMasked; // alpha-tested casters (MASK cutout), drawn after the solid partition
    VkPipelineLayout        shadowLayout;
    VkDescriptorSetLayout   shadowGeomSetLayout; // moment render: frustums + transforms + compacted indices + geometry
    VkPipelineCache         shadowCache;
    VkSampler               shadowSampler;       // linear/clamp sampler for sampler2DArray
    // Moment prefilter: fullscreen separable box over the atlas.
    VkPipeline              shadowBlurPipeline;
    VkPipelineLayout        shadowBlurLayout;
    VkDescriptorSetLayout   shadowBlurSetLayout; // 1 combined-image-sampler (blur source array)

    // Transient nearest-occluder depth for the shadow atlas render: one slice per frustum.
    VkImage                 shadowDepthImage;
    GpuAllocation           shadowDepthAlloc;
    VkImageView             shadowDepthSliceView[ANO_SHADOW_FRUSTUM_COUNT];

    // CDF-stats shadow atlas + separable-blur temp: ONE instance across frames. RGBA16_UNORM 2D arrays, ANO_SHADOW_ATLAS_LAYERS layers (2 sublayers/frustum).
    VkImage                 shadowAtlasImage;
    GpuAllocation           shadowAtlasAlloc;
    VkImageView             shadowAtlasArrayView;                        // sampled by lighting frags + blur-X
    VkImageView             shadowAtlasLayerView[ANO_SHADOW_ATLAS_LAYERS]; // per-sublayer render targets
    VkImage                 shadowTempImage;
    GpuAllocation           shadowTempAlloc;
    VkImageView             shadowTempArrayView;                         // blur-Y source
    VkImageView             shadowTempLayerView[ANO_SHADOW_ATLAS_LAYERS]; // blur-X render targets

    // Dirty-frustum cache. shadowCacheMode: 0 = normal, 1 = every frame dirty (ANO_FORCE_NO_SHADOW_CACHE), 2 = freeze (ANO_SHADOW_CACHE_FREEZE).
    uint32_t                shadowCacheMode;
    bool                    shadowLayerValid[ANO_SHADOW_FRUSTUM_COUNT];
    bool                    shadowGlobalDirty;  // set by apply-path scene mutations; consumed each record
    uint8_t*                slotMotion;         // per-slot: non-static motion descriptor installed
    uint32_t                slotMotionCap;
    uint32_t                motionActiveCount;  // live slots with non-static motion

    // Swept-bound motion exposure: CPU mirrors of staged base pose + mesh index (device copies not host-readable). All arrays [slotMotionCap], grown by ensureEntityCapacity.
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

    // Re-render budget: caps content-dirty renders per frame. Matrix-dirty frustums exempt (always render). 0 = unlimited.
    bool                    shadowMatrixDirty[ANO_SHADOW_FRUSTUM_COUNT]; // light changed: not deferrable
    uint64_t                shadowLastRendered[ANO_SHADOW_FRUSTUM_COUNT]; // globalFrame stamp: oldest-first fairness
    uint32_t                shadowRenderBudget; // max content-dirty renders/frame (ANO_SHADOW_BUDGET)

    // Shadow config: per-frustum (light/face/active) + per-light (map location). Both SlotUpload. shadowCfgMirror is the render-thread CPU copy for per-frustum gating.
    SlotUpload              shadowConfig;           // ShadowFrustumConfig[ANO_SHADOW_FRUSTUM_COUNT]
    SlotUpload              shadowInfo;             // ShadowLightInfo[lightBuffer.capacity]
    ShadowFrustumConfig*    shadowCfgMirror;        // [ANO_SHADOW_FRUSTUM_COUNT] render-thread mirror

    // Static rig frustum allocator (init only): rig fills [0, shadowFrustumNext). shadowTypeUsed indexed by LightType (0 dir/1 point/2 spot).
    uint32_t                shadowFrustumNext;
    uint32_t                shadowTypeUsed[3];

    // Runtime frustum pools: free-lists above the static rig. Single = dir/spot in [RT_SINGLE_BASE, RT_POINT_BASE); point = 6-block bases in [RT_POINT_BASE, FRUSTUM_COUNT).
    uint32_t                rtSingleFree[ANO_SHADOW_RT_SINGLE_COUNT];
    uint32_t                rtSingleFreeCount;
    uint32_t                rtPointFree[ANO_SHADOW_RT_POINT_COUNT];
    uint32_t                rtPointFreeCount;

    // Fallback resources
    VkImage                 fallbackImage;
    VkImageView             fallbackImageView;



    // Culling system
    CullingBuffers          culling;

    // ECS <-> render bridge. Render master owns slot authority; GPU layout keyed off render_slots, not logic entity index.
    mi_heap_t              *renderHeap;     // backs slot table + bridge rings
    RenderSlotTable         slots;          // logical render_id -> stable GPU slot
    AnoRenderBridge         bridge;         // logic->render commands, render->logic events
    uint64_t                globalFrame;    // monotonic frame counter for slot quarantine
    LightRegistry           lightRegistry;  // runtime light attach/detach lifecycle (audit 4.7 Phase 3)

    // Runtime render config. lightingMode is an AnoLightingMode stored as u32; debugView selects a visualization (0 = off). Render-thread only.
    uint32_t                lightingMode;   // AnoLightingMode; default ANO_LIGHTING_SHADOWMAP (0)
    uint32_t                debugView;      // RC debug visualization selector (0 = off)
    // Per-view screen-area cull threshold (px): entity dropped when projected radius falls below. Runtime ano_render_set_view_cull_threshold, 0 disables. Squared into CullUBO.viewCullParams[v][1].
    float                   cullPixelThreshold[ANO_VIEW_COUNT];
    // Per-view LOD threshold (px): projected radius where level 1 begins (each halving drops a level). Runtime ano_render_set_view_lod_threshold, into CullUBO.viewCullParams[v][2]; 0 disables LOD.
    float                   lodPixelThreshold[ANO_VIEW_COUNT];
    // Global LOD-level bias added to every entity's auto-selected level (+ coarser, - finer). Into CullUBO.viewCullParams[v][3]. Runtime ano_render_set_lod_bias.
    int32_t                 lodBias;
    // Shadow LOD offset relative to view 0 LOD (+ coarser). Into CullUBO.shadowLodBias. Runtime ano_render_set_shadow_lod_bias.
    int32_t                 shadowLodBias;
    // Hi-Z occlusion. viewProjHist[slot] holds each frame slot's viewProj, published into CullUBO.prevViewProj for reprojection. hizEnable is the per-view runtime toggle (0 = off), set via ano_render_set_view_hiz_enable.
    mat4                    viewProjHist[MAX_FRAMES_IN_FLIGHT][ANO_VIEW_COUNT];
    uint32_t                hizEnable[ANO_VIEW_COUNT];

    // Async Hi-Z build: pyramid reduce chain runs on the compute queue, cull consumes a lag-2 pyramid. gfxTimeline counts graphics submits, hizTimeline counts builds (1-based ordinals). Gate: asyncHiz.
    bool                    asyncHiz;
    VkSemaphore             gfxTimeline;
    VkSemaphore             hizTimeline;
    uint64_t                timelineOrdinal;    // last submitted ordinal; the frame being recorded is +1
    uint64_t                hizValidOrdinal;    // first ordinal whose cull may trust the sampled pyramids
    VkCommandPool           computeCommandPool; // compute-family pool for the per-frame build CBs

    // Async light-cull: per-view froxel binning runs on the compute queue during the shadow region. preludeTimeline/lcTimeline order the split prelude + light-cull submits. Gate: asyncLc.
    bool                    asyncLc;
    VkSemaphore             preludeTimeline;
    VkSemaphore             lcTimeline;

    // Task-shader meshlet cull: every mesh-drawing pipeline carries a flat.task stage that frustum/cone/Hi-Z-culls 32 meshlets per workgroup, launching mesh workgroups for survivors. Decided once at init (meshShader && taskShader && !ANO_FORCE_NO_TASK).
    bool                    taskCull;

    // GPU timestamp profiling. Queried once at init; validBits == 0 disables the per-pass timing path.
    float                   timestampPeriodNs;  // ns per timestamp tick (limits.timestampPeriod)
    uint32_t                timestampValidBits;  // graphics-queue timestampValidBits (0 = unsupported)
} RendererState;


#endif
