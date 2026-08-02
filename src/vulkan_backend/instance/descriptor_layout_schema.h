#ifndef ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H
#define ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H

#include <meta>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

enum class AnoVkDescriptorStage : uint8_t {
	geometry,
	geometry_fragment,
	fragment,
	task,
	compute,
	fragment_compute,
	vertex_fragment_compute,
};
enum class AnoVkLayoutClass : uint8_t { global, compute, shadow_geometry, fragment, mixed };
using enum AnoVkDescriptorStage;

template<VkDescriptorType Type, AnoVkDescriptorStage Stage, uint32_t Count = 1,
	VkDescriptorBindingFlags Flags = 0>
struct AnoVkBinding final {
	static_assert(Count > 0 || (Flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) != 0);
	static constexpr VkDescriptorType descriptorType = Type;
	static constexpr AnoVkDescriptorStage stage = Stage;
	static constexpr uint32_t descriptorCount = Count;
	static constexpr VkDescriptorBindingFlags bindingFlags = Flags;
};

template<AnoVkDescriptorStage S> using AnoVkUniform = AnoVkBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S>;
template<AnoVkDescriptorStage S> using AnoVkStorage = AnoVkBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S>;
template<AnoVkDescriptorStage S> using AnoVkDynamicStorage = AnoVkBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, S>;
template<AnoVkDescriptorStage S, uint32_t N = 1, VkDescriptorBindingFlags F = 0>
using AnoVkSampler = AnoVkBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, S, N, F>;
template<AnoVkDescriptorStage S> using AnoVkStorageImage = AnoVkBinding<VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, S>;

struct AnoVkGlobalSetSchema final {
	static constexpr uint32_t optionalTail = 1;
	AnoVkUniform<geometry_fragment> camera;
	AnoVkStorage<geometry_fragment> transforms, materials;
	AnoVkStorage<geometry> entities, vertices, indices, meshes, compactedEntities;
	AnoVkStorage<fragment> lights;
	AnoVkStorage<geometry_fragment> instances;
	AnoVkStorage<fragment> clusterCounts, clusterIndices, lightRuntime;
	AnoVkSampler<task> hizPyramid;
};

template<uint32_t ViewCount>
struct AnoVkCullSetSchema final {
	static_assert(ViewCount > 0);
	AnoVkUniform<compute> cull;
	AnoVkStorage<compute> transforms, entities, meshes, meshBounds, indirect, drawCount,
		compactedEntityIndices, materials, shadowFrustums, sortKeys;
	AnoVkSampler<compute, ViewCount> hizPyramids;
};

struct AnoVkHizSetSchema final {
	AnoVkSampler<compute> pyramid;
	AnoVkStorageImage<compute> destinationMip;
	AnoVkSampler<compute> depth;
};

struct AnoVkShadowSetupSetSchema final {
	AnoVkStorage<compute> config, transforms, lights, frustums, samplingViewProjections;
};

struct AnoVkShadowGeometrySetSchema final {
	AnoVkStorage<geometry_fragment> viewProjections;
	AnoVkSampler<fragment> atlas;
	AnoVkStorage<fragment> lightInfo;
	AnoVkUniform<fragment> samplingViewProjections;
};

struct AnoVkUpdateSetSchema final {
	AnoVkUniform<compute> global;
	AnoVkStorage<compute> transforms, motion, previousTransforms;
};

struct AnoVkScatterSetSchema final {
	AnoVkStorage<compute> streamSlots;
	AnoVkDynamicStorage<compute> streamTransforms;
	AnoVkStorage<compute> transforms;
};

struct AnoVkLightCullSetSchema final {
	AnoVkUniform<compute> global;
	AnoVkStorage<compute> lightRuntime, lights, clusterCounts, clusterIndices;
};

struct AnoVkLightSetupSetSchema final {
	AnoVkStorage<compute> transforms, lights, lightRuntime;
};

struct AnoVkShadowBlurSetSchema final {
	AnoVkSampler<fragment> atlas;
};

struct AnoVkTonemapSetSchema final {
	AnoVkSampler<fragment> hdrColor;
};

inline constexpr VkDescriptorBindingFlags ANO_VK_BINDLESS_BINDING_FLAGS =
	VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
	VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
	VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

struct AnoVkBindlessSetSchema final {
	AnoVkSampler<fragment_compute, 0, ANO_VK_BINDLESS_BINDING_FLAGS> textures;
};

struct AnoVkTextRasterSetSchema final {
	AnoVkStorage<vertex_fragment_compute> curvePoints, glyphDirectory, frameData;
	AnoVkStorageImage<compute> overlay;
	AnoVkStorage<compute> uiPrimitives, uiClips, uiPaints, uiStops, uiCurves,
		uiTileOffsets, uiTileEntries;
};

struct AnoVkDescriptorBindingSpec final {
	VkDescriptorType descriptorType;
	uint32_t descriptorCount;
	AnoVkDescriptorStage stage;
	VkDescriptorBindingFlags bindingFlags;
};

template<size_t Count>
struct AnoVkDescriptorBindingTable final {
	AnoVkDescriptorBindingSpec values[Count];
	static constexpr uint32_t count = (uint32_t)Count;
};

constexpr VkShaderStageFlags ano_vk_descriptor_stage_flags(
	AnoVkDescriptorStage stage, VkShaderStageFlags geometryStage)
{
	switch (stage) {
	case geometry: return geometryStage;
	case geometry_fragment: return geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	case fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
	case task: return VK_SHADER_STAGE_TASK_BIT_EXT;
	case compute: return VK_SHADER_STAGE_COMPUTE_BIT;
	case fragment_compute: return VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
	case vertex_fragment_compute:
		return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
			VK_SHADER_STAGE_COMPUTE_BIT;
	}
	return 0;
}

