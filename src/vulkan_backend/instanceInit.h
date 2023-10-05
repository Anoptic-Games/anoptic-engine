/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef INSTANCEINIT_H
#define INSTANCEINIT_H

#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"

// Function interfaces

// Initializes a Vulkan instance
VkResult createInstance(VkInstance *instance, VkDebugUtilsMessengerEXT *debugMessenger);

// Casts a Vulkan instance into the fires of perdition
void cleanupVulkan(VulkanComponents* components);

// Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
GLFWwindow* initWindow(VulkanComponents* components, WindowParameters parameters, Monitors* monitors);

// Enumerates all monitors and their parameters
void enumerateMonitors(Monitors* monitors);

// Creates a target surface
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);

// Selects the optimal graphics device
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char*** availableDevices, uint32_t* deviceCount, char* preferredDevice);

// Initializes a logical device based on a chosen physical device
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);

// Initializes a swap chain
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window, uint32_t preferredMode);

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
bool createSyncObjects(VulkanComponents* components);

// Frees up memory allocated for monitor info
void cleanupMonitors(Monitors* monitors);

#endif
