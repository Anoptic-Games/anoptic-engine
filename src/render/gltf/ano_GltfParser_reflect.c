/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include "ano_GltfParser.h"
#include "cpp/ano_alloc.h"
#include <string.h>
#include <assert.h>
#include <meta>
#include <string_view>
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

enum class GltfMaterialExtensionFeature : uint32_t {
    KHR_materials_pbrSpecularGlossiness = PBR_FEATURE_SPECULAR_GLOSSINESS,
    KHR_materials_clearcoat = PBR_FEATURE_CLEARCOAT,
    KHR_materials_transmission = PBR_FEATURE_TRANSMISSION,
    KHR_materials_ior = PBR_FEATURE_IOR,
    KHR_materials_specular = PBR_FEATURE_SPECULAR,
    KHR_materials_volume = PBR_FEATURE_VOLUME,
    KHR_materials_sheen = PBR_FEATURE_SHEEN,
    KHR_materials_emissive_strength = PBR_FEATURE_EMISSIVE_STRENGTH,
    KHR_materials_iridescence = PBR_FEATURE_IRIDESCENCE,
    KHR_materials_diffuse_transmission = PBR_FEATURE_DIFFUSE_TRANSMISSION,
    KHR_materials_anisotropy = PBR_FEATURE_ANISOTROPY,
    KHR_materials_dispersion = PBR_FEATURE_DISPERSION,
    KHR_materials_unlit = PBR_FEATURE_NONE,
};

consteval PbrFeatureFlags gltf_material_extension_feature(std::string_view name)
{
    static constexpr auto features =
        std::define_static_array(std::meta::enumerators_of(^^GltfMaterialExtensionFeature));
    template for (constexpr auto feature : features) {
        if (name == std::meta::identifier_of(feature))
            return static_cast<PbrFeatureFlags>([:feature:]);
    }
    return UINT32_MAX;
}

template<class Fn>
static void gltf_visit_material_extensions(const AnoGltfMaterialExtensionsKnown& extensions,
                                           Fn&& visit)
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^AnoGltfMaterialExtensionsKnown, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        constexpr PbrFeatureFlags feature =
            gltf_material_extension_feature(std::meta::identifier_of(member));
        static_assert(feature != UINT32_MAX, "every known material extension needs a feature mapping");
        const auto& extension = extensions.*(&[:member:]);
        if (extension.present)
            visit(extension.value, feature);
    }
}

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
    *pos = ano_gltf_find_accessor(data, prim, AnoGltfAttributeType::position, 0);
    *norm = ano_gltf_find_accessor(data, prim, AnoGltfAttributeType::normal, 0);
    *tex = ano_gltf_find_accessor(data, prim, AnoGltfAttributeType::texcoord, 0);
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

template<class T> struct GltfOptionalTraits { static constexpr bool value = false; };
template<class T> struct GltfOptionalTraits<AnoGltfOptional<T>> {
    static constexpr bool value = true;
    using Value = T;
};
template<class T> struct GltfExtensionsTraits { static constexpr bool value = false; };
template<class T> struct GltfExtensionsTraits<AnoGltfExtensions<T>> { static constexpr bool value = true; };

template<class Object, class Fn>
static void gltf_visit_texture_fields(const Object& object, Fn& visit)
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^Object, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        using Field = [:std::meta::type_of(member):];
        const Field& field = object.*(&[:member:]);
        if constexpr (std::is_same_v<Field, AnoGltfTextureInfo>) {
            visit.template operator()<member>(&field);
        } else if constexpr (GltfOptionalTraits<Field>::value) {
            if (field.present)
                gltf_visit_texture_fields(field.value, visit);
        } else if constexpr (GltfExtensionsTraits<Field>::value) {
            gltf_visit_texture_fields(field.known, visit);
        }
    }
}

template<class Fn>
static void gltf_visit_material_textures(const AnoGltfMaterial* material, Fn&& visit)
{
    auto match = [&]<std::meta::info source>(const AnoGltfTextureInfo* info) {
        static constexpr auto destinations = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^MaterialData,
                                                 std::meta::access_context::unchecked()));
        template for (constexpr auto destination : destinations) {
            constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(destination, ^^AnoMaterialTexture));
            if constexpr (!annotations.empty() &&
                          std::meta::identifier_of(destination) == std::meta::identifier_of(source)) {
                constexpr auto spec = std::meta::extract<AnoMaterialTexture>(annotations[0]);
                visit.template operator()<destination>(
                    info, std::integral_constant<AnoMaterialTexture, spec>{});
            }
        }
    };
    gltf_visit_texture_fields(*material, match);
}

