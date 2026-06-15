#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/components.h"

int main() {
    printf("Starting Vulkan components test...\n");

    RenderPrimitives primitives = {0};

    // Register some mock meshes
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

    printf("Vulkan components test passed!\n");
    return 0;
}
