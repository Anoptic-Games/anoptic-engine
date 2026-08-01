#ifndef ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H
#define ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H

#include <meta>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

enum class AnoVkDescriptorStage : uint8_t { geometry, geometry_fragment, fragment, task, compute };
enum class AnoVkLayoutClass : uint8_t { global, compute, shadow_geometry };
using enum AnoVkDescriptorStage;

template<VkDescriptorType Type, AnoVkDescriptorStage Stage, uint32_t Count = 1>
struct AnoVkBinding final {
	static_assert(Count > 0);
	static constexpr VkDescriptorType descriptorType = Type;
	static constexpr AnoVkDescriptorStage stage = Stage;
	static constexpr uint32_t descriptorCount = Count;
};

template<AnoVkDescriptorStage S> using AnoVkUniform = AnoVkBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S>;
template<AnoVkDescriptorStage S> using AnoVkStorage = AnoVkBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S>;
template<AnoVkDescriptorStage S, uint32_t N = 1> using AnoVkSampler = AnoVkBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, S, N>;
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

struct AnoVkDescriptorBindingSpec final {
	VkDescriptorType descriptorType;
	uint32_t descriptorCount;
	AnoVkDescriptorStage stage;
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
	}
	return 0;
}

template<AnoVkLayoutClass Layout>
consteval bool ano_vk_stage_allowed(AnoVkDescriptorStage stage)
{
	if constexpr (Layout == AnoVkLayoutClass::global)
		return stage != compute;
	if constexpr (Layout == AnoVkLayoutClass::compute)
		return stage == compute;
	return stage == geometry_fragment || stage == fragment;
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
		result.values[at++] = { Binding::descriptorType, Binding::descriptorCount, Binding::stage };
	}
	return result;
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

static_assert(ANO_VK_GLOBAL_BINDINGS.values[ANO_VK_GLOBAL_BINDINGS.count - 1].stage == task);

#endif