consteval bool gltf_material_texture_schema_valid()
{
    bool seen[static_cast<size_t>(AnoGltfTextureSource::count)]{};
    bool propertySeen[static_cast<size_t>(AnoGltfTextureSource::count)][2]{};
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
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoMaterialTextureProperty));
        static_assert(annotations.size() <= 1);
        if constexpr (!annotations.empty()) {
            constexpr auto property =
                std::meta::extract<AnoMaterialTextureProperty>(annotations[0]);
            using Field = [:std::meta::type_of(member):];
            static_assert(std::is_same_v<Field, float>);
            const size_t source = static_cast<size_t>(property.source);
            const size_t kind = static_cast<size_t>(property.kind);
            if (source >= static_cast<size_t>(AnoGltfTextureSource::count) || kind >= 2 ||
                !seen[source] || propertySeen[source][kind])
                return false;
            propertySeen[source][kind] = true;
        }
    }
    return found == static_cast<size_t>(AnoGltfTextureSource::count);
}

static_assert(gltf_material_texture_schema_valid());

static PbrFeatureFlags gltf_material_texture_features(const AnoGltfMaterial* material)
{
    PbrFeatureFlags features = PBR_FEATURE_NONE;
    gltf_visit_material_textures(material, [&]<std::meta::info Destination>(
        const AnoGltfTextureInfo* info, auto encoded) {
        constexpr auto spec = decltype(encoded)::value;
        if constexpr (spec.detectsFeature)
            if (ano_gltf_has_index(info->index)) features |= spec.feature;
    });
    return features;
}

static void gltf_mark_material_textures(const AnoGltfData* data, TextureUsageFlags* imageUsage,
                                        const AnoGltfMaterial* material,
                                        PbrFeatureFlags supportedFeatures)
{
    gltf_visit_material_textures(material, [&]<std::meta::info Destination>(
        const AnoGltfTextureInfo* info, auto encoded) {
        constexpr auto spec = decltype(encoded)::value;
        constexpr TextureUsageFlags usage = spec.domain == AnoMaterialTextureDomain::color
            ? TEXTURE_USE_COLOR : TEXTURE_USE_DATA;
        if (supportedFeatures & spec.feature)
            mark_texture(data, imageUsage, info, usage);
    });
}

static void gltf_project_material_textures(MaterialData* output, const AnoGltfData* data,
                                           const uint32_t* colorIndex, const uint32_t* dataIndex,
                                           const AnoGltfMaterial* material,
                                           PbrFeatureFlags supportedFeatures)
{
    gltf_visit_material_textures(material, [&]<std::meta::info Destination>(
        const AnoGltfTextureInfo* info, auto encoded) {
        constexpr auto spec = decltype(encoded)::value;
        if ((supportedFeatures & spec.feature) && ano_gltf_has_index(info->index)) {
            const uint32_t* slots = spec.domain == AnoMaterialTextureDomain::color
                ? colorIndex : dataIndex;
            const uint32_t slot = gltf_slot(data, slots, info);
            output->*(&[:Destination:]) = slot;
            if (slot == ANO_BINDLESS_NONE)
                return;
            static constexpr auto destinations = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^MaterialData, std::meta::access_context::unchecked()));
            template for (constexpr auto destination : destinations) {
                constexpr auto annotations = std::define_static_array(
                    std::meta::annotations_of_with_type(
                        destination, ^^AnoMaterialTextureProperty));
                if constexpr (!annotations.empty()) {
                    constexpr auto property =
                        std::meta::extract<AnoMaterialTextureProperty>(annotations[0]);
                    if constexpr (property.source == spec.source) {
                        if constexpr (property.kind == AnoMaterialTexturePropertyKind::scale)
                            output->*(&[:destination:]) = info->scale;
                        else
                            output->*(&[:destination:]) = info->strength;
                    }
                }
            }
        }
    });
}

template<class Destination, class Source>
static void gltf_project_value(Destination& destination, const Source& source)
{
    if constexpr (std::is_assignable_v<Destination&, const Source&>)
        destination = source;
    else if constexpr (std::is_same_v<Destination, uint32_t> && std::is_enum_v<Source>)
        destination = static_cast<uint32_t>(source);
}

template<size_t DestinationCount, size_t SourceCount>
static void gltf_project_value(
    float (&destination)[DestinationCount], const AnoGltfFixedArray<float, SourceCount>& source)
{
    static_assert(DestinationCount == SourceCount || DestinationCount == SourceCount + 1);
    memcpy(destination, source.values, SourceCount * sizeof(float));
    if constexpr (DestinationCount > SourceCount)
        destination[SourceCount] = 1.0f;
}

// Copies same-named, compatible glTF fields into MaterialData. Texture handles and alpha mode
// deliberately have incompatible types and remain governed by their stronger projections.
template<class Source>
static void gltf_project_material_values(MaterialData* output, const Source& source)
{
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^MaterialData, std::meta::access_context::unchecked()));
    static constexpr auto sources = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Source, std::meta::access_context::unchecked()));
    template for (constexpr auto destination : destinations) {
        template for (constexpr auto member : sources) {
            if constexpr (std::meta::identifier_of(destination) == std::meta::identifier_of(member))
                gltf_project_value(output->*(&[:destination:]), source.*(&[:member:]));
        }
    }
}

template<class Destination, class Source>
struct GltfMaterialValueCompatible final {
    static constexpr bool value = std::is_assignable_v<Destination&, const Source&> ||
        (std::is_same_v<Destination, uint32_t> && std::is_enum_v<Source>);
};

