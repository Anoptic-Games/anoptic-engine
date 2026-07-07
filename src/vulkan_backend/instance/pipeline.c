/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_filesystem.h>
#include <anoptic_logging.h>
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

// Open a shipped engine file resolved against ano_fs_gamepath()
static FILE* openEngineFile(const char* relative)
{
	ano_fspath dir = ano_fs_gamepath();
	if (dir.length == 0)
		return NULL;

	char path[MAXPATH + 128];
	int n = snprintf(path, sizeof path, "%s/%s", dir.str, relative);
	if (n < 0 || n >= (int)sizeof path)
		return NULL;
	return fopen(path, "rb");
}

bool loadFile(const char* filename, struct Buffer* buffer)
{
	FILE* file = openEngineFile(filename);
	if (file == NULL)
	{
		ano_log(ANO_ERROR, "Failed to open file (relative to the executable): %s", filename);
		return false;
	}

	fseek(file, 0, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0, SEEK_SET);


	buffer->data = ano_aligned_malloc(size, alignof(uint32_t));
	if (buffer->data == NULL) 
	{
		ano_log(ANO_ERROR, "Failed to allocate memory for file: %s", filename);
		fclose(file);
		return false;
	}

	if (fread(buffer->data, 1, size, file) != size) 
	{
		ano_log(ANO_ERROR, "Failed to read file: %s", filename);
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
		ano_olog(ANO_ERROR, "Failed to create shader module!");
		return NULL;
	}

	return shaderModule;
}

// Load flat.task and fill a TASK stage with the {shadowPass, coneCull} specialization
bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
                             TaskStageStorage* store, VkShaderModule* outModule,
                             VkPipelineShaderStageCreateInfo* stage)
{
	struct Buffer code;
	if (!loadFile("resources/shaders/flat.task.spv", &code)) return false;
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
