/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include "ano_GltfParser.h"
#include "cpp/ano_alloc.h"
#include <string.h>
#include <assert.h>
#include <meta>
#include <type_traits>
#include <anoptic_log.h>
#include <anogltf.h>

extern GpuAllocator stagingAllocator;
extern RendererState rendererState;

// Recursive flatten walk.
static void flatten_node(const ModelAsset* asset, uint32_t nodeIndex, const mat4 parentTransform,
                         AnoRenderableDesc* out, uint32_t cap, uint32_t* idx);
static PbrFeatureFlags gltf_identify_material_features(const AnoGltfMaterial* material);

// Proof token: AnoGltfData past buffer loading and validation (minted in parseGltf).
typedef struct ValidatedGltf {
    const AnoGltfData* data;
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

// Inputs: validated data and prim. Outputs: *pos/*norm/*tex, NULL when absent (last texcoord wins).
// Output: true when uploadable (POSITION and indices present).
static bool prim_accessors(const AnoGltfData* data, const AnoGltfPrimitive* prim,
                           const AnoGltfAccessor** pos, const AnoGltfAccessor** norm,
                           const AnoGltfAccessor** tex)
{
    *pos = *norm = *tex = NULL;
    for (uint32_t a = 0; a < prim->attributes.values.count; ++a) {
        const AnoGltfAttribute* attribute = &prim->attributes.values.data[a];
        if (!ano_gltf_has_index(attribute->accessor) || attribute->accessor.value >= data->accessorsCount)
            continue;
        const AnoGltfAccessor* accessor = &data->accessors[attribute->accessor.value];
        if (attribute->type == AnoGltfAttributeType::position) {
            *pos = accessor;
        } else if (attribute->type == AnoGltfAttributeType::normal) {
            *norm = accessor;
        } else if (attribute->type == AnoGltfAttributeType::texcoord) {
            *tex = accessor;
        }
    }
    return *pos != NULL && ano_gltf_has_index(prim->indices)
        && prim->indices.value < data->accessorsCount;
}

// Inputs: d and texture-info (or absent). Output: referenced source image index, or NO_INDEX.
static inline uint32_t gltf_texture_image(const AnoGltfData* d, const AnoGltfTextureInfo* info)
{
    if (!info || !ano_gltf_has_index(info->index) || info->index.value >= d->texturesCount)
        return ANO_GLTF_NO_INDEX;
    const AnoGltfTexture* texture = &d->textures[info->index.value];
    return ano_gltf_has_index(texture->source) && texture->source.value < d->imagesCount
        ? texture->source.value : ANO_GLTF_NO_INDEX;
}

// Inputs: d, imgUsage, texture-info (or absent), bit. Output: bit OR'd into source image mask.
static inline void mark_texture(const AnoGltfData* d, TextureUsageFlags* imgUsage,
                                const AnoGltfTextureInfo* info, TextureUsageFlags bit)
{
    const uint32_t image = gltf_texture_image(d, info);
    if (image != ANO_GLTF_NO_INDEX)
        imgUsage[image] |= bit;
}

// Inputs: d, slots (per-image), texture-info (or absent). Output: slot or ANO_BINDLESS_NONE.
static inline uint32_t gltf_slot(const AnoGltfData* d, const uint32_t* slots,
                                 const AnoGltfTextureInfo* info)
{
    const uint32_t image = gltf_texture_image(d, info);
    return image != ANO_GLTF_NO_INDEX ? slots[image] : ANO_BINDLESS_NONE;
}

// Inputs: material, canonical texture source. Output: referenced texture-info or NULL.
static inline const AnoGltfTextureInfo* gltf_material_texture(
    const AnoGltfMaterial* material, AnoGltfTextureSource source)
{
    using enum AnoGltfTextureSource;
    const AnoGltfMaterialExtensionsKnown& ext = material->extensions.known;
    switch (source) {
    case base_color:
        return material->pbrMetallicRoughness.present
            ? &material->pbrMetallicRoughness.value.baseColorTexture : NULL;
    case metallic_roughness:
        return material->pbrMetallicRoughness.present
            ? &material->pbrMetallicRoughness.value.metallicRoughnessTexture : NULL;
    case normal: return &material->normalTexture;
    case occlusion: return &material->occlusionTexture;
    case emissive: return &material->emissiveTexture;
    case clearcoat: return ext.KHR_materials_clearcoat.present
        ? &ext.KHR_materials_clearcoat.value.clearcoatTexture : NULL;
    case clearcoat_roughness: return ext.KHR_materials_clearcoat.present
        ? &ext.KHR_materials_clearcoat.value.clearcoatRoughnessTexture : NULL;
    case clearcoat_normal: return ext.KHR_materials_clearcoat.present
        ? &ext.KHR_materials_clearcoat.value.clearcoatNormalTexture : NULL;
    case transmission: return ext.KHR_materials_transmission.present
        ? &ext.KHR_materials_transmission.value.transmissionTexture : NULL;
    case thickness: return ext.KHR_materials_volume.present
        ? &ext.KHR_materials_volume.value.thicknessTexture : NULL;
    case specular: return ext.KHR_materials_specular.present
        ? &ext.KHR_materials_specular.value.specularTexture : NULL;
    case specular_color: return ext.KHR_materials_specular.present
        ? &ext.KHR_materials_specular.value.specularColorTexture : NULL;
    case sheen_color: return ext.KHR_materials_sheen.present
        ? &ext.KHR_materials_sheen.value.sheenColorTexture : NULL;
    case sheen_roughness: return ext.KHR_materials_sheen.present
        ? &ext.KHR_materials_sheen.value.sheenRoughnessTexture : NULL;
    case iridescence: return ext.KHR_materials_iridescence.present
        ? &ext.KHR_materials_iridescence.value.iridescenceTexture : NULL;
    case iridescence_thickness: return ext.KHR_materials_iridescence.present
        ? &ext.KHR_materials_iridescence.value.iridescenceThicknessTexture : NULL;
    case anisotropy: return ext.KHR_materials_anisotropy.present
        ? &ext.KHR_materials_anisotropy.value.anisotropyTexture : NULL;
    case diffuse_transmission: return ext.KHR_materials_diffuse_transmission.present
        ? &ext.KHR_materials_diffuse_transmission.value.diffuseTransmissionTexture : NULL;
    case diffuse_transmission_color: return ext.KHR_materials_diffuse_transmission.present
        ? &ext.KHR_materials_diffuse_transmission.value.diffuseTransmissionColorTexture : NULL;
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

static PbrFeatureFlags gltf_material_texture_features(const AnoGltfMaterial* material)
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
                const AnoGltfTextureInfo* info = gltf_material_texture(material, spec.source);
                if (info && ano_gltf_has_index(info->index)) features |= spec.feature;
            }
        }
    }
    return features;
}

