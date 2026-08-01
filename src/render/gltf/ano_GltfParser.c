/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include "ano_GltfParser.h"
#include "cpp/ano_alloc.h"
#include <string.h>
#include <assert.h>
#include <meta>
#include <type_traits>
#include <anoptic_log.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern GpuAllocator stagingAllocator;
extern RendererState rendererState;

// Recursive flatten walk.
static void flatten_node(const ModelAsset* asset, uint32_t nodeIndex, const mat4 parentTransform,
                         AnoRenderableDesc* out, uint32_t cap, uint32_t* idx);

// Proof token: cgltf_data past cgltf_validate (minted in parseGltf).
typedef struct ValidatedGltf {
    const cgltf_data* data;
} ValidatedGltf;

// One sized allocation being handed out as sub-arrays.
typedef struct GltfBlock {
    uint8_t* cur;
    uint8_t* end;
} GltfBlock;

// Every carved element type must fit the 16-byte carve grain (ModelNode's mat4 needs 16).
static_assert(alignof(ModelAsset) <= 16 && alignof(ModelMesh) <= 16 &&
              alignof(ModelPrimitive) <= 16 && alignof(ModelNode) <= 16,
              "asset block carves assume <=16-byte alignment");
static_assert(alignof(Vertex) <= 16 && alignof(uint32_t) <= 16 && alignof(VkBuffer) <= 16,
              "scratch block carves assume <=16-byte alignment");

// Inputs: element count n, element size sz. Output: 16-rounded byte footprint.
static inline size_t gltf_span(size_t n, size_t sz)
{
    return (n * sz + 15u) & ~(size_t)15u;
}

// Inputs: blk over one zeroed allocation; n/sz exactly as summed during sizing.
// Output: zeroed 16-aligned sub-array for n elements of sz bytes.
// Invariant: cur never passes end while every carve was a sizing term.
static void* gltf_carve_bytes(GltfBlock* blk, size_t n, size_t sz)
{
    size_t bytes = gltf_span(n, sz);
#ifdef DEBUG_BUILD
    assert(blk->cur + bytes <= blk->end);
#endif
    void* p = blk->cur;
    blk->cur += bytes;
    return p;
}

template<ano::Data T>
[[nodiscard]] static T* gltf_carve(GltfBlock* blk, size_t n)
{
    return static_cast<T*>(gltf_carve_bytes(blk, n, sizeof(T)));
}

// Inputs: prim. Outputs: *pos/*norm/*tex accessors, NULL when absent (last texcoord wins).
// Output: true when uploadable (POSITION and indices present).
static bool prim_accessors(const cgltf_primitive* prim, cgltf_accessor** pos,
                           cgltf_accessor** norm, cgltf_accessor** tex)
{
    *pos = *norm = *tex = NULL;
    for (size_t a = 0; a < prim->attributes_count; ++a) {
        if (prim->attributes[a].type == cgltf_attribute_type_position) {
            *pos = prim->attributes[a].data;
        } else if (prim->attributes[a].type == cgltf_attribute_type_normal) {
            *norm = prim->attributes[a].data;
        } else if (prim->attributes[a].type == cgltf_attribute_type_texcoord) {
            *tex = prim->attributes[a].data;
        }
    }
    return *pos != NULL && prim->indices != NULL;
}

// Inputs: d, imgUsage, tex (or NULL), bit.
// Output: bit OR'd into tex's image mask.
static inline void mark_texture(const cgltf_data* d, TextureUsageFlags* imgUsage,
                                const cgltf_texture* tex, TextureUsageFlags bit)
{
    if (tex && tex->image) imgUsage[tex->image - d->images] |= bit;
}

// Inputs: d, slots (per-image), tex (or NULL).
// Output: bindless slot for tex's image, or ANO_BINDLESS_NONE.
static inline uint32_t gltf_slot(const cgltf_data* d, const uint32_t* slots, const cgltf_texture* tex)
{
    return (tex && tex->image) ? slots[tex->image - d->images] : ANO_BINDLESS_NONE;
}

