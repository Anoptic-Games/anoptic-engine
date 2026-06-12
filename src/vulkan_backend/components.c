/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include "vulkan_backend/components.h"
#include <stdlib.h>
#include <string.h>

void ano_vk_register_vertex(RenderPrimitives* primitives, VertexData data) {
    primitives->vertexCount++;
    primitives->vertexBuffers = realloc(primitives->vertexBuffers, sizeof(VertexData) * primitives->vertexCount);
    data.usageCount = 0;
    primitives->vertexBuffers[primitives->vertexCount - 1] = data;
}

void ano_vk_increment_vertex_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->vertexCount) {
        primitives->vertexBuffers[index].usageCount++;
    }
}

void ano_vk_decrement_vertex_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->vertexCount && primitives->vertexBuffers[index].usageCount > 0) {
        primitives->vertexBuffers[index].usageCount--;
    }
}

void ano_vk_register_index(RenderPrimitives* primitives, IndexData data) {
    primitives->indexCount++;
    primitives->indexBuffers = realloc(primitives->indexBuffers, sizeof(IndexData) * primitives->indexCount);
    data.usageCount = 0;
    primitives->indexBuffers[primitives->indexCount - 1] = data;
}

void ano_vk_increment_index_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->indexCount) {
        primitives->indexBuffers[index].usageCount++;
    }
}

void ano_vk_decrement_index_usage(RenderPrimitives* primitives, uint32_t index) {
    if (index < primitives->indexCount && primitives->indexBuffers[index].usageCount > 0) {
        primitives->indexBuffers[index].usageCount--;
    }
}

void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data) {
    primitives->textureCount++;
    primitives->textureBuffers = realloc(primitives->textureBuffers, sizeof(TextureData) * primitives->textureCount);
    data.usageCount = 0;
    primitives->textureBuffers[primitives->textureCount - 1] = data;
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
    if (primitives->vertexBuffers) {
        free(primitives->vertexBuffers);
        primitives->vertexBuffers = NULL;
    }
    primitives->vertexCount = 0;

    if (primitives->indexBuffers) {
        free(primitives->indexBuffers);
        primitives->indexBuffers = NULL;
    }
    primitives->indexCount = 0;

    if (primitives->textureBuffers) {
        free(primitives->textureBuffers);
        primitives->textureBuffers = NULL;
    }
    primitives->textureCount = 0;
}
