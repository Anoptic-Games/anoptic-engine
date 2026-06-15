/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <mimalloc-override.h>
#include "vulkan_backend/components.h"
#include "vulkan_backend/structs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