static void gltf_mark_material_textures(const AnoGltfData* data, TextureUsageFlags* imageUsage,
                                        const AnoGltfMaterial* material,
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

static void gltf_project_material_textures(MaterialData* output, const AnoGltfData* data,
                                           const uint32_t* colorIndex, const uint32_t* dataIndex,
                                           const AnoGltfMaterial* material,
                                           PbrFeatureFlags supportedFeatures)
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTexture));
        if constexpr (!annotations.empty()) {
            constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
            const AnoGltfTextureInfo* texture = gltf_material_texture(material, spec.source);
            if ((supportedFeatures & spec.feature) && texture && ano_gltf_has_index(texture->index)) {
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
    const AnoGltfData* d = g.data;
    size_t prims = 0, children = 0, roots = 0;
    for (uint32_t m = 0; m < d->meshesCount; ++m)
        prims += d->meshes[m].primitives.count;
    for (uint32_t n = 0; n < d->nodesCount; ++n) {
        children += d->nodes[n].children.count;
        if (!ano_gltf_has_index(d->nodes[n].parent))
            roots++;
    }
    *primsTotal = prims;
    *childTotal = children;
    *rootTotal = roots;
    return gltf_span(1, sizeof(ModelAsset))
         + gltf_span(d->meshesCount, sizeof(ModelMesh))
         + gltf_span(prims, sizeof(ModelPrimitive))
         + gltf_span(d->nodesCount, sizeof(ModelNode))
         + gltf_span(children, sizeof(uint32_t))
         + gltf_span(roots, sizeof(uint32_t));
}

// Inputs: g (validated). Outputs: *maxVerts/*maxIdx (widest uploadable primitive).
static void scratch_extents(ValidatedGltf g, size_t* maxVerts, size_t* maxIdx)
{
    const AnoGltfData* d = g.data;
    size_t verts = 0, idx = 0;
    for (uint32_t m = 0; m < d->meshesCount; ++m) {
        for (uint32_t p = 0; p < d->meshes[m].primitives.count; ++p) {
            const AnoGltfAccessor *pos, *norm, *tex;
            const AnoGltfPrimitive* prim = &d->meshes[m].primitives.data[p];
            if (!prim_accessors(d, prim, &pos, &norm, &tex))
                continue;
            if (pos->count > verts)
                verts = pos->count;
            const AnoGltfAccessor* indices = &d->accessors[prim->indices.value];
            if (indices->count > idx)
                idx = indices->count;
        }
    }
    *maxVerts = verts;
    *maxIdx = idx;
}

// Inputs: exact anogltf allocation size. Output: engine-allocator storage or NULL.
static void* gltf_allocate(void*, size_t bytes)
{
    return ano::allocate<uint8_t>(bytes);
}

// Inputs: an allocation returned by gltf_allocate. Output: allocation released.
static void gltf_release(void*, void* allocation)
{
    free(allocation);
}

// Inputs: host path. Output: exact byte length for anogltf, or a bounded I/O result.
static AnoGltfResult gltf_file_size(void*, const char* path, size_t* bytes)
{
    if (!path || !bytes)
        return AnoGltfResult::invalid_options;
    FILE* file = fopen(path, "rb");
    if (!file)
        return AnoGltfResult::file_not_found;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return AnoGltfResult::io_error;
    }
    const long length = ftell(file);
    fclose(file);
    if (length < 0)
        return AnoGltfResult::io_error;
    *bytes = static_cast<size_t>(length);
    return AnoGltfResult::success;
}

