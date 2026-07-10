/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"


bool createDescriptorPool(VulkanContext* ctx, RendererState* state)
{ // Central to init
	// Per-view sets (global, light-cull, tonemap) scale by ANO_VIEW_COUNT, view-independent (cull, update, scatter) are one per frame.
	VkDescriptorPoolSize poolSize[5] = {};
	poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	// +1u shadow geom set binding 3 (packed sampling viewProjs read as UBO).
	poolSize[0].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (2u * ANO_VIEW_COUNT + 4u + 1u);
	// Shadows (audit 4.7) add per frame: cull binding 9 (1 SSBO) + shadowsetup set (5 SSBO) + shadow geom set (2 SSBO + 1 sampler + 1 UBO) + 2 sets.
	// Transparency sort adds cull binding 10 (1 SSBO sort-key), the +1 shared term below.
	// global 12 SSBOs/view (binding 12 = per-light LightRuntime), + lightsetup set (3 SSBO) shared.
	// Text overlay adds per frame: raster set (3 SSBO + 1 storage image) + overlay sample set (1 sampler), 2 sets.
	// UI lane adds per frame: raster-set bindings 4-10 (7 SSBO).
	poolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize[1].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (16u * ANO_VIEW_COUNT + 16u + 7u + 1u + 3u + 3u + 1u + 7u);
	poolSize[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	poolSize[2].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 1; // scatter binding 1 xform ring slice
	poolSize[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	// Per frame: ANO_VIEW_COUNT tonemap + 4 (shadow atlas + blurAtlasSet + blurTempSet + 1 spare).
	// Hi-Z (review 4.9 step 3) adds 2 sampled bindings (pyramid + depth) per per-mip build set (2*ANO_VIEW_COUNT*ANO_MAX_HIZ_MIPS/frame) + cull binding 11 pyramids (ANO_VIEW_COUNT/frame).
	poolSize[3].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (ANO_VIEW_COUNT + 4u + 2u * ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS + ANO_VIEW_COUNT + 1u);
	// Hi-Z build set binding 1: one r32f storage-image dest per mip per view per frame.
	// + 1/frame: the text overlay raster destination.
	poolSize[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSize[4].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS + 1u);

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 5;
	poolInfo.pPoolSizes = poolSize;
	// maxSets: 2 blur sets/frame + (global+light-cull+tonemap)/view + cull+update+scatter+shadow(2) + ANO_VIEW_COUNT*ANO_MAX_HIZ_MIPS Hi-Z build sets/frame.
	poolInfo.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT * (3u * ANO_VIEW_COUNT + 9u + ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS + 2u); // +1 lightsetup, +2 text overlay

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &(rendererState.globalDescriptorPool)) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create descriptor pool!");
		return false;
	}

	return true;
}

bool createBindlessTextureArray(VulkanContext* ctx, RendererState* state)
{
	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = rendererState.bindlessTextures.maxTextures;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &rendererState.bindlessTextures.pool) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create bindless texture descriptor pool!");
		return false;
	}

	VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo = {};
	variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
	variableInfo.descriptorSetCount = 1;
	variableInfo.pDescriptorCounts = &rendererState.bindlessTextures.maxTextures;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = &variableInfo;
	allocInfo.descriptorPool = rendererState.bindlessTextures.pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &rendererState.bindlessTextures.layout;

	if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &rendererState.bindlessTextures.set) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to allocate bindless texture descriptor set!");
		return false;
	}

	return true;
}

