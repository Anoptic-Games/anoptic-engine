/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef RENDER_COMPONENTS_H
#define RENDER_COMPONENTS_H

#include <vulkan/vulkan.h>
#include "gpu_alloc.h"

/* Enums */

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

inline constexpr uint32_t ANO_NO_DRAW_SLOT = UINT32_MAX;
inline constexpr PbrFeatureFlags ANO_PBR_FLAT_FEATURES =
    PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_BASE_COLOR_TEXTURE |
    PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR | PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE |
    PBR_FEATURE_NORMAL_TEXTURE | PBR_FEATURE_OCCLUSION_TEXTURE |
    PBR_FEATURE_ALPHA_MODE_OPAQUE | PBR_FEATURE_ALPHA_MODE_BLEND;
inline constexpr PbrFeatureFlags ANO_PBR_TRANSMISSION_FEATURES =
    ANO_PBR_FLAT_FEATURES | PBR_FEATURE_TRANSMISSION | PBR_FEATURE_VOLUME |
    PBR_FEATURE_IOR | PBR_FEATURE_DOUBLE_SIDED;
inline constexpr PbrFeatureFlags ANO_PBR_ADDITIVE_FEATURES =
    PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_BASE_COLOR_TEXTURE |
    PBR_FEATURE_EMISSIVE_FACTOR | PBR_FEATURE_EMISSIVE_TEXTURE |
    PBR_FEATURE_EMISSIVE_STRENGTH | PBR_FEATURE_ALPHA_MODE_BLEND |
    PBR_FEATURE_DOUBLE_SIDED;

enum class AnoPipelineKind : uint8_t { skeleton, graphics, compute };
enum class AnoPipelineSchedule : uint8_t { none, frame, explicit_path };
enum class AnoShaderFamily : uint8_t {
    none, flat, flat_masked, transmission, additive, cull, update, scatter,
    tpsort, lightcull, shadowsetup, lightsetup, hiz, textraster
};

struct AnoPipelineSpec final {
    AnoPipelineKind kind;
    AnoPipelineSchedule schedule;
    uint32_t implementationCount;
    uint32_t drawSlot;
    PbrFeatureFlags supportedFeatures;
    AnoShaderFamily shader;
};
struct AnoPipelineSentinel final {};

typedef enum PipelineType
{
    PIPELINE_FLAT [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 0, ANO_PBR_FLAT_FEATURES, AnoShaderFamily::flat}]] = 0,
    PIPELINE_PARTICLE [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_SDF_COMPOSITE [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_UI [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_TRANSMISSION [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 2, 1, ANO_PBR_TRANSMISSION_FEATURES, AnoShaderFamily::transmission}]],
    PIPELINE_ADDITIVE [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 1, 2, ANO_PBR_ADDITIVE_FEATURES, AnoShaderFamily::additive}]],
    PIPELINE_FLAT_TWOSIDED [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 3, ANO_PBR_FLAT_FEATURES | PBR_FEATURE_DOUBLE_SIDED, AnoShaderFamily::flat}]],
    PIPELINE_FLAT_MASKED [[=AnoPipelineSpec{AnoPipelineKind::graphics, AnoPipelineSchedule::frame, 3, 4, ANO_PBR_FLAT_FEATURES | PBR_FEATURE_DOUBLE_SIDED | PBR_FEATURE_ALPHA_MODE_MASK, AnoShaderFamily::flat_masked}]],
    PIPELINE_COMPUTE_CULL [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::cull}]],
    PIPELINE_COMPUTE_UPDATE [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::update}]],
    PIPELINE_COMPUTE_SCATTER [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::scatter}]],
    PIPELINE_COMPUTE_TPSORT [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::tpsort}]],
    // Skeleton slots: buffers/prototype table size for them, no pipeline created yet
    PIPELINE_DECAL [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_SKINNED [[=AnoPipelineSpec{AnoPipelineKind::skeleton, AnoPipelineSchedule::none, 0, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::none}]],
    PIPELINE_COMPUTE_LIGHTCULL [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::lightcull}]],
    PIPELINE_COMPUTE_SHADOWSETUP [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::shadowsetup}]],
    PIPELINE_COMPUTE_LIGHTSETUP [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::frame, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::lightsetup}]],
    PIPELINE_COMPUTE_HIZ [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 2, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::hiz}]],
    PIPELINE_COMPUTE_TEXTRASTER [[=AnoPipelineSpec{AnoPipelineKind::compute, AnoPipelineSchedule::explicit_path, 1, ANO_NO_DRAW_SLOT, PBR_FEATURE_NONE, AnoShaderFamily::textraster}]],
    PIPELINE_TYPE_COUNT [[=AnoPipelineSentinel{}]]
} PipelineType;

