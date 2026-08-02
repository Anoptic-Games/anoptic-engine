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

bool ano_vk_init_global_layout(VulkanContext* ctx, RendererState* state)
{
	// Geometry stage: mesh on capable devices, vertex fallback, plus task for the meshlet cull.
	VkShaderStageFlags geometryStage = (ctx->deviceCapabilities.meshShader
		? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
		| (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);

	const auto& specs = ANO_VK_GLOBAL_BINDINGS;
	VkDescriptorSetLayoutBinding bindings[ANO_VK_GLOBAL_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(specs, bindings, geometryStage))
		return false;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = specs.count
		- (state->taskCull ? 0 : AnoVkGlobalSetSchema::optionalTail);
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
	const auto& cullSpecs = ANO_VK_CULL_BINDINGS<ANO_VIEW_COUNT>;
	VkDescriptorSetLayoutBinding bindings[ANO_VK_CULL_BINDINGS<ANO_VIEW_COUNT>.count] = {};
	if (!ano_vk_materialize_layout_bindings(cullSpecs, bindings))
		return false;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = cullSpecs.count;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->culling.setLayout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create cull descriptor set layout!");
		return false;
	}

	const auto& hizSpecs = ANO_VK_HIZ_BINDINGS;
	VkDescriptorSetLayoutBinding hizBindings[ANO_VK_HIZ_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(hizSpecs, hizBindings))
		return false;

	VkDescriptorSetLayoutCreateInfo hizLayoutInfo = {};
	hizLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	hizLayoutInfo.bindingCount = hizSpecs.count;
	hizLayoutInfo.pBindings = hizBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &hizLayoutInfo, NULL, &state->hizSetLayout) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create Hi-Z descriptor set layout!");
		return false;
	}

	const auto& setupSpecs = ANO_VK_SHADOW_SETUP_BINDINGS;
	VkDescriptorSetLayoutBinding setupBindings[ANO_VK_SHADOW_SETUP_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(setupSpecs, setupBindings))
		return false;
	VkDescriptorSetLayoutCreateInfo setupInfo = {};
	setupInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setupInfo.bindingCount = setupSpecs.count;
	setupInfo.pBindings = setupBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &setupInfo, NULL, &state->shadowSetupSetLayout) != VK_SUCCESS)
		return false;

	VkShaderStageFlags geomStage = (ctx->deviceCapabilities.meshShader
		? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
		| (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
	const auto& geomSpecs = ANO_VK_SHADOW_GEOMETRY_BINDINGS;
	VkDescriptorSetLayoutBinding geomBindings[ANO_VK_SHADOW_GEOMETRY_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(geomSpecs, geomBindings, geomStage))
		return false;
	VkDescriptorSetLayoutCreateInfo geomInfo = {};
	geomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	geomInfo.bindingCount = geomSpecs.count;
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

	const auto& bindlessSpecs = ANO_VK_BINDLESS_BINDINGS;
	VkDescriptorSetLayoutBinding bindlessBindings[ANO_VK_BINDLESS_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(
			bindlessSpecs, bindlessBindings, 0, state->bindlessTextures.maxTextures))
		return false;
	VkDescriptorBindingFlags bindlessFlags[ANO_VK_BINDLESS_BINDINGS.count] = {};
	ano_vk_materialize_layout_binding_flags(bindlessSpecs, bindlessFlags);

	VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo = {};
	extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	extendedInfo.bindingCount = bindlessSpecs.count;
	extendedInfo.pBindingFlags = bindlessFlags;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &extendedInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layoutInfo.bindingCount = bindlessSpecs.count;
	layoutInfo.pBindings = bindlessBindings;

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
