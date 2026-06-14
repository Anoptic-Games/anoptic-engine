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

// Forward declaration for internal recursive instantiation
static void instantiate_node(ModelAsset* asset, uint32_t nodeIndex, mat4 parentTransform);

ModelAsset* parseGltf(VulkanContext* ctx, const char* fileName)
{
    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, fileName, &data);
    
    if (result != cgltf_result_success) {
        printf("Failed to parse glTF file: %s\n", fileName);
        return NULL;
    }
    
    result = cgltf_load_buffers(&options, data, fileName);
    if (result != cgltf_result_success) {
        printf("Failed to load glTF buffers for: %s\n", fileName);
        cgltf_free(data);
        return NULL;
    }

    printf("Successfully parsed %s with cgltf!\n", fileName);

    ModelAsset* asset = calloc(1, sizeof(ModelAsset));
    strncpy(asset->name, fileName, 63);

    // 1. Upload Geometry & Map to Asset Meshes
    asset->meshCount = data->meshes_count;
    asset->meshes = calloc(asset->meshCount, sizeof(ModelMesh));
    
    for (size_t m = 0; m < data->meshes_count; ++m) {
        cgltf_mesh* cgMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        
        outMesh->primitiveCount = cgMesh->primitives_count;
        outMesh->primitives = calloc(outMesh->primitiveCount, sizeof(ModelPrimitive));
        
        for (size_t p = 0; p < cgMesh->primitives_count; ++p) {
            cgltf_primitive* prim = &cgMesh->primitives[p];
            
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
                vertices[v].color.v[0] = 1.0f;
                vertices[v].color.v[1] = 1.0f;
                vertices[v].color.v[2] = 1.0f;
            }
            
            uint32_t indexCount = prim->indices->count;
            uint16_t* indices = calloc(indexCount, sizeof(uint16_t));
            for (uint32_t i = 0; i < indexCount; ++i) {
                indices[i] = (uint16_t)cgltf_accessor_read_index(prim->indices, i);
            }
            
            outMesh->primitives[p].geometryPoolIndex = geometry_pool_upload(
                &rendererState.globalGeometryPool, 
                &stagingAllocator, 
                ctx->device, 
                ctx->queueFamilyIndices.transferFamily, 
                ctx->transferQueue, 
                vertices, vertexCount, 
                indices, indexCount
            );
            
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

    // 2. Upload Textures & Bind Materials
    VkCommandBuffer textureCmd = beginSingleTimeCommands(ctx);
    VkBuffer* stagingBuffers = calloc(maxStaging, sizeof(VkBuffer));
    uint32_t stagingCount = 0;
    
    VkImageView* loadedTextures = calloc(data->textures_count, sizeof(VkImageView));
    VkImage* loadedImages = calloc(data->textures_count, sizeof(VkImage));
    GpuAllocation* loadedAllocs = calloc(data->textures_count, sizeof(GpuAllocation));
    bool* textureLoaded = calloc(data->textures_count, sizeof(bool));

    for (size_t t = 0; t < data->textures_count; ++t) {
        cgltf_texture* tex = &data->textures[t];
        if (tex->image && tex->image->uri) {
            bool success = createTextureImage(
                ctx, textureCmd, &loadedImages[t], &loadedAllocs[t], 
                &loadedTextures[t], (char*)tex->image->uri, false, 
                &stagingBuffers[stagingCount++]
            );
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

    // Pre-validate material buffer capacity
    uint32_t totalPrimitives = 0;
    for (size_t m = 0; m < data->meshes_count; ++m) {
        totalPrimitives += data->meshes[m].primitives_count;
    }
    if (rendererState.materialBuffer.count + totalPrimitives > rendererState.materialBuffer.capacity) {
        printf("Warning: Material buffer cannot fit %u new materials (Capacity: %u, Current: %u). Some materials will fall back to index 0.\n", 
               totalPrimitives, rendererState.materialBuffer.capacity, rendererState.materialBuffer.count);
    }

    // 3. Bake Material SSBO entries per primitive
    for (size_t m = 0; m < data->meshes_count; ++m) {
        cgltf_mesh* cgMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        
        for (size_t p = 0; p < cgMesh->primitives_count; ++p) {
            cgltf_primitive* prim = &cgMesh->primitives[p];
            
            uint32_t bindlessTexIdx = 0; // Fallback index
            if (prim->material && prim->material->has_pbr_metallic_roughness) {
                cgltf_texture_view* baseColor = &prim->material->pbr_metallic_roughness.base_color_texture;
                if (baseColor->texture) {
                    size_t texIdx = baseColor->texture - data->textures;
                    if (textureLoaded[texIdx]) {
                        bindlessTexIdx = bindless_register_texture(
                            ctx, &rendererState.bindlessTextures, 
                            loadedTextures[texIdx], rendererState.textureSampler
                        );
                    }
                }
            }

            // Assign a persistent material index in the global SSBO
            uint32_t matIdx = 0;
            bool writeMaterial = false;
            
            if (rendererState.materialBuffer.count < rendererState.materialBuffer.capacity) {
                matIdx = rendererState.materialBuffer.count++;
                writeMaterial = true;
            } else {
                // If capacity is exhausted, reuse index 0 (fallback)
                matIdx = 0;
            }
            
            outMesh->primitives[p].materialIndex = matIdx;
            
            if (writeMaterial) {
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
            }
        }
    }

    free(loadedTextures);
    free(loadedImages);
    free(loadedAllocs);
    free(textureLoaded);

    // 4. Construct Node Hierarchy
    asset->nodeCount = data->nodes_count;
    asset->nodes = calloc(asset->nodeCount, sizeof(ModelNode));
    
    for (size_t n = 0; n < data->nodes_count; ++n) {
        cgltf_node* cgNode = &data->nodes[n];
        ModelNode* outNode = &asset->nodes[n];
        
        if (cgNode->name) {
            strncpy(outNode->name, cgNode->name, 63);
        }
        
        // Extract local transform
        cgltf_float matrix[16];
        cgltf_node_transform_local(cgNode, matrix);
        float* destMat = (float*)&outNode->localTransform;
        for (int i = 0; i < 16; i++) destMat[i] = matrix[i];
        
        outNode->meshIndex = cgNode->mesh ? (cgNode->mesh - data->meshes) : -1;
        outNode->parentIndex = cgNode->parent ? (cgNode->parent - data->nodes) : -1;
        
        outNode->childCount = cgNode->children_count;
        if (outNode->childCount > 0) {
            outNode->childIndices = calloc(outNode->childCount, sizeof(uint32_t));
            for (uint32_t c = 0; c < outNode->childCount; ++c) {
                outNode->childIndices[c] = cgNode->children[c] - data->nodes;
            }
        }
    }
    
    // Store Root Nodes (Fallback to all parentless nodes if scene is incomplete)
    uint32_t rootCount = 0;
    for (size_t n = 0; n < data->nodes_count; ++n) {
        if (!data->nodes[n].parent) rootCount++;
    }
    
    asset->rootNodeCount = rootCount;
    if (rootCount > 0) {
        asset->rootNodes = calloc(rootCount, sizeof(uint32_t));
        uint32_t rIdx = 0;
        for (size_t n = 0; n < data->nodes_count; ++n) {
            if (!data->nodes[n].parent) {
                asset->rootNodes[rIdx++] = n;
            }
        }
    }

    cgltf_free(data);
    printf("Successfully extracted ModelAsset: %s\n", fileName);
    return asset;
}

static void instantiate_node(ModelAsset* asset, uint32_t nodeIndex, mat4 parentTransform) {
    ModelNode* node = &asset->nodes[nodeIndex];
    
    mat4 worldTransform;
    multiplyMat4(worldTransform, parentTransform, node->localTransform);
    
    // If the node has a mesh, spawn a RenderEntity for each of its primitives
    if (node->meshIndex >= 0) {
        ModelMesh* mesh = &asset->meshes[node->meshIndex];
        
        // Reallocate the entities array to fit new instances
        uint32_t currentCount = rendererState.entityCount;
        rendererState.entityCount += mesh->primitiveCount;
        rendererState.entities = realloc(rendererState.entities, rendererState.entityCount * sizeof(RenderEntity));
        
        for (uint32_t p = 0; p < mesh->primitiveCount; p++) {
            ModelPrimitive* prim = &mesh->primitives[p];
            uint32_t entIdx = currentCount + p;
            
            rendererState.entities[entIdx].meshIndex = prim->geometryPoolIndex;
            rendererState.entities[entIdx].materialIndex = prim->materialIndex;
            
            float* destMat = (float*)&rendererState.entities[entIdx].transform;
            float* srcMat = (float*)&worldTransform;
            for (int i = 0; i < 16; i++) {
                destMat[i] = srcMat[i];
            }
        }
    }
    
    // Recurse down children
    for (uint32_t c = 0; c < node->childCount; c++) {
        instantiate_node(asset, node->childIndices[c], worldTransform);
    }
}

void instantiate_model(ModelAsset* asset, mat4 rootTransform) {
    if (!asset) return;
    
    // Start traversal from root nodes
    for (uint32_t r = 0; r < asset->rootNodeCount; r++) {
        instantiate_node(asset, asset->rootNodes[r], rootTransform);
    }
}
