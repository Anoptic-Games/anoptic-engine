/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <mimalloc-override.h>
#include "vulkan_backend/components.h"
#include "vulkan_backend/structs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Render config: the pipeline types that emit draws, in partition (draw-slot) order. This —
// not PIPELINE_TYPE_COUNT — sizes the indirect / drawCount / compacted-index buffers and the
// cull UBO draw-slot map. See components.h for the rationale.
const PipelineType ano_draw_pipelines[] = {
    PIPELINE_FLAT,          // slot 0: opaque flat-shaded geometry
    PIPELINE_TRANSMISSION,  // slot 1: refraction / transmission
};

// The cull UBO map (CullUBO.drawSlotOf) is indexed by a material's raw PipelineType, so it
// must span the whole enum; it is stored as uvec4[4] in cull.comp, hence the 16-slot bound.
_Static_assert(PIPELINE_TYPE_COUNT <= 16, "drawSlotOf map (CullUBO/cull.comp) holds 16 entries");

// out: number of draw partitions (drawing pipeline types). Single source of truth for sizing.
uint32_t ano_draw_pipeline_count(void) {
    return (uint32_t)(sizeof(ano_draw_pipelines) / sizeof(ano_draw_pipelines[0]));
}

// in:  type — any PipelineType
// out: its compacted draw-partition index, or ANO_NO_DRAW_SLOT if the type never draws
//      (compute passes, unimplemented skeletons). Linear scan over the tiny draw list.
uint32_t ano_draw_slot_of(PipelineType type) {
    for (uint32_t i = 0; i < ano_draw_pipeline_count(); ++i) {
        if (ano_draw_pipelines[i] == type) return i;
    }
    return ANO_NO_DRAW_SLOT;
}

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data) {
    if (primitives->meshCount >= primitives->meshCapacity) {
        uint32_t newCapacity = primitives->meshCapacity == 0 ? 8 : primitives->meshCapacity * 2;
        void* temp = realloc(primitives->meshes, sizeof(MeshData) * newCapacity);
        if (!temp) {
            printf("Error: Failed to reallocate memory for meshes!\n");
            return;
        }
        primitives->meshes = temp;
        primitives->meshCapacity = newCapacity;
    }
    
    data.usageCount = 0;
    primitives->meshes[primitives->meshCount++] = data;
}

void ano_vk_increment_mesh_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->meshCount) {
        primitives->meshes[index].usageCount++;
    }
}

void ano_vk_decrement_mesh_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->meshCount && primitives->meshes[index].usageCount > 0) {
        primitives->meshes[index].usageCount--;
    }
}

void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data) {
    if (primitives->textureCount >= primitives->textureCapacity) {
        uint32_t newCapacity = primitives->textureCapacity == 0 ? 8 : primitives->textureCapacity * 2;
        void* temp = realloc(primitives->textureBuffers, sizeof(TextureData) * newCapacity);
        if (!temp) {
            printf("Error: Failed to reallocate memory for textures!\n");
            return;
        }
        primitives->textureBuffers = temp;
        primitives->textureCapacity = newCapacity;
    }
    
    data.usageCount = 0;
    primitives->textureBuffers[primitives->textureCount++] = data;
}

void ano_vk_increment_texture_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->textureCount) {
        primitives->textureBuffers[index].usageCount++;
    }
}

void ano_vk_decrement_texture_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->textureCount && primitives->textureBuffers[index].usageCount > 0) {
        primitives->textureBuffers[index].usageCount--;
    }
}

void ano_vk_cleanup_primitives(RenderPrimitives* primitives) {
    if (primitives->meshes) {
        free(primitives->meshes);
        primitives->meshes = NULL;
    }
    primitives->meshCount = 0;
    primitives->meshCapacity = 0; // must track the freed pointer; else re-register skips realloc and derefs NULL

    if (primitives->textureBuffers) {
        free(primitives->textureBuffers);
        primitives->textureBuffers = NULL;
    }
    primitives->textureCount = 0;
    primitives->textureCapacity = 0;
}

