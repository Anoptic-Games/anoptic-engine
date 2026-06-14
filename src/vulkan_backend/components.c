/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include "vulkan_backend/components.h"
#include <stdlib.h>
#include <string.h>

void ano_vk_register_mesh(RenderPrimitives* primitives, MeshData data) {
    primitives->meshCount++;
    primitives->meshes = realloc(primitives->meshes, sizeof(MeshData) * primitives->meshCount);
    data.usageCount = 0;
    primitives->meshes[primitives->meshCount - 1] = data;
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
    if (primitives->meshes) {
        free(primitives->meshes);
        primitives->meshes = NULL;
    }
    primitives->meshCount = 0;

    if (primitives->textureBuffers) {
        free(primitives->textureBuffers);
        primitives->textureBuffers = NULL;
    }
    primitives->textureCount = 0;
}
