#ifndef ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H
#define ANOPTIC_VK_DESCRIPTOR_LAYOUT_SCHEMA_H

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

enum class AnoVkDescriptorStage : uint8_t {
	geometry,
	geometry_fragment,
	fragment,
	task,
	compute
};

enum class AnoVkDescriptorPresence : uint8_t {
	always,
	task_cull
};

template<VkDescriptorType DescriptorType, AnoVkDescriptorStage Stage,
	uint32_t DescriptorCount = 1,
	AnoVkDescriptorPresence Presence = AnoVkDescriptorPresence::always>
struct AnoVkDescriptorBinding final {
	using AnoVkDescriptorBindingMarker = void;
	static constexpr VkDescriptorType descriptorType = DescriptorType;
	static constexpr AnoVkDescriptorStage stage = Stage;
	static constexpr uint32_t descriptorCount = DescriptorCount;
	static constexpr AnoVkDescriptorPresence presence = Presence;
};

struct AnoVkGlobalSetSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		AnoVkDescriptorStage::geometry_fragment> camera;
	// Fragment-visible: lights derive world pose from their entity transform.
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry_fragment> transforms;
	// Geometry reads doubleSided for meshlet cone culling, fragment reads the rest.
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry_fragment> materials;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry> entities;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry> vertices;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry> indices;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry> meshes;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry> compactedEntities;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> lights;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry_fragment> instances;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> clusterCounts;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> clusterIndices;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> lightRuntime;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::task, 1,
		AnoVkDescriptorPresence::task_cull> hizPyramid;
};

template<uint32_t ViewCount>
struct AnoVkCullSetSchema final {
	// 0: CullUBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		AnoVkDescriptorStage::compute> cull;
	// 1: TransformSSBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> transforms;
	// 2: EntitySSBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> entities;
	// 3: MeshSSBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> meshes;
	// 4: MeshBoundsSSBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> meshBounds;
	// 5: IndirectBuffer
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> indirect;
	// 6: DrawCount
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> drawCount;
	// 7: CompactedEntityIndices
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> compactedEntityIndices;
	// 8: MaterialSSBO
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> materials;
	// 9: ShadowFrustumSSBO, GPU-built shadow frustums the cull tests against.
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> shadowFrustums;
	// 10: SortKeys, cull writes per-draw depth keys, tpsort.comp reads them.
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> sortKeys;
	// 11: Hi-Z occlusion pyramids, one combined-image-sampler per camera view.
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::compute, ViewCount> hizPyramids;
};

// Hi-Z pyramid build set: 0 sampled all-mip view, 1 this mip's r32f storage dest, 2 MSAA camera depth.
struct AnoVkHizSetSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::compute> pyramid;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		AnoVkDescriptorStage::compute> destinationMip;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::compute> depth;
};

// --- Dynamic shadow set layouts ---

// shadowsetup compute set: 0 config, 1 transforms, 2 lights, 3 frustums, 4 packed sampling viewProjs.
struct AnoVkShadowSetupSetSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> config;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> transforms;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> lights;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> frustums;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> samplingViewProjections;
};

// Shadow geom set 2: 0 viewProjs, 1 atlas, 2 light info, 3 sampling viewProjs UBO.
struct AnoVkShadowGeometrySetSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::geometry_fragment> viewProjections;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::fragment> atlas;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> lightInfo;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		AnoVkDescriptorStage::fragment> samplingViewProjections;
};

struct AnoVkDescriptorBindingSpec final {
	uint32_t binding;
	VkDescriptorType descriptorType;
	uint32_t descriptorCount;
	AnoVkDescriptorStage stage;
	AnoVkDescriptorPresence presence;
};

template<size_t Count>
struct AnoVkDescriptorBindingTable final {
	AnoVkDescriptorBindingSpec values[Count];
	static constexpr size_t count = Count;
};

inline constexpr AnoVkDescriptorBindingTable<14> ANO_VK_GLOBAL_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, AnoVkDescriptorStage::geometry_fragment, AnoVkDescriptorPresence::always},
	{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry_fragment, AnoVkDescriptorPresence::always},
	{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry_fragment, AnoVkDescriptorPresence::always},
	{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry, AnoVkDescriptorPresence::always},
	{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry, AnoVkDescriptorPresence::always},
	{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry, AnoVkDescriptorPresence::always},
	{6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry, AnoVkDescriptorPresence::always},
	{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry, AnoVkDescriptorPresence::always},
	{8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry_fragment, AnoVkDescriptorPresence::always},
	{10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, AnoVkDescriptorStage::task, AnoVkDescriptorPresence::task_cull}
}};

template<uint32_t ViewCount>
inline constexpr AnoVkDescriptorBindingTable<12> ANO_VK_CULL_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ViewCount, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always}
}};

inline constexpr AnoVkDescriptorBindingTable<3> ANO_VK_HIZ_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always}
}};

