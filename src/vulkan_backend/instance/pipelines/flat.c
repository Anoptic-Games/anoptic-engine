/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <mimalloc-override.h>
#include "flat.h"
#include "vulkan_backend/instance/pipeline.h"
#include <stdio.h>
#include <stdlib.h>

// Shared builder for the two opaque flat lanes (review finding 7): PIPELINE_FLAT rasterizes with
// backface culling (glTF single-sided materials — the default — never need their backfaces), while
// PIPELINE_FLAT_TWOSIDED keeps cullMode NONE for doubleSided materials (the parser routes them).
// Identical shaders/layout/state otherwise, so depth stays EQUAL-compatible across both lanes.
static bool flat_init_with_cull(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto,
                                PipelineType type, VkCullModeFlags cullMode)
{
	// 1. Setup cache
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache);

	// Mesh stage on capable devices, vertex stage on the fallback path.
	bool useMesh = ctx->deviceCapabilities.meshShader;
	VkShaderStageFlags geometryStage = useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;

	// 2. Setup layout
	// Push: transformBaseOffset + shadowFrustumIndex (the latter used only by the depth-pass
	// shadow variant; the camera variant leaves it unread).
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = geometryStage;
	pushConstantRange.offset = 0;
	pushConstantRange.size = 2u * sizeof(uint32_t);

	// Set 2 = dynamic shadows (audit 4.7): the geometry stage reads a shadow frustum's viewProj
	// in the depth pass; the fragment stage samples the shadow atlas. Same layout for the depth
	// pipeline (which reuses this one), so all three sets are always present.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 3;
	VkDescriptorSetLayout setLayouts[3] = {state->globalSetLayout, proto->descriptorLayout, state->shadowGeomSetLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &proto->layout) != VK_SUCCESS) 
	{
		printf("Failed to create flat pipeline layout!\n");
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
		PBR_FEATURE_OCCLUSION_TEXTURE |
		PBR_FEATURE_ALPHA_MODE_OPAQUE |
		PBR_FEATURE_ALPHA_MODE_BLEND;
	if (cullMode == VK_CULL_MODE_NONE)
		proto->supportedFeatures |= PBR_FEATURE_DOUBLE_SIDED; // only the uncull(ed) lane renders backfaces

	// Load shaders: mesh shader on capable devices, vertex shader on the fallback. The depth
	// pre-pass variant (index 2) uses the ANO_DEPTH_ONLY compile of the same source (position
	// only, no user attributes) — invariant gl_Position in both keeps clip depth bit-identical,
	// which the opaque variant's EQUAL test requires.
	struct Buffer geomShaderCode;
	char geomShaderPath[256];
	snprintf(geomShaderPath, sizeof(geomShaderPath), "%s/resources/shaders/%s.spv", PROJECT_ROOT, useMesh ? "flat.mesh" : "flat.vert");
	if (!loadFile(geomShaderPath, &geomShaderCode)) return false;

	struct Buffer depthGeomShaderCode;
	snprintf(geomShaderPath, sizeof(geomShaderPath), "%s/resources/shaders/%s.spv", PROJECT_ROOT, useMesh ? "flat_depth.mesh" : "flat_depth.vert");
	if (!loadFile(geomShaderPath, &depthGeomShaderCode)) return false;

	struct Buffer fragShaderCode;
	char fragShaderPath[256];
	snprintf(fragShaderPath, sizeof(fragShaderPath), "%s/resources/shaders/flat.frag.spv", PROJECT_ROOT);
	if (!loadFile(fragShaderPath, &fragShaderCode)) return false;

	VkShaderModule geomShaderModule = createShaderModule(ctx->device, &geomShaderCode);
	VkShaderModule depthGeomShaderModule = createShaderModule(ctx->device, &depthGeomShaderCode);
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
	// frontFace stays COUNTER_CLOCKWISE on both lanes: the engine's full transform chain classifies
	// glTF front faces as front under it (flat.frag's gl_FrontFacing normal flip depends on this).
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
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	// Two color attachments: [0] HDR color, [1] R32_UINT picking id (audit 3.1). The id attachment
	// never blends; the opaque variant writes it (colorWriteMask=R), the blended variant masks it off.
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

	// Opaque variant (index 0) reads the depth pre-pass result: test EQUAL against the pre-pass depth
	// with NO depth write, so each visible pixel is shaded exactly once (the pre-pass, index 2, already
	// laid down the nearest depth). The pre-pass itself uses depthWrite ON + LESS (built further below).
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
	depthStencil.stencilTestEnable = VK_FALSE;

	// [0] HDR target (geometry renders here, not the swapchain), [1] R32_UINT picking id. The
	// opaque pass provides both attachments; the count must match the rendering scope (vulkanMaster.c).
	VkFormat colorFormats[2] = { ANO_HDR_COLOR_FORMAT, VK_FORMAT_R32_UINT };
	VkFormat depthFormat = state->depthFormat;

	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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

	// Opaque variant (index 0): EQUAL test, no depth write (the pre-pass owns depth).
	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[0].pipeline) != VK_SUCCESS) return false;
	proto->implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[0].depthWrite = VK_FALSE;
	proto->implementations[0].blendEnable = VK_FALSE;

	// Blended variant (index 1). Bound by no g_framePass today, but must stay a valid 2-attachment
	// pipeline; mask off the id write so transparent-flat is non-pickable if ever bound. Restore LESS
	// (the opaque variant above switched the shared state to EQUAL for the pre-pass).
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	colorBlendAttachment->blendEnable = VK_TRUE;
	colorBlendAttachment->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment->alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachments[1].colorWriteMask = 0; // id not written by the blended variant

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL, &proto->implementations[1].pipeline) != VK_SUCCESS) return false;
	proto->implementations[1].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[1].depthWrite = VK_FALSE;
	proto->implementations[1].blendEnable = VK_TRUE;

	// Depth pre-pass variant (index 2): the ANO_DEPTH_ONLY geometry stage (no user attributes ->
	// ~3.5x less ISBE per meshlet, the frame's top warp-launch limiter) with the fragment stage
	// stripped and no color attachments, so it only lays down the nearest depth. depthWrite ON +
	// LESS. Same-source compile + invariant gl_Position guarantees bit-identical clip-space depth,
	// which the opaque variant's EQUAL test above relies on. Runs first (g_framePasses) so the
	// heavy lighting shader shades each visible pixel exactly once.
	VkPipelineShaderStageCreateInfo depthGeomStageInfo = geomShaderStageInfo;
	depthGeomStageInfo.module = depthGeomShaderModule;

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

	VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo; // inherit shared raster/viewport/msaa/layout
	prepassInfo.pNext = &prepassRendering;
	prepassInfo.stageCount = 1;                  // depth-only geometry stage; no fragment shader
	prepassInfo.pStages = &depthGeomStageInfo;
	prepassInfo.pDepthStencilState = &prepassDepth;
	prepassInfo.pColorBlendState = &prepassBlend;

	if (vkCreateGraphicsPipelines(ctx->device, proto->cache, 1, &prepassInfo, NULL, &proto->implementations[2].pipeline) != VK_SUCCESS) return false;
	proto->implementations[2].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	proto->implementations[2].depthWrite = VK_TRUE;
	proto->implementations[2].blendEnable = VK_FALSE;

	ano_aligned_free(geomShaderCode.data);
	ano_aligned_free(depthGeomShaderCode.data);
	ano_aligned_free(fragShaderCode.data);

	vkDestroyShaderModule(ctx->device, geomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, depthGeomShaderModule, NULL);
	vkDestroyShaderModule(ctx->device, fragShaderModule, NULL);

	return true;
}

bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull(ctx, state, proto, PIPELINE_FLAT, VK_CULL_MODE_BACK_BIT);
}

bool ano_pipeline_flat_twosided_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto)
{
	return flat_init_with_cull(ctx, state, proto, PIPELINE_FLAT_TWOSIDED, VK_CULL_MODE_NONE);
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
