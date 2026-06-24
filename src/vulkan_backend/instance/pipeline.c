/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <mimalloc-override.h>
#include "pipeline.h"
#include "pipelines/flat.h"
#include "pipelines/transmission.h"
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>



// TODO: add a struct to hold all discovered shaders and their buffers

// Utility functions

// TODO: add a generalized function to loop over 

bool loadFile(const char* filename, struct Buffer* buffer) 
{
	FILE* file = fopen(filename, "rb");
	if (file == NULL) 
	{
		fprintf(stderr, "Failed to open file: %s\n", filename);
		return false;
	}

	fseek(file, 0, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0, SEEK_SET);


	buffer->data = ano_aligned_malloc(size, alignof(uint32_t));
	if (buffer->data == NULL) 
	{
		fprintf(stderr, "Failed to allocate memory for file: %s\n", filename);
		fclose(file);
		return false;
	}

	if (fread(buffer->data, 1, size, file) != size) 
	{
		fprintf(stderr, "Failed to read file: %s\n", filename);
		free(buffer->data);
		fclose(file);
		return false;
	}

	//buffer->data[size] = 0;
	buffer->size = size;
	
	fclose(file);
	return true;
}

VkShaderModule createShaderModule(VkDevice device, struct Buffer* code) 
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code->size;
	createInfo.pCode = (uint32_t *) code->data; // cursed
	
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) 
	{
		printf("Failed to create shader module!\n");
		return NULL;
	}

	return shaderModule;
}


bool ano_vk_init_global_layout(VulkanContext* ctx, RendererState* state)
{
	// The per-vertex geometry work runs in the mesh stage on capable devices and in
	// the vertex stage on the fallback path. VK_SHADER_STAGE_MESH_BIT_EXT is invalid
	// on devices without the extension, so the stage flag must track the active path.
	VkShaderStageFlags geometryStage = ctx->deviceCapabilities.meshShader
		? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

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
	// froxel and loops [offset, offset+count) of the index list (see flat.frag / LIGHTING_SCALE.md).
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

	// 12: radiance-cascade scene voxel albedo sampler (RADIANCE_CASCADES.md M3a, debug voxel view).
	// 13: irradiance field sampler (M3b) — the GI ambient term. Both ×1 shared, fragment-sampled.
	VkDescriptorSetLayoutBinding rcVoxelLayoutBinding = {};
	rcVoxelLayoutBinding.binding = 12;
	rcVoxelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rcVoxelLayoutBinding.descriptorCount = 1;
	rcVoxelLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	rcVoxelLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding rcIrradianceLayoutBinding = rcVoxelLayoutBinding;
	rcIrradianceLayoutBinding.binding = 13;

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
		rcVoxelLayoutBinding,
		rcIrradianceLayoutBinding
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 14;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->globalSetLayout) != VK_SUCCESS)
	{
		printf("Failed to create global descriptor set layout!\n");
		return false;
	}

	return true;
}

bool ano_vk_init_cull_layout(VulkanContext* ctx, RendererState* state)
{
    VkDescriptorSetLayoutBinding bindings[10] = {};

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

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 10;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->culling.setLayout) != VK_SUCCESS)
    {
        printf("Failed to create cull descriptor set layout!\n");
        return false;
    }

    // --- Dynamic shadow set layouts (audit 4.7), created here so the FLAT/TRANSMISSION pipeline
    // layouts (set 2) and the shadowsetup compute pipeline can reference them. ---

    // shadowsetup compute set: 0 config (in), 1 transforms (in), 2 lights (in), 3 frustums (out).
    VkDescriptorSetLayoutBinding setupBindings[4] = {};
    for (uint32_t b = 0; b < 4; ++b) {
        setupBindings[b].binding = b;
        setupBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        setupBindings[b].descriptorCount = 1;
        setupBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setupInfo = {};
    setupInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setupInfo.bindingCount = 4;
    setupInfo.pBindings = setupBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &setupInfo, NULL, &state->shadowSetupSetLayout) != VK_SUCCESS)
        return false;

    // shadow geometry/sampling set (set 2): 0 shadow frustum viewProjs (geometry depth pass +
    // fragment sampling), 1 shadow atlas array (fragment), 2 per-light shadow info (fragment).
    VkShaderStageFlags geomStage = ctx->deviceCapabilities.meshShader
        ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutBinding geomBindings[3] = {};
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
    VkDescriptorSetLayoutCreateInfo geomInfo = {};
    geomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    geomInfo.bindingCount = 3;
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
	printf("Bindless texture array: maxTextures = %u (device update-after-bind limit %u)\n",
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
		printf("Failed to create bindless texture descriptor set layout!\n");
		return false;
	}

	state->prototypes[PIPELINE_FLAT].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_TRANSMISSION].descriptorLayout = state->bindlessTextures.layout;

	return true;
}