inline constexpr AnoVkDescriptorBindingTable<5> ANO_VK_SHADOW_SETUP_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always},
	{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::compute, AnoVkDescriptorPresence::always}
}};

inline constexpr AnoVkDescriptorBindingTable<4> ANO_VK_SHADOW_GEOMETRY_FALLBACK = {{
	{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::geometry_fragment, AnoVkDescriptorPresence::always},
	{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always},
	{3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, AnoVkDescriptorStage::fragment, AnoVkDescriptorPresence::always}
}};

constexpr bool ano_vk_descriptor_type_valid(VkDescriptorType type)
{
	return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		|| type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
		|| type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		|| type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

constexpr bool ano_vk_descriptor_stage_valid(AnoVkDescriptorStage stage)
{
	switch (stage) {
	case AnoVkDescriptorStage::geometry:
	case AnoVkDescriptorStage::geometry_fragment:
	case AnoVkDescriptorStage::fragment:
	case AnoVkDescriptorStage::task:
	case AnoVkDescriptorStage::compute:
		return true;
	}
	return false;
}

constexpr bool ano_vk_descriptor_presence_valid(AnoVkDescriptorPresence presence)
{
	switch (presence) {
	case AnoVkDescriptorPresence::always:
	case AnoVkDescriptorPresence::task_cull:
		return true;
	}
	return false;
}

constexpr VkShaderStageFlags ano_vk_descriptor_stage_flags(
	AnoVkDescriptorStage stage, VkShaderStageFlags geometryStage)
{
	switch (stage) {
	case AnoVkDescriptorStage::geometry:
		return geometryStage;
	case AnoVkDescriptorStage::geometry_fragment:
		return geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	case AnoVkDescriptorStage::fragment:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case AnoVkDescriptorStage::task:
		return VK_SHADER_STAGE_TASK_BIT_EXT;
	case AnoVkDescriptorStage::compute:
		return VK_SHADER_STAGE_COMPUTE_BIT;
	}
	return 0;
}

#if ANOPTIC_HAS_REFLECTION
template<class T>
concept AnoVkDescriptorBindingType = requires {
	typename T::AnoVkDescriptorBindingMarker;
	T::descriptorType;
	T::descriptorCount;
	T::stage;
	T::presence;
};

template<class Schema>
consteval size_t ano_vk_descriptor_schema_member_count()
{
	return std::meta::nonstatic_data_members_of(
		^^Schema, std::meta::access_context::unchecked()).size();
}

template<class Schema>
consteval bool ano_vk_descriptor_schema_valid()
{
	bool sawOptional = false;

	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		if constexpr (!AnoVkDescriptorBindingType<Binding>) {
			return false;
		} else {
			if (!ano_vk_descriptor_type_valid(Binding::descriptorType)
					|| !ano_vk_descriptor_stage_valid(Binding::stage)
					|| !ano_vk_descriptor_presence_valid(Binding::presence)
					|| Binding::descriptorCount == 0)
				return false;
			if (Binding::presence == AnoVkDescriptorPresence::task_cull) {
				sawOptional = true;
				if (Binding::stage != AnoVkDescriptorStage::task)
					return false;
			} else if (sawOptional) {
				return false;
			}
		}
	}
	return true;
}

template<class Schema>
consteval bool ano_vk_global_schema_valid()
{
	if (!ano_vk_descriptor_schema_valid<Schema>())
		return false;
	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		if constexpr (AnoVkDescriptorBindingType<Binding>) {
			if (Binding::stage == AnoVkDescriptorStage::task
					&& Binding::presence != AnoVkDescriptorPresence::task_cull)
				return false;
		}
	}
	return true;
}

template<class Schema>
consteval bool ano_vk_compute_schema_valid()
{
	if (!ano_vk_descriptor_schema_valid<Schema>())
		return false;
	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		if constexpr (AnoVkDescriptorBindingType<Binding>) {
			if (Binding::stage != AnoVkDescriptorStage::compute
					|| Binding::presence != AnoVkDescriptorPresence::always)
				return false;
		}
	}
	return true;
}

template<class Schema>
consteval bool ano_vk_shadow_geometry_schema_valid()
{
	if (!ano_vk_descriptor_schema_valid<Schema>())
		return false;
	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		if constexpr (AnoVkDescriptorBindingType<Binding>) {
			if ((Binding::stage != AnoVkDescriptorStage::geometry_fragment
					&& Binding::stage != AnoVkDescriptorStage::fragment)
					|| Binding::presence != AnoVkDescriptorPresence::always)
				return false;
		}
	}
	return true;
}

template<class Schema>
consteval auto ano_vk_reflect_descriptor_bindings()
{
	static_assert(ano_vk_descriptor_schema_valid<Schema>());
	constexpr size_t count = ano_vk_descriptor_schema_member_count<Schema>();
	AnoVkDescriptorBindingTable<count> result{};
	size_t at = 0;

	template for (constexpr auto member : std::define_static_array(
		std::meta::nonstatic_data_members_of(
			^^Schema, std::meta::access_context::unchecked()))) {
		using Binding = [:std::meta::type_of(member):];
		result.values[at] = {
			(uint32_t)at,
			Binding::descriptorType,
			Binding::descriptorCount,
			Binding::stage,
			Binding::presence
		};
		++at;
	}
	return result;
}

