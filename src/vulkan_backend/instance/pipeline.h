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


// Creates a render pass
bool createRenderPass(VulkanComponents* components, VkDevice device, VkFormat swapChainImageFormat, VkRenderPass* renderPass);

// Creates a graphics pipeline
VkPipeline createGraphicsPipeline(VulkanComponents* components);



#endif
