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
#include "vulkan_backend/structs.h"
#endif

// Function interfaces

// Initializes a Vulkan instance
VkResult createInstance(VkInstance *instance, VkDebugUtilsMessengerEXT *debugMessenger);

// Casts a Vulkan instance into the fires of perdition
void cleanupVulkan(VulkanComponents* components);

// Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
GLFWwindow* initWindow(VulkanComponents* components);

// Creates a target surface
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);

// Selects the optimal graphics device
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);

// Initializes a logical device based on a chosen physical device
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);

// Initializes a swap chain
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window);

// Does the same, again
void recreateSwapChain(VulkanComponents* components, GLFWwindow* window);

// You know what this does
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);

// Creates framebuffers
bool createFramebuffers(VkDevice device, FrameBufferGroup* frameBufferGroup, ImageViewGroup viewGroup, SwapChainGroup swapGroup, VkRenderPass renderPass);

// Creates a command pool
bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool);

// Creates a command buffer
bool createCommandBuffer(VulkanComponents* components);

// Creates fences, semaphores, etc.
bool createSyncObjects(VulkanComponents* components) ;
