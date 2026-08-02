/*
 * Anoptic-native glTF 2.0 loader for C+Ultra translation units.
 *
 * LANGUAGE CONTRACT: C++26 with standardized reflection (P2996R13 or newer).
 * The .h extension is intentional. The API is C-shaped, uses C ABI entry points,
 * raw arrays, final plain-data structures, strong indices and no C++ runtime.
 * Define ANOGLTF_IMPLEMENTATION in exactly one first-party .c translation unit.
 *
 * cgltf provenance: parsing and validation behavior is being replaced against
 * cgltf 1.15 as the differential oracle. The bounded JSON tokenizer below is
 * adapted from the jsmn copy shipped by cgltf. Copyright and MIT terms appear
 * at the end of this file and must remain with substantial derived portions.
 */

#pragma once

#if !defined(__cplusplus)
#error "anogltf.h is C+Ultra: compile this .h from a C++26 translation unit"
#endif

#if __cplusplus < 202302L
#error "anogltf.h requires C++26"
#endif

#include <meta>

#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error "anogltf.h requires standardized C++26 reflection (P2996R13 or newer)"
#endif

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

inline constexpr uint32_t ANO_GLTF_NO_INDEX = UINT32_MAX;

struct AnoGltfJsonName final {
    char text[16]{};
    uint8_t length = 0;

    template<size_t Count>
    consteval AnoGltfJsonName(const char (&name)[Count])
    {
        static_assert(Count > 0 && Count <= sizeof(text) + 1);
        length = static_cast<uint8_t>(Count - 1);
        for (size_t i = 0; i < Count - 1; ++i)
            text[i] = name[i];
    }
};

struct AnoGltfEnumInvalid final {};

struct AnoGltfIndexKind final {
    std::meta::info tag;
};

enum class AnoGltfResult : uint8_t {
    success,
    data_too_short,
    unknown_format,
    invalid_json,
    invalid_gltf,
    invalid_options,
    file_not_found,
    io_error,
    out_of_memory,
    limit_exceeded,
    unsupported_required_extension,
};

enum class AnoGltfFileType : uint8_t {
    invalid,
    gltf,
    glb,
};

enum class AnoGltfComponentType : uint16_t {
    invalid [[=AnoGltfEnumInvalid{}]] = 0,
    byte = 5120,
    unsigned_byte = 5121,
    short_ = 5122,
    unsigned_short = 5123,
    unsigned_int = 5125,
    float_ = 5126,
};

enum class AnoGltfAccessorType : uint8_t {
    invalid [[=AnoGltfEnumInvalid{}]],
    scalar [[=AnoGltfJsonName{"SCALAR"}]],
    vec2 [[=AnoGltfJsonName{"VEC2"}]],
    vec3 [[=AnoGltfJsonName{"VEC3"}]],
    vec4 [[=AnoGltfJsonName{"VEC4"}]],
    mat2 [[=AnoGltfJsonName{"MAT2"}]],
    mat3 [[=AnoGltfJsonName{"MAT3"}]],
    mat4 [[=AnoGltfJsonName{"MAT4"}]],
};

enum class AnoGltfBufferViewTarget : uint16_t {
    unspecified [[=AnoGltfEnumInvalid{}]] = 0,
    array_buffer = 34962,
    element_array_buffer = 34963,
};

enum class AnoGltfFilter : uint16_t {
    unspecified [[=AnoGltfEnumInvalid{}]] = 0,
    nearest = 9728,
    linear = 9729,
    nearest_mipmap_nearest = 9984,
    linear_mipmap_nearest = 9985,
    nearest_mipmap_linear = 9986,
    linear_mipmap_linear = 9987,
};

enum class AnoGltfWrap : uint16_t {
    repeat = 10497,
    clamp_to_edge = 33071,
    mirrored_repeat = 33648,
};

enum class AnoGltfPrimitiveMode : uint8_t {
    points = 0,
    lines = 1,
    line_loop = 2,
    line_strip = 3,
    triangles = 4,
    triangle_strip = 5,
    triangle_fan = 6,
};

enum class AnoGltfAlphaMode : uint8_t {
    opaque [[=AnoGltfJsonName{"OPAQUE"}]],
    mask [[=AnoGltfJsonName{"MASK"}]],
    blend [[=AnoGltfJsonName{"BLEND"}]],
};

enum class AnoGltfAttributeType : uint8_t {
    invalid,
    position,
    normal,
    tangent,
    texcoord,
    color,
    joints,
    weights,
    custom,
    translation,
    rotation,
    scale,
};

enum class AnoGltfCameraType : uint8_t {
    invalid [[=AnoGltfEnumInvalid{}]],
    perspective [[=AnoGltfJsonName{"perspective"}]],
    orthographic [[=AnoGltfJsonName{"orthographic"}]],
};

enum class AnoGltfLightType : uint8_t {
    invalid [[=AnoGltfEnumInvalid{}]],
    directional [[=AnoGltfJsonName{"directional"}]],
    point [[=AnoGltfJsonName{"point"}]],
    spot [[=AnoGltfJsonName{"spot"}]],
};

enum class AnoGltfInterpolation : uint8_t {
    linear [[=AnoGltfJsonName{"LINEAR"}]],
    step [[=AnoGltfJsonName{"STEP"}]],
    cubic_spline [[=AnoGltfJsonName{"CUBICSPLINE"}]],
};

enum class AnoGltfAnimationPath : uint8_t {
    invalid [[=AnoGltfEnumInvalid{}]],
    translation [[=AnoGltfJsonName{"translation"}]],
    rotation [[=AnoGltfJsonName{"rotation"}]],
    scale [[=AnoGltfJsonName{"scale"}]],
    weights [[=AnoGltfJsonName{"weights"}]],
};

enum class AnoGltfMeshoptMode : uint8_t {
    invalid [[=AnoGltfEnumInvalid{}]],
    attributes [[=AnoGltfJsonName{"ATTRIBUTES"}]],
    triangles [[=AnoGltfJsonName{"TRIANGLES"}]],
    indices [[=AnoGltfJsonName{"INDICES"}]],
};

enum class AnoGltfMeshoptFilter : uint8_t {
    none [[=AnoGltfJsonName{"NONE"}]],
    octahedral [[=AnoGltfJsonName{"OCTAHEDRAL"}]],
    quaternion [[=AnoGltfJsonName{"QUATERNION"}]],
    exponential [[=AnoGltfJsonName{"EXPONENTIAL"}]],
    color [[=AnoGltfJsonName{"COLOR"}]],
};

struct AnoGltfBufferTag final {};
struct AnoGltfBufferViewTag final {};
struct AnoGltfAccessorTag final {};
struct AnoGltfImageTag final {};
struct AnoGltfTextureTag final {};
struct AnoGltfSamplerTag final {};
struct AnoGltfMaterialTag final {};
struct AnoGltfMeshTag final {};
struct AnoGltfSkinTag final {};
struct AnoGltfCameraTag final {};
struct AnoGltfLightTag final {};
struct AnoGltfNodeTag final {};
struct AnoGltfSceneTag final {};
struct AnoGltfAnimationTag final {};
struct AnoGltfAnimationSamplerTag final {};
struct AnoGltfAnimationChannelTag final {};
struct AnoGltfVariantTag final {};

template<class Tag>
struct AnoGltfIndex final {
    uint32_t value = ANO_GLTF_NO_INDEX;
};

using AnoGltfBufferIndex = AnoGltfIndex<AnoGltfBufferTag>;
using AnoGltfBufferViewIndex = AnoGltfIndex<AnoGltfBufferViewTag>;
using AnoGltfAccessorIndex = AnoGltfIndex<AnoGltfAccessorTag>;
using AnoGltfImageIndex = AnoGltfIndex<AnoGltfImageTag>;
using AnoGltfTextureIndex = AnoGltfIndex<AnoGltfTextureTag>;
using AnoGltfSamplerIndex = AnoGltfIndex<AnoGltfSamplerTag>;
using AnoGltfMaterialIndex = AnoGltfIndex<AnoGltfMaterialTag>;
using AnoGltfMeshIndex = AnoGltfIndex<AnoGltfMeshTag>;
using AnoGltfSkinIndex = AnoGltfIndex<AnoGltfSkinTag>;
using AnoGltfCameraIndex = AnoGltfIndex<AnoGltfCameraTag>;
using AnoGltfLightIndex = AnoGltfIndex<AnoGltfLightTag>;
using AnoGltfNodeIndex = AnoGltfIndex<AnoGltfNodeTag>;
using AnoGltfSceneIndex = AnoGltfIndex<AnoGltfSceneTag>;
using AnoGltfAnimationIndex = AnoGltfIndex<AnoGltfAnimationTag>;
using AnoGltfAnimationSamplerIndex = AnoGltfIndex<AnoGltfAnimationSamplerTag>;
using AnoGltfAnimationChannelIndex = AnoGltfIndex<AnoGltfAnimationChannelTag>;
using AnoGltfVariantIndex = AnoGltfIndex<AnoGltfVariantTag>;

template<class Tag>
constexpr bool ano_gltf_has_index(AnoGltfIndex<Tag> index)
{
    return index.value != ANO_GLTF_NO_INDEX;
}

struct AnoGltfString final {
    const char* data = nullptr;
    uint32_t length = 0;
};

template<class T>
struct AnoGltfArray final {
    T* data = nullptr;
    uint32_t count = 0;
};

template<class T>
struct AnoGltfOptional final {
    bool present = false;
    T value{};
};

template<class T, size_t Count>
struct AnoGltfFixedArray final {
    T values[Count]{};
};

using AnoGltfFloat2 = AnoGltfFixedArray<float, 2>;
using AnoGltfFloat3 = AnoGltfFixedArray<float, 3>;
using AnoGltfFloat4 = AnoGltfFixedArray<float, 4>;
using AnoGltfFloat16 = AnoGltfFixedArray<float, 16>;

struct AnoGltfNumberArray final {
    double values[16]{};
    uint8_t count = 0;
};

struct AnoGltfRequired final {};
struct AnoGltfIgnore final {};

struct AnoGltfJson final {
    const char* data = nullptr;
    uint32_t length = 0;
};

struct AnoGltfExtras final {
    AnoGltfJson json;
    AnoGltfArray<AnoGltfString> targetNames;
};

struct AnoGltfExtension final {
    AnoGltfString name;
    AnoGltfJson value;
};

struct AnoGltfNoExtensions final {};
struct AnoGltfEmptyExtension final {};

template<class Known = AnoGltfNoExtensions>
struct AnoGltfExtensions final {
    Known known;
    AnoGltfArray<AnoGltfExtension> all;
};

struct AnoGltfAttribute final {
    AnoGltfString name;
    AnoGltfAttributeType type = AnoGltfAttributeType::invalid;
    int32_t set = 0;
    AnoGltfAccessorIndex accessor;
};

struct AnoGltfAttributeMap final {
    AnoGltfArray<AnoGltfAttribute> values;
};

struct AnoGltfDracoAttribute final {
    AnoGltfString name;
    AnoGltfAttributeType type = AnoGltfAttributeType::invalid;
    int32_t set = 0;
    uint32_t uniqueId = 0;
};

struct AnoGltfDracoAttributeMap final {
    AnoGltfArray<AnoGltfDracoAttribute> values;
};

struct AnoGltfMeshoptCompression final {
    [[=AnoGltfRequired{}]] AnoGltfBufferIndex buffer;
    uint64_t byteOffset = 0;
    [[=AnoGltfRequired{}]] uint64_t byteLength = 0;
    [[=AnoGltfRequired{}]] uint32_t byteStride = 0;
    [[=AnoGltfRequired{}]] uint64_t count = 0;
    [[=AnoGltfRequired{}]] AnoGltfMeshoptMode mode = AnoGltfMeshoptMode::invalid;
    AnoGltfMeshoptFilter filter = AnoGltfMeshoptFilter::none;
};

struct AnoGltfBufferViewExtensionsKnown final {
    AnoGltfOptional<AnoGltfMeshoptCompression> EXT_meshopt_compression;
    AnoGltfOptional<AnoGltfMeshoptCompression> KHR_meshopt_compression;
};

struct AnoGltfAsset final {
    AnoGltfString copyright;
    AnoGltfString generator;
    [[=AnoGltfRequired{}]] AnoGltfString version;
    AnoGltfString minVersion;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfMeshoptFallback final {
    bool fallback = false;
};

struct AnoGltfBufferExtensionsKnown final {
    AnoGltfOptional<AnoGltfMeshoptFallback> EXT_meshopt_compression;
    AnoGltfOptional<AnoGltfMeshoptFallback> KHR_meshopt_compression;
};

struct [[=AnoGltfIndexKind{^^AnoGltfBufferTag}]] AnoGltfBuffer final {
    AnoGltfString name;
    [[=AnoGltfRequired{}]] uint64_t byteLength = 0;
    AnoGltfString uri;
    [[=AnoGltfIgnore{}]] const uint8_t* data = nullptr;
    [[=AnoGltfIgnore{}]] uint64_t dataSize = 0;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfBufferExtensionsKnown> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfBufferViewTag}]] AnoGltfBufferView final {
    AnoGltfString name;
    [[=AnoGltfRequired{}]] AnoGltfBufferIndex buffer;
    uint64_t byteOffset = 0;
    [[=AnoGltfRequired{}]] uint64_t byteLength = 0;
    uint32_t byteStride = 0;
    AnoGltfBufferViewTarget target = AnoGltfBufferViewTarget::unspecified;
    [[=AnoGltfIgnore{}]] const uint8_t* decodedData = nullptr;
    [[=AnoGltfIgnore{}]] uint64_t decodedDataSize = 0;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfBufferViewExtensionsKnown> extensions;
};

struct AnoGltfAccessorSparseIndices final {
    [[=AnoGltfRequired{}]] AnoGltfBufferViewIndex bufferView;
    uint64_t byteOffset = 0;
    [[=AnoGltfRequired{}]] AnoGltfComponentType componentType = AnoGltfComponentType::invalid;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfAccessorSparseValues final {
    [[=AnoGltfRequired{}]] AnoGltfBufferViewIndex bufferView;
    uint64_t byteOffset = 0;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfAccessorSparse final {
    [[=AnoGltfRequired{}]] uint64_t count = 0;
    [[=AnoGltfRequired{}]] AnoGltfAccessorSparseIndices indices;
    [[=AnoGltfRequired{}]] AnoGltfAccessorSparseValues values;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfAccessorTag}]] AnoGltfAccessor final {
    AnoGltfString name;
    AnoGltfBufferViewIndex bufferView;
    uint64_t byteOffset = 0;
    [[=AnoGltfRequired{}]] AnoGltfComponentType componentType = AnoGltfComponentType::invalid;
    bool normalized = false;
    [[=AnoGltfRequired{}]] uint64_t count = 0;
    [[=AnoGltfRequired{}]] AnoGltfAccessorType type = AnoGltfAccessorType::invalid;
    AnoGltfNumberArray min;
    AnoGltfNumberArray max;
    AnoGltfAccessorSparse sparse;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfImageTag}]] AnoGltfImage final {
    AnoGltfString name;
    AnoGltfString uri;
    AnoGltfBufferViewIndex bufferView;
    AnoGltfString mimeType;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfSamplerTag}]] AnoGltfSampler final {
    AnoGltfString name;
    AnoGltfFilter magFilter = AnoGltfFilter::unspecified;
    AnoGltfFilter minFilter = AnoGltfFilter::unspecified;
    AnoGltfWrap wrapS = AnoGltfWrap::repeat;
    AnoGltfWrap wrapT = AnoGltfWrap::repeat;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfTextureSourceExtension final {
    [[=AnoGltfRequired{}]] AnoGltfImageIndex source;
};

struct AnoGltfTextureExtensionsKnown final {
    AnoGltfOptional<AnoGltfTextureSourceExtension> KHR_texture_basisu;
    AnoGltfOptional<AnoGltfTextureSourceExtension> EXT_texture_webp;
};

struct AnoGltfTextureTransform final {
    AnoGltfFloat2 offset{};
    float rotation = 0.0f;
    AnoGltfFloat2 scale = {{1.0f, 1.0f}};
    AnoGltfOptional<uint32_t> texCoord;
};

struct AnoGltfTextureInfoExtensionsKnown final {
    AnoGltfOptional<AnoGltfTextureTransform> KHR_texture_transform;
};

struct AnoGltfTextureInfo final {
    [[=AnoGltfRequired{}]] AnoGltfTextureIndex index;
    uint32_t texCoord = 0;
    float scale = 1.0f;
    float strength = 1.0f;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfTextureInfoExtensionsKnown> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfTextureTag}]] AnoGltfTexture final {
    AnoGltfString name;
    AnoGltfSamplerIndex sampler;
    AnoGltfImageIndex source;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfTextureExtensionsKnown> extensions;
};

struct AnoGltfPbrMetallicRoughness final {
    AnoGltfFloat4 baseColorFactor = {{1.0f, 1.0f, 1.0f, 1.0f}};
    AnoGltfTextureInfo baseColorTexture;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    AnoGltfTextureInfo metallicRoughnessTexture;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfPbrSpecularGlossiness final {
    AnoGltfFloat4 diffuseFactor = {{1.0f, 1.0f, 1.0f, 1.0f}};
    AnoGltfTextureInfo diffuseTexture;
    AnoGltfFloat3 specularFactor = {{1.0f, 1.0f, 1.0f}};
    float glossinessFactor = 1.0f;
    AnoGltfTextureInfo specularGlossinessTexture;
};

struct AnoGltfClearcoat final {
    float clearcoatFactor = 0.0f;
    AnoGltfTextureInfo clearcoatTexture;
    float clearcoatRoughnessFactor = 0.0f;
    AnoGltfTextureInfo clearcoatRoughnessTexture;
    AnoGltfTextureInfo clearcoatNormalTexture;
};

struct AnoGltfTransmission final {
    float transmissionFactor = 0.0f;
    AnoGltfTextureInfo transmissionTexture;
};

struct AnoGltfIor final {
    float ior = 1.5f;
};

struct AnoGltfSpecular final {
    float specularFactor = 1.0f;
    AnoGltfTextureInfo specularTexture;
    AnoGltfFloat3 specularColorFactor = {{1.0f, 1.0f, 1.0f}};
    AnoGltfTextureInfo specularColorTexture;
};

struct AnoGltfVolume final {
    float thicknessFactor = 0.0f;
    AnoGltfTextureInfo thicknessTexture;
    float attenuationDistance = FLT_MAX;
    AnoGltfFloat3 attenuationColor = {{1.0f, 1.0f, 1.0f}};
};

struct AnoGltfSheen final {
    AnoGltfFloat3 sheenColorFactor{};
    AnoGltfTextureInfo sheenColorTexture;
    float sheenRoughnessFactor = 0.0f;
    AnoGltfTextureInfo sheenRoughnessTexture;
};

struct AnoGltfEmissiveStrength final {
    float emissiveStrength = 1.0f;
};

struct AnoGltfIridescence final {
    float iridescenceFactor = 0.0f;
    AnoGltfTextureInfo iridescenceTexture;
    float iridescenceIor = 1.3f;
    float iridescenceThicknessMinimum = 100.0f;
    float iridescenceThicknessMaximum = 400.0f;
    AnoGltfTextureInfo iridescenceThicknessTexture;
};

struct AnoGltfDiffuseTransmission final {
    float diffuseTransmissionFactor = 0.0f;
    AnoGltfTextureInfo diffuseTransmissionTexture;
    AnoGltfFloat3 diffuseTransmissionColorFactor = {{1.0f, 1.0f, 1.0f}};
    AnoGltfTextureInfo diffuseTransmissionColorTexture;
};

struct AnoGltfAnisotropy final {
    float anisotropyStrength = 0.0f;
    float anisotropyRotation = 0.0f;
    AnoGltfTextureInfo anisotropyTexture;
};

struct AnoGltfDispersion final {
    float dispersion = 0.0f;
};

struct AnoGltfMaterialExtensionsKnown final {
    AnoGltfOptional<AnoGltfPbrSpecularGlossiness> KHR_materials_pbrSpecularGlossiness;
    AnoGltfOptional<AnoGltfClearcoat> KHR_materials_clearcoat;
    AnoGltfOptional<AnoGltfTransmission> KHR_materials_transmission;
    AnoGltfOptional<AnoGltfIor> KHR_materials_ior;
    AnoGltfOptional<AnoGltfSpecular> KHR_materials_specular;
    AnoGltfOptional<AnoGltfVolume> KHR_materials_volume;
    AnoGltfOptional<AnoGltfSheen> KHR_materials_sheen;
    AnoGltfOptional<AnoGltfEmissiveStrength> KHR_materials_emissive_strength;
    AnoGltfOptional<AnoGltfIridescence> KHR_materials_iridescence;
    AnoGltfOptional<AnoGltfDiffuseTransmission> KHR_materials_diffuse_transmission;
    AnoGltfOptional<AnoGltfAnisotropy> KHR_materials_anisotropy;
    AnoGltfOptional<AnoGltfDispersion> KHR_materials_dispersion;
    AnoGltfOptional<AnoGltfEmptyExtension> KHR_materials_unlit;
};

struct [[=AnoGltfIndexKind{^^AnoGltfMaterialTag}]] AnoGltfMaterial final {
    AnoGltfString name;
    AnoGltfOptional<AnoGltfPbrMetallicRoughness> pbrMetallicRoughness;
    AnoGltfTextureInfo normalTexture;
    AnoGltfTextureInfo occlusionTexture;
    AnoGltfTextureInfo emissiveTexture;
    AnoGltfFloat3 emissiveFactor{};
    AnoGltfAlphaMode alphaMode = AnoGltfAlphaMode::opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfMaterialExtensionsKnown> extensions;
};

struct AnoGltfMorphTarget final {
    AnoGltfAttributeMap attributes;
};

struct AnoGltfDracoMeshCompression final {
    [[=AnoGltfRequired{}]] AnoGltfBufferViewIndex bufferView;
    [[=AnoGltfRequired{}]] AnoGltfDracoAttributeMap attributes;
};

struct AnoGltfMaterialVariantMapping final {
    [[=AnoGltfRequired{}]] AnoGltfMaterialIndex material;
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfVariantIndex> variants;
};

struct AnoGltfMaterialVariantsMappings final {
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfMaterialVariantMapping> mappings;
};

struct AnoGltfPrimitiveExtensionsKnown final {
    AnoGltfOptional<AnoGltfDracoMeshCompression> KHR_draco_mesh_compression;
    AnoGltfOptional<AnoGltfMaterialVariantsMappings> KHR_materials_variants;
};

struct AnoGltfPrimitive final {
    [[=AnoGltfRequired{}]] AnoGltfAttributeMap attributes;
    AnoGltfAccessorIndex indices;
    AnoGltfMaterialIndex material;
    AnoGltfPrimitiveMode mode = AnoGltfPrimitiveMode::triangles;
    AnoGltfArray<AnoGltfMorphTarget> targets;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfPrimitiveExtensionsKnown> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfMeshTag}]] AnoGltfMesh final {
    AnoGltfString name;
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfPrimitive> primitives;
    AnoGltfArray<float> weights;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfSkinTag}]] AnoGltfSkin final {
    AnoGltfString name;
    AnoGltfAccessorIndex inverseBindMatrices;
    AnoGltfNodeIndex skeleton;
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfNodeIndex> joints;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfCameraPerspective final {
    AnoGltfOptional<float> aspectRatio;
    [[=AnoGltfRequired{}]] float yfov = 0.0f;
    AnoGltfOptional<float> zfar;
    [[=AnoGltfRequired{}]] float znear = 0.0f;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfCameraOrthographic final {
    [[=AnoGltfRequired{}]] float xmag = 0.0f;
    [[=AnoGltfRequired{}]] float ymag = 0.0f;
    [[=AnoGltfRequired{}]] float zfar = 0.0f;
    [[=AnoGltfRequired{}]] float znear = 0.0f;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfCameraTag}]] AnoGltfCamera final {
    AnoGltfString name;
    [[=AnoGltfRequired{}]] AnoGltfCameraType type = AnoGltfCameraType::invalid;
    AnoGltfCameraPerspective perspective;
    AnoGltfCameraOrthographic orthographic;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfLightSpot final {
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7853981633974483f;
};

struct [[=AnoGltfIndexKind{^^AnoGltfLightTag}]] AnoGltfLight final {
    AnoGltfString name;
    AnoGltfFloat3 color = {{1.0f, 1.0f, 1.0f}};
    float intensity = 1.0f;
    [[=AnoGltfRequired{}]] AnoGltfLightType type = AnoGltfLightType::invalid;
    AnoGltfOptional<float> range;
    AnoGltfLightSpot spot;
    AnoGltfExtras extras;
};

struct AnoGltfMeshGpuInstancing final {
    [[=AnoGltfRequired{}]] AnoGltfAttributeMap attributes;
};

struct AnoGltfNodeLight final {
    [[=AnoGltfRequired{}]] AnoGltfLightIndex light;
};

struct AnoGltfNodeExtensionsKnown final {
    AnoGltfOptional<AnoGltfMeshGpuInstancing> EXT_mesh_gpu_instancing;
    AnoGltfOptional<AnoGltfNodeLight> KHR_lights_punctual;
};

struct [[=AnoGltfIndexKind{^^AnoGltfNodeTag}]] AnoGltfNode final {
    AnoGltfString name;
    AnoGltfCameraIndex camera;
    AnoGltfArray<AnoGltfNodeIndex> children;
    AnoGltfSkinIndex skin;
    AnoGltfOptional<AnoGltfFloat16> matrix;
    AnoGltfMeshIndex mesh;
    AnoGltfOptional<AnoGltfFloat4> rotation;
    AnoGltfOptional<AnoGltfFloat3> scale;
    AnoGltfOptional<AnoGltfFloat3> translation;
    AnoGltfArray<float> weights;
    [[=AnoGltfIgnore{}]] AnoGltfNodeIndex parent;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfNodeExtensionsKnown> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfSceneTag}]] AnoGltfScene final {
    AnoGltfString name;
    AnoGltfArray<AnoGltfNodeIndex> nodes;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfAnimationSamplerTag}]] AnoGltfAnimationSampler final {
    [[=AnoGltfRequired{}]] AnoGltfAccessorIndex input;
    [[=AnoGltfRequired{}]] AnoGltfAccessorIndex output;
    AnoGltfInterpolation interpolation = AnoGltfInterpolation::linear;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct AnoGltfAnimationTarget final {
    AnoGltfNodeIndex node;
    [[=AnoGltfRequired{}]] AnoGltfAnimationPath path = AnoGltfAnimationPath::invalid;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfAnimationChannelTag}]] AnoGltfAnimationChannel final {
    [[=AnoGltfRequired{}]] AnoGltfAnimationSamplerIndex sampler;
    [[=AnoGltfRequired{}]] AnoGltfAnimationTarget target;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfAnimationTag}]] AnoGltfAnimation final {
    AnoGltfString name;
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfAnimationSampler> samplers;
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfAnimationChannel> channels;
    AnoGltfExtras extras;
    AnoGltfExtensions<> extensions;
};