// TODO: write two garbo removers for the shader buffers and modules

// The juicy part

bool ano_vk_init_pipelines(VulkanContext* ctx, RendererState* state)
{
	if (!ano_pipeline_flat_init(ctx, state, &state->prototypes[PIPELINE_FLAT]))
	{
		return false;
	}

	if (!ano_pipeline_transmission_init(ctx, state, &state->prototypes[PIPELINE_TRANSMISSION]))
	{
		return false;
	}

    // Compute Update Pipeline
    VkDescriptorSetLayoutBinding updateBindings[4] = {};
    
    // 0: GlobalUBO
    updateBindings[0].binding = 0;
    updateBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    updateBindings[0].descriptorCount = 1;
    updateBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: TransformSSBO
    updateBindings[1].binding = 1;
    updateBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    updateBindings[1].descriptorCount = 1;
    updateBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: MotionSSBO
    updateBindings[2].binding = 2;
    updateBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    updateBindings[2].descriptorCount = 1;
    updateBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: PrevTransformSSBO
    updateBindings[3].binding = 3;
    updateBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    updateBindings[3].descriptorCount = 1;
    updateBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo updateLayoutInfo = {};
    updateLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    updateLayoutInfo.bindingCount = 4;
    updateLayoutInfo.pBindings = updateBindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &updateLayoutInfo, NULL, &state->updateSetLayout) != VK_SUCCESS)
    {
        printf("Failed to create update descriptor set layout!\n");
        return false;
    }

    VkPipelineLayoutCreateInfo updatePipelineLayoutInfo = {};
    updatePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    updatePipelineLayoutInfo.setLayoutCount = 1;
    updatePipelineLayoutInfo.pSetLayouts = &state->updateSetLayout;
    
    VkPushConstantRange updatePcRange = {};
    updatePcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    updatePcRange.offset = 0;
    updatePcRange.size = sizeof(uint32_t);
    
    updatePipelineLayoutInfo.pushConstantRangeCount = 1;
    updatePipelineLayoutInfo.pPushConstantRanges = &updatePcRange;

    if (vkCreatePipelineLayout(ctx->device, &updatePipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_UPDATE].layout) != VK_SUCCESS)
    {
        printf("Failed to create compute update pipeline layout!\n");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_UPDATE].type = PIPELINE_COMPUTE_UPDATE;
    state->prototypes[PIPELINE_COMPUTE_UPDATE].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_UPDATE].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_UPDATE].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer updateShaderCode;
    char updateShaderPath[256];
    snprintf(updateShaderPath, sizeof(updateShaderPath), "%s/resources/shaders/update.comp.spv", PROJECT_ROOT);
    if (!loadFile(updateShaderPath, &updateShaderCode)) return false;

    VkShaderModule updateShaderModule = createShaderModule(ctx->device, &updateShaderCode);

    VkComputePipelineCreateInfo updatePipelineInfo = {};
    updatePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    updatePipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_UPDATE].layout;
    updatePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    updatePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    updatePipelineInfo.stage.module = updateShaderModule;
    updatePipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo updateCacheInfo = {};
    updateCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &updateCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_UPDATE].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_UPDATE].cache, 1, &updatePipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_UPDATE].implementations[0].pipeline) != VK_SUCCESS) return false;
    
    state->prototypes[PIPELINE_COMPUTE_UPDATE].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(updateShaderCode.data);
    vkDestroyShaderModule(ctx->device, updateShaderModule, NULL);

    // Compute Scatter Pipeline (streamed transforms, Path B)
    VkDescriptorSetLayoutBinding scatterBindings[3] = {};
    for (int b = 0; b < 3; ++b) {
        scatterBindings[b].binding = (uint32_t)b;
        scatterBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        scatterBindings[b].descriptorCount = 1;
        scatterBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // 0: StreamSlots (per-frame), 1: StreamTransforms (xform ring, dynamic offset selects
    // the published slice), 2: TransformSSBO (written).
    scatterBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

    VkDescriptorSetLayoutCreateInfo scatterLayoutInfo = {};
    scatterLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    scatterLayoutInfo.bindingCount = 3;
    scatterLayoutInfo.pBindings = scatterBindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &scatterLayoutInfo, NULL, &state->scatterSetLayout) != VK_SUCCESS)
    {
        printf("Failed to create scatter descriptor set layout!\n");
        return false;
    }

    VkPushConstantRange scatterPcRange = {};
    scatterPcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    scatterPcRange.offset = 0;
    scatterPcRange.size = sizeof(uint32_t); // streamCount

    VkPipelineLayoutCreateInfo scatterPipelineLayoutInfo = {};
    scatterPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    scatterPipelineLayoutInfo.setLayoutCount = 1;
    scatterPipelineLayoutInfo.pSetLayouts = &state->scatterSetLayout;
    scatterPipelineLayoutInfo.pushConstantRangeCount = 1;
    scatterPipelineLayoutInfo.pPushConstantRanges = &scatterPcRange;

    if (vkCreatePipelineLayout(ctx->device, &scatterPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].layout) != VK_SUCCESS)
    {
        printf("Failed to create compute scatter pipeline layout!\n");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_SCATTER].type = PIPELINE_COMPUTE_SCATTER;
    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_SCATTER].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer scatterShaderCode;
    char scatterShaderPath[256];
    snprintf(scatterShaderPath, sizeof(scatterShaderPath), "%s/resources/shaders/scatter.comp.spv", PROJECT_ROOT);
    if (!loadFile(scatterShaderPath, &scatterShaderCode)) return false;

    VkShaderModule scatterShaderModule = createShaderModule(ctx->device, &scatterShaderCode);

    VkComputePipelineCreateInfo scatterPipelineInfo = {};
    scatterPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    scatterPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_SCATTER].layout;
    scatterPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    scatterPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    scatterPipelineInfo.stage.module = scatterShaderModule;
    scatterPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo scatterCacheInfo = {};
    scatterCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &scatterCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_SCATTER].cache, 1, &scatterPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations[0].pipeline) != VK_SUCCESS) return false;

    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(scatterShaderCode.data);
    vkDestroyShaderModule(ctx->device, scatterShaderModule, NULL);

    // Compute Culling Pipeline
    VkPipelineCacheCreateInfo compCacheInfo = {};
    compCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &compCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_CULL].cache);

    VkPipelineLayoutCreateInfo compLayoutInfo = {};
    compLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compLayoutInfo.setLayoutCount = 1;
    compLayoutInfo.pSetLayouts = &state->culling.setLayout;

    if (vkCreatePipelineLayout(ctx->device, &compLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_CULL].layout) != VK_SUCCESS)
    {
        printf("Failed to create compute cull pipeline layout!\n");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_CULL].type = PIPELINE_COMPUTE_CULL;
    state->prototypes[PIPELINE_COMPUTE_CULL].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_CULL].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_CULL].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer compShaderCode;
    char compShaderPath[256];
    snprintf(compShaderPath, sizeof(compShaderPath), "%s/resources/shaders/cull.comp.spv", PROJECT_ROOT);
    if (!loadFile(compShaderPath, &compShaderCode)) return false;

    VkShaderModule compShaderModule = createShaderModule(ctx->device, &compShaderCode);

    // constant_id 1: useMeshShader (selects the indirect command format the cull pass writes)
    VkBool32 compUseMeshShader = ctx->deviceCapabilities.meshShader ? VK_TRUE : VK_FALSE;

    VkSpecializationMapEntry compSpecMapEntry = {};
    compSpecMapEntry.constantID = 1;
    compSpecMapEntry.offset = 0;
    compSpecMapEntry.size = sizeof(VkBool32);

    VkSpecializationInfo compSpecInfo = {};
    compSpecInfo.mapEntryCount = 1;
    compSpecInfo.pMapEntries = &compSpecMapEntry;
    compSpecInfo.dataSize = sizeof(VkBool32);
    compSpecInfo.pData = &compUseMeshShader;

    VkComputePipelineCreateInfo computePipelineInfo = {};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_CULL].layout;
    computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computePipelineInfo.stage.module = compShaderModule;
    computePipelineInfo.stage.pName = "main";
    computePipelineInfo.stage.pSpecializationInfo = &compSpecInfo;

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_CULL].cache, 1, &computePipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_CULL].implementations[0].pipeline) != VK_SUCCESS) return false;
    
    state->prototypes[PIPELINE_COMPUTE_CULL].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(compShaderCode.data);
    vkDestroyShaderModule(ctx->device, compShaderModule, NULL);

    // Compute Light-cull Pipeline (clustered-forward froxel light assignment).
    // 0: GlobalUBO (in)  1: TransformSSBO (in, light world pos)  2: LightSSBO (in)
    // 3: clusterLightCount (out)  4: clusterLightIndices (out)
    VkDescriptorSetLayoutBinding lightcullBindings[5] = {};
    for (uint32_t b = 0; b < 5; ++b) {
        lightcullBindings[b].binding = b;
        lightcullBindings[b].descriptorType = (b == 0)
            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightcullBindings[b].descriptorCount = 1;
        lightcullBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lightcullLayoutInfo = {};
    lightcullLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightcullLayoutInfo.bindingCount = 5;
    lightcullLayoutInfo.pBindings = lightcullBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &lightcullLayoutInfo, NULL, &state->lightcullSetLayout) != VK_SUCCESS)
        return false;

    VkPipelineLayoutCreateInfo lightcullPipelineLayoutInfo = {};
    lightcullPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightcullPipelineLayoutInfo.setLayoutCount = 1;
    lightcullPipelineLayoutInfo.pSetLayouts = &state->lightcullSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &lightcullPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].type = PIPELINE_COMPUTE_LIGHTCULL;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer lightcullShaderCode;
    char lightcullShaderPath[256];
    snprintf(lightcullShaderPath, sizeof(lightcullShaderPath), "%s/resources/shaders/lightcull.comp.spv", PROJECT_ROOT);
    if (!loadFile(lightcullShaderPath, &lightcullShaderCode)) return false;
    VkShaderModule lightcullShaderModule = createShaderModule(ctx->device, &lightcullShaderCode);

    VkComputePipelineCreateInfo lightcullPipelineInfo = {};
    lightcullPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    lightcullPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout;
    lightcullPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    lightcullPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    lightcullPipelineInfo.stage.module = lightcullShaderModule;
    lightcullPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo lightcullCacheInfo = {};
    lightcullCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &lightcullCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].cache, 1, &lightcullPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(lightcullShaderCode.data);
    vkDestroyShaderModule(ctx->device, lightcullShaderModule, NULL);

    // Compute Shadow-setup Pipeline (audit 4.7): builds each shadow frustum's light-space viewProj
    // + planes from the light's live transform. Set layout created in ano_vk_init_cull_layout.
    VkPipelineLayoutCreateInfo shadowSetupLayoutInfo = {};
    shadowSetupLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowSetupLayoutInfo.setLayoutCount = 1;
    shadowSetupLayoutInfo.pSetLayouts = &state->shadowSetupSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &shadowSetupLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].type = PIPELINE_COMPUTE_SHADOWSETUP;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer shadowSetupCode;
    char shadowSetupPath[256];
    snprintf(shadowSetupPath, sizeof(shadowSetupPath), "%s/resources/shaders/shadowsetup.comp.spv", PROJECT_ROOT);
    if (!loadFile(shadowSetupPath, &shadowSetupCode)) return false;
    VkShaderModule shadowSetupModule = createShaderModule(ctx->device, &shadowSetupCode);

    VkComputePipelineCreateInfo shadowSetupPipelineInfo = {};
    shadowSetupPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    shadowSetupPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].layout;
    shadowSetupPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowSetupPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shadowSetupPipelineInfo.stage.module = shadowSetupModule;
    shadowSetupPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo shadowSetupCacheInfo = {};
    shadowSetupCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &shadowSetupCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].cache, 1, &shadowSetupPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(shadowSetupCode.data);
    vkDestroyShaderModule(ctx->device, shadowSetupModule, NULL);

	return true;
}

