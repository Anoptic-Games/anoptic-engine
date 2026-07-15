/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef GLTF_PARSER_H
#define GLTF_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <anoptic_render.h> // AnoRenderableDesc
#include "vulkan_backend/structs.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/instance/instanceInit.h"


/* Blueprint Assets */

typedef struct ModelPrimitive {
    uint32_t geometryPoolIndex;
    uint32_t materialIndex; // material palette index
} ModelPrimitive;

typedef struct ModelMesh {
    uint32_t primitiveCount;
    ModelPrimitive* primitives;
} ModelMesh;

typedef struct ModelNode {
    char name[64];
    mat4 localTransform;
    int32_t meshIndex;   // -1 if no mesh
    int32_t parentIndex; // -1 if root node
    uint32_t childCount;
    uint32_t* childIndices;
} ModelNode;

// CPU blueprint (GPU mesh/material indices); not spawned.
typedef struct ModelAsset {
    char name[64];
    
    uint32_t nodeCount;
    ModelNode* nodes;
    
    uint32_t meshCount;
    ModelMesh* meshes;
    
    uint32_t rootNodeCount;
    uint32_t* rootNodes;
} ModelAsset;


/* Functions */

// Ingest logical path via resource manager (anoresgfx scene) into ModelAsset blueprint. Caller owns return; NULL on failure.
ModelAsset* parseGltf(VulkanContext* ctx, const char* logical);

// Flatten asset at `rootTransform` into AnoRenderableDesc per mesh prim (CPU only). Returns TOTAL count; fills out[0..min(count,cap)). Cap 0 / out NULL sizes.
uint32_t model_flatten(const ModelAsset* asset, const mat4 rootTransform, AnoRenderableDesc* out, uint32_t cap);

#endif
