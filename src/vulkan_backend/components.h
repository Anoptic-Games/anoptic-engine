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
	PIPELINE_BASIC	= 0,
	PIPELINE_PBR_UNIFORM,
	PIPELINE_PBR_TEXTURE,
	PIPELINE_TYPE_CAP // Not an actual type, corresponds to the total number of types supported
	// Expand with more types as needed
} PipelineType;

// Informs what elements should be included in a descriptor
typedef enum AssetDataType
{
	DATA_UNDEFINED = 0,
	DATA_UBO_CAMERA,			// Uniform buffer object containing model, view, and projection Mat4 transformations
	DATA_UBO_MESH,				// Uniform buffer object containing mesh-specific transformations
	DATA_TEXTURE_BASIC,			// Base color texture
	DATA_LIGHT_POINT			// Omnidirectional light sources
} AssetDataType;

// This might actually make sense. This will have to make sense. Just a basic fucking check. I don't care anymore.
// We'll prolly have another one of this for handling of raw asset data. Like packaging a set of N textures into a 3D ablative map.
// For now these ONLY serve in defining pipeline initialization parameters. They're basically descriptors for descriptors. Start minimal and extend as needed

// Just bits and bytes. It doesn't have to make sense, just match at the binary level. We'll figure it out as we go.
// Patterns are equivalent to descriptor sets, holding data for one pipeline binding
typedef struct DataPattern
{
	uint32_t dataCount;
	AssetDataType* dataTypes; 
} DataPattern;

typedef struct DataChain
{
	uint32_t patternCount;
	DataPattern* patterns;		// These should follow a regular pattern (scene-wide resources go first)
	uint32_t variantBindIndex;	// The pipeline index (point from which we can safely bind mesh-specific resources)
} DataChain;

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
	VertexData vertexBuffers;
	uint32_t indexCount;
	IndexData indexBuffers;
	uint32_t textureCount;
	TextureData textureBuffers;
} RenderPrimitives;

//====================== Pipeline assets

typedef struct PipelineImplementation
{
	PipelineType type;
	VkPipelineBindPoint bindPoint;
	VkPipeline pipeline;
	uint32_t samplerCount;
	VkSampler** textureSamplers; // Texture samplers will be stored in a separate buffer, and linked to by multiple pipelines
} PipelineImplementation;

// Tracks logical pipeline setups
typedef struct PipelinePrototype
{
	PipelineType type;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSetLayout descriptorSetLayout;
	uint32_t implementationCount;
	PipelineImplementation* implementations[];
} PipelinePrototype;

//====================== Render primitives, meshes, and grouping

// Tracks descriptor sets associated with render primitives & mesh-specific values
typedef struct Renderable
{
	uint32_t primitiveCount;
	void* primitives; // When an instance is created or destroyed, these addresses have their uint32_t usageCount values updated
	VkBuffer vertex;
	uint32_t indexCount;
    VkBuffer index;
    VkDescriptorSet descriptorSet; // ONE descriptor set containing ALL of the data
} Renderable;

// Contains all meshes associated with a given pipeline, both prototypes and in-world instances
typedef struct MeshBuffer
{
	PipelineType pipelineType;
	PipelineImplementation* pipeline;
	VkDescriptorPool descriptorPool; // All uniforms of this buffer will be allocated from this
	VkDescriptorSetLayout descriptorSetLayout; // One descriptor set layout covering ALL of the mesh-specific data for members of this buffer
	uint32_t renderableCount;
	Renderable* renderables;
} MeshBuffer;


#endif