// Bespoke fullscreen tonemap pass: encodes the HDR resolve target to the swapchain. Not a
// PipelineType prototype (no cull/compaction, no g_framePasses entry) — standalone state on
// RendererState. Vertex stage is a vertex-bufferless fullscreen triangle (gl_VertexIndex);
// fragment samples set 0 binding 0 (the per-frame hdrColorView) and writes the swapchain.
// in:  ctx, state (imageFormat = swapchain target format must be set)
// out: true on success; populates state->tonemap{SetLayout,Layout,Cache,Pipeline}
bool ano_vk_init_tonemap(VulkanContext* ctx, RendererState* state)
{
	// Set layout: one combined image sampler (the HDR resolve), fragment-only.
	VkDescriptorSetLayoutBinding samplerBinding = {};
	samplerBinding.binding = 0;
	samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = 1;
	setLayoutInfo.pBindings = &samplerBinding;
	if (vkCreateDescriptorSetLayout(ctx->device, &setLayoutInfo, NULL, &state->tonemapSetLayout) != VK_SUCCESS)
	{
		printf("Failed to create tonemap descriptor set layout!\n");
		return false;
	}

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &state->tonemapSetLayout;
	if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->tonemapLayout) != VK_SUCCESS)
	{
		printf("Failed to create tonemap pipeline layout!\n");
		return false;
	}

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->tonemapCache);

	struct Buffer vertCode, fragCode;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/tonemap.vert.spv", PROJECT_ROOT);
	if (!loadFile(path, &vertCode)) return false;
	snprintf(path, sizeof(path), "%s/resources/shaders/tonemap.frag.spv", PROJECT_ROOT);
	if (!loadFile(path, &fragCode)) return false;
	VkShaderModule vertModule = createShaderModule(ctx->device, &vertCode);
	VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertModule;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragModule;
	stages[1].pName = "main";

	// No vertex buffers: the vertex shader synthesizes a fullscreen triangle from gl_VertexIndex.
	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	// Tonemap writes the single-sample swapchain directly: no MSAA, no resolve.
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment = {};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	// Tonemap targets the swapchain LDR format (no depth attachment).
	VkFormat colorFormat = state->imageFormat;
	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &colorFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state->tonemapLayout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->tonemapCache, 1, &pipelineInfo, NULL, &state->tonemapPipeline);

	ano_aligned_free(vertCode.data);
	ano_aligned_free(fragCode.data);
	vkDestroyShaderModule(ctx->device, vertModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);

	if (r != VK_SUCCESS)
	{
		printf("Failed to create tonemap pipeline!\n");
		return false;
	}
	return true;
}

