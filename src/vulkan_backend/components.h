/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef RENDER_COMPONENTS_H
#define RENDER_COMPONENTS_H

#include <vulkan/vulkan.h>
#include "gpu_alloc.h"

//====================== Enums

typedef enum PipelineType
{
    PIPELINE_FLAT = 0,          // Flat-shaded geometry (replaces PIPELINE_BASIC)
    PIPELINE_PARTICLE,          // Point-sprite / billboard particles
    PIPELINE_SDF_COMPOSITE,     // SDF raymarching compositing pass (future)
    PIPELINE_UI,                // UI overlay (future)
    PIPELINE_TRANSMISSION,      // Refraction / transmission & volume effects
    PIPELINE_COMPUTE_CULL,      // GPU compute culling
    PIPELINE_COMPUTE_UPDATE,    // GPU animation/transform update pass
    PIPELINE_COMPUTE_SCATTER,   // streamed-transform scatter pass (Path B)
    // --- Skeletons: enum slots reserved so the per-type buffers (indirect/drawCount/
    // compacted indices) and the prototype table size for them, but no pipeline is
    // created and no g_framePasses entry drives them yet. See render_bridge.h notes
    // and resources/shaders/{decal,skinned,pose}.* for the planned shape.
    PIPELINE_DECAL,             // (skeleton) projected / UV-overlay decal draw stream
    PIPELINE_SKINNED,           // (skeleton) skinned-mesh draw stream (own vertex stage + bone palette)
    PIPELINE_COMPUTE_LIGHTCULL, // clustered-forward froxel light assignment (compute, never draws)
    PIPELINE_COMPUTE_SHADOWSETUP, // per-shadow-frustum light-space viewProj + frustum-plane build (compute, never draws)
    PIPELINE_COMPUTE_RC_PROBE,  // radiance cascades: 3D storage-image plumbing pass (RADIANCE_CASCADES.md M1)
    PIPELINE_TYPE_COUNT         // Sentinel — array sizing, not a real type
} PipelineType;

// Draw partitions (render config). cull.comp compacts visible draws into per-pipeline
// partitions of the indirect / drawCount / compacted-index buffers, indexed by draw slot.
// Only pipeline types that actually emit draws get a partition: the COMPUTE_* passes never
// draw and PARTICLE / SDF_COMPOSITE / UI / DECAL / SKINNED are unimplemented skeletons, so
// sizing by PIPELINE_TYPE_COUNT would reserve — and vkCmdFillBuffer-zero every frame — most
// of the per-slot GPU footprint as permanently-idle VRAM (the dominant cost at a million
// entities). This list is the single source of truth: order defines the slot index, length
// defines the partition count the buffers and the cull UBO map size to. To add a drawing
// pipeline, append it here and give it a g_framePasses entry; nothing else resizes by hand.
#define ANO_NO_DRAW_SLOT 0xFFFFFFFFu

extern const PipelineType ano_draw_pipelines[]; // drawing pipeline types, in slot order
uint32_t ano_draw_pipeline_count(void);         // partition count = number of drawing types
uint32_t ano_draw_slot_of(PipelineType type);   // enum -> partition index, ANO_NO_DRAW_SLOT if it never draws

typedef enum PassType
{
    PASS_COMPUTE,       // compute dispatch (culling, SDF evaluation, etc.)
    PASS_GRAPHICS,      // rasterization pass
} PassType;

typedef struct RenderPassDef
{
    PassType            type;
    PipelineType        prototype;              // which pipeline prototype to bind
    uint32_t            implementationIndex;    // which variant (opaque, transparent, etc.)
    // Recorded once per view (audit 4.8) vs once per frame. Per-view passes (light-cull, the
    // geometry passes) run inside the view loop binding that view's sets/targets; view-independent
    // passes (update, scatter, cull) run once before it. cull is single-pass multi-frustum.
    bool                perView;

    // Graphics-only:
    uint32_t                colorAttachmentCount;
    VkFormat                colorFormats[4];
    VkFormat                depthFormat;
    VkAttachmentLoadOp      colorLoadOp;
    VkAttachmentLoadOp      depthLoadOp;
    // STORE when a later pass must read this pass's depth (e.g. opaque -> transmission).
    // DONT_CARE discards it: fine on immediate-mode GPUs that leave depth in memory, but
    // on a tile-based renderer (Apple/MoltenVK) the next pass's LOAD then gets garbage.
    VkAttachmentStoreOp     depthStoreOp;
    VkClearValue            colorClear;
    VkClearValue            depthClear;
    VkResolveModeFlagBits   resolveMode;

    // Compute-only:
    uint32_t                dispatchX, dispatchY, dispatchZ;
} RenderPassDef;

