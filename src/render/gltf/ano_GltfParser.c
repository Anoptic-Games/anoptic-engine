/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

// GPU-side glTF ingest from anoresgfx conditioned scenes: LOD geometry, bindless textures, material SSBO, ModelAsset node graph. No file opens — cgltf/stb live in res_graphics.

#include "ano_GltfParser.h"
#include <string.h>
#include <anoptic_log.h>
#include <anoptic_memory.h>
#include <anoptic_res_graphics.h>
#include <anoptic_resources.h>

extern GpuAllocator stagingAllocator;
extern RendererState rendererState;

// Scene vertex is wide (tangent/color/uv1/joints/weights); GPU Vertex is pos+normal+uv — narrow field-by-field. Shared fields must match width.
// TODO(W7, M13): Vertex becomes anoresgfx_vertex; narrowing copy dies.
static_assert(sizeof(((Vertex *)0)->position) == sizeof(((anoresgfx_vertex *)0)->position)
              && sizeof(((Vertex *)0)->normal) == sizeof(((anoresgfx_vertex *)0)->normal)
              && sizeof(((Vertex *)0)->texCoord) == sizeof(((anoresgfx_vertex *)0)->texcoord),
              "the narrowing copy moves whole fields: their widths must match");
static_assert(sizeof(Vertex) < sizeof(anoresgfx_vertex),
              "the GPU vertex has not caught up to the conditioned one yet (M13)");

// File-truth feature bits must equal PbrFeatureFlags.
static_assert(PBR_FEATURE_BASE_COLOR_FACTOR == ANORESGFX_PBR_BASE_COLOR_FACTOR
              && PBR_FEATURE_NORMAL_TEXTURE == ANORESGFX_PBR_NORMAL_TEXTURE
              && PBR_FEATURE_ALPHA_MODE_BLEND == ANORESGFX_PBR_ALPHA_MODE_BLEND
              && PBR_FEATURE_CLEARCOAT == ANORESGFX_PBR_CLEARCOAT
              && PBR_FEATURE_EMISSIVE_STRENGTH == ANORESGFX_PBR_EMISSIVE_STRENGTH
              && PBR_FEATURE_SPECULAR_GLOSSINESS == ANORESGFX_PBR_SPECULAR_GLOSSINESS,
              "PbrFeatureFlags and ANORESGFX_PBR_* must agree bit for bit");

// Recursive flatten walk.
static void flatten_node(const ModelAsset* asset, uint32_t nodeIndex, const mat4 parentTransform,
                         AnoRenderableDesc* out, uint32_t cap, uint32_t* idx);

// One gated texture slot: which feature admits it, whether it samples as color.
typedef struct SlotRule {
    uint32_t feature;
    bool     srgb;
    const anoresgfx_texref* (*ref)(const anoresgfx_material*);
} SlotRule;

#define SLOT(field) \
    static const anoresgfx_texref* slot_##field(const anoresgfx_material* m) { return &m->field; }
SLOT(base_color) SLOT(metallic_roughness) SLOT(normal) SLOT(occlusion) SLOT(emissive)
SLOT(clearcoat) SLOT(clearcoat_roughness) SLOT(clearcoat_normal)
SLOT(transmission) SLOT(thickness) SLOT(specular) SLOT(specular_color)
SLOT(sheen_color) SLOT(sheen_roughness) SLOT(iridescence) SLOT(iridescence_thickness)
SLOT(anisotropy) SLOT(diffuse_transmission) SLOT(diffuse_transmission_color)
#undef SLOT

static const SlotRule SLOT_RULES[] = {
    { PBR_FEATURE_BASE_COLOR_TEXTURE,         true,  slot_base_color },
    { PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE, false, slot_metallic_roughness },
    { PBR_FEATURE_NORMAL_TEXTURE,             false, slot_normal },
    { PBR_FEATURE_OCCLUSION_TEXTURE,          false, slot_occlusion },
    { PBR_FEATURE_EMISSIVE_TEXTURE,           true,  slot_emissive },
    { PBR_FEATURE_CLEARCOAT,                  false, slot_clearcoat },
    { PBR_FEATURE_CLEARCOAT,                  false, slot_clearcoat_roughness },
    { PBR_FEATURE_CLEARCOAT,                  false, slot_clearcoat_normal },
    { PBR_FEATURE_TRANSMISSION,               false, slot_transmission },
    { PBR_FEATURE_VOLUME,                     false, slot_thickness },
    { PBR_FEATURE_SPECULAR,                   false, slot_specular },
    { PBR_FEATURE_SPECULAR,                   true,  slot_specular_color },
    { PBR_FEATURE_SHEEN,                      true,  slot_sheen_color },
    { PBR_FEATURE_SHEEN,                      false, slot_sheen_roughness },
    { PBR_FEATURE_IRIDESCENCE,                false, slot_iridescence },
    { PBR_FEATURE_IRIDESCENCE,                false, slot_iridescence_thickness },
    { PBR_FEATURE_ANISOTROPY,                 false, slot_anisotropy },
    { PBR_FEATURE_DIFFUSE_TRANSMISSION,       false, slot_diffuse_transmission },
    { PBR_FEATURE_DIFFUSE_TRANSMISSION,       true,  slot_diffuse_transmission_color },
};