// Dynamic shadow depth pipeline (audit 4.7). Reuses the FLAT geometry stage (flat.mesh/flat.vert)
// with the shadowPass specialization constant set, so the depth render projects by a shadow
// frustum's viewProj into the single-sample shadow atlas. No color attachment; depth bias on to
// suppress acne. Reuses the FLAT pipeline layout (sets 0/1/2 + the 2-uint push). Also creates the
// depth-compare sampler the fragment shaders PCF with. Must run after ano_vk_init_pipelines.
// in:  ctx, state (prototypes[PIPELINE_FLAT].layout must exist)
// out: true on success; populates state->shadow{Pipeline,Cache,Sampler}
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state)
{
	// PCF depth-compare sampler: linear filtering does the 2x2 bilinear comparison per tap.
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // frag depth <= occluder -> lit
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxLod = 1.0f;
	if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &state->shadowSampler) != VK_SUCCESS)
		return false;

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->shadowCache);

	bool useMesh = ctx->deviceCapabilities.meshShader;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	struct Buffer geomCode, fragCode;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/%s.spv", PROJECT_ROOT, useMesh ? "flat.mesh" : "flat.vert");
	if (!loadFile(path, &geomCode)) return false;
	snprintf(path, sizeof(path), "%s/resources/shaders/shadow_depth.frag.spv", PROJECT_ROOT);
	if (!loadFile(path, &fragCode)) return false;
	VkShaderModule geomModule = createShaderModule(ctx->device, &geomCode);
	VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

	// shadowPass = true (constant_id 0 in flat.mesh / flat.vert).
	VkBool32 shadowPassTrue = VK_TRUE;
	VkSpecializationMapEntry specEntry = { .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
	VkSpecializationInfo specInfo = { .mapEntryCount = 1, .pMapEntries = &specEntry, .dataSize = sizeof(VkBool32), .pData = &shadowPassTrue };

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = geometryStage;
	stages[0].module = geomModule;
	stages[0].pName = "main";
	stages[0].pSpecializationInfo = &specInfo;
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragModule;
	stages[1].pName = "main";

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;       // demo geometry is double-sided
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;
	rasterizer.depthBiasEnable = VK_TRUE;          // suppress shadow acne
	rasterizer.depthBiasConstantFactor = 1.5f;
	rasterizer.depthBiasSlopeFactor = 2.5f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // shadow atlas is single-sample

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.maxDepthBounds = 1.0f;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 0; // depth only

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkFormat depthFormat = ANO_SHADOW_DEPTH_FORMAT;
	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 0;
	renderingInfo.depthAttachmentFormat = depthFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = useMesh ? NULL : &vertexInput;
	pipelineInfo.pInputAssemblyState = useMesh ? NULL : &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state->prototypes[PIPELINE_FLAT].layout; // reuse flat's sets 0/1/2 + push
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipeline);

	ano_aligned_free(geomCode.data);
	ano_aligned_free(fragCode.data);
	vkDestroyShaderModule(ctx->device, geomModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);

	if (r != VK_SUCCESS) { printf("Failed to create shadow depth pipeline!\n"); return false; }
	return true;
}