bool createDescriptorSets(VulkanContext* ctx, RendererState* state)
{ // Central to init
	// Global, light-cull, tonemap sets are per view per frame, cull/update/scatter one per frame.
	enum { PERVIEW_SETS = MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT };

	VkDescriptorSetLayout layouts[PERVIEW_SETS];
	for (int i = 0; i < PERVIEW_SETS; ++i)
	{
		layouts[i] = rendererState.globalSetLayout;
	}
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.descriptorPool = rendererState.globalDescriptorPool;
	allocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
	allocInfo.pSetLayouts = layouts;

		VkDescriptorSet globalSetsTemp[PERVIEW_SETS];
	if (vkAllocateDescriptorSets(ctx->device, &allocInfo, globalSetsTemp) != VK_SUCCESS)
	{
        ano_log(ANO_FATAL, "Failed to allocate global descriptor sets!");
		return false;
	}
	for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++)
		for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
			rendererState.frames[i].views[v].globalSet = globalSetsTemp[i*ANO_VIEW_COUNT + v];

    VkDescriptorSetLayout cullLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        cullLayouts[i] = rendererState.culling.setLayout;
    }
    VkDescriptorSetAllocateInfo cullAllocInfo = {};
    cullAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    cullAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    cullAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    cullAllocInfo.pSetLayouts = cullLayouts;

        VkDescriptorSet cullSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &cullAllocInfo, cullSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate cull descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].cullSet = cullSetsTemp[i];

    VkDescriptorSetLayout updateLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        updateLayouts[i] = rendererState.updateSetLayout;
    }
    VkDescriptorSetAllocateInfo updateAllocInfo = {};
    updateAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    updateAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    updateAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    updateAllocInfo.pSetLayouts = updateLayouts;

    VkDescriptorSet updateSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &updateAllocInfo, updateSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate update descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].updateSet = updateSetsTemp[i];

    VkDescriptorSetLayout scatterLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        scatterLayouts[i] = rendererState.scatterSetLayout;
    }
    VkDescriptorSetAllocateInfo scatterAllocInfo = {};
    scatterAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    scatterAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    scatterAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    scatterAllocInfo.pSetLayouts = scatterLayouts;

    VkDescriptorSet scatterSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &scatterAllocInfo, scatterSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate scatter descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].scatterSet = scatterSetsTemp[i];

    VkDescriptorSetLayout lightsetupLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        lightsetupLayouts[i] = rendererState.lightsetupSetLayout;
    }
    VkDescriptorSetAllocateInfo lightsetupAllocInfo = {};
    lightsetupAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightsetupAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    lightsetupAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    lightsetupAllocInfo.pSetLayouts = lightsetupLayouts;

    VkDescriptorSet lightsetupSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &lightsetupAllocInfo, lightsetupSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate lightsetup descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].lightsetupSet = lightsetupSetsTemp[i];

    VkDescriptorSetLayout lightcullLayouts[PERVIEW_SETS];
    for (int i = 0; i < PERVIEW_SETS; ++i)
    {
        lightcullLayouts[i] = rendererState.lightcullSetLayout;
    }
    VkDescriptorSetAllocateInfo lightcullAllocInfo = {};
    lightcullAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightcullAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    lightcullAllocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
    lightcullAllocInfo.pSetLayouts = lightcullLayouts;

    VkDescriptorSet lightcullSetsTemp[PERVIEW_SETS];
    if (vkAllocateDescriptorSets(ctx->device, &lightcullAllocInfo, lightcullSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate light-cull descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++)
        for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
            rendererState.frames[i].views[v].lightcullSet = lightcullSetsTemp[i*ANO_VIEW_COUNT + v];

    // Tonemap set (one per view per frame): samples that view's HDR resolve target.
    VkDescriptorSetLayout tonemapLayouts[PERVIEW_SETS];
    for (int i = 0; i < PERVIEW_SETS; ++i)
    {
        tonemapLayouts[i] = rendererState.tonemapSetLayout;
    }
    VkDescriptorSetAllocateInfo tonemapAllocInfo = {};
    tonemapAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    tonemapAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    tonemapAllocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
    tonemapAllocInfo.pSetLayouts = tonemapLayouts;

    VkDescriptorSet tonemapSetsTemp[PERVIEW_SETS];
    if (vkAllocateDescriptorSets(ctx->device, &tonemapAllocInfo, tonemapSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate tonemap descriptor sets!");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
            rendererState.frames[i].views[v].tonemapSet = tonemapSetsTemp[i*ANO_VIEW_COUNT + v];
    }

    // Hi-Z build sets (review 4.9 step 3): one per mip per view per frame, allocated for ANO_MAX_HIZ_MIPS.
    VkDescriptorSetLayout hizLayouts[MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS; ++i)
        hizLayouts[i] = rendererState.hizSetLayout;
    VkDescriptorSetAllocateInfo hizAllocInfo = {};
    hizAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    hizAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    hizAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS);
    hizAllocInfo.pSetLayouts = hizLayouts;
    VkDescriptorSet hizSetsTemp[MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT * ANO_MAX_HIZ_MIPS];
    if (vkAllocateDescriptorSets(ctx->device, &hizAllocInfo, hizSetsTemp) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to allocate Hi-Z descriptor sets!");
        return false;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
            for (uint32_t m = 0; m < ANO_MAX_HIZ_MIPS; m++)
                rendererState.frames[i].views[v].hizSets[m] =
                    hizSetsTemp[(i * ANO_VIEW_COUNT + v) * ANO_MAX_HIZ_MIPS + m];

    // Shadow sets (audit 4.7): one shadowsetup set + one shadow geom/sampling set per frame.
    VkDescriptorSetLayout setupLayouts[MAX_FRAMES_IN_FLIGHT], geomLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) { setupLayouts[i] = rendererState.shadowSetupSetLayout; geomLayouts[i] = rendererState.shadowGeomSetLayout; }
    VkDescriptorSetAllocateInfo setupAlloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rendererState.globalDescriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = setupLayouts };
    VkDescriptorSet setupTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &setupAlloc, setupTemp) != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to allocate shadowsetup sets!"); return false; }
    VkDescriptorSetAllocateInfo geomAlloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rendererState.globalDescriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = geomLayouts };
    VkDescriptorSet geomTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &geomAlloc, geomTemp) != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to allocate shadow geom sets!"); return false; }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) { rendererState.frames[i].shadow.setupSet = setupTemp[i]; rendererState.frames[i].shadow.geomSet = geomTemp[i]; }

    // Moment-blur source sets (audit 4.7 MSM): two per frame (blur-X reads atlas, blur-Y reads temp).
    VkDescriptorSetLayout blurLayouts[2 * MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < 2 * MAX_FRAMES_IN_FLIGHT; ++i) blurLayouts[i] = rendererState.shadowBlurSetLayout;
    VkDescriptorSetAllocateInfo blurAlloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rendererState.globalDescriptorPool, .descriptorSetCount = 2 * MAX_FRAMES_IN_FLIGHT, .pSetLayouts = blurLayouts };
    VkDescriptorSet blurTemp[2 * MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &blurAlloc, blurTemp) != VK_SUCCESS) { ano_log(ANO_FATAL, "Failed to allocate shadow blur sets!"); return false; }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rendererState.frames[i].shadow.blurAtlasSet = blurTemp[2 * i + 0];
        rendererState.frames[i].shadow.blurTempSet  = blurTemp[2 * i + 1];
    }

	return true;
}

