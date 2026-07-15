/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <anoptic_memory.h>
#include <anoptic_log.h>
#include "flat.h"
#include "vulkan_backend/instance/pipeline.h"
#include <stdio.h>
#include <stdlib.h>

// Shared builder for the flat lanes: cullMode per lane; masked builds the alphaMode MASK cutout lane.
static bool flat_init_with_cull(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto,
                                PipelineType type, VkCullModeFlags cullMode, bool masked)
{
	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache);

	// Mesh stage on capable devices, vertex stage on the fallback path.
	// Task cull prepends a flat.task stage to every mesh-path variant.
	bool useMesh = ctx->deviceCapabilities.meshShader;
	bool useTask = state->taskCull;
	VkShaderStageFlags geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// 2. Setup layout
	// Push: transformBaseOffset + shadowFrustumIndex. Task stage flag joins the range.
	VkPushConstantRange pushConstantRange = {};
	// FRAGMENT joins the range: shadow_depth.frag reads shadowFrustumIndex.
	pushConstantRange.stageFlags = geometryStage | VK_SHADER_STAGE_FRAGMENT_BIT | (useTask ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
	pushConstantRange.offset = 0;
	pushConstantRange.size = 2u * sizeof(uint32_t);

	// Set 2 = dynamic shadows: geometry reads shadow viewProj, fragment samples the shadow atlas.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 3;
	VkDescriptorSetLayout setLayouts[3] = {state->globalSetLayout, proto->descriptorLayout, state->shadowGeomSetLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &proto->layout) != VK_SUCCESS) 
	{
		ano_log(ANO_FATAL, "Failed to create flat pipeline layout!");
		return false;
	}

	proto->type = type;
	proto->implementationCount = 3;
	proto->implementations = calloc(3, sizeof(PipelineImplementation));
	proto->supportedFeatures =
		PBR_FEATURE_BASE_COLOR_FACTOR |
		PBR_FEATURE_BASE_COLOR_TEXTURE |
		PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR |
		PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE |
		PBR_FEATURE_NORMAL_TEXTURE |
		PBR_FEATURE_OCCLUSION_TEXTURE |
		PBR_FEATURE_ALPHA_MODE_OPAQUE |
		PBR_FEATURE_ALPHA_MODE_BLEND;
	if (cullMode == VK_CULL_MODE_NONE)
		proto->supportedFeatures |= PBR_FEATURE_DOUBLE_SIDED; // double-sided lane
	if (masked)
		proto->supportedFeatures |= PBR_FEATURE_ALPHA_MODE_MASK;

	// Load shaders: mesh or VS fallback. Index 2 = ANO_DEPTH_ONLY. RM owns SPIR-V.
	char geomShaderPath[64];
	snprintf(geomShaderPath, sizeof(geomShaderPath), "shaders/%s.spv",
		useMesh ? (useTask ? "flat_task.mesh" : "flat.mesh") : "flat.vert");
	VkShaderModule geomShaderModule = ano_pipeline_shader(ctx->device, geomShaderPath);
	if (geomShaderModule == VK_NULL_HANDLE) return false;

	snprintf(geomShaderPath, sizeof(geomShaderPath), "shaders/%s.spv",
		useMesh ? (useTask ? "flat_depth_task.mesh" : "flat_depth.mesh") : "flat_depth.vert");
	VkShaderModule depthGeomShaderModule = ano_pipeline_shader(ctx->device, geomShaderPath);
	if (depthGeomShaderModule == VK_NULL_HANDLE) return false;

	// fp16 variant when the device has shaderFloat16; masked lane loads the ANO_ALPHA_MASK compile.
	const char* fragPath = masked
		? (ctx->deviceCapabilities.shaderFloat16 ? "shaders/flat_masked_fp16.frag.spv"
		                                         : "shaders/flat_masked.frag.spv")
		: (ctx->deviceCapabilities.shaderFloat16 ? "shaders/flat_fp16.frag.spv"
		                                         : "shaders/flat.frag.spv");
	VkShaderModule fragShaderModule = ano_pipeline_shader(ctx->device, fragPath);
	if (fragShaderModule == VK_NULL_HANDLE) return false;

	// Task meshlet-cull stage: cone culling only on the BACK-culled lane.
	VkShaderModule taskModule = VK_NULL_HANDLE;
	TaskStageStorage taskStore;
	VkPipelineShaderStageCreateInfo taskStageInfo = {};
	if (useTask && !ano_pipeline_task_stage(ctx, VK_FALSE,
			cullMode == VK_CULL_MODE_BACK_BIT ? VK_TRUE : VK_FALSE,
			&taskStore, &taskModule, &taskStageInfo))
		return false;

	VkPipelineShaderStageCreateInfo geomShaderStageInfo = {};
	geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	geomShaderStageInfo.stage = geometryStage;
	geomShaderStageInfo.module = geomShaderModule;
	geomShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	// [task,] geom, frag stage array, task slot first.
	VkPipelineShaderStageCreateInfo shaderStages[3] = {taskStageInfo, geomShaderStageInfo, fragShaderStageInfo};
	VkPipelineShaderStageCreateInfo* colorStages = useTask ? shaderStages : &shaderStages[1];
	uint32_t colorStageCount = useTask ? 3u : 2u;

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
	// frontFace COUNTER_CLOCKWISE on both lanes.
	rasterizer.cullMode = cullMode;
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
	// Masked lane: alpha-to-coverage dithers cutout alpha across the MSAA samples.
	multisampling.alphaToCoverageEnable = masked ? VK_TRUE : VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	// Two color attachments: [0] HDR color, [1] R32_UINT picking id.
	VkPipelineColorBlendAttachmentState blendAttachments[2] = {};
	VkPipelineColorBlendAttachmentState* colorBlendAttachment = &blendAttachments[0];
	colorBlendAttachment->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment->blendEnable = VK_FALSE;
	colorBlendAttachment->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachments[1].blendEnable = VK_FALSE;
	blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT; // opaque variant writes the id

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 2;
	colorBlending.pAttachments = blendAttachments;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	// Opaque variant (index 0): EQUAL test, no depth write (the pre-pass owns depth).
	// Masked lane has no pre-pass: its variant owns depth with LESS + write.
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = masked ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = masked ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_EQUAL;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
	depthStencil.stencilTestEnable = VK_FALSE;

	// [0] HDR target, [1] R32_UINT picking id.
	VkFormat colorFormats[2] = { ANO_HDR_COLOR_FORMAT, VK_FORMAT_R32_UINT };
	VkFormat depthFormat = state->depthFormat;

	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
	renderingInfo.depthAttachmentFormat = depthFormat;

	// Mesh path needs no vertex-input or input-assembly state.
	// Vertex fallback: empty vertex-input (programmable pulling) + triangle-list assembly.
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = colorStageCount;
	pipelineInfo.pStages = colorStages;
	pipelineInfo.pVertexInputState = useMesh ? NULL : &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = useMesh ? NULL : &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = proto->layout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;
	pipelineInfo.subpass = 0;

	// Opaque variant (index 0): EQUAL, no write. Masked: LESS + write + alpha-to-coverage.
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[0].pipeline) != VK_SUCCESS) return false;
	proto->implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[0].depthWrite = masked ? VK_TRUE : VK_FALSE;
	proto->implementations[0].blendEnable = VK_FALSE;

	// Blended variant (index 1): mask off the id write, restore LESS.
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	colorBlendAttachment->blendEnable = VK_TRUE;
	colorBlendAttachment->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachments[1].colorWriteMask = 0; // id unwritten

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[1].pipeline) != VK_SUCCESS) return false;
	proto->implementations[1].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[1].depthWrite = VK_FALSE;
	proto->implementations[1].blendEnable = VK_TRUE;

	// Depth pre-pass variant (index 2): ANO_DEPTH_ONLY geometry, no fragment stage, no color attachments.
	// depthWrite ON + LESS. Same task module/spec as the color variants.
	VkPipelineShaderStageCreateInfo depthStages[2] = {taskStageInfo, geomShaderStageInfo};
	depthStages[1].module = depthGeomShaderModule;

	VkPipelineDepthStencilStateCreateInfo prepassDepth = depthStencil;
	prepassDepth.depthWriteEnable = VK_TRUE;
	prepassDepth.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendStateCreateInfo prepassBlend = {};
	prepassBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	prepassBlend.attachmentCount = 0;

	VkPipelineRenderingCreateInfo prepassRendering = {};
	prepassRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	prepassRendering.colorAttachmentCount = 0;
	prepassRendering.depthAttachmentFormat = depthFormat;

	// No fragment stage: disable alpha-to-coverage (unwritten alpha).
	VkPipelineMultisampleStateCreateInfo prepassMs = multisampling;
	prepassMs.alphaToCoverageEnable = VK_FALSE;

	VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo; // inherit shared raster/viewport/msaa/layout
	prepassInfo.pNext = &prepassRendering;
	prepassInfo.stageCount = useTask ? 2 : 1;
	prepassInfo.pStages = useTask ? depthStages : &depthStages[1];
	prepassInfo.pDepthStencilState = &prepassDepth;
	prepassInfo.pColorBlendState = &prepassBlend;
	prepassInfo.pMultisampleState = &prepassMs;

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &prepassInfo, NULL, &proto->implementations[2].pipeline) != VK_SUCCESS) return false;
	proto->implementations[2].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[2].depthWrite = VK_TRUE;
	proto->implementations[2].blendEnable = VK_FALSE;

	vkDestroyShaderModule(ctx->device, geomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, depthGeomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);
	if (taskModule != VK_NULL_HANDLE)
		vkDestroyShaderModule(ctx->device, taskModule, NULL);

	return true;
}

bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull(ctx, state, proto, PIPELINE_FLAT, VK_CULL_MODE_BACK_BIT, false);
}

bool ano_pipeline_flat_twosided_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull(ctx, state, proto, PIPELINE_FLAT_TWOSIDED, VK_CULL_MODE_NONE, false);
}

bool ano_pipeline_flat_masked_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	// Cutout casters typically doubleSided: cullMode NONE.
	return flat_init_with_cull(ctx, state, proto, PIPELINE_FLAT_MASKED, VK_CULL_MODE_NONE, true);
}

void ano_pipeline_flat_cleanup(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	(void)state;
	if (proto->cache != VK_NULL_HANDLE)
	{
		vkDestroyPipelineCache(ctx->device, proto->cache, NULL);
		proto->cache = VK_NULL_HANDLE;
	}

	if (proto->layout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx->device, proto->layout, NULL);
		proto->layout = VK_NULL_HANDLE;
	}

	if (proto->implementations != NULL)
	{
		for (uint32_t j = 0; j < proto->implementationCount; ++j)
		{
			if (proto->implementations[j].pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(ctx->device, proto->implementations[j].pipeline, NULL);
				proto->implementations[j].pipeline = VK_NULL_HANDLE;
			}
		}
		free(proto->implementations);
		proto->implementations = NULL;
		proto->implementationCount = 0;
	}
}