struct [[=AnoGltfIndexKind{^^AnoGltfVariantTag}]] AnoGltfMaterialVariant final {
    [[=AnoGltfRequired{}]] AnoGltfString name;
    AnoGltfExtras extras;
};

struct AnoGltfRootLights final {
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfLight> lights;
};

struct AnoGltfRootVariants final {
    [[=AnoGltfRequired{}]] AnoGltfArray<AnoGltfMaterialVariant> variants;
};

struct AnoGltfRootExtensionsKnown final {
    AnoGltfOptional<AnoGltfRootLights> KHR_lights_punctual;
    AnoGltfOptional<AnoGltfRootVariants> KHR_materials_variants;
};

using AnoGltfAllocateFn = void* (*)(void* user, size_t bytes);
using AnoGltfFreeFn = void (*)(void* user, void* allocation);
using AnoGltfFileSizeFn = AnoGltfResult (*)(void* user, const char* path, size_t* bytes);
using AnoGltfFileReadFn = AnoGltfResult (*)(void* user, const char* path, void* destination, size_t bytes);

struct AnoGltfOptions final {
    AnoGltfAllocateFn allocate = nullptr;
    AnoGltfFreeFn free = nullptr;
    void* user = nullptr;
    AnoGltfFileSizeFn fileSize = nullptr;
    AnoGltfFileReadFn fileRead = nullptr;
    void* fileUser = nullptr;
    uint32_t maxJsonTokens = 0;
    uint32_t maxNesting = 0;
};

struct AnoGltfData final {
    AnoGltfFileType fileType = AnoGltfFileType::invalid;
    AnoGltfAsset asset;
    AnoGltfMesh* meshes = nullptr;
    uint32_t meshesCount = 0;
    AnoGltfMaterial* materials = nullptr;
    uint32_t materialsCount = 0;
    AnoGltfBuffer* buffers = nullptr;
    uint32_t buffersCount = 0;
    AnoGltfBufferView* bufferViews = nullptr;
    uint32_t bufferViewsCount = 0;
    AnoGltfAccessor* accessors = nullptr;
    uint32_t accessorsCount = 0;
    AnoGltfImage* images = nullptr;
    uint32_t imagesCount = 0;
    AnoGltfTexture* textures = nullptr;
    uint32_t texturesCount = 0;
    AnoGltfSampler* samplers = nullptr;
    uint32_t samplersCount = 0;
    AnoGltfSkin* skins = nullptr;
    uint32_t skinsCount = 0;
    AnoGltfCamera* cameras = nullptr;
    uint32_t camerasCount = 0;
    AnoGltfLight* lights = nullptr;
    uint32_t lightsCount = 0;
    AnoGltfNode* nodes = nullptr;
    uint32_t nodesCount = 0;
    AnoGltfScene* scenes = nullptr;
    uint32_t scenesCount = 0;
    AnoGltfSceneIndex scene;
    AnoGltfAnimation* animations = nullptr;
    uint32_t animationsCount = 0;
    AnoGltfMaterialVariant* variants = nullptr;
    uint32_t variantsCount = 0;
    AnoGltfArray<AnoGltfString> extensionsUsed;
    AnoGltfArray<AnoGltfString> extensionsRequired;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfRootExtensionsKnown> extensions;
    const uint8_t* glbBin = nullptr;
    uint64_t glbBinSize = 0;
    size_t arenaSize = 0;
    uint32_t jsonTokenCount = 0;
    void* sourceAllocation = nullptr;
    size_t sourceAllocationSize = 0;
    AnoGltfFreeFn sourceFree = nullptr;
    void* sourceAllocatorUser = nullptr;
    void* bufferStorage = nullptr;
    size_t bufferStorageSize = 0;
    AnoGltfFreeFn bufferStorageFree = nullptr;
    void* bufferStorageUser = nullptr;
    AnoGltfFreeFn arenaFree = nullptr;
    void* allocatorUser = nullptr;
};

template<class T>
concept AnoGltfPlainData = std::is_standard_layout_v<T>
    && std::is_trivially_copyable_v<T>
    && !std::is_polymorphic_v<T>;

static_assert(AnoGltfPlainData<AnoGltfAsset>);
static_assert(AnoGltfPlainData<AnoGltfBuffer>);
static_assert(AnoGltfPlainData<AnoGltfBufferView>);
static_assert(AnoGltfPlainData<AnoGltfAccessor>);
static_assert(AnoGltfPlainData<AnoGltfMaterial>);
static_assert(AnoGltfPlainData<AnoGltfMesh>);
static_assert(AnoGltfPlainData<AnoGltfNode>);
static_assert(AnoGltfPlainData<AnoGltfAnimation>);
static_assert(AnoGltfPlainData<AnoGltfData>);

template<class T>
[[nodiscard]] auto ano_gltf_index_of(const T* objects, uint32_t count, const T* object)
{
    static constexpr auto annotations = std::define_static_array(
        std::meta::annotations_of_with_type(^^T, ^^AnoGltfIndexKind));
    static_assert(annotations.size() == 1, "indexed glTF objects declare exactly one tag");
    constexpr std::meta::info tag =
        std::meta::extract<AnoGltfIndexKind>(annotations[0]).tag;
    using Tag = [:tag:];
    AnoGltfIndex<Tag> index;
    if (!objects || !object || count == 0)
        return index;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(objects);
    const uintptr_t address = reinterpret_cast<uintptr_t>(object);
    if (count > (UINTPTR_MAX - begin) / sizeof(T))
        return index;
    const uintptr_t end = begin + static_cast<uintptr_t>(count) * sizeof(T);
    if (address < begin || address >= end || (address - begin) % sizeof(T) != 0)
        return index;
    index.value = static_cast<uint32_t>((address - begin) / sizeof(T));
    return index;
}

static_assert(std::is_same_v<
    decltype(ano_gltf_index_of<AnoGltfMesh>(nullptr, 0, nullptr)), AnoGltfMeshIndex>);
static_assert(std::is_same_v<
    decltype(ano_gltf_index_of<AnoGltfAnimationChannel>(nullptr, 0, nullptr)),
    AnoGltfAnimationChannelIndex>);

extern "C" {

[[nodiscard]] AnoGltfResult ano_gltf_parse_memory(
    const void* bytes, size_t byteCount, const AnoGltfOptions* options, AnoGltfData** outData);
[[nodiscard]] AnoGltfResult ano_gltf_parse_file(
    const char* path, const AnoGltfOptions* options, AnoGltfData** outData);
[[nodiscard]] AnoGltfResult ano_gltf_load_buffers(
    AnoGltfData* data, const char* gltfPath, const AnoGltfOptions* options);
[[nodiscard]] AnoGltfResult ano_gltf_load_buffer_base64(
    const AnoGltfOptions* options, size_t byteCount, const char* base64, void** outData);
[[nodiscard]] AnoGltfResult ano_gltf_bind_buffer(
    AnoGltfData* data, AnoGltfBufferIndex buffer, const void* bytes, size_t byteCount);
[[nodiscard]] AnoGltfResult ano_gltf_bind_buffer_view(
    AnoGltfData* data, AnoGltfBufferViewIndex view, const void* bytes, size_t byteCount);
[[nodiscard]] AnoGltfResult ano_gltf_validate_loaded_data(const AnoGltfData* data);
void ano_gltf_free(AnoGltfData* data);
[[nodiscard]] const char* ano_gltf_result_string(AnoGltfResult result);

[[nodiscard]] size_t ano_gltf_decode_string(char* string);
[[nodiscard]] size_t ano_gltf_decode_uri(char* uri);
[[nodiscard]] AnoGltfResult ano_gltf_copy_extras_json(
    const AnoGltfExtras* extras, char* destination, size_t* destinationSize);
[[nodiscard]] uint32_t ano_gltf_component_count(AnoGltfAccessorType type);
[[nodiscard]] uint32_t ano_gltf_component_size(AnoGltfComponentType type);
[[nodiscard]] uint32_t ano_gltf_element_size(
    AnoGltfAccessorType type, AnoGltfComponentType componentType);
[[nodiscard]] const uint8_t* ano_gltf_buffer_view_data(
    const AnoGltfData* data, AnoGltfBufferViewIndex view);
[[nodiscard]] const AnoGltfAccessor* ano_gltf_find_accessor(
    const AnoGltfData* data, const AnoGltfPrimitive* primitive,
    AnoGltfAttributeType type, int32_t set);
[[nodiscard]] bool ano_gltf_accessor_read_float(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, float* output, size_t outputCount);
[[nodiscard]] bool ano_gltf_accessor_read_uint(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output, size_t outputCount);
[[nodiscard]] bool ano_gltf_accessor_read_index(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output);
[[nodiscard]] uint64_t ano_gltf_accessor_unpack_floats(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    float* output, uint64_t outputCount);
[[nodiscard]] uint64_t ano_gltf_accessor_unpack_indices(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    void* output, size_t outputComponentSize, uint64_t outputCount);
void ano_gltf_node_transform_local(const AnoGltfNode* node, float output[16]);
[[nodiscard]] bool ano_gltf_node_transform_world(
    const AnoGltfData* data, AnoGltfNodeIndex node, float output[16]);

}

#ifdef ANOGLTF_IMPLEMENTATION

#include <stdio.h>

namespace anogltf_detail {

inline constexpr uint32_t defaultMaxJsonTokens = 1u << 20;
inline constexpr uint32_t defaultMaxNesting = 128;
inline constexpr uint32_t glbMagic = 0x46546c67u;
inline constexpr uint32_t glbVersion = 2;
inline constexpr uint32_t glbJsonChunk = 0x4e4f534au;
inline constexpr uint32_t glbBinChunk = 0x004e4942u;

enum class TokenType : uint8_t {
    undefined,
    object,
    array,
    string,
    primitive,
};

struct Token final {
    TokenType type = TokenType::undefined;
    ptrdiff_t start = -1;
    ptrdiff_t end = -1;
    int32_t size = 0;
    int32_t parent = -1;
    uint32_t next = 0;
    uint16_t depth = 0;
};

struct TokenParser final {
    size_t position = 0;
    uint32_t next = 0;
    int32_t parent = -1;
};

enum class TokenError : int32_t {
    no_memory = -1,
    invalid = -2,
    partial = -3,
};

struct Input final {
    const char* json = nullptr;
    size_t jsonSize = 0;
    const uint8_t* bin = nullptr;
    size_t binSize = 0;
    AnoGltfFileType fileType = AnoGltfFileType::invalid;
};

struct Arena final {
    uint8_t* base = nullptr;
    size_t limit = 0;
    size_t at = 0;
    bool failed = false;
};

template<class T>
struct ArrayTraits final {
    static constexpr bool value = false;
};

template<class T>
struct ArrayTraits<AnoGltfArray<T>> final {
    static constexpr bool value = true;
    using Element = T;
};

template<class T>
struct IndexTraits final {
    static constexpr bool value = false;
};

template<class Tag>
struct IndexTraits<AnoGltfIndex<Tag>> final {
    static constexpr bool value = true;
};

template<class T>
struct OptionalTraits final {
    static constexpr bool value = false;
};

template<class T>
struct OptionalTraits<AnoGltfOptional<T>> final {
    static constexpr bool value = true;
    using Value = T;
};

template<class T>
struct FixedArrayTraits final {
    static constexpr bool value = false;
};

template<class T, size_t Count>
struct FixedArrayTraits<AnoGltfFixedArray<T, Count>> final {
    static constexpr bool value = true;
    using Element = T;
    static constexpr size_t count = Count;
};

template<class T>
struct ExtensionsTraits final {
    static constexpr bool value = false;
};

template<class Known>
struct ExtensionsTraits<AnoGltfExtensions<Known>> final {
    static constexpr bool value = true;
    using Schema = Known;
};

struct RootSchema final {
    [[=AnoGltfRequired{}]] AnoGltfAsset asset;
    AnoGltfArray<AnoGltfMesh> meshes;
    AnoGltfArray<AnoGltfMaterial> materials;
    AnoGltfArray<AnoGltfBuffer> buffers;
    AnoGltfArray<AnoGltfBufferView> bufferViews;
    AnoGltfArray<AnoGltfAccessor> accessors;
    AnoGltfArray<AnoGltfImage> images;
    AnoGltfArray<AnoGltfTexture> textures;
    AnoGltfArray<AnoGltfSampler> samplers;
    AnoGltfArray<AnoGltfSkin> skins;
    AnoGltfArray<AnoGltfCamera> cameras;
    AnoGltfArray<AnoGltfNode> nodes;
    AnoGltfArray<AnoGltfScene> scenes;
    AnoGltfSceneIndex scene;
    AnoGltfArray<AnoGltfAnimation> animations;
    AnoGltfArray<AnoGltfString> extensionsUsed;
    AnoGltfArray<AnoGltfString> extensionsRequired;
    AnoGltfExtras extras;
    AnoGltfExtensions<AnoGltfRootExtensionsKnown> extensions;
};

template<class T>
consteval bool plain_schema_type()
{
    if constexpr (ArrayTraits<T>::value) {
        return AnoGltfPlainData<T>
            && plain_schema_type<typename ArrayTraits<T>::Element>();
    } else if constexpr (OptionalTraits<T>::value) {
        return AnoGltfPlainData<T>
            && plain_schema_type<typename OptionalTraits<T>::Value>();
    } else if constexpr (FixedArrayTraits<T>::value) {
        return AnoGltfPlainData<T>
            && plain_schema_type<typename FixedArrayTraits<T>::Element>();
    } else if constexpr (ExtensionsTraits<T>::value) {
        return AnoGltfPlainData<T>
            && plain_schema_type<typename ExtensionsTraits<T>::Schema>()
            && plain_schema_type<AnoGltfExtension>();
    } else if constexpr (std::is_array_v<T>) {
        return plain_schema_type<std::remove_extent_t<T>>();
    } else if constexpr (std::is_class_v<T>) {
        if constexpr (!AnoGltfPlainData<T> || !std::is_final_v<T>)
            return false;
        bool plain = true;
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto member : members) {
            using Field = [:std::meta::type_of(member):];
            if constexpr (!plain_schema_type<Field>())
                plain = false;
        }
        return plain;
    } else {
        return std::is_trivially_copyable_v<T>;
    }
}

static_assert(plain_schema_type<RootSchema>());
static_assert(plain_schema_type<AnoGltfData>());

static void* default_allocate(void*, size_t bytes)
{
    return malloc(bytes);
}

static void default_free(void*, void* allocation)
{
    free(allocation);
}

static AnoGltfResult default_file_size(void*, const char* path, size_t* bytes)
{
    if (!path || !bytes)
        return AnoGltfResult::invalid_options;
    FILE* file = fopen(path, "rb");
    if (!file)
        return AnoGltfResult::file_not_found;
    const bool sought = fseek(file, 0, SEEK_END) == 0;
    const long length = sought ? ftell(file) : -1;
    const bool closed = fclose(file) == 0;
    if (length < 0 || !closed)
        return AnoGltfResult::io_error;
    *bytes = static_cast<size_t>(length);
    return AnoGltfResult::success;
}

static AnoGltfResult default_file_read(void*, const char* path, void* destination, size_t bytes)
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

static AnoGltfResult resolve_options(const AnoGltfOptions* source, AnoGltfOptions* output)
{
    *output = {};
    if (source)
        *output = *source;
    if ((output->allocate == nullptr) != (output->free == nullptr))
        return AnoGltfResult::invalid_options;
    if ((output->fileSize == nullptr) != (output->fileRead == nullptr))
        return AnoGltfResult::invalid_options;
    if (!output->allocate) {
        output->allocate = default_allocate;
        output->free = default_free;
    }
    if (!output->fileSize) {
        output->fileSize = default_file_size;
        output->fileRead = default_file_read;
    }
    if (output->maxJsonTokens == 0)
        output->maxJsonTokens = defaultMaxJsonTokens;
    if (output->maxNesting == 0)
        output->maxNesting = defaultMaxNesting;
    if (output->maxJsonTokens > INT32_MAX || output->maxNesting > UINT16_MAX)
        return AnoGltfResult::invalid_options;
    return AnoGltfResult::success;
}

static uint32_t read_u32_le(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
        | static_cast<uint32_t>(bytes[1]) << 8
        | static_cast<uint32_t>(bytes[2]) << 16
        | static_cast<uint32_t>(bytes[3]) << 24;
}

static bool checked_add(size_t a, size_t b, size_t* out)
{
    if (a > SIZE_MAX - b)
        return false;
    *out = a + b;
    return true;
}

static bool checked_mul(size_t a, size_t b, size_t* out)
{
    if (a != 0 && b > SIZE_MAX / a)
        return false;
    *out = a * b;
    return true;
}

static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (a > UINT64_MAX - b)
        return false;
    *out = a + b;
    return true;
}

static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (a != 0 && b > UINT64_MAX / a)
        return false;
    *out = a * b;
    return true;
}

static void* arena_take(Arena* arena, size_t count, size_t size, size_t alignment)
{
    size_t bytes = 0;
    if (!checked_mul(count, size, &bytes) || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        arena->failed = true;
        return nullptr;
    }
    const size_t mask = alignment - 1;
    if (arena->at > SIZE_MAX - mask) {
        arena->failed = true;
        return nullptr;
    }
    const size_t aligned = (arena->at + mask) & ~mask;
    size_t end = 0;
    if (!checked_add(aligned, bytes, &end) || end > arena->limit) {
        arena->failed = true;
        return nullptr;
    }
    arena->at = end;
    return arena->base ? arena->base + aligned : nullptr;
}