// Inputs: material, canonical texture source. Output: referenced cgltf texture or NULL.
static inline const cgltf_texture* gltf_material_texture(
    const cgltf_material* material, AnoGltfTextureSource source)
{
    using enum AnoGltfTextureSource;
    switch (source) {
    case base_color:
        return material->has_pbr_metallic_roughness
            ? material->pbr_metallic_roughness.base_color_texture.texture : NULL;
    case metallic_roughness:
        return material->has_pbr_metallic_roughness
            ? material->pbr_metallic_roughness.metallic_roughness_texture.texture : NULL;
    case normal: return material->normal_texture.texture;
    case occlusion: return material->occlusion_texture.texture;
    case emissive: return material->emissive_texture.texture;
    case clearcoat: return material->clearcoat.clearcoat_texture.texture;
    case clearcoat_roughness: return material->clearcoat.clearcoat_roughness_texture.texture;
    case clearcoat_normal: return material->clearcoat.clearcoat_normal_texture.texture;
    case transmission: return material->transmission.transmission_texture.texture;
    case thickness: return material->volume.thickness_texture.texture;
    case specular: return material->specular.specular_texture.texture;
    case specular_color: return material->specular.specular_color_texture.texture;
    case sheen_color: return material->sheen.sheen_color_texture.texture;
    case sheen_roughness: return material->sheen.sheen_roughness_texture.texture;
    case iridescence: return material->iridescence.iridescence_texture.texture;
    case iridescence_thickness: return material->iridescence.iridescence_thickness_texture.texture;
    case anisotropy: return material->anisotropy.anisotropy_texture.texture;
    case diffuse_transmission: return material->diffuse_transmission.diffuse_transmission_texture.texture;
    case diffuse_transmission_color: return material->diffuse_transmission.diffuse_transmission_color_texture.texture;
    case count: return NULL;
    }
    return NULL;
}

consteval bool gltf_material_texture_schema_valid()
{
    bool seen[static_cast<size_t>(AnoGltfTextureSource::count)]{};
    size_t found = 0;
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTexture));
        static_assert(annotations.size() <= 1);
        if constexpr (!annotations.empty()) {
            constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
            using Field = [:std::meta::type_of(member):];
            static_assert(std::is_same_v<Field, uint32_t>);
            const size_t source = static_cast<size_t>(spec.source);
            if (source >= static_cast<size_t>(AnoGltfTextureSource::count) || seen[source])
                return false;
            if (spec.feature == 0 || (spec.feature & (spec.feature - 1u)) != 0)
                return false;
            if (spec.domain != AnoMaterialTextureDomain::color &&
                spec.domain != AnoMaterialTextureDomain::data)
                return false;
            seen[source] = true;
            ++found;
        }
    }
    return found == static_cast<size_t>(AnoGltfTextureSource::count);
}

static_assert(gltf_material_texture_schema_valid());

static PbrFeatureFlags gltf_material_texture_features(const cgltf_material* material)
{
    PbrFeatureFlags features = PBR_FEATURE_NONE;
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTexture));
        if constexpr (!annotations.empty()) {
            constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
            if constexpr (spec.detectsFeature) {
                if (gltf_material_texture(material, spec.source)) features |= spec.feature;
            }
        }
    }
    return features;
}

static void gltf_mark_material_textures(const cgltf_data* data, TextureUsageFlags* imageUsage,
                                        const cgltf_material* material,
                                        PbrFeatureFlags supportedFeatures)
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTexture));
        if constexpr (!annotations.empty()) {
            constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
            if (supportedFeatures & spec.feature) {
                constexpr TextureUsageFlags usage = spec.domain == AnoMaterialTextureDomain::color
                    ? TEXTURE_USE_COLOR : TEXTURE_USE_DATA;
                mark_texture(data, imageUsage, gltf_material_texture(material, spec.source), usage);
            }
        }
    }
}

