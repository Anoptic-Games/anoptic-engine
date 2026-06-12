/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef RENDER_COMPONENTS_H
#define RENDER_COMPONENTS_H

#include <vulkan/vulkan.h>

//====================== Enums

typedef enum PipelineType
{
    PIPELINE_FLAT = 0,          // Flat-shaded geometry (replaces PIPELINE_BASIC)
    PIPELINE_PARTICLE,          // Point-sprite / billboard particles
    PIPELINE_SDF_COMPOSITE,     // SDF raymarching compositing pass (future)
    PIPELINE_UI,                // UI overlay (future)
    PIPELINE_TYPE_COUNT         // Sentinel — array sizing, not a real type
} PipelineType;

//====================== Primitive assets

typedef struct TextureData
{
	uint32_t usageCount; // number of active meshes using this resource
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
} TextureData;

typedef struct IndexData
{
	uint32_t usageCount; // number of active meshes using this resource
	uint32_t indexCount;
    VkBuffer index;
    VkDeviceMemory indexMemory;	
} IndexData;

typedef struct VertexData
{
	uint32_t usageCount; // number of active meshes using this resource
    VkBuffer vertex;
    VkDeviceMemory vertexMemory;
} VertexData;

//


// Tracks loaded graphics resources and their usage
typedef struct RenderPrimitives
{
	uint32_t vertexCount;
	VertexData* vertexBuffers;
	uint32_t indexCount;
	IndexData* indexBuffers;
	uint32_t textureCount;
	TextureData* textureBuffers;
} RenderPrimitives;

void ano_vk_register_vertex(RenderPrimitives* primitives, VertexData data);
void ano_vk_increment_vertex_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_vertex_usage(RenderPrimitives* primitives, uint32_t index);

void ano_vk_register_index(RenderPrimitives* primitives, IndexData data);
void ano_vk_increment_index_usage(RenderPrimitives* primitives, uint32_t index);
void ano_vk_decrement_index_usage(RenderPrimitives* primitives, uint32_t index);

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
    uint32_t                    implementationCount;
    PipelineImplementation*     implementations;  // allocated as a flat array, not FAM
    VkPipelineCache             cache;
} PipelinePrototype;

#endif
