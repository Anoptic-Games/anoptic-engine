/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include "pipeline.h"
#include "pipelines/flat.h"
#include "pipelines/transmission.h"
#include "pipelines/additive.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

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

// Task meshlet-cull stage (review priority 10): loads flat.task and fills a TASK stage with the
// lane's {shadowPass, coneCull} specialization. One shared module load per pipeline builder; the
// caller destroys *outModule after pipeline creation. See pipeline.h for the storage contract.
bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
                             TaskStageStorage* store, VkShaderModule* outModule,
                             VkPipelineShaderStageCreateInfo* stage)
{
	struct Buffer code;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/flat.task.spv", PROJECT_ROOT);
	if (!loadFile(path, &code)) return false;
	*outModule = createShaderModule(ctx->device, &code);
	ano_aligned_free(code.data);
	if (*outModule == NULL) return false;

	store->entries[0] = (VkSpecializationMapEntry){ .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
	store->entries[1] = (VkSpecializationMapEntry){ .constantID = 1, .offset = sizeof(VkBool32), .size = sizeof(VkBool32) };
	store->data[0] = shadowPass;
	store->data[1] = coneCull;
	store->spec = (VkSpecializationInfo){ .mapEntryCount = 2, .pMapEntries = store->entries,
		.dataSize = sizeof(store->data), .pData = store->data };

	*stage = (VkPipelineShaderStageCreateInfo){ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_TASK_BIT_EXT, .module = *outModule, .pName = "main",
		.pSpecializationInfo = &store->spec };
	return true;
}


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
		printf("Failed to create global descriptor set layout!\n");
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
        printf("Failed to create cull descriptor set layout!\n");
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
        printf("Failed to create Hi-Z descriptor set layout!\n");
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
    // fragment sampling; + the task meshlet cull, which tests shadow draws against the frustum
    // planes), 1 shadow atlas array (fragment), 2 per-light shadow info (fragment).
    VkShaderStageFlags geomStage = (ctx->deviceCapabilities.meshShader
        ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
        | (state->taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
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
	state->prototypes[PIPELINE_FLAT_TWOSIDED].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_TRANSMISSION].descriptorLayout = state->bindlessTextures.layout;
	state->prototypes[PIPELINE_ADDITIVE].descriptorLayout = state->bindlessTextures.layout;

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

	if (!ano_pipeline_flat_twosided_init(ctx, state, &state->prototypes[PIPELINE_FLAT_TWOSIDED]))
	{
		return false;
	}

	if (!ano_pipeline_transmission_init(ctx, state, &state->prototypes[PIPELINE_TRANSMISSION]))
	{
		return false;
	}

	if (!ano_pipeline_additive_init(ctx, state, &state->prototypes[PIPELINE_ADDITIVE]))
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

    // Compute Hi-Z Pyramid Build Pipeline (review 4.9 step 3). One module, two implementations via the
    // isReduce spec constant (constant_id 0): [0] = reduce (MSAA depth -> mip 0), [1] = downsample
    // (mip srcMip -> srcMip+1). Push constant (24 B) = { int srcMip; ivec2 dstSize; ivec2 srcSize; }
    // matching hiz.comp's std430 block. Shares state->hizSetLayout across both.
    VkPipelineCacheCreateInfo hizCacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(ctx->device, &hizCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].cache);

    VkPushConstantRange hizPush = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 24 };
    VkPipelineLayoutCreateInfo hizPipeLayoutInfo = {};
    hizPipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    hizPipeLayoutInfo.setLayoutCount = 1;
    hizPipeLayoutInfo.pSetLayouts = &state->hizSetLayout;
    hizPipeLayoutInfo.pushConstantRangeCount = 1;
    hizPipeLayoutInfo.pPushConstantRanges = &hizPush;
    if (vkCreatePipelineLayout(ctx->device, &hizPipeLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].layout) != VK_SUCCESS)
    {
        printf("Failed to create Hi-Z pipeline layout!\n");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_HIZ].type = PIPELINE_COMPUTE_HIZ;
    state->prototypes[PIPELINE_COMPUTE_HIZ].implementationCount = 2;
    state->prototypes[PIPELINE_COMPUTE_HIZ].implementations = calloc(2, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_HIZ].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer hizShaderCode;
    char hizShaderPath[256];
    snprintf(hizShaderPath, sizeof(hizShaderPath), "%s/resources/shaders/hiz.comp.spv", PROJECT_ROOT);
    if (!loadFile(hizShaderPath, &hizShaderCode)) return false;
    VkShaderModule hizShaderModule = createShaderModule(ctx->device, &hizShaderCode);

    // Avenue 1: when depth MAX-resolve is supported, BOTH Hi-Z pipelines use the RESOLVED_DEPTH build so
    // binding 2 is a single-sample sampler2D (the reduce reads the resolved depth; the downsample never
    // samples binding 2 but must declare the same type + the layout the bound single-sample view provides,
    // so both bind the resolve view — see updateHiZDescriptorSets). Without resolve support both use the
    // base sampler2DMS module. Load the variant only when it will actually be used.
    struct Buffer hizResolveCode = {0};
    VkShaderModule hizResolveModule = VK_NULL_HANDLE;
    if (ctx->deviceCapabilities.depthMaxResolve)
    {
        char hizResolvePath[256];
        snprintf(hizResolvePath, sizeof(hizResolvePath), "%s/resources/shaders/hiz_resolve.comp.spv", PROJECT_ROOT);
        if (!loadFile(hizResolvePath, &hizResolveCode)) return false;
        hizResolveModule = createShaderModule(ctx->device, &hizResolveCode);
    }

    // Two spec constants: id 0 isReduce, id 1 msaaSamples (reduce source sample count, baked so the
    // per-sample fetch loop unrolls; VkSampleCountFlagBits enum values equal the counts). msaaSamples
    // was set in pickPhysicalDevice, before this function runs.
    struct HizSpecData { VkBool32 isReduce; int32_t msaaSamples; };
    VkSpecializationMapEntry hizSpecMap[2] = {
        { .constantID = 0, .offset = offsetof(struct HizSpecData, isReduce),    .size = sizeof(VkBool32) },
        { .constantID = 1, .offset = offsetof(struct HizSpecData, msaaSamples), .size = sizeof(int32_t)  },
    };
    for (uint32_t impl = 0; impl < 2; impl++)
    {
        struct HizSpecData hizSpecData = {
            .isReduce    = (impl == 0u) ? VK_TRUE : VK_FALSE, // [0] reduce, [1] downsample
            .msaaSamples = (int32_t)ctx->msaaSamples,
        };
        VkSpecializationInfo hizSpec = {};
        hizSpec.mapEntryCount = 2;
        hizSpec.pMapEntries = hizSpecMap;
        hizSpec.dataSize = sizeof(hizSpecData);
        hizSpec.pData = &hizSpecData;

        VkComputePipelineCreateInfo hizPipeInfo = {};
        hizPipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        hizPipeInfo.layout = state->prototypes[PIPELINE_COMPUTE_HIZ].layout;
        hizPipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        hizPipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        hizPipeInfo.stage.module = (hizResolveModule != VK_NULL_HANDLE) ? hizResolveModule : hizShaderModule;
        hizPipeInfo.stage.pName = "main";
        hizPipeInfo.stage.pSpecializationInfo = &hizSpec;

        if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_HIZ].cache, 1, &hizPipeInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].pipeline) != VK_SUCCESS) return false;
        state->prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }

    ano_aligned_free(hizShaderCode.data);
    vkDestroyShaderModule(ctx->device, hizShaderModule, NULL);
    if (hizResolveModule != VK_NULL_HANDLE)
    {
        ano_aligned_free(hizResolveCode.data);
        vkDestroyShaderModule(ctx->device, hizResolveModule, NULL);
    }

    // Compute Transparency-Sort Pipeline (audit 4.7). Reuses the cull descriptor set layout (it
    // operates on the same indirect / drawCount / compacted / sortKeys buffers); one workgroup per
    // camera view sorts that view's transmission partition back-to-front. No push constants (view =
    // gl_WorkGroupID.x); shares cull's useMeshShader spec constant so the command stride matches.
    VkPipelineCacheCreateInfo tpsortCacheInfo = {};
    tpsortCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &tpsortCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].cache);

    VkPipelineLayoutCreateInfo tpsortLayoutInfo = {};
    tpsortLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tpsortLayoutInfo.setLayoutCount = 1;
    tpsortLayoutInfo.pSetLayouts = &state->culling.setLayout;
    if (vkCreatePipelineLayout(ctx->device, &tpsortLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].layout) != VK_SUCCESS)
    {
        printf("Failed to create transparency-sort pipeline layout!\n");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_TPSORT].type = PIPELINE_COMPUTE_TPSORT;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_TPSORT].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer tpsortShaderCode;
    char tpsortShaderPath[256];
    snprintf(tpsortShaderPath, sizeof(tpsortShaderPath), "%s/resources/shaders/tpsort.comp.spv", PROJECT_ROOT);
    if (!loadFile(tpsortShaderPath, &tpsortShaderCode)) return false;
    VkShaderModule tpsortShaderModule = createShaderModule(ctx->device, &tpsortShaderCode);

    VkBool32 tpsortUseMeshShader = ctx->deviceCapabilities.meshShader ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry tpsortSpecMapEntry = {};
    tpsortSpecMapEntry.constantID = 1;
    tpsortSpecMapEntry.offset = 0;
    tpsortSpecMapEntry.size = sizeof(VkBool32);
    VkSpecializationInfo tpsortSpecInfo = {};
    tpsortSpecInfo.mapEntryCount = 1;
    tpsortSpecInfo.pMapEntries = &tpsortSpecMapEntry;
    tpsortSpecInfo.dataSize = sizeof(VkBool32);
    tpsortSpecInfo.pData = &tpsortUseMeshShader;

    VkComputePipelineCreateInfo tpsortPipelineInfo = {};
    tpsortPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    tpsortPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_TPSORT].layout;
    tpsortPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tpsortPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    tpsortPipelineInfo.stage.module = tpsortShaderModule;
    tpsortPipelineInfo.stage.pName = "main";
    tpsortPipelineInfo.stage.pSpecializationInfo = &tpsortSpecInfo;

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_TPSORT].cache, 1, &tpsortPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(tpsortShaderCode.data);
    vkDestroyShaderModule(ctx->device, tpsortShaderModule, NULL);

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

    // Compute Light-setup Pipeline: per-light world pose (worldPos/worldDir) precompute, so the
    // fragment passes stop reloading the 64B transform + re-deriving lightPos/lightForward per fragment.
    // 0: TransformSSBO (in)  1: LightSSBO (in)  2: LightRuntimeSSBO (out, 64B/light). Push constant: light count.
    VkDescriptorSetLayoutBinding lightsetupBindings[3] = {};
    for (uint32_t b = 0; b < 3; ++b) {
        lightsetupBindings[b].binding = b;
        lightsetupBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightsetupBindings[b].descriptorCount = 1;
        lightsetupBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lightsetupLayoutInfo = {};
    lightsetupLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightsetupLayoutInfo.bindingCount = 3;
    lightsetupLayoutInfo.pBindings = lightsetupBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &lightsetupLayoutInfo, NULL, &state->lightsetupSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange lightsetupPush = {};
    lightsetupPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    lightsetupPush.offset = 0;
    lightsetupPush.size = sizeof(uint32_t); // lightCount

    VkPipelineLayoutCreateInfo lightsetupPipelineLayoutInfo = {};
    lightsetupPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightsetupPipelineLayoutInfo.setLayoutCount = 1;
    lightsetupPipelineLayoutInfo.pSetLayouts = &state->lightsetupSetLayout;
    lightsetupPipelineLayoutInfo.pushConstantRangeCount = 1;
    lightsetupPipelineLayoutInfo.pPushConstantRanges = &lightsetupPush;
    if (vkCreatePipelineLayout(ctx->device, &lightsetupPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].type = PIPELINE_COMPUTE_LIGHTSETUP;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].supportedFeatures = PBR_FEATURE_NONE;

    struct Buffer lightsetupShaderCode;
    char lightsetupShaderPath[256];
    snprintf(lightsetupShaderPath, sizeof(lightsetupShaderPath), "%s/resources/shaders/lightsetup.comp.spv", PROJECT_ROOT);
    if (!loadFile(lightsetupShaderPath, &lightsetupShaderCode)) return false;
    VkShaderModule lightsetupShaderModule = createShaderModule(ctx->device, &lightsetupShaderCode);

    VkComputePipelineCreateInfo lightsetupPipelineInfo = {};
    lightsetupPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    lightsetupPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].layout;
    lightsetupPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    lightsetupPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    lightsetupPipelineInfo.stage.module = lightsetupShaderModule;
    lightsetupPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo lightsetupCacheInfo = {};
    lightsetupCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &lightsetupCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].cache, 1, &lightsetupPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(lightsetupShaderCode.data);
    vkDestroyShaderModule(ctx->device, lightsetupShaderModule, NULL);

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

