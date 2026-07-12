/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>
#include <anoptic_log.h>
#include "vulkan_backend/instance/pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Compute-pipeline prototypes + descriptor set layouts.
bool ano_vk_init_compute(VulkanContext* ctx, RendererState* state)
{
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

    // 2: MotionSSBO
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
        ano_log(ANO_FATAL, "Failed to create update descriptor set layout!");
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
        ano_log(ANO_FATAL, "Failed to create compute update pipeline layout!");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_UPDATE].type = PIPELINE_COMPUTE_UPDATE;
    state->prototypes[PIPELINE_COMPUTE_UPDATE].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_UPDATE].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_UPDATE].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule updateShaderModule = ano_pipeline_shader(ctx->device, "shaders/update.comp.spv");
    if (updateShaderModule == VK_NULL_HANDLE) return false;

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

    vkDestroyShaderModule(ctx->device, updateShaderModule, NULL);

    // Compute Scatter Pipeline (streamed transforms, Path B)
    VkDescriptorSetLayoutBinding scatterBindings[3] = {};
    for (int b = 0; b < 3; ++b) {
        scatterBindings[b].binding = (uint32_t)b;
        scatterBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        scatterBindings[b].descriptorCount = 1;
        scatterBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // 0: StreamSlots  1: StreamTransforms (dynamic)  2: TransformSSBO (written)
    scatterBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

    VkDescriptorSetLayoutCreateInfo scatterLayoutInfo = {};
    scatterLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    scatterLayoutInfo.bindingCount = 3;
    scatterLayoutInfo.pBindings = scatterBindings;

    if (vkCreateDescriptorSetLayout(ctx->device, &scatterLayoutInfo, NULL, &state->scatterSetLayout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create scatter descriptor set layout!");
        return false;
    }

    VkPushConstantRange scatterPcRange = {};
    scatterPcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    scatterPcRange.offset = 0;
    scatterPcRange.size = sizeof(uint32_t); // streamCount

    VkPipelineLayoutCreateInfo scatterPipelineLayoutInfo = {};
    scatterPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    scatterPipelineLayoutInfo.setLayoutCount = 1;
    scatterPipelineLayoutInfo.pSetLayouts = &state->scatterSetLayout;
    scatterPipelineLayoutInfo.pushConstantRangeCount = 1;
    scatterPipelineLayoutInfo.pPushConstantRanges = &scatterPcRange;

    if (vkCreatePipelineLayout(ctx->device, &scatterPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create compute scatter pipeline layout!");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_SCATTER].type = PIPELINE_COMPUTE_SCATTER;
    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_SCATTER].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule scatterShaderModule = ano_pipeline_shader(ctx->device, "shaders/scatter.comp.spv");
    if (scatterShaderModule == VK_NULL_HANDLE) return false;

    VkComputePipelineCreateInfo scatterPipelineInfo = {};
    scatterPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    scatterPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_SCATTER].layout;
    scatterPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    scatterPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    scatterPipelineInfo.stage.module = scatterShaderModule;
    scatterPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo scatterCacheInfo = {};
    scatterCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &scatterCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_SCATTER].cache, 1, &scatterPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations[0].pipeline) != VK_SUCCESS) return false;

    state->prototypes[PIPELINE_COMPUTE_SCATTER].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    vkDestroyShaderModule(ctx->device, scatterShaderModule, NULL);

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
        ano_log(ANO_FATAL, "Failed to create compute cull pipeline layout!");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_CULL].type = PIPELINE_COMPUTE_CULL;
    state->prototypes[PIPELINE_COMPUTE_CULL].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_CULL].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_CULL].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule compShaderModule = ano_pipeline_shader(ctx->device, "shaders/cull.comp.spv");
    if (compShaderModule == VK_NULL_HANDLE) return false;

    // constant_id 1: useMeshShader
    VkBool32 compUseMeshShader = ctx->deviceCapabilities.meshShader ? VK_TRUE : VK_FALSE;

    VkSpecializationMapEntry compSpecMapEntry = {};
    compSpecMapEntry.constantID = 1;
    compSpecMapEntry.offset = 0;
    compSpecMapEntry.size = sizeof(VkBool32);

    VkSpecializationInfo compSpecInfo = {};
    compSpecInfo.mapEntryCount = 1;
    compSpecInfo.pMapEntries = &compSpecMapEntry;
    compSpecInfo.dataSize = sizeof(VkBool32);
    compSpecInfo.pData = &compUseMeshShader;

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

    vkDestroyShaderModule(ctx->device, compShaderModule, NULL);

    // Compute Hi-Z Pyramid Build Pipeline. [0] reduce, [1] downsample via isReduce spec constant (0).
    // Push constant 24 B: { int srcMip; ivec2 dstSize; ivec2 srcSize; }
    VkPipelineCacheCreateInfo hizCacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(ctx->device, &hizCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].cache);

    VkPushConstantRange hizPush = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 24 };
    VkPipelineLayoutCreateInfo hizPipeLayoutInfo = {};
    hizPipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    hizPipeLayoutInfo.setLayoutCount = 1;
    hizPipeLayoutInfo.pSetLayouts = &state->hizSetLayout;
    hizPipeLayoutInfo.pushConstantRangeCount = 1;
    hizPipeLayoutInfo.pPushConstantRanges = &hizPush;
    if (vkCreatePipelineLayout(ctx->device, &hizPipeLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create Hi-Z pipeline layout!");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_HIZ].type = PIPELINE_COMPUTE_HIZ;
    state->prototypes[PIPELINE_COMPUTE_HIZ].implementationCount = 2;
    state->prototypes[PIPELINE_COMPUTE_HIZ].implementations = calloc(2, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_HIZ].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule hizShaderModule = ano_pipeline_shader(ctx->device, "shaders/hiz.comp.spv");
    if (hizShaderModule == VK_NULL_HANDLE) return false;

    // Depth MAX-resolve: both Hi-Z pipelines use the resolved single-sample module, else the base sampler2DMS.
    VkShaderModule hizResolveModule = VK_NULL_HANDLE;
    if (ctx->deviceCapabilities.depthMaxResolve)
    {
        hizResolveModule = ano_pipeline_shader(ctx->device, "shaders/hiz_resolve.comp.spv");
        if (hizResolveModule == VK_NULL_HANDLE) return false;
    }

    // Spec constants: id 0 isReduce, id 1 msaaSamples (reduce source sample count).
    struct HizSpecData { VkBool32 isReduce; int32_t msaaSamples; };
    VkSpecializationMapEntry hizSpecMap[2] = {
        { .constantID = 0, .offset = offsetof(struct HizSpecData, isReduce),    .size = sizeof(VkBool32) },
        { .constantID = 1, .offset = offsetof(struct HizSpecData, msaaSamples), .size = sizeof(int32_t)  },
    };
    for (uint32_t impl = 0; impl < 2; impl++)
    {
        struct HizSpecData hizSpecData = {
            .isReduce    = (impl == 0u) ? VK_TRUE : VK_FALSE, // [0] reduce, [1] downsample
            .msaaSamples = (int32_t)ctx->msaaSamples,
        };
        VkSpecializationInfo hizSpec = {};
        hizSpec.mapEntryCount = 2;
        hizSpec.pMapEntries = hizSpecMap;
        hizSpec.dataSize = sizeof(hizSpecData);
        hizSpec.pData = &hizSpecData;

        VkComputePipelineCreateInfo hizPipeInfo = {};
        hizPipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        hizPipeInfo.layout = state->prototypes[PIPELINE_COMPUTE_HIZ].layout;
        hizPipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        hizPipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        hizPipeInfo.stage.module = (hizResolveModule != VK_NULL_HANDLE) ? hizResolveModule : hizShaderModule;
        hizPipeInfo.stage.pName = "main";
        hizPipeInfo.stage.pSpecializationInfo = &hizSpec;

        if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_HIZ].cache, 1, &hizPipeInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].pipeline) != VK_SUCCESS) return false;
        state->prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }

    vkDestroyShaderModule(ctx->device, hizShaderModule, NULL);
    if (hizResolveModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(ctx->device, hizResolveModule, NULL);
    }

    // Compute Transparency-Sort Pipeline. Reuses the cull descriptor set layout; shares useMeshShader spec constant.
    VkPipelineCacheCreateInfo tpsortCacheInfo = {};
    tpsortCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &tpsortCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].cache);

    VkPipelineLayoutCreateInfo tpsortLayoutInfo = {};
    tpsortLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tpsortLayoutInfo.setLayoutCount = 1;
    tpsortLayoutInfo.pSetLayouts = &state->culling.setLayout;
    if (vkCreatePipelineLayout(ctx->device, &tpsortLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create transparency-sort pipeline layout!");
        return false;
    }

    state->prototypes[PIPELINE_COMPUTE_TPSORT].type = PIPELINE_COMPUTE_TPSORT;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_TPSORT].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule tpsortShaderModule = ano_pipeline_shader(ctx->device, "shaders/tpsort.comp.spv");
    if (tpsortShaderModule == VK_NULL_HANDLE) return false;

    VkBool32 tpsortUseMeshShader = ctx->deviceCapabilities.meshShader ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry tpsortSpecMapEntry = {};
    tpsortSpecMapEntry.constantID = 1;
    tpsortSpecMapEntry.offset = 0;
    tpsortSpecMapEntry.size = sizeof(VkBool32);
    VkSpecializationInfo tpsortSpecInfo = {};
    tpsortSpecInfo.mapEntryCount = 1;
    tpsortSpecInfo.pMapEntries = &tpsortSpecMapEntry;
    tpsortSpecInfo.dataSize = sizeof(VkBool32);
    tpsortSpecInfo.pData = &tpsortUseMeshShader;

    VkComputePipelineCreateInfo tpsortPipelineInfo = {};
    tpsortPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    tpsortPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_TPSORT].layout;
    tpsortPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tpsortPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    tpsortPipelineInfo.stage.module = tpsortShaderModule;
    tpsortPipelineInfo.stage.pName = "main";
    tpsortPipelineInfo.stage.pSpecializationInfo = &tpsortSpecInfo;

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_TPSORT].cache, 1, &tpsortPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    vkDestroyShaderModule(ctx->device, tpsortShaderModule, NULL);

    // Compute Light-cull Pipeline (clustered-forward froxel light assignment).
    // 0: GlobalUBO (in)  1: TransformSSBO (in, light world pos)  2: LightSSBO (in)
    // 3: clusterLightCount (out)  4: clusterLightIndices (out)
    VkDescriptorSetLayoutBinding lightcullBindings[5] = {};
    for (uint32_t b = 0; b < 5; ++b) {
        lightcullBindings[b].binding = b;
        lightcullBindings[b].descriptorType = (b == 0)
            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightcullBindings[b].descriptorCount = 1;
        lightcullBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lightcullLayoutInfo = {};
    lightcullLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightcullLayoutInfo.bindingCount = 5;
    lightcullLayoutInfo.pBindings = lightcullBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &lightcullLayoutInfo, NULL, &state->lightcullSetLayout) != VK_SUCCESS)
        return false;

    VkPipelineLayoutCreateInfo lightcullPipelineLayoutInfo = {};
    lightcullPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightcullPipelineLayoutInfo.setLayoutCount = 1;
    lightcullPipelineLayoutInfo.pSetLayouts = &state->lightcullSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &lightcullPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].type = PIPELINE_COMPUTE_LIGHTCULL;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule lightcullShaderModule = ano_pipeline_shader(ctx->device, "shaders/lightcull.comp.spv");
    if (lightcullShaderModule == VK_NULL_HANDLE) return false;

    VkComputePipelineCreateInfo lightcullPipelineInfo = {};
    lightcullPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    lightcullPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout;
    lightcullPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    lightcullPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    lightcullPipelineInfo.stage.module = lightcullShaderModule;
    lightcullPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo lightcullCacheInfo = {};
    lightcullCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &lightcullCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].cache, 1, &lightcullPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    vkDestroyShaderModule(ctx->device, lightcullShaderModule, NULL);

    // Compute Light-setup Pipeline: per-light world pose (worldPos/worldDir) precompute.
    // 0: TransformSSBO (in)  1: LightSSBO (in)  2: LightRuntimeSSBO (out, 64B/light). Push constant: light count.
    VkDescriptorSetLayoutBinding lightsetupBindings[3] = {};
    for (uint32_t b = 0; b < 3; ++b) {
        lightsetupBindings[b].binding = b;
        lightsetupBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightsetupBindings[b].descriptorCount = 1;
        lightsetupBindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo lightsetupLayoutInfo = {};
    lightsetupLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightsetupLayoutInfo.bindingCount = 3;
    lightsetupLayoutInfo.pBindings = lightsetupBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &lightsetupLayoutInfo, NULL, &state->lightsetupSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange lightsetupPush = {};
    lightsetupPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    lightsetupPush.offset = 0;
    lightsetupPush.size = sizeof(uint32_t); // lightCount

    VkPipelineLayoutCreateInfo lightsetupPipelineLayoutInfo = {};
    lightsetupPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightsetupPipelineLayoutInfo.setLayoutCount = 1;
    lightsetupPipelineLayoutInfo.pSetLayouts = &state->lightsetupSetLayout;
    lightsetupPipelineLayoutInfo.pushConstantRangeCount = 1;
    lightsetupPipelineLayoutInfo.pPushConstantRanges = &lightsetupPush;
    if (vkCreatePipelineLayout(ctx->device, &lightsetupPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].type = PIPELINE_COMPUTE_LIGHTSETUP;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule lightsetupShaderModule = ano_pipeline_shader(ctx->device, "shaders/lightsetup.comp.spv");
    if (lightsetupShaderModule == VK_NULL_HANDLE) return false;

    VkComputePipelineCreateInfo lightsetupPipelineInfo = {};
    lightsetupPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    lightsetupPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].layout;
    lightsetupPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    lightsetupPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    lightsetupPipelineInfo.stage.module = lightsetupShaderModule;
    lightsetupPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo lightsetupCacheInfo = {};
    lightsetupCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &lightsetupCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].cache, 1, &lightsetupPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_LIGHTSETUP].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    vkDestroyShaderModule(ctx->device, lightsetupShaderModule, NULL);

    // Compute Shadow-setup Pipeline: builds each shadow frustum's light-space viewProj + planes.
    VkPipelineLayoutCreateInfo shadowSetupLayoutInfo = {};
    shadowSetupLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowSetupLayoutInfo.setLayoutCount = 1;
    shadowSetupLayoutInfo.pSetLayouts = &state->shadowSetupSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &shadowSetupLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].type = PIPELINE_COMPUTE_SHADOWSETUP;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].supportedFeatures = PBR_FEATURE_NONE;

    VkShaderModule shadowSetupModule = ano_pipeline_shader(ctx->device, "shaders/shadowsetup.comp.spv");
    if (shadowSetupModule == VK_NULL_HANDLE) return false;

    VkComputePipelineCreateInfo shadowSetupPipelineInfo = {};
    shadowSetupPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    shadowSetupPipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].layout;
    shadowSetupPipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowSetupPipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shadowSetupPipelineInfo.stage.module = shadowSetupModule;
    shadowSetupPipelineInfo.stage.pName = "main";

    VkPipelineCacheCreateInfo shadowSetupCacheInfo = {};
    shadowSetupCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &shadowSetupCacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].cache);

    if (vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].cache, 1, &shadowSetupPipelineInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations[0].pipeline) != VK_SUCCESS) return false;
    state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    vkDestroyShaderModule(ctx->device, shadowSetupModule, NULL);

    return true;
}
