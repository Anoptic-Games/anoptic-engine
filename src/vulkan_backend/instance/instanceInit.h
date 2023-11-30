/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef INSTANCEINIT_H
#define INSTANCEINIT_H

#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/vulkanConfig.h"
#include "vulkan_backend/texture/texture.h"
#include "anoptic_time.h"

// Function interfaces

// Initializes a Vulkan instance
VkResult createInstance(VulkanComponents* vkComponents);

// Casts a Vulkan instance into the fires of perdition
void cleanupVulkan(VulkanComponents* components);

// Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
GLFWwindow* initWindow(VulkanComponents* components, Monitors* monitors);

// Enumerates all monitors and their parameters
void enumerateMonitors(Monitors* monitors);

// Creates a target surface
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);

// Selects the optimal graphics device
bool pickPhysicalDevice(VulkanComponents* components, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice);

// Initializes a logical device based on a chosen physical device
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);

// Initializes a swap chain
SwapChainGroup initSwapChain(VulkanComponents *components, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain);

// Does the same, again
void recreateSwapChain(VulkanComponents* components, GLFWwindow* window);

// Generic helper function for creating 2D image views
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

// You know what this does
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);

// Creates framebuffers
bool createFramebuffers(VulkanComponents* components);

// Creates a command pool
bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool);

// Generic function for data buffer creation, updates buffer and bufferMemory with the created addresses
bool createDataBuffer(VulkanComponents* components, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory);

// Creates a vertex buffer
bool createVertexBuffer(VulkanComponents* components, uint32_t vertexCount, VkBuffer* vertex, VkDeviceMemory* vertexMemory);

// Creates an index buffer
bool createIndexBuffer(VulkanComponents* components, uint32_t indexCount, VkBuffer* index, VkDeviceMemory* indexMemory);

// Creates uniform buffers for each frame
bool createUniformBuffers(VulkanComponents* components);

// Upsades the uniform buffer
bool updateUniformBuffer(VulkanComponents* components);

// Creates a color draw target for MSAA
void createColorResources(VulkanComponents* components);

// Creates a depth image and view for the current swapchain
bool createDepthResources(VulkanComponents* components);

// Creates a descriptor pool
bool createDescriptorPool(VulkanComponents* components);

// Creates a descriptor set
bool createDescriptorSets(VulkanComponents* components);

// Updates descriptor sets to point to their corresponding uniform buffers
void updateDescriptorSets(VulkanComponents* components);

// Finds available memory types appropriate for a given buffer
uint32_t findMemoryType(VulkanComponents* components, uint32_t typeFilter, VkMemoryPropertyFlags properties);

// Allocates memory for a buffer
bool allocateBuffer(VulkanComponents* components, VkBuffer buffer, VkMemoryPropertyFlags properties, VkDeviceMemory* bufferMemory);

// Transfers data from the host to a destination buffer
bool stagingTransfer(VulkanComponents* components, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize);

// Helper function to decrease verbosity of transient command calls
VkCommandBuffer beginSingleTimeCommands(VulkanComponents* components);

// Returns true if the given format has a stencil component
bool hasStencilComponent(VkFormat format);

// Helper function to decrease verbosity of transient command calls, to be used after beginSingleTimeCommands()
void endSingleTimeCommands(VulkanComponents* components, VkCommandBuffer commandBuffer);

// Copies data from one GPU buffer to another
bool copyBuffer(VulkanComponents* components, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

// Creates a command buffer
bool createCommandBuffer(VulkanComponents* components);

// Creates fences, semaphores, etc.
bool createSyncObjects(VulkanComponents* components);

// Frees up memory allocated for monitor info
void cleanupMonitors(Monitors* monitors);

// More Function Prototypes
// TODO: SSA can sort this out
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount);
const char** getRequiredExtensions(uint32_t* extensionsCount);
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger);

#endif
