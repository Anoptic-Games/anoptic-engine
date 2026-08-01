#ifndef ANOPTIC_VK_MATERIAL_SCHEMA_H
#define ANOPTIC_VK_MATERIAL_SCHEMA_H

#include <stdint.h>
#include "vulkan_backend/components.h"

enum class AnoGltfTextureSource : uint8_t {
    base_color, metallic_roughness, normal, occlusion, emissive,
    clearcoat, clearcoat_roughness, clearcoat_normal, transmission, thickness,
    specular, specular_color, sheen_color, sheen_roughness, iridescence,
    iridescence_thickness, anisotropy, diffuse_transmission, diffuse_transmission_color,
    count,
};

enum class AnoMaterialTextureDomain : uint8_t { color, data };

struct AnoMaterialDefault final {
    double value;
};

struct AnoMaterialTexture final {
    PbrFeatureFlags feature;
    AnoGltfTextureSource source;
    AnoMaterialTextureDomain domain;
    bool detectsFeature;
};

#endif
