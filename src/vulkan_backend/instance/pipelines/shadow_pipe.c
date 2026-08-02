/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include "vulkan_backend/instance/descriptor_layout_schema.h"
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
// Cache idiom: refused mint zeroes handle to VK_NULL_HANDLE; init continues.
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
	if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->shadowCache) != VK_SUCCESS)
		state->shadowCache = VK_NULL_HANDLE;

	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlagBits geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// Depth-only geometry variant (ANO_DEPTH_ONLY compile of flat.mesh/flat.vert).
	// Unwind idiom: buffers/modules inert at declare; goto fail discharges live. Masked pair too.
	// loadFile refuse: buffer indeterminate -> re-inert before unwind.
	struct Buffer geomCode = {}, fragCode = {}, mGeomCode = {}, mFragCode = {};
	VkShaderModule geomModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE, taskModule = VK_NULL_HANDLE,
		mGeomModule = VK_NULL_HANDLE, mFragModule = VK_NULL_HANDLE;

	char path[64];
	bool depthOk = [&]() -> bool {
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		useMesh ? (useTask ? "flat_depth_task.mesh" : "flat_depth.mesh") : "flat_depth.vert");
	if (!loadFile(path, &geomCode)) { geomCode.data = NULL; return false; }
	if (!loadFile("resources/shaders/shadow_depth.frag.spv", &fragCode)) { fragCode.data = NULL; return false; }
	geomModule = createShaderModule(ctx->device, &geomCode);
	fragModule = createShaderModule(ctx->device, &fragCode);

	// Task meshlet cull, shadow variant: frustum-only.
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
	if (!ano_pipeline_stage(geometryStage, geomModule, &specInfo, &stages[1])
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, NULL, &stages[2]))
		return false;

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
		snprintf(path, sizeof(path), "resources/shaders/%s.spv",
			useMesh ? (useTask ? "flat_depth_masked_task.mesh" : "flat_depth_masked.mesh") : "flat_depth_masked.vert");
		if (!loadFile(path, &mGeomCode)) { mGeomCode.data = NULL; return false; }
		if (!loadFile("resources/shaders/shadow_depth_masked.frag.spv", &mFragCode)) { mFragCode.data = NULL; return false; }
		mGeomModule = createShaderModule(ctx->device, &mGeomCode);
		mFragModule = createShaderModule(ctx->device, &mFragCode);

		VkPipelineShaderStageCreateInfo mStages[3] = { stages[0], stages[1], stages[2] };
		// Same shadowPass spec info, masked modules.
		if (!ano_pipeline_stage(geometryStage, mGeomModule, &specInfo, &mStages[1])
			|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, mFragModule, NULL, &mStages[2]))
			return false;
		pipelineInfo.pStages = useTask ? mStages : &mStages[1];

		mr = vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &pipelineInfo, NULL, &state->shadowPipelineMasked);

	}

	if (r != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to create shadow depth pipeline!"); return false; }
	if (mr != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to create masked shadow depth pipeline!"); return false; }
	return true;
	}();

	ano_aligned_free(geomCode.data);
	ano_aligned_free(fragCode.data);
	ano_aligned_free(mGeomCode.data);
	ano_aligned_free(mFragCode.data);
	vkDestroyShaderModule(ctx->device, geomModule, NULL);
	vkDestroyShaderModule(ctx->device, fragModule, NULL);
	vkDestroyShaderModule(ctx->device, mGeomModule, NULL);
	vkDestroyShaderModule(ctx->device, mFragModule, NULL);
	vkDestroyShaderModule(ctx->device, taskModule, NULL);
	if (!depthOk) return false;

	// --- Moment prefilter pipeline: fullscreen separable box over the atlas (X then Y) ---
	// One combined-image-sampler at set 0 + 16-byte push (dir + layer).
	// Vertex: shadowblur.vert (layered) with vertex-stage gl_Layer, else tonemap.vert. Frag: shadowblur.frag.
	const auto& blurSpecs = ANO_VK_SHADOW_BLUR_BINDINGS;
	VkDescriptorSetLayoutBinding blurBindings[ANO_VK_SHADOW_BLUR_BINDINGS.count] = {};
	if (!ano_vk_materialize_layout_bindings(blurSpecs, blurBindings))
		return false;
	VkDescriptorSetLayoutCreateInfo blurSetInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = blurSpecs.count, .pBindings = blurBindings };
	if (vkCreateDescriptorSetLayout(ctx->device, &blurSetInfo, NULL, &state->shadowBlurSetLayout) != VK_SUCCESS) {
		ano_log(ANO_FATAL, "Failed to create shadow blur set layout!"); return false; }

	// vec2 dir + int layer + int pad. VERTEX for shadowblur.vert's gl_Layer routing.
	VkPushConstantRange blurPush = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo blurLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1, .pSetLayouts = &state->shadowBlurSetLayout,
		.pushConstantRangeCount = 1, .pPushConstantRanges = &blurPush };
	if (vkCreatePipelineLayout(ctx->device, &blurLayoutInfo, NULL, &state->shadowBlurLayout) != VK_SUCCESS) {
		ano_log(ANO_FATAL, "Failed to create shadow blur pipeline layout!"); return false; }

	// Blur phase: resources remain inert until acquired, then discharge once.
	struct Buffer blurVertCode = {}, blurFragCode = {};
	VkShaderModule blurVert = VK_NULL_HANDLE, blurFrag = VK_NULL_HANDLE;
	bool blurOk = [&]() -> bool {
	snprintf(path, sizeof(path), "resources/shaders/%s.spv",
		ctx->deviceCapabilities.shaderOutputLayer ? "shadowblur.vert" : "tonemap.vert");
	if (!loadFile(path, &blurVertCode)) { blurVertCode.data = NULL; return false; }
	if (!loadFile("resources/shaders/shadowblur.frag.spv", &blurFragCode)) { blurFragCode.data = NULL; return false; }
	blurVert = createShaderModule(ctx->device, &blurVertCode);
	blurFrag = createShaderModule(ctx->device, &blurFragCode);

	VkPipelineShaderStageCreateInfo blurStages[2];
	if (!ano_pipeline_stage(VK_SHADER_STAGE_VERTEX_BIT, blurVert, NULL, &blurStages[0])
		|| !ano_pipeline_stage(VK_SHADER_STAGE_FRAGMENT_BIT, blurFrag, NULL, &blurStages[1]))
		return false;

	VkPipelineVertexInputStateCreateInfo blurVertexInput = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo blurIA = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkPipelineViewportStateCreateInfo blurVP = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1 };
	VkPipelineRasterizationStateCreateInfo blurRaster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo blurMS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
	VkPipelineColorBlendAttachmentState blurBlend = {};
	blurBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blurCB = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &blurBlend };
	VkPipelineDepthStencilStateCreateInfo blurDS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE };
	VkDynamicState blurDynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo blurDyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, .pDynamicStates = blurDynamicStates };
	VkFormat blurFormat = ANO_SHADOW_STATS_FORMAT;
	VkPipelineRenderingCreateInfo blurRendering = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1, .pColorAttachmentFormats = &blurFormat };

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

	return vkCreateGraphicsPipelines(ctx->device, state->shadowCache, 1, &blurPipeline, NULL,
	                                 &state->shadowBlurPipeline) == VK_SUCCESS;
	}();
	if (!blurOk) ano_log(ANO_FATAL, "Failed to create shadow blur pipeline!");

	// Both paths, unconditional.
	ano_aligned_free(blurVertCode.data);
	ano_aligned_free(blurFragCode.data);
	vkDestroyShaderModule(ctx->device, blurVert, NULL);
	vkDestroyShaderModule(ctx->device, blurFrag, NULL);
	return blurOk;

}