// Radiance-cascade voxelize pipeline (RADIANCE_CASCADES.md M2). Reuses the FLAT geometry stage
// (flat.mesh/flat.vert) with shadowPass = true, so the geometry projects by an ortho clipmap
// viewProj (set 2, binding 0) instead of the camera, paired with voxelize.frag which imageStores
// albedo/opacity + emission into the two scene voxel volumes (set 2, bindings 1/2). No colour or
// depth attachment — pure imageStore. Standalone (not a prototype), like the shadow + tonemap passes.
// Must run after ano_vk_init_pipelines (needs globalSetLayout + the bindless layout).
bool ano_vk_init_rc_voxelize(VulkanContext* ctx, RendererState* state)
{
	// Set 2: 0 = ortho frustum buffer (geometry stage), 1 = albedo image, 2 = emission image (frag).
	VkDescriptorSetLayoutBinding setBindings[3] = {};
	setBindings[0].binding = 0;
	setBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	setBindings[0].descriptorCount = 1;
	setBindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_VERTEX_BIT;
	setBindings[1].binding = 1;
	setBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	setBindings[1].descriptorCount = 1;
	setBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	setBindings[2].binding = 2;
	setBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	setBindings[2].descriptorCount = 1;
	setBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 3;
	setInfo.pBindings = setBindings;
	if (vkCreateDescriptorSetLayout(ctx->device, &setInfo, NULL, &state->rcVoxelizeSetLayout) != VK_SUCCESS) return false;

	bool useMesh = ctx->deviceCapabilities.meshShader;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// Same 2-uint push as flat (transformBaseOffset + shadowFrustumIndex == axis here).
	VkPushConstantRange pushRange = { .stageFlags = geometryStage, .offset = 0, .size = 2u * sizeof(uint32_t) };
	VkDescriptorSetLayout setLayouts[3] = { state->globalSetLayout, state->bindlessTextures.layout, state->rcVoxelizeSetLayout };
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 3;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;
	if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->rcVoxelizeLayout) != VK_SUCCESS) return false;

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->rcVoxelizeCache);

	struct Buffer geomCode, fragCode;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/%s.spv", PROJECT_ROOT, useMesh ? "flat.mesh" : "flat.vert");
	if (!loadFile(path, &geomCode)) return false;
	snprintf(path, sizeof(path), "%s/resources/shaders/voxelize.frag.spv", PROJECT_ROOT);
	if (!loadFile(path, &fragCode)) return false;
	VkShaderModule geomModule = createShaderModule(ctx->device, &geomCode);
	VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

	VkBool32 shadowPassTrue = VK_TRUE; // reuse the shadowPass path: project by set-2 frustum viewProj
	VkSpecializationMapEntry specEntry = { .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
	VkSpecializationInfo specInfo = { .mapEntryCount = 1, .pMapEntries = &specEntry, .dataSize = sizeof(VkBool32), .pData = &shadowPassTrue };

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = geometryStage;
	stages[0].module = geomModule;
	stages[0].pName = "main";
	stages[0].pSpecializationInfo = &specInfo;
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragModule;
	stages[1].pName = "main";

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;      // voxelize both faces
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // no attachments, single-sample raster

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 0; // no colour attachment; imageStore side effects only

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 0; // no attachments

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = useMesh ? NULL : &vertexInput;
	pipelineInfo.pInputAssemblyState = useMesh ? NULL : &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state->rcVoxelizeLayout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->rcVoxelizeCache, 1, &pipelineInfo, NULL, &state->rcVoxelizePipeline);

	ano_aligned_free(geomCode.data);
	ano_aligned_free(fragCode.data);
	vkDestroyShaderModule(ctx->device, geomModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);

	if (r != VK_SUCCESS) { printf("Failed to create rc voxelize pipeline!\n"); return false; }
	return true;
}

