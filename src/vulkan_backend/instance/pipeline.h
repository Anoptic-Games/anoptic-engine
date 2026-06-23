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

bool ano_vk_init_global_layout(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_cull_layout(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_material_layouts(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_pipelines(VulkanContext* ctx, RendererState* state);
bool ano_vk_init_tonemap(VulkanContext* ctx, RendererState* state); // fullscreen HDR->swapchain encode
bool ano_vk_init_shadow(VulkanContext* ctx, RendererState* state);  // depth-only shadow pipeline + compare sampler
void ano_vk_cleanup_pipelines(VulkanContext* ctx, RendererState* state);

#endif