template<class T>
static T* arena_take(Arena* arena, size_t count)
{
    return static_cast<T*>(arena_take(arena, count, sizeof(T), alignof(T)));
}

static Token* allocate_token(TokenParser* parser, Token* tokens, size_t tokenCapacity)
{
    if (parser->next >= tokenCapacity)
        return nullptr;
    Token* token = &tokens[parser->next++];
    *token = {};
    token->parent = -1;
    return token;
}

static void fill_token(Token* token, TokenType type, ptrdiff_t start, ptrdiff_t end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int32_t parse_primitive(
    TokenParser* parser, const char* json, size_t length, Token* tokens, size_t tokenCapacity)
{
    const ptrdiff_t start = static_cast<ptrdiff_t>(parser->position);
    for (; parser->position < length; ++parser->position) {
        const unsigned char c = static_cast<unsigned char>(json[parser->position]);
        switch (c) {
        case '\t': case '\r': case '\n': case ' ': case ',': case ']': case '}':
            goto found;
        default:
            if (c < 32 || c >= 127) {
                parser->position = static_cast<size_t>(start);
                return static_cast<int32_t>(TokenError::invalid);
            }
            break;
        }
    }

found:
    if (!tokens) {
        if (parser->position != 0)
            --parser->position;
        return 0;
    }
    Token* token = allocate_token(parser, tokens, tokenCapacity);
    if (!token) {
        parser->position = static_cast<size_t>(start);
        return static_cast<int32_t>(TokenError::no_memory);
    }
    fill_token(token, TokenType::primitive, start, static_cast<ptrdiff_t>(parser->position));
    token->parent = parser->parent;
    if (parser->position != 0)
        --parser->position;
    return 0;
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int32_t parse_string(
    TokenParser* parser, const char* json, size_t length, Token* tokens, size_t tokenCapacity)
{
    const ptrdiff_t start = static_cast<ptrdiff_t>(parser->position++);
    for (; parser->position < length; ++parser->position) {
        const char c = json[parser->position];
        if (c == '"') {
            if (!tokens)
                return 0;
            Token* token = allocate_token(parser, tokens, tokenCapacity);
            if (!token) {
                parser->position = static_cast<size_t>(start);
                return static_cast<int32_t>(TokenError::no_memory);
            }
            fill_token(token, TokenType::string, start + 1, static_cast<ptrdiff_t>(parser->position));
            token->parent = parser->parent;
            return 0;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            parser->position = static_cast<size_t>(start);
            return static_cast<int32_t>(TokenError::invalid);
        }
        if (c != '\\')
            continue;
        if (++parser->position >= length) {
            parser->position = static_cast<size_t>(start);
            return static_cast<int32_t>(TokenError::partial);
        }
        switch (json[parser->position]) {
        case '"': case '/': case '\\': case 'b': case 'f': case 'r': case 'n': case 't':
            break;
        case 'u':
            for (int i = 0; i < 4; ++i) {
                if (++parser->position >= length || !is_hex(json[parser->position])) {
                    parser->position = static_cast<size_t>(start);
                    return static_cast<int32_t>(TokenError::invalid);
                }
            }
            break;
        default:
            parser->position = static_cast<size_t>(start);
            return static_cast<int32_t>(TokenError::invalid);
        }
    }
    parser->position = static_cast<size_t>(start);
    return static_cast<int32_t>(TokenError::partial);
}

static int32_t tokenize(
    TokenParser* parser, const char* json, size_t length, Token* tokens, size_t tokenCapacity)
{
    int32_t count = static_cast<int32_t>(parser->next);
    for (; parser->position < length; ++parser->position) {
        const char c = json[parser->position];
        Token* token = nullptr;
        switch (c) {
        case '{': case '[':
            if (count == INT32_MAX)
                return static_cast<int32_t>(TokenError::no_memory);
            ++count;
            if (!tokens)
                break;
            token = allocate_token(parser, tokens, tokenCapacity);
            if (!token)
                return static_cast<int32_t>(TokenError::no_memory);
            if (parser->parent != -1) {
                ++tokens[parser->parent].size;
                token->parent = parser->parent;
            }
            token->type = c == '{' ? TokenType::object : TokenType::array;
            token->start = static_cast<ptrdiff_t>(parser->position);
            parser->parent = static_cast<int32_t>(parser->next - 1);
            break;
        case '}': case ']': {
            if (!tokens)
                break;
            const TokenType expected = c == '}' ? TokenType::object : TokenType::array;
            if (parser->next == 0)
                return static_cast<int32_t>(TokenError::invalid);
            token = &tokens[parser->next - 1];
            for (;;) {
                if (token->start != -1 && token->end == -1) {
                    if (token->type != expected)
                        return static_cast<int32_t>(TokenError::invalid);
                    token->end = static_cast<ptrdiff_t>(parser->position + 1);
                    parser->parent = token->parent;
                    break;
                }
                if (token->parent == -1)
                    return static_cast<int32_t>(TokenError::invalid);
                token = &tokens[token->parent];
            }
            break;
        }
        case '"': {
            const int32_t result = parse_string(parser, json, length, tokens, tokenCapacity);
            if (result < 0)
                return result;
            if (count == INT32_MAX)
                return static_cast<int32_t>(TokenError::no_memory);
            ++count;
            if (parser->parent != -1 && tokens)
                ++tokens[parser->parent].size;
            break;
        }
        case '\t': case '\r': case '\n': case ' ':
            break;
        case ':':
            if (tokens)
                parser->parent = static_cast<int32_t>(parser->next - 1);
            break;
        case ',':
            if (tokens && parser->parent != -1
                && tokens[parser->parent].type != TokenType::array
                && tokens[parser->parent].type != TokenType::object)
                parser->parent = tokens[parser->parent].parent;
            break;
        case '-': case '0': case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': case 't': case 'f': case 'n': {
            if (tokens && parser->parent != -1) {
                const Token* parent = &tokens[parser->parent];
                if (parent->type == TokenType::object
                    || (parent->type == TokenType::string && parent->size != 0))
                    return static_cast<int32_t>(TokenError::invalid);
            }
            const int32_t result = parse_primitive(parser, json, length, tokens, tokenCapacity);
            if (result < 0)
                return result;
            if (count == INT32_MAX)
                return static_cast<int32_t>(TokenError::no_memory);
            ++count;
            if (parser->parent != -1 && tokens)
                ++tokens[parser->parent].size;
            break;
        }
        default:
            return static_cast<int32_t>(TokenError::invalid);
        }
    }
    if (tokens) {
        for (uint32_t i = 0; i < parser->next; ++i) {
            if (tokens[i].start != -1 && tokens[i].end == -1)
                return static_cast<int32_t>(TokenError::partial);
        }
    }
    return count;
}

static AnoGltfResult finalize_tokens(Token* tokens, uint32_t count, uint32_t maxDepth)
{
    if (count == 0 || tokens[0].parent != -1)
        return AnoGltfResult::invalid_json;
    for (uint32_t i = 0; i < count; ++i) {
        tokens[i].next = i + 1;
        if (tokens[i].parent >= static_cast<int32_t>(i))
            return AnoGltfResult::invalid_json;
        const uint32_t depth = tokens[i].parent < 0
            ? 1u : static_cast<uint32_t>(tokens[tokens[i].parent].depth) + 1u;
        if (depth > maxDepth)
            return AnoGltfResult::limit_exceeded;
        tokens[i].depth = static_cast<uint16_t>(depth);
        if (i != 0 && tokens[i].parent == -1)
            return AnoGltfResult::invalid_json;
    }
    for (uint32_t i = count; i-- > 1;) {
        const uint32_t parent = static_cast<uint32_t>(tokens[i].parent);
        if (tokens[parent].next < tokens[i].next)
            tokens[parent].next = tokens[i].next;
    }
    return tokens[0].next == count ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

static AnoGltfResult split_input(const void* source, size_t byteCount, Input* output)
{
    if (!source || byteCount < 2)
        return AnoGltfResult::data_too_short;
    const uint8_t* bytes = static_cast<const uint8_t*>(source);
    if (byteCount >= 4 && read_u32_le(bytes) == glbMagic) {
        if (byteCount < 20)
            return AnoGltfResult::data_too_short;
        if (read_u32_le(bytes + 4) != glbVersion)
            return AnoGltfResult::invalid_gltf;
        const uint32_t declaredLength = read_u32_le(bytes + 8);
        if (declaredLength != byteCount)
            return AnoGltfResult::invalid_gltf;

        size_t at = 12;
        bool foundJson = false;
        bool foundBin = false;
        while (at < byteCount) {
            if (byteCount - at < 8)
                return AnoGltfResult::invalid_gltf;
            const uint32_t chunkLength = read_u32_le(bytes + at);
            const uint32_t chunkType = read_u32_le(bytes + at + 4);
            at += 8;
            if (chunkLength > byteCount - at)
                return AnoGltfResult::invalid_gltf;
            if (!foundJson) {
                if (chunkType != glbJsonChunk)
                    return AnoGltfResult::invalid_gltf;
                output->json = reinterpret_cast<const char*>(bytes + at);
                output->jsonSize = chunkLength;
                foundJson = true;
            } else if (chunkType == glbJsonChunk) {
                return AnoGltfResult::invalid_gltf;
            } else if (chunkType == glbBinChunk) {
                if (foundBin)
                    return AnoGltfResult::invalid_gltf;
                output->bin = bytes + at;
                output->binSize = chunkLength;
                foundBin = true;
            }
            at += chunkLength;
        }
        if (!foundJson || output->jsonSize == 0)
            return AnoGltfResult::invalid_gltf;
        output->fileType = AnoGltfFileType::glb;
        return AnoGltfResult::success;
    }

    size_t start = 0;
    if (byteCount >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        start = 3;
    while (start < byteCount) {
        const uint8_t c = bytes[start];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        ++start;
    }
    if (start == byteCount)
        return AnoGltfResult::data_too_short;
    if (bytes[start] != '{')
        return AnoGltfResult::unknown_format;
    output->json = reinterpret_cast<const char*>(bytes + start);
    output->jsonSize = byteCount - start;
    output->fileType = AnoGltfFileType::gltf;
    return AnoGltfResult::success;
}

static uint8_t hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return static_cast<uint8_t>(c - '0');
    if (c >= 'A' && c <= 'F')
        return static_cast<uint8_t>(c - 'A' + 10);
    return static_cast<uint8_t>(c - 'a' + 10);
}

static bool append_byte(char byte, char* output, size_t capacity, size_t* at)
{
    if (output) {
        if (*at >= capacity)
            return false;
        output[*at] = byte;
    }
    ++*at;
    return true;
}

static bool append_codepoint(uint32_t codepoint, char* output, size_t capacity, size_t* at)
{
    if (codepoint <= 0x7f)
        return append_byte(static_cast<char>(codepoint), output, capacity, at);
    if (codepoint <= 0x7ff)
        return append_byte(static_cast<char>(0xc0u | codepoint >> 6), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint & 0x3fu)), output, capacity, at);
    if (codepoint <= 0xffff)
        return append_byte(static_cast<char>(0xe0u | codepoint >> 12), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint >> 6 & 0x3fu)), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint & 0x3fu)), output, capacity, at);
    if (codepoint <= 0x10ffff)
        return append_byte(static_cast<char>(0xf0u | codepoint >> 18), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint >> 12 & 0x3fu)), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint >> 6 & 0x3fu)), output, capacity, at)
            && append_byte(static_cast<char>(0x80u | (codepoint & 0x3fu)), output, capacity, at);
    return false;
}

static bool copy_utf8(
    const char* source, size_t length, size_t* sourceAt, char* output, size_t capacity, size_t* outputAt)
{
    const uint8_t first = static_cast<uint8_t>(source[*sourceAt]);
    size_t width = 0;
    if (first >= 0xc2 && first <= 0xdf)
        width = 2;
    else if (first >= 0xe0 && first <= 0xef)
        width = 3;
    else if (first >= 0xf0 && first <= 0xf4)
        width = 4;
    else
        return false;
    if (width > length - *sourceAt)
        return false;
    for (size_t i = 1; i < width; ++i) {
        const uint8_t continuation = static_cast<uint8_t>(source[*sourceAt + i]);
        if ((continuation & 0xc0u) != 0x80u)
            return false;
    }
    const uint8_t second = static_cast<uint8_t>(source[*sourceAt + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
        || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
        return false;
    for (size_t i = 0; i < width; ++i) {
        if (!append_byte(source[*sourceAt + i], output, capacity, outputAt))
            return false;
    }
    *sourceAt += width - 1;
    return true;
}

static bool decode_json_string(
    const char* source, size_t length, char* output, size_t capacity, size_t* decodedLength)
{
    size_t outAt = 0;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = static_cast<uint8_t>(source[i]);
        if (c >= 0x80) {
            if (!copy_utf8(source, length, &i, output, capacity, &outAt))
                return false;
            continue;
        }
        if (c < 0x20)
            return false;
        if (c != '\\') {
            if (!append_byte(static_cast<char>(c), output, capacity, &outAt))
                return false;
            continue;
        }
        if (++i >= length)
            return false;
        switch (source[i]) {
        case '"': case '\\': case '/':
            if (!append_byte(source[i], output, capacity, &outAt))
                return false;
            break;
        case 'b':
            if (!append_byte('\b', output, capacity, &outAt))
                return false;
            break;
        case 'f':
            if (!append_byte('\f', output, capacity, &outAt))
                return false;
            break;
        case 'n':
            if (!append_byte('\n', output, capacity, &outAt))
                return false;
            break;
        case 'r':
            if (!append_byte('\r', output, capacity, &outAt))
                return false;
            break;
        case 't':
            if (!append_byte('\t', output, capacity, &outAt))
                return false;
            break;
        case 'u': {
            if (length - i <= 4)
                return false;
            uint32_t codepoint = 0;
            for (size_t digit = 0; digit < 4; ++digit) {
                const char hex = source[++i];
                if (!is_hex(hex))
                    return false;
                codepoint = codepoint << 4 | hex_value(hex);
            }
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (length - i <= 6 || source[i + 1] != '\\' || source[i + 2] != 'u')
                    return false;
                i += 2;
                uint32_t low = 0;
                for (size_t digit = 0; digit < 4; ++digit) {
                    const char hex = source[++i];
                    if (!is_hex(hex))
                        return false;
                    low = low << 4 | hex_value(hex);
                }
                if (low < 0xdc00 || low > 0xdfff)
                    return false;
                codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + low - 0xdc00u;
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                return false;
            }
            if (!append_codepoint(codepoint, output, capacity, &outAt))
                return false;
            break;
        }
        default:
            return false;
        }
    }
    *decodedLength = outAt;
    return true;
}

static bool token_string_equal(
    const char* json, const Token& token, const char* expected, size_t expectedLength)
{
    if (token.type != TokenType::string)
        return false;
    char decoded[64];
    size_t decodedLength = 0;
    const size_t rawLength = static_cast<size_t>(token.end - token.start);
    if (!decode_json_string(json + token.start, rawLength, decoded, sizeof(decoded), &decodedLength))
        return false;
    return decodedLength == expectedLength && memcmp(decoded, expected, expectedLength) == 0;
}

static constexpr uint32_t json_name_hash(const char* bytes, size_t length)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint8_t>(bytes[i]);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

inline constexpr size_t maxJsonFieldNameBytes = 64;

enum class DecodedJsonNameStatus : uint8_t {
    invalid,
    unbuffered,
    buffered,
};

struct DecodedJsonName final {
    char bytes[maxJsonFieldNameBytes];
    size_t length;
    uint32_t hash;
};

[[gnu::noinline]] static DecodedJsonNameStatus decode_json_name(
    const char* json, const Token& token, DecodedJsonName* output)
{
    const char* raw = json + token.start;
    const size_t rawLength = static_cast<size_t>(token.end - token.start);
    if (decode_json_string(
            raw, rawLength, output->bytes, sizeof(output->bytes), &output->length)) {
        output->hash = json_name_hash(output->bytes, output->length);
        return DecodedJsonNameStatus::buffered;
    }
    return decode_json_string(raw, rawLength, nullptr, 0, &output->length)
        ? DecodedJsonNameStatus::unbuffered : DecodedJsonNameStatus::invalid;
}

[[gnu::noinline, gnu::noclone]] static bool decoded_json_name_equal(
    const DecodedJsonName& decoded, const char* expected)
{
    return memcmp(decoded.bytes, expected, decoded.length) == 0;
}

static bool parse_u64(const char* json, const Token& token, uint64_t* output)
{
    if (token.type != TokenType::primitive || token.start >= token.end)
        return false;
    const char* text = json + token.start;
    const size_t length = static_cast<size_t>(token.end - token.start);
    if ((length > 1 && text[0] == '0') || text[0] < '0' || text[0] > '9')
        return false;
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t digit = static_cast<uint8_t>(text[i] - '0');
        if (digit > 9 || value > (UINT64_MAX - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *output = value;
    return true;
}

static bool parse_bool(const char* json, const Token& token, bool* output)
{
    if (token.type != TokenType::primitive)
        return false;
    const size_t length = static_cast<size_t>(token.end - token.start);
    const char* text = json + token.start;
    if (length == 4 && memcmp(text, "true", 4) == 0) {
        *output = true;
        return true;
    }
    if (length == 5 && memcmp(text, "false", 5) == 0) {
        *output = false;
        return true;
    }
    return false;
}

static bool parse_double(const char* json, const Token& token, double* output)
{
    if (token.type != TokenType::primitive || token.start >= token.end)
        return false;
    const char* text = json + token.start;
    const size_t length = static_cast<size_t>(token.end - token.start);
    size_t at = 0;
    bool negative = false;
    if (text[at] == '-') {
        negative = true;
        if (++at == length)
            return false;
    }
    if (text[at] < '0' || text[at] > '9')
        return false;
    if (text[at] == '0' && at + 1 < length && text[at + 1] >= '0' && text[at + 1] <= '9')
        return false;

    double value = 0.0;
    uint32_t significant = 0;
    bool significantStarted = false;
    int32_t decimalExponent = 0;
    while (at < length && text[at] >= '0' && text[at] <= '9') {
        const uint8_t digit = static_cast<uint8_t>(text[at] - '0');
        if (digit != 0 || significantStarted) {
            significantStarted = true;
            if (significant < 19) {
                value = value * 10.0 + static_cast<double>(digit);
                ++significant;
            } else {
                ++decimalExponent;
            }
        }
        ++at;
    }
    if (at < length && text[at] == '.') {
        if (++at == length || text[at] < '0' || text[at] > '9')
            return false;
        while (at < length && text[at] >= '0' && text[at] <= '9') {
            const uint8_t digit = static_cast<uint8_t>(text[at] - '0');
            if (!significantStarted && digit == 0) {
                --decimalExponent;
            } else if (significant < 19) {
                significantStarted = true;
                value = value * 10.0 + static_cast<double>(digit);
                --decimalExponent;
                ++significant;
            }
            ++at;
        }
    }
    int32_t explicitExponent = 0;
    if (at < length && (text[at] == 'e' || text[at] == 'E')) {
        if (++at == length)
            return false;
        bool exponentNegative = false;
        if (text[at] == '+' || text[at] == '-') {
            exponentNegative = text[at] == '-';
            if (++at == length)
                return false;
        }
        if (text[at] < '0' || text[at] > '9')
            return false;
        while (at < length && text[at] >= '0' && text[at] <= '9') {
            if (explicitExponent < 10000)
                explicitExponent = explicitExponent * 10 + text[at] - '0';
            ++at;
        }
        if (exponentNegative)
            explicitExponent = -explicitExponent;
    }
    if (at != length)
        return false;
    if ((explicitExponent > 0 && decimalExponent > 10000 - explicitExponent)
        || (explicitExponent < 0 && decimalExponent < -10000 - explicitExponent))
        return false;
    decimalExponent += explicitExponent;
    if (decimalExponent > 308 || decimalExponent < -400)
        return false;
    while (decimalExponent > 0) {
        if (value > DBL_MAX / 10.0)
            return false;
        value *= 10.0;
        --decimalExponent;
    }
    while (decimalExponent < 0) {
        value *= 0.1;
        ++decimalExponent;
    }
    *output = negative ? -value : value;
    return true;
}

template<bool Write, class T>
static AnoGltfResult decode_value(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena);

template<class T>
consteval uint64_t object_required_mask()
{
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^T, std::meta::access_context::unchecked()));
    static_assert(members.size() <= 64, "reflected JSON objects use a 64-bit presence mask");
    uint64_t requiredMask = 0;
    size_t memberIndex = 0;
    template for (constexpr auto member : members) {
        constexpr auto ignored = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoGltfIgnore));
        constexpr auto required = std::define_static_array(
            std::meta::annotations_of_with_type(member, ^^AnoGltfRequired));
        static_assert(ignored.size() <= 1 && required.size() <= 1);
        static_assert(ignored.empty() || required.empty(), "ignored JSON fields cannot be required");
        if constexpr (ignored.empty()) {
            constexpr auto name = std::meta::identifier_of(member);
            static_assert(!name.empty() && name.size() <= maxJsonFieldNameBytes);
            if constexpr (!required.empty())
                requiredMask |= UINT64_C(1) << memberIndex;
        }
        ++memberIndex;
    }
    return requiredMask;
}

