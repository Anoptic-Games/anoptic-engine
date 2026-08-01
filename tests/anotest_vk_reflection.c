#include "vulkan_backend/instance/descriptor_layout_schema.h"

#if ANOPTIC_HAS_REFLECTION
struct AnoVkInvalidMemberSchema final {
	int accidentalRuntimeState;
};

struct AnoVkIncompleteBinding final {
	using AnoVkDescriptorBindingMarker = void;
};

struct AnoVkIncompleteBindingSchema final {
	AnoVkIncompleteBinding incomplete;
};

struct AnoVkInvalidStageSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		(AnoVkDescriptorStage)255> invalidStage;
};

struct AnoVkInvalidDescriptorSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_MAX_ENUM,
		AnoVkDescriptorStage::fragment> invalidDescriptor;
};

struct AnoVkInvalidCountSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute, 0> emptyArray;
};

struct AnoVkInvalidOptionalOrderSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkDescriptorStage::task, 1,
		AnoVkDescriptorPresence::task_cull> optional;
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> requiredAfterOptional;
};

struct AnoVkInvalidOptionalStageSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute, 1,
		AnoVkDescriptorPresence::task_cull> optionalCompute;
};

struct AnoVkInvalidGlobalTaskSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::task> alwaysPresentTaskBinding;
};

struct AnoVkInvalidComputeSchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::fragment> fragmentBinding;
};

struct AnoVkInvalidShadowGeometrySchema final {
	AnoVkDescriptorBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkDescriptorStage::compute> computeBinding;
};

static_assert(ano_vk_global_schema_valid<AnoVkGlobalSetSchema>());
static_assert(ano_vk_compute_schema_valid<AnoVkCullSetSchema<2>>());
static_assert(ano_vk_compute_schema_valid<AnoVkHizSetSchema>());
static_assert(ano_vk_compute_schema_valid<AnoVkShadowSetupSetSchema>());
static_assert(ano_vk_shadow_geometry_schema_valid<AnoVkShadowGeometrySetSchema>());

static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidMemberSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkIncompleteBindingSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidStageSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidDescriptorSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidCountSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidOptionalOrderSchema>());
static_assert(!ano_vk_descriptor_schema_valid<AnoVkInvalidOptionalStageSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidGlobalTaskSchema>());
static_assert(!ano_vk_compute_schema_valid<AnoVkInvalidComputeSchema>());
static_assert(!ano_vk_shadow_geometry_schema_valid<AnoVkInvalidShadowGeometrySchema>());
#endif

template<size_t Count>
static bool sequential_bindings(const AnoVkDescriptorBindingTable<Count>& specs)
{
	for (size_t i = 0; i < Count; ++i) {
		if (specs.values[i].binding != i || specs.values[i].descriptorCount == 0)
			return false;
	}
	return true;
}

static bool binding_matches(const AnoVkDescriptorBindingSpec& binding,
	VkDescriptorType type, uint32_t count, AnoVkDescriptorStage stage,
	AnoVkDescriptorPresence presence = AnoVkDescriptorPresence::always)
{
	return binding.descriptorType == type
		&& binding.descriptorCount == count
		&& binding.stage == stage
		&& binding.presence == presence;
}

int main()
{
	const auto& global = ano_vk_global_binding_specs();
	const VkDescriptorType globalTypes[14] = {
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
	};
	const AnoVkDescriptorStage globalStages[14] = {
		AnoVkDescriptorStage::geometry_fragment,
		AnoVkDescriptorStage::geometry_fragment,
		AnoVkDescriptorStage::geometry_fragment,
		AnoVkDescriptorStage::geometry,
		AnoVkDescriptorStage::geometry,
		AnoVkDescriptorStage::geometry,
		AnoVkDescriptorStage::geometry,
		AnoVkDescriptorStage::geometry,
		AnoVkDescriptorStage::fragment,
		AnoVkDescriptorStage::geometry_fragment,
		AnoVkDescriptorStage::fragment,
		AnoVkDescriptorStage::fragment,
		AnoVkDescriptorStage::fragment,
		AnoVkDescriptorStage::task
	};

	if (!sequential_bindings(global))
		return 1;
	for (size_t i = 0; i < global.count; ++i) {
		const AnoVkDescriptorPresence presence = i == 13
			? AnoVkDescriptorPresence::task_cull
			: AnoVkDescriptorPresence::always;
		if (!binding_matches(global.values[i], globalTypes[i], 1,
				globalStages[i], presence))
			return 2;
	}

	const auto& cull = ano_vk_cull_binding_specs<2>();
	if (!sequential_bindings(cull))
		return 3;
	for (size_t i = 0; i < cull.count; ++i) {
		const VkDescriptorType type = i == 0
			? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
			: (i == 11 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
				: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		const uint32_t count = i == 11 ? 2 : 1;
		if (!binding_matches(cull.values[i], type, count,
				AnoVkDescriptorStage::compute))
			return 4;
	}

	const auto& hiz = ano_vk_hiz_binding_specs();
	const VkDescriptorType hizTypes[3] = {
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
	};
	if (!sequential_bindings(hiz))
		return 5;
	for (size_t i = 0; i < hiz.count; ++i) {
		if (!binding_matches(hiz.values[i], hizTypes[i], 1,
				AnoVkDescriptorStage::compute))
			return 6;
	}

	const auto& setup = ano_vk_shadow_setup_binding_specs();
	if (!sequential_bindings(setup))
		return 7;
	for (size_t i = 0; i < setup.count; ++i) {
		if (!binding_matches(setup.values[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				1, AnoVkDescriptorStage::compute))
			return 8;
	}

	const auto& geometry = ano_vk_shadow_geometry_binding_specs();
	const VkDescriptorType geometryTypes[4] = {
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
	};
	if (!sequential_bindings(geometry))
		return 9;
	for (size_t i = 0; i < geometry.count; ++i) {
		const AnoVkDescriptorStage stage = i == 0
			? AnoVkDescriptorStage::geometry_fragment
			: AnoVkDescriptorStage::fragment;
		if (!binding_matches(geometry.values[i], geometryTypes[i], 1, stage))
			return 10;
	}

	if (ano_vk_descriptor_stage_flags(AnoVkDescriptorStage::geometry, 0x100u) != 0x100u)
		return 11;
	if (ano_vk_descriptor_stage_flags(AnoVkDescriptorStage::geometry_fragment, 0x100u)
			!= (0x100u | VK_SHADER_STAGE_FRAGMENT_BIT))
		return 12;
	if (ano_vk_descriptor_stage_flags(AnoVkDescriptorStage::compute, 0)
			!= VK_SHADER_STAGE_COMPUTE_BIT)
		return 13;
	if (ano_vk_descriptor_stage_flags(AnoVkDescriptorStage::fragment, 0)
			!= VK_SHADER_STAGE_FRAGMENT_BIT)
		return 14;
	if (ano_vk_descriptor_stage_flags(AnoVkDescriptorStage::task, 0)
			!= VK_SHADER_STAGE_TASK_BIT_EXT)
		return 15;
	return 0;
}