// Inputs: host path and exact destination span. Output: all bytes read or an I/O result.
static AnoGltfResult gltf_file_read(void*, const char* path, void* destination, size_t bytes)
{
    if (!path || (!destination && bytes != 0))
        return AnoGltfResult::invalid_options;
    FILE* file = fopen(path, "rb");
    if (!file)
        return AnoGltfResult::file_not_found;
    const bool read = bytes == 0 || fread(destination, 1, bytes, file) == bytes;
    const bool closed = fclose(file) == 0;
    return read && closed ? AnoGltfResult::success : AnoGltfResult::io_error;
}

// Inputs: glTF path and relative URI. Output: combined, decoded path or false when too long.
static bool gltf_image_path(char output[1024], const char* gltfPath, AnoGltfString uri)
{
    const char* slash = strrchr(gltfPath, '/');
    const char* backslash = strrchr(gltfPath, '\\');
    const char* separator = slash;
    if (!separator || (backslash && backslash > separator))
        separator = backslash;
    const size_t prefix = separator ? static_cast<size_t>(separator - gltfPath) + 1u : 0u;
    if (prefix + uri.length + 1u > 1024u)
        return false;
    if (prefix)
        memcpy(output, gltfPath, prefix);
    if (uri.length)
        memcpy(output + prefix, uri.data, uri.length);
    output[prefix + uri.length] = '\0';
    const size_t decodedLength = ano_gltf_decode_uri(output + prefix);
    (void)decodedLength;
    return true;
}