// Bindless index for a slot's image if it made it to the GPU, else leave the default.
static void bind_slot(const anoresgfx_texref* ref, const bool* loaded,
                      const uint32_t* bindless, uint32_t* out)
{
    if (ref->image >= 0 && loaded[ref->image])
        *out = bindless[ref->image];
}

ModelAsset* parseGltf(VulkanContext* ctx, const char* logical)
{
    // 1. Parse + condition via resource manager.
    ano_res_lifetime lifetime = ano_res_lifetime_engine();
    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    if (ano_res_reader_register(&reader) != 0 || ano_res_read_begin(&reader, &read) != 0)
        return NULL;
    anores_t src = ano_res_get(lifetime, logical);
    if (src.gen == 0) {
        ano_res_read_end(&read);
        ano_res_reader_unregister(&reader);
        ano_log(ANO_ERROR, "Failed to load glTF resource: %s", logical);
        return NULL;
    }
    anores_t sceneH = ano_resgfx_model(lifetime, &read, src);
    ano_res_read_end(&read);
    ano_res_unload(lifetime, src);
    if (ano_res_read_begin(&reader, &read) != 0) {
        ano_res_reader_unregister(&reader);
        return NULL;
    }
    anoresgfx_scene s = ano_resgfx_scene(&read, sceneH);
    if (sceneH.gen == 0 || s.node_count == 0) {
        if (sceneH.gen != 0)
            ano_res_unload(lifetime, sceneH);
        ano_res_read_end(&read);
        ano_res_reader_unregister(&reader);
        ano_res_collect();
        ano_log(ANO_ERROR, "Failed to condition glTF scene: %s", logical);
        return NULL;
    }

    ModelAsset* asset = calloc(1, sizeof(ModelAsset));
    strncpy(asset->name, logical, 63);

    // 2. Geometry: upload each prim as LOD chain (narrow wide scene verts into scratch).
    //    TODO(W7, M13): widened Vertex deletes this copy.
    asset->meshCount = s.mesh_count;
    asset->meshes = calloc(asset->meshCount, sizeof(ModelMesh));
    for (uint32_t m = 0; m < s.mesh_count; ++m) {
        const anoresgfx_mesh* inMesh = &s.meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        outMesh->primitiveCount = inMesh->prim_count;
        outMesh->primitives = calloc(inMesh->prim_count, sizeof(ModelPrimitive));
        for (uint32_t p = 0; p < inMesh->prim_count; ++p) {
            const anoresgfx_prim* prim = &s.prims[inMesh->prim_first + p];
            Vertex* verts = calloc(prim->vertex_count ? prim->vertex_count : 1, sizeof(Vertex));
            if (verts == NULL)
                continue;
            for (uint32_t v = 0; v < prim->vertex_count; ++v) {
                const anoresgfx_vertex* sv = &s.vertices[prim->vertex_first + v];
                memcpy(&verts[v].position, sv->position, sizeof sv->position);
                memcpy(&verts[v].normal,   sv->normal,   sizeof sv->normal);
                memcpy(&verts[v].texCoord, sv->texcoord, sizeof sv->texcoord);
            }
            AnoLodConfig lodCfg = ano_lod_config_default(ANO_DEFAULT_LOD_COUNT);
            uint32_t lodBase = 0u, lodProduced = 0u;
            geometry_pool_upload_chain(
                &rendererState.globalGeometryPool,
                &stagingAllocator,
                ctx->device,
                ctx->queueFamilyIndices.transferFamily,
                ctx->transferQueue,
                verts, prim->vertex_count,
                s.indices + prim->index_first, prim->index_count,
                &lodCfg, &lodBase, &lodProduced
            );
            free(verts);
            outMesh->primitives[p].geometryPoolIndex = lodBase;
        }
    }

    // 3. Textures: gate by file features ∩ active pipelines; decode, upload, bindless.
    PbrFeatureFlags activeFeatures = ano_vk_get_active_pipelines_supported_features(&rendererState);

    bool*     needed   = calloc(s.image_count ? s.image_count : 1, sizeof(bool));
    bool*     srgb     = calloc(s.image_count ? s.image_count : 1, sizeof(bool));
    bool*     loaded   = calloc(s.image_count ? s.image_count : 1, sizeof(bool));
    uint32_t* bindless = calloc(s.image_count ? s.image_count : 1, sizeof(uint32_t));

    for (uint32_t m = 0; m < s.material_count; ++m) {
        const anoresgfx_material* mat = &s.materials[m];
        PbrFeatureFlags supported = mat->features & activeFeatures;
        for (size_t r = 0; r < sizeof SLOT_RULES / sizeof SLOT_RULES[0]; ++r) {
            if (!(supported & SLOT_RULES[r].feature))
                continue;
            const anoresgfx_texref* ref = SLOT_RULES[r].ref(mat);
            if (ref->image < 0 || (uint32_t)ref->image >= s.image_count)
                continue;
            needed[ref->image] = true;
            if (SLOT_RULES[r].srgb)
                srgb[ref->image] = true;
        }
    }

    uint32_t maxStaging = 10;
    for (uint32_t t = 0; t < s.image_count; ++t)
        if (needed[t] && s.images[t].path[0] != '\0')
            maxStaging++;

    VkCommandBuffer textureCmd = beginSingleTimeCommands(ctx);
    VkBuffer* stagingBuffers = calloc(maxStaging, sizeof(VkBuffer));
    uint32_t stagingCount = 0;

    for (uint32_t t = 0; t < s.image_count; ++t) {
        if (!needed[t] || s.images[t].path[0] == '\0')
            continue;
        anores_t img = ano_res_get(lifetime, s.images[t].path);
        if (img.gen == 0)
            continue;                           // get already logged the miss
        anoresgfx_pixels px = ano_resgfx_image(lifetime, &read, img);
        if (px.rgba == NULL) {
            ano_log(ANO_WARN, "Texture decode failed, skipping: %s", s.images[t].path);
            ano_res_unload(lifetime, img);
            continue;
        }
        VkImage image; GpuAllocation alloc; VkImageView view;
        bool ok = createTextureImageFromPixels(ctx, textureCmd, &image, &alloc, &view,
                                               px.rgba, px.width, px.height,
                                               srgb[t], true,
                                               &stagingBuffers[stagingCount]);
        ano_aligned_free(px.rgba);
        ano_res_unload(lifetime, img);                    // encoded bytes are done too
        if (!ok)
            continue;
        stagingCount++;
        TextureData td = {0};
        td.textureImage = image;
        td.textureImageAlloc = alloc;
        td.textureImageView = view;
        ano_vk_register_texture(&rendererState.primitives, td);
        bindless[t] = bindless_register_texture(ctx, &rendererState.bindlessTextures,
                                                view, rendererState.textureSampler);
        loaded[t] = true;
    }

    endSingleTimeCommands(ctx, textureCmd);
    for (uint32_t i = 0; i < stagingCount; ++i)
        vkDestroyBuffer(ctx->device, stagingBuffers[i], NULL);
    free(stagingBuffers);
    gpu_alloc_reset(&stagingAllocator);

    // 4. Bake material SSBO entries per primitive.
    uint32_t totalPrimitives = 0;
    for (uint32_t m = 0; m < s.mesh_count; ++m)
        totalPrimitives += s.meshes[m].prim_count;
    if (rendererState.materialBuffer.count + totalPrimitives > rendererState.materialBuffer.capacity)
        ano_log(ANO_WARN, "Warning: Material buffer cannot fit %u new materials (Capacity: %u, Current: %u). Some materials will fall back to index 0.",
                totalPrimitives, rendererState.materialBuffer.capacity, rendererState.materialBuffer.count);

    for (uint32_t m = 0; m < s.mesh_count; ++m) {
        const anoresgfx_mesh* inMesh = &s.meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        for (uint32_t p = 0; p < inMesh->prim_count; ++p) {
            const anoresgfx_prim* prim = &s.prims[inMesh->prim_first + p];

            uint32_t matIdx = 0;
            bool writeMaterial = false;
            if (rendererState.materialBuffer.count < rendererState.materialBuffer.capacity) {
                matIdx = rendererState.materialBuffer.count++;
                writeMaterial = true;
            }
            outMesh->primitives[p].materialIndex = matIdx;
            if (!writeMaterial)
                continue;

            MaterialData matData;
            ano_vk_init_default_material_data(&matData);

            if (prim->material >= 0 && (uint32_t)prim->material < s.material_count) {
                const anoresgfx_material* mat = &s.materials[prim->material];
                PbrFeatureFlags supported = mat->features & activeFeatures;
                matData.features = supported;

                // 1. pbrMetallicRoughness
                if (supported & PBR_FEATURE_BASE_COLOR_TEXTURE)
                    bind_slot(&mat->base_color, loaded, bindless, &matData.baseColorTexture);
                for (int i = 0; i < 4; i++)
                    matData.baseColorFactor[i] = mat->base_color_factor[i];
                matData.metallicFactor  = mat->metallic_factor;
                matData.roughnessFactor = mat->roughness_factor;
                if (supported & PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE)
                    bind_slot(&mat->metallic_roughness, loaded, bindless,
                              &matData.metallicRoughnessTexture);

                // 2. Core properties
                if (supported & PBR_FEATURE_NORMAL_TEXTURE) {
                    if (mat->normal.image >= 0 && loaded[mat->normal.image]) {
                        matData.normalTexture = bindless[mat->normal.image];
                        matData.normalScale   = mat->normal.scale;
                    }
                }
                if (supported & PBR_FEATURE_OCCLUSION_TEXTURE) {
                    if (mat->occlusion.image >= 0 && loaded[mat->occlusion.image]) {
                        matData.occlusionTexture  = bindless[mat->occlusion.image];
                        matData.occlusionStrength = mat->occlusion.scale;
                    }
                }
                if (supported & PBR_FEATURE_EMISSIVE_TEXTURE)
                    bind_slot(&mat->emissive, loaded, bindless, &matData.emissiveTexture);
                for (int i = 0; i < 3; i++)
                    matData.emissiveFactor[i] = mat->emissive_factor[i];
                matData.emissiveFactor[3] = 1.0f;

                matData.alphaMode = mat->alpha_mode;
                if (mat->alpha_mode == 1u)
                    matData.alphaCutoff = mat->alpha_cutoff;
                matData.doubleSided = mat->double_sided ? 1 : 0;

                // 3. Clearcoat
                if (supported & PBR_FEATURE_CLEARCOAT) {
                    matData.clearcoatFactor          = mat->clearcoat_factor;
                    matData.clearcoatRoughnessFactor = mat->clearcoat_roughness_factor;
                    bind_slot(&mat->clearcoat, loaded, bindless, &matData.clearcoatTexture);
                    bind_slot(&mat->clearcoat_roughness, loaded, bindless,
                              &matData.clearcoatRoughnessTexture);
                    bind_slot(&mat->clearcoat_normal, loaded, bindless,
                              &matData.clearcoatNormalTexture);
                }
                // 4. Transmission
                if (supported & PBR_FEATURE_TRANSMISSION) {
                    matData.transmissionFactor = mat->transmission_factor;
                    bind_slot(&mat->transmission, loaded, bindless, &matData.transmissionTexture);
                }
                // 5. Volume
                if (supported & PBR_FEATURE_VOLUME) {
                    matData.thicknessFactor     = mat->thickness_factor;
                    matData.attenuationDistance = mat->attenuation_distance;
                    for (int i = 0; i < 3; i++)
                        matData.attenuationColor[i] = mat->attenuation_color[i];
                    matData.attenuationColor[3] = 1.0f;
                    bind_slot(&mat->thickness, loaded, bindless, &matData.thicknessTexture);
                }
                // 6. IOR
                if (supported & PBR_FEATURE_IOR)
                    matData.ior = mat->ior;
                // 7. Specular
                if (supported & PBR_FEATURE_SPECULAR) {
                    matData.specularFactor = mat->specular_factor;
                    for (int i = 0; i < 3; i++)
                        matData.specularColorFactor[i] = mat->specular_color_factor[i];
                    matData.specularColorFactor[3] = 1.0f;
                    bind_slot(&mat->specular, loaded, bindless, &matData.specularTexture);
                    bind_slot(&mat->specular_color, loaded, bindless,
                              &matData.specularColorTexture);
                }
                // 8. Sheen
                if (supported & PBR_FEATURE_SHEEN) {
                    matData.sheenRoughnessFactor = mat->sheen_roughness_factor;
                    for (int i = 0; i < 3; i++)
                        matData.sheenColorFactor[i] = mat->sheen_color_factor[i];
                    matData.sheenColorFactor[3] = 1.0f;
                    bind_slot(&mat->sheen_color, loaded, bindless, &matData.sheenColorTexture);
                    bind_slot(&mat->sheen_roughness, loaded, bindless,
                              &matData.sheenRoughnessTexture);
                }
                // 9. Iridescence
                if (supported & PBR_FEATURE_IRIDESCENCE) {
                    matData.iridescenceFactor            = mat->iridescence_factor;
                    matData.iridescenceIor               = mat->iridescence_ior;
                    matData.iridescenceThicknessMinimum  = mat->iridescence_thickness_min;
                    matData.iridescenceThicknessMaximum  = mat->iridescence_thickness_max;
                    bind_slot(&mat->iridescence, loaded, bindless, &matData.iridescenceTexture);
                    bind_slot(&mat->iridescence_thickness, loaded, bindless,
                              &matData.iridescenceThicknessTexture);
                }
                // 10. Anisotropy
                if (supported & PBR_FEATURE_ANISOTROPY) {
                    matData.anisotropyStrength = mat->anisotropy_strength;
                    matData.anisotropyRotation = mat->anisotropy_rotation;
                    bind_slot(&mat->anisotropy, loaded, bindless, &matData.anisotropyTexture);
                }
                // 11. Dispersion
                if (supported & PBR_FEATURE_DISPERSION)
                    matData.dispersion = mat->dispersion;
                // 12. Diffuse Transmission
                if (supported & PBR_FEATURE_DIFFUSE_TRANSMISSION) {
                    matData.diffuseTransmissionFactor = mat->diffuse_transmission_factor;
                    for (int i = 0; i < 3; i++)
                        matData.diffuseTransmissionColorFactor[i] =
                            mat->diffuse_transmission_color_factor[i];
                    matData.diffuseTransmissionColorFactor[3] = 1.0f;
                    bind_slot(&mat->diffuse_transmission, loaded, bindless,
                              &matData.diffuseTransmissionTexture);
                    bind_slot(&mat->diffuse_transmission_color, loaded, bindless,
                              &matData.diffuseTransmissionColorTexture);
                }
                // 13. Emissive Strength
                if (supported & PBR_FEATURE_EMISSIVE_STRENGTH)
                    matData.emissiveStrength = mat->emissive_strength;

                // Pipeline: transmission/volume -> TRANSMISSION; emissive>1|BLEND -> ADDITIVE; MASK -> FLAT_MASKED; doubleSided -> FLAT_TWOSIDED; else FLAT.
                uint32_t selectedPipeline = PIPELINE_FLAT;
                if (supported & (PBR_FEATURE_TRANSMISSION | PBR_FEATURE_VOLUME)) {
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

            for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                rendererState.materialBuffer.mapped[frame][matIdx] = matData;
        }
    }

    free(needed);
    free(srgb);
    free(loaded);
    free(bindless);

    // 5. Node hierarchy into the blueprint.
    asset->nodeCount = s.node_count;
    asset->nodes = calloc(asset->nodeCount, sizeof(ModelNode));
    for (uint32_t n = 0; n < s.node_count; ++n) {
        const anoresgfx_node* in = &s.nodes[n];
        ModelNode* outNode = &asset->nodes[n];
        memcpy(outNode->name, in->name, sizeof outNode->name);
        memcpy(&outNode->localTransform, in->local, sizeof(mat4));
        outNode->meshIndex   = in->mesh;
        outNode->parentIndex = in->parent;
        outNode->childCount  = in->child_count;
        if (in->child_count > 0) {
            outNode->childIndices = calloc(in->child_count, sizeof(uint32_t));
            memcpy(outNode->childIndices, s.children + in->child_first,
                   in->child_count * sizeof(uint32_t));
        }
    }
    asset->rootNodeCount = s.root_count;
    if (s.root_count > 0) {
        asset->rootNodes = calloc(s.root_count, sizeof(uint32_t));
        memcpy(asset->rootNodes, s.roots, s.root_count * sizeof(uint32_t));
    }

    // CPU scene done; GPU owns the data.
    ano_res_unload(lifetime, sceneH);
    ano_res_read_end(&read);
    ano_res_reader_unregister(&reader);
    ano_res_collect();
    ano_debug_log(ANO_INFO, "Successfully ingested ModelAsset: %s", logical);
    return asset;
}

// Walk node subtree, appending one descriptor per mesh primitive.
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
