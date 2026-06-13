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
    PIPELINE_COMPUTE_CULL,      // GPU compute culling
    PIPELINE_TYPE_COUNT         // Sentinel — array sizing, not a real type
} PipelineType;

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

    // Graphics-only:
    uint32_t                colorAttachmentCount;
    VkFormat                colorFormats[4];
    VkFormat                depthFormat;
    VkAttachmentLoadOp      colorLoadOp;
    VkAttachmentLoadOp      depthLoadOp;
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
	MeshData* meshes;
	uint32_t textureCount;
	TextureData* textureBuffers;
} RenderPrimitives;

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data);
void ano_vk_increment_mesh_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_mesh_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data);
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
} PipelinePrototype;

#endif
