/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <anoptic_memory.h>
#include "flat.h"
#include "vulkan_backend/instance/pipeline.h"
#include <stdio.h>
#include <stdlib.h>

bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache);

	// Mesh stage on capable devices, vertex stage on the fallback path.
	bool useMesh = ctx->deviceCapabilities.meshShader;
	VkShaderStageFlags geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// 2. Setup layout
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = geometryStage;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(uint32_t);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 2;
	VkDescriptorSetLayout setLayouts[2] = {state->globalSetLayout, proto->descriptorLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &proto->layout) != VK_SUCCESS) 
	{
		printf("Failed to create flat pipeline layout!\n");
		return false;
	}

	proto->type = PIPELINE_FLAT;
	proto->implementationCount = 2;
	proto->implementations = calloc(2, sizeof(PipelineImplementation));
	proto->supportedFeatures = 
		PBR_FEATURE_BASE_COLOR_FACTOR | 
		PBR_FEATURE_BASE_COLOR_TEXTURE | 
		PBR_FEATURE_METALLIC_ROUGHNESS_FACTOR | 
		PBR_FEATURE_METALLIC_ROUGHNESS_TEXTURE | 
		PBR_FEATURE_OCCLUSION_TEXTURE |
		PBR_FEATURE_ALPHA_MODE_OPAQUE |
		PBR_FEATURE_ALPHA_MODE_BLEND |
		PBR_FEATURE_DOUBLE_SIDED;   // rasterizer uses cullMode NONE, so all geometry is double-sided

	// Load shaders: mesh shader on capable devices, vertex shader on the fallback.
	// Paths are exe-relative; loadFile resolves them against ano_fs_gamepath().
	struct Buffer geomShaderCode;
	char geomShaderPath[64];
	snprintf(geomShaderPath, sizeof(geomShaderPath), "resources/shaders/%s.spv", useMesh ? "flat.mesh" : "flat.vert");
	if (!loadFile(geomShaderPath, &geomShaderCode)) return false;

	struct Buffer fragShaderCode;
	if (!loadFile("resources/shaders/flat.frag.spv", &fragShaderCode)) return false;

	VkShaderModule geomShaderModule = createShaderModule(ctx->device, &geomShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(ctx->device, &fragShaderCode);

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

	VkPipelineShaderStageCreateInfo shaderStages[] = {geomShaderStageInfo, fragShaderStageInfo};

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

	// The mesh path needs neither vertex-input nor input-assembly state. The fallback
	// vertex path uses programmable vertex pulling (no vertex buffers, so an empty
	// vertex-input state) and a triangle-list assembly over the bound index buffer.
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
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

	// Opaque variant (index 0)
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[0].pipeline) != VK_SUCCESS) return false;
	proto->implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[0].depthWrite = VK_TRUE;
	proto->implementations[0].blendEnable = VK_FALSE;

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
	
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[1].pipeline) != VK_SUCCESS) return false;
	proto->implementations[1].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[1].depthWrite = VK_FALSE;
	proto->implementations[1].blendEnable = VK_TRUE;

	ano_aligned_free(geomShaderCode.data);
	ano_aligned_free(fragShaderCode.data);

	vkDestroyShaderModule(ctx->device, geomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);

	return true;
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