ModelAsset* parseGltf(VulkanContext* ctx, const char* fileName)
{
    AnoGltfOptions options = {};
    options.allocate = gltf_allocate;
    options.free = gltf_release;
    options.fileSize = gltf_file_size;
    options.fileRead = gltf_file_read;
    AnoGltfData* data = NULL;
    AnoGltfResult result = ano_gltf_parse_file(fileName, &options, &data);

    if (result != AnoGltfResult::success) {
        ano_log(ANO_ERROR, "Failed to parse glTF file (%s): %s",
                ano_gltf_result_string(result), fileName);
        return NULL;
    }

    result = ano_gltf_load_buffers(data, fileName, &options);
    if (result != AnoGltfResult::success) {
        ano_log(ANO_ERROR, "Failed to load glTF buffers (%s): %s",
                ano_gltf_result_string(result), fileName);
        ano_gltf_free(data);
        return NULL;
    }

    // Untrusted-input gate: buffer, view, accessor and sparse ranges are all loaded and bounded.
    result = ano_gltf_validate_loaded_data(data);
    if (result != AnoGltfResult::success) {
        ano_log(ANO_ERROR, "glTF failed validation (%s), rejecting: %s",
                ano_gltf_result_string(result), fileName);
        ano_gltf_free(data);
        return NULL;
    }
    ValidatedGltf gltf = { data };

    ano_debug_log(ANO_INFO, "Successfully parsed %s with anogltf!", fileName);

    // Persistent ModelAsset block (one calloc, one free at unload).
    size_t primsTotal, childTotal, rootTotal;
    size_t assetBytes = asset_block_size(gltf, &primsTotal, &childTotal, &rootTotal);
    uint8_t* assetBase = ano::allocate_zero<uint8_t>(assetBytes);
    if (!assetBase) {
        ano_log(ANO_ERROR, "Failed to allocate %zu-byte asset block for: %s", assetBytes, fileName);
        ano_gltf_free(data);
        return NULL;
    }
    GltfBlock assetBlk = { assetBase, assetBase + assetBytes };
    ModelAsset*     asset     = gltf_carve<ModelAsset>(&assetBlk, 1);
    ModelMesh*      meshPool  = gltf_carve<ModelMesh>(&assetBlk, data->meshesCount);
    ModelPrimitive* primPool  = gltf_carve<ModelPrimitive>(&assetBlk, primsTotal);
    ModelNode*      nodePool  = gltf_carve<ModelNode>(&assetBlk, data->nodesCount);
    uint32_t*       childPool = gltf_carve<uint32_t>(&assetBlk, childTotal);
    uint32_t*       rootPool  = gltf_carve<uint32_t>(&assetBlk, rootTotal);
    strncpy(asset->name, fileName, 63);

    // Scratch block (LOCALHEAPATTR): verts/indices (widest prim) + image slots + staging.
    size_t maxVerts, maxIdx;
    scratch_extents(gltf, &maxVerts, &maxIdx);
    // Staging bound follows images (one upload each).
    size_t maxStaging = 10 + data->imagesCount;
    size_t scratchBytes = gltf_span(maxVerts, sizeof(Vertex))
                        + gltf_span(maxIdx, sizeof(uint32_t))
                        + gltf_span(data->imagesCount, sizeof(uint32_t))          // colorIndex
                        + gltf_span(data->imagesCount, sizeof(uint32_t))          // dataIndex
                        + gltf_span(data->imagesCount, sizeof(uint32_t))          // imageUsage
                        + gltf_span(maxStaging, sizeof(VkBuffer));                 // stagingBuffers
    mi_heap_t* scratchHeap LOCALHEAPATTR = mi_heap_new();
    uint8_t* scratchBase = scratchHeap ? ano::heap_allocate_zero<uint8_t>(scratchHeap, scratchBytes) : NULL;
    if (!scratchBase) {
        ano_log(ANO_ERROR, "Failed to allocate %zu-byte scratch block for: %s", scratchBytes, fileName);
        free(assetBase);
        ano_gltf_free(data);
        return NULL;
    }
    GltfBlock scratchBlk = { scratchBase, scratchBase + scratchBytes };
    Vertex*            vertices       = gltf_carve<Vertex>(&scratchBlk, maxVerts);
    uint32_t*          indices        = gltf_carve<uint32_t>(&scratchBlk, maxIdx);
    uint32_t*          colorIndex     = gltf_carve<uint32_t>(&scratchBlk, data->imagesCount);
    uint32_t*          dataIndex      = gltf_carve<uint32_t>(&scratchBlk, data->imagesCount);
    TextureUsageFlags* imageUsage     = gltf_carve<TextureUsageFlags>(&scratchBlk, data->imagesCount);
    VkBuffer*          stagingBuffers = gltf_carve<VkBuffer>(&scratchBlk, maxStaging);

    // Seed ANO_BINDLESS_NONE.
    static_assert(ANO_BINDLESS_NONE == 0xFFFFFFFFu, "0xFF fill must equal ANO_BINDLESS_NONE");
    memset(colorIndex, 0xFF, data->imagesCount * sizeof(uint32_t));
    memset(dataIndex,  0xFF, data->imagesCount * sizeof(uint32_t));

    // 1. Upload Geometry & Map to Asset Meshes
    asset->meshCount = data->meshesCount;
    asset->meshes = meshPool;
    size_t primsUsed = 0; // running sub-slice offset into primPool

    for (uint32_t m = 0; m < data->meshesCount; ++m) {
        const AnoGltfMesh* gltfMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];

        outMesh->primitiveCount = gltfMesh->primitives.count;
        outMesh->primitives = primPool + primsUsed;
        primsUsed += gltfMesh->primitives.count;

        for (uint32_t p = 0; p < gltfMesh->primitives.count; ++p) {
            const AnoGltfPrimitive* prim = &gltfMesh->primitives.data[p];
            outMesh->primitives[p].geometryPoolIndex = ANO_MESH_NONE;

            const AnoGltfAccessor *posAccessor, *normAccessor, *texAccessor;
            if (!prim_accessors(data, prim, &posAccessor, &normAccessor, &texAccessor)) {
                ano_log(ANO_WARN, "Warning: Primitive missing positions or indices. Skipping.");
                continue;
            }
            const AnoGltfAccessor* indexAccessor = &data->accessors[prim->indices.value];
            if (posAccessor->count > UINT32_MAX || indexAccessor->count > UINT32_MAX) {
                ano_log(ANO_WARN, "Primitive exceeds the renderer's 32-bit geometry limits. Skipping.");
                continue;
            }

            uint32_t vertexCount = static_cast<uint32_t>(posAccessor->count);
#ifdef DEBUG_BUILD
            assert(vertexCount <= maxVerts); // extents came from the same prim_accessors walk
#endif
            // Re-zero reused scratch.
            memset(vertices, 0, vertexCount * sizeof(Vertex));

            bool decoded = true;
            for (uint32_t v = 0; decoded && v < vertexCount; ++v) {
                decoded = ano_gltf_accessor_read_float(
                    data, posAccessor, v, &vertices[v].position.v[0], 3);
                if (normAccessor) {
                    decoded &= ano_gltf_accessor_read_float(
                        data, normAccessor, v, &vertices[v].normal.v[0], 3);
                } else {
                    vertices[v].normal.v[0] = 0.0f;
                    vertices[v].normal.v[1] = 1.0f;
                    vertices[v].normal.v[2] = 0.0f;
                }
                if (texAccessor) {
                    decoded &= ano_gltf_accessor_read_float(
                        data, texAccessor, v, &vertices[v].texCoord.v[0], 2);
                }
            }
            if (!decoded) {
                ano_log(ANO_WARN, "Primitive accessor decoding failed after validation. Skipping.");
                continue;
            }

            uint32_t indexCount = static_cast<uint32_t>(indexAccessor->count);
#ifdef DEBUG_BUILD
            assert(indexCount <= maxIdx);
#endif
            for (uint32_t i = 0; i < indexCount; ++i) {
                if (!ano_gltf_accessor_read_index(data, indexAccessor, i, &indices[i])) {
                    decoded = false;
                    break;
                }
            }
            if (!decoded) {
                ano_log(ANO_WARN, "Primitive index decoding failed after validation. Skipping.");
                continue;
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
    if (data->texturesCount > 0) {
        for (uint32_t m = 0; m < data->materialsCount; ++m) {
            const AnoGltfMaterial* mat = &data->materials[m];
            PbrFeatureFlags matFeatures = gltf_identify_material_features(mat);
            PbrFeatureFlags supportedFeatures = matFeatures & activeFeatures;
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Material %u (%.*s): required features = 0x%08X, supported = 0x%08X",
                   m, mat->name.length ? static_cast<int>(mat->name.length) : 7,
                   mat->name.length ? mat->name.data : "unnamed", matFeatures, supportedFeatures);

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
    for (uint32_t i = 0; i < data->imagesCount; ++i) {
        const AnoGltfImage* img = &data->images[i];
        if (img->uri.length == 0)
            continue;
        if (imageUsage[i] == TEXTURE_USE_NONE) {
            ano_debug_log(ANO_INFO, "[GLTF DEBUG] Skipping image %u: %.*s (not needed or unsupported by pipeline)",
                          i, static_cast<int>(img->uri.length), img->uri.data);
            continue;
        }
        // Halted: skip before acquisition.
        if (haltLoad) {
            skipped++;
            continue;
        }
        ano_debug_log(ANO_INFO, "[GLTF DEBUG] Loading image %u: %.*s",
                      i, static_cast<int>(img->uri.length), img->uri.data);
        // Resolve image URI against the glTF file's directory, then percent-decode the tail.
        char texPath[1024];
        if (!gltf_image_path(texPath, fileName, img->uri)) {
            ano_log(ANO_WARN, "Image URI too long, skipping: %.*s",
                    static_cast<int>(img->uri.length), img->uri.data);
            continue;
        }

        TexturePackage pkg = {0};
        // Exhaustive over AnoTextureResultCode.
        switch (createTextureImage(ctx, textureCmd, &pkg, texPath, false, imageUsage[i], true).code) {
        case ANO_TEXTURE_BUILT:
            break;
        case ANO_TEXTURE_SOURCE:
            ano_log(ANO_WARN, "Unusable image source, skipping: %.*s",
                    static_cast<int>(img->uri.length), img->uri.data);
            continue;
        case ANO_TEXTURE_INVALID:
            ano_log(ANO_ERROR, "Image request outside the constructor's contract: %.*s",
                    static_cast<int>(img->uri.length), img->uri.data);
            continue;
        case ANO_TEXTURE_DEVICE:
            ano_log(ANO_ERROR, "Device or texture arena refused %.*s; halting texture construction.",
                    static_cast<int>(img->uri.length), img->uri.data);
            haltLoad = true;
            continue;
        }

        if (pkg.staging) stagingBuffers[stagingCount++] = pkg.staging; // batch-owned

        if (!ano_vk_register_texture(&rendererState.primitives, ano_texture_record(&pkg))) {
            ano_log(ANO_ERROR, "Texture registry refused %.*s; halting texture construction.",
                    static_cast<int>(img->uri.length), img->uri.data);
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
            ano_log(ANO_WARN, "No bindless slot for image %.*s; halting texture construction.",
                    static_cast<int>(img->uri.length), img->uri.data);
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
    for (uint32_t m = 0; m < data->meshesCount; ++m) {
        const AnoGltfMesh* gltfMesh = &data->meshes[m];
        ModelMesh* outMesh = &asset->meshes[m];

        for (uint32_t p = 0; p < gltfMesh->primitives.count; ++p) {
            const AnoGltfPrimitive* prim = &gltfMesh->primitives.data[p];

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

                if (ano_gltf_has_index(prim->material) && prim->material.value < data->materialsCount) {
                    const AnoGltfMaterial* material = &data->materials[prim->material.value];
                    const AnoGltfMaterialExtensionsKnown& ext = material->extensions.known;
                    PbrFeatureFlags matFeatures = gltf_identify_material_features(material);
                    PbrFeatureFlags supportedFeatures = matFeatures & activeFeatures;

                    matData.features = supportedFeatures;
                    gltf_project_material_textures(&matData, data, colorIndex, dataIndex,
                                                   material, supportedFeatures);

                    // 1. pbrMetallicRoughness
                    if (material->pbrMetallicRoughness.present) {
                        const AnoGltfPbrMetallicRoughness& pbr = material->pbrMetallicRoughness.value;
                        for (int i = 0; i < 4; i++) {
                            matData.baseColorFactor[i] = pbr.baseColorFactor.values[i];
                        }
                        matData.metallicFactor = pbr.metallicFactor;
                        matData.roughnessFactor = pbr.roughnessFactor;
                    }

                    // 2. Core properties
                    if ((supportedFeatures & PBR_FEATURE_NORMAL_TEXTURE) &&
                        matData.normalTexture != ANO_BINDLESS_NONE)
                        matData.normalScale = material->normalTexture.scale;

                    if ((supportedFeatures & PBR_FEATURE_OCCLUSION_TEXTURE) &&
                        matData.occlusionTexture != ANO_BINDLESS_NONE)
                        matData.occlusionStrength = material->occlusionTexture.strength;

                    for (int i = 0; i < 3; i++) {
                        matData.emissiveFactor[i] = material->emissiveFactor.values[i];
                    }
                    matData.emissiveFactor[3] = 1.0f; // Padding

                    if (material->alphaMode == AnoGltfAlphaMode::opaque) {
                        matData.alphaMode = 0;
                    } else if (material->alphaMode == AnoGltfAlphaMode::mask) {
                        matData.alphaMode = 1;
                        matData.alphaCutoff = material->alphaCutoff;
                    } else if (material->alphaMode == AnoGltfAlphaMode::blend) {
                        matData.alphaMode = 2;
                    }

                    matData.doubleSided = material->doubleSided ? 1 : 0;

                    // 3. Clearcoat
                    if (supportedFeatures & PBR_FEATURE_CLEARCOAT) {
                        matData.clearcoatFactor = ext.KHR_materials_clearcoat.value.clearcoatFactor;
                        matData.clearcoatRoughnessFactor = ext.KHR_materials_clearcoat.value.clearcoatRoughnessFactor;
                    }

                    // 4. Transmission
                    if (supportedFeatures & PBR_FEATURE_TRANSMISSION) {
                        matData.transmissionFactor = ext.KHR_materials_transmission.value.transmissionFactor;
                    }

                    // 5. Volume
                    if (supportedFeatures & PBR_FEATURE_VOLUME) {
                        const AnoGltfVolume& volume = ext.KHR_materials_volume.value;
                        matData.thicknessFactor = volume.thicknessFactor;
                        matData.attenuationDistance = volume.attenuationDistance;
                        for (int i = 0; i < 3; i++) {
                            matData.attenuationColor[i] = volume.attenuationColor.values[i];
                        }
                        matData.attenuationColor[3] = 1.0f; // Padding
                    }

                    // 6. IOR
                    if (supportedFeatures & PBR_FEATURE_IOR) {
                        matData.ior = ext.KHR_materials_ior.value.ior;
                    }

                    // 7. Specular
                    if (supportedFeatures & PBR_FEATURE_SPECULAR) {
                        const AnoGltfSpecular& specular = ext.KHR_materials_specular.value;
                        matData.specularFactor = specular.specularFactor;
                        for (int i = 0; i < 3; i++) {
                            matData.specularColorFactor[i] = specular.specularColorFactor.values[i];
                        }
                        matData.specularColorFactor[3] = 1.0f; // Padding
                    }

                    // 8. Sheen
                    if (supportedFeatures & PBR_FEATURE_SHEEN) {
                        const AnoGltfSheen& sheen = ext.KHR_materials_sheen.value;
                        matData.sheenRoughnessFactor = sheen.sheenRoughnessFactor;
                        for (int i = 0; i < 3; i++) {
                            matData.sheenColorFactor[i] = sheen.sheenColorFactor.values[i];
                        }
                        matData.sheenColorFactor[3] = 1.0f; // Padding
                    }

                    // 9. Iridescence
                    if (supportedFeatures & PBR_FEATURE_IRIDESCENCE) {
                        const AnoGltfIridescence& iridescence = ext.KHR_materials_iridescence.value;
                        matData.iridescenceFactor = iridescence.iridescenceFactor;
                        matData.iridescenceIor = iridescence.iridescenceIor;
                        matData.iridescenceThicknessMinimum = iridescence.iridescenceThicknessMinimum;
                        matData.iridescenceThicknessMaximum = iridescence.iridescenceThicknessMaximum;
                    }

                    // 10. Anisotropy
                    if (supportedFeatures & PBR_FEATURE_ANISOTROPY) {
                        matData.anisotropyStrength = ext.KHR_materials_anisotropy.value.anisotropyStrength;
                        matData.anisotropyRotation = ext.KHR_materials_anisotropy.value.anisotropyRotation;
                    }

                    // 11. Dispersion
                    if (supportedFeatures & PBR_FEATURE_DISPERSION) {
                        matData.dispersion = ext.KHR_materials_dispersion.value.dispersion;
                    }

                    // 12. Diffuse Transmission
                    if (supportedFeatures & PBR_FEATURE_DIFFUSE_TRANSMISSION) {
                        const AnoGltfDiffuseTransmission& transmission = ext.KHR_materials_diffuse_transmission.value;
                        matData.diffuseTransmissionFactor = transmission.diffuseTransmissionFactor;
                        for (int i = 0; i < 3; i++) {
                            matData.diffuseTransmissionColorFactor[i] = transmission.diffuseTransmissionColorFactor.values[i];
                        }
                        matData.diffuseTransmissionColorFactor[3] = 1.0f; // Padding
                    }

                    // 13. Emissive Strength
                    if (supportedFeatures & PBR_FEATURE_EMISSIVE_STRENGTH) {
                        matData.emissiveStrength = ext.KHR_materials_emissive_strength.value.emissiveStrength;
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
    asset->nodeCount = data->nodesCount;
    asset->nodes = nodePool;
    size_t childUsed = 0; // running sub-slice offset into childPool

    for (uint32_t n = 0; n < data->nodesCount; ++n) {
        const AnoGltfNode* gltfNode = &data->nodes[n];
        ModelNode* outNode = &asset->nodes[n];

        if (gltfNode->name.length) {
            const size_t copy = gltfNode->name.length < 63u ? gltfNode->name.length : 63u;
            memcpy(outNode->name, gltfNode->name.data, copy);
        }

        // Extract local transform
        float matrix[16];
        ano_gltf_node_transform_local(gltfNode, matrix);
        float* destMat = (float*)&outNode->localTransform;
        for (int i = 0; i < 16; i++) destMat[i] = matrix[i];

        outNode->meshIndex = ano_gltf_has_index(gltfNode->mesh)
            ? static_cast<int32_t>(gltfNode->mesh.value) : -1;
        outNode->parentIndex = ano_gltf_has_index(gltfNode->parent)
            ? static_cast<int32_t>(gltfNode->parent.value) : -1;

        outNode->childCount = gltfNode->children.count;
        if (outNode->childCount > 0) {
            outNode->childIndices = childPool + childUsed;
            childUsed += outNode->childCount;
            for (uint32_t c = 0; c < outNode->childCount; ++c) {
                outNode->childIndices[c] = gltfNode->children.data[c].value;
            }
        }
    }

    // Store root nodes (all parentless nodes, counted by asset_block_size)
    asset->rootNodeCount = rootTotal;
    if (rootTotal > 0) {
        asset->rootNodes = rootPool;
        uint32_t rIdx = 0;
        for (uint32_t n = 0; n < data->nodesCount; ++n) {
            if (!ano_gltf_has_index(data->nodes[n].parent)) {
                asset->rootNodes[rIdx++] = n;
            }
        }
    }

    ano_gltf_free(data);
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

static PbrFeatureFlags gltf_identify_material_features(const AnoGltfMaterial* material) {
    if (!material) {
        return PBR_FEATURE_NONE;
    }

    PbrFeatureFlags features = gltf_material_texture_features(material);

    // 1. pbrMetallicRoughness
    if (material->pbrMetallicRoughness.present) {
        features |= PBR_FEATURE_BASE_COLOR_FACTOR;
        features |= PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR;
    }

    // 2. Core properties
    if (material->emissiveFactor.values[0] > 0.0f || material->emissiveFactor.values[1] > 0.0f || material->emissiveFactor.values[2] > 0.0f) {
        features |= PBR_FEATURE_EMISSIVE_FACTOR;
    }

    // Alpha modes
    if (material->alphaMode == AnoGltfAlphaMode::opaque) {
        features |= PBR_FEATURE_ALPHA_MODE_OPAQUE;
    } else if (material->alphaMode == AnoGltfAlphaMode::mask) {
        features |= PBR_FEATURE_ALPHA_MODE_MASK;
    } else if (material->alphaMode == AnoGltfAlphaMode::blend) {
        features |= PBR_FEATURE_ALPHA_MODE_BLEND;
    }

    if (material->doubleSided) {
        features |= PBR_FEATURE_DOUBLE_SIDED;
    }

    // Extensions
    const AnoGltfMaterialExtensionsKnown& ext = material->extensions.known;
    if (ext.KHR_materials_clearcoat.present) {
        features |= PBR_FEATURE_CLEARCOAT;
    }
    if (ext.KHR_materials_transmission.present) {
        features |= PBR_FEATURE_TRANSMISSION;
    }
    if (ext.KHR_materials_volume.present) {
        features |= PBR_FEATURE_VOLUME;
    }
    if (ext.KHR_materials_ior.present) {
        features |= PBR_FEATURE_IOR;
    }
    if (ext.KHR_materials_specular.present) {
        features |= PBR_FEATURE_SPECULAR;
    }
    if (ext.KHR_materials_sheen.present) {
        features |= PBR_FEATURE_SHEEN;
    }
    if (ext.KHR_materials_iridescence.present) {
        features |= PBR_FEATURE_IRIDESCENCE;
    }
    if (ext.KHR_materials_anisotropy.present) {
        features |= PBR_FEATURE_ANISOTROPY;
    }
    if (ext.KHR_materials_dispersion.present) {
        features |= PBR_FEATURE_DISPERSION;
    }
    if (ext.KHR_materials_diffuse_transmission.present) {
        features |= PBR_FEATURE_DIFFUSE_TRANSMISSION;
    }
    if (ext.KHR_materials_emissive_strength.present) {
        features |= PBR_FEATURE_EMISSIVE_STRENGTH;
    }
    if (ext.KHR_materials_pbrSpecularGlossiness.present) {
        features |= PBR_FEATURE_SPECULAR_GLOSSINESS;
    }

    return features;
}
