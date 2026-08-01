#ifndef ANOPTIC_VK_GLOBAL_LAYOUT_SCHEMA_H
#define ANOPTIC_VK_GLOBAL_LAYOUT_SCHEMA_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifndef ANOPTIC_HAS_REFLECTION
#define ANOPTIC_HAS_REFLECTION 0
#endif

#if ANOPTIC_HAS_REFLECTION
#include <meta>
#ifndef __cpp_impl_reflection
#error ANOPTIC_HAS_REFLECTION requires the C++26 reflection feature macro
#endif
#endif

enum class AnoVkGlobalStage : uint8_t {
	geometry,
	geometry_fragment,
	fragment,
	task
};

enum class AnoVkGlobalPresence : uint8_t {
	always,
	task_cull
};

template<VkDescriptorType DescriptorType, AnoVkGlobalStage Stage,
	AnoVkGlobalPresence Presence = AnoVkGlobalPresence::always>
struct AnoVkGlobalBinding final {
	using AnoVkGlobalBindingMarker = void;
	static constexpr VkDescriptorType descriptorType = DescriptorType;
	static constexpr AnoVkGlobalStage stage = Stage;
	static constexpr AnoVkGlobalPresence presence = Presence;
};

struct AnoVkGlobalSetSchema final {
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		AnoVkGlobalStage::geometry_fragment> camera;
	// Fragment-visible: lights derive world pose from their entity transform.
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry_fragment> transforms;
	// Geometry reads doubleSided for meshlet cone culling, fragment reads the rest.
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry_fragment> materials;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry> entities;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry> vertices;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry> indices;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry> meshes;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry> compactedEntities;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::fragment> lights;
	// Per-entity instance channel (tint/flags/scalars).
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::geometry_fragment> instances;
	// 10/11: clustered-forward froxel light lists, fragment-only.
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::fragment> clusterCounts;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::fragment> clusterIndices;
	// 12: per-light LightRuntime record, precomputed by lightsetup.comp, fragment-only.
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::fragment> lightRuntime;
	// 13: this view's Hi-Z pyramid for the task meshlet cull occlusion test.
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkGlobalStage::task, AnoVkGlobalPresence::task_cull> hizPyramid;
};

struct AnoVkGlobalBindingSpec final {
	uint32_t binding;
	VkDescriptorType descriptorType;
	AnoVkGlobalStage stage;
	AnoVkGlobalPresence presence;
};

template<size_t Count>
struct AnoVkGlobalBindingTable final {
	AnoVkGlobalBindingSpec values[Count];
	static constexpr size_t count = Count;
};

inline constexpr AnoVkGlobalBindingTable<14> ANO_VK_GLOBAL_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, AnoVkGlobalStage::geometry_fragment, AnoVkGlobalPresence::always},
	{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry_fragment, AnoVkGlobalPresence::always},
	{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry_fragment, AnoVkGlobalPresence::always},
	{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry, AnoVkGlobalPresence::always},
	{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry, AnoVkGlobalPresence::always},
	{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry, AnoVkGlobalPresence::always},
	{6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry, AnoVkGlobalPresence::always},
	{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry, AnoVkGlobalPresence::always},
	{8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::fragment, AnoVkGlobalPresence::always},
	{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::geometry_fragment, AnoVkGlobalPresence::always},
	{10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::fragment, AnoVkGlobalPresence::always},
	{11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::fragment, AnoVkGlobalPresence::always},
	{12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AnoVkGlobalStage::fragment, AnoVkGlobalPresence::always},
	{13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, AnoVkGlobalStage::task, AnoVkGlobalPresence::task_cull}
}};