uint32_t ano_draw_pipeline_count(void);         // number of drawing types == per-camera-view draw-slot stride
uint32_t ano_draw_slot_of(PipelineType type);   // enum -> draw slot, ANO_NO_DRAW_SLOT if it never draws

// Compacted-draw partitions: camera views get every draw slot (view*drawSlotCount+slot); each shadow frustum gets solid + MASKED. Single sizing source for the three buffers and their cull map.
uint32_t ano_draw_partition_count(void);

typedef enum PassType
{
    PASS_COMPUTE,       // compute dispatch
    PASS_GRAPHICS,      // rasterization pass
} PassType;

typedef struct RenderPassDef
{
    PassType            type;
    PipelineType        prototype;              // which pipeline prototype to bind
    uint32_t            implementationIndex;    // which variant (opaque, transparent, etc.)
    // Recorded once per view vs once per frame
    bool                perView;

    // Graphics-only:
    uint32_t                colorAttachmentCount;
    VkFormat                colorFormats[4];
    VkFormat                depthFormat;
    VkAttachmentLoadOp      colorLoadOp;
    VkAttachmentLoadOp      depthLoadOp;
    // STORE when a later pass must read this pass's depth (opaque -> transmission)
    VkAttachmentStoreOp     depthStoreOp;
    VkClearValue            colorClear;
    VkClearValue            depthClear;
    VkResolveModeFlagBits   resolveMode;
    // Emit a depth write->read barrier on this view's depth image before this pass begins
    bool                    depthBarrierBefore;

    // Compute-only:
    uint32_t                dispatchX, dispatchY, dispatchZ;
} RenderPassDef;

/* Primitive Assets */

// One image + alloc; optional srgbView (colour) and unormView (data)
typedef struct TextureData
{
	uint32_t usageCount; // number of active meshes using this resource
	VkImage textureImage;
	GpuAllocation textureImageAlloc;
	VkImageView srgbView;    // colour, or VK_NULL_HANDLE
	VkImageView unormView;   // data, or VK_NULL_HANDLE
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

// Out: true if registered; false leaves registry unchanged (caller keeps handles)
[[nodiscard]] bool ano_vk_register_texture(RenderPrimitives* primitives, TextureData data);
void ano_vk_increment_texture_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_texture_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_cleanup_primitives(RenderPrimitives* primitives);

typedef struct PipelineImplementation
{
    VkPipeline           pipeline;
    VkPipelineBindPoint  bindPoint;
    VkBool32             depthWrite;    // whether this variant writes depth
    VkBool32             blendEnable;   // opaque vs. transparent
} PipelineImplementation;

// A logical pipeline class. Owns the layout and the cache.
typedef struct PipelinePrototype
{
    PipelineType                type;
    VkPipelineLayout            layout;           // shared across all implementations
    VkDescriptorSetLayout       descriptorLayout; // material descriptor layout
    uint32_t                    implementationCount;
    PipelineImplementation*     implementations;  // flat array
    VkPipelineCache             cache;
    PbrFeatureFlags             supportedFeatures; // PBR features supported by this pipeline
} PipelinePrototype;

struct RendererState;

// Pure compatibility check helper
bool ano_vk_check_feature_compatibility(PbrFeatureFlags pipelineFeatures, PbrFeatureFlags requiredFeatures, PbrFeatureFlags* outUnsupported);

// Query features globally supported by all active graphics pipelines
PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state);

struct MaterialData;
void ano_vk_init_default_material_data(struct MaterialData* mat);

#endif