// Binds clustered-forward froxel buffers: global bindings 10/11 (fragment reads) + light-cull set bindings 0-4 (in UBO/light-runtime/lights, out count/index). Init-only.
void updateClusterDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    // Per view per frame: each view's froxel lists + camera UBO. transforms/lights are shared.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            VkDescriptorBufferInfo countInfo = { vr->clusterLightCountBuffer, 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo indexInfo = { vr->clusterLightIndexBuffer, 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo uboInfo   = { vr->uniformBuffer, 0, VK_WHOLE_SIZE };
            // Binding 1: precomputed per-light world pose (lightsetup.comp output).
            VkDescriptorBufferInfo poseInfo  = { state->lightRuntimeBuffer.buffer[i], 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo lightInfo = { state->lightBuffer.device, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet writes[7] = {};
            // Global set: 10 = cluster light count, 11 = cluster light index (fragment-read).
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = vr->globalSet;
            writes[0].dstBinding = 10;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &countInfo;
            writes[1] = writes[0];
            writes[1].dstBinding = 11;
            writes[1].pBufferInfo = &indexInfo;
            // Light-cull set: 0 UBO, 1 light runtime (pose), 2 lights, 3 count(out), 4 index(out).
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = vr->lightcullSet;
            writes[2].dstBinding = 0;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo = &uboInfo;
            writes[3] = writes[2];
            writes[3].dstBinding = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo = &poseInfo;
            writes[4] = writes[3];
            writes[4].dstBinding = 2;
            writes[4].pBufferInfo = &lightInfo;
            writes[5] = writes[3];
            writes[5].dstBinding = 3;
            writes[5].pBufferInfo = &countInfo;
            writes[6] = writes[3];
            writes[6].dstBinding = 4;
            writes[6].pBufferInfo = &indexInfo;

            vkUpdateDescriptorSets(ctx->device, 7, writes, 0, NULL);
        }
    }
}

