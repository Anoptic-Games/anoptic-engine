#include "vulkan_backend/instance/global_layout_schema.h"

#if ANOPTIC_HAS_REFLECTION
struct AnoVkInvalidMemberSchema final {
	int accidentalRuntimeState;
};

struct AnoVkInvalidStageSchema final {
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		(AnoVkGlobalStage)255> invalidStage;
};

struct AnoVkInvalidDescriptorSchema final {
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_MAX_ENUM,
		AnoVkGlobalStage::fragment> invalidDescriptor;
};

struct AnoVkInvalidOptionalOrderSchema final {
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		AnoVkGlobalStage::task, AnoVkGlobalPresence::task_cull> optional;
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::fragment> requiredAfterOptional;
};

struct AnoVkInvalidTaskSchema final {
	AnoVkGlobalBinding<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		AnoVkGlobalStage::task> alwaysPresentTaskBinding;
};

static_assert(ano_vk_global_schema_valid<AnoVkGlobalSetSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidMemberSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidStageSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidDescriptorSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidOptionalOrderSchema>());
static_assert(!ano_vk_global_schema_valid<AnoVkInvalidTaskSchema>());
#endif

int main()
{
	const auto& specs = ano_vk_global_binding_specs();
	const VkDescriptorType expectedTypes[14] = {
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

	if (specs.count != 14)
		return 1;
	for (size_t i = 0; i < specs.count; ++i) {
		if (specs.values[i].binding != i
				|| specs.values[i].descriptorType != expectedTypes[i])
			return 2;
		if (i < 13 && specs.values[i].presence != AnoVkGlobalPresence::always)
			return 3;
	}
	if (specs.values[13].presence != AnoVkGlobalPresence::task_cull
			|| specs.values[13].stage != AnoVkGlobalStage::task)
		return 4;
	if (ano_vk_global_stage_flags(AnoVkGlobalStage::geometry, 0x100u) != 0x100u)
		return 5;
	if (ano_vk_global_stage_flags(AnoVkGlobalStage::geometry_fragment, 0x100u)
			!= (0x100u | VK_SHADER_STAGE_FRAGMENT_BIT))
		return 6;
	return 0;
}
