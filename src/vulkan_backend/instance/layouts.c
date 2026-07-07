/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_logging.h>
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
	// The per-vertex geometry work runs in the mesh stage on capable devices and in
	// the vertex stage on the fallback path. VK_SHADER_STAGE_MESH_BIT_EXT is invalid
	// on devices without the extension, so the stage flag must track the active path.
	// The task meshlet cull (review priority 10) reads a subset of these bindings ahead
	// of the mesh stage; folding TASK into the shared flag keeps every mesh-path binding
	// task-visible (surplus visibility on the few it doesn't read is legal and free).
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
	// Also visible to the fragment stage: lights derive world position/direction
	// from their driving entity's transform (transforms[transformIndex]).
	ssboLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	ssboLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding materialLayoutBinding = {};
	materialLayoutBinding.binding = 2;
	materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialLayoutBinding.descriptorCount = 1;
	// Geometry stage reads doubleSided for per-meshlet cone culling (flat.mesh); fragment reads the rest.
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

	// Open-ended per-entity instance channel (tint/flags/scalars). Fragment reads
	// the packed tint/flags now; geometry stage is included so a future per-slot
	// scalar (e.g. anim phase) needs no layout change.
	VkDescriptorSetLayoutBinding instanceDataLayoutBinding = {};
	instanceDataLayoutBinding.binding = 9;
	instanceDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	instanceDataLayoutBinding.descriptorCount = 1;
	instanceDataLayoutBinding.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT;
	instanceDataLayoutBinding.pImmutableSamplers = NULL;

	// 10/11: clustered-forward froxel light lists. Fragment-only: the fragment maps to its
	// froxel and loops [offset, offset+count) of the index list (see flat.frag).
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

	// 12: per-light LightRuntime record (world pose + color*intensity + range/cone/type), precomputed by
	// lightsetup.comp. Fragment-only: replaces the per-fragment LightData load + transform derivation.
	VkDescriptorSetLayoutBinding lightRuntimeLayoutBinding = {};
	lightRuntimeLayoutBinding.binding = 12;
	lightRuntimeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightRuntimeLayoutBinding.descriptorCount = 1;
	lightRuntimeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	lightRuntimeLayoutBinding.pImmutableSamplers = NULL;

	// 13: this view's Hi-Z pyramid (lag slot's — same image the entity cull samples at its binding
	// 11), for the task meshlet cull's occlusion test. Only present when the task path is on: the
	// TASK stage flag is invalid without the mesh-shader extension.
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

    // 9: ShadowFrustumSSBO (audit 4.7): GPU-built shadow frustums the cull tests against.
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 10: SortKeys (audit 4.7 transparency sort): cull writes per-draw depth keys, tpsort.comp reads them.
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 11: Hi-Z occlusion pyramids (review 4.9 step 3), one combined-image-sampler per camera view (the
    // cull samples hizPyramid[view] = last frame's pyramid). tpsort reuses this layout but never binds
    // it (it declares only a CullUBO prefix), so the extra binding is inert there.
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

    // Hi-Z occlusion pyramid build set (review 4.9 step 3): one per mip per view per frame. 0 = the
    // pyramid sampled all-mip view (downsample reads mip srcMip), 1 = this mip's r32f storage dest,
    // 2 = the MSAA camera depth (reduce reads it for mip 0). Both build pipelines (reduce/downsample)
    // share this layout — each uses one of bindings 0/2; the other stays bound but unread.
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

    // --- Dynamic shadow set layouts (audit 4.7), created here so the FLAT/TRANSMISSION pipeline
    // layouts (set 2) and the shadowsetup compute pipeline can reference them. ---

    // shadowsetup compute set: 0 config (in), 1 transforms (in), 2 lights (in), 3 frustums (out),
    // 4 packed sampling viewProjs (out).
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

    // shadow geometry/sampling set (set 2): 0 shadow frustum viewProjs (geometry depth pass +
    // the task meshlet cull, which tests shadow draws against the frustum planes), 1 shadow
    // atlas array (fragment), 2 per-light shadow info (fragment), 3 packed sampling viewProjs
    // as a UBO (fragment — constant-bank matrix operands instead of a per-lane register block).
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
	// Bindless upper bound. A combined image sampler counts against both the sampler and the
	// sampled-image update-after-bind limits, per stage and per set; clamp the 4096 target to
	// the smallest of the four. Apple/MoltenVK caps these at 1024, so an unclamped 4096 trips
	// VUID-VkPipelineLayoutCreateInfo-descriptorType-03022 / -pSetLayouts-03036.
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

	state->bindlessTextures.maxTextures = uabLimit < 4096u ? uabLimit : 4096u;
	state->bindlessTextures.textureCount = 0;
	ano_log(ANO_INFO, "Bindless texture array: maxTextures = %u (device update-after-bind limit %u)",
		state->bindlessTextures.maxTextures, uabLimit);

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorCount = state->bindlessTextures.maxTextures;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = NULL;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

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