// Radiance-cascade trace pipeline (RADIANCE_CASCADES.md M3b). Compute: gathers the two scene voxel
// volumes (sampled) into the irradiance field (storage image). Standalone, like voxelize. Run after
// Load one compute SPIR-V from resources/shaders and build a pipeline against `layout`/`cache`.
static bool rc_make_compute(VulkanContext* ctx, const char* spv, VkPipelineLayout layout, VkPipelineCache cache, VkPipeline* out)
{
	struct Buffer code;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/%s", PROJECT_ROOT, spv);
	if (!loadFile(path, &code)) return false;
	VkShaderModule module = createShaderModule(ctx->device, &code);
	VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pci.layout = layout;
	pci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pci.stage.module = module;
	pci.stage.pName = "main";
	VkResult r = vkCreateComputePipelines(ctx->device, cache, 1, &pci, NULL, out);
	ano_aligned_free(code.data);
	vkDestroyShaderModule(ctx->device, module, NULL);
	return r == VK_SUCCESS;
}

// Radiance-cascade compute set + the three pipelines that share it (trace/merge/integrate).
// in: ctx, state; out: rcTrace{SetLayout,Layout,Cache,Pipeline}, rcMergePipeline, rcIntegratePipeline.
// Set 0: 0/1 = voxel albedo+emission (sampled), 2 = irradiance (STORAGE_IMAGE), 3 = cascade array
// (STORAGE_IMAGE[ANO_RC_CASCADE_COUNT]). Push constant: uint cascade level.
bool ano_vk_init_rc_trace(VulkanContext* ctx, RendererState* state)
{
	VkDescriptorSetLayoutBinding b[4] = {};
	b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[0].descriptorCount = 1;                      b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[1].descriptorCount = 1;                      b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[2].binding = 2; b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          b[2].descriptorCount = 1;                      b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[3].binding = 3; b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          b[3].descriptorCount = ANO_RC_CASCADE_COUNT;   b[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 4;
	setInfo.pBindings = b;
	if (vkCreateDescriptorSetLayout(ctx->device, &setInfo, NULL, &state->rcTraceSetLayout) != VK_SUCCESS) return false;

	VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) }; // cascade level
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &state->rcTraceSetLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->rcTraceLayout) != VK_SUCCESS) return false;

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->rcTraceCache);

	if (!rc_make_compute(ctx, "rc_trace.comp.spv",     state->rcTraceLayout, state->rcTraceCache, &state->rcTracePipeline))     { printf("Failed to create rc trace pipeline!\n");     return false; }
	if (!rc_make_compute(ctx, "rc_merge.comp.spv",     state->rcTraceLayout, state->rcTraceCache, &state->rcMergePipeline))     { printf("Failed to create rc merge pipeline!\n");     return false; }
	if (!rc_make_compute(ctx, "rc_integrate.comp.spv", state->rcTraceLayout, state->rcTraceCache, &state->rcIntegratePipeline)) { printf("Failed to create rc integrate pipeline!\n"); return false; }
	return true;
}

