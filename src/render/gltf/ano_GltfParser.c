/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include "ano_GltfParser.h"
#include <string.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

extern GpuAllocator stagingAllocator;
extern RendererState rendererState;

// Forward declaration for the internal recursive flatten walk (model_flatten).
static void flatten_node(const ModelAsset* asset, uint32_t nodeIndex, const mat4 parentTransform,
                         AnoRenderableDesc* out, uint32_t cap, uint32_t* idx);

ModelAsset* parseGltf(VulkanContext* ctx, const char* fileName)
{
    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, fileName, &data);
    
    if (result != cgltf_result_success) {
        ano_log(ANO_ERROR, "Failed to parse glTF file: %s", fileName);
        return NULL;
    }
    
    result = cgltf_load_buffers(&options, data, fileName);
    if (result != cgltf_result_success) {
        ano_log(ANO_ERROR, "Failed to load glTF buffers for: %s", fileName);
        cgltf_free(data);
        return NULL;
    }

    ano_debug_log(ANO_INFO, "Successfully parsed %s with cgltf!", fileName);

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
            cgltf_accessor* normAccessor = NULL;
            cgltf_accessor* texAccessor = NULL;
            
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    posAccessor = prim->attributes[a].data;
                } else if (prim->attributes[a].type == cgltf_attribute_type_normal) {
                    normAccessor = prim->attributes[a].data;
                } else if (prim->attributes[a].type == cgltf_attribute_type_texcoord) {
                    texAccessor = prim->attributes[a].data;
                }
            }
            
            if (!posAccessor || !prim->indices) {
                ano_log(ANO_WARN, "Warning: Primitive missing positions or indices. Skipping.");
                continue;
            }
            
            uint32_t vertexCount = posAccessor->count;
            Vertex* vertices = calloc(vertexCount, sizeof(Vertex));
            
            for (uint32_t v = 0; v < vertexCount; ++v) {
                cgltf_accessor_read_float(posAccessor, v, &vertices[v].position.v[0], 3);
                if (normAccessor) {
                    cgltf_accessor_read_float(normAccessor, v, &vertices[v].normal.v[0], 3);
                } else {
                    vertices[v].normal.v[0] = 0.0f;
                    vertices[v].normal.v[1] = 1.0f;
                    vertices[v].normal.v[2] = 0.0f;
                }
                if (texAccessor) {
                    cgltf_accessor_read_float(texAccessor, v, &vertices[v].texCoord.v[0], 2);
                }
            }
            
            uint32_t indexCount = prim->indices->count;
            uint32_t* indices = calloc(indexCount, sizeof(uint32_t));
            for (uint32_t i = 0; i < indexCount; ++i) {
                indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
            }
            
            // Upload as an LOD chain (review 4.9 step 2). geometryPoolIndex is the chain BASE; the
            // chain length lives in the base mesh's metadata (cull reads it). ANO_DEFAULT_LOD_COUNT
            // is 4, so LOD chains are on engine-wide (set it to 1 for a single full-detail level).
            AnoLodConfig lodCfg = ano_lod_config_default(ANO_DEFAULT_LOD_COUNT);
            uint32_t lodBase = 0u, lodProduced = 0u;
            geometry_pool_upload_chain(
                &rendererState.globalGeometryPool,
                &stagingAllocator,
                ctx->device,
                ctx->queueFamilyIndices.transferFamily,
                ctx->transferQueue,
                vertices, vertexCount,
                indices, indexCount,
                &lodCfg, &lodBase, &lodProduced
            );
            outMesh->primitives[p].geometryPoolIndex = lodBase;
            
            free(vertices);
            free(indices);
        }
    }

    // Identify PBR features globally supported by the active pipelines
    PbrFeatureFlags activeFeatures = ano_vk_get_active_pipelines_supported_features(&rendererState);
    ano_debug_log(ANO_INFO, "[GLTF DEBUG] Active pipeline PBR features supported: 0x%08X", activeFeatures);

    // Identify which textures are actually needed based on supported features, and their color
    // space by USAGE: color slots (baseColor, emissive, *Color) decode sRGB; every other slot is
    // linear data (normal, metallicRoughness, occlusion, ...) that an sRGB decode would warp.
    // A texture referenced by both kinds keeps sRGB (set-once semantics).
    bool* textureNeeded = NULL;
    bool* textureSrgb = NULL;
    if (data->textures_count > 0) {
        textureNeeded = calloc(data->textures_count, sizeof(bool));
        textureSrgb = calloc(data->textures_count, sizeof(bool));
        for (size_t m = 0; m < data->materials_count; ++m) {
            cgltf_material* mat = &data->materials[m];
            PbrFeatureFlags matFeatures = ano_gltf_identify_material_features(mat);
            PbrFeatureFlags supportedFeatures = matFeatures & activeFeatures;
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Material %zu (%s): required features = 0x%08X, supported = 0x%08X",
                   m, mat->name ? mat->name : "unnamed", matFeatures, supportedFeatures);

            if (mat->has_pbr_metallic_roughness) {
                if ((supportedFeatures & PBR_FEATURE_BASE_COLOR_TEXTURE) && mat->pbr_metallic_roughness.base_color_texture.texture) {
                    size_t texIdx = mat->pbr_metallic_roughness.base_color_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                    textureSrgb[texIdx] = true;
                }
                if ((supportedFeatures & PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE) && mat->pbr_metallic_roughness.metallic_roughness_texture.texture) {
                    size_t texIdx = mat->pbr_metallic_roughness.metallic_roughness_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if ((supportedFeatures & PBR_FEATURE_NORMAL_TEXTURE) && mat->normal_texture.texture) {
                size_t texIdx = mat->normal_texture.texture - data->textures;
                textureNeeded[texIdx] = true;
            }
            if ((supportedFeatures & PBR_FEATURE_OCCLUSION_TEXTURE) && mat->occlusion_texture.texture) {
                size_t texIdx = mat->occlusion_texture.texture - data->textures;
                textureNeeded[texIdx] = true;
            }
            if ((supportedFeatures & PBR_FEATURE_EMISSIVE_TEXTURE) && mat->emissive_texture.texture) {
                size_t texIdx = mat->emissive_texture.texture - data->textures;
                textureNeeded[texIdx] = true;
                textureSrgb[texIdx] = true;
            }
            if (supportedFeatures & PBR_FEATURE_CLEARCOAT) {
                if (mat->clearcoat.clearcoat_texture.texture) {
                    size_t texIdx = mat->clearcoat.clearcoat_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
                if (mat->clearcoat.clearcoat_roughness_texture.texture) {
                    size_t texIdx = mat->clearcoat.clearcoat_roughness_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
                if (mat->clearcoat.clearcoat_normal_texture.texture) {
                    size_t texIdx = mat->clearcoat.clearcoat_normal_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_TRANSMISSION) {
                if (mat->transmission.transmission_texture.texture) {
                    size_t texIdx = mat->transmission.transmission_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_VOLUME) {
                if (mat->volume.thickness_texture.texture) {
                    size_t texIdx = mat->volume.thickness_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_SPECULAR) {
                if (mat->specular.specular_texture.texture) {
                    size_t texIdx = mat->specular.specular_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
                if (mat->specular.specular_color_texture.texture) {
                    size_t texIdx = mat->specular.specular_color_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                    textureSrgb[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_SHEEN) {
                if (mat->sheen.sheen_color_texture.texture) {
                    size_t texIdx = mat->sheen.sheen_color_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                    textureSrgb[texIdx] = true;
                }
                if (mat->sheen.sheen_roughness_texture.texture) {
                    size_t texIdx = mat->sheen.sheen_roughness_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_IRIDESCENCE) {
                if (mat->iridescence.iridescence_texture.texture) {
                    size_t texIdx = mat->iridescence.iridescence_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
                if (mat->iridescence.iridescence_thickness_texture.texture) {
                    size_t texIdx = mat->iridescence.iridescence_thickness_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_ANISOTROPY) {
                if (mat->anisotropy.anisotropy_texture.texture) {
                    size_t texIdx = mat->anisotropy.anisotropy_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
            }
            if (supportedFeatures & PBR_FEATURE_DIFFUSE_TRANSMISSION) {
                if (mat->diffuse_transmission.diffuse_transmission_texture.texture) {
                    size_t texIdx = mat->diffuse_transmission.diffuse_transmission_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                }
                if (mat->diffuse_transmission.diffuse_transmission_color_texture.texture) {
                    size_t texIdx = mat->diffuse_transmission.diffuse_transmission_color_texture.texture - data->textures;
                    textureNeeded[texIdx] = true;
                    textureSrgb[texIdx] = true;
                }
            }
        }
    }

    // Count staging buffers needed for textures
    uint32_t maxStaging = 10;
    for (size_t t = 0; t < data->textures_count; ++t) {
        if (data->textures[t].image && data->textures[t].image->uri && textureNeeded && textureNeeded[t]) {
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
    uint32_t* bindlessIndices = calloc(data->textures_count, sizeof(uint32_t));

    for (size_t t = 0; t < data->textures_count; ++t) {
        cgltf_texture* tex = &data->textures[t];
        if (tex->image && tex->image->uri && textureNeeded && textureNeeded[t]) {
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Loading texture %zu: %s", t, tex->image->uri);
            // Resolve the image URI against the glTF file's own directory, exactly as
            // cgltf_load_buffers resolves .bin URIs (combine, then percent-decode the
            // uri tail) -- both halves of a model share one base directory instead of
            // textures silently depending on the process CWD.
            char texPath[1024];
            if (strlen(fileName) + strlen(tex->image->uri) + 1 >= sizeof texPath) {
                ano_log(ANO_WARN, "Texture URI too long, skipping: %s", tex->image->uri);
                textureLoaded[t] = false;
                continue;
            }
            cgltf_combine_paths(texPath, fileName, tex->image->uri);
            cgltf_decode_uri(texPath + strlen(texPath) - strlen(tex->image->uri));
            bool success = createTextureImage(
                ctx, textureCmd, &loadedImages[t], &loadedAllocs[t],
                &loadedTextures[t], texPath, false,
                textureSrgb[t], // color slots decode sRGB; data slots (normal/MR/occlusion) stay linear
                &stagingBuffers[stagingCount++]
            );
            textureLoaded[t] = success;
            if (success) {
                TextureData td = {0};
                td.textureImage = loadedImages[t];
                td.textureImageAlloc = loadedAllocs[t];
                td.textureImageView = loadedTextures[t];
                ano_vk_register_texture(&rendererState.primitives, td);
                
                bindlessIndices[t] = bindless_register_texture(
                    ctx, &rendererState.bindlessTextures, 
                    loadedTextures[t], rendererState.textureSampler
                );
            }
        } else if (tex->image && tex->image->uri) {
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Skipping texture %zu: %s (not needed or unsupported by pipeline)", t, tex->image->uri);
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
        ano_log(ANO_WARN, "Warning: Material buffer cannot fit %u new materials (Capacity: %u, Current: %u). Some materials will fall back to index 0.", 
               totalPrimitives, rendererState.materialBuffer.capacity, rendererState.materialBuffer.count);
    }

    // 3. Bake Material SSBO entries per primitive
    for (size_t m = 0; m < data->meshes_count; ++m) {
        cgltf_mesh* cgMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        
        for (size_t p = 0; p < cgMesh->primitives_count; ++p) {
            cgltf_primitive* prim = &cgMesh->primitives[p];
            
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
                MaterialData matData;
                ano_vk_init_default_material_data(&matData);
                
                if (prim->material) {
                    PbrFeatureFlags matFeatures = ano_gltf_identify_material_features(prim->material);
                    PbrFeatureFlags supportedFeatures = matFeatures & activeFeatures;
                    
                    matData.features = supportedFeatures;
                    
                    // 1. pbrMetallicRoughness
                    if (prim->material->has_pbr_metallic_roughness) {
                        if (supportedFeatures & PBR_FEATURE_BASE_COLOR_TEXTURE) {
                            cgltf_texture_view* texView = &prim->material->pbr_metallic_roughness.base_color_texture;
                            if (texView->texture) {
                                size_t texIdx = texView->texture - data->textures;
                                if (textureLoaded[texIdx]) {
                                    matData.baseColorTexture = bindlessIndices[texIdx];
                                }
                            }
                        }
                        
                        for (int i = 0; i < 4; i++) {
                            matData.baseColorFactor[i] = (float)prim->material->pbr_metallic_roughness.base_color_factor[i];
                        }
                        matData.metallicFactor = (float)prim->material->pbr_metallic_roughness.metallic_factor;
                        matData.roughnessFactor = (float)prim->material->pbr_metallic_roughness.roughness_factor;
                        
                        if (supportedFeatures & PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE) {
                            cgltf_texture_view* texView = &prim->material->pbr_metallic_roughness.metallic_roughness_texture;
                            if (texView->texture) {
                                size_t texIdx = texView->texture - data->textures;
                                if (textureLoaded[texIdx]) {
                                    matData.metallicRoughnessTexture = bindlessIndices[texIdx];
                                }
                            }
                        }
                    }
                    
                    // 2. Core properties
                    if (supportedFeatures & PBR_FEATURE_NORMAL_TEXTURE) {
                        if (prim->material->normal_texture.texture) {
                            size_t texIdx = prim->material->normal_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.normalTexture = bindlessIndices[texIdx];
                                matData.normalScale = (float)prim->material->normal_texture.scale;
                            }
                        }
                    }
                    
                    if (supportedFeatures & PBR_FEATURE_OCCLUSION_TEXTURE) {
                        if (prim->material->occlusion_texture.texture) {
                            size_t texIdx = prim->material->occlusion_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.occlusionTexture = bindlessIndices[texIdx];
                                matData.occlusionStrength = (float)prim->material->occlusion_texture.scale;
                            }
                        }
                    }
                    
                    if (supportedFeatures & PBR_FEATURE_EMISSIVE_TEXTURE) {
                        if (prim->material->emissive_texture.texture) {
                            size_t texIdx = prim->material->emissive_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.emissiveTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    for (int i = 0; i < 3; i++) {
                        matData.emissiveFactor[i] = (float)prim->material->emissive_factor[i];
                    }
                    matData.emissiveFactor[3] = 1.0f; // Padding
                    
                    if (prim->material->alpha_mode == cgltf_alpha_mode_opaque) {
                        matData.alphaMode = 0;
                    } else if (prim->material->alpha_mode == cgltf_alpha_mode_mask) {
                        matData.alphaMode = 1;
                        matData.alphaCutoff = (float)prim->material->alpha_cutoff;
                    } else if (prim->material->alpha_mode == cgltf_alpha_mode_blend) {
                        matData.alphaMode = 2;
                    }
                    
                    matData.doubleSided = prim->material->double_sided ? 1 : 0;
                    
                    // 3. Clearcoat
                    if (supportedFeatures & PBR_FEATURE_CLEARCOAT) {
                        matData.clearcoatFactor = (float)prim->material->clearcoat.clearcoat_factor;
                        matData.clearcoatRoughnessFactor = (float)prim->material->clearcoat.clearcoat_roughness_factor;
                        
                        if (prim->material->clearcoat.clearcoat_texture.texture) {
                            size_t texIdx = prim->material->clearcoat.clearcoat_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.clearcoatTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->clearcoat.clearcoat_roughness_texture.texture) {
                            size_t texIdx = prim->material->clearcoat.clearcoat_roughness_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.clearcoatRoughnessTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->clearcoat.clearcoat_normal_texture.texture) {
                            size_t texIdx = prim->material->clearcoat.clearcoat_normal_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.clearcoatNormalTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 4. Transmission
                    if (supportedFeatures & PBR_FEATURE_TRANSMISSION) {
                        matData.transmissionFactor = (float)prim->material->transmission.transmission_factor;
                        if (prim->material->transmission.transmission_texture.texture) {
                            size_t texIdx = prim->material->transmission.transmission_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.transmissionTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 5. Volume
                    if (supportedFeatures & PBR_FEATURE_VOLUME) {
                        matData.thicknessFactor = (float)prim->material->volume.thickness_factor;
                        matData.attenuationDistance = (float)prim->material->volume.attenuation_distance;
                        for (int i = 0; i < 3; i++) {
                            matData.attenuationColor[i] = (float)prim->material->volume.attenuation_color[i];
                        }
                        matData.attenuationColor[3] = 1.0f; // Padding
                        
                        if (prim->material->volume.thickness_texture.texture) {
                            size_t texIdx = prim->material->volume.thickness_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.thicknessTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 6. IOR
                    if (supportedFeatures & PBR_FEATURE_IOR) {
                        matData.ior = (float)prim->material->ior.ior;
                    }
                    
                    // 7. Specular
                    if (supportedFeatures & PBR_FEATURE_SPECULAR) {
                        matData.specularFactor = (float)prim->material->specular.specular_factor;
                        for (int i = 0; i < 3; i++) {
                            matData.specularColorFactor[i] = (float)prim->material->specular.specular_color_factor[i];
                        }
                        matData.specularColorFactor[3] = 1.0f; // Padding
                        
                        if (prim->material->specular.specular_texture.texture) {
                            size_t texIdx = prim->material->specular.specular_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.specularTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->specular.specular_color_texture.texture) {
                            size_t texIdx = prim->material->specular.specular_color_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.specularColorTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 8. Sheen
                    if (supportedFeatures & PBR_FEATURE_SHEEN) {
                        matData.sheenRoughnessFactor = (float)prim->material->sheen.sheen_roughness_factor;
                        for (int i = 0; i < 3; i++) {
                            matData.sheenColorFactor[i] = (float)prim->material->sheen.sheen_color_factor[i];
                        }
                        matData.sheenColorFactor[3] = 1.0f; // Padding
                        
                        if (prim->material->sheen.sheen_color_texture.texture) {
                            size_t texIdx = prim->material->sheen.sheen_color_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.sheenColorTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->sheen.sheen_roughness_texture.texture) {
                            size_t texIdx = prim->material->sheen.sheen_roughness_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.sheenRoughnessTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 9. Iridescence
                    if (supportedFeatures & PBR_FEATURE_IRIDESCENCE) {
                        matData.iridescenceFactor = (float)prim->material->iridescence.iridescence_factor;
                        matData.iridescenceIor = (float)prim->material->iridescence.iridescence_ior;
                        matData.iridescenceThicknessMinimum = (float)prim->material->iridescence.iridescence_thickness_min;
                        matData.iridescenceThicknessMaximum = (float)prim->material->iridescence.iridescence_thickness_max;
                        
                        if (prim->material->iridescence.iridescence_texture.texture) {
                            size_t texIdx = prim->material->iridescence.iridescence_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.iridescenceTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->iridescence.iridescence_thickness_texture.texture) {
                            size_t texIdx = prim->material->iridescence.iridescence_thickness_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.iridescenceThicknessTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 10. Anisotropy
                    if (supportedFeatures & PBR_FEATURE_ANISOTROPY) {
                        matData.anisotropyStrength = (float)prim->material->anisotropy.anisotropy_strength;
                        matData.anisotropyRotation = (float)prim->material->anisotropy.anisotropy_rotation;
                        
                        if (prim->material->anisotropy.anisotropy_texture.texture) {
                            size_t texIdx = prim->material->anisotropy.anisotropy_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.anisotropyTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 11. Dispersion
                    if (supportedFeatures & PBR_FEATURE_DISPERSION) {
                        matData.dispersion = (float)prim->material->dispersion.dispersion;
                    }
                    
                    // 12. Diffuse Transmission
                    if (supportedFeatures & PBR_FEATURE_DIFFUSE_TRANSMISSION) {
                        matData.diffuseTransmissionFactor = (float)prim->material->diffuse_transmission.diffuse_transmission_factor;
                        for (int i = 0; i < 3; i++) {
                            matData.diffuseTransmissionColorFactor[i] = (float)prim->material->diffuse_transmission.diffuse_transmission_color_factor[i];
                        }
                        matData.diffuseTransmissionColorFactor[3] = 1.0f; // Padding
                        
                        if (prim->material->diffuse_transmission.diffuse_transmission_texture.texture) {
                            size_t texIdx = prim->material->diffuse_transmission.diffuse_transmission_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.diffuseTransmissionTexture = bindlessIndices[texIdx];
                            }
                        }
                        if (prim->material->diffuse_transmission.diffuse_transmission_color_texture.texture) {
                            size_t texIdx = prim->material->diffuse_transmission.diffuse_transmission_color_texture.texture - data->textures;
                            if (textureLoaded[texIdx]) {
                                matData.diffuseTransmissionColorTexture = bindlessIndices[texIdx];
                            }
                        }
                    }
                    
                    // 13. Emissive Strength
                    if (supportedFeatures & PBR_FEATURE_EMISSIVE_STRENGTH) {
                        matData.emissiveStrength = (float)prim->material->emissive_strength.emissive_strength;
                    }
                    
                    // Pipeline routing (audit 4.7 transparency lanes; review finding 7 sidedness):
                    //   transmission/volume         -> PIPELINE_TRANSMISSION (depth-sorted "over" lane)
                    //   emissiveStrength>1 OR BLEND  -> PIPELINE_ADDITIVE (order-independent ONE/ONE)
                    //   alphaMode MASK               -> PIPELINE_FLAT_MASKED (alpha-tested cutout, cullMode NONE)
                    //   opaque + doubleSided         -> PIPELINE_FLAT_TWOSIDED (cullMode NONE)
                    //   otherwise                    -> PIPELINE_FLAT (opaque, backface-culled)
                    // alphaMode 1 == MASK, 2 == BLEND (set above). The additive branch is exclusive of
                    // transmission; MASK wins over doubleSided (the masked lane is already uncull(ed)).
                    uint32_t selectedPipeline = PIPELINE_FLAT;
                    if (supportedFeatures & (PBR_FEATURE_TRANSMISSION | PBR_FEATURE_VOLUME)) {
                        selectedPipeline = PIPELINE_TRANSMISSION;
                    } else if (matData.emissiveStrength > 1.0f || matData.alphaMode == 2u) {
                        selectedPipeline = PIPELINE_ADDITIVE;
                    } else if (matData.alphaMode == 1u) {
                        selectedPipeline = PIPELINE_FLAT_MASKED;
                    } else if (matData.doubleSided) {
                        selectedPipeline = PIPELINE_FLAT_TWOSIDED;
                    }
                    matData.pipelineType = selectedPipeline;
                }
                
                for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
                    rendererState.materialBuffer.mapped[frame][matIdx] = matData;
                }
            }
        }
    }

    if (textureNeeded) {
        free(textureNeeded);
    }
    if (textureSrgb) {
        free(textureSrgb);
    }
    free(loadedTextures);
    free(loadedImages);
    free(loadedAllocs);
    free(textureLoaded);
    free(bindlessIndices);

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
    ano_debug_log(ANO_INFO, "Successfully extracted ModelAsset: %s", fileName);
    return asset;
}

// Walks the node subtree, appending one descriptor per mesh primitive. *idx counts ALL primitives
// (so the caller learns the true total); a descriptor is written only while *idx < cap.
static void flatten_node(const ModelAsset* asset, uint32_t nodeIndex, const mat4 parentTransform,
                         AnoRenderableDesc* out, uint32_t cap, uint32_t* idx) {
    const ModelNode* node = &asset->nodes[nodeIndex];

    mat4 worldTransform;
    multiplyMat4(worldTransform, parentTransform, node->localTransform);

    if (node->meshIndex >= 0) {
        const ModelMesh* mesh = &asset->meshes[node->meshIndex];
        for (uint32_t p = 0; p < mesh->primitiveCount; p++) {
            if (out && *idx < cap) {
                out[*idx].mesh_index     = mesh->primitives[p].geometryPoolIndex;
                out[*idx].material_index = mesh->primitives[p].materialIndex;
                float* d = (float*)&out[*idx].transform;
                float* s = (float*)&worldTransform;
                for (int i = 0; i < 16; i++) d[i] = s[i];
            }
            (*idx)++;
        }
    }

    for (uint32_t c = 0; c < node->childCount; c++)
        flatten_node(asset, node->childIndices[c], worldTransform, out, cap, idx);
}

uint32_t model_flatten(const ModelAsset* asset, const mat4 rootTransform, AnoRenderableDesc* out, uint32_t cap) {
    if (!asset) return 0u;
    uint32_t idx = 0u;
    for (uint32_t r = 0; r < asset->rootNodeCount; r++)
        flatten_node(asset, asset->rootNodes[r], rootTransform, out, cap, &idx);
    return idx;
}

PbrFeatureFlags ano_gltf_identify_material_features(const cgltf_material* material) {
    if (!material) {
        return PBR_FEATURE_NONE;
    }
    
    PbrFeatureFlags features = PBR_FEATURE_NONE;
    
    // 1. pbrMetallicRoughness
    if (material->has_pbr_metallic_roughness) {
        features |= PBR_FEATURE_BASE_COLOR_FACTOR;
        features |= PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR;
        
        if (material->pbr_metallic_roughness.base_color_texture.texture) {
            features |= PBR_FEATURE_BASE_COLOR_TEXTURE;
        }
        if (material->pbr_metallic_roughness.metallic_roughness_texture.texture) {
            features |= PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE;
        }
    }
    
    // 2. Core properties
    if (material->normal_texture.texture) {
        features |= PBR_FEATURE_NORMAL_TEXTURE;
    }
    if (material->occlusion_texture.texture) {
        features |= PBR_FEATURE_OCCLUSION_TEXTURE;
    }
    if (material->emissive_texture.texture) {
        features |= PBR_FEATURE_EMISSIVE_TEXTURE;
    }
    if (material->emissive_factor[0] > 0.0f || material->emissive_factor[1] > 0.0f || material->emissive_factor[2] > 0.0f) {
        features |= PBR_FEATURE_EMISSIVE_FACTOR;
    }
    
    // Alpha modes
    if (material->alpha_mode == cgltf_alpha_mode_opaque) {
        features |= PBR_FEATURE_ALPHA_MODE_OPAQUE;
    } else if (material->alpha_mode == cgltf_alpha_mode_mask) {
        features |= PBR_FEATURE_ALPHA_MODE_MASK;
    } else if (material->alpha_mode == cgltf_alpha_mode_blend) {
        features |= PBR_FEATURE_ALPHA_MODE_BLEND;
    }
    
    if (material->double_sided) {
        features |= PBR_FEATURE_DOUBLE_SIDED;
    }
    
    // Extensions
    if (material->has_clearcoat) {
        features |= PBR_FEATURE_CLEARCOAT;
    }
    if (material->has_transmission) {
        features |= PBR_FEATURE_TRANSMISSION;
    }
    if (material->has_volume) {
        features |= PBR_FEATURE_VOLUME;
    }
    if (material->has_ior) {
        features |= PBR_FEATURE_IOR;
    }
    if (material->has_specular) {
        features |= PBR_FEATURE_SPECULAR;
    }
    if (material->has_sheen) {
        features |= PBR_FEATURE_SHEEN;
    }
    if (material->has_iridescence) {
        features |= PBR_FEATURE_IRIDESCENCE;
    }
    if (material->has_anisotropy) {
        features |= PBR_FEATURE_ANISOTROPY;
    }
    if (material->has_dispersion) {
        features |= PBR_FEATURE_DISPERSION;
    }
    if (material->has_diffuse_transmission) {
        features |= PBR_FEATURE_DIFFUSE_TRANSMISSION;
    }
    if (material->has_emissive_strength) {
        features |= PBR_FEATURE_EMISSIVE_STRENGTH;
    }
    if (material->has_pbr_specular_glossiness) {
        features |= PBR_FEATURE_SPECULAR_GLOSSINESS;
    }
    
    return features;
}
