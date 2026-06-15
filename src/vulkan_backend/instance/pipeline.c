/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <mimalloc-override.h>
#include "pipeline.h"

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
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	uboLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding ssboLayoutBinding = {};
	ssboLayoutBinding.binding = 1;
	ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ssboLayoutBinding.descriptorCount = 1;
	ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	ssboLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding materialLayoutBinding = {};
	materialLayoutBinding.binding = 2;
	materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialLayoutBinding.descriptorCount = 1;
	materialLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	materialLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding entityLayoutBinding = {};
	entityLayoutBinding.binding = 3;
	entityLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	entityLayoutBinding.descriptorCount = 1;
	entityLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	entityLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding vertexBufferLayoutBinding = {};
	vertexBufferLayoutBinding.binding = 4;
	vertexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	vertexBufferLayoutBinding.descriptorCount = 1;
	vertexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	vertexBufferLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding indexBufferLayoutBinding = {};
	indexBufferLayoutBinding.binding = 5;
	indexBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indexBufferLayoutBinding.descriptorCount = 1;
	indexBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	indexBufferLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding meshDataLayoutBinding = {};
	meshDataLayoutBinding.binding = 6;
	meshDataLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	meshDataLayoutBinding.descriptorCount = 1;
	meshDataLayoutBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	meshDataLayoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutBinding bindings[7] = {
		uboLayoutBinding, 
		ssboLayoutBinding, 
		materialLayoutBinding, 
		entityLayoutBinding,
		vertexBufferLayoutBinding,
		indexBufferLayoutBinding,
		meshDataLayoutBinding
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 7;
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
    VkDescriptorSetLayoutBinding bindings[7] = {};

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

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->culling.setLayout) != VK_SUCCESS)
    {
        printf("Failed to create cull descriptor set layout!\n");
        return false;
    }

    return true;
}


bool ano_vk_init_material_layouts(VulkanContext* ctx, RendererState* state)
{
	state->bindlessTextures.maxTextures = 4096;
	state->bindlessTextures.textureCount = 0;

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

	return true;
}


// TODO: write two garbo removers for the shader buffers and modules

// The juicy part

