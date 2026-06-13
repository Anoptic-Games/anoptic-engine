/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include "ano_GltfParser.h"
#include <string.h>
#include <mimalloc.h>
#include <mimalloc-override.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

extern GpuAllocator stagingAllocator;
extern RendererState rendererState;

bool parseGltf(VulkanContext* ctx, const char* fileName)
{
    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, fileName, &data);
    
    if (result != cgltf_result_success) {
        printf("Failed to parse glTF file: %s\n", fileName);
        return false;
    }
    
    result = cgltf_load_buffers(&options, data, fileName);
    if (result != cgltf_result_success) {
        printf("Failed to load glTF buffers for: %s\n", fileName);
        cgltf_free(data);
        return false;
    }

    printf("Successfully parsed %s with cgltf!\n", fileName);

    // 1. Upload Geometry
    uint32_t** geomPoolIndices = calloc(data->meshes_count, sizeof(uint32_t*));
    
    for (size_t m = 0; m < data->meshes_count; ++m) {
        cgltf_mesh* mesh = &data->meshes[m];
        geomPoolIndices[m] = calloc(mesh->primitives_count, sizeof(uint32_t));
        
        for (size_t p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &mesh->primitives[p];
            
            // Find accessors
            cgltf_accessor* posAccessor = NULL;
            cgltf_accessor* texAccessor = NULL;
            
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    posAccessor = prim->attributes[a].data;
                } else if (prim->attributes[a].type == cgltf_attribute_type_texcoord) {
                    texAccessor = prim->attributes[a].data;
                }
            }
            
            if (!posAccessor || !prim->indices) {
                printf("Warning: Primitive missing positions or indices. Skipping.\n");
                continue;
            }
            
            uint32_t vertexCount = posAccessor->count;
            Vertex* vertices = calloc(vertexCount, sizeof(Vertex));
            
            for (uint32_t v = 0; v < vertexCount; ++v) {
                cgltf_accessor_read_float(posAccessor, v, &vertices[v].position.v[0], 3);
                if (texAccessor) {
                    cgltf_accessor_read_float(texAccessor, v, &vertices[v].texCoord.v[0], 2);
                }
                vertices[v].color.v[0] = 0.5f;
                vertices[v].color.v[1] = 0.5f;
                vertices[v].color.v[2] = 0.5f;
            }
            
            uint32_t indexCount = prim->indices->count;
            uint16_t* indices = calloc(indexCount, sizeof(uint16_t));
            for (uint32_t i = 0; i < indexCount; ++i) {
                indices[i] = (uint16_t)cgltf_accessor_read_index(prim->indices, i);
            }
            
            geomPoolIndices[m][p] = geometry_pool_upload(&rendererState.globalGeometryPool, &stagingAllocator, ctx->device, rendererState.commandPool, ctx->transferQueue, vertices, vertexCount, indices, indexCount);
            
            free(vertices);
            free(indices);
        }
    }

    // Count staging buffers needed for textures
    uint32_t maxStaging = 10;
    for (size_t t = 0; t < data->textures_count; ++t) {
        if (data->textures[t].image && data->textures[t].image->uri) {
            maxStaging++;
        }
    }

    // 2. Upload Textures
    VkCommandBuffer textureCmd = beginSingleTimeCommands(ctx);
    VkBuffer* stagingBuffers = calloc(maxStaging, sizeof(VkBuffer));
    uint32_t stagingCount = 0;
    
    // We will allocate an array of Vulkan Image Views corresponding to cgltf_textures
    VkImageView* loadedTextures = calloc(data->textures_count, sizeof(VkImageView));
    VkImage* loadedImages = calloc(data->textures_count, sizeof(VkImage));
    GpuAllocation* loadedAllocs = calloc(data->textures_count, sizeof(GpuAllocation));
    bool* textureLoaded = calloc(data->textures_count, sizeof(bool));

    for (size_t t = 0; t < data->textures_count; ++t) {
        cgltf_texture* tex = &data->textures[t];
        if (tex->image && tex->image->uri) {
            bool success = createTextureImage(ctx, textureCmd, &loadedImages[t], &loadedAllocs[t], &loadedTextures[t], (char*)tex->image->uri, false, &stagingBuffers[stagingCount++]);
            textureLoaded[t] = success;
            if (success) {
                TextureData td = {0};
                td.textureImage = loadedImages[t];
                td.textureImageAlloc = loadedAllocs[t];
                td.textureImageView = loadedTextures[t];
                ano_vk_register_texture(&rendererState.primitives, td);
            }
        }
    }

    endSingleTimeCommands(ctx, textureCmd);

    for (uint32_t i = 0; i < stagingCount; ++i) {
        vkDestroyBuffer(ctx->device, stagingBuffers[i], NULL);
    }
    free(stagingBuffers);
    gpu_alloc_reset(&stagingAllocator);

    // Count entities based on node instances
    uint32_t totalEntities = 0;
    for (size_t n = 0; n < data->nodes_count; ++n) {
        if (data->nodes[n].mesh) {
            totalEntities += data->nodes[n].mesh->primitives_count;
        }
    }

    // 3. Package Renderables
    rendererState.entityCount = totalEntities;
    rendererState.entities = calloc(totalEntities, sizeof(RenderEntity));

    uint32_t entityIdx = 0;
    for (size_t n = 0; n < data->nodes_count; ++n) {
        cgltf_node* node = &data->nodes[n];
        if (!node->mesh) continue;

        cgltf_float matrix[16];
        cgltf_node_transform_world(node, matrix);

        cgltf_mesh* mesh = node->mesh;
        size_t m_idx = mesh - data->meshes;

        for (size_t p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &mesh->primitives[p];
            
            rendererState.entities[entityIdx].meshIndex = geomPoolIndices[m_idx][p];
            
            // Copy transform matrix
            float* destMat = (float*)&rendererState.entities[entityIdx].transform;
            for (int i = 0; i < 16; i++) {
                destMat[i] = matrix[i];
            }
            
            uint32_t bindlessTexIdx = 0; // Fallback
            if (prim->material && prim->material->has_pbr_metallic_roughness) {
                cgltf_texture_view* baseColor = &prim->material->pbr_metallic_roughness.base_color_texture;
                if (baseColor->texture) {
                    size_t texIdx = baseColor->texture - data->textures;
                    if (textureLoaded[texIdx]) {
                        bindlessTexIdx = bindless_register_texture(ctx, &rendererState.bindlessTextures, loadedTextures[texIdx], rendererState.textureSampler);
                    }
                }
            }

            uint32_t matIdx = entityIdx;
            rendererState.entities[entityIdx].materialIndex = matIdx;
            
            MaterialData matData = {0};
            matData.albedoIndex = bindlessTexIdx;
            matData.roughness = 1.0f;
            matData.color[0] = 1.0f;
            matData.color[1] = 1.0f;
            matData.color[2] = 1.0f;
            matData.color[3] = 1.0f;
            
            for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
                rendererState.materialBuffer.mapped[frame][matIdx] = matData;
            }
            rendererState.materialBuffer.count++;
            
            entityIdx++;
        }
    }

    for (size_t m = 0; m < data->meshes_count; ++m) {
        free(geomPoolIndices[m]);
    }
    free(geomPoolIndices);
    
    free(loadedTextures);
    free(loadedImages);
    free(loadedAllocs);
    free(textureLoaded);
    
    cgltf_free(data);
    printf("Loading complete via cgltf!\n");
    return true;
}
