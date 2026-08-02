/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_log.h>
#include "vulkan_backend/instance/descriptor_layout_schema.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/pipeline_registry.h"
#include <stdlib.h>
#include <stddef.h>

#include <vulkan/vulkan.h>

// Compute prototypes + descriptor layouts.
// Cache idiom: refused mint -> VK_NULL_HANDLE; init continues.
// Commit-last: publish implementationCount only after array exists.
template<PipelineType Type>
static bool compute_build(VulkanContext* ctx, RendererState* state,
    const VkSpecializationInfo* specialization = nullptr, uint32_t implementation = 0,
    const char* shaderPath = ano_pipeline_compute_shader_path<Type>())
{
    constexpr auto spec = ano_pipeline_spec<Type>();
    static_assert(spec.kind == AnoPipelineKind::compute);
    PipelinePrototype* proto = &state->prototypes[Type];
    if (proto->implementations == nullptr && !ano_pipeline_prepare_prototype(proto, Type))
        return false;
    if (proto->type != Type || implementation >= proto->implementationCount)
        return false;

    if (implementation == 0) {
        VkPipelineCacheCreateInfo cacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        if (vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &proto->cache) != VK_SUCCESS)
            proto->cache = VK_NULL_HANDLE;
    }

    struct Buffer code = {};
    if (!loadFile(shaderPath, &code))
        return false;
    VkShaderModule module = createShaderModule(ctx->device, &code);
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = proto->layout,
    };
    const bool built = ano_pipeline_stage(
        VK_SHADER_STAGE_COMPUTE_BIT, module, specialization, &pipelineInfo.stage) &&
        vkCreateComputePipelines(ctx->device, proto->cache, 1, &pipelineInfo, NULL,
            &proto->implementations[implementation].pipeline) == VK_SUCCESS;
    ano_aligned_free(code.data);
    vkDestroyShaderModule(ctx->device, module, NULL);
    return built;
}

