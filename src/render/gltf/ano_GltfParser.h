/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Provides structures and function interfaces for loading glTF assets */

#ifndef GLTF_PARSER_H
#define GLTF_PARSER_H

#include <stdio.h>
#include <stdlib.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/instance/instanceInit.h"

// Types

enum GltfKeys // Should the key hashing function ever be changed, !!UPDATE THESE!!
{
	// Keys from root object
	GLTF_EMPTY					= 0,

	// Keys under 'asset':
	GLTF_ASSET					= 544,
	GLTF_GENERATOR				= 967,
	GLTF_VERSION				= 774,

	// Keys under 'scene':
	GLTF_SCENE					= 526,
	GLTF_SCENES					= 641,
	GLTF_NAME					= 417,
	GLTF_NODES					= 537,

	// Keys under 'node':
	GLTF_MESH					= 429,
	GLTF_ROTATION				= 880,
	GLTF_TRANSLATION			= 1199,
	GLTF_SCALE					= 520,		 

	// Keys under 'material':
	GLTF_MATERIALS				= 962,
	GLTF_DOUBLESIDED			= 1124,
	GLTF_OPACITYFACTOR			= 1368,
	GLTF_PBRMETALICROUGHNESS	= 2093,
	GLTF_BASECOLORTEXTURE		= 1675,
	GLTF_INDEX					= 536,
	GLTF_METALLICFACTOR			= 1450,
	GLTF_ROUGHNESSFACTOR		= 1597,

	// Keys under 'mesh':
	GLTF_MESHES					= 645,
	GLTF_PRIMITIVES				= 1100,
	GLTF_ATTRIBUTES				= 1095,
	GLTF_POSITION				= 629,
	GLTF_NORMAL					= 457,
	GLTF_TEXCOORD_0				= 759,
	GLTF_INDICES				= 735,
	GLTF_MATERIAL				= 847,

	// Keys under 'texture':
	GLTF_TEXTURES				= 900,
	GLTF_SAMPLER				= 756,
	GLTF_SOURCE					= 657,

	// Keys under 'image':
	GLTF_IMAGES					= 630,
	GLTF_MIMETYPE				= 842,
	GLTF_URI					= 336,

	// Keys under 'accessor':
	GLTF_ACCESSORS				= 966,
	GLTF_BUFFERVIEW				= 1045,
	GLTF_COMPONENTTYPE			= 1397,
	GLTF_COUNT					= 553,
	GLTF_MAX					= 326,
	GLTF_MIN					= 324,
	GLTF_TYPE					= 450,

	// Keys under 'bufferView':
	GLTF_BUFFERVIEWS			= 1160,
	GLTF_BUFFER					= 634,
	GLTF_BYTEOFFSET				= 1051,
	GLTF_BYTELENGTH				= 1046,

	// Keys under 'sampler':
	GLTF_SAMPLERS				= 871,
	GLTF_MAGFILTER				= 923,
	GLTF_MINFILTER				= 938,

	// Keys under 'buffer':
	GLTF_BUFFERS				= 749,
	GLTF_BINARYDATA				= 1023

};

enum GltfComponentType
{
	GLTF_COMPONENT_TYPE_BYTE		   = 5120,
	GLTF_COMPONENT_TYPE_UNSIGNED_BYTE  = 5121,
	GLTF_COMPONENT_TYPE_SHORT		  = 5122,
	GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
	GLTF_COMPONENT_TYPE_UNSIGNED_INT   = 5125,
	GLTF_COMPONENT_TYPE_FLOAT		  = 5126
};

enum GltfDataType
{
	GLTF_DATA_TYPE_SCALAR,
	GLTF_DATA_TYPE_VEC2,
	GLTF_DATA_TYPE_VEC3,
	GLTF_DATA_TYPE_VEC4,
	GLTF_DATA_TYPE_MAT2,
	GLTF_DATA_TYPE_MAT3,
	GLTF_DATA_TYPE_MAT4
};

typedef struct BoundingBox
{
	Vector3 max;
	Vector3 min;
} BoundingBox;

typedef struct GltfBuffer
{
	uint32_t index;
	uint64_t byteLength;
	void* address;
	char* uri;
} GltfBuffer;

typedef struct GltfSampler
{
	uint32_t magFilter;
	uint32_t minFilter;
} GltfSampler;

typedef struct GltfBufferView
{
	uint32_t index;
	uint64_t byteLength;
	uint64_t byteOffset;
} GltfBufferView;

typedef struct GltfAccessor
{
	uint32_t bufferView;
	uint32_t componentType;
	uint32_t count;
	union
	{
		float max[4]; // 4 is the max possible size (e.g., for VEC4)
		uint32_t maxInt[4];
	};
	union
	{
		float min[4];
		uint32_t minInt[4];
	};
	char type[6]; // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
} GltfAccessor;

typedef struct GltfImage
{
	char* mimeType;
	char* name;
	char* uri;
} GltfImage;

typedef struct GltfTexture
{
	uint32_t sampler;
	uint32_t source;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
} GltfTexture;

typedef struct GltfPrimitive
{
	uint32_t position;
	uint32_t normal;
	uint32_t texcoord;

	uint32_t indices;
	uint32_t material;
} GltfPrimitive;

typedef struct GltfMesh
{
	char* name;
	VkBuffer vertex;
    VkDeviceMemory vertexMemory;
	uint32_t indexCount;
    VkBuffer index;
    VkDeviceMemory indexMemory;
	GltfPrimitive primitives;
} GltfMesh;

typedef struct PbrMetallicRoughness
{
	uint32_t baseColorTexture;
	float metallicFactor;
	float roughnessFactor;
} PbrMetallicRoughness;

typedef struct GltfMaterial
{
	bool doubleSided;
	char* name;
	PbrMetallicRoughness pbr; 
} GltfMaterial;

typedef struct GltfNode
{
	uint32_t mesh;
	char* name;
	Vector4 rotation;
} GltfNode;

typedef struct GltfScene
{
	char* name;
	uint32_t nodeCount;
	uint32_t* nodes;
} GltfScene;

typedef struct GltfElements
{
	uint32_t sceneCount;
	GltfScene* scenes;
	uint32_t nodeCount;
	GltfNode* nodes;
	uint32_t materialCount;
	GltfMaterial* materials;
	uint32_t meshCount;
	GltfMesh* meshes;
	uint32_t textureCount;
	GltfTexture* textures;
	uint32_t imageCount;
	GltfImage* images;
	uint32_t accessorCount;
	GltfAccessor* accessors;
	uint32_t bufferViewCount;
	GltfBufferView* bufferViews;
	uint32_t samplerCount;
	GltfSampler* samplers;
	uint32_t bufferCount;
	GltfBuffer* buffers;
} GltfElements;

// Function interfaces

bool count_gltf_elements(const char *json, GltfElements *counts);

// Parses a glTF file, adding asset buffers to memory and creating renderable entity packages
bool parseGltf(VulkanComponents* components, const char* fileName);

#endif