template<bool Write, class T>
static AnoGltfResult decode_array(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if (token.type != TokenType::array || token.size < 0
        || static_cast<uint64_t>(token.size) > UINT32_MAX)
        return AnoGltfResult::invalid_gltf;
    using Element = typename ArrayTraits<T>::Element;
    const uint32_t count = static_cast<uint32_t>(token.size);
    Element* elements = count ? arena_take<Element>(arena, count) : nullptr;
    if (arena->failed)
        return AnoGltfResult::limit_exceeded;
    if constexpr (Write) {
        output->data = elements;
        output->count = count;
    }
    uint32_t child = tokenIndex + 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (child >= token.next)
            return AnoGltfResult::invalid_json;
        Element* element = nullptr;
        if constexpr (Write)
            element = &elements[i];
        const AnoGltfResult result = decode_value<Write>(json, tokens, child, element, arena);
        if (result != AnoGltfResult::success)
            return result;
        child = tokens[child].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

template<bool Write, class T>
static AnoGltfResult decode_object(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if (token.type != TokenType::object || token.size < 0)
        return AnoGltfResult::invalid_gltf;
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^T, std::meta::access_context::unchecked()));
    static constexpr uint64_t requiredMask = object_required_mask<T>();
    if constexpr (Write)
        *output = T{};
    uint64_t seen = 0;
    uint32_t child = tokenIndex + 1;
    for (int32_t pair = 0; pair < token.size; ++pair) {
        if (child >= token.next || tokens[child].type != TokenType::string
            || child + 1 >= token.next)
            return AnoGltfResult::invalid_json;
        const uint32_t valueIndex = child + 1;
        DecodedJsonName key{};
        const DecodedJsonNameStatus keyStatus =
            decode_json_name(json, tokens[child], &key);
        if (keyStatus == DecodedJsonNameStatus::invalid) {
            return AnoGltfResult::invalid_json;
        } else if (keyStatus == DecodedJsonNameStatus::buffered) {
            bool matched = false;
            AnoGltfResult result = AnoGltfResult::success;
            size_t memberIndex = 0;
            template for (constexpr auto member : members) {
                constexpr auto ignored = std::define_static_array(
                    std::meta::annotations_of_with_type(member, ^^AnoGltfIgnore));
                if constexpr (ignored.empty()) {
                    constexpr auto name = std::meta::identifier_of(member);
                    constexpr uint32_t fieldHash = json_name_hash(name.data(), name.size());
                    if (!matched && key.hash == fieldHash && key.length == name.size()
                        && decoded_json_name_equal(key, name.data())) {
                        const uint64_t bit = UINT64_C(1) << memberIndex;
                        if ((seen & bit) != 0)
                            return AnoGltfResult::invalid_gltf;
                        seen |= bit;
                        using Field = [:std::meta::type_of(member):];
                        Field* field = nullptr;
                        if constexpr (Write)
                            field = &(output->*(&[:member:]));
                        result = decode_value<Write>(
                            json, tokens, valueIndex, field, arena);
                        matched = true;
                    }
                }
                ++memberIndex;
            }
            if (result != AnoGltfResult::success)
                return result;
        }
        child = tokens[valueIndex].next;
    }
    if (child != token.next)
        return AnoGltfResult::invalid_json;
    return (seen & requiredMask) == requiredMask
        ? AnoGltfResult::success : AnoGltfResult::invalid_gltf;
}

template<bool Write>
static AnoGltfResult decode_json(
    const char* json, const Token* tokens, uint32_t tokenIndex, AnoGltfJson* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    ptrdiff_t start = token.start;
    ptrdiff_t end = token.end;
    if (token.type == TokenType::string) {
        --start;
        ++end;
    }
    if (start < 0 || end < start || static_cast<uint64_t>(end - start) > UINT32_MAX)
        return AnoGltfResult::limit_exceeded;
    const size_t length = static_cast<size_t>(end - start);
    char* copy = arena_take<char>(arena, length + 1);
    if (arena->failed)
        return AnoGltfResult::limit_exceeded;
    if constexpr (Write) {
        memcpy(copy, json + start, length);
        copy[length] = '\0';
        output->data = copy;
        output->length = static_cast<uint32_t>(length);
    }
    return AnoGltfResult::success;
}

template<bool Write>
static AnoGltfResult decode_extras(
    const char* json, const Token* tokens, uint32_t tokenIndex, AnoGltfExtras* output, Arena* arena)
{
    AnoGltfJson* raw = nullptr;
    if constexpr (Write)
        raw = &output->json;
    AnoGltfResult result = decode_json<Write>(json, tokens, tokenIndex, raw, arena);
    if (result != AnoGltfResult::success || tokens[tokenIndex].type != TokenType::object)
        return result;

    const Token& token = tokens[tokenIndex];
    uint32_t child = tokenIndex + 1;
    bool foundTargetNames = false;
    for (int32_t pair = 0; pair < token.size; ++pair) {
        if (child >= token.next || child + 1 >= token.next)
            return AnoGltfResult::invalid_json;
        const uint32_t valueIndex = child + 1;
        if (token_string_equal(json, tokens[child], "targetNames", 11)) {
            if (foundTargetNames)
                return AnoGltfResult::invalid_gltf;
            foundTargetNames = true;
            AnoGltfArray<AnoGltfString>* names = nullptr;
            if constexpr (Write)
                names = &output->targetNames;
            result = decode_value<Write>(json, tokens, valueIndex, names, arena);
            if (result != AnoGltfResult::success)
                return result;
        }
        child = tokens[valueIndex].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

template<bool Write, class T>
static AnoGltfResult decode_extensions(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if (token.type != TokenType::object || token.size < 0)
        return AnoGltfResult::invalid_gltf;
    using Known = typename ExtensionsTraits<T>::Schema;
    Known* known = nullptr;
    if constexpr (Write)
        known = &output->known;
    AnoGltfResult result = decode_object<Write, Known>(json, tokens, tokenIndex, known, arena);
    if (result != AnoGltfResult::success)
        return result;

    const uint32_t count = static_cast<uint32_t>(token.size);
    AnoGltfExtension* entries = count ? arena_take<AnoGltfExtension>(arena, count) : nullptr;
    if (arena->failed)
        return AnoGltfResult::limit_exceeded;
    if constexpr (Write) {
        output->all.data = count ? entries : nullptr;
        output->all.count = count;
    }
    uint32_t child = tokenIndex + 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (child >= token.next || child + 1 >= token.next)
            return AnoGltfResult::invalid_json;
        const uint32_t valueIndex = child + 1;
        AnoGltfString* name = nullptr;
        AnoGltfJson* value = nullptr;
        if constexpr (Write) {
            name = &entries[i].name;
            value = &entries[i].value;
        }
        result = decode_value<Write>(json, tokens, child, name, arena);
        if (result != AnoGltfResult::success)
            return result;
        result = decode_json<Write>(json, tokens, valueIndex, value, arena);
        if (result != AnoGltfResult::success)
            return result;
        child = tokens[valueIndex].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

static bool string_starts_with(const AnoGltfString& string, const char* prefix, size_t length)
{
    return string.length >= length && memcmp(string.data, prefix, length) == 0;
}

static bool parse_decimal_suffix(const AnoGltfString& string, size_t at, int32_t* output)
{
    if (at >= string.length || string.data[at++] != '_' || at == string.length)
        return false;
    if (string.data[at] == '0' && at + 1 != string.length)
        return false;
    uint32_t value = 0;
    for (; at < string.length; ++at) {
        const uint8_t digit = static_cast<uint8_t>(string.data[at] - '0');
        if (digit > 9 || value > static_cast<uint32_t>(INT32_MAX - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *output = static_cast<int32_t>(value);
    return true;
}

template<class T>
static void classify_attribute(T* attribute)
{
    if (attribute->name.length != 0 && attribute->name.data[0] == '_') {
        attribute->type = AnoGltfAttributeType::custom;
        return;
    }
    struct Candidate final {
        const char* name;
        uint8_t length;
        AnoGltfAttributeType type;
        bool indexed;
    };
    static constexpr Candidate candidates[] = {
        {"POSITION", 8, AnoGltfAttributeType::position, false},
        {"NORMAL", 6, AnoGltfAttributeType::normal, false},
        {"TANGENT", 7, AnoGltfAttributeType::tangent, false},
        {"TEXCOORD", 8, AnoGltfAttributeType::texcoord, true},
        {"COLOR", 5, AnoGltfAttributeType::color, true},
        {"JOINTS", 6, AnoGltfAttributeType::joints, true},
        {"WEIGHTS", 7, AnoGltfAttributeType::weights, true},
        {"TRANSLATION", 11, AnoGltfAttributeType::translation, false},
        {"ROTATION", 8, AnoGltfAttributeType::rotation, false},
        {"SCALE", 5, AnoGltfAttributeType::scale, false},
    };
    for (const Candidate& candidate : candidates) {
        const bool exact = attribute->name.length == candidate.length
            && memcmp(attribute->name.data, candidate.name, candidate.length) == 0;
        if ((!candidate.indexed && exact)
            || (candidate.indexed
                && string_starts_with(attribute->name, candidate.name, candidate.length)
                && parse_decimal_suffix(attribute->name, candidate.length, &attribute->set))) {
            attribute->type = candidate.type;
            return;
        }
    }
}

template<bool Write>
static AnoGltfResult decode_attribute_map(
    const char* json, const Token* tokens, uint32_t tokenIndex, AnoGltfAttributeMap* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if (token.type != TokenType::object || token.size < 0)
        return AnoGltfResult::invalid_gltf;
    const uint32_t count = static_cast<uint32_t>(token.size);
    AnoGltfAttribute* attributes = count ? arena_take<AnoGltfAttribute>(arena, count) : nullptr;
    if (arena->failed)
        return AnoGltfResult::limit_exceeded;
    if constexpr (Write) {
        output->values.data = count ? attributes : nullptr;
        output->values.count = count;
    }
    uint32_t child = tokenIndex + 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (child >= token.next || child + 1 >= token.next)
            return AnoGltfResult::invalid_json;
        const uint32_t valueIndex = child + 1;
        AnoGltfString* name = nullptr;
        AnoGltfAccessorIndex* accessor = nullptr;
        if constexpr (Write) {
            name = &attributes[i].name;
            accessor = &attributes[i].accessor;
        }
        AnoGltfResult result = decode_value<Write>(json, tokens, child, name, arena);
        if (result != AnoGltfResult::success)
            return result;
        result = decode_value<Write>(json, tokens, valueIndex, accessor, arena);
        if (result != AnoGltfResult::success)
            return result;
        if constexpr (Write)
            classify_attribute(&attributes[i]);
        child = tokens[valueIndex].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

template<bool Write>
static AnoGltfResult decode_draco_attribute_map(
    const char* json, const Token* tokens, uint32_t tokenIndex,
    AnoGltfDracoAttributeMap* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if (token.type != TokenType::object || token.size < 0)
        return AnoGltfResult::invalid_gltf;
    const uint32_t count = static_cast<uint32_t>(token.size);
    AnoGltfDracoAttribute* attributes = count
        ? arena_take<AnoGltfDracoAttribute>(arena, count) : nullptr;
    if (arena->failed)
        return AnoGltfResult::limit_exceeded;
    if constexpr (Write) {
        output->values.data = count ? attributes : nullptr;
        output->values.count = count;
    }
    uint32_t child = tokenIndex + 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (child >= token.next || child + 1 >= token.next)
            return AnoGltfResult::invalid_json;
        const uint32_t valueIndex = child + 1;
        AnoGltfString* name = nullptr;
        uint32_t* uniqueId = nullptr;
        if constexpr (Write) {
            name = &attributes[i].name;
            uniqueId = &attributes[i].uniqueId;
        }
        AnoGltfResult result = decode_value<Write>(json, tokens, child, name, arena);
        if (result != AnoGltfResult::success)
            return result;
        result = decode_value<Write>(json, tokens, valueIndex, uniqueId, arena);
        if (result != AnoGltfResult::success)
            return result;
        if constexpr (Write)
            classify_attribute(&attributes[i]);
        child = tokens[valueIndex].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

template<bool Write, class T>
static AnoGltfResult decode_fixed_array(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    constexpr size_t count = FixedArrayTraits<T>::count;
    if (token.type != TokenType::array || token.size != static_cast<int32_t>(count))
        return AnoGltfResult::invalid_gltf;
    using Element = typename FixedArrayTraits<T>::Element;
    uint32_t child = tokenIndex + 1;
    for (size_t i = 0; i < count; ++i) {
        if (child >= token.next)
            return AnoGltfResult::invalid_json;
        Element* element = nullptr;
        if constexpr (Write)
            element = &output->values[i];
        const AnoGltfResult result = decode_value<Write>(json, tokens, child, element, arena);
        if (result != AnoGltfResult::success)
            return result;
        child = tokens[child].next;
    }
    return child == token.next ? AnoGltfResult::success : AnoGltfResult::invalid_json;
}

template<class T>
static bool enum_value_valid(T value)
{
    static_assert(std::is_enum_v<T>);
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^T));
    bool valid = false;
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto invalid = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoGltfEnumInvalid));
        static_assert(invalid.size() <= 1);
        if constexpr (invalid.empty()) {
            if (value == [:enumerator:])
                valid = true;
        }
    }
    return valid;
}

template<class T>
consteval bool enum_has_json_names()
{
    static_assert(std::is_enum_v<T>);
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^T));
    bool named = false;
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto names = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^AnoGltfJsonName));
        static_assert(names.size() <= 1);
        if constexpr (!names.empty())
            named = true;
    }
    return named;
}

template<bool Write, class T>
static AnoGltfResult decode_enum(const char* json, const Token& token, T* output)
{
    static_assert(std::is_enum_v<T>);
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^T));
    T value{};
    bool matched = false;
    if constexpr (enum_has_json_names<T>()) {
        if (token.type != TokenType::string)
            return AnoGltfResult::invalid_gltf;
        DecodedJsonName decoded{};
        if (decode_json_name(json, token, &decoded) != DecodedJsonNameStatus::buffered)
            return AnoGltfResult::invalid_gltf;
        template for (constexpr auto enumerator : enumerators) {
            constexpr auto names = std::define_static_array(
                std::meta::annotations_of_with_type(enumerator, ^^AnoGltfJsonName));
            if constexpr (!names.empty()) {
                constexpr AnoGltfJsonName name =
                    std::meta::extract<AnoGltfJsonName>(names[0]);
                constexpr uint32_t hash = json_name_hash(name.text, name.length);
                if (!matched && decoded.hash == hash && decoded.length == name.length
                    && decoded_json_name_equal(decoded, name.text)) {
                    value = [:enumerator:];
                    matched = true;
                }
            }
        }
    } else {
        using Underlying = std::underlying_type_t<T>;
        static_assert(std::is_unsigned_v<Underlying>);
        uint64_t raw = 0;
        if (!parse_u64(json, token, &raw))
            return AnoGltfResult::invalid_gltf;
        if constexpr (sizeof(Underlying) < sizeof(uint64_t)) {
            constexpr uint64_t maximum =
                (UINT64_C(1) << (sizeof(Underlying) * 8)) - UINT64_C(1);
            if (raw > maximum)
                return AnoGltfResult::invalid_gltf;
        }
        value = static_cast<T>(static_cast<Underlying>(raw));
        matched = enum_value_valid(value);
    }
    if (!matched)
        return AnoGltfResult::invalid_gltf;
    if constexpr (Write)
        *output = value;
    return AnoGltfResult::success;
}