static void gltf_project_material_textures(MaterialData* output, const cgltf_data* data,
                                           const uint32_t* colorIndex, const uint32_t* dataIndex,
                                           const cgltf_material* material,
                                           PbrFeatureFlags supportedFeatures)
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTexture));
        if constexpr (!annotations.empty()) {
            constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
            const cgltf_texture* texture = gltf_material_texture(material, spec.source);
            if ((supportedFeatures & spec.feature) && texture) {
                const uint32_t* slots = spec.domain == AnoMaterialTextureDomain::color
                    ? colorIndex : dataIndex;
                output->*(&[:member:]) = gltf_slot(data, slots, texture);
            }
        }
    }
}

// Inputs: g (validated). Outputs: *primsTotal/*childTotal/*rootTotal.
// Output: persistent asset block byte size (gltf_span carve sequence).
static size_t asset_block_size(ValidatedGltf g, size_t* primsTotal, size_t* childTotal,
                               size_t* rootTotal)
{
    const cgltf_data* d = g.data;
    size_t prims = 0, children = 0, roots = 0;
    for (size_t m = 0; m < d->meshes_count; ++m)
        prims += d->meshes[m].primitives_count;
    for (size_t n = 0; n < d->nodes_count; ++n) {
        children += d->nodes[n].children_count;
        if (!d->nodes[n].parent)
            roots++;
    }
    *primsTotal = prims;
    *childTotal = children;
    *rootTotal = roots;
    return gltf_span(1, sizeof(ModelAsset))
         + gltf_span(d->meshes_count, sizeof(ModelMesh))
         + gltf_span(prims, sizeof(ModelPrimitive))
         + gltf_span(d->nodes_count, sizeof(ModelNode))
         + gltf_span(children, sizeof(uint32_t))
         + gltf_span(roots, sizeof(uint32_t));
}

// Inputs: g (validated). Outputs: *maxVerts/*maxIdx (widest uploadable primitive).
static void scratch_extents(ValidatedGltf g, size_t* maxVerts, size_t* maxIdx)
{
    const cgltf_data* d = g.data;
    size_t verts = 0, idx = 0;
    for (size_t m = 0; m < d->meshes_count; ++m) {
        for (size_t p = 0; p < d->meshes[m].primitives_count; ++p) {
            cgltf_accessor *pos, *norm, *tex;
            if (!prim_accessors(&d->meshes[m].primitives[p], &pos, &norm, &tex))
                continue;
            if (pos->count > verts)
                verts = pos->count;
            if (d->meshes[m].primitives[p].indices->count > idx)
                idx = d->meshes[m].primitives[p].indices->count;
        }
    }
    *maxVerts = verts;
    *maxIdx = idx;
}

