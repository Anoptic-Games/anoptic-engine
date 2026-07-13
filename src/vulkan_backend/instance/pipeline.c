/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_resources.h>
#include <anoptic_log.h>
#include "pipeline.h"
#include "pipelines/flat.h"
#include "pipelines/transmission.h"
#include "pipelines/additive.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>




// Shader bytes ride the resource manager: logical names, any-CWD resolution, the
// manager owns the SPIR-V (single-copy; pipeline rebuilds re-request for free).
VkShaderModule ano_pipeline_shader(VkDevice device, const char* logical)
{
	ano_res_lifetime lifetime = ano_res_lifetime_engine();
	ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
	ano_res_read read = {0};
	if (ano_res_reader_register(&reader) != 0 || ano_res_read_begin(&reader, &read) != 0)
		return VK_NULL_HANDLE;
	anores_t h = ano_res_get(lifetime, logical);
	anostr_t code = ano_res_bytes(&read, h);
	size_t size = anostr_len(code);
	if (size == 0 || size % 4 != 0)
	{
		ano_res_read_end(&read);
		ano_res_reader_unregister(&reader);
		ano_log(ANO_ERROR, "Shader unavailable or not SPIR-V-sized: %s", logical);
		return VK_NULL_HANDLE;
	}

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = size;
	createInfo.pCode = (const uint32_t *) anostr_bytes(&code); // payloads are >=16-aligned

	VkShaderModule shaderModule;
	VkResult result = vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);
	ano_res_read_end(&read);
	ano_res_reader_unregister(&reader);
	if (result != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to create shader module: %s", logical);
		return VK_NULL_HANDLE;
	}
	return shaderModule;
}

// Load flat.task and fill a TASK stage with the {shadowPass, coneCull} specialization
bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
                             TaskStageStorage* store, VkShaderModule* outModule,
                             VkPipelineShaderStageCreateInfo* stage)
{
	*outModule = ano_pipeline_shader(ctx->device, "shaders/flat.task.spv");
	if (*outModule == VK_NULL_HANDLE) return false;

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

	if (!ano_pipeline_flat_masked_init(ctx, state, &state->prototypes[PIPELINE_FLAT_MASKED]))
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

    if (!ano_vk_init_compute(ctx, state))
        return false;

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
	// Text overlay: composite blend pipeline + raster set layout
	if (state->textOverlayPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->textOverlayPipeline, NULL);
		state->textOverlayPipeline = VK_NULL_HANDLE;
	}
	if (state->textWorldPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->textWorldPipeline, NULL);
		state->textWorldPipeline = VK_NULL_HANDLE;
	}
	if (state->textWorldLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(ctx->device, state->textWorldLayout, NULL);
		state->textWorldLayout = VK_NULL_HANDLE;
	}
	if (state->textRasterSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(ctx->device, state->textRasterSetLayout, NULL);
		state->textRasterSetLayout = VK_NULL_HANDLE;
	}
	// Hi-Z build set layout
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

	// Shadow depth pass: pipeline + cache + sampler + two shadow set layouts
	if (state->shadowPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->shadowPipeline, NULL);
		state->shadowPipeline = VK_NULL_HANDLE;
	}
	if (state->shadowPipelineMasked != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(ctx->device, state->shadowPipelineMasked, NULL);
		state->shadowPipelineMasked = VK_NULL_HANDLE;
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