template<bool Write, class T>
static AnoGltfResult decode_value(
    const char* json, const Token* tokens, uint32_t tokenIndex, T* output, Arena* arena)
{
    const Token& token = tokens[tokenIndex];
    if constexpr (OptionalTraits<T>::value) {
        using Value = typename OptionalTraits<T>::Value;
        Value* value = nullptr;
        if constexpr (Write) {
            output->present = true;
            value = &output->value;
        }
        return decode_value<Write>(json, tokens, tokenIndex, value, arena);
    } else if constexpr (FixedArrayTraits<T>::value) {
        return decode_fixed_array<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (ExtensionsTraits<T>::value) {
        return decode_extensions<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (ArrayTraits<T>::value) {
        return decode_array<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (std::is_same_v<T, AnoGltfJson>) {
        return decode_json<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (std::is_same_v<T, AnoGltfExtras>) {
        return decode_extras<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (std::is_same_v<T, AnoGltfAttributeMap>) {
        return decode_attribute_map<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (std::is_same_v<T, AnoGltfDracoAttributeMap>) {
        return decode_draco_attribute_map<Write>(json, tokens, tokenIndex, output, arena);
    } else if constexpr (std::is_same_v<T, AnoGltfMorphTarget>) {
        AnoGltfAttributeMap* attributes = nullptr;
        if constexpr (Write)
            attributes = &output->attributes;
        return decode_attribute_map<Write>(json, tokens, tokenIndex, attributes, arena);
    } else if constexpr (IndexTraits<T>::value) {
        uint64_t value = 0;
        if (!parse_u64(json, token, &value) || value >= ANO_GLTF_NO_INDEX)
            return AnoGltfResult::invalid_gltf;
        if constexpr (Write)
            output->value = static_cast<uint32_t>(value);
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, AnoGltfString>) {
        if (token.type != TokenType::string)
            return AnoGltfResult::invalid_gltf;
        size_t decodedLength = 0;
        const char* raw = json + token.start;
        const size_t rawLength = static_cast<size_t>(token.end - token.start);
        if (!decode_json_string(raw, rawLength, nullptr, 0, &decodedLength))
            return AnoGltfResult::invalid_json;
        if (decodedLength > UINT32_MAX)
            return AnoGltfResult::limit_exceeded;
        char* decoded = arena_take<char>(arena, decodedLength + 1);
        if (arena->failed)
            return AnoGltfResult::limit_exceeded;
        if constexpr (Write) {
            size_t written = 0;
            if (!decode_json_string(raw, rawLength, decoded, decodedLength, &written)
                || written != decodedLength)
                return AnoGltfResult::invalid_json;
            decoded[decodedLength] = '\0';
            output->data = decoded;
            output->length = static_cast<uint32_t>(decodedLength);
        }
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, AnoGltfNumberArray>) {
        if (token.type != TokenType::array || token.size < 0 || token.size > 16)
            return AnoGltfResult::invalid_gltf;
        uint32_t child = tokenIndex + 1;
        for (int32_t i = 0; i < token.size; ++i) {
            double value = 0.0;
            if (child >= token.next || !parse_double(json, tokens[child], &value))
                return AnoGltfResult::invalid_gltf;
            if constexpr (Write)
                output->values[i] = value;
            child = tokens[child].next;
        }
        if (child != token.next)
            return AnoGltfResult::invalid_json;
        if constexpr (Write)
            output->count = static_cast<uint8_t>(token.size);
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, bool>) {
        bool value = false;
        if (!parse_bool(json, token, &value))
            return AnoGltfResult::invalid_gltf;
        if constexpr (Write)
            *output = value;
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        uint64_t value = 0;
        if (!parse_u64(json, token, &value) || value > UINT32_MAX)
            return AnoGltfResult::invalid_gltf;
        if constexpr (Write)
            *output = static_cast<uint32_t>(value);
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        uint64_t value = 0;
        if (!parse_u64(json, token, &value))
            return AnoGltfResult::invalid_gltf;
        if constexpr (Write)
            *output = value;
        return AnoGltfResult::success;
    } else if constexpr (std::is_same_v<T, float>) {
        double value = 0.0;
        if (!parse_double(json, token, &value) || value < -FLT_MAX || value > FLT_MAX)
            return AnoGltfResult::invalid_gltf;
        if constexpr (Write)
            *output = static_cast<float>(value);
        return AnoGltfResult::success;
    } else if constexpr (std::is_enum_v<T>) {
        return decode_enum<Write>(json, token, output);
    } else {
        return decode_object<Write>(json, tokens, tokenIndex, output, arena);
    }
}

static bool string_equal(const AnoGltfString& string, const char* literal, size_t length)
{
    return string.length == length && memcmp(string.data, literal, length) == 0;
}

static uint32_t component_size(AnoGltfComponentType type)
{
    using enum AnoGltfComponentType;
    switch (type) {
    case byte:
    case unsigned_byte:
        return 1;
    case short_:
    case unsigned_short:
        return 2;
    case unsigned_int:
    case float_:
        return 4;
    case invalid:
        return 0;
    }
    return 0;
}

static uint32_t component_count(AnoGltfAccessorType type)
{
    using enum AnoGltfAccessorType;
    switch (type) {
    case scalar: return 1;
    case vec2: return 2;
    case vec3: return 3;
    case vec4: return 4;
    case mat2: return 4;
    case mat3: return 9;
    case mat4: return 16;
    case invalid: return 0;
    }
    return 0;
}

static uint32_t element_size(AnoGltfComponentType component, AnoGltfAccessorType type)
{
    const uint32_t scalarSize = component_size(component);
    if (scalarSize == 0)
        return 0;
    using enum AnoGltfAccessorType;
    switch (type) {
    case scalar: return scalarSize;
    case vec2: return scalarSize * 2;
    case vec3: return scalarSize * 3;
    case vec4: return scalarSize * 4;
    case mat2: {
        const uint32_t column = scalarSize * 2;
        return ((column + 3u) & ~3u) * 2u;
    }
    case mat3: {
        const uint32_t column = scalarSize * 3;
        return ((column + 3u) & ~3u) * 3u;
    }
    case mat4:
        return scalarSize * 16;
    case invalid:
        return 0;
    }
    return 0;
}

static bool valid_target(AnoGltfBufferViewTarget target)
{
    return target == AnoGltfBufferViewTarget::unspecified || enum_value_valid(target);
}

static bool valid_component(AnoGltfComponentType component)
{
    return component_size(component) != 0;
}

static bool view_contains(
    const AnoGltfBufferView& view, uint64_t byteOffset, uint64_t count,
    uint64_t stride, uint64_t itemSize)
{
    uint64_t occupied = itemSize;
    if (count == 0)
        occupied = 0;
    else if (count > 1) {
        uint64_t preceding = 0;
        if (!checked_mul_u64(count - 1, stride, &preceding)
            || !checked_add_u64(preceding, itemSize, &occupied))
            return false;
    }
    uint64_t end = 0;
    return checked_add_u64(byteOffset, occupied, &end) && end <= view.byteLength;
}

static AnoGltfResult validate_sparse(
    const RootSchema& root, const AnoGltfAccessor& accessor, uint32_t accessorElementSize)
{
    const AnoGltfAccessorSparse& sparse = accessor.sparse;
    if (sparse.count == 0)
        return AnoGltfResult::success;
    if (sparse.count > accessor.count
        || !ano_gltf_has_index(sparse.indices.bufferView)
        || !ano_gltf_has_index(sparse.values.bufferView)
        || sparse.indices.bufferView.value >= root.bufferViews.count
        || sparse.values.bufferView.value >= root.bufferViews.count)
        return AnoGltfResult::invalid_gltf;

    const AnoGltfComponentType indexType = sparse.indices.componentType;
    if (indexType != AnoGltfComponentType::unsigned_byte
        && indexType != AnoGltfComponentType::unsigned_short
        && indexType != AnoGltfComponentType::unsigned_int)
        return AnoGltfResult::invalid_gltf;
    const uint32_t indexSize = component_size(indexType);
    if (sparse.indices.byteOffset % indexSize != 0
        || sparse.values.byteOffset % component_size(accessor.componentType) != 0)
        return AnoGltfResult::invalid_gltf;

    const AnoGltfBufferView& indexView = root.bufferViews.data[sparse.indices.bufferView.value];
    const AnoGltfBufferView& valueView = root.bufferViews.data[sparse.values.bufferView.value];
    if (indexView.byteStride != 0 || valueView.byteStride != 0
        || indexView.target != AnoGltfBufferViewTarget::unspecified
        || valueView.target != AnoGltfBufferViewTarget::unspecified)
        return AnoGltfResult::invalid_gltf;
    if (!view_contains(indexView, sparse.indices.byteOffset, sparse.count, indexSize, indexSize)
        || !view_contains(valueView, sparse.values.byteOffset, sparse.count,
            accessorElementSize, accessorElementSize))
        return AnoGltfResult::invalid_gltf;
    return AnoGltfResult::success;
}

template<class Tag>
static bool valid_optional_index(AnoGltfIndex<Tag> index, uint32_t count)
{
    return !ano_gltf_has_index(index) || index.value < count;
}

template<class Tag>
static bool valid_required_index(AnoGltfIndex<Tag> index, uint32_t count)
{
    return ano_gltf_has_index(index) && index.value < count;
}

static bool valid_filter(AnoGltfFilter filter)
{
    return filter == AnoGltfFilter::unspecified || enum_value_valid(filter);
}

static bool string_array_unique(const AnoGltfArray<AnoGltfString>& strings)
{
    for (uint32_t i = 0; i < strings.count; ++i) {
        if (strings.data[i].length == 0)
            return false;
        for (uint32_t j = 0; j < i; ++j) {
            if (strings.data[i].length == strings.data[j].length
                && memcmp(strings.data[i].data, strings.data[j].data, strings.data[i].length) == 0)
                return false;
        }
    }
    return true;
}

static bool string_array_contains(
    const AnoGltfArray<AnoGltfString>& strings, const AnoGltfString& needle)
{
    for (uint32_t i = 0; i < strings.count; ++i) {
        if (strings.data[i].length == needle.length
            && memcmp(strings.data[i].data, needle.data, needle.length) == 0)
            return true;
    }
    return false;
}

static bool string_array_contains(
    const AnoGltfArray<AnoGltfString>& strings, const char* literal, size_t length)
{
    const AnoGltfString needle{literal, static_cast<uint32_t>(length)};
    return length <= UINT32_MAX && string_array_contains(strings, needle);
}

template<class Known>
static bool extension_known(const AnoGltfString& extension)
{
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Known, std::meta::access_context::unchecked()));
    bool known = false;
    template for (constexpr auto member : members) {
        constexpr auto name = std::meta::identifier_of(member);
        if (extension.length == name.size()
            && memcmp(extension.data, name.data(), name.size()) == 0)
            known = true;
    }
    return known;
}

static bool extension_supported(const AnoGltfString& extension)
{
    return string_equal(extension, "KHR_mesh_quantization", sizeof("KHR_mesh_quantization") - 1)
        || extension_known<AnoGltfBufferViewExtensionsKnown>(extension)
        || extension_known<AnoGltfBufferExtensionsKnown>(extension)
        || extension_known<AnoGltfTextureExtensionsKnown>(extension)
        || extension_known<AnoGltfTextureInfoExtensionsKnown>(extension)
        || extension_known<AnoGltfMaterialExtensionsKnown>(extension)
        || extension_known<AnoGltfPrimitiveExtensionsKnown>(extension)
        || extension_known<AnoGltfNodeExtensionsKnown>(extension)
        || extension_known<AnoGltfRootExtensionsKnown>(extension);
}

template<class T>
static bool extensions_declared(const AnoGltfArray<AnoGltfString>& used, const T& value)
{
    if constexpr (ExtensionsTraits<T>::value) {
        for (uint32_t i = 0; i < value.all.count; ++i) {
            if (!string_array_contains(used, value.all.data[i].name))
                return false;
            for (uint32_t j = 0; j < i; ++j) {
                const AnoGltfString& left = value.all.data[i].name;
                const AnoGltfString& right = value.all.data[j].name;
                if (left.length == right.length
                    && memcmp(left.data, right.data, left.length) == 0)
                    return false;
            }
        }
        return extensions_declared(used, value.known);
    } else if constexpr (OptionalTraits<T>::value) {
        return !value.present || extensions_declared(used, value.value);
    } else if constexpr (ArrayTraits<T>::value) {
        for (uint32_t i = 0; i < value.count; ++i) {
            if (!extensions_declared(used, value.data[i]))
                return false;
        }
    } else if constexpr (FixedArrayTraits<T>::value) {
        for (size_t i = 0; i < FixedArrayTraits<T>::count; ++i) {
            if (!extensions_declared(used, value.values[i]))
                return false;
        }
    } else if constexpr (std::is_class_v<T>) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto member : members) {
            constexpr auto ignored = std::define_static_array(
                std::meta::annotations_of_with_type(member, ^^AnoGltfIgnore));
            if constexpr (ignored.empty()) {
                if (!extensions_declared(used, value.*(&[:member:])))
                    return false;
            }
        }
    }
    return true;
}

static AnoGltfResult validate_extension_lists(const RootSchema& root)
{
    if (!string_array_unique(root.extensionsUsed) || !string_array_unique(root.extensionsRequired))
        return AnoGltfResult::invalid_gltf;
    if (!extensions_declared(root.extensionsUsed, root))
        return AnoGltfResult::invalid_gltf;
    for (uint32_t required = 0; required < root.extensionsRequired.count; ++required) {
        const AnoGltfString& extension = root.extensionsRequired.data[required];
        if (!extension_supported(extension))
            return AnoGltfResult::unsupported_required_extension;
        bool declaredUsed = false;
        for (uint32_t used = 0; used < root.extensionsUsed.count; ++used) {
            if (extension.length == root.extensionsUsed.data[used].length
                && memcmp(extension.data, root.extensionsUsed.data[used].data, extension.length) == 0) {
                declaredUsed = true;
                break;
            }
        }
        if (!declaredUsed)
            return AnoGltfResult::invalid_gltf;
    }
    return AnoGltfResult::success;
}

static AnoGltfResult validate_meshopt(
    const RootSchema& root, const AnoGltfBufferView& view,
    const AnoGltfMeshoptCompression& meshopt)
{
    if (!valid_required_index(meshopt.buffer, root.buffers.count) || meshopt.byteLength == 0
        || meshopt.byteStride == 0 || meshopt.count == 0)
        return AnoGltfResult::invalid_gltf;
    uint64_t end = 0;
    if (!checked_add_u64(meshopt.byteOffset, meshopt.byteLength, &end)
        || end > root.buffers.data[meshopt.buffer.value].byteLength)
        return AnoGltfResult::invalid_gltf;
    uint64_t decodedSize = 0;
    if (!checked_mul_u64(meshopt.byteStride, meshopt.count, &decodedSize)
        || decodedSize != view.byteLength
        || (view.byteStride != 0 && view.byteStride != meshopt.byteStride))
        return AnoGltfResult::invalid_gltf;
    using enum AnoGltfMeshoptMode;
    switch (meshopt.mode) {
    case attributes:
        if (meshopt.byteStride % 4 != 0 || meshopt.byteStride > 256)
            return AnoGltfResult::invalid_gltf;
        break;
    case triangles:
        if (meshopt.count % 3 != 0 || (meshopt.byteStride != 2 && meshopt.byteStride != 4)
            || meshopt.filter != AnoGltfMeshoptFilter::none)
            return AnoGltfResult::invalid_gltf;
        break;
    case indices:
        if ((meshopt.byteStride != 2 && meshopt.byteStride != 4)
            || meshopt.filter != AnoGltfMeshoptFilter::none)
            return AnoGltfResult::invalid_gltf;
        break;
    case invalid:
        return AnoGltfResult::invalid_gltf;
    }
    using enum AnoGltfMeshoptFilter;
    switch (meshopt.filter) {
    case none:
    case exponential:
        break;
    case octahedral:
        if (meshopt.byteStride != 4 && meshopt.byteStride != 8)
            return AnoGltfResult::invalid_gltf;
        break;
    case quaternion:
        if (meshopt.byteStride != 8)
            return AnoGltfResult::invalid_gltf;
        break;
    case color:
        if (meshopt.byteStride != 4 && meshopt.byteStride != 8)
            return AnoGltfResult::invalid_gltf;
        break;
    }
    return AnoGltfResult::success;
}

enum class AttributeDomain : uint8_t {
    primitive,
    morph,
    instancing,
};

static bool attribute_accessor_valid(
    const AnoGltfAttribute& attribute, const AnoGltfAccessor& accessor,
    AttributeDomain domain, bool quantized)
{
    using enum AnoGltfAttributeType;
    const bool signedInteger = accessor.componentType == AnoGltfComponentType::byte
        || accessor.componentType == AnoGltfComponentType::short_;
    const bool unsignedInteger = accessor.componentType == AnoGltfComponentType::unsigned_byte
        || accessor.componentType == AnoGltfComponentType::unsigned_short;
    const bool normalizedInteger = accessor.normalized && (signedInteger || unsignedInteger);

    if (domain == AttributeDomain::instancing) {
        switch (attribute.type) {
        case translation:
        case scale:
            return attribute.set == 0 && accessor.type == AnoGltfAccessorType::vec3
                && accessor.componentType == AnoGltfComponentType::float_;
        case rotation:
            return attribute.set == 0 && accessor.type == AnoGltfAccessorType::vec4
                && (accessor.componentType == AnoGltfComponentType::float_
                    || (accessor.normalized && signedInteger));
        case custom:
            return true;
        case invalid:
        case position:
        case normal:
        case tangent:
        case texcoord:
        case color:
        case joints:
        case weights:
            return false;
        }
    }

    if (attribute.type == custom)
        return accessor.componentType != AnoGltfComponentType::unsigned_int;
    if (attribute.type == translation || attribute.type == rotation || attribute.type == scale)
        return false;

    const bool morph = domain == AttributeDomain::morph;
    switch (attribute.type) {
    case position:
        return accessor.type == AnoGltfAccessorType::vec3
            && accessor.min.count != 0 && accessor.max.count != 0
            && (accessor.componentType == AnoGltfComponentType::float_
                || (quantized && (morph ? signedInteger : signedInteger || unsignedInteger)));
    case normal:
        return accessor.type == AnoGltfAccessorType::vec3
            && (accessor.componentType == AnoGltfComponentType::float_
                || (quantized && accessor.normalized && signedInteger));
    case tangent:
        return accessor.type == (morph ? AnoGltfAccessorType::vec3 : AnoGltfAccessorType::vec4)
            && (accessor.componentType == AnoGltfComponentType::float_
                || (quantized && accessor.normalized && signedInteger));
    case texcoord:
        if (accessor.type != AnoGltfAccessorType::vec2)
            return false;
        if (accessor.componentType == AnoGltfComponentType::float_)
            return true;
        if (morph)
            return normalizedInteger || (quantized && signedInteger);
        return (accessor.normalized && unsignedInteger)
            || (quantized && (signedInteger || unsignedInteger));
    case color:
        return (accessor.type == AnoGltfAccessorType::vec3
                || accessor.type == AnoGltfAccessorType::vec4)
            && (accessor.componentType == AnoGltfComponentType::float_
                || (accessor.normalized && (unsignedInteger || (morph && signedInteger))));
    case joints:
        return !morph && accessor.type == AnoGltfAccessorType::vec4
            && unsignedInteger && !accessor.normalized;
    case weights:
        return !morph && accessor.type == AnoGltfAccessorType::vec4
            && (accessor.componentType == AnoGltfComponentType::float_
                || (accessor.normalized && unsignedInteger));
    case invalid:
    case custom:
    case translation:
    case rotation:
    case scale:
        return false;
    }
    return false;
}

static uint32_t attribute_type_count(
    const AnoGltfAttributeMap& map, AnoGltfAttributeType type)
{
    uint32_t count = 0;
    int32_t maximum = -1;
    for (uint32_t i = 0; i < map.values.count; ++i) {
        if (map.values.data[i].type != type)
            continue;
        ++count;
        if (map.values.data[i].set > maximum)
            maximum = map.values.data[i].set;
    }
    return count != 0 && maximum + 1 != static_cast<int32_t>(count)
        ? UINT32_MAX : count;
}

static bool topology_count_valid(AnoGltfPrimitiveMode mode, uint64_t count)
{
    using enum AnoGltfPrimitiveMode;
    switch (mode) {
    case points:
        return count != 0;
    case lines:
        return count != 0 && count % 2 == 0;
    case line_loop:
    case line_strip:
        return count >= 2;
    case triangles:
        return count != 0 && count % 3 == 0;
    case triangle_strip:
    case triangle_fan:
        return count >= 3;
    }
    return false;
}

static bool animation_output_valid(
    AnoGltfAnimationPath path, const AnoGltfAccessor& accessor)
{
    const bool normalizedInteger = accessor.normalized
        && (accessor.componentType == AnoGltfComponentType::byte
            || accessor.componentType == AnoGltfComponentType::unsigned_byte
            || accessor.componentType == AnoGltfComponentType::short_
            || accessor.componentType == AnoGltfComponentType::unsigned_short);
    using enum AnoGltfAnimationPath;
    switch (path) {
    case translation:
    case scale:
        return accessor.type == AnoGltfAccessorType::vec3
            && accessor.componentType == AnoGltfComponentType::float_;
    case rotation:
        return accessor.type == AnoGltfAccessorType::vec4
            && (accessor.componentType == AnoGltfComponentType::float_ || normalizedInteger);
    case weights:
        return accessor.type == AnoGltfAccessorType::scalar
            && (accessor.componentType == AnoGltfComponentType::float_ || normalizedInteger);
    case invalid:
        return false;
    }
    return false;
}

static AnoGltfResult validate_attribute_map(
    const RootSchema& root, const AnoGltfAttributeMap& map,
    AttributeDomain domain, bool quantized)
{
    for (uint32_t i = 0; i < map.values.count; ++i) {
        const AnoGltfAttribute& attribute = map.values.data[i];
        if (!valid_required_index(attribute.accessor, root.accessors.count))
            return AnoGltfResult::invalid_gltf;
        const AnoGltfAccessor& accessor = root.accessors.data[attribute.accessor.value];
        if (!attribute_accessor_valid(attribute, accessor, domain, quantized))
            return AnoGltfResult::invalid_gltf;
        if (ano_gltf_has_index(accessor.bufferView)) {
            const AnoGltfBufferView& view = root.bufferViews.data[accessor.bufferView.value];
            const uint32_t stride = view.byteStride
                ? view.byteStride : element_size(accessor.componentType, accessor.type);
            if (accessor.byteOffset % 4 != 0 || stride % 4 != 0)
                return AnoGltfResult::invalid_gltf;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const AnoGltfAttribute& previous = map.values.data[j];
            if (attribute.name.length == previous.name.length
                && memcmp(attribute.name.data, previous.name.data, attribute.name.length) == 0)
                return AnoGltfResult::invalid_gltf;
        }
    }
    using enum AnoGltfAttributeType;
    const uint32_t texcoords = attribute_type_count(map, texcoord);
    const uint32_t colors = attribute_type_count(map, color);
    const uint32_t jointsCount = attribute_type_count(map, joints);
    const uint32_t weightsCount = attribute_type_count(map, weights);
    if (texcoords == UINT32_MAX || colors == UINT32_MAX
        || jointsCount == UINT32_MAX || weightsCount == UINT32_MAX
        || jointsCount != weightsCount)
        return AnoGltfResult::invalid_gltf;
    return AnoGltfResult::success;
}

static AnoGltfResult validate_draco_attribute_map(const AnoGltfDracoAttributeMap& map)
{
    if (map.values.count == 0)
        return AnoGltfResult::invalid_gltf;
    for (uint32_t i = 0; i < map.values.count; ++i) {
        const AnoGltfDracoAttribute& attribute = map.values.data[i];
        if (attribute.type == AnoGltfAttributeType::invalid
            || attribute.type == AnoGltfAttributeType::translation
            || attribute.type == AnoGltfAttributeType::rotation
            || attribute.type == AnoGltfAttributeType::scale)
            return AnoGltfResult::invalid_gltf;
        for (uint32_t j = 0; j < i; ++j) {
            const AnoGltfDracoAttribute& previous = map.values.data[j];
            if (attribute.name.length == previous.name.length
                && memcmp(attribute.name.data, previous.name.data, attribute.name.length) == 0)
                return AnoGltfResult::invalid_gltf;
        }
    }
    return AnoGltfResult::success;
}

static AnoGltfResult validate_texture_info(const RootSchema& root, const AnoGltfTextureInfo& info)
{
    if (!valid_optional_index(info.index, root.textures.count))
        return AnoGltfResult::invalid_gltf;
    if (info.extensions.known.KHR_texture_transform.present) {
        const AnoGltfTextureTransform& transform =
            info.extensions.known.KHR_texture_transform.value;
        if (transform.texCoord.present && transform.texCoord.value > INT32_MAX)
            return AnoGltfResult::invalid_gltf;
    }
    return AnoGltfResult::success;
}

template<class T>
static AnoGltfResult validate_texture_infos(const RootSchema& root, const T& value)
{
    if constexpr (std::is_same_v<T, AnoGltfTextureInfo>) {
        return validate_texture_info(root, value);
    } else if constexpr (OptionalTraits<T>::value) {
        return value.present
            ? validate_texture_infos(root, value.value)
            : AnoGltfResult::success;
    } else if constexpr (ExtensionsTraits<T>::value) {
        return validate_texture_infos(root, value.known);
    } else if constexpr (ArrayTraits<T>::value || FixedArrayTraits<T>::value) {
        return AnoGltfResult::success;
    } else if constexpr (std::is_class_v<T>) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto member : members) {
            constexpr auto ignored = std::define_static_array(
                std::meta::annotations_of_with_type(member, ^^AnoGltfIgnore));
            static_assert(ignored.size() <= 1);
            if constexpr (ignored.empty()) {
                const AnoGltfResult result = validate_texture_infos(root, value.*(&[:member:]));
                if (result != AnoGltfResult::success)
                    return result;
            }
        }
    }
    return AnoGltfResult::success;
}

static AnoGltfResult validate_material(const RootSchema& root, const AnoGltfMaterial& material)
{
    if (!enum_value_valid(material.alphaMode))
        return AnoGltfResult::invalid_gltf;
    return validate_texture_infos(root, material);
}

static AnoGltfResult derive_node_parents(RootSchema* root, uint8_t* marks)
{
    for (uint32_t i = 0; i < root->nodes.count; ++i)
        root->nodes.data[i].parent = {};
    for (uint32_t parent = 0; parent < root->nodes.count; ++parent) {
        const AnoGltfArray<AnoGltfNodeIndex>& children = root->nodes.data[parent].children;
        for (uint32_t i = 0; i < children.count; ++i) {
            const AnoGltfNodeIndex child = children.data[i];
            if (!valid_required_index(child, root->nodes.count) || child.value == parent
                || ano_gltf_has_index(root->nodes.data[child.value].parent))
                return AnoGltfResult::invalid_gltf;
            root->nodes.data[child.value].parent.value = parent;
        }
    }
    memset(marks, 0, root->nodes.count);
    for (uint32_t start = 0; start < root->nodes.count; ++start) {
        uint32_t node = start;
        while (node < root->nodes.count && marks[node] == 0) {
            marks[node] = 1;
            const AnoGltfNodeIndex parent = root->nodes.data[node].parent;
            node = ano_gltf_has_index(parent) ? parent.value : root->nodes.count;
        }
        if (node < root->nodes.count && marks[node] == 1)
            return AnoGltfResult::invalid_gltf;
        node = start;
        while (node < root->nodes.count && marks[node] == 1) {
            marks[node] = 2;
            const AnoGltfNodeIndex parent = root->nodes.data[node].parent;
            node = ano_gltf_has_index(parent) ? parent.value : root->nodes.count;
        }
    }
    return AnoGltfResult::success;
}

static bool accessor_is_draco_placeholder(const RootSchema& root, uint32_t accessorIndex)
{
    for (uint32_t mesh = 0; mesh < root.meshes.count; ++mesh) {
        const AnoGltfArray<AnoGltfPrimitive>& primitives = root.meshes.data[mesh].primitives;
        for (uint32_t primitiveIndex = 0; primitiveIndex < primitives.count; ++primitiveIndex) {
            const AnoGltfPrimitive& primitive = primitives.data[primitiveIndex];
            if (!primitive.extensions.known.KHR_draco_mesh_compression.present)
                continue;
            if (ano_gltf_has_index(primitive.indices)
                && primitive.indices.value == accessorIndex)
                return true;
            for (uint32_t attribute = 0; attribute < primitive.attributes.values.count; ++attribute) {
                if (primitive.attributes.values.data[attribute].accessor.value == accessorIndex)
                    return true;
            }
        }
    }
    return false;
}

static bool buffer_is_meshopt_fallback(const AnoGltfBuffer& buffer)
{
    return (buffer.extensions.known.EXT_meshopt_compression.present
            && buffer.extensions.known.EXT_meshopt_compression.value.fallback)
        || (buffer.extensions.known.KHR_meshopt_compression.present
            && buffer.extensions.known.KHR_meshopt_compression.value.fallback);
}