template<AnoVkLayoutClass Layout>
consteval bool ano_vk_stage_allowed(AnoVkDescriptorStage stage)
{
	if constexpr (Layout == AnoVkLayoutClass::global)
		return stage == geometry || stage == geometry_fragment || stage == fragment || stage == task;
	if constexpr (Layout == AnoVkLayoutClass::compute)
		return stage == compute;
	if constexpr (Layout == AnoVkLayoutClass::shadow_geometry)
		return stage == geometry_fragment || stage == fragment;
	if constexpr (Layout == AnoVkLayoutClass::fragment)
		return stage == fragment;
	return stage == compute || stage == fragment_compute || stage == vertex_fragment_compute;
}

template<class Schema, AnoVkLayoutClass Layout>
consteval auto ano_vk_reflect_bindings()
{
	static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
		^^Schema, std::meta::access_context::unchecked()));
	AnoVkDescriptorBindingTable<members.size()> result{};
	size_t at = 0;
	template for (constexpr auto member : members) {
		using Binding = [:std::meta::type_of(member):];
		static_assert(ano_vk_stage_allowed<Layout>(Binding::stage));
		result.values[at++] = {
			Binding::descriptorType, Binding::descriptorCount, Binding::stage, Binding::bindingFlags
		};
	}
	return result;
}

template<size_t Count>
[[nodiscard]] inline bool ano_vk_materialize_layout_bindings(
	const AnoVkDescriptorBindingTable<Count>& specs,
	VkDescriptorSetLayoutBinding (&bindings)[Count],
	VkShaderStageFlags geometryStage = 0,
	uint32_t runtimeDescriptorCount = 0)
{
	for (size_t i = 0; i < Count; ++i) {
		const uint32_t descriptorCount = specs.values[i].descriptorCount
			? specs.values[i].descriptorCount : runtimeDescriptorCount;
		if (descriptorCount == 0)
			return false;
		bindings[i].binding = (uint32_t)i;
		bindings[i].descriptorType = specs.values[i].descriptorType;
		bindings[i].descriptorCount = descriptorCount;
		bindings[i].stageFlags = ano_vk_descriptor_stage_flags(
			specs.values[i].stage, geometryStage);
		bindings[i].pImmutableSamplers = NULL;
	}
	return true;
}

template<size_t Count>
inline void ano_vk_materialize_layout_binding_flags(
	const AnoVkDescriptorBindingTable<Count>& specs,
	VkDescriptorBindingFlags (&flags)[Count])
{
	for (size_t i = 0; i < Count; ++i)
		flags[i] = specs.values[i].bindingFlags;
}

inline constexpr auto ANO_VK_GLOBAL_BINDINGS =
	ano_vk_reflect_bindings<AnoVkGlobalSetSchema, AnoVkLayoutClass::global>();
template<uint32_t ViewCount>
inline constexpr auto ANO_VK_CULL_BINDINGS =
	ano_vk_reflect_bindings<AnoVkCullSetSchema<ViewCount>, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_HIZ_BINDINGS =
	ano_vk_reflect_bindings<AnoVkHizSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_SHADOW_SETUP_BINDINGS =
	ano_vk_reflect_bindings<AnoVkShadowSetupSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_SHADOW_GEOMETRY_BINDINGS =
	ano_vk_reflect_bindings<AnoVkShadowGeometrySetSchema, AnoVkLayoutClass::shadow_geometry>();
inline constexpr auto ANO_VK_UPDATE_BINDINGS =
	ano_vk_reflect_bindings<AnoVkUpdateSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_SCATTER_BINDINGS =
	ano_vk_reflect_bindings<AnoVkScatterSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_LIGHT_CULL_BINDINGS =
	ano_vk_reflect_bindings<AnoVkLightCullSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_LIGHT_SETUP_BINDINGS =
	ano_vk_reflect_bindings<AnoVkLightSetupSetSchema, AnoVkLayoutClass::compute>();
inline constexpr auto ANO_VK_SHADOW_BLUR_BINDINGS =
	ano_vk_reflect_bindings<AnoVkShadowBlurSetSchema, AnoVkLayoutClass::fragment>();
inline constexpr auto ANO_VK_TONEMAP_BINDINGS =
	ano_vk_reflect_bindings<AnoVkTonemapSetSchema, AnoVkLayoutClass::fragment>();
inline constexpr auto ANO_VK_BINDLESS_BINDINGS =
	ano_vk_reflect_bindings<AnoVkBindlessSetSchema, AnoVkLayoutClass::mixed>();
inline constexpr auto ANO_VK_TEXT_RASTER_BINDINGS =
	ano_vk_reflect_bindings<AnoVkTextRasterSetSchema, AnoVkLayoutClass::mixed>();

static_assert(ANO_VK_GLOBAL_BINDINGS.values[ANO_VK_GLOBAL_BINDINGS.count - 1].stage == task);
static_assert(ANO_VK_BINDLESS_BINDINGS.count == 1 &&
	ANO_VK_BINDLESS_BINDINGS.values[0].descriptorCount == 0 &&
	ANO_VK_BINDLESS_BINDINGS.values[0].bindingFlags == ANO_VK_BINDLESS_BINDING_FLAGS);

#endif