// Unwind: the build lambda returns false; each typed mint discharges its shader immediately.
bool ano_vk_init_compute(VulkanContext* ctx, RendererState* state)
{
    bool ok = [&]() -> bool {

    // Compute Update Pipeline
    const auto& updateSpecs = ANO_VK_UPDATE_BINDINGS;
    VkDescriptorSetLayoutBinding updateBindings[ANO_VK_UPDATE_BINDINGS.count] = {};
    if (!ano_vk_materialize_layout_bindings(updateSpecs, updateBindings))
        return false;

    VkDescriptorSetLayoutCreateInfo updateLayoutInfo = {};
    updateLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    updateLayoutInfo.bindingCount = updateSpecs.count;
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

    if (!compute_build<PIPELINE_COMPUTE_UPDATE>(ctx, state))
        return false;
    
    // Compute Scatter Pipeline (streamed transforms, Path B)
    const auto& scatterSpecs = ANO_VK_SCATTER_BINDINGS;
    VkDescriptorSetLayoutBinding scatterBindings[ANO_VK_SCATTER_BINDINGS.count] = {};
    if (!ano_vk_materialize_layout_bindings(scatterSpecs, scatterBindings))
        return false;

    VkDescriptorSetLayoutCreateInfo scatterLayoutInfo = {};
    scatterLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    scatterLayoutInfo.bindingCount = scatterSpecs.count;
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

    if (!compute_build<PIPELINE_COMPUTE_SCATTER>(ctx, state))
        return false;

    // Compute Culling Pipeline
    VkPipelineLayoutCreateInfo compLayoutInfo = {};
    compLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compLayoutInfo.setLayoutCount = 1;
    compLayoutInfo.pSetLayouts = &state->culling.setLayout;

    if (vkCreatePipelineLayout(ctx->device, &compLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_CULL].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create compute cull pipeline layout!");
        return false;
    }

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

    if (!compute_build<PIPELINE_COMPUTE_CULL>(ctx, state, &compSpecInfo))
        return false;
    
    // Compute Hi-Z Pyramid Build Pipeline. [0] reduce, [1] downsample via isReduce spec constant (0).
    // Push constant 24 B: { int srcMip; ivec2 dstSize; ivec2 srcSize; }
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

    const char* hizShaderPath = ctx->deviceCapabilities.depthMaxResolve
        ? "resources/shaders/hiz_resolve.comp.spv"
        : ano_pipeline_compute_shader_path<PIPELINE_COMPUTE_HIZ>();

    // Spec constants: id 0 isReduce, id 1 msaaSamples (reduce source sample count).
    constexpr auto hizPipeline = ano_pipeline_spec<PIPELINE_COMPUTE_HIZ>();
    struct HizSpecData { VkBool32 isReduce; int32_t msaaSamples; };
    VkSpecializationMapEntry hizSpecMap[2] = {
        { .constantID = 0, .offset = offsetof(struct HizSpecData, isReduce),    .size = sizeof(VkBool32) },
        { .constantID = 1, .offset = offsetof(struct HizSpecData, msaaSamples), .size = sizeof(int32_t)  },
    };
    for (uint32_t impl = 0; impl < hizPipeline.implementationCount; ++impl)
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

        if (!compute_build<PIPELINE_COMPUTE_HIZ>(
                ctx, state, &hizSpec, impl, hizShaderPath))
            return false;
    }

    // Compute Transparency-Sort Pipeline. Reuses the cull descriptor set layout; shares useMeshShader spec constant.
    VkPipelineLayoutCreateInfo tpsortLayoutInfo = {};
    tpsortLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tpsortLayoutInfo.setLayoutCount = 1;
    tpsortLayoutInfo.pSetLayouts = &state->culling.setLayout;
    if (vkCreatePipelineLayout(ctx->device, &tpsortLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TPSORT].layout) != VK_SUCCESS)
    {
        ano_log(ANO_FATAL, "Failed to create transparency-sort pipeline layout!");
        return false;
    }

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

    if (!compute_build<PIPELINE_COMPUTE_TPSORT>(ctx, state, &tpsortSpecInfo))
        return false;
    // Compute Light-cull Pipeline (clustered-forward froxel light assignment).
    // 0: GlobalUBO (in)  1: TransformSSBO (in, light world pos)  2: LightSSBO (in)
    // 3: clusterLightCount (out)  4: clusterLightIndices (out)
    const auto& lightcullSpecs = ANO_VK_LIGHT_CULL_BINDINGS;
    VkDescriptorSetLayoutBinding lightcullBindings[ANO_VK_LIGHT_CULL_BINDINGS.count] = {};
    if (!ano_vk_materialize_layout_bindings(lightcullSpecs, lightcullBindings))
        return false;
    VkDescriptorSetLayoutCreateInfo lightcullLayoutInfo = {};
    lightcullLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightcullLayoutInfo.bindingCount = lightcullSpecs.count;
    lightcullLayoutInfo.pBindings = lightcullBindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &lightcullLayoutInfo, NULL, &state->lightcullSetLayout) != VK_SUCCESS)
        return false;

    VkPipelineLayoutCreateInfo lightcullPipelineLayoutInfo = {};
    lightcullPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightcullPipelineLayoutInfo.setLayoutCount = 1;
    lightcullPipelineLayoutInfo.pSetLayouts = &state->lightcullSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &lightcullPipelineLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout) != VK_SUCCESS)
        return false;

    if (!compute_build<PIPELINE_COMPUTE_LIGHTCULL>(ctx, state))
        return false;
    // Compute Light-setup Pipeline: per-light world pose (worldPos/worldDir) precompute.
    // 0: TransformSSBO (in)  1: LightSSBO (in)  2: LightRuntimeSSBO (out, 64B/light). Push constant: light count.
    const auto& lightsetupSpecs = ANO_VK_LIGHT_SETUP_BINDINGS;
    VkDescriptorSetLayoutBinding lightsetupBindings[ANO_VK_LIGHT_SETUP_BINDINGS.count] = {};
    if (!ano_vk_materialize_layout_bindings(lightsetupSpecs, lightsetupBindings))
        return false;
    VkDescriptorSetLayoutCreateInfo lightsetupLayoutInfo = {};
    lightsetupLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightsetupLayoutInfo.bindingCount = lightsetupSpecs.count;
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

    if (!compute_build<PIPELINE_COMPUTE_LIGHTSETUP>(ctx, state))
        return false;
    // Compute Shadow-setup Pipeline: builds each shadow frustum's light-space viewProj + planes.
    VkPipelineLayoutCreateInfo shadowSetupLayoutInfo = {};
    shadowSetupLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowSetupLayoutInfo.setLayoutCount = 1;
    shadowSetupLayoutInfo.pSetLayouts = &state->shadowSetupSetLayout;
    if (vkCreatePipelineLayout(ctx->device, &shadowSetupLayoutInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_SHADOWSETUP].layout) != VK_SUCCESS)
        return false;

    if (!compute_build<PIPELINE_COMPUTE_SHADOWSETUP>(ctx, state))
        return false;
    return true;
    }();

    return ok;
}