bool ano_vk_check_feature_compatibility(PbrFeatureFlags pipelineFeatures, PbrFeatureFlags requiredFeatures, PbrFeatureFlags* outUnsupported) {
    PbrFeatureFlags unsupported = requiredFeatures & ~pipelineFeatures;
    if (outUnsupported) {
        *outUnsupported = unsupported;
    }
    return (unsupported == PBR_FEATURE_NONE);
}

PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state) {
    PbrFeatureFlags features = PBR_FEATURE_NONE;
    for (int i = 0; i < PIPELINE_TYPE_COUNT; ++i) {
        if (state->prototypes[i].layout != VK_NULL_HANDLE && state->prototypes[i].implementationCount > 0) {
            if (state->prototypes[i].implementations[0].bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
                features |= state->prototypes[i].supportedFeatures;
            }
        }
    }
    return features;
}

void ano_vk_init_default_material_data(struct MaterialData* mat) {
    memset(mat, 0, sizeof(struct MaterialData));
    
    // Initialize all texture indices to 0xFFFFFFFF (no texture sentinel)
    mat->baseColorTexture = 0xFFFFFFFF;
    mat->metallicRoughnessTexture = 0xFFFFFFFF;
    mat->normalTexture = 0xFFFFFFFF;
    mat->occlusionTexture = 0xFFFFFFFF;
    mat->emissiveTexture = 0xFFFFFFFF;
    mat->clearcoatTexture = 0xFFFFFFFF;
    mat->clearcoatRoughnessTexture = 0xFFFFFFFF;
    mat->clearcoatNormalTexture = 0xFFFFFFFF;
    mat->transmissionTexture = 0xFFFFFFFF;
    mat->thicknessTexture = 0xFFFFFFFF;
    mat->specularTexture = 0xFFFFFFFF;
    mat->specularColorTexture = 0xFFFFFFFF;
    mat->sheenColorTexture = 0xFFFFFFFF;
    mat->sheenRoughnessTexture = 0xFFFFFFFF;
    mat->iridescenceTexture = 0xFFFFFFFF;
    mat->iridescenceThicknessTexture = 0xFFFFFFFF;
    mat->anisotropyTexture = 0xFFFFFFFF;
    mat->diffuseTransmissionTexture = 0xFFFFFFFF;
    mat->diffuseTransmissionColorTexture = 0xFFFFFFFF;

    mat->features = PBR_FEATURE_NONE;
    
    mat->baseColorFactor[0] = 1.0f;
    mat->baseColorFactor[1] = 1.0f;
    mat->baseColorFactor[2] = 1.0f;
    mat->baseColorFactor[3] = 1.0f;
    
    mat->metallicFactor = 1.0f;
    mat->roughnessFactor = 1.0f;
    
    mat->normalScale = 1.0f;
    
    mat->occlusionStrength = 1.0f;
    
    mat->emissiveStrength = 1.0f;
    
    mat->alphaCutoff = 0.5f;
    mat->alphaMode = 0; // OPAQUE
    
    mat->ior = 1.5f;
    
    mat->specularFactor = 1.0f;
    mat->specularColorFactor[0] = 1.0f;
    mat->specularColorFactor[1] = 1.0f;
    mat->specularColorFactor[2] = 1.0f;
    mat->specularColorFactor[3] = 1.0f;
    
    mat->attenuationDistance = 1e30f;
    mat->attenuationColor[0] = 1.0f;
    mat->attenuationColor[1] = 1.0f;
    mat->attenuationColor[2] = 1.0f;
    mat->attenuationColor[3] = 1.0f;
    
    mat->iridescenceIor = 1.3f;
    mat->iridescenceThicknessMinimum = 100.0f;
    mat->iridescenceThicknessMaximum = 400.0f;
    
    mat->diffuseTransmissionColorFactor[0] = 1.0f;
    mat->diffuseTransmissionColorFactor[1] = 1.0f;
    mat->diffuseTransmissionColorFactor[2] = 1.0f;
    mat->diffuseTransmissionColorFactor[3] = 1.0f;

    mat->pipelineType = 0; // PIPELINE_FLAT
}
