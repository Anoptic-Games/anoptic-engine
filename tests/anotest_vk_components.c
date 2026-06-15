#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/components.h"

int main() {
    printf("Starting Vulkan components test...\n");
    
    RenderPrimitives primitives = {0};
    
    // Register some mock components
    MeshData md1 = {0};
    md1.meshRegionIndex = 1;
    ano_vk_register_mesh(&primitives, md1);
    
    MeshData md2 = {0};
    md2.meshRegionIndex = 2;
    ano_vk_register_mesh(&primitives, md2);
    
    assert(primitives.meshCount == 2);
    assert(primitives.meshes[0].usageCount == 0);
    assert(primitives.meshes[1].usageCount == 0);
    
    ano_vk_increment_mesh_usage(&primitives, 0);
    ano_vk_increment_mesh_usage(&primitives, 0);
    ano_vk_increment_mesh_usage(&primitives, 1);
    
    assert(primitives.meshes[0].usageCount == 2);
    assert(primitives.meshes[1].usageCount == 1);
    
    ano_vk_decrement_mesh_usage(&primitives, 0);
    ano_vk_decrement_mesh_usage(&primitives, 1);
    ano_vk_decrement_mesh_usage(&primitives, 1); // Should not go below 0
    
    assert(primitives.meshes[0].usageCount == 1);
    assert(primitives.meshes[1].usageCount == 0);
    
    TextureData td1 = {0};
    td1.textureImage = (VkImage)0x4;
    ano_vk_register_texture(&primitives, td1);
    
    assert(primitives.textureCount == 1);
    ano_vk_increment_texture_usage(&primitives, 0);
    assert(primitives.textureBuffers[0].usageCount == 1);
    
    ano_vk_cleanup_primitives(&primitives);
    
    assert(primitives.meshCount == 0);
    assert(primitives.textureCount == 0);

    // Test PBR feature flags and material default initialization
    MaterialData mat;
    ano_vk_init_default_material_data(&mat);
    assert(mat.features == PBR_FEATURE_NONE);
    assert(mat.baseColorFactor[0] == 1.0f);
    assert(mat.metallicFactor == 1.0f);
    assert(mat.roughnessFactor == 1.0f);
    assert(mat.ior == 1.5f);
    assert(mat.alphaMode == 0);
    assert(mat.emissiveStrength == 1.0f);

    PbrFeatureFlags pipeline = PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_BASE_COLOR_TEXTURE;
    PbrFeatureFlags required = PBR_FEATURE_BASE_COLOR_TEXTURE | PBR_FEATURE_NORMAL_TEXTURE;
    PbrFeatureFlags unsupported = 0;
    
    bool compat = ano_vk_check_feature_compatibility(pipeline, required, &unsupported);
    assert(!compat);
    assert(unsupported == PBR_FEATURE_NORMAL_TEXTURE);

    compat = ano_vk_check_feature_compatibility(pipeline, PBR_FEATURE_BASE_COLOR_TEXTURE, &unsupported);
    assert(compat);
    assert(unsupported == PBR_FEATURE_NONE);
    
    printf("Vulkan components test passed!\n");
    return 0;
}
