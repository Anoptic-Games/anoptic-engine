/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

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


	buffer->data = ano_aligned_malloc(size, _Alignof(uint32_t));
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

bool createRenderPass(VulkanComponents* components, VkDevice device, VkFormat swapChainImageFormat, VkRenderPass* renderPass) 
{
	// Initial draw target - multisampled color
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = components->physicalDeviceComp.msaaSamples;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	
	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Depth buffer - multisampled
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = components->renderComp.buffers.depthFormat;
	depthAttachment.samples = components->physicalDeviceComp.msaaSamples;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Present target - resolved from multisampled color image
	VkAttachmentDescription colorAttachmentResolve = {};
	colorAttachmentResolve.format = swapChainImageFormat;
	colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentResolveRef = {};
	colorAttachmentResolveRef.attachment = 2;
	colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;
	subpass.pResolveAttachments = &colorAttachmentResolveRef;

	VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment, colorAttachmentResolve};

	VkRenderPassCreateInfo renderPassInfo= {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = sizeof(attachments)/sizeof(VkAttachmentDescription);
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	
	if (vkCreateRenderPass(device, &renderPassInfo, NULL, renderPass) != VK_SUCCESS)
	{
		printf("Failed to create render pass!\n");
		return false;
	}
	printf("Render pass: %p\n", renderPass);
	return true;
}



bool createUboDescriptorSetLayout(VulkanComponents* components)
{ // Generalize this to call multiple layout creation functions, for each type of renderable asset supported
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = NULL; // Optional

	VkDescriptorSetLayoutBinding bindings[1] = {uboLayoutBinding};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(components->deviceQueueComp.device, &layoutInfo, NULL, &(components->renderComp.descriptorSetLayout)) != VK_SUCCESS)
	{
		printf("Failed to create UBO descriptor set layout!\n");
		return false;
	}

	return true;
}


bool createMeshDescriptorSetLayout(VulkanComponents* components)
{
	// Descriptor set layout binding for the combined image sampler
	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = NULL;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// Descriptor set layout binding for the extra model transformations uniform buffer
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0; // Binding point 0
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.pImmutableSamplers = NULL;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Assuming vertex shader will use this buffer

	// Array of bindings
	VkDescriptorSetLayoutBinding bindings[2] = {uboLayoutBinding, samplerLayoutBinding};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2; // Now we have two bindings
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(components->deviceQueueComp.device, &layoutInfo, NULL, &(components->renderComp.meshDescriptorSetLayout)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor set layout!\n");
		return false;
	}

	return true;
}


// TODO: write two garbo removers for the shader buffers and modules

// The juicy part

VkPipeline createGraphicsPipeline(VulkanComponents* components)
{
	struct Buffer vertShaderCode;
	char vertShaderPath[256]; // Adjust size as needed.
	snprintf(vertShaderPath, sizeof(vertShaderPath), "%s/resources/shaders/vert.spv", PROJECT_ROOT);
	if (!loadFile(vertShaderPath, &vertShaderCode))
	{
		printf("Error loading shaders!\n");
		return NULL;
	}

	struct Buffer fragShaderCode;
	char fragShaderPath[256]; // Adjust size as needed.
	snprintf(fragShaderPath, sizeof(fragShaderPath), "%s/resources/shaders/frag.spv", PROJECT_ROOT);
	if (!loadFile(fragShaderPath, &fragShaderCode))
	{
		printf("Error loading shaders!\n");
		return NULL;
	}
	printf("Loaded files!\n");

	VkShaderModule vertShaderModule = createShaderModule(components->deviceQueueComp.device, &vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(components->deviceQueueComp.device, &fragShaderCode);
	if (vertShaderModule == NULL || fragShaderModule == NULL)
	{
		printf("We failed, bros..\n");
		return NULL;
	}
	printf("Created shaders!\n");

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";
	
	VkPipelineShaderStageCreateInfo shaderStages[2] = {vertShaderStageInfo, fragShaderStageInfo};

	// Dynamic state for runtime adjustment of viewport dimensions

	VkDynamicState dynamicStates[2] =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	// Information on how to process vertex data
	VkVertexInputBindingDescription bindingDescription = getBindingDescription();
	VkVertexInputAttributeDescription attributeDescriptions[3];
	getAttributeDescriptions(&attributeDescriptions[0]);

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = sizeof(attributeDescriptions) / sizeof(attributeDescriptions[0]);
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;
	
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float) components->swapChainComp.swapChainGroup.imageExtent.width;
	viewport.height = (float) components->swapChainComp.swapChainGroup.imageExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = (VkOffset2D){0, 0};
	scissor.extent = components->swapChainComp.swapChainGroup.imageExtent;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f; // Optional
	depthStencil.maxDepthBounds = 1.0f; // Optional
	depthStencil.stencilTestEnable = VK_FALSE;
	//depthStencil.front = {}; // Optional
	//depthStencil.back = {}; // Optional

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f; // Optional
	rasterizer.depthBiasClamp = 0.0f; // Optional
	rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = components->physicalDeviceComp.msaaSamples;
	multisampling.minSampleShading = 1.0f; // Optional
	multisampling.pSampleMask = NULL; // Optional
	multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
	multisampling.alphaToOneEnable = VK_FALSE; // Optional

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	if (!createUboDescriptorSetLayout(components))
	{
		printf("Failed to create descriptor set layout!\n");
		return NULL;
	}

	if (!createMeshDescriptorSetLayout(components))
	{
		printf("Failed to create descriptor set layout!\n");
		return NULL;
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 2; // Optional
	VkDescriptorSetLayout setLayouts[2] = {components->renderComp.descriptorSetLayout, components->renderComp.meshDescriptorSetLayout};
	pipelineLayoutInfo.pSetLayouts = setLayouts; // Optional
	pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
	pipelineLayoutInfo.pPushConstantRanges = NULL; // Optional

	if (vkCreatePipelineLayout(components->deviceQueueComp.device, &pipelineLayoutInfo, NULL, &(components->renderComp.pipelineLayout)) != VK_SUCCESS) 
	{
		printf("Failed to create pipeline layout!\n");
		return NULL;
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = components->renderComp.pipelineLayout;
	pipelineInfo.renderPass = components->renderComp.renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
	pipelineInfo.basePipelineIndex = -1; // Optional

	VkPipeline graphicsPipeline;
	if (vkCreateGraphicsPipelines(components->deviceQueueComp.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline) != VK_SUCCESS) 
	{
		printf("Failed to create graphics pipeline!\n");
		return NULL;
	}

	// TODO: Figure out why this crashes on Windows?
	// DONE: It crashes on Windows because we're using _aligned_malloc() from the Win. API, which has its own free() function
	#ifdef _WIN64
		// TODO: TEST
		ano_aligned_free(vertShaderCode.data);
		ano_aligned_free(fragShaderCode.data);
	#else
		free(vertShaderCode.data);
		free(fragShaderCode.data);
	#endif

	// TODO: generalize shader acquisition and lifecycle control, move this stuff to the cleanup function
	vkDestroyShaderModule(components->deviceQueueComp.device, vertShaderModule, NULL);
	vkDestroyShaderModule(components->deviceQueueComp.device, fragShaderModule, NULL);


	return graphicsPipeline;
}