// Binds dynamic shadow sets (audit 4.7), per frame: shadowsetup compute set (config/transforms/lights in, frustums out) + shadow geom/sampling set (frustum viewProjs, depth atlas sampler, per-light info). Init-only.
void updateShadowDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        ShadowResources* sh = &state->frames[i].shadow;
        VkDescriptorBufferInfo cfgI  = { state->shadowConfig.device, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo xfI   = { state->transformBuffer.buffer[i], 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo ltI   = { state->lightBuffer.device, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo frI   = { sh->frustumBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo infoI = { state->shadowInfo.device, 0, VK_WHOLE_SIZE };
        // Atlas + blur temp are single shared images, bound by every frame's sets.
        VkDescriptorImageInfo  atI   = { state->shadowSampler, state->shadowAtlasArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo  tmI   = { state->shadowSampler, state->shadowTempArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkDescriptorBufferInfo vpI = { sh->sampleVPBuffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet w[11] = {};
        // shadowsetup set (0 config, 1 transforms, 2 lights, 3 frustums-out, 4 sampling viewProjs-out), all storage.
        for (int j = 0; j < 5; ++j) {
            w[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[j].dstSet = sh->setupSet;
            w[j].dstBinding = (uint32_t)j;
            w[j].descriptorCount = 1;
            w[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        w[0].pBufferInfo = &cfgI; w[1].pBufferInfo = &xfI; w[2].pBufferInfo = &ltI; w[3].pBufferInfo = &frI;
        w[4].pBufferInfo = &vpI;

        // shadow geom/sampling set (0 frustum viewProjs, 1 atlas array sampler, 2 per-light info, 3 packed sampling viewProjs as UBO).
        w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[5].dstSet = sh->geomSet; w[5].dstBinding = 0; w[5].descriptorCount = 1;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[5].pBufferInfo = &frI;
        w[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[6].dstSet = sh->geomSet; w[6].dstBinding = 1; w[6].descriptorCount = 1;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[6].pImageInfo = &atI;
        w[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[7].dstSet = sh->geomSet; w[7].dstBinding = 2; w[7].descriptorCount = 1;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[7].pBufferInfo = &infoI;
        w[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[8].dstSet = sh->geomSet; w[8].dstBinding = 3; w[8].descriptorCount = 1;
        w[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[8].pBufferInfo = &vpI;

        // moment-blur source sets: blurAtlasSet samples the atlas (X pass), blurTempSet the temp (Y pass).
        w[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[9].dstSet = sh->blurAtlasSet; w[9].dstBinding = 0; w[9].descriptorCount = 1;
        w[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[9].pImageInfo = &atI;
        w[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[10].dstSet = sh->blurTempSet; w[10].dstBinding = 0; w[10].descriptorCount = 1;
        w[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[10].pImageInfo = &tmI;

        vkUpdateDescriptorSets(ctx->device, 11, w, 0, NULL);
    }
}

void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    // Per view per frame: the composite samples each view's HDR resolve into its destination rect.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = vr->hdrColorView;
            imageInfo.sampler = state->textureSampler;

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = vr->tonemapSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
        }
    }
}

// (Re)bind each per-mip Hi-Z build set (review 4.9 step 3), rerun after swapchain resize.
// Per set: 0 = pyramid all-mip sampled view, 1 = this mip's r32f storage dest, 2 = this view's MSAA depth (reduce source).
// Only the live hizMipCount mips are written.
void updateHiZDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            for (uint32_t m = 0; m < vr->hizMipCount; m++)
            {
                VkDescriptorImageInfo pyr = { .sampler = state->textureSampler, .imageView = vr->hizSampledView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
                VkDescriptorImageInfo dst = { .imageView = vr->hizMipViews[m], .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
                // Binding 2 = reduce depth source: depthMaxResolve binds the single-sample resolve view to every mip, else MSAA depth (sampler2DMS). Layout SHADER_READ.
                VkImageView depSrc = ctx->deviceCapabilities.depthMaxResolve ? vr->depthResolveView : vr->depthView;
                VkDescriptorImageInfo dep = { .sampler = state->textureSampler, .imageView = depSrc, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

                VkWriteDescriptorSet w[3] = {};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = vr->hizSets[m]; w[0].dstBinding = 0; w[0].descriptorCount = 1;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &pyr;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = vr->hizSets[m]; w[1].dstBinding = 1; w[1].descriptorCount = 1;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[1].pImageInfo = &dst;
                w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[2].dstSet = vr->hizSets[m]; w[2].dstBinding = 2; w[2].descriptorCount = 1;
                w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &dep;
                vkUpdateDescriptorSets(ctx->device, 3, w, 0, NULL);
            }
        }
    }

    // Cull set binding 11 (review 4.9 step 3): each frame's cull samples the pyramid built `lag` submits earlier (1 in-frame, 2 async). Layout SHADER_READ. Re-run on resize.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uint32_t lag = state->asyncHiz ? 2u : 1u;
        uint32_t prev = (i + MAX_FRAMES_IN_FLIGHT - lag) % MAX_FRAMES_IN_FLIGHT;
        VkDescriptorImageInfo hizImg[ANO_VIEW_COUNT];
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            hizImg[v].sampler = state->textureSampler;
            hizImg[v].imageView = state->frames[prev].views[v].hizSampledView;
            hizImg[v].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet cw = {};
        cw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw.dstSet = state->frames[i].cullSet;
        cw.dstBinding = 11;
        cw.descriptorCount = ANO_VIEW_COUNT;
        cw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cw.pImageInfo = hizImg;
        vkUpdateDescriptorSets(ctx->device, 1, &cw, 0, NULL);

        // Global set binding 13 (review priority 10): task meshlet cull samples the same lag slot's pyramid, one per view. Only when the task layout carries the binding.
        if (state->taskCull)
        {
            for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
            {
                VkWriteDescriptorSet tw = {};
                tw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                tw.dstSet = state->frames[i].views[v].globalSet;
                tw.dstBinding = 13;
                tw.descriptorCount = 1;
                tw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                tw.pImageInfo = &hizImg[v];
                vkUpdateDescriptorSets(ctx->device, 1, &tw, 0, NULL);
            }
        }
    }
}



void updateUboDescriptorSets(VulkanContext* ctx, RendererState* state)
{ // Central to init, must be called on asset uploads.

	// Update scene-wide UBO descriptors
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = rendererState.frames[i].views[0].uniformBuffer; // rebound per view below, view 0 for shared compute sets
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(GlobalUBO);

		VkDescriptorBufferInfo ssboInfo = {};
		ssboInfo.buffer = rendererState.transformBuffer.buffer[i];
		ssboInfo.offset = 0;
		ssboInfo.range = sizeof(mat4) * rendererState.transformBuffer.capacity;

		VkDescriptorBufferInfo materialInfo = {};
		materialInfo.buffer = rendererState.materialBuffer.buffer[i];
		materialInfo.offset = 0;
		materialInfo.range = sizeof(MaterialData) * rendererState.materialBuffer.capacity;

		VkDescriptorBufferInfo vertexBufferInfo = {};
		vertexBufferInfo.buffer = rendererState.globalGeometryPool.vertexBuffer;
		vertexBufferInfo.offset = 0;
		vertexBufferInfo.range = rendererState.globalGeometryPool.vertexCapacity;

		VkDescriptorBufferInfo indexBufferInfo = {};
		indexBufferInfo.buffer = rendererState.globalGeometryPool.indexBuffer;
		indexBufferInfo.offset = 0;
		indexBufferInfo.range = rendererState.globalGeometryPool.indexCapacity;

		uint32_t maxMeshes = ANO_MAX_MESHES; // must match createCullingBuffers' buffer sizing
		VkDescriptorBufferInfo globalMeshInfo = {};
		globalMeshInfo.buffer = rendererState.culling.meshDataBuffer[i];
		globalMeshInfo.offset = 0;
		globalMeshInfo.range = sizeof(uint32_t) * 9 * maxMeshes; // MeshData is 9 u32/slot (== buffer stride)

		VkDescriptorBufferInfo compactedEntityIndicesInfo = {};
		compactedEntityIndicesInfo.buffer = rendererState.culling.compactedEntityIndicesBuffer[i];
		compactedEntityIndicesInfo.offset = 0;
		compactedEntityIndicesInfo.range = sizeof(uint32_t) * rendererState.culling.maxEntities * ano_draw_partition_count();

		VkDescriptorBufferInfo lightInfo = {};
		lightInfo.buffer = rendererState.lightBuffer.device;        // ×1 device-local (SlotUpload)
		lightInfo.offset = 0;
		lightInfo.range = sizeof(LightData) * rendererState.lightBuffer.capacity;

		VkDescriptorBufferInfo instanceDataInfo = {};
		instanceDataInfo.buffer = rendererState.instanceDataBuffer.device;  // ×1 device-local (SlotUpload)
		instanceDataInfo.offset = 0;
		instanceDataInfo.range = sizeof(AnoInstanceData) * rendererState.instanceDataBuffer.capacity;

		VkDescriptorBufferInfo lightRuntimeInfo = {};
		lightRuntimeInfo.buffer = rendererState.lightRuntimeBuffer.buffer[i]; // ×3 device-local, lightsetup.comp output
		lightRuntimeInfo.offset = 0;
		lightRuntimeInfo.range = (VkDeviceSize)(sizeof(float) * 16u) * rendererState.lightRuntimeBuffer.capacity; // 64B/light (LightRuntime)

		VkWriteDescriptorSet descriptorWrites[11] = {};

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[0].dstBinding = 0;   // Corresponds to binding in shader.
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pBufferInfo = &ssboInfo;

		descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[2].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[2].dstBinding = 2;
		descriptorWrites[2].dstArrayElement = 0;
		descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[2].descriptorCount = 1;
		descriptorWrites[2].pBufferInfo = &materialInfo;

		VkDescriptorBufferInfo entityInfo = {};
		entityInfo.buffer = rendererState.culling.entity.device;   // ×1 device-local (SlotUpload)
		entityInfo.offset = 0;
		entityInfo.range = sizeof(uint32_t) * 2 * rendererState.culling.maxEntities;

		descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[3].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[3].dstBinding = 3;
		descriptorWrites[3].dstArrayElement = 0;
		descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[3].descriptorCount = 1;
		descriptorWrites[3].pBufferInfo = &entityInfo;

		descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[4].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[4].dstBinding = 4;
		descriptorWrites[4].dstArrayElement = 0;
		descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[4].descriptorCount = 1;
		descriptorWrites[4].pBufferInfo = &vertexBufferInfo;

		descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[5].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[5].dstBinding = 5;
		descriptorWrites[5].dstArrayElement = 0;
		descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[5].descriptorCount = 1;
		descriptorWrites[5].pBufferInfo = &indexBufferInfo;

		descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[6].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[6].dstBinding = 6;
		descriptorWrites[6].dstArrayElement = 0;
		descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[6].descriptorCount = 1;
		descriptorWrites[6].pBufferInfo = &globalMeshInfo;

		descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[7].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[7].dstBinding = 7;
		descriptorWrites[7].dstArrayElement = 0;
		descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[7].descriptorCount = 1;
		descriptorWrites[7].pBufferInfo = &compactedEntityIndicesInfo;

		descriptorWrites[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[8].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[8].dstBinding = 8;
		descriptorWrites[8].dstArrayElement = 0;
		descriptorWrites[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[8].descriptorCount = 1;
		descriptorWrites[8].pBufferInfo = &lightInfo;

		descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[9].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[9].dstBinding = 9;
		descriptorWrites[9].dstArrayElement = 0;
		descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[9].descriptorCount = 1;
		descriptorWrites[9].pBufferInfo = &instanceDataInfo;

		// Binding 12: per-light world pose (lightsetup.comp output), per frame.
		descriptorWrites[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[10].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[10].dstBinding = 12;
		descriptorWrites[10].dstArrayElement = 0;
		descriptorWrites[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[10].descriptorCount = 1;
		descriptorWrites[10].pBufferInfo = &lightRuntimeInfo;

		// Global set per view (audit 4.8): bindings 1-9 + 12 shared scene SSBOs, binding 0 = each view's camera UBO. Bindings 10/11 written by updateClusterDescriptorSets.
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			bufferInfo.buffer = rendererState.frames[i].views[v].uniformBuffer;
			for (int k = 0; k < 11; k++) descriptorWrites[k].dstSet = rendererState.frames[i].views[v].globalSet;
			vkUpdateDescriptorSets(ctx->device, 11, descriptorWrites, 0, NULL);
		}
		// Restore bufferInfo to view 0 for shared compute sets (view-independent GlobalUBO).
		bufferInfo.buffer = rendererState.frames[i].views[0].uniformBuffer;

        // Update cull sets
        VkDescriptorBufferInfo cullUboInfo = {};
        cullUboInfo.buffer = rendererState.culling.ubo.buffer[i];
        cullUboInfo.offset = 0;
        cullUboInfo.range = sizeof(CullUBO);

        VkDescriptorBufferInfo meshDataInfo = {};
        meshDataInfo.buffer = rendererState.culling.meshDataBuffer[i];
        meshDataInfo.offset = 0;
        meshDataInfo.range = sizeof(uint32_t) * 9 * maxMeshes; // MeshData is 9 u32/slot (== buffer stride)

        VkDescriptorBufferInfo meshBoundsInfo = {};
        meshBoundsInfo.buffer = rendererState.culling.meshBoundsBuffer[i];
        meshBoundsInfo.offset = 0;
        meshBoundsInfo.range = sizeof(float) * 4 * maxMeshes;

        VkDescriptorBufferInfo indirectInfo = {};
        indirectInfo.buffer = rendererState.indirectBuffer.buffer[i];
        indirectInfo.offset = 0;
        // Range uses the max command stride createIndirectDrawBuffer allocated (VkDrawIndexedIndirectCommand 20B > VkDrawMeshTasksIndirectCommandEXT 12B).
        VkDeviceSize indirectCmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
            ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
        indirectInfo.range = indirectCmdStride * rendererState.indirectBuffer.capacity * ano_draw_partition_count();

        VkDescriptorBufferInfo countInfo = {};
        countInfo.buffer = rendererState.culling.drawCountBuffer[i];
        countInfo.offset = 0;
        countInfo.range = sizeof(uint32_t) * ano_draw_partition_count();

        VkDescriptorBufferInfo compactedEntityIndicesCullInfo = {};
        compactedEntityIndicesCullInfo.buffer = rendererState.culling.compactedEntityIndicesBuffer[i];
        compactedEntityIndicesCullInfo.offset = 0;
        compactedEntityIndicesCullInfo.range = sizeof(uint32_t) * rendererState.culling.maxEntities * ano_draw_partition_count();

        VkDescriptorBufferInfo materialCullInfo = {};
        materialCullInfo.buffer = rendererState.materialBuffer.buffer[i];
        materialCullInfo.offset = 0;
        materialCullInfo.range = sizeof(MaterialData) * rendererState.materialBuffer.capacity;

        // Binding 9: the GPU-built shadow frustums (audit 4.7), per frame.
        VkDescriptorBufferInfo shadowFrustumCullInfo = {};
        shadowFrustumCullInfo.buffer = rendererState.frames[i].shadow.frustumBuffer;
        shadowFrustumCullInfo.offset = 0;
        shadowFrustumCullInfo.range = VK_WHOLE_SIZE;

        // Binding 10: per-draw depth keys (transparency sort). Camera partitions only.
        VkDescriptorBufferInfo sortKeysCullInfo = {};
        sortKeysCullInfo.buffer = rendererState.culling.sortKeysBuffer[i];
        sortKeysCullInfo.offset = 0;
        sortKeysCullInfo.range = sizeof(float) * (VkDeviceSize)ANO_VIEW_COUNT * rendererState.culling.maxEntities;

        VkWriteDescriptorSet cullWrites[11] = {};
        for(int j=0; j<11; ++j) {
            cullWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cullWrites[j].dstSet = rendererState.frames[i].cullSet;
            cullWrites[j].dstBinding = j;
            cullWrites[j].dstArrayElement = 0;
            cullWrites[j].descriptorCount = 1;
            if (j == 0) cullWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            else cullWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        cullWrites[0].pBufferInfo = &cullUboInfo;
        cullWrites[1].pBufferInfo = &ssboInfo; // TransformSSBO
        cullWrites[2].pBufferInfo = &entityInfo;
        cullWrites[3].pBufferInfo = &meshDataInfo;
        cullWrites[4].pBufferInfo = &meshBoundsInfo;
        cullWrites[5].pBufferInfo = &indirectInfo;
        cullWrites[6].pBufferInfo = &countInfo;
        cullWrites[7].pBufferInfo = &compactedEntityIndicesCullInfo;
        cullWrites[8].pBufferInfo = &materialCullInfo;
        cullWrites[9].pBufferInfo = &shadowFrustumCullInfo;
        cullWrites[10].pBufferInfo = &sortKeysCullInfo;

        vkUpdateDescriptorSets(ctx->device, 11, cullWrites, 0, NULL);

        // Update Compute Descriptor Sets
        VkDescriptorBufferInfo motionInfo = {};
        motionInfo.buffer = rendererState.motionBuffer.device;     // ×1 device-local (SlotUpload)
        motionInfo.offset = 0;
        motionInfo.range = sizeof(AnoMotionDescriptor) * rendererState.motionBuffer.capacity;

        VkDescriptorBufferInfo initialTransformInfo = {};
        initialTransformInfo.buffer = rendererState.initialTransformBuffer.device; // ×1 device-local (SlotUpload)
        initialTransformInfo.offset = 0;
        initialTransformInfo.range = sizeof(mat4) * rendererState.initialTransformBuffer.capacity;

        VkWriteDescriptorSet updateWrites[4] = {};
        for(int j=0; j<4; ++j) {
            updateWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            updateWrites[j].dstSet = rendererState.frames[i].updateSet;
            updateWrites[j].dstBinding = j;
            updateWrites[j].dstArrayElement = 0;
            updateWrites[j].descriptorCount = 1;
            if (j == 0) updateWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            else updateWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        updateWrites[0].pBufferInfo = &bufferInfo; // GlobalUBO
        updateWrites[1].pBufferInfo = &ssboInfo;   // TransformSSBO
        updateWrites[2].pBufferInfo = &motionInfo; // MotionSSBO
        updateWrites[3].pBufferInfo = &initialTransformInfo;

        vkUpdateDescriptorSets(ctx->device, 4, updateWrites, 0, NULL);

        // Scatter set (streamed transforms): 0 resolved slots, 1 xform ring (DYNAMIC, one slice), 2 live transform buffer.
        VkDescriptorBufferInfo streamSlotInfo = {};
        streamSlotInfo.buffer = rendererState.transformStream.slotBuffer[i];
        streamSlotInfo.offset = 0;
        streamSlotInfo.range = sizeof(uint32_t) * rendererState.transformStream.capacity;

        VkDescriptorBufferInfo streamXformInfo = {};
        streamXformInfo.buffer = rendererState.transformStream.xformRing;
        streamXformInfo.offset = 0;                                       // dynamic offset added at bind
        streamXformInfo.range = rendererState.transformStream.sliceStride; // one slice

        VkWriteDescriptorSet scatterWrites[3] = {};
        for (int j = 0; j < 3; ++j) {
            scatterWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            scatterWrites[j].dstSet = rendererState.frames[i].scatterSet;
            scatterWrites[j].dstBinding = (uint32_t)j;
            scatterWrites[j].dstArrayElement = 0;
            scatterWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            scatterWrites[j].descriptorCount = 1;
        }
        scatterWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        scatterWrites[0].pBufferInfo = &streamSlotInfo;
        scatterWrites[1].pBufferInfo = &streamXformInfo;
        scatterWrites[2].pBufferInfo = &ssboInfo; // same TransformSSBO update writes

        vkUpdateDescriptorSets(ctx->device, 3, scatterWrites, 0, NULL);

        // Light-setup set: transforms (in, buffer[i]) + lights (in, device) -> per-light world pose (out, buffer[i]).
        VkWriteDescriptorSet lightsetupWrites[3] = {};
        for (int j = 0; j < 3; ++j) {
            lightsetupWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lightsetupWrites[j].dstSet = rendererState.frames[i].lightsetupSet;
            lightsetupWrites[j].dstBinding = (uint32_t)j;
            lightsetupWrites[j].dstArrayElement = 0;
            lightsetupWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lightsetupWrites[j].descriptorCount = 1;
        }
        lightsetupWrites[0].pBufferInfo = &ssboInfo;      // TransformSSBO (buffer[i])
        lightsetupWrites[1].pBufferInfo = &lightInfo;     // LightSSBO (device)
        lightsetupWrites[2].pBufferInfo = &lightRuntimeInfo; // LightRuntimeSSBO (buffer[i], out)

        vkUpdateDescriptorSets(ctx->device, 3, lightsetupWrites, 0, NULL);
	}
}

