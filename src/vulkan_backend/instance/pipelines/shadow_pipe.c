/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_logging.h>
#include "vulkan_backend/instance/pipeline.h"
#include "flat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Dynamic shadow depth pipeline: depth-only FLAT geometry variant + shadowPass spec constant.
// Run after ano_vk_init_pipelines.
// in:  ctx, state (prototypes[PIPELINE_FLAT].layout must exist)
// out: true on success; populates state->shadow{Pipeline,Cache,Sampler}
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state)
{
	// Linear/clamp sampler for the moment atlas.
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

	// Depth-only geometry variant (ANO_DEPTH_ONLY compile of flat.mesh/flat.vert).
	struct Buffer geomCode, fragCode;
	char path[64];
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		useMesh ? (useTask ? "flat_depth_task.mesh" : "flat_depth.mesh") : "flat_depth.vert");
	if (!loadFile(path, &geomCode)) return false;
	if (!loadFile("resources/shaders/shadow_depth.frag.spv", &fragCode)) return false;
	VkShaderModule geomModule = createShaderModule(ctx->device, &geomCode);
	VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

	// Task meshlet cull, shadow variant: frustum-only.
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
	stages[0] = taskStageInfo; // leading task slot
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
	// NONE: one partition mixes both sidedness lanes' casters.
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;
	// No rasterizer depth bias.

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // single-sample shadow atlas

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.maxDepthBounds = 1.0f;

	// Two CDF-stats color attachments (MRT sublayers), blending disabled. Blur reuses statsBlend[0].
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

	// Two CDF stats color targets (MRT sublayers) + transient depth. Blur reuses statsFormat[0].
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
	pipelineInfo.layout = state->prototypes[PIPELINE_FLAT].layout; // flat's sets 0/1/2 + push
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult r = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipeline);

	// Alpha-tested caster variant: ANO_DEPTH_MASKED geometry + shadow_depth_masked.frag.
	// Shares fixed state, only stages differ. Must precede the shader frees (reuses taskModule).
	VkResult mr = VK_SUCCESS;
	if (r == VK_SUCCESS) {
		struct Buffer mGeomCode, mFragCode;
		snprintf(path, sizeof(path), "resources/shaders/%s.spv",
			useMesh ? (useTask ? "flat_depth_masked_task.mesh" : "flat_depth_masked.mesh") : "flat_depth_masked.vert");
		if (!loadFile(path, &mGeomCode)) return false;
		if (!loadFile("resources/shaders/shadow_depth_masked.frag.spv", &mFragCode)) return false;
		VkShaderModule mGeomModule = createShaderModule(ctx->device, &mGeomCode);
		VkShaderModule mFragModule = createShaderModule(ctx->device, &mFragCode);

		VkPipelineShaderStageCreateInfo mStages[3] = { stages[0], stages[1], stages[2] };
		mStages[1].module = mGeomModule; // keeps shadowPass spec info
		mStages[2].module = mFragModule;
		pipelineInfo.pStages = useTask ? mStages : &mStages[1];

		mr = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipelineMasked);

		ano_aligned_free(mGeomCode.data);
		ano_aligned_free(mFragCode.data);
		vkDestroyShaderModule(ctx->device, mGeomModule, NULL);
		vkDestroyShaderModule(ctx->device, mFragModule, NULL);
	}

	ano_aligned_free(geomCode.data);
	ano_aligned_free(fragCode.data);
	vkDestroyShaderModule(ctx->device, geomModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);
	if (taskModule != VK_NULL_HANDLE)
		vkDestroyShaderModule(ctx->device, taskModule, NULL);

	if (r != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to create shadow depth pipeline!"); return false; }
	if (mr != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to create masked shadow depth pipeline!"); return false; }

	// --- Moment prefilter pipeline: fullscreen separable box over the atlas (X then Y) ---
	// One combined-image-sampler at set 0 + 16-byte push (dir + layer).
	// Vertex: shadowblur.vert (layered) with vertex-stage gl_Layer, else tonemap.vert. Frag: shadowblur.frag.
	VkDescriptorSetLayoutBinding blurBinding = {};
	blurBinding.binding = 0;
	blurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	blurBinding.descriptorCount = 1;
	blurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo blurSetInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1, .pBindings = &blurBinding };
	if (vkCreateDescriptorSetLayout(ctx->device, &blurSetInfo, NULL, &state->shadowBlurSetLayout) != VK_SUCCESS) {
		ano_log(ANO_FATAL, "Failed to create shadow blur set layout!"); return false; }

	// vec2 dir + int layer + int pad. VERTEX for shadowblur.vert's gl_Layer routing.
	VkPushConstantRange blurPush = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo blurLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1, .pSetLayouts = &state->shadowBlurSetLayout,
		.pushConstantRangeCount = 1, .pPushConstantRanges = &blurPush };
	if (vkCreatePipelineLayout(ctx->device, &blurLayoutInfo, NULL, &state->shadowBlurLayout) != VK_SUCCESS) {
		ano_log(ANO_FATAL, "Failed to create shadow blur pipeline layout!"); return false; }

	struct Buffer blurVertCode, blurFragCode;
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		ctx->deviceCapabilities.shaderOutputLayer ? "shadowblur.vert" : "tonemap.vert");
	if (!loadFile(path, &blurVertCode)) return false;
	if (!loadFile("resources/shaders/shadowblur.frag.spv", &blurFragCode)) return false;
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
	if (br != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to create shadow blur pipeline!"); return false; }

	return true;
}