ModelAsset* parseGltf(VulkanContext* ctx, const char* fileName)
{
    cgltf_options options = {};
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

    // Untrusted-input gate: cgltf_validate (accessor/bufferView/buffer ranges).
    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        ano_log(ANO_ERROR, "glTF failed validation (result %d), rejecting: %s", (int)result, fileName);
        cgltf_free(data);
        return NULL;
    }
    ValidatedGltf gltf = { data };

    ano_debug_log(ANO_INFO, "Successfully parsed %s with cgltf!", fileName);

    // Persistent ModelAsset block (one calloc, one free at unload).
    size_t primsTotal, childTotal, rootTotal;
    size_t assetBytes = asset_block_size(gltf, &primsTotal, &childTotal, &rootTotal);
    uint8_t* assetBase = ano::allocate_zero<uint8_t>(assetBytes);
    if (!assetBase) {
        ano_log(ANO_ERROR, "Failed to allocate %zu-byte asset block for: %s", assetBytes, fileName);
        cgltf_free(data);
        return NULL;
    }
    GltfBlock assetBlk = { assetBase, assetBase + assetBytes };
    ModelAsset*     asset     = gltf_carve<ModelAsset>(&assetBlk, 1);
    ModelMesh*      meshPool  = gltf_carve<ModelMesh>(&assetBlk, data->meshes_count);
    ModelPrimitive* primPool  = gltf_carve<ModelPrimitive>(&assetBlk, primsTotal);
    ModelNode*      nodePool  = gltf_carve<ModelNode>(&assetBlk, data->nodes_count);
    uint32_t*       childPool = gltf_carve<uint32_t>(&assetBlk, childTotal);
    uint32_t*       rootPool  = gltf_carve<uint32_t>(&assetBlk, rootTotal);
    strncpy(asset->name, fileName, 63);

    // Scratch block (LOCALHEAPATTR): verts/indices (widest prim) + image slots + staging.
    size_t maxVerts, maxIdx;
    scratch_extents(gltf, &maxVerts, &maxIdx);
    // Staging bound follows images (one upload each).
    size_t maxStaging = 10 + data->images_count;
    size_t scratchBytes = gltf_span(maxVerts, sizeof(Vertex))
                        + gltf_span(maxIdx, sizeof(uint32_t))
                        + gltf_span(data->images_count, sizeof(uint32_t))          // colorIndex
                        + gltf_span(data->images_count, sizeof(uint32_t))          // dataIndex
                        + gltf_span(data->images_count, sizeof(uint32_t))          // imageUsage
                        + gltf_span(maxStaging, sizeof(VkBuffer));                 // stagingBuffers
    mi_heap_t* scratchHeap LOCALHEAPATTR = mi_heap_new();
    uint8_t* scratchBase = scratchHeap ? ano::heap_allocate_zero<uint8_t>(scratchHeap, scratchBytes) : NULL;
    if (!scratchBase) {
        ano_log(ANO_ERROR, "Failed to allocate %zu-byte scratch block for: %s", scratchBytes, fileName);
        free(assetBase);
        cgltf_free(data);
        return NULL;
    }
    GltfBlock scratchBlk = { scratchBase, scratchBase + scratchBytes };
    Vertex*            vertices       = gltf_carve<Vertex>(&scratchBlk, maxVerts);
    uint32_t*          indices        = gltf_carve<uint32_t>(&scratchBlk, maxIdx);
    uint32_t*          colorIndex     = gltf_carve<uint32_t>(&scratchBlk, data->images_count);
    uint32_t*          dataIndex      = gltf_carve<uint32_t>(&scratchBlk, data->images_count);
    TextureUsageFlags* imageUsage     = gltf_carve<TextureUsageFlags>(&scratchBlk, data->images_count);
    VkBuffer*          stagingBuffers = gltf_carve<VkBuffer>(&scratchBlk, maxStaging);

    // Seed ANO_BINDLESS_NONE.
    static_assert(ANO_BINDLESS_NONE == 0xFFFFFFFFu, "0xFF fill must equal ANO_BINDLESS_NONE");
    memset(colorIndex, 0xFF, data->images_count * sizeof(uint32_t));
    memset(dataIndex,  0xFF, data->images_count * sizeof(uint32_t));

    // 1. Upload Geometry & Map to Asset Meshes
    asset->meshCount = data->meshes_count;
    asset->meshes = meshPool;
    size_t primsUsed = 0; // running sub-slice offset into primPool

    for (size_t m = 0; m < data->meshes_count; ++m) {
        cgltf_mesh* cgMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];
        
        outMesh->primitiveCount = cgMesh->primitives_count;
        outMesh->primitives = primPool + primsUsed;
        primsUsed += cgMesh->primitives_count;

        for (size_t p = 0; p < cgMesh->primitives_count; ++p) {
            cgltf_primitive* prim = &cgMesh->primitives[p];
            outMesh->primitives[p].geometryPoolIndex = ANO_MESH_NONE;

            cgltf_accessor *posAccessor, *normAccessor, *texAccessor;
            if (!prim_accessors(prim, &posAccessor, &normAccessor, &texAccessor)) {
                ano_log(ANO_WARN, "Warning: Primitive missing positions or indices. Skipping.");
                continue;
            }

            uint32_t vertexCount = posAccessor->count;
#ifdef DEBUG_BUILD
            assert(vertexCount <= maxVerts); // extents came from the same prim_accessors walk
#endif
            // Re-zero reused scratch.
            memset(vertices, 0, vertexCount * sizeof(Vertex));

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
#ifdef DEBUG_BUILD
            assert(indexCount <= maxIdx);
#endif
            for (uint32_t i = 0; i < indexCount; ++i) {
                indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
            }
            
            // Upload as an LOD chain; geometryPoolIndex is the chain base.
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
        }
    }

    // Identify PBR features globally supported by the active pipelines
    PbrFeatureFlags activeFeatures = ano_vk_get_active_pipelines_supported_features(&rendererState);
    ano_debug_log(ANO_INFO, "[GLTF DEBUG] Active pipeline PBR features supported: 0x%08X", activeFeatures);

    // Mark texture roles per image (COLOR=sRGB, DATA=linear).
    if (data->textures_count > 0) {
        for (size_t m = 0; m < data->materials_count; ++m) {
            cgltf_material* mat = &data->materials[m];
            PbrFeatureFlags matFeatures = ano_gltf_identify_material_features(mat);
            PbrFeatureFlags supportedFeatures = matFeatures & activeFeatures;
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Material %zu (%s): required features = 0x%08X, supported = 0x%08X",
                   m, mat->name ? mat->name : "unnamed", matFeatures, supportedFeatures);

            gltf_mark_material_textures(data, imageUsage, mat, supportedFeatures);
        }
    }

    // 2. Upload Textures & Bind Materials
    // Batch CB for texture uploads. VK_NULL_HANDLE -> per-image mint; epilogue ends a held CB only.
    VkCommandBuffer textureCmd = beginSingleTimeCommands(ctx);
    if (textureCmd == VK_NULL_HANDLE)
        ano_log(ANO_WARN, "No transient command buffer for the texture batch; uploading per image.");
    uint32_t stagingCount = 0;
    // Halt latch: device/arena, registry, or bindless full.
    bool haltLoad = false;
    uint32_t skipped = 0;
    // Registry-refused package: destroy in epilogue after the batch retires.
    TexturePackage refused = {0};

    // One upload per image; views follow imageUsage.
    for (size_t i = 0; i < data->images_count; ++i) {
        cgltf_image* img = &data->images[i];
        if (!img->uri)
            continue;
        if (imageUsage[i] == TEXTURE_USE_NONE) {
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Skipping image %zu: %s (not needed or unsupported by pipeline)", i, img->uri);
            continue;
        }
        // Halted: skip before acquisition.
        if (haltLoad) {
            skipped++;
            continue;
        }
        ano_debug_log(ANO_INFO, "[GLTF DEBUG] Loading image %zu: %s", i, img->uri);
        // Resolve image URI against the glTF file's directory, then percent-decode the tail.
        char texPath[1024];
        if (strlen(fileName) + strlen(img->uri) + 1 >= sizeof texPath) {
            ano_log(ANO_WARN, "Image URI too long, skipping: %s", img->uri);
            continue;
        }
        cgltf_combine_paths(texPath, fileName, img->uri);
        cgltf_decode_uri(texPath + strlen(texPath) - strlen(img->uri));

        TexturePackage pkg = {0};
        // Exhaustive over AnoTextureResultCode.
        switch (createTextureImage(ctx, textureCmd, &pkg, texPath, false, imageUsage[i], true).code) {
        case ANO_TEXTURE_BUILT:
            break;
        case ANO_TEXTURE_SOURCE:
            ano_log(ANO_WARN, "Unusable image source, skipping: %s", img->uri);
            continue;
        case ANO_TEXTURE_INVALID:
            ano_log(ANO_ERROR, "Image request outside the constructor's contract: %s", img->uri);
            continue;
        case ANO_TEXTURE_DEVICE:
            ano_log(ANO_ERROR, "Device or texture arena refused %s; halting texture construction.", img->uri);
            haltLoad = true;
            continue;
        }

        if (pkg.staging) stagingBuffers[stagingCount++] = pkg.staging; // batch-owned

        if (!ano_vk_register_texture(&rendererState.primitives, ano_texture_record(&pkg))) {
            ano_log(ANO_ERROR, "Texture registry refused %s; halting texture construction.", img->uri);
            refused = pkg; // epilogue destroy
            haltLoad = true;
            continue;
        }
        // Bindless slots per image domain. ANO_BINDLESS_NONE on refusal.
        if (imageUsage[i] & TEXTURE_USE_COLOR) {
            colorIndex[i] = bindless_register_texture(ctx, &rendererState.bindlessTextures, pkg.srgbView, rendererState.textureSampler);
            haltLoad |= colorIndex[i] == ANO_BINDLESS_NONE;
        }
        if (!haltLoad && (imageUsage[i] & TEXTURE_USE_DATA)) {
            dataIndex[i] = bindless_register_texture(ctx, &rendererState.bindlessTextures, pkg.unormView, rendererState.textureSampler);
            haltLoad |= dataIndex[i] == ANO_BINDLESS_NONE;
        }
        if (haltLoad)
            ano_log(ANO_WARN, "No bindless slot for image %s; halting texture construction.", img->uri);
    }

    if (skipped)
        ano_log(ANO_WARN, "Texture construction halted: skipped %u further image uploads; those materials sample untextured.", skipped);

    if (textureCmd != VK_NULL_HANDLE) endSingleTimeCommands(ctx, textureCmd);

    // Post-submit: destroy staging and any refused views/image (arena span stays).
    for (uint32_t i = 0; i < stagingCount; ++i) {
        vkDestroyBuffer(ctx->device, stagingBuffers[i], NULL);
    }
    vkDestroyImageView(ctx->device, refused.unormView, NULL);
    vkDestroyImageView(ctx->device, refused.srgbView, NULL);
    vkDestroyImage(ctx->device, refused.image, NULL);
    refused = (TexturePackage){0};
    gpu_alloc_reset(&stagingAllocator);

    // Pre-validate material buffer capacity
    uint32_t totalPrimitives = primsTotal;
    if (rendererState.materialBuffer.count + totalPrimitives > rendererState.materialBuffer.capacity) {
        ano_log(ANO_WARN, "Warning: Material buffer cannot fit %u new materials (Capacity: %u, Current: %u). Some materials will fall back to index 0.", 
               totalPrimitives, rendererState.materialBuffer.capacity, rendererState.materialBuffer.count);
    }

    // 3. Bake Material SSBO entries per primitive
    // Slot = colorIndex/dataIndex (already NONE when unavailable). Normal/occlusion guard their scalars.
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
                // Fallback to index 0
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
                    gltf_project_material_textures(&matData, data, colorIndex, dataIndex,
                                                   prim->material, supportedFeatures);
                    
                    // 1. pbrMetallicRoughness
                    if (prim->material->has_pbr_metallic_roughness) {
                        for (int i = 0; i < 4; i++) {
                            matData.baseColorFactor[i] = (float)prim->material->pbr_metallic_roughness.base_color_factor[i];
                        }
                        matData.metallicFactor = (float)prim->material->pbr_metallic_roughness.metallic_factor;
                        matData.roughnessFactor = (float)prim->material->pbr_metallic_roughness.roughness_factor;
                    }
                    
                    // 2. Core properties
                    if ((supportedFeatures & PBR_FEATURE_NORMAL_TEXTURE) &&
                        matData.normalTexture != ANO_BINDLESS_NONE)
                        matData.normalScale = (float)prim->material->normal_texture.scale;
                    
                    if ((supportedFeatures & PBR_FEATURE_OCCLUSION_TEXTURE) &&
                        matData.occlusionTexture != ANO_BINDLESS_NONE)
                        matData.occlusionStrength = (float)prim->material->occlusion_texture.scale;
                    
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
                    }
                    
                    // 4. Transmission
                    if (supportedFeatures & PBR_FEATURE_TRANSMISSION) {
                        matData.transmissionFactor = (float)prim->material->transmission.transmission_factor;
                    }
                    
                    // 5. Volume
                    if (supportedFeatures & PBR_FEATURE_VOLUME) {
                        matData.thicknessFactor = (float)prim->material->volume.thickness_factor;
                        matData.attenuationDistance = (float)prim->material->volume.attenuation_distance;
                        for (int i = 0; i < 3; i++) {
                            matData.attenuationColor[i] = (float)prim->material->volume.attenuation_color[i];
                        }
                        matData.attenuationColor[3] = 1.0f; // Padding
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
                    }
                    
                    // 8. Sheen
                    if (supportedFeatures & PBR_FEATURE_SHEEN) {
                        matData.sheenRoughnessFactor = (float)prim->material->sheen.sheen_roughness_factor;
                        for (int i = 0; i < 3; i++) {
                            matData.sheenColorFactor[i] = (float)prim->material->sheen.sheen_color_factor[i];
                        }
                        matData.sheenColorFactor[3] = 1.0f; // Padding
                    }
                    
                    // 9. Iridescence
                    if (supportedFeatures & PBR_FEATURE_IRIDESCENCE) {
                        matData.iridescenceFactor = (float)prim->material->iridescence.iridescence_factor;
                        matData.iridescenceIor = (float)prim->material->iridescence.iridescence_ior;
                        matData.iridescenceThicknessMinimum = (float)prim->material->iridescence.iridescence_thickness_min;
                        matData.iridescenceThicknessMaximum = (float)prim->material->iridescence.iridescence_thickness_max;
                    }
                    
                    // 10. Anisotropy
                    if (supportedFeatures & PBR_FEATURE_ANISOTROPY) {
                        matData.anisotropyStrength = (float)prim->material->anisotropy.anisotropy_strength;
                        matData.anisotropyRotation = (float)prim->material->anisotropy.anisotropy_rotation;
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
                    }
                    
                    // 13. Emissive Strength
                    if (supportedFeatures & PBR_FEATURE_EMISSIVE_STRENGTH) {
                        matData.emissiveStrength = (float)prim->material->emissive_strength.emissive_strength;
                    }
                    
                    // Pipeline routing:
                    //   transmission/volume         -> PIPELINE_TRANSMISSION
                    //   emissiveStrength>1 OR BLEND  -> PIPELINE_ADDITIVE
                    //   alphaMode MASK               -> PIPELINE_FLAT_MASKED
                    //   opaque + doubleSided         -> PIPELINE_FLAT_TWOSIDED
                    //   otherwise                    -> PIPELINE_FLAT
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

    // 4. Construct Node Hierarchy
    asset->nodeCount = data->nodes_count;
    asset->nodes = nodePool;
    size_t childUsed = 0; // running sub-slice offset into childPool

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
            outNode->childIndices = childPool + childUsed;
            childUsed += outNode->childCount;
            for (uint32_t c = 0; c < outNode->childCount; ++c) {
                outNode->childIndices[c] = cgNode->children[c] - data->nodes;
            }
        }
    }
    
    // Store root nodes (all parentless nodes, counted by asset_block_size)
    asset->rootNodeCount = rootTotal;
    if (rootTotal > 0) {
        asset->rootNodes = rootPool;
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

PbrFeatureFlags ano_gltf_identify_material_features(const cgltf_material* material) {
    if (!material) {
        return PBR_FEATURE_NONE;
    }
    
    PbrFeatureFlags features = gltf_material_texture_features(material);
    
    // 1. pbrMetallicRoughness
    if (material->has_pbr_metallic_roughness) {
        features |= PBR_FEATURE_BASE_COLOR_FACTOR;
        features |= PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR;
    }
    
    // 2. Core properties
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
