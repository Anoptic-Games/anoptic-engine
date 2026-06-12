#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "vulkan_backend/components.h"

int main() {
    printf("Starting Vulkan components test...\n");
    
    RenderPrimitives primitives = {0};
    
    // Register some mock components
    VertexData vd1 = {0};
    vd1.vertex = (VkBuffer)0x1;
    ano_vk_register_vertex(&primitives, vd1);
    
    VertexData vd2 = {0};
    vd2.vertex = (VkBuffer)0x2;
    ano_vk_register_vertex(&primitives, vd2);
    
    assert(primitives.vertexCount == 2);
    assert(primitives.vertexBuffers[0].usageCount == 0);
    assert(primitives.vertexBuffers[1].usageCount == 0);
    
    ano_vk_increment_vertex_usage(&primitives, 0);
    ano_vk_increment_vertex_usage(&primitives, 0);
    ano_vk_increment_vertex_usage(&primitives, 1);
    
    assert(primitives.vertexBuffers[0].usageCount == 2);
    assert(primitives.vertexBuffers[1].usageCount == 1);
    
    ano_vk_decrement_vertex_usage(&primitives, 0);
    ano_vk_decrement_vertex_usage(&primitives, 1);
    ano_vk_decrement_vertex_usage(&primitives, 1); // Should not go below 0
    
    assert(primitives.vertexBuffers[0].usageCount == 1);
    assert(primitives.vertexBuffers[1].usageCount == 0);
    
    IndexData id1 = {0};
    id1.index = (VkBuffer)0x3;
    ano_vk_register_index(&primitives, id1);
    
    assert(primitives.indexCount == 1);
    ano_vk_increment_index_usage(&primitives, 0);
    assert(primitives.indexBuffers[0].usageCount == 1);
    
    TextureData td1 = {0};
    td1.textureImage = (VkImage)0x4;
    ano_vk_register_texture(&primitives, td1);
    
    assert(primitives.textureCount == 1);
    ano_vk_increment_texture_usage(&primitives, 0);
    assert(primitives.textureBuffers[0].usageCount == 1);
    
    ano_vk_cleanup_primitives(&primitives);
    
    assert(primitives.vertexCount == 0);
    assert(primitives.indexCount == 0);
    assert(primitives.textureCount == 0);
    
    printf("Vulkan components test passed!\n");
    return 0;
}
