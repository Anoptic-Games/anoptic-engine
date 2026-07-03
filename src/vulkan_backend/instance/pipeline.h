/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef PIPELINE_H
#define PIPELINE_H


#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include "anoptic_memalign.h"
#include "vulkan_backend/vertex/vertex.h"

// Pipeline-specific structs
struct Buffer
{
    uint32_t size;
    char* data;
};

// Shader loading utilities shared by pipeline implementations
bool loadFile(const char* filename, struct Buffer* buffer);
VkShaderModule createShaderModule(VkDevice device, struct Buffer* code);

// Task meshlet-cull stage (review priority 10), shared by every mesh-drawing pipeline builder.
// Storage for the specialization structs is caller-provided and must outlive pipeline creation;
// the caller destroys *outModule after vkCreateGraphicsPipelines. Call only when state->taskCull.
// in:  shadowPass/coneCull = flat.task constant_id 0/1 for this pipeline's lane
// out: *stage ready to prepend to pStages; false on shader-load failure
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
bool ano_vk_init_tonemap(VulkanContext* ctx, RendererState* state); // fullscreen HDR->swapchain encode
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state);  // depth-only shadow pipeline + compare sampler
void ano_vk_cleanup_pipelines(VulkanContext* ctx, RendererState* state);

#endif
