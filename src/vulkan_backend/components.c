/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <anoptic_memory.h>
#include <anoptic_log.h>
#include "vulkan_backend/components.h"
#include "vulkan_backend/structs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <meta>
#include <type_traits>

// Drawing pipeline types in draw-slot order.
const PipelineType ano_draw_pipelines[] = {
    PIPELINE_FLAT,          // opaque
    PIPELINE_TRANSMISSION,  // transmission
    PIPELINE_ADDITIVE,      // additive
    PIPELINE_FLAT_TWOSIDED, // doubleSided
    PIPELINE_FLAT_MASKED,   // MASK cutout
};

// drawSlotOf map: 16 entries (cull.comp uvec4[4]).
static_assert(PIPELINE_FLAT_MASKED < 16, "material-carried pipeline types must fit the 16-entry drawSlotOf map (CullUBO/cull.comp)");

// out: draw-pipeline count (== per-view draw-slot stride)
uint32_t ano_draw_pipeline_count(void) {
    return (uint32_t)(sizeof(ano_draw_pipelines) / sizeof(ano_draw_pipelines[0]));
}

// out: compacted-draw partition count
uint32_t ano_draw_partition_count(void) {
    return ANO_VIEW_COUNT * ano_draw_pipeline_count() + 2u * ANO_SHADOW_FRUSTUM_COUNT;
}

// in:  type (any PipelineType)
// out: draw-partition index, or ANO_NO_DRAW_SLOT if never draws
uint32_t ano_draw_slot_of(PipelineType type) {
    for (uint32_t i = 0; i < ano_draw_pipeline_count(); ++i) {
        if (ano_draw_pipelines[i] == type) return i;
    }
    return ANO_NO_DRAW_SLOT;
}

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data) {
    if (primitives->meshCount >= primitives->meshCapacity) {
        uint32_t newCapacity = primitives->meshCapacity == 0 ? 8 : primitives->meshCapacity * 2;
        MeshData* temp = static_cast<MeshData*>(realloc(primitives->meshes, sizeof(MeshData) * newCapacity));
        if (!temp) {
            ano_log(ANO_ERROR, "Error: Failed to reallocate memory for meshes!");
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

// in:  primitives, data
// out: true on append; false on realloc failure (primitives unchanged)
bool ano_vk_register_texture(RenderPrimitives* primitives, TextureData data) {
    if (primitives->textureCount >= primitives->textureCapacity) {
        uint32_t newCapacity = primitives->textureCapacity == 0 ? 8 : primitives->textureCapacity * 2;
        TextureData* temp = static_cast<TextureData*>(realloc(primitives->textureBuffers, sizeof(TextureData) * newCapacity));
        if (!temp) {
            ano_log(ANO_ERROR, "Error: Failed to reallocate memory for textures!");
            return false;
        }
        primitives->textureBuffers = temp;
        primitives->textureCapacity = newCapacity;
    }

    data.usageCount = 0;
    primitives->textureBuffers[primitives->textureCount++] = data;
    return true;
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
    primitives->meshCapacity = 0;

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

    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto defaults = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialDefault));
        static_assert(defaults.size() <= 1);
        if constexpr (!defaults.empty()) {
            constexpr auto value = std::meta::extract<AnoMaterialDefault>(defaults[0]).value;
            using Field = [:std::meta::type_of(member):];
            auto& field = mat->*(&[:member:]);
            if constexpr (std::is_array_v<Field>) {
                using Element = std::remove_extent_t<Field>;
                static_assert(std::is_arithmetic_v<Element>);
                for (auto& element : field) element = static_cast<Element>(value);
            } else {
                static_assert(std::is_arithmetic_v<Field>);
                field = static_cast<Field>(value);
            }
        }
    }
}
