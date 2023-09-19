/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef VULKANMASTER_H
#define VULKANMASTER_H


#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"

// Function interfaces

// Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
VulkanComponents* initVulkan(GLFWwindow* window, VulkanComponents* components); 

// A celebration
void unInitVulkan();

// Draws a single frame

void drawFrame(VulkanComponents* components, GLFWwindow* window);

#endif