template<size_t DestinationCount, size_t SourceCount>
struct GltfMaterialValueCompatible<
    float[DestinationCount], AnoGltfFixedArray<float, SourceCount>> final {
    static constexpr bool value =
        DestinationCount == SourceCount || DestinationCount == SourceCount + 1;
};

template<std::meta::info Destination, class Source>
consteval size_t gltf_material_source_matches()
{
    size_t matches = 0;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Source, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::identifier_of(Destination) == std::meta::identifier_of(member)) {
            using DestinationType = [:std::meta::type_of(Destination):];
            using SourceType = [:std::meta::type_of(member):];
            if constexpr (GltfMaterialValueCompatible<DestinationType, SourceType>::value)
                ++matches;
        }
    }
    return matches;
}

template<std::meta::info Destination>
consteval size_t gltf_material_value_matches()
{
    size_t matches = gltf_material_source_matches<Destination, AnoGltfMaterial>() +
        gltf_material_source_matches<Destination, AnoGltfPbrMetallicRoughness>();
    static constexpr auto extensions = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^AnoGltfMaterialExtensionsKnown, std::meta::access_context::unchecked()));
    template for (constexpr auto extension : extensions) {
        using Optional = [:std::meta::type_of(extension):];
        static_assert(GltfOptionalTraits<Optional>::value);
        using Source = typename GltfOptionalTraits<Optional>::Value;
        matches += gltf_material_source_matches<Destination, Source>();
    }
    return matches;
}

consteval bool gltf_material_value_schema_valid()
{
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^MaterialData, std::meta::access_context::unchecked()));
    template for (constexpr auto destination : destinations) {
        constexpr auto name = std::meta::identifier_of(destination);
        constexpr auto textures = std::define_static_array(
            std::meta::annotations_of_with_type(destination, ^^AnoMaterialTexture));
        constexpr auto properties = std::define_static_array(
            std::meta::annotations_of_with_type(destination, ^^AnoMaterialTextureProperty));
        if constexpr (textures.empty() && properties.empty() && name != "features" &&
                      name != "pipelineType" && !name.starts_with("pad")) {
            if (gltf_material_value_matches<destination>() != 1)
                return false;
        }
    }
    return true;
}

static_assert(static_cast<uint8_t>(AnoGltfAlphaMode::opaque) == 0 &&
              static_cast<uint8_t>(AnoGltfAlphaMode::mask) == 1 &&
              static_cast<uint8_t>(AnoGltfAlphaMode::blend) == 2);
static_assert(gltf_material_value_schema_valid(),
              "every material value needs one reflected source or an explicit policy annotation");

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

static void* gltf_allocate(void*, size_t bytes)
{
    return ano::allocate<uint8_t>(bytes);
}

static void gltf_release(void*, void* allocation)
{
    free(allocation);
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
            decoded = ano_gltf_accessor_unpack_indices(
                data, indexAccessor, indices, sizeof(*indices), indexCount) == indexCount;
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
                    gltf_project_material_values(&matData, *material);

                    if (material->pbrMetallicRoughness.present) {
                        const AnoGltfPbrMetallicRoughness& pbr = material->pbrMetallicRoughness.value;
                        gltf_project_material_values(&matData, pbr);
                    }

                    gltf_visit_material_extensions(ext, [&](const auto& extension,
                                                            PbrFeatureFlags feature) {
                        if (supportedFeatures & feature)
                            gltf_project_material_values(&matData, extension);
                    });

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
        memcpy(&outNode->localTransform, matrix, sizeof(matrix));

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
                memcpy(&out[*idx].transform, &worldTransform, sizeof(worldTransform));
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
    if (!material)
        return PBR_FEATURE_NONE;

    PbrFeatureFlags features = gltf_material_texture_features(material);
    if (material->pbrMetallicRoughness.present)
        features |= PBR_FEATURE_BASE_COLOR_FACTOR | PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR;
    if (material->emissiveFactor.values[0] > 0.0f || material->emissiveFactor.values[1] > 0.0f ||
        material->emissiveFactor.values[2] > 0.0f)
        features |= PBR_FEATURE_EMISSIVE_FACTOR;
    static_assert(PBR_FEATURE_ALPHA_MODE_MASK == (PBR_FEATURE_ALPHA_MODE_OPAQUE << 1) &&
                  PBR_FEATURE_ALPHA_MODE_BLEND == (PBR_FEATURE_ALPHA_MODE_OPAQUE << 2));
    features |= PBR_FEATURE_ALPHA_MODE_OPAQUE << static_cast<uint8_t>(material->alphaMode);
    if (material->doubleSided)
        features |= PBR_FEATURE_DOUBLE_SIDED;

    const AnoGltfMaterialExtensionsKnown& ext = material->extensions.known;
    gltf_visit_material_extensions(ext, [&](const auto&, PbrFeatureFlags feature) {
        features |= feature;
    });

    return features;
}
