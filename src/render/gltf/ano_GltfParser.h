/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef GLTF_PARSER_H
#define GLTF_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <anoptic_render.h> // AnoRenderableDesc (model_flatten's public output type)
#include "vulkan_backend/structs.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/instance/instanceInit.h"

// ---------------------------------------------------------
// Blueprint Assets
// ---------------------------------------------------------

typedef struct ModelPrimitive {
    uint32_t geometryPoolIndex;
    uint32_t materialIndex; // Index into the model's bindless texture handles, etc.
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

// Represents a loaded glTF file resting in GPU memory but NOT spawned in the world
typedef struct ModelAsset {
    char name[64];
    
    uint32_t nodeCount;
    ModelNode* nodes;
    
    uint32_t meshCount;
    ModelMesh* meshes;
    
    uint32_t rootNodeCount;
    uint32_t* rootNodes;
} ModelAsset;

// ---------------------------------------------------------
// Functions
// ---------------------------------------------------------

// Parses a glTF file, loading assets into GPU memory and returning a ModelAsset blueprint.
ModelAsset* parseGltf(VulkanContext* ctx, const char* fileName);

// Flattens a parsed asset, at `rootTransform`, into renderable primitive descriptors (one per mesh
// primitive: its geometry-pool mesh index, material index, and world transform). Pure CPU, no GPU
// state touched 〜 the render side exposes this so the LOGIC master composes scene instances and
// emits the creates itself (audit: logic owns the scene). Returns the TOTAL primitive count; fills
// out[0..min(count,cap)). Call once with cap 0 (or out NULL) to size, then again to fill.
uint32_t model_flatten(const ModelAsset* asset, const mat4 rootTransform, AnoRenderableDesc* out, uint32_t cap);

// Forward declaration of cgltf_material to avoid header inclusion issues
struct cgltf_material;

// Identifies all the PBR properties and extensions used by a glTF material
PbrFeatureFlags ano_gltf_identify_material_features(const struct cgltf_material* material);

#endif