void ano_vk_cleanup_pipelines(VulkanContext* ctx, RendererState* state)
{
	// Tonemap pass (standalone)
	if (state->tonemapPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->tonemapPipeline, NULL);
		state->tonemapPipeline = VK_NULL_HANDLE;
	}
	if (state->tonemapLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx->device, state->tonemapLayout, NULL);
		state->tonemapLayout = VK_NULL_HANDLE;
	}
	if (state->tonemapSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->tonemapSetLayout, NULL);
		state->tonemapSetLayout = VK_NULL_HANDLE;
	}
	if (state->tonemapCache != VK_NULL_HANDLE)
	{
		vkDestroyPipelineCache(ctx->device, state->tonemapCache, NULL);
		state->tonemapCache = VK_NULL_HANDLE;
	}

	// Shadow depth pass (standalone): pipeline + cache + sampler + the two shadow set layouts.
	// (The shadowsetup compute prototype is freed by the generic prototype loop below.)
	if (state->shadowPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->shadowPipeline, NULL);
		state->shadowPipeline = VK_NULL_HANDLE;
	}
	if (state->shadowCache != VK_NULL_HANDLE)
	{
		vkDestroyPipelineCache(ctx->device, state->shadowCache, NULL);
		state->shadowCache = VK_NULL_HANDLE;
	}
	if (state->shadowSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(ctx->device, state->shadowSampler, NULL);
		state->shadowSampler = VK_NULL_HANDLE;
	}
	if (state->shadowGeomSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->shadowGeomSetLayout, NULL);
		state->shadowGeomSetLayout = VK_NULL_HANDLE;
	}
	if (state->shadowSetupSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->shadowSetupSetLayout, NULL);
		state->shadowSetupSetLayout = VK_NULL_HANDLE;
	}

	// Global layout
	if (state->globalSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->globalSetLayout, NULL);
		state->globalSetLayout = VK_NULL_HANDLE;
	}

    if (state->culling.setLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->culling.setLayout, NULL);
        state->culling.setLayout = VK_NULL_HANDLE;
    }
    
    if (state->updateSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->updateSetLayout, NULL);
        state->updateSetLayout = VK_NULL_HANDLE;
    }

    if (state->scatterSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->scatterSetLayout, NULL);
        state->scatterSetLayout = VK_NULL_HANDLE;
    }

    if (state->lightcullSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->lightcullSetLayout, NULL);
        state->lightcullSetLayout = VK_NULL_HANDLE;
    }

    // Radiance-cascade voxelize pipeline (standalone, like shadow/tonemap) + its set layout.
    if (state->rcVoxelizePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(ctx->device, state->rcVoxelizePipeline, NULL);
        state->rcVoxelizePipeline = VK_NULL_HANDLE;
    }
    if (state->rcVoxelizeLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(ctx->device, state->rcVoxelizeLayout, NULL);
        state->rcVoxelizeLayout = VK_NULL_HANDLE;
    }
    if (state->rcVoxelizeCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(ctx->device, state->rcVoxelizeCache, NULL);
        state->rcVoxelizeCache = VK_NULL_HANDLE;
    }
    if (state->rcVoxelizeSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->rcVoxelizeSetLayout, NULL);
        state->rcVoxelizeSetLayout = VK_NULL_HANDLE;
    }

    // Radiance-cascade trace/merge/integrate pipelines (standalone compute) + their shared set layout.
    if (state->rcTracePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(ctx->device, state->rcTracePipeline, NULL);
        state->rcTracePipeline = VK_NULL_HANDLE;
    }
    if (state->rcMergePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(ctx->device, state->rcMergePipeline, NULL);
        state->rcMergePipeline = VK_NULL_HANDLE;
    }
    if (state->rcIntegratePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(ctx->device, state->rcIntegratePipeline, NULL);
        state->rcIntegratePipeline = VK_NULL_HANDLE;
    }
    if (state->rcTraceLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(ctx->device, state->rcTraceLayout, NULL);
        state->rcTraceLayout = VK_NULL_HANDLE;
    }
    if (state->rcTraceCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(ctx->device, state->rcTraceCache, NULL);
        state->rcTraceCache = VK_NULL_HANDLE;
    }
    if (state->rcTraceSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->rcTraceSetLayout, NULL);
        state->rcTraceSetLayout = VK_NULL_HANDLE;
    }

	// Material layouts
	if (state->bindlessTextures.layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->bindlessTextures.layout, NULL);
		state->bindlessTextures.layout = VK_NULL_HANDLE;
	}

	// Bindless descriptor pool
	if (state->bindlessTextures.pool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(ctx->device, state->bindlessTextures.pool, NULL);
		state->bindlessTextures.pool = VK_NULL_HANDLE;
	}

	// Global descriptor pool
	if (state->globalDescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(ctx->device, state->globalDescriptorPool, NULL);
		state->globalDescriptorPool = VK_NULL_HANDLE;
	}

	// Pipelines
	for (int i = 0; i < PIPELINE_TYPE_COUNT; ++i)
	{
		if (state->prototypes[i].cache != VK_NULL_HANDLE)
		{
			vkDestroyPipelineCache(ctx->device, state->prototypes[i].cache, NULL);
			state->prototypes[i].cache = VK_NULL_HANDLE;
		}

		if (state->prototypes[i].layout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(ctx->device, state->prototypes[i].layout, NULL);
			state->prototypes[i].layout = VK_NULL_HANDLE;
		}

		if (state->prototypes[i].implementations != NULL)
		{
			for (uint32_t j = 0; j < state->prototypes[i].implementationCount; ++j)
			{
				if (state->prototypes[i].implementations[j].pipeline != VK_NULL_HANDLE)
				{
					vkDestroyPipeline(ctx->device, state->prototypes[i].implementations[j].pipeline, NULL);
					state->prototypes[i].implementations[j].pipeline = VK_NULL_HANDLE;
				}
			}
			free(state->prototypes[i].implementations);
			state->prototypes[i].implementations = NULL;
			state->prototypes[i].implementationCount = 0;
		}
	}
}
