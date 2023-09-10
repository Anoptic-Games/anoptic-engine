//========================================================================
// Anoptic Engine 0.01
//------------------------------------------------------------------------
// Copyright (c) 2023 Matei Anghel
// Copyright (c) 2023 Cristian Necsoiu
//
// This file is part of 'The Anoptic Engine'.
// 
// 'The Anoptic Engine' is free software: you can redistribute it
// and/or modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation, version 3 of the License.
//
// 'The Anoptic Engine' is distributed WITHOUT ANY WARRANTY, without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
// See the GNU General Public License for more details.
//
// This notice may not be removed or altered from any source distribution.
//
// You should have received a copy of the GNU General Public License along with this software. 
// If not, see <https://www.gnu.org/licenses/>.
//
//========================================================================

#include <vulkan/vulkan.h>

#ifndef STRUCTS_H
#define STRUCTS_H
#include "graphics/structs.h"
#endif

// Function interfaces


// Creates a render pass
bool createRenderPass(VkDevice device, VkFormat swapChainImageFormat, VkRenderPass* renderPass);

// Creates a graphics pipeline
VkPipeline createGraphicsPipeline(VkDevice device, VkExtent2D swapChainExtent, VkPipelineLayout *pipelineLayout, VkRenderPass renderPass);