static AnoGltfResult validate_schema(const Input& input, const RootSchema& root)
{
    if (!string_equal(root.asset.version, "2.0", 3))
        return AnoGltfResult::invalid_gltf;
    if (root.asset.minVersion.length != 0
        && !string_equal(root.asset.minVersion, "2.0", 3))
        return AnoGltfResult::invalid_gltf;
    const AnoGltfResult extensionResult = validate_extension_lists(root);
    if (extensionResult != AnoGltfResult::success)
        return extensionResult;
    const bool meshQuantized = string_array_contains(
        root.extensionsRequired, "KHR_mesh_quantization",
        sizeof("KHR_mesh_quantization") - 1);

    for (uint32_t i = 0; i < root.buffers.count; ++i) {
        const AnoGltfBuffer& buffer = root.buffers.data[i];
        if (buffer.byteLength == 0)
            return AnoGltfResult::invalid_gltf;
        if (buffer.uri.length == 0) {
            if (buffer_is_meshopt_fallback(buffer))
                continue;
            if (input.fileType != AnoGltfFileType::glb || i != 0 || !input.bin)
                return AnoGltfResult::invalid_gltf;
            uint64_t paddedLength = 0;
            if (!checked_add_u64(buffer.byteLength, 3, &paddedLength)
                || input.binSize < buffer.byteLength || input.binSize > paddedLength)
                return AnoGltfResult::invalid_gltf;
        }
    }

    for (uint32_t i = 0; i < root.bufferViews.count; ++i) {
        const AnoGltfBufferView& view = root.bufferViews.data[i];
        if (!ano_gltf_has_index(view.buffer) || view.buffer.value >= root.buffers.count
            || view.byteLength == 0 || view.byteOffset % 4 != 0 || !valid_target(view.target))
            return AnoGltfResult::invalid_gltf;
        if (view.byteStride != 0
            && (view.byteStride < 4 || view.byteStride > 252 || view.byteStride % 4 != 0))
            return AnoGltfResult::invalid_gltf;
        uint64_t end = 0;
        if (!checked_add_u64(view.byteOffset, view.byteLength, &end)
            || end > root.buffers.data[view.buffer.value].byteLength)
            return AnoGltfResult::invalid_gltf;
        const AnoGltfBufferViewExtensionsKnown& extensions = view.extensions.known;
        if (extensions.EXT_meshopt_compression.present) {
            const AnoGltfResult result = validate_meshopt(
                root, view, extensions.EXT_meshopt_compression.value);
            if (result != AnoGltfResult::success)
                return result;
        }
        if (extensions.KHR_meshopt_compression.present) {
            const AnoGltfResult result = validate_meshopt(
                root, view, extensions.KHR_meshopt_compression.value);
            if (result != AnoGltfResult::success)
                return result;
        }
    }

    for (uint32_t i = 0; i < root.accessors.count; ++i) {
        const AnoGltfAccessor& accessor = root.accessors.data[i];
        const uint32_t scalarSize = component_size(accessor.componentType);
        const uint32_t components = component_count(accessor.type);
        const uint32_t itemSize = element_size(accessor.componentType, accessor.type);
        if (!valid_component(accessor.componentType) || components == 0 || itemSize == 0
            || accessor.count == 0 || accessor.byteOffset % scalarSize != 0)
            return AnoGltfResult::invalid_gltf;
        if (accessor.normalized && (accessor.componentType == AnoGltfComponentType::float_
            || accessor.componentType == AnoGltfComponentType::unsigned_int))
            return AnoGltfResult::invalid_gltf;
        if ((accessor.min.count != 0 && accessor.min.count != components)
            || (accessor.max.count != 0 && accessor.max.count != components))
            return AnoGltfResult::invalid_gltf;
        if (accessor.min.count != 0 && accessor.max.count != 0) {
            for (uint32_t component = 0; component < components; ++component) {
                if (accessor.min.values[component] > accessor.max.values[component])
                    return AnoGltfResult::invalid_gltf;
            }
        }

        if (ano_gltf_has_index(accessor.bufferView)) {
            if (accessor.bufferView.value >= root.bufferViews.count)
                return AnoGltfResult::invalid_gltf;
            const AnoGltfBufferView& view = root.bufferViews.data[accessor.bufferView.value];
            const uint64_t stride = view.byteStride ? view.byteStride : itemSize;
            if (stride < itemSize || stride % scalarSize != 0
                || !view_contains(view, accessor.byteOffset, accessor.count, stride, itemSize))
                return AnoGltfResult::invalid_gltf;
        } else if (accessor.sparse.count == 0 && !accessor_is_draco_placeholder(root, i)) {
            return AnoGltfResult::invalid_gltf;
        }

        const AnoGltfResult sparseResult = validate_sparse(root, accessor, itemSize);
        if (sparseResult != AnoGltfResult::success)
            return sparseResult;
    }

    for (uint32_t i = 0; i < root.images.count; ++i) {
        const AnoGltfImage& image = root.images.data[i];
        const bool hasUri = image.uri.length != 0;
        const bool hasView = ano_gltf_has_index(image.bufferView);
        if (hasUri == hasView || !valid_optional_index(image.bufferView, root.bufferViews.count)
            || (hasView && image.mimeType.length == 0))
            return AnoGltfResult::invalid_gltf;
    }
    for (uint32_t i = 0; i < root.samplers.count; ++i) {
        const AnoGltfSampler& sampler = root.samplers.data[i];
        if (!valid_filter(sampler.magFilter) || !valid_filter(sampler.minFilter)
            || !enum_value_valid(sampler.wrapS) || !enum_value_valid(sampler.wrapT))
            return AnoGltfResult::invalid_gltf;
    }
    for (uint32_t i = 0; i < root.textures.count; ++i) {
        const AnoGltfTexture& texture = root.textures.data[i];
        if (!valid_optional_index(texture.sampler, root.samplers.count)
            || !valid_optional_index(texture.source, root.images.count))
            return AnoGltfResult::invalid_gltf;
        bool hasSource = ano_gltf_has_index(texture.source);
        if (texture.extensions.known.KHR_texture_basisu.present) {
            const AnoGltfImageIndex source = texture.extensions.known.KHR_texture_basisu.value.source;
            if (!valid_required_index(source, root.images.count))
                return AnoGltfResult::invalid_gltf;
            hasSource = true;
        }
        if (texture.extensions.known.EXT_texture_webp.present) {
            const AnoGltfImageIndex source = texture.extensions.known.EXT_texture_webp.value.source;
            if (!valid_required_index(source, root.images.count))
                return AnoGltfResult::invalid_gltf;
            hasSource = true;
        }
        if (!hasSource)
            return AnoGltfResult::invalid_gltf;
    }
    for (uint32_t i = 0; i < root.materials.count; ++i) {
        const AnoGltfResult result = validate_material(root, root.materials.data[i]);
        if (result != AnoGltfResult::success)
            return result;
    }

    for (uint32_t meshIndex = 0; meshIndex < root.meshes.count; ++meshIndex) {
        const AnoGltfMesh& mesh = root.meshes.data[meshIndex];
        if (mesh.primitives.count == 0)
            return AnoGltfResult::invalid_gltf;
        uint32_t targetCount = 0;
        for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.count; ++primitiveIndex) {
            const AnoGltfPrimitive& primitive = mesh.primitives.data[primitiveIndex];
            if (primitive.attributes.values.count == 0 || !enum_value_valid(primitive.mode)
                || !valid_optional_index(primitive.indices, root.accessors.count)
                || !valid_optional_index(primitive.material, root.materials.count))
                return AnoGltfResult::invalid_gltf;
            AnoGltfResult result = validate_attribute_map(
                root, primitive.attributes, AttributeDomain::primitive, meshQuantized);
            if (result != AnoGltfResult::success)
                return result;
            const uint64_t vertexCount =
                root.accessors.data[primitive.attributes.values.data[0].accessor.value].count;
            for (uint32_t attribute = 1; attribute < primitive.attributes.values.count; ++attribute) {
                const AnoGltfAccessorIndex accessor =
                    primitive.attributes.values.data[attribute].accessor;
                if (root.accessors.data[accessor.value].count != vertexCount)
                    return AnoGltfResult::invalid_gltf;
            }
            if (primitiveIndex == 0)
                targetCount = primitive.targets.count;
            else if (primitive.targets.count != targetCount)
                return AnoGltfResult::invalid_gltf;
            for (uint32_t target = 0; target < primitive.targets.count; ++target) {
                const AnoGltfAttributeMap& targetAttributes =
                    primitive.targets.data[target].attributes;
                if (targetAttributes.values.count == 0)
                    return AnoGltfResult::invalid_gltf;
                result = validate_attribute_map(
                    root, targetAttributes, AttributeDomain::morph, meshQuantized);
                if (result != AnoGltfResult::success)
                    return result;
                for (uint32_t attribute = 0; attribute < targetAttributes.values.count; ++attribute) {
                    const AnoGltfAttribute& targetAttribute =
                        targetAttributes.values.data[attribute];
                    const AnoGltfAccessorIndex accessor =
                        targetAttribute.accessor;
                    if (root.accessors.data[accessor.value].count != vertexCount)
                        return AnoGltfResult::invalid_gltf;
                    bool basePresent = false;
                    for (uint32_t base = 0; base < primitive.attributes.values.count; ++base) {
                        const AnoGltfAttribute& baseAttribute =
                            primitive.attributes.values.data[base];
                        if (targetAttribute.name.length == baseAttribute.name.length
                            && memcmp(targetAttribute.name.data, baseAttribute.name.data,
                                      targetAttribute.name.length) == 0) {
                            basePresent = true;
                            break;
                        }
                    }
                    if (!basePresent)
                        return AnoGltfResult::invalid_gltf;
                }
            }
            uint64_t elementCount = vertexCount;
            if (ano_gltf_has_index(primitive.indices)) {
                const AnoGltfAccessor& indices = root.accessors.data[primitive.indices.value];
                elementCount = indices.count;
                if (indices.type != AnoGltfAccessorType::scalar
                    || (indices.componentType != AnoGltfComponentType::unsigned_byte
                        && indices.componentType != AnoGltfComponentType::unsigned_short
                        && indices.componentType != AnoGltfComponentType::unsigned_int))
                    return AnoGltfResult::invalid_gltf;
                if (ano_gltf_has_index(indices.bufferView)) {
                    const AnoGltfBufferView& view = root.bufferViews.data[indices.bufferView.value];
                    const uint32_t stride = view.byteStride
                        ? view.byteStride : component_size(indices.componentType);
                    if (stride != component_size(indices.componentType))
                        return AnoGltfResult::invalid_gltf;
                }
            }
            if (!topology_count_valid(primitive.mode, elementCount))
                return AnoGltfResult::invalid_gltf;
            const AnoGltfPrimitiveExtensionsKnown& extensions = primitive.extensions.known;
            if (extensions.KHR_draco_mesh_compression.present) {
                const AnoGltfDracoMeshCompression& draco = extensions.KHR_draco_mesh_compression.value;
                if (!valid_required_index(draco.bufferView, root.bufferViews.count)
                    || (primitive.mode != AnoGltfPrimitiveMode::triangles
                        && primitive.mode != AnoGltfPrimitiveMode::triangle_strip))
                    return AnoGltfResult::invalid_gltf;
                result = validate_draco_attribute_map(draco.attributes);
                if (result != AnoGltfResult::success)
                    return result;
                for (uint32_t attribute = 0; attribute < draco.attributes.values.count; ++attribute) {
                    const AnoGltfDracoAttribute& compressed = draco.attributes.values.data[attribute];
                    bool declared = false;
                    for (uint32_t base = 0; base < primitive.attributes.values.count; ++base) {
                        const AnoGltfAttribute& source = primitive.attributes.values.data[base];
                        if (compressed.name.length == source.name.length
                            && memcmp(compressed.name.data, source.name.data,
                                      compressed.name.length) == 0) {
                            declared = true;
                            break;
                        }
                    }
                    if (!declared)
                        return AnoGltfResult::invalid_gltf;
                }
            }
            if (extensions.KHR_materials_variants.present) {
                const AnoGltfArray<AnoGltfMaterialVariantMapping>& mappings =
                    extensions.KHR_materials_variants.value.mappings;
                if (mappings.count == 0)
                    return AnoGltfResult::invalid_gltf;
                for (uint32_t mappingIndex = 0; mappingIndex < mappings.count; ++mappingIndex) {
                    const AnoGltfMaterialVariantMapping& mapping = mappings.data[mappingIndex];
                    if (!valid_required_index(mapping.material, root.materials.count)
                        || mapping.variants.count == 0)
                        return AnoGltfResult::invalid_gltf;
                    const uint32_t variantCount = root.extensions.known.KHR_materials_variants.present
                        ? root.extensions.known.KHR_materials_variants.value.variants.count : 0;
                    for (uint32_t variant = 0; variant < mapping.variants.count; ++variant) {
                        if (!valid_required_index(mapping.variants.data[variant], variantCount))
                            return AnoGltfResult::invalid_gltf;
                        for (uint32_t previousMapping = 0;
                             previousMapping <= mappingIndex; ++previousMapping) {
                            const AnoGltfMaterialVariantMapping& previous =
                                mappings.data[previousMapping];
                            const uint32_t limit = previousMapping == mappingIndex
                                ? variant : previous.variants.count;
                            for (uint32_t previousVariant = 0;
                                 previousVariant < limit; ++previousVariant) {
                                if (previous.variants.data[previousVariant].value
                                    == mapping.variants.data[variant].value)
                                    return AnoGltfResult::invalid_gltf;
                            }
                        }
                    }
                }
            }
        }
        if ((mesh.weights.count != 0 && mesh.weights.count != targetCount)
            || (mesh.extras.targetNames.count != 0 && mesh.extras.targetNames.count != targetCount))
            return AnoGltfResult::invalid_gltf;
    }

    for (uint32_t i = 0; i < root.skins.count; ++i) {
        const AnoGltfSkin& skin = root.skins.data[i];
        if (skin.joints.count == 0 || !valid_optional_index(skin.skeleton, root.nodes.count)
            || !valid_optional_index(skin.inverseBindMatrices, root.accessors.count))
            return AnoGltfResult::invalid_gltf;
        for (uint32_t joint = 0; joint < skin.joints.count; ++joint) {
            if (!valid_required_index(skin.joints.data[joint], root.nodes.count))
                return AnoGltfResult::invalid_gltf;
            for (uint32_t previous = 0; previous < joint; ++previous) {
                if (skin.joints.data[joint].value == skin.joints.data[previous].value)
                    return AnoGltfResult::invalid_gltf;
            }
        }
        if (ano_gltf_has_index(skin.inverseBindMatrices)) {
            const AnoGltfAccessor& matrices =
                root.accessors.data[skin.inverseBindMatrices.value];
            if (matrices.type != AnoGltfAccessorType::mat4
                || matrices.componentType != AnoGltfComponentType::float_
                || matrices.count < skin.joints.count)
                return AnoGltfResult::invalid_gltf;
        }
    }

    for (uint32_t i = 0; i < root.cameras.count; ++i) {
        const AnoGltfCamera& camera = root.cameras.data[i];
        using enum AnoGltfCameraType;
        switch (camera.type) {
        case perspective:
            if (camera.perspective.yfov <= 0.0f || camera.perspective.znear <= 0.0f
                || (camera.perspective.aspectRatio.present && camera.perspective.aspectRatio.value <= 0.0f)
                || (camera.perspective.zfar.present
                    && camera.perspective.zfar.value <= camera.perspective.znear))
                return AnoGltfResult::invalid_gltf;
            break;
        case orthographic:
            if (camera.orthographic.xmag == 0.0f || camera.orthographic.ymag == 0.0f
                || camera.orthographic.znear < 0.0f
                || camera.orthographic.zfar <= camera.orthographic.znear)
                return AnoGltfResult::invalid_gltf;
            break;
        case invalid:
            return AnoGltfResult::invalid_gltf;
        }
    }

    const AnoGltfOptional<AnoGltfRootLights>& rootLights =
        root.extensions.known.KHR_lights_punctual;
    if (rootLights.present) {
        for (uint32_t i = 0; i < rootLights.value.lights.count; ++i) {
            const AnoGltfLight& light = rootLights.value.lights.data[i];
            using enum AnoGltfLightType;
            switch (light.type) {
            case directional:
            case point:
                break;
            case spot:
                if (light.spot.innerConeAngle < 0.0f
                    || light.spot.outerConeAngle <= light.spot.innerConeAngle
                    || light.spot.outerConeAngle > 1.5707963267948966f)
                    return AnoGltfResult::invalid_gltf;
                break;
            case invalid:
                return AnoGltfResult::invalid_gltf;
            }
            if (light.intensity < 0.0f || (light.range.present && light.range.value <= 0.0f))
                return AnoGltfResult::invalid_gltf;
        }
    }

    for (uint32_t i = 0; i < root.nodes.count; ++i) {
        const AnoGltfNode& node = root.nodes.data[i];
        if (!valid_optional_index(node.camera, root.cameras.count)
            || !valid_optional_index(node.skin, root.skins.count)
            || !valid_optional_index(node.mesh, root.meshes.count)
            || (ano_gltf_has_index(node.skin) && !ano_gltf_has_index(node.mesh))
            || (node.matrix.present && (node.rotation.present || node.scale.present || node.translation.present)))
            return AnoGltfResult::invalid_gltf;
        if (node.rotation.present) {
            for (float component : node.rotation.value.values) {
                if (component < -1.0f || component > 1.0f)
                    return AnoGltfResult::invalid_gltf;
            }
        }
        if (node.weights.count != 0) {
            if (!ano_gltf_has_index(node.mesh))
                return AnoGltfResult::invalid_gltf;
            const AnoGltfMesh& mesh = root.meshes.data[node.mesh.value];
            if (mesh.primitives.count != 0
                && node.weights.count != mesh.primitives.data[0].targets.count)
                return AnoGltfResult::invalid_gltf;
        }
        if (node.extensions.known.KHR_lights_punctual.present) {
            const uint32_t lightCount = rootLights.present ? rootLights.value.lights.count : 0;
            if (!valid_required_index(node.extensions.known.KHR_lights_punctual.value.light, lightCount))
                return AnoGltfResult::invalid_gltf;
        }
        if (node.extensions.known.EXT_mesh_gpu_instancing.present) {
            if (!ano_gltf_has_index(node.mesh))
                return AnoGltfResult::invalid_gltf;
            const AnoGltfAttributeMap& attributes =
                node.extensions.known.EXT_mesh_gpu_instancing.value.attributes;
            if (attributes.values.count == 0)
                return AnoGltfResult::invalid_gltf;
            const AnoGltfResult result = validate_attribute_map(
                root, attributes, AttributeDomain::instancing, meshQuantized);
            if (result != AnoGltfResult::success)
                return result;
            const uint64_t instanceCount =
                root.accessors.data[attributes.values.data[0].accessor.value].count;
            for (uint32_t attribute = 1; attribute < attributes.values.count; ++attribute) {
                if (root.accessors.data[attributes.values.data[attribute].accessor.value].count
                    != instanceCount)
                    return AnoGltfResult::invalid_gltf;
            }
        }
    }

    for (uint32_t i = 0; i < root.scenes.count; ++i) {
        const AnoGltfScene& scene = root.scenes.data[i];
        for (uint32_t node = 0; node < scene.nodes.count; ++node) {
            if (!valid_required_index(scene.nodes.data[node], root.nodes.count)
                || ano_gltf_has_index(root.nodes.data[scene.nodes.data[node].value].parent))
                return AnoGltfResult::invalid_gltf;
            for (uint32_t previous = 0; previous < node; ++previous) {
                if (scene.nodes.data[previous].value == scene.nodes.data[node].value)
                    return AnoGltfResult::invalid_gltf;
            }
        }
    }
    if (!valid_optional_index(root.scene, root.scenes.count))
        return AnoGltfResult::invalid_gltf;

    for (uint32_t animationIndex = 0; animationIndex < root.animations.count; ++animationIndex) {
        const AnoGltfAnimation& animation = root.animations.data[animationIndex];
        if (animation.samplers.count == 0 || animation.channels.count == 0)
            return AnoGltfResult::invalid_gltf;
        for (uint32_t samplerIndex = 0; samplerIndex < animation.samplers.count; ++samplerIndex) {
            const AnoGltfAnimationSampler& sampler = animation.samplers.data[samplerIndex];
            if (!valid_required_index(sampler.input, root.accessors.count)
                || !valid_required_index(sampler.output, root.accessors.count)
                || !enum_value_valid(sampler.interpolation))
                return AnoGltfResult::invalid_gltf;
            const AnoGltfAccessor& inputAccessor = root.accessors.data[sampler.input.value];
            if (inputAccessor.type != AnoGltfAccessorType::scalar
                || inputAccessor.componentType != AnoGltfComponentType::float_
                || inputAccessor.min.count != 1 || inputAccessor.max.count != 1
                || inputAccessor.min.values[0] < 0.0
                || (sampler.interpolation == AnoGltfInterpolation::cubic_spline
                    && inputAccessor.count < 2))
                return AnoGltfResult::invalid_gltf;
        }
        for (uint32_t channelIndex = 0; channelIndex < animation.channels.count; ++channelIndex) {
            const AnoGltfAnimationChannel& channel = animation.channels.data[channelIndex];
            if (!valid_required_index(channel.sampler, animation.samplers.count)
                || !valid_optional_index(channel.target.node, root.nodes.count)
                || !enum_value_valid(channel.target.path))
                return AnoGltfResult::invalid_gltf;
            if (!ano_gltf_has_index(channel.target.node))
                continue;
            for (uint32_t previous = 0; previous < channelIndex; ++previous) {
                const AnoGltfAnimationTarget& target =
                    animation.channels.data[previous].target;
                if (ano_gltf_has_index(target.node)
                    && target.node.value == channel.target.node.value
                    && target.path == channel.target.path)
                    return AnoGltfResult::invalid_gltf;
            }
            const AnoGltfAnimationSampler& sampler = animation.samplers.data[channel.sampler.value];
            const AnoGltfAccessor& input = root.accessors.data[sampler.input.value];
            const AnoGltfAccessor& output = root.accessors.data[sampler.output.value];
            const AnoGltfNode& targetNode = root.nodes.data[channel.target.node.value];
            if (!animation_output_valid(channel.target.path, output) || targetNode.matrix.present)
                return AnoGltfResult::invalid_gltf;
            uint64_t components = 1;
            if (channel.target.path == AnoGltfAnimationPath::weights) {
                if (!ano_gltf_has_index(targetNode.mesh)
                    || root.meshes.data[targetNode.mesh.value].primitives.count == 0)
                    return AnoGltfResult::invalid_gltf;
                components =
                    root.meshes.data[targetNode.mesh.value].primitives.data[0].targets.count;
            }
            const uint64_t values = sampler.interpolation == AnoGltfInterpolation::cubic_spline ? 3 : 1;
            uint64_t expected = 0;
            if (!checked_mul_u64(input.count, components, &expected)
                || !checked_mul_u64(expected, values, &expected)
                || output.count != expected)
                return AnoGltfResult::invalid_gltf;
        }
    }
    if (root.extensions.known.KHR_materials_variants.present) {
        const AnoGltfArray<AnoGltfMaterialVariant>& variants =
            root.extensions.known.KHR_materials_variants.value.variants;
        for (uint32_t i = 0; i < variants.count; ++i) {
            if (variants.data[i].name.length == 0)
                return AnoGltfResult::invalid_gltf;
        }
    }
    return AnoGltfResult::success;
}

