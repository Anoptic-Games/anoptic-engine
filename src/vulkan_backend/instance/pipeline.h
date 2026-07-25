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
// in:  code = an already-loaded SPIR-V blob, borrowed for the call
// out: the module, or VK_NULL_HANDLE when vkCreateShaderModule refuses it. That sentinel is the
//      only failure channel and must reach ano_pipeline_stage, never a stage struct.
[[nodiscard]] VkShaderModule createShaderModule(VkDevice device, struct Buffer* code);

// The sole route a shader module takes into a pipeline stage: a refused mint stops here instead
// of reaching vkCreate*Pipelines (VUID-VkPipelineShaderStageCreateInfo-module).
// in:  stage = one stage bit; module = createShaderModule's return; spec = caller-owned
//      specialization info that must outlive pipeline creation, NULL for none
// out: false with *out untouched when module is VK_NULL_HANDLE, else true with *out complete
// inv: no builder writes VkPipelineShaderStageCreateInfo.module by hand
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
