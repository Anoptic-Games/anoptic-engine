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
VkResult createInstance(VulkanContext* ctx);

// Casts a Vulkan instance into the fires of perdition
void cleanupVulkan(VulkanContext* ctx);

// Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
GLFWwindow* initWindow(VulkanContext* ctx, Monitors* monitors);

// Enumerates all monitors and their parameters
void enumerateMonitors(Monitors* monitors);

// Creates a target surface
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);

// Selects the optimal graphics device
bool pickPhysicalDevice(VulkanContext* ctx, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice);

// Populates physical device capabilities
struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device);

// Initializes a logical device based on a chosen physical device
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);

// Initializes a swap chain
bool initSwapChain(VulkanContext* ctx, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state);

void recreateSwapChain(VulkanContext* ctx, GLFWwindow* window);

void cleanupSwapChain(VulkanContext* ctx, RendererState* state);

// Generic helper function for creating 2D image views
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

bool createImageViews(VulkanContext* ctx, RendererState* state);



// Creates a command pool
bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool);

// Generic function for data buffer creation, updates buffer and bufferMemory with the created addresses
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation);

// Creates uniform buffers for each frame
bool createUniformBuffers(VulkanContext* ctx, RendererState* state);

// updateUniformBuffer declared in vulkan_backend/frame/frame.h

// Updates a mesh's transform matrices
bool updateMeshTransforms(VulkanContext* ctx, RenderEntity* entity, float move);

// Creates a color draw target for MSAA
void createColorResources(VulkanContext* ctx);

// Creates a depth image and view for the current swapchain
bool createDepthResources(VulkanContext* ctx, RendererState* state);

// Per-view half-res R32F depth pyramids, recreated with the swapchain
bool createHiZResources(VulkanContext* ctx, RendererState* state);
void updateHiZDescriptorSets(VulkanContext* ctx, RendererState* state);

// Creates the descriptor pool
bool createDescriptorPool(VulkanContext* ctx, RendererState* state);

bool createBindlessTextureArray(VulkanContext* ctx, RendererState* state);

// Creates the descriptor sets
bool createDescriptorSets(VulkanContext* ctx, RendererState* state);

// Updates UBO descriptor sets to point to their corresponding uniform buffers
void updateUboDescriptorSets(VulkanContext* ctx, RendererState* state);

// (Re)binds each frame's tonemap set to its HDR resolve view; rerun after a swapchain resize
void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the clustered-forward froxel buffers (global set 10/11 + light-cull set); init-only
void updateClusterDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the dynamic shadow sets (shadowsetup compute set + shadow geom/sampling set 2); init-only
void updateShadowDescriptorSets(VulkanContext* ctx, RendererState* state);

// Finds available memory types appropriate for a given buffer
uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties);

// Helper function to decrease verbosity of transient command calls
VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx);

// Returns true if the given format has a stencil component
bool hasStencilComponent(VkFormat format);

// Helper function to decrease verbosity of transient command calls, to be used after beginSingleTimeCommands()
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer);

// Copies data from one GPU buffer to another
bool copyBuffer(VulkanContext* ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

// One-shot staged upload: host data -> transient staging buffer -> dstBuffer (device-local)
bool stagingTransfer(VulkanContext* ctx, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize);

// Creates a command buffer
bool createCommandBuffer(VulkanContext* ctx, RendererState* state);

// Creates fences, semaphores, etc.
bool createSyncObjects(VulkanContext* ctx, RendererState* state);

// Frees up memory allocated for monitor info
void cleanupMonitors(Monitors* monitors);

// More Function Prototypes
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount);
const char** getRequiredExtensions(uint32_t* extensionsCount);
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger);

// Cross-file helpers exposed by the instance/ split
struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

#endif