static AnoGltfResult parse(
    const void* bytes, size_t byteCount, const AnoGltfOptions* sourceOptions, AnoGltfData** outData)
{
    if (!outData)
        return AnoGltfResult::invalid_options;
    *outData = nullptr;
    if (!bytes || byteCount > static_cast<size_t>(PTRDIFF_MAX))
        return bytes ? AnoGltfResult::limit_exceeded : AnoGltfResult::data_too_short;

    AnoGltfOptions options{};
    const AnoGltfResult optionsResult = resolve_options(sourceOptions, &options);
    if (optionsResult != AnoGltfResult::success)
        return optionsResult;

    Input input{};
    AnoGltfResult result = split_input(bytes, byteCount, &input);
    if (result != AnoGltfResult::success)
        return result;
    if (input.jsonSize > static_cast<size_t>(PTRDIFF_MAX))
        return AnoGltfResult::limit_exceeded;

    TokenParser countParser{};
    const int32_t counted = tokenize(&countParser, input.json, input.jsonSize, nullptr, 0);
    if (counted < 1)
        return AnoGltfResult::invalid_json;
    if (static_cast<uint32_t>(counted) > options.maxJsonTokens)
        return AnoGltfResult::limit_exceeded;
    const uint32_t tokenCount = static_cast<uint32_t>(counted);
    size_t scratchBytes = 0;
    if (!checked_mul(tokenCount, sizeof(Token), &scratchBytes))
        return AnoGltfResult::limit_exceeded;
    Token* tokens = static_cast<Token*>(options.allocate(options.user, scratchBytes));
    if (!tokens)
        return AnoGltfResult::out_of_memory;
    memset(tokens, 0, scratchBytes);

    TokenParser parser{};
    const int32_t parsed = tokenize(&parser, input.json, input.jsonSize, tokens, tokenCount);
    if (parsed != counted || parser.next != tokenCount || tokens[0].type != TokenType::object) {
        options.free(options.user, tokens);
        return AnoGltfResult::invalid_json;
    }
    result = finalize_tokens(tokens, tokenCount, options.maxNesting);
    if (result != AnoGltfResult::success) {
        options.free(options.user, tokens);
        return result;
    }

    Arena planner{nullptr, SIZE_MAX, sizeof(AnoGltfData), false};
    result = decode_object<false, RootSchema>(input.json, tokens, 0, nullptr, &planner);
    if (result != AnoGltfResult::success || planner.failed) {
        options.free(options.user, tokens);
        return planner.failed ? AnoGltfResult::limit_exceeded : result;
    }
    uint8_t* arenaMemory = static_cast<uint8_t*>(options.allocate(options.user, planner.at));
    if (!arenaMemory) {
        options.free(options.user, tokens);
        return AnoGltfResult::out_of_memory;
    }
    memset(arenaMemory, 0, planner.at);

    Arena arena{arenaMemory, planner.at, sizeof(AnoGltfData), false};
    RootSchema root{};
    result = decode_object<true, RootSchema>(input.json, tokens, 0, &root, &arena);
    if (result == AnoGltfResult::success && !arena.failed && arena.at == planner.at) {
        result = derive_node_parents(&root, reinterpret_cast<uint8_t*>(tokens));
        if (result == AnoGltfResult::success)
            result = validate_schema(input, root);
    } else if (result == AnoGltfResult::success) {
        result = AnoGltfResult::invalid_gltf;
    }
    if (result != AnoGltfResult::success) {
        options.free(options.user, arenaMemory);
        options.free(options.user, tokens);
        return result;
    }

    AnoGltfData* data = reinterpret_cast<AnoGltfData*>(arenaMemory);
    *data = {};
    data->fileType = input.fileType;
    data->asset = root.asset;
    data->meshes = root.meshes.data;
    data->meshesCount = root.meshes.count;
    data->materials = root.materials.data;
    data->materialsCount = root.materials.count;
    data->buffers = root.buffers.data;
    data->buffersCount = root.buffers.count;
    data->bufferViews = root.bufferViews.data;
    data->bufferViewsCount = root.bufferViews.count;
    data->accessors = root.accessors.data;
    data->accessorsCount = root.accessors.count;
    data->images = root.images.data;
    data->imagesCount = root.images.count;
    data->textures = root.textures.data;
    data->texturesCount = root.textures.count;
    data->samplers = root.samplers.data;
    data->samplersCount = root.samplers.count;
    data->skins = root.skins.data;
    data->skinsCount = root.skins.count;
    data->cameras = root.cameras.data;
    data->camerasCount = root.cameras.count;
    data->nodes = root.nodes.data;
    data->nodesCount = root.nodes.count;
    data->scenes = root.scenes.data;
    data->scenesCount = root.scenes.count;
    data->scene = root.scene;
    data->animations = root.animations.data;
    data->animationsCount = root.animations.count;
    if (root.extensions.known.KHR_lights_punctual.present) {
        data->lights = root.extensions.known.KHR_lights_punctual.value.lights.data;
        data->lightsCount = root.extensions.known.KHR_lights_punctual.value.lights.count;
    }
    if (root.extensions.known.KHR_materials_variants.present) {
        data->variants = root.extensions.known.KHR_materials_variants.value.variants.data;
        data->variantsCount = root.extensions.known.KHR_materials_variants.value.variants.count;
    }
    data->extensionsUsed = root.extensionsUsed;
    data->extensionsRequired = root.extensionsRequired;
    data->extras = root.extras;
    data->extensions = root.extensions;
    data->glbBin = input.bin;
    data->glbBinSize = input.binSize;
    data->arenaSize = planner.at;
    data->jsonTokenCount = tokenCount;
    data->arenaFree = options.free;
    data->allocatorUser = options.user;
    options.free(options.user, tokens);
    *outData = data;
    return AnoGltfResult::success;
}

static int uri_hex_value(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    return -1;
}

static size_t decode_string(char* string)
{
    if (!string)
        return 0;
    const size_t length = strlen(string);
    size_t written = 0;
    if (!decode_json_string(string, length, string, length, &written)) {
        string[0] = '\0';
        return 0;
    }
    string[written] = '\0';
    return written;
}

static size_t decode_uri(char* uri)
{
    if (!uri)
        return 0;
    char* read = uri;
    char* write = uri;
    while (*read) {
        if (*read == '%' && read[1] && read[2]) {
            const int high = uri_hex_value(read[1]);
            const int low = uri_hex_value(read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = static_cast<char>(high * 16 + low);
                read += 3;
                continue;
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
    return static_cast<size_t>(write - uri);
}

static bool string_starts_with_literal(
    const AnoGltfString& string, const char* literal, size_t literalLength)
{
    return string.length >= literalLength
        && memcmp(string.data, literal, literalLength) == 0;
}

static bool string_has_scheme(const AnoGltfString& string)
{
    if (string.length < 3)
        return false;
    for (uint32_t i = 0; i + 2 < string.length; ++i) {
        if (string.data[i] == ':' && string.data[i + 1] == '/' && string.data[i + 2] == '/')
            return true;
    }
    return false;
}

static int base64_value(char character)
{
    if (character >= 'A' && character <= 'Z')
        return character - 'A';
    if (character >= 'a' && character <= 'z')
        return character - 'a' + 26;
    if (character >= '0' && character <= '9')
        return character - '0' + 52;
    if (character == '+')
        return 62;
    if (character == '/')
        return 63;
    return -1;
}

static AnoGltfResult decode_base64(
    const char* encoded, size_t encodedLength, uint8_t* output, size_t outputLength)
{
    if ((!encoded && encodedLength != 0) || (!output && outputLength != 0))
        return AnoGltfResult::invalid_options;
    size_t read = 0;
    size_t written = 0;
    uint32_t accumulator = 0;
    uint32_t bits = 0;
    bool padding = false;
    while (read < encodedLength) {
        const char character = encoded[read++];
        if (character == '=') {
            padding = true;
            continue;
        }
        if (padding)
            return AnoGltfResult::io_error;
        const int value = base64_value(character);
        if (value < 0)
            return AnoGltfResult::io_error;
        accumulator = accumulator << 6 | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written >= outputLength)
                return AnoGltfResult::io_error;
            output[written++] = static_cast<uint8_t>(accumulator >> bits);
            if (bits == 0)
                accumulator = 0;
            else
                accumulator &= (1u << bits) - 1u;
        }
    }
    if (written != outputLength || bits >= 6 || (bits != 0 && accumulator != 0))
        return AnoGltfResult::io_error;
    return AnoGltfResult::success;
}

static size_t path_prefix_length(const char* path)
{
    if (!path)
        return 0;
    const char* last = nullptr;
    for (const char* at = path; *at; ++at) {
        if (*at == '/' || *at == '\\')
            last = at;
    }
    return last ? static_cast<size_t>(last - path) + 1 : 0;
}

static void clear_storage_bindings(AnoGltfData* data, const void* storage, size_t storageSize)
{
    const uintptr_t begin = reinterpret_cast<uintptr_t>(storage);
    const uintptr_t end = begin + storageSize;
    for (uint32_t i = 0; i < data->buffersCount; ++i) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(data->buffers[i].data);
        if (address >= begin && address < end) {
            data->buffers[i].data = nullptr;
            data->buffers[i].dataSize = 0;
        }
    }
}

static AnoGltfResult bind_buffer(
    AnoGltfData* data, AnoGltfBufferIndex buffer, const void* bytes, size_t byteCount)
{
    if (!data || !ano_gltf_has_index(buffer) || buffer.value >= data->buffersCount)
        return AnoGltfResult::invalid_options;
    AnoGltfBuffer& destination = data->buffers[buffer.value];
    if ((!bytes && byteCount != 0) || byteCount < destination.byteLength)
        return AnoGltfResult::data_too_short;
    destination.data = static_cast<const uint8_t*>(bytes);
    destination.dataSize = byteCount;
    return AnoGltfResult::success;
}

static AnoGltfResult bind_buffer_view(
    AnoGltfData* data, AnoGltfBufferViewIndex view, const void* bytes, size_t byteCount)
{
    if (!data || !ano_gltf_has_index(view) || view.value >= data->bufferViewsCount)
        return AnoGltfResult::invalid_options;
    AnoGltfBufferView& destination = data->bufferViews[view.value];
    if ((!bytes && byteCount != 0) || byteCount < destination.byteLength)
        return AnoGltfResult::data_too_short;
    destination.decodedData = static_cast<const uint8_t*>(bytes);
    destination.decodedDataSize = byteCount;
    return AnoGltfResult::success;
}

static AnoGltfResult parse_file(
    const char* path, const AnoGltfOptions* sourceOptions, AnoGltfData** outData)
{
    if (!outData)
        return AnoGltfResult::invalid_options;
    *outData = nullptr;
    AnoGltfOptions options{};
    const AnoGltfResult optionsResult = resolve_options(sourceOptions, &options);
    if (optionsResult != AnoGltfResult::success)
        return optionsResult;
    if (!path || !options.fileSize || !options.fileRead)
        return AnoGltfResult::invalid_options;
    size_t byteCount = 0;
    AnoGltfResult result = options.fileSize(options.fileUser, path, &byteCount);
    if (result != AnoGltfResult::success)
        return result;
    if (byteCount == 0)
        return AnoGltfResult::data_too_short;
    void* bytes = options.allocate(options.user, byteCount);
    if (!bytes)
        return AnoGltfResult::out_of_memory;
    result = options.fileRead(options.fileUser, path, bytes, byteCount);
    if (result == AnoGltfResult::success)
        result = parse(bytes, byteCount, &options, outData);
    if (result != AnoGltfResult::success) {
        options.free(options.user, bytes);
        return result;
    }
    (*outData)->sourceAllocation = bytes;
    (*outData)->sourceAllocationSize = byteCount;
    (*outData)->sourceFree = options.free;
    (*outData)->sourceAllocatorUser = options.user;
    return AnoGltfResult::success;
}

static AnoGltfResult validate_loaded_data(const AnoGltfData* data);

static AnoGltfResult load_buffers(
    AnoGltfData* data, const char* gltfPath, const AnoGltfOptions* sourceOptions)
{
    if (!data || data->bufferStorage)
        return AnoGltfResult::invalid_options;
    AnoGltfOptions options{};
    const AnoGltfResult optionsResult = resolve_options(sourceOptions, &options);
    if (optionsResult != AnoGltfResult::success)
        return optionsResult;

    size_t payloadBytes = 0;
    size_t pathBytes = 0;
    const size_t prefixLength = path_prefix_length(gltfPath);
    for (uint32_t i = 0; i < data->buffersCount; ++i) {
        const AnoGltfBuffer& buffer = data->buffers[i];
        if (buffer.data)
            continue;
        if (buffer.uri.length == 0)
            continue;
        if (buffer.byteLength > SIZE_MAX
            || !checked_add(payloadBytes, static_cast<size_t>(buffer.byteLength), &payloadBytes))
            return AnoGltfResult::limit_exceeded;
        if (!string_starts_with_literal(buffer.uri, "data:", 5)) {
            if (string_has_scheme(buffer.uri))
                return AnoGltfResult::unknown_format;
            size_t requiredPath = 0;
            if (!gltfPath
                || !checked_add(prefixLength, buffer.uri.length, &requiredPath)
                || !checked_add(requiredPath, 1, &requiredPath))
                return gltfPath ? AnoGltfResult::limit_exceeded : AnoGltfResult::unknown_format;
            if (requiredPath > pathBytes)
                pathBytes = requiredPath;
        }
    }
    if (pathBytes != 0 && !options.fileRead)
        return AnoGltfResult::invalid_options;
    size_t allocationSize = 0;
    if (!checked_add(payloadBytes, pathBytes, &allocationSize))
        return AnoGltfResult::limit_exceeded;
    uint8_t* storage = nullptr;
    if (allocationSize != 0) {
        storage = static_cast<uint8_t*>(options.allocate(options.user, allocationSize));
        if (!storage)
            return AnoGltfResult::out_of_memory;
    }
    uint8_t* destination = storage;
    char* pathScratch = storage ? reinterpret_cast<char*>(storage + payloadBytes) : nullptr;
    AnoGltfResult result = AnoGltfResult::success;

    if (data->buffersCount != 0 && !data->buffers[0].data
        && data->buffers[0].uri.length == 0 && data->glbBin) {
        if (data->glbBinSize < data->buffers[0].byteLength)
            result = AnoGltfResult::data_too_short;
        else {
            data->buffers[0].data = data->glbBin;
            data->buffers[0].dataSize = data->glbBinSize;
        }
    }

    for (uint32_t i = 0; result == AnoGltfResult::success && i < data->buffersCount; ++i) {
        AnoGltfBuffer& buffer = data->buffers[i];
        if (buffer.data || buffer.uri.length == 0)
            continue;
        const size_t byteLength = static_cast<size_t>(buffer.byteLength);
        if (string_starts_with_literal(buffer.uri, "data:", 5)) {
            uint32_t comma = 5;
            while (comma < buffer.uri.length && buffer.uri.data[comma] != ',')
                ++comma;
            if (comma == buffer.uri.length || comma < 7
                || memcmp(buffer.uri.data + comma - 7, ";base64", 7) != 0)
                result = AnoGltfResult::unknown_format;
            else
                result = decode_base64(
                    buffer.uri.data + comma + 1, buffer.uri.length - comma - 1,
                    destination, byteLength);
        } else {
            if (prefixLength)
                memcpy(pathScratch, gltfPath, prefixLength);
            memcpy(pathScratch + prefixLength, buffer.uri.data, buffer.uri.length);
            pathScratch[prefixLength + buffer.uri.length] = '\0';
            const size_t decodedLength = decode_uri(pathScratch + prefixLength);
            if (memchr(pathScratch + prefixLength, '\0', decodedLength))
                result = AnoGltfResult::invalid_gltf;
            else
                result = options.fileRead(
                    options.fileUser, pathScratch, destination, byteLength);
        }
        if (result == AnoGltfResult::success) {
            buffer.data = destination;
            buffer.dataSize = byteLength;
            destination += byteLength;
        }
    }
    if (result != AnoGltfResult::success) {
        if (storage) {
            clear_storage_bindings(data, storage, allocationSize);
            options.free(options.user, storage);
        }
        return result;
    }
    data->bufferStorage = storage;
    data->bufferStorageSize = allocationSize;
    data->bufferStorageFree = storage ? options.free : nullptr;
    data->bufferStorageUser = options.user;
    result = validate_loaded_data(data);
    if (result != AnoGltfResult::success && storage) {
        clear_storage_bindings(data, storage, allocationSize);
        options.free(options.user, storage);
        data->bufferStorage = nullptr;
        data->bufferStorageSize = 0;
        data->bufferStorageFree = nullptr;
        data->bufferStorageUser = nullptr;
    }
    return result;
}

static const uint8_t* buffer_view_data(
    const AnoGltfData* data, AnoGltfBufferViewIndex viewIndex)
{
    if (!data || !ano_gltf_has_index(viewIndex) || viewIndex.value >= data->bufferViewsCount)
        return nullptr;
    const AnoGltfBufferView& view = data->bufferViews[viewIndex.value];
    if (view.decodedData)
        return view.decodedDataSize >= view.byteLength ? view.decodedData : nullptr;
    if (!ano_gltf_has_index(view.buffer) || view.buffer.value >= data->buffersCount)
        return nullptr;
    const AnoGltfBuffer& buffer = data->buffers[view.buffer.value];
    uint64_t end = 0;
    if (!buffer.data || !checked_add_u64(view.byteOffset, view.byteLength, &end)
        || end > buffer.dataSize)
        return nullptr;
    return buffer.data + view.byteOffset;
}

static uint32_t accessor_stride(const AnoGltfData* data, const AnoGltfAccessor* accessor)
{
    const uint32_t packed = element_size(accessor->componentType, accessor->type);
    if (!ano_gltf_has_index(accessor->bufferView))
        return packed;
    if (!data || accessor->bufferView.value >= data->bufferViewsCount)
        return 0;
    const uint32_t stride = data->bufferViews[accessor->bufferView.value].byteStride;
    return stride ? stride : packed;
}