constexpr bool ano_vk_global_descriptor_type_valid(VkDescriptorType type)
{
	return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		|| type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
		|| type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

constexpr bool ano_vk_global_stage_valid(AnoVkGlobalStage stage)
{
	switch (stage) {
	case AnoVkGlobalStage::geometry:
	case AnoVkGlobalStage::geometry_fragment:
	case AnoVkGlobalStage::fragment:
	case AnoVkGlobalStage::task:
		return true;
	}
	return false;
}

constexpr bool ano_vk_global_presence_valid(AnoVkGlobalPresence presence)
{
	switch (presence) {
	case AnoVkGlobalPresence::always:
	case AnoVkGlobalPresence::task_cull:
		return true;
	}
	return false;
}

constexpr VkShaderStageFlags ano_vk_global_stage_flags(
	AnoVkGlobalStage stage, VkShaderStageFlags geometryStage)
{
	switch (stage) {
	case AnoVkGlobalStage::geometry:
		return geometryStage;
	case AnoVkGlobalStage::geometry_fragment:
		return geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	case AnoVkGlobalStage::fragment:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case AnoVkGlobalStage::task:
		return VK_SHADER_STAGE_TASK_BIT_EXT;
	}
	return 0;
}

#if ANOPTIC_HAS_REFLECTION
template<class T>
concept AnoVkGlobalBindingType = requires {
	typename T::AnoVkGlobalBindingMarker;
};

template<class Schema>
consteval size_t ano_vk_global_schema_member_count()
{
	return std::meta::nonstatic_data_members_of(
		^^Schema, std::meta::access_context::unchecked()).size();
}

template<class Schema>
consteval bool ano_vk_global_schema_valid()
{
	bool sawOptional = false;

	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		if constexpr (!AnoVkGlobalBindingType<Binding>) {
			return false;
		} else {
			if (!ano_vk_global_descriptor_type_valid(Binding::descriptorType)
					|| !ano_vk_global_stage_valid(Binding::stage)
					|| !ano_vk_global_presence_valid(Binding::presence))
				return false;
			if (Binding::presence == AnoVkGlobalPresence::task_cull) {
				sawOptional = true;
				if (Binding::stage != AnoVkGlobalStage::task)
					return false;
			} else if (sawOptional || Binding::stage == AnoVkGlobalStage::task) {
				return false;
			}
		}
	}
	return true;
}

template<class Schema>
consteval auto ano_vk_reflect_global_bindings()
{
	static_assert(ano_vk_global_schema_valid<Schema>());
	constexpr size_t count = ano_vk_global_schema_member_count<Schema>();
	AnoVkGlobalBindingTable<count> result{};
	size_t at = 0;

	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		result.values[at] = {
			(uint32_t)at,
			Binding::descriptorType,
			Binding::stage,
			Binding::presence
		};
		++at;
	}
	return result;
}

template<size_t Count>
consteval bool ano_vk_global_tables_equal(
	const AnoVkGlobalBindingTable<Count>& lhs,
	const AnoVkGlobalBindingTable<Count>& rhs)
{
	for (size_t i = 0; i < Count; ++i) {
		if (lhs.values[i].binding != rhs.values[i].binding
				|| lhs.values[i].descriptorType != rhs.values[i].descriptorType
				|| lhs.values[i].stage != rhs.values[i].stage
				|| lhs.values[i].presence != rhs.values[i].presence)
			return false;
	}
	return true;
}

inline constexpr auto ANO_VK_GLOBAL_REFLECTED =
	ano_vk_reflect_global_bindings<AnoVkGlobalSetSchema>();
static_assert(ANO_VK_GLOBAL_REFLECTED.count == 14);
static_assert(ano_vk_global_tables_equal(
	ANO_VK_GLOBAL_REFLECTED, ANO_VK_GLOBAL_FALLBACK));
#endif

constexpr const AnoVkGlobalBindingTable<14>& ano_vk_global_binding_specs()
{
#if ANOPTIC_HAS_REFLECTION
	return ANO_VK_GLOBAL_REFLECTED;
#else
	return ANO_VK_GLOBAL_FALLBACK;
#endif
}

#endif
