/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef PIPELINE_H
#define PIPELINE_H


#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"

// Pipeline-specific structs
struct Buffer
{
    uint32_t size;
    char* data;
};


// Creates a render pass
bool createRenderPass(VkDevice device, VkFormat swapChainImageFormat, VkRenderPass* renderPass);

// Creates a graphics pipeline
VkPipeline createGraphicsPipeline(VkDevice device, VkExtent2D swapChainExtent, VkPipelineLayout *pipelineLayout, VkRenderPass renderPass);



#endif