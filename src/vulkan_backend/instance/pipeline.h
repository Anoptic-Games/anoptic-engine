/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef PIPELINE_H
#define PIPELINE_H


#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include <anoptic_memory.h>
#include "vulkan_backend/vertex/vertex.h"

// Pipeline-specific structs
struct Buffer
{
    uint32_t size;
    char* data;
};

// Shader loading utilities.
// filename is relative to the executable directory ("resources/shaders/x.spv").
bool loadFile(const char* filename, struct Buffer* buffer);
// in:  code = borrowed SPIR-V blob
// out: module, or VK_NULL_HANDLE on refuse (sole fail path -> ano_pipeline_stage)
[[nodiscard]] VkShaderModule createShaderModule(VkDevice device, struct Buffer* code);

// Materialize the reflected count/features/bind point into an otherwise uncommitted prototype.
// false leaves implementations/count unpublished.
[[nodiscard]] bool ano_pipeline_prepare_prototype(PipelinePrototype* proto, PipelineType type);

// Sole module -> stage path. VK_NULL_HANDLE stops here (VUID-VkPipelineShaderStageCreateInfo-module).
// in:  stage bit; module from createShaderModule; spec outlives create, NULL for none
// out: false, *out untouched on VK_NULL_HANDLE; else true, *out complete
// inv: builders never write .module by hand
[[nodiscard]] static inline bool ano_pipeline_stage(VkShaderStageFlagBits stage, VkShaderModule module,
                                                    const VkSpecializationInfo* spec,
                                                    VkPipelineShaderStageCreateInfo* out)
{
    if (module == VK_NULL_HANDLE)
        return false;
    *out = (VkPipelineShaderStageCreateInfo){ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage, .module = module, .pName = "main", .pSpecializationInfo = spec };
    return true;
}

// Task meshlet-cull stage shared by mesh-drawing pipeline builders.
// Caller-provided storage must outlive pipeline creation.
// Caller destroys *outModule after vkCreateGraphicsPipelines.
// Call only when state->taskCull.
// in:  shadowPass/coneCull = flat.task constant_id 0/1 for this pipeline's lane
// out: *stage ready to prepend to pStages, false on shader-load failure
typedef struct TaskStageStorage
{
    VkSpecializationMapEntry entries[2];
    VkBool32                 data[2];
    VkSpecializationInfo     spec;
} TaskStageStorage;
[[nodiscard]] bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
                                           TaskStageStorage* store, VkShaderModule* outModule,
                                           VkPipelineShaderStageCreateInfo* stage);

bool ano_vk_init_global_layout(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_cull_layout(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_material_layouts(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_pipelines(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_compute(VulkanContext* ctx, RendererState* state); // compute prototypes (pipelines/compute.c)
bool ano_vk_init_tonemap(VulkanContext* ctx, RendererState* state); // fullscreen HDR->swapchain encode
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state);  // depth-only shadow pipeline + compare sampler
void ano_vk_cleanup_pipelines(VulkanContext* ctx, RendererState* state);

#endif