template<size_t Count>
consteval bool ano_vk_descriptor_tables_equal(
	const AnoVkDescriptorBindingTable<Count>& lhs,
	const AnoVkDescriptorBindingTable<Count>& rhs)
{
	for (size_t i = 0; i < Count; ++i) {
		if (lhs.values[i].binding != rhs.values[i].binding
				|| lhs.values[i].descriptorType != rhs.values[i].descriptorType
				|| lhs.values[i].descriptorCount != rhs.values[i].descriptorCount
				|| lhs.values[i].stage != rhs.values[i].stage
				|| lhs.values[i].presence != rhs.values[i].presence)
			return false;
	}
	return true;
}

inline constexpr auto ANO_VK_GLOBAL_REFLECTED =
	ano_vk_reflect_descriptor_bindings<AnoVkGlobalSetSchema>();
template<uint32_t ViewCount>
inline constexpr auto ANO_VK_CULL_REFLECTED =
	ano_vk_reflect_descriptor_bindings<AnoVkCullSetSchema<ViewCount>>();
inline constexpr auto ANO_VK_HIZ_REFLECTED =
	ano_vk_reflect_descriptor_bindings<AnoVkHizSetSchema>();
inline constexpr auto ANO_VK_SHADOW_SETUP_REFLECTED =
	ano_vk_reflect_descriptor_bindings<AnoVkShadowSetupSetSchema>();
inline constexpr auto ANO_VK_SHADOW_GEOMETRY_REFLECTED =
	ano_vk_reflect_descriptor_bindings<AnoVkShadowGeometrySetSchema>();
#endif

constexpr const AnoVkDescriptorBindingTable<14>& ano_vk_global_binding_specs()
{
#if ANOPTIC_HAS_REFLECTION
	static_assert(ano_vk_global_schema_valid<AnoVkGlobalSetSchema>());
	static_assert(ano_vk_descriptor_tables_equal(
		ANO_VK_GLOBAL_REFLECTED, ANO_VK_GLOBAL_FALLBACK));
	return ANO_VK_GLOBAL_REFLECTED;
#else
	return ANO_VK_GLOBAL_FALLBACK;
#endif
}

template<uint32_t ViewCount>
constexpr const AnoVkDescriptorBindingTable<12>& ano_vk_cull_binding_specs()
{
	static_assert(ViewCount > 0);
#if ANOPTIC_HAS_REFLECTION
	static_assert(ano_vk_compute_schema_valid<AnoVkCullSetSchema<ViewCount>>());
	static_assert(ano_vk_descriptor_tables_equal(
		ANO_VK_CULL_REFLECTED<ViewCount>, ANO_VK_CULL_FALLBACK<ViewCount>));
	return ANO_VK_CULL_REFLECTED<ViewCount>;
#else
	return ANO_VK_CULL_FALLBACK<ViewCount>;
#endif
}

constexpr const AnoVkDescriptorBindingTable<3>& ano_vk_hiz_binding_specs()
{
#if ANOPTIC_HAS_REFLECTION
	static_assert(ano_vk_compute_schema_valid<AnoVkHizSetSchema>());
	static_assert(ano_vk_descriptor_tables_equal(
		ANO_VK_HIZ_REFLECTED, ANO_VK_HIZ_FALLBACK));
	return ANO_VK_HIZ_REFLECTED;
#else
	return ANO_VK_HIZ_FALLBACK;
#endif
}

constexpr const AnoVkDescriptorBindingTable<5>& ano_vk_shadow_setup_binding_specs()
{
#if ANOPTIC_HAS_REFLECTION
	static_assert(ano_vk_compute_schema_valid<AnoVkShadowSetupSetSchema>());
	static_assert(ano_vk_descriptor_tables_equal(
		ANO_VK_SHADOW_SETUP_REFLECTED, ANO_VK_SHADOW_SETUP_FALLBACK));
	return ANO_VK_SHADOW_SETUP_REFLECTED;
#else
	return ANO_VK_SHADOW_SETUP_FALLBACK;
#endif
}

constexpr const AnoVkDescriptorBindingTable<4>& ano_vk_shadow_geometry_binding_specs()
{
#if ANOPTIC_HAS_REFLECTION
	static_assert(ano_vk_shadow_geometry_schema_valid<AnoVkShadowGeometrySetSchema>());
	static_assert(ano_vk_descriptor_tables_equal(
		ANO_VK_SHADOW_GEOMETRY_REFLECTED, ANO_VK_SHADOW_GEOMETRY_FALLBACK));
	return ANO_VK_SHADOW_GEOMETRY_REFLECTED;
#else
	return ANO_VK_SHADOW_GEOMETRY_FALLBACK;
#endif
}

#endif
