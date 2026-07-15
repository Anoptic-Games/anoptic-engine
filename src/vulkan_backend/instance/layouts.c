/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include "pipeline.h"
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

	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	uboLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding ssboLayoutBinding = {};
	ssboLayoutBinding.binding = 1;
	ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ssboLayoutBinding.descriptorCount = 1;
	// Fragment-visible: lights derive world pose from their entity transform.
	ssboLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	ssboLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding materialLayoutBinding = {};
	materialLayoutBinding.binding = 2;
	materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialLayoutBinding.descriptorCount = 1;
	// Geometry reads doubleSided for meshlet cone culling, fragment reads the rest.
	materialLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	materialLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding entityLayoutBinding = {};
	entityLayoutBinding.binding = 3;
	entityLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	entityLayoutBinding.descriptorCount = 1;
	entityLayoutBinding.stageFlags = geometryStage;
	entityLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding vertexBufferLayoutBinding = {};
	vertexBufferLayoutBinding.binding = 4;
	vertexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	vertexBufferLayoutBinding.descriptorCount = 1;
	vertexBufferLayoutBinding.stageFlags = geometryStage;
	vertexBufferLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding indexBufferLayoutBinding = {};
	indexBufferLayoutBinding.binding = 5;
	indexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indexBufferLayoutBinding.descriptorCount = 1;
	indexBufferLayoutBinding.stageFlags = geometryStage;
	indexBufferLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding meshDataLayoutBinding = {};
	meshDataLayoutBinding.binding = 6;
	meshDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	meshDataLayoutBinding.descriptorCount = 1;
	meshDataLayoutBinding.stageFlags = geometryStage;
	meshDataLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding compactedEntityIndicesLayoutBinding = {};
	compactedEntityIndicesLayoutBinding.binding = 7;
	compactedEntityIndicesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	compactedEntityIndicesLayoutBinding.descriptorCount = 1;
	compactedEntityIndicesLayoutBinding.stageFlags = geometryStage;
	compactedEntityIndicesLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding lightLayoutBinding = {};
	lightLayoutBinding.binding = 8;
	lightLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightLayoutBinding.descriptorCount = 1;
	lightLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	lightLayoutBinding.pImmutableSamplers = NULL;

	// Per-entity instance channel (tint/flags/scalars).
	VkDescriptorSetLayoutBinding instanceDataLayoutBinding = {};
	instanceDataLayoutBinding.binding = 9;
	instanceDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	instanceDataLayoutBinding.descriptorCount = 1;
	instanceDataLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	instanceDataLayoutBinding.pImmutableSamplers = NULL;

	// 10/11: clustered-forward froxel light lists, fragment-only.
	VkDescriptorSetLayoutBinding clusterCountLayoutBinding = {};
	clusterCountLayoutBinding.binding = 10;
	clusterCountLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	clusterCountLayoutBinding.descriptorCount = 1;
	clusterCountLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	clusterCountLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding clusterIndexLayoutBinding = {};
	clusterIndexLayoutBinding.binding = 11;
	clusterIndexLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	clusterIndexLayoutBinding.descriptorCount = 1;
	clusterIndexLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	clusterIndexLayoutBinding.pImmutableSamplers = NULL;

	// 12: per-light LightRuntime record, precomputed by lightsetup.comp, fragment-only.
	VkDescriptorSetLayoutBinding lightRuntimeLayoutBinding = {};
	lightRuntimeLayoutBinding.binding = 12;
	lightRuntimeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightRuntimeLayoutBinding.descriptorCount = 1;
	lightRuntimeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	lightRuntimeLayoutBinding.pImmutableSamplers = NULL;

	// 13: this view's Hi-Z pyramid for the task meshlet cull occlusion test.
	VkDescriptorSetLayoutBinding hizPyramidLayoutBinding = {};
	hizPyramidLayoutBinding.binding = 13;
	hizPyramidLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	hizPyramidLayoutBinding.descriptorCount = 1;
	hizPyramidLayoutBinding.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT;
	hizPyramidLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding bindings[14] = {
		uboLayoutBinding,
		ssboLayoutBinding,
		materialLayoutBinding,
		entityLayoutBinding,
		vertexBufferLayoutBinding,
		indexBufferLayoutBinding,
		meshDataLayoutBinding,
		compactedEntityIndicesLayoutBinding,
		lightLayoutBinding,
		instanceDataLayoutBinding,
		clusterCountLayoutBinding,
		clusterIndexLayoutBinding,
		lightRuntimeLayoutBinding,
		hizPyramidLayoutBinding
	};

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
    VkDescriptorSetLayoutBinding bindings[12] = {};

    // 0: CullUBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: TransformSSBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: EntitySSBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: MeshSSBO
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 4: MeshBoundsSSBO
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 5: IndirectBuffer
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 6: DrawCount
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 7: CompactedEntityIndices
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 8: MaterialSSBO
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 9: ShadowFrustumSSBO, GPU-built shadow frustums the cull tests against.
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 10: SortKeys, cull writes per-draw depth keys, tpsort.comp reads them.
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 11: Hi-Z occlusion pyramids, one combined-image-sampler per camera view.
    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[11].descriptorCount = ANO_VIEW_COUNT;
    bindings[11].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 12;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->culling.setLayout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create cull descriptor set layout!");
        return false;
    }

    // Hi-Z pyramid build set: 0 sampled all-mip view, 1 this mip's r32f storage dest, 2 MSAA camera depth.
    VkDescriptorSetLayoutBinding hizBindings[3] = {};
    hizBindings[0].binding = 0;
    hizBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hizBindings[0].descriptorCount = 1;
    hizBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    hizBindings[1].binding = 1;
    hizBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    hizBindings[1].descriptorCount = 1;
    hizBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    hizBindings[2].binding = 2;
    hizBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hizBindings[2].descriptorCount = 1;
    hizBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo hizLayoutInfo = {};
    hizLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    hizLayoutInfo.bindingCount = 3;
    hizLayoutInfo.pBindings = hizBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &hizLayoutInfo, NULL, &state->hizSetLayout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create Hi-Z descriptor set layout!");
        return false;
    }

    // --- Dynamic shadow set layouts ---

    // shadowsetup compute set: 0 config, 1 transforms, 2 lights, 3 frustums, 4 packed sampling viewProjs.
    VkDescriptorSetLayoutBinding setupBindings[5] = {};
    for (uint32_t b = 0; b < 5; ++b) {
        setupBindings[b].binding = b;
        setupBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        setupBindings[b].descriptorCount = 1;
        setupBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setupInfo = {};
    setupInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setupInfo.bindingCount = 5;
    setupInfo.pBindings = setupBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &setupInfo, NULL, &state->shadowSetupSetLayout) != VK_SUCCESS)
        return false;

    // Shadow geom set 2: 0 viewProjs, 1 atlas, 2 light info, 3 sampling viewProjs UBO.
    VkShaderStageFlags geomStage = (ctx->deviceCapabilities.meshShader
        ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
        | (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
    VkDescriptorSetLayoutBinding geomBindings[4] = {};
    geomBindings[0].binding = 0;
    geomBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    geomBindings[0].descriptorCount = 1;
    geomBindings[0].stageFlags = geomStage | VK_SHADER_STAGE_FRAGMENT_BIT;
    geomBindings[1].binding = 1;
    geomBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    geomBindings[1].descriptorCount = 1;
    geomBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    geomBindings[2].binding = 2;
    geomBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    geomBindings[2].descriptorCount = 1;
    geomBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    geomBindings[3].binding = 3;
    geomBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    geomBindings[3].descriptorCount = 1;
    geomBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo geomInfo = {};
    geomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    geomInfo.bindingCount = 4;
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
