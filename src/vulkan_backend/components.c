/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <mimalloc-override.h>
#include "vulkan_backend/components.h"
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

    if (primitives->textureBuffers) {
        free(primitives->textureBuffers);
        primitives->textureBuffers = NULL;
    }
    primitives->textureCount = 0;
}