bool ano_vk_init_pipelines(VulkanContext* ctx, RendererState* state)
{
	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->prototypes[PIPELINE_FLAT].cache);

	// 2. Setup layout
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(uint32_t);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 2;
	VkDescriptorSetLayout setLayouts[2] = {state->globalSetLayout, state->prototypes[PIPELINE_FLAT].descriptorLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_FLAT].layout) != VK_SUCCESS) 
	{
		printf("Failed to create pipeline layout!\n");
		return false;
	}

	state->prototypes[PIPELINE_FLAT].type = PIPELINE_FLAT;
	state->prototypes[PIPELINE_FLAT].implementationCount = 2;
	state->prototypes[PIPELINE_FLAT].implementations = calloc(2, sizeof(PipelineImplementation));
	state->prototypes[PIPELINE_FLAT].supportedFeatures = 
		PBR_FEATURE_BASE_COLOR_FACTOR | 
		PBR_FEATURE_BASE_COLOR_TEXTURE | 
		PBR_FEATURE_ALPHA_MODE_OPAQUE | 
		PBR_FEATURE_ALPHA_MODE_BLEND;

	// Load shaders
	struct Buffer meshShaderCode;
	char meshShaderPath[256];
	snprintf(meshShaderPath, sizeof(meshShaderPath), "%s/resources/shaders/flat.mesh.spv", PROJECT_ROOT);
	if (!loadFile(meshShaderPath, &meshShaderCode)) return false;

	struct Buffer fragShaderCode;
	char fragShaderPath[256];
	snprintf(fragShaderPath, sizeof(fragShaderPath), "%s/resources/shaders/flat.frag.spv", PROJECT_ROOT);
	if (!loadFile(fragShaderPath, &fragShaderCode)) return false;

	VkShaderModule meshShaderModule = createShaderModule(ctx->device, &meshShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(ctx->device, &fragShaderCode);

	VkSpecializationMapEntry specMapEntry = {};
	specMapEntry.constantID = 0;
	specMapEntry.offset = 0;
	specMapEntry.size = sizeof(VkBool32);

	VkBool32 useFirstInstance = ctx->deviceCapabilities.drawIndirectFirstInstance ? VK_TRUE : VK_FALSE;

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = 1;
	specInfo.pMapEntries = &specMapEntry;
	specInfo.dataSize = sizeof(VkBool32);
	specInfo.pData = &useFirstInstance;

	VkPipelineShaderStageCreateInfo meshShaderStageInfo = {};
	meshShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	meshShaderStageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
	meshShaderStageInfo.module = meshShaderModule;
	meshShaderStageInfo.pName = "main";
	meshShaderStageInfo.pSpecializationInfo = &specInfo;

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = {meshShaderStageInfo, fragShaderStageInfo};

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float) state->imageExtent.width;
	viewport.height = (float) state->imageExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = state->imageExtent;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = ctx->msaaSamples;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = NULL;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkFormat colorFormat = state->imageFormat;
	VkFormat depthFormat = state->depthFormat;

	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &colorFormat;
	renderingInfo.depthAttachmentFormat = depthFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = NULL; // Mesh shaders do not take vertex input
	pipelineInfo.pInputAssemblyState = NULL; // Mesh shaders do not take input assembly
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state->prototypes[PIPELINE_FLAT].layout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;
	pipelineInfo.subpass = 0;

	// Opaque variant (index 0)
	if (vkCreateGraphicsPipelines(ctx->device, state->prototypes[PIPELINE_FLAT].cache, 1, &pipelineInfo, NULL, &state->prototypes[PIPELINE_FLAT].implementations[0].pipeline) != VK_SUCCESS) return false;
	state->prototypes[PIPELINE_FLAT].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	state->prototypes[PIPELINE_FLAT].implementations[0].depthWrite = VK_TRUE;
	state->prototypes[PIPELINE_FLAT].implementations[0].blendEnable = VK_FALSE;

	// Blended variant (index 1)
	depthStencil.depthWriteEnable = VK_FALSE;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlending.pAttachments = &colorBlendAttachment;
	
	if (vkCreateGraphicsPipelines(ctx->device, state->prototypes[PIPELINE_FLAT].cache, 1, &pipelineInfo, NULL, &state->prototypes[PIPELINE_FLAT].implementations[1].pipeline) != VK_SUCCESS) return false;
	state->prototypes[PIPELINE_FLAT].implementations[1].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	state->prototypes[PIPELINE_FLAT].implementations[1].depthWrite = VK_FALSE;
	state->prototypes[PIPELINE_FLAT].implementations[1].blendEnable = VK_TRUE;

    ano_aligned_free(meshShaderCode.data);
    ano_aligned_free(fragShaderCode.data);

	vkDestroyShaderModule(ctx->device, meshShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);

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

    // 2: AngularVelocitySSBO
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

    VkSpecializationMapEntry compSpecMapEntry = {};
    compSpecMapEntry.constantID = 0;
    compSpecMapEntry.offset = 0;
    compSpecMapEntry.size = sizeof(VkBool32);

    VkBool32 compUseFirstInstance = ctx->deviceCapabilities.drawIndirectFirstInstance ? VK_TRUE : VK_FALSE;

    VkSpecializationInfo compSpecInfo = {};
    compSpecInfo.mapEntryCount = 1;
    compSpecInfo.pMapEntries = &compSpecMapEntry;
    compSpecInfo.dataSize = sizeof(VkBool32);
    compSpecInfo.pData = &compUseFirstInstance;

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

	return true;
}

void ano_vk_cleanup_pipelines(VulkanContext* ctx, RendererState* state)
{
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