//====================== Primitive assets

typedef struct TextureData
{
	uint32_t usageCount; // number of active meshes using this resource
	VkImage textureImage;
	GpuAllocation textureImageAlloc;
	VkImageView textureImageView;
} TextureData;

typedef struct MeshData
{
	uint32_t usageCount; // number of active meshes using this resource
	uint32_t meshRegionIndex;
} MeshData;

// Tracks loaded graphics resources and their usage
typedef struct RenderPrimitives
{
	uint32_t meshCount;
	uint32_t meshCapacity;
	MeshData* meshes;
	uint32_t textureCount;
	uint32_t textureCapacity;
	TextureData* textureBuffers;
} RenderPrimitives;

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data);
void ano_vk_increment_mesh_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_mesh_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data);
void ano_vk_increment_texture_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_texture_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_cleanup_primitives(RenderPrimitives* primitives);

// PBR Feature Flags mapping to glTF properties and extensions
typedef enum PbrFeatureBits {
    PBR_FEATURE_NONE = 0,
    // Core properties
    PBR_FEATURE_BASE_COLOR_FACTOR          = 1 << 0,
    PBR_FEATURE_BASE_COLOR_TEXTURE         = 1 << 1,
    PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR  = 1 << 2,
    PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE = 1 << 3,
    PBR_FEATURE_NORMAL_TEXTURE             = 1 << 4,
    PBR_FEATURE_OCCLUSION_TEXTURE          = 1 << 5,
    PBR_FEATURE_EMISSIVE_FACTOR            = 1 << 6,
    PBR_FEATURE_EMISSIVE_TEXTURE           = 1 << 7,
    
    // Alpha modes
    PBR_FEATURE_ALPHA_MODE_OPAQUE          = 1 << 8,
    PBR_FEATURE_ALPHA_MODE_MASK            = 1 << 9,
    PBR_FEATURE_ALPHA_MODE_BLEND           = 1 << 10,
    
    // Double-sided
    PBR_FEATURE_DOUBLE_SIDED               = 1 << 11,
    
    // Ratified extensions
    PBR_FEATURE_CLEARCOAT                  = 1 << 12,
    PBR_FEATURE_TRANSMISSION               = 1 << 13,
    PBR_FEATURE_VOLUME                     = 1 << 14,
    PBR_FEATURE_IOR                        = 1 << 15,
    PBR_FEATURE_SPECULAR                   = 1 << 16,
    PBR_FEATURE_SHEEN                      = 1 << 17,
    PBR_FEATURE_IRIDESCENCE                = 1 << 18,
    PBR_FEATURE_ANISOTROPY                 = 1 << 19,
    PBR_FEATURE_DISPERSION                 = 1 << 20,
    PBR_FEATURE_DIFFUSE_TRANSMISSION       = 1 << 21,
    PBR_FEATURE_EMISSIVE_STRENGTH          = 1 << 22,
    
    // Legacy extensions
    PBR_FEATURE_SPECULAR_GLOSSINESS        = 1 << 23,
} PbrFeatureBits;

typedef uint32_t PbrFeatureFlags;

typedef struct PipelineImplementation
{
    VkPipeline           pipeline;
    VkPipelineBindPoint  bindPoint;
    VkBool32             depthWrite;    // whether this variant writes depth
    VkBool32             blendEnable;   // opaque vs. transparent
} PipelineImplementation;

// A logical pipeline class. Known at compile time. Created at init.
// Owns the layout (shared by all its implementations) and the cache.
typedef struct PipelinePrototype
{
    PipelineType                type;
    VkPipelineLayout            layout;           // shared across all implementations
    VkDescriptorSetLayout       descriptorLayout; // material descriptor layout
    uint32_t                    implementationCount;
    PipelineImplementation*     implementations;  // allocated as a flat array, not FAM
    VkPipelineCache             cache;
    PbrFeatureFlags             supportedFeatures; // PBR features supported by this pipeline
} PipelinePrototype;

// Forward declaration of RendererState to avoid circular dependencies
struct RendererState;

// Pure compatibility check helper
bool ano_vk_check_feature_compatibility(PbrFeatureFlags pipelineFeatures, PbrFeatureFlags requiredFeatures, PbrFeatureFlags* outUnsupported);

// Query features globally supported by all active graphics pipelines
PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state);

// Forward declaration of MaterialData to allow initialization helper
struct MaterialData;
void ano_vk_init_default_material_data(struct MaterialData* mat);

#endif