template<class T>
static T read_unaligned(const uint8_t* bytes)
{
    T value{};
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static int64_t read_component_integer(const uint8_t* bytes, AnoGltfComponentType type)
{
    using enum AnoGltfComponentType;
    switch (type) {
    case byte: return read_unaligned<int8_t>(bytes);
    case unsigned_byte: return read_unaligned<uint8_t>(bytes);
    case short_: return read_unaligned<int16_t>(bytes);
    case unsigned_short: return read_unaligned<uint16_t>(bytes);
    case unsigned_int: return read_unaligned<uint32_t>(bytes);
    case float_:
    case invalid:
        return 0;
    }
    return 0;
}

static uint32_t read_component_index(const uint8_t* bytes, AnoGltfComponentType type)
{
    using enum AnoGltfComponentType;
    switch (type) {
    case unsigned_byte: return read_unaligned<uint8_t>(bytes);
    case unsigned_short: return read_unaligned<uint16_t>(bytes);
    case unsigned_int: return read_unaligned<uint32_t>(bytes);
    case byte:
    case short_:
    case float_:
    case invalid:
        return 0;
    }
    return 0;
}

static float read_component_float(
    const uint8_t* bytes, AnoGltfComponentType type, bool normalized)
{
    using enum AnoGltfComponentType;
    if (type == float_)
        return read_unaligned<float>(bytes);
    if (!normalized)
        return static_cast<float>(read_component_integer(bytes, type));
    switch (type) {
    case byte: {
        const int8_t value = read_unaligned<int8_t>(bytes);
        return value == INT8_MIN ? -1.0f : static_cast<float>(value) / 127.0f;
    }
    case unsigned_byte:
        return static_cast<float>(read_unaligned<uint8_t>(bytes)) / 255.0f;
    case short_: {
        const int16_t value = read_unaligned<int16_t>(bytes);
        return value == INT16_MIN ? -1.0f : static_cast<float>(value) / 32767.0f;
    }
    case unsigned_short:
        return static_cast<float>(read_unaligned<uint16_t>(bytes)) / 65535.0f;
    case unsigned_int:
    case float_:
    case invalid:
        return 0.0f;
    }
    return 0.0f;
}

static bool read_element_float(
    const uint8_t* element, AnoGltfAccessorType type, AnoGltfComponentType componentType,
    bool normalized, float* output, size_t outputCount)
{
    const uint32_t count = component_count(type);
    const uint32_t scalarSize = component_size(componentType);
    if (!element || !output || count == 0 || scalarSize == 0 || outputCount < count)
        return false;
    if (type == AnoGltfAccessorType::mat2 && scalarSize == 1) {
        static constexpr uint8_t offsets[] = {0, 1, 4, 5};
        for (uint32_t i = 0; i < 4; ++i)
            output[i] = read_component_float(element + offsets[i], componentType, normalized);
        return true;
    }
    if (type == AnoGltfAccessorType::mat3 && scalarSize == 1) {
        static constexpr uint8_t offsets[] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
        for (uint32_t i = 0; i < 9; ++i)
            output[i] = read_component_float(element + offsets[i], componentType, normalized);
        return true;
    }
    if (type == AnoGltfAccessorType::mat3 && scalarSize == 2) {
        static constexpr uint8_t offsets[] = {0, 2, 4, 8, 10, 12, 16, 18, 20};
        for (uint32_t i = 0; i < 9; ++i)
            output[i] = read_component_float(element + offsets[i], componentType, normalized);
        return true;
    }
    for (uint32_t i = 0; i < count; ++i)
        output[i] = read_component_float(element + scalarSize * i, componentType, normalized);
    return true;
}

static bool read_element_uint(
    const uint8_t* element, AnoGltfAccessorType type, AnoGltfComponentType componentType,
    uint32_t* output, size_t outputCount)
{
    const uint32_t count = component_count(type);
    const uint32_t scalarSize = component_size(componentType);
    if (!element || !output || count == 0 || scalarSize == 0 || outputCount < count
        || type == AnoGltfAccessorType::mat2 || type == AnoGltfAccessorType::mat3
        || type == AnoGltfAccessorType::mat4 || componentType == AnoGltfComponentType::float_)
        return false;
    for (uint32_t i = 0; i < count; ++i)
        output[i] = static_cast<uint32_t>(read_component_integer(element + scalarSize * i, componentType));
    return true;
}

static bool accessor_base_element(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, const uint8_t** output)
{
    *output = nullptr;
    if (!ano_gltf_has_index(accessor->bufferView))
        return true;
    if (accessor->bufferView.value >= data->bufferViewsCount)
        return false;
    const AnoGltfBufferView& view = data->bufferViews[accessor->bufferView.value];
    const uint8_t* bytes = buffer_view_data(data, accessor->bufferView);
    const uint32_t stride = accessor_stride(data, accessor);
    const uint32_t itemSize = element_size(accessor->componentType, accessor->type);
    uint64_t offset = 0;
    uint64_t end = 0;
    if (!bytes || stride == 0
        || !checked_mul_u64(index, stride, &offset)
        || !checked_add_u64(offset, accessor->byteOffset, &offset)
        || !checked_add_u64(offset, itemSize, &end)
        || end > view.byteLength)
        return false;
    *output = bytes + offset;
    return true;
}

static bool sparse_value(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t needle, const uint8_t** output)
{
    *output = nullptr;
    const AnoGltfAccessorSparse& sparse = accessor->sparse;
    if (sparse.count == 0)
        return true;
    const uint8_t* indices = buffer_view_data(data, sparse.indices.bufferView);
    const uint8_t* values = buffer_view_data(data, sparse.values.bufferView);
    const uint32_t indexStride = component_size(sparse.indices.componentType);
    const uint32_t valueStride = element_size(accessor->componentType, accessor->type);
    if (!indices || !values || indexStride == 0 || valueStride == 0)
        return false;
    indices += sparse.indices.byteOffset;
    values += sparse.values.byteOffset;
    uint64_t first = 0;
    uint64_t count = sparse.count;
    while (count != 0) {
        const uint64_t step = count / 2;
        const uint64_t middle = first + step;
        const uint32_t value = read_component_index(
            indices + middle * indexStride, sparse.indices.componentType);
        if (value < needle) {
            first = middle + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    if (first < sparse.count) {
        const uint32_t value = read_component_index(
            indices + first * indexStride, sparse.indices.componentType);
        if (value == needle)
            *output = values + first * valueStride;
    }
    return true;
}

static bool accessor_read_float(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, float* output, size_t outputCount)
{
    if (!data || !accessor || !output || index >= accessor->count)
        return false;
    const uint8_t* sparse = nullptr;
    if (!sparse_value(data, accessor, index, &sparse))
        return false;
    if (sparse)
        return read_element_float(
            sparse, accessor->type, accessor->componentType,
            accessor->normalized, output, outputCount);
    const uint8_t* base = nullptr;
    if (!accessor_base_element(data, accessor, index, &base))
        return false;
    if (!base) {
        const uint32_t count = component_count(accessor->type);
        if (outputCount < count)
            return false;
        memset(output, 0, count * sizeof(float));
        return true;
    }
    return read_element_float(
        base, accessor->type, accessor->componentType,
        accessor->normalized, output, outputCount);
}

static bool accessor_read_uint(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output, size_t outputCount)
{
    if (!data || !accessor || !output || index >= accessor->count)
        return false;
    const uint8_t* sparse = nullptr;
    if (!sparse_value(data, accessor, index, &sparse))
        return false;
    if (sparse)
        return read_element_uint(sparse, accessor->type, accessor->componentType, output, outputCount);
    const uint8_t* base = nullptr;
    if (!accessor_base_element(data, accessor, index, &base))
        return false;
    if (!base) {
        const uint32_t count = component_count(accessor->type);
        if (outputCount < count)
            return false;
        memset(output, 0, count * sizeof(uint32_t));
        return true;
    }
    return read_element_uint(base, accessor->type, accessor->componentType, output, outputCount);
}

static bool accessor_read_index(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output)
{
    if (!output || !accessor || accessor->type != AnoGltfAccessorType::scalar
        || (accessor->componentType != AnoGltfComponentType::unsigned_byte
            && accessor->componentType != AnoGltfComponentType::unsigned_short
            && accessor->componentType != AnoGltfComponentType::unsigned_int))
        return false;
    uint32_t value = 0;
    if (!accessor_read_uint(data, accessor, index, &value, 1))
        return false;
    *output = value;
    return true;
}

static uint64_t accessor_unpack_floats(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    float* output, uint64_t outputCount)
{
    if (!accessor)
        return 0;
    const uint32_t perElement = component_count(accessor->type);
    uint64_t available = 0;
    if (perElement == 0 || !checked_mul_u64(accessor->count, perElement, &available))
        return 0;
    if (!output)
        return available;
    if (outputCount > available)
        outputCount = available;
    const uint64_t elements = outputCount / perElement;
    for (uint64_t i = 0; i < elements; ++i) {
        if (!accessor_read_float(data, accessor, i, output + i * perElement, perElement))
            return 0;
    }
    return elements * perElement;
}

static uint64_t accessor_unpack_indices(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    void* output, size_t outputComponentSize, uint64_t outputCount)
{
    if (!accessor || accessor->type != AnoGltfAccessorType::scalar)
        return 0;
    if (!output)
        return accessor->count;
    if ((outputComponentSize != 1 && outputComponentSize != 2 && outputComponentSize != 4)
        || component_size(accessor->componentType) > outputComponentSize)
        return 0;
    if (outputCount > accessor->count)
        outputCount = accessor->count;
    for (uint64_t i = 0; i < outputCount; ++i) {
        uint32_t value = 0;
        if (!accessor_read_index(data, accessor, i, &value))
            return 0;
        switch (outputComponentSize) {
        case 1: static_cast<uint8_t*>(output)[i] = static_cast<uint8_t>(value); break;
        case 2: static_cast<uint16_t*>(output)[i] = static_cast<uint16_t>(value); break;
        case 4: static_cast<uint32_t*>(output)[i] = value; break;
        default: return 0;
        }
    }
    return outputCount;
}

static bool view_uses_meshopt(const AnoGltfBufferView& view)
{
    return view.extensions.known.EXT_meshopt_compression.present
        || view.extensions.known.KHR_meshopt_compression.present;
}

static AnoGltfResult validate_loaded_data(const AnoGltfData* data)
{
    if (!data)
        return AnoGltfResult::invalid_options;
    for (uint32_t i = 0; i < data->buffersCount; ++i) {
        const AnoGltfBuffer& buffer = data->buffers[i];
        if (!buffer.data && buffer_is_meshopt_fallback(buffer))
            continue;
        if (!buffer.data || buffer.dataSize < buffer.byteLength)
            return AnoGltfResult::data_too_short;
    }
    for (uint32_t i = 0; i < data->accessorsCount; ++i) {
        const AnoGltfAccessor& accessor = data->accessors[i];
        if (accessor.sparse.count == 0)
            continue;
        const AnoGltfAccessorSparse& sparse = accessor.sparse;
        const uint8_t* indices = buffer_view_data(data, sparse.indices.bufferView);
        const uint32_t stride = component_size(sparse.indices.componentType);
        if (!indices || stride == 0)
            return AnoGltfResult::data_too_short;
        indices += sparse.indices.byteOffset;
        uint64_t previous = 0;
        for (uint64_t item = 0; item < sparse.count; ++item) {
            const uint32_t index = read_component_index(
                indices + item * stride, sparse.indices.componentType);
            if (index >= accessor.count || (item != 0 && index <= previous))
                return AnoGltfResult::invalid_gltf;
            previous = index;
        }
    }
    for (uint32_t meshIndex = 0; meshIndex < data->meshesCount; ++meshIndex) {
        const AnoGltfMesh& mesh = data->meshes[meshIndex];
        for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.count; ++primitiveIndex) {
            const AnoGltfPrimitive& primitive = mesh.primitives.data[primitiveIndex];
            if (!ano_gltf_has_index(primitive.indices))
                continue;
            const AnoGltfAccessor& indices = data->accessors[primitive.indices.value];
            if (ano_gltf_has_index(indices.bufferView)) {
                const AnoGltfBufferView& view = data->bufferViews[indices.bufferView.value];
                if (view_uses_meshopt(view) && !view.decodedData)
                    continue;
            }
            const uint64_t vertexCount =
                data->accessors[primitive.attributes.values.data[0].accessor.value].count;
            for (uint64_t index = 0; index < indices.count; ++index) {
                uint32_t value = 0;
                if (!accessor_read_index(data, &indices, index, &value))
                    return AnoGltfResult::data_too_short;
                const uint32_t restart = indices.componentType
                        == AnoGltfComponentType::unsigned_byte ? UINT8_MAX
                    : indices.componentType == AnoGltfComponentType::unsigned_short ? UINT16_MAX
                    : UINT32_MAX;
                if (value == restart)
                    return AnoGltfResult::invalid_gltf;
                if (value >= vertexCount)
                    return AnoGltfResult::invalid_gltf;
            }
        }
    }
    for (uint32_t skinIndex = 0; skinIndex < data->skinsCount; ++skinIndex) {
        const AnoGltfSkin& skin = data->skins[skinIndex];
        if (!ano_gltf_has_index(skin.inverseBindMatrices))
            continue;
        const AnoGltfAccessor& matrices = data->accessors[skin.inverseBindMatrices.value];
        for (uint64_t matrixIndex = 0; matrixIndex < matrices.count; ++matrixIndex) {
            float matrix[16];
            if (!accessor_read_float(data, &matrices, matrixIndex, matrix, 16))
                return AnoGltfResult::data_too_short;
            if (matrix[3] != 0.0f || matrix[7] != 0.0f
                || matrix[11] != 0.0f || matrix[15] != 1.0f)
                return AnoGltfResult::invalid_gltf;
        }
    }
    for (uint32_t animationIndex = 0;
         animationIndex < data->animationsCount; ++animationIndex) {
        const AnoGltfAnimation& animation = data->animations[animationIndex];
        for (uint32_t samplerIndex = 0;
             samplerIndex < animation.samplers.count; ++samplerIndex) {
            const AnoGltfAccessor& input =
                data->accessors[animation.samplers.data[samplerIndex].input.value];
            float previous = 0.0f;
            for (uint64_t key = 0; key < input.count; ++key) {
                float timestamp = 0.0f;
                if (!accessor_read_float(data, &input, key, &timestamp, 1))
                    return AnoGltfResult::data_too_short;
                if ((key == 0 && (timestamp < 0.0f
                        || timestamp != static_cast<float>(input.min.values[0])))
                    || (key != 0 && timestamp <= previous))
                    return AnoGltfResult::invalid_gltf;
                previous = timestamp;
            }
            if (previous != static_cast<float>(input.max.values[0]))
                return AnoGltfResult::invalid_gltf;
        }
    }
    return AnoGltfResult::success;
}

static void node_transform_local(const AnoGltfNode* node, float output[16])
{
    if (!node || !output)
        return;
    if (node->matrix.present) {
        memcpy(output, node->matrix.value.values, sizeof(float) * 16);
        return;
    }
    const float tx = node->translation.present ? node->translation.value.values[0] : 0.0f;
    const float ty = node->translation.present ? node->translation.value.values[1] : 0.0f;
    const float tz = node->translation.present ? node->translation.value.values[2] : 0.0f;
    const float qx = node->rotation.present ? node->rotation.value.values[0] : 0.0f;
    const float qy = node->rotation.present ? node->rotation.value.values[1] : 0.0f;
    const float qz = node->rotation.present ? node->rotation.value.values[2] : 0.0f;
    const float qw = node->rotation.present ? node->rotation.value.values[3] : 1.0f;
    const float sx = node->scale.present ? node->scale.value.values[0] : 1.0f;
    const float sy = node->scale.present ? node->scale.value.values[1] : 1.0f;
    const float sz = node->scale.present ? node->scale.value.values[2] : 1.0f;

    output[0] = (1.0f - 2.0f * qy * qy - 2.0f * qz * qz) * sx;
    output[1] = (2.0f * qx * qy + 2.0f * qz * qw) * sx;
    output[2] = (2.0f * qx * qz - 2.0f * qy * qw) * sx;
    output[3] = 0.0f;
    output[4] = (2.0f * qx * qy - 2.0f * qz * qw) * sy;
    output[5] = (1.0f - 2.0f * qx * qx - 2.0f * qz * qz) * sy;
    output[6] = (2.0f * qy * qz + 2.0f * qx * qw) * sy;
    output[7] = 0.0f;
    output[8] = (2.0f * qx * qz + 2.0f * qy * qw) * sz;
    output[9] = (2.0f * qy * qz - 2.0f * qx * qw) * sz;
    output[10] = (1.0f - 2.0f * qx * qx - 2.0f * qy * qy) * sz;
    output[11] = 0.0f;
    output[12] = tx;
    output[13] = ty;
    output[14] = tz;
    output[15] = 1.0f;
}

static bool node_transform_world(
    const AnoGltfData* data, AnoGltfNodeIndex nodeIndex, float output[16])
{
    if (!data || !output || !ano_gltf_has_index(nodeIndex) || nodeIndex.value >= data->nodesCount)
        return false;
    const AnoGltfNode* node = &data->nodes[nodeIndex.value];
    node_transform_local(node, output);
    AnoGltfNodeIndex parent = node->parent;
    uint32_t remaining = data->nodesCount;
    while (ano_gltf_has_index(parent)) {
        if (parent.value >= data->nodesCount || remaining-- == 0)
            return false;
        float parentMatrix[16];
        node_transform_local(&data->nodes[parent.value], parentMatrix);
        for (uint32_t column = 0; column < 4; ++column) {
            const float x = output[column * 4 + 0];
            const float y = output[column * 4 + 1];
            const float z = output[column * 4 + 2];
            output[column * 4 + 0] = x * parentMatrix[0] + y * parentMatrix[4] + z * parentMatrix[8];
            output[column * 4 + 1] = x * parentMatrix[1] + y * parentMatrix[5] + z * parentMatrix[9];
            output[column * 4 + 2] = x * parentMatrix[2] + y * parentMatrix[6] + z * parentMatrix[10];
        }
        output[12] += parentMatrix[12];
        output[13] += parentMatrix[13];
        output[14] += parentMatrix[14];
        parent = data->nodes[parent.value].parent;
    }
    return true;
}

} // namespace anogltf_detail

extern "C" {

AnoGltfResult ano_gltf_parse_memory(
    const void* bytes, size_t byteCount, const AnoGltfOptions* options, AnoGltfData** outData)
{
    return anogltf_detail::parse(bytes, byteCount, options, outData);
}

AnoGltfResult ano_gltf_parse_file(
    const char* path, const AnoGltfOptions* options, AnoGltfData** outData)
{
    return anogltf_detail::parse_file(path, options, outData);
}

AnoGltfResult ano_gltf_load_buffers(
    AnoGltfData* data, const char* gltfPath, const AnoGltfOptions* options)
{
    return anogltf_detail::load_buffers(data, gltfPath, options);
}

AnoGltfResult ano_gltf_load_buffer_base64(
    const AnoGltfOptions* sourceOptions, size_t byteCount, const char* base64, void** outData)
{
    if (!outData || (!base64 && byteCount != 0))
        return AnoGltfResult::invalid_options;
    *outData = nullptr;
    AnoGltfOptions options{};
    const AnoGltfResult optionsResult = anogltf_detail::resolve_options(sourceOptions, &options);
    if (optionsResult != AnoGltfResult::success)
        return optionsResult;
    size_t roundedBytes = 0;
    size_t encodedLimit = 0;
    if (!anogltf_detail::checked_add(byteCount, 2, &roundedBytes)
        || !anogltf_detail::checked_mul(roundedBytes / 3, 4, &encodedLimit))
        return AnoGltfResult::limit_exceeded;
    size_t encodedLength = 0;
    if (base64) {
        while (encodedLength <= encodedLimit && base64[encodedLength] != '\0')
            ++encodedLength;
        if (encodedLength > encodedLimit)
            return AnoGltfResult::io_error;
    }
    const size_t allocationSize = byteCount ? byteCount : 1;
    void* allocation = options.allocate(options.user, allocationSize);
    if (!allocation)
        return AnoGltfResult::out_of_memory;
    const AnoGltfResult result = anogltf_detail::decode_base64(
        base64, encodedLength, static_cast<uint8_t*>(allocation), byteCount);
    if (result != AnoGltfResult::success) {
        options.free(options.user, allocation);
        return result;
    }
    *outData = allocation;
    return AnoGltfResult::success;
}

AnoGltfResult ano_gltf_bind_buffer(
    AnoGltfData* data, AnoGltfBufferIndex buffer, const void* bytes, size_t byteCount)
{
    return anogltf_detail::bind_buffer(data, buffer, bytes, byteCount);
}

AnoGltfResult ano_gltf_bind_buffer_view(
    AnoGltfData* data, AnoGltfBufferViewIndex view, const void* bytes, size_t byteCount)
{
    return anogltf_detail::bind_buffer_view(data, view, bytes, byteCount);
}

AnoGltfResult ano_gltf_validate_loaded_data(const AnoGltfData* data)
{
    return anogltf_detail::validate_loaded_data(data);
}

void ano_gltf_free(AnoGltfData* data)
{
    if (!data)
        return;
    if (data->bufferStorage && data->bufferStorageFree)
        data->bufferStorageFree(data->bufferStorageUser, data->bufferStorage);
    if (data->sourceAllocation && data->sourceFree)
        data->sourceFree(data->sourceAllocatorUser, data->sourceAllocation);
    if (data->arenaFree)
        data->arenaFree(data->allocatorUser, data);
}

size_t ano_gltf_decode_string(char* string)
{
    return anogltf_detail::decode_string(string);
}

size_t ano_gltf_decode_uri(char* uri)
{
    return anogltf_detail::decode_uri(uri);
}

AnoGltfResult ano_gltf_copy_extras_json(
    const AnoGltfExtras* extras, char* destination, size_t* destinationSize)
{
    if (!extras || !destinationSize || (extras->json.length != 0 && !extras->json.data))
        return AnoGltfResult::invalid_options;
    const size_t required = static_cast<size_t>(extras->json.length) + 1;
    if (!destination) {
        *destinationSize = required;
        return AnoGltfResult::success;
    }
    const size_t capacity = *destinationSize;
    *destinationSize = required;
    if (capacity == 0)
        return AnoGltfResult::data_too_short;
    const size_t copied = extras->json.length < capacity - 1
        ? extras->json.length : capacity - 1;
    if (copied != 0)
        memcpy(destination, extras->json.data, copied);
    destination[copied] = '\0';
    return copied == extras->json.length
        ? AnoGltfResult::success : AnoGltfResult::data_too_short;
}

uint32_t ano_gltf_component_count(AnoGltfAccessorType type)
{
    return anogltf_detail::component_count(type);
}

uint32_t ano_gltf_component_size(AnoGltfComponentType type)
{
    return anogltf_detail::component_size(type);
}

uint32_t ano_gltf_element_size(AnoGltfAccessorType type, AnoGltfComponentType componentType)
{
    return anogltf_detail::element_size(componentType, type);
}

const uint8_t* ano_gltf_buffer_view_data(
    const AnoGltfData* data, AnoGltfBufferViewIndex view)
{
    return anogltf_detail::buffer_view_data(data, view);
}

const AnoGltfAccessor* ano_gltf_find_accessor(
    const AnoGltfData* data, const AnoGltfPrimitive* primitive,
    AnoGltfAttributeType type, int32_t set)
{
    if (!data || !primitive)
        return nullptr;
    for (uint32_t i = 0; i < primitive->attributes.values.count; ++i) {
        const AnoGltfAttribute& attribute = primitive->attributes.values.data[i];
        if (attribute.type == type && attribute.set == set
            && ano_gltf_has_index(attribute.accessor)
            && attribute.accessor.value < data->accessorsCount)
            return &data->accessors[attribute.accessor.value];
    }
    return nullptr;
}

bool ano_gltf_accessor_read_float(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, float* output, size_t outputCount)
{
    return anogltf_detail::accessor_read_float(data, accessor, index, output, outputCount);
}

bool ano_gltf_accessor_read_uint(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output, size_t outputCount)
{
    return anogltf_detail::accessor_read_uint(data, accessor, index, output, outputCount);
}

bool ano_gltf_accessor_read_index(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    uint64_t index, uint32_t* output)
{
    return anogltf_detail::accessor_read_index(data, accessor, index, output);
}

uint64_t ano_gltf_accessor_unpack_floats(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    float* output, uint64_t outputCount)
{
    return anogltf_detail::accessor_unpack_floats(data, accessor, output, outputCount);
}

uint64_t ano_gltf_accessor_unpack_indices(
    const AnoGltfData* data, const AnoGltfAccessor* accessor,
    void* output, size_t outputComponentSize, uint64_t outputCount)
{
    return anogltf_detail::accessor_unpack_indices(
        data, accessor, output, outputComponentSize, outputCount);
}

void ano_gltf_node_transform_local(const AnoGltfNode* node, float output[16])
{
    anogltf_detail::node_transform_local(node, output);
}

bool ano_gltf_node_transform_world(
    const AnoGltfData* data, AnoGltfNodeIndex node, float output[16])
{
    return anogltf_detail::node_transform_world(data, node, output);
}

const char* ano_gltf_result_string(AnoGltfResult result)
{
    using enum AnoGltfResult;
    switch (result) {
    case success: return "success";
    case data_too_short: return "data too short";
    case unknown_format: return "unknown format";
    case invalid_json: return "invalid JSON";
    case invalid_gltf: return "invalid glTF";
    case invalid_options: return "invalid options";
    case file_not_found: return "file not found";
    case io_error: return "I/O error";
    case out_of_memory: return "out of memory";
    case limit_exceeded: return "resource limit exceeded";
    case unsupported_required_extension: return "unsupported required extension";
    }
    return "unknown result";
}

} // extern "C"

#endif // ANOGLTF_IMPLEMENTATION

/*
 * MIT License for cgltf-derived portions
 *
 * Copyright (c) 2018-2021 Johannes Kuhlmann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