// Dynamic shadow depth pipeline (audit 4.7). Uses the depth-only FLAT geometry variant
// (flat_depth.mesh/flat_depth.vert = ANO_DEPTH_ONLY compile, position-only outputs)
// with the shadowPass specialization constant set, so the depth render projects by a shadow
// frustum's viewProj into the single-sample shadow atlas. No color attachment; depth bias on to
// suppress acne. Reuses the FLAT pipeline layout (sets 0/1/2 + the 2-uint push). Also creates the
// depth-compare sampler the fragment shaders PCF with. Must run after ano_vk_init_pipelines.
// in:  ctx, state (prototypes[PIPELINE_FLAT].layout must exist)
// out: true on success; populates state->shadow{Pipeline,Cache,Sampler}
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state)
{
	// Plain linear/clamp sampler for the moment atlas: the CDF reconstruction happens in-shader, so
	// there is no hardware depth-compare. Linear filtering gives the 2x2 bilinear of the (affine,
	// filterable) optimized moments — both for the lighting frags and the separable blur taps.
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxLod = 1.0f;
	if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &state->shadowSampler) != VK_SUCCESS)
		return false;

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->shadowCache);

	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// Depth-only geometry variant (ANO_DEPTH_ONLY compile of flat.mesh/flat.vert): position-only
	// outputs — the shadow render consumes no attributes, and slimming the ISBE payload here is a
	// direct occupancy win on the caster geometry. shadow_depth.frag declares no inputs to match.
	struct Buffer geomCode, fragCode;
	char path[256];
	snprintf(path, sizeof(path), "%s/resources/shaders/%s.spv", PROJECT_ROOT,
		useMesh ? (useTask ? "flat_depth_task.mesh" : "flat_depth.mesh") : "flat_depth.vert");
	if (!loadFile(path, &geomCode)) return false;
	snprintf(path, sizeof(path), "%s/resources/shaders/shadow_depth.frag.spv", PROJECT_ROOT);
	if (!loadFile(path, &fragCode)) return false;
	VkShaderModule geomModule = createShaderModule(ctx->device, &geomCode);
	VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

	// Task meshlet cull (review priority 10), shadow variant: frustum-only against the draw's
	// shadow frustum planes — one shadow partition mixes both sidedness lanes' casters (cone
	// would hole doubleSided shadows) and no shadow Hi-Z exists.
	VkShaderModule taskModule = VK_NULL_HANDLE;
	TaskStageStorage taskStore;
	VkPipelineShaderStageCreateInfo taskStageInfo = {};
	if (useTask && !ano_pipeline_task_stage(ctx, VK_TRUE, VK_FALSE, &taskStore, &taskModule, &taskStageInfo))
		return false;

	// shadowPass = true (constant_id 0 in flat.mesh / flat.vert).
	VkBool32 shadowPassTrue = VK_TRUE;
	VkSpecializationMapEntry specEntry = { .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
	VkSpecializationInfo specInfo = { .mapEntryCount = 1, .pMapEntries = &specEntry, .dataSize = sizeof(VkBool32), .pData = &shadowPassTrue };

	VkPipelineShaderStageCreateInfo stages[3] = {};
	stages[0] = taskStageInfo; // leading slot; skipped when the task path is off
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = geometryStage;
	stages[1].module = geomModule;
	stages[1].pName = "main";
	stages[1].pSpecializationInfo = &specInfo;
	stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[2].module = fragModule;
	stages[2].pName = "main";

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	// NONE stays (review finding 7): each frustum has ONE shadow partition mixing both sidedness
	// lanes' casters, and a doubleSided caster (curtains, foliage) culled here would drop its shadow.
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;
	// No rasterizer depth bias: MSM acne suppression lives in the moment bias (alpha) + a sample-time
	// depth offset, so the stored moments describe the true geometric depth (gl_FragCoord.z).

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // shadow atlas is single-sample

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.maxDepthBounds = 1.0f;

	// Two CDF-stats color attachments (the two atlas sublayers = 4 depth bands, MRT), blending disabled —
	// the depth test keeps the nearest occluder, so each write is a plain overwrite of that fragment. The
	// blur pipeline below reuses statsBlend[0] (single attachment). All bands share the same blend state.
	VkPipelineColorBlendAttachmentState statsBlend[2] = {};
	for (int i = 0; i < 2; i++) {
		statsBlend[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		statsBlend[i].blendEnable = VK_FALSE;
	}
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 2;
	colorBlending.pAttachments = statsBlend;

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

	// Two CDF stats color targets (MRT sublayers) + a transient depth (nearest-occluder select; never
	// sampled). The blur pipeline below reuses statsFormat[0] (single attachment).
	VkFormat statsFormat[2] = { ANO_SHADOW_STATS_FORMAT, ANO_SHADOW_STATS_FORMAT };
	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = statsFormat;
	renderingInfo.depthAttachmentFormat = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = useTask ? 3 : 2;
	pipelineInfo.pStages = useTask ? stages : &stages[1];
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
	if (taskModule != VK_NULL_HANDLE)
		vkDestroyShaderModule(ctx->device, taskModule, NULL);

	if (r != VK_SUCCESS) { printf("Failed to create shadow depth pipeline!\n"); return false; }

	// --- Moment prefilter pipeline: fullscreen separable box over the atlas (X then Y) ---
	// One combined-image-sampler (the blur source array) at set 0, plus a 16-byte push (dir + layer).
	// Vertex stage: shadowblur.vert (fullscreen triangle + gl_Layer from the push constant) when the
	// device has vertex-stage gl_Layer, so both blur directions render as ONE layered pass each;
	// else the plain fullscreen triangle (tonemap.vert) with per-layer passes. Frag: shadowblur.frag.
	VkDescriptorSetLayoutBinding blurBinding = {};
	blurBinding.binding = 0;
	blurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	blurBinding.descriptorCount = 1;
	blurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo blurSetInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1, .pBindings = &blurBinding };
	if (vkCreateDescriptorSetLayout(ctx->device, &blurSetInfo, NULL, &state->shadowBlurSetLayout) != VK_SUCCESS) {
		printf("Failed to create shadow blur set layout!\n"); return false; }

	// vec2 dir + int layer + int pad. VERTEX included for shadowblur.vert's gl_Layer routing; the
	// fallback vertex stage ignores it (the record loop always pushes VERTEX|FRAGMENT to match).
	VkPushConstantRange blurPush = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo blurLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1, .pSetLayouts = &state->shadowBlurSetLayout,
		.pushConstantRangeCount = 1, .pPushConstantRanges = &blurPush };
	if (vkCreatePipelineLayout(ctx->device, &blurLayoutInfo, NULL, &state->shadowBlurLayout) != VK_SUCCESS) {
		printf("Failed to create shadow blur pipeline layout!\n"); return false; }

	struct Buffer blurVertCode, blurFragCode;
	snprintf(path, sizeof(path), "%s/resources/shaders/%s.spv", PROJECT_ROOT,
		ctx->deviceCapabilities.shaderOutputLayer ? "shadowblur.vert" : "tonemap.vert");
	if (!loadFile(path, &blurVertCode)) return false;
	snprintf(path, sizeof(path), "%s/resources/shaders/shadowblur.frag.spv", PROJECT_ROOT);
	if (!loadFile(path, &blurFragCode)) return false;
	VkShaderModule blurVert = createShaderModule(ctx->device, &blurVertCode);
	VkShaderModule blurFrag = createShaderModule(ctx->device, &blurFragCode);

	VkPipelineShaderStageCreateInfo blurStages[2] = {};
	blurStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	blurStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	blurStages[0].module = blurVert; blurStages[0].pName = "main";
	blurStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	blurStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	blurStages[1].module = blurFrag; blurStages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo blurVertexInput = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo blurIA = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkPipelineViewportStateCreateInfo blurVP = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1 };
	VkPipelineRasterizationStateCreateInfo blurRaster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo blurMS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
	VkPipelineColorBlendStateCreateInfo blurCB = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &statsBlend[0] };
	VkPipelineDepthStencilStateCreateInfo blurDS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE };
	VkPipelineDynamicStateCreateInfo blurDyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, .pDynamicStates = dynamicStates };
	VkPipelineRenderingCreateInfo blurRendering = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1, .pColorAttachmentFormats = &statsFormat[0] };

	VkGraphicsPipelineCreateInfo blurPipeline = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &blurRendering };
	blurPipeline.stageCount = 2; blurPipeline.pStages = blurStages;
	blurPipeline.pVertexInputState = &blurVertexInput;
	blurPipeline.pInputAssemblyState = &blurIA;
	blurPipeline.pViewportState = &blurVP;
	blurPipeline.pRasterizationState = &blurRaster;
	blurPipeline.pMultisampleState = &blurMS;
	blurPipeline.pDepthStencilState = &blurDS;
	blurPipeline.pColorBlendState = &blurCB;
	blurPipeline.pDynamicState = &blurDyn;
	blurPipeline.layout = state->shadowBlurLayout;
	blurPipeline.renderPass = VK_NULL_HANDLE;

	VkResult br = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &blurPipeline, NULL, &state->shadowBlurPipeline);
	ano_aligned_free(blurVertCode.data);
	ano_aligned_free(blurFragCode.data);
	vkDestroyShaderModule(ctx->device, blurVert, NULL);
	vkDestroyShaderModule(ctx->device, blurFrag, NULL);
	if (br != VK_SUCCESS) { printf("Failed to create shadow blur pipeline!\n"); return false; }

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
	// Hi-Z build set layout (review 4.9 step 3). Its PIPELINE_COMPUTE_HIZ prototype (layout/cache/2
	// implementations) is freed by the generic prototype loop below.
	if (state->hizSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->hizSetLayout, NULL);
		state->hizSetLayout = VK_NULL_HANDLE;
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
	if (state->shadowBlurPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->shadowBlurPipeline, NULL);
		state->shadowBlurPipeline = VK_NULL_HANDLE;
	}
	if (state->shadowBlurLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx->device, state->shadowBlurLayout, NULL);
		state->shadowBlurLayout = VK_NULL_HANDLE;
	}
	if (state->shadowBlurSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->shadowBlurSetLayout, NULL);
		state->shadowBlurSetLayout = VK_NULL_HANDLE;
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

    if (state->lightsetupSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx->device, state->lightsetupSetLayout, NULL);
        state->lightsetupSetLayout = VK_NULL_HANDLE;
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
