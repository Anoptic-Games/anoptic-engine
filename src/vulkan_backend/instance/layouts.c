/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include "pipeline.h"
#include "descriptor_layout_schema.h"
#include "pipelines/flat.h"
#include "pipelines/transmission.h"
#include "pipelines/additive.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Inputs: compile-time schema table and geometry-stage expansion for this device.
// Output: fully initialized Vulkan layout bindings in schema order.
template<size_t Count>
static void ano_vk_materialize_layout_bindings(
	const AnoVkDescriptorBindingTable<Count>& specs,
	VkDescriptorSetLayoutBinding (&bindings)[Count],
	VkShaderStageFlags geometryStage)
{
	for (size_t i = 0; i < Count; ++i) {
		bindings[i].binding = specs.values[i].binding;
		bindings[i].descriptorType = specs.values[i].descriptorType;
		bindings[i].descriptorCount = specs.values[i].descriptorCount;
		bindings[i].stageFlags = ano_vk_descriptor_stage_flags(
			specs.values[i].stage, geometryStage);
		bindings[i].pImmutableSamplers = NULL;
	}
}

bool ano_vk_init_global_layout(VulkanContext* ctx, RendererState* state)
{
	// Geometry stage: mesh on capable devices, vertex fallback, plus task for the meshlet cull.
	VkShaderStageFlags geometryStage = (ctx->deviceCapabilities.meshShader
		? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
		| (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);

	const auto& specs = ano_vk_global_binding_specs();
	VkDescriptorSetLayoutBinding bindings[14] = {};
	ano_vk_materialize_layout_bindings(specs, bindings, geometryStage);

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = state->taskCull ? 14 : 13;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->globalSetLayout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create global descriptor set layout!");
		return false;
	}

	return true;
}

bool ano_vk_init_cull_layout(VulkanContext* ctx, RendererState* state)
{
	const auto& cullSpecs = ano_vk_cull_binding_specs<ANO_VIEW_COUNT>();
	VkDescriptorSetLayoutBinding bindings[12] = {};
	ano_vk_materialize_layout_bindings(cullSpecs, bindings, 0);

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = (uint32_t)cullSpecs.count;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->culling.setLayout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create cull descriptor set layout!");
		return false;
	}

	const auto& hizSpecs = ano_vk_hiz_binding_specs();
	VkDescriptorSetLayoutBinding hizBindings[3] = {};
	ano_vk_materialize_layout_bindings(hizSpecs, hizBindings, 0);

	VkDescriptorSetLayoutCreateInfo hizLayoutInfo = {};
	hizLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	hizLayoutInfo.bindingCount = (uint32_t)hizSpecs.count;
	hizLayoutInfo.pBindings = hizBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &hizLayoutInfo, NULL, &state->hizSetLayout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create Hi-Z descriptor set layout!");
		return false;
	}

	const auto& setupSpecs = ano_vk_shadow_setup_binding_specs();
	VkDescriptorSetLayoutBinding setupBindings[5] = {};
	ano_vk_materialize_layout_bindings(setupSpecs, setupBindings, 0);
	VkDescriptorSetLayoutCreateInfo setupInfo = {};
	setupInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setupInfo.bindingCount = (uint32_t)setupSpecs.count;
	setupInfo.pBindings = setupBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &setupInfo, NULL, &state->shadowSetupSetLayout) != VK_SUCCESS)
		return false;

	VkShaderStageFlags geomStage = (ctx->deviceCapabilities.meshShader
		? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
		| (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
	const auto& geomSpecs = ano_vk_shadow_geometry_binding_specs();
	VkDescriptorSetLayoutBinding geomBindings[4] = {};
	ano_vk_materialize_layout_bindings(geomSpecs, geomBindings, geomStage);
	VkDescriptorSetLayoutCreateInfo geomInfo = {};
	geomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	geomInfo.bindingCount = (uint32_t)geomSpecs.count;
	geomInfo.pBindings = geomBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &geomInfo, NULL, &state->shadowGeomSetLayout) != VK_SUCCESS)
		return false;

	return true;
}


bool ano_vk_init_material_layouts(VulkanContext* ctx, RendererState* state)
{
	// Bindless upper bound: clamp the 4096 target to the smallest update-after-bind limit.
	VkPhysicalDeviceDescriptorIndexingProperties indexingProps = {};
	indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

	VkPhysicalDeviceProperties2 deviceProps2 = {};
	deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProps2.pNext = &indexingProps;
	vkGetPhysicalDeviceProperties2(ctx->physicalDevice, &deviceProps2);

	uint32_t uabLimit = indexingProps.maxPerStageDescriptorUpdateAfterBindSamplers;
	if (indexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages < uabLimit)
		uabLimit = indexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages;
	if (indexingProps.maxDescriptorSetUpdateAfterBindSamplers < uabLimit)
		uabLimit = indexingProps.maxDescriptorSetUpdateAfterBindSamplers;
	if (indexingProps.maxDescriptorSetUpdateAfterBindSampledImages < uabLimit)
		uabLimit = indexingProps.maxDescriptorSetUpdateAfterBindSampledImages;

	// Reserve headroom for the fixed samplers sharing pipeline layouts (shadow atlas, Hi-Z).
	const uint32_t fixedSamplerReserve = 16u;
	uint32_t uabBudget = uabLimit > fixedSamplerReserve ? uabLimit - fixedSamplerReserve : 1u;

	state->bindlessTextures.maxTextures = uabBudget < 4096u ? uabBudget : 4096u;
	state->bindlessTextures.textureCount = 0;
	ano_log(ANO_INFO, "Bindless texture array: maxTextures = %u (device update-after-bind limit %u)",
		state->bindlessTextures.maxTextures, uabLimit);

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorCount = state->bindlessTextures.maxTextures;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = NULL;
	// FRAGMENT for geometry sampling, COMPUTE for UI overlay image prims.
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorBindingFlags bindlessFlags = 
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo = {};
	extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	extendedInfo.bindingCount = 1;
	extendedInfo.pBindingFlags = &bindlessFlags;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &extendedInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &samplerLayoutBinding;

	if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->bindlessTextures.layout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create bindless texture descriptor set layout!");
		return false;
	}

	state->prototypes[PIPELINE_FLAT].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_FLAT_TWOSIDED].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_FLAT_MASKED].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_TRANSMISSION].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_ADDITIVE].descriptorLayout = state->bindlessTextures.layout;

	return true;
}
