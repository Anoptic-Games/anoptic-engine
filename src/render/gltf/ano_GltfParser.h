/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Provides structures and function interfaces for loading glTF assets */

#ifndef GLTF_PARSER_H
#define GLTF_PARSER_H

#include <stdio.h>
#include <stdlib.h>

#include "vulkan_backend/vertex/vertex.h"
#include "jsmn.h"

// Types

enum GltfComponentType
{
    GLTF_COMPONENT_TYPE_BYTE           = 5120,
    GLTF_COMPONENT_TYPE_UNSIGNED_BYTE  = 5121,
    GLTF_COMPONENT_TYPE_SHORT          = 5122,
    GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
    GLTF_COMPONENT_TYPE_UNSIGNED_INT   = 5125,
    GLTF_COMPONENT_TYPE_FLOAT          = 5126
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
	uint32_t index;
	uint32_t componentType;
	uint32_t dataType;
	uint64_t count;
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

#endif
