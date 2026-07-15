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

// Shader module from logical resource via resource manager. Caller destroys module. NULL on miss.
VkShaderModule ano_pipeline_shader(VkDevice device, const char* logical);

// Task meshlet-cull stage. Call only when taskCull. store outlives create; caller destroys *outModule.
// in: shadowPass/coneCull = flat.task constant_id 0/1; out: *stage or false
typedef struct TaskStageStorage
{
    VkSpecializationMapEntry entries[2];
    VkBool32                 data[2];
    VkSpecializationInfo     spec;
} TaskStageStorage;
bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
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
