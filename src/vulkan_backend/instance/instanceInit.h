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

/* Function Interfaces */

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
// Failure answers VK_NULL_HANDLE, never an indeterminate handle 〜 callers check before storing it.
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

// Swapchain colour views, one per presentable image. false leaves views NULL and viewCount 0.
[[nodiscard]] bool createImageViews(VulkanContext* ctx, RendererState* state);

// Creates a command pool
bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool);

// Generic function for data buffer creation, updates buffer and bufferMemory with the created addresses.
// Total out-params: false leaves *buffer VK_NULL_HANDLE and *allocation zeroed, never indeterminate.
[[nodiscard]] bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation);

// createDataBuffer with gfx+compute CONCURRENT sharing when computeShared, EXCLUSIVE otherwise.
// computeShared demands two distinct queue families, i.e. the asyncLc gate.
[[nodiscard]] bool createDataBufferShared(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, bool computeShared, VkBuffer* buffer, GpuAllocation* allocation);

// Creates uniform buffers for each frame
bool createUniformBuffers(VulkanContext* ctx, RendererState* state);

// updateUniformBuffer declared in vulkan_backend/frame/frame.h

// Updates a mesh's transform matrices
bool updateMeshTransforms(VulkanContext* ctx, RenderEntity* entity, float move);

// Creates the MSAA colour/picking-id draw targets + their per-frame resolve targets.
// false at the first refused image, view or transition; no dead handle is published.
[[nodiscard]] bool createColorResourcesChecked(VulkanContext* ctx);

// createColorResourcesChecked with the status dropped.
void createColorResources(VulkanContext* ctx);

// Creates a depth image and view for the current swapchain.
// The status is the only failure channel: frame 0 / view 0 is filled first, so no field of the
// published state distinguishes a later frame's refusal.
[[nodiscard]] bool createDepthResources(VulkanContext* ctx, RendererState* state);

// Per-view half-res R32F depth pyramids, recreated with the swapchain
bool createHiZResources(VulkanContext* ctx, RendererState* state);
void updateHiZDescriptorSets(VulkanContext* ctx, RendererState* state);

// Creates the descriptor pool
bool createDescriptorPool(VulkanContext* ctx, RendererState* state);

bool createBindlessTextureArray(VulkanContext* ctx, RendererState* state);

// Creates the descriptor sets
bool createDescriptorSets(VulkanContext* ctx, RendererState* state);

// Points every set at the current scene buffers; sole re-point path for the per-frame TransformSSBO
// (global/view, cull, update, scatter, lightsetup and shadowsetup binding 1), so entity growth needs one call
void updateUboDescriptorSets(VulkanContext* ctx, RendererState* state);

// (Re)binds each frame's tonemap set to its HDR resolve view; rerun after a swapchain resize
void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the clustered-forward froxel buffers (global set 10/11 + light-cull set); init-only
void updateClusterDescriptorSets(VulkanContext* ctx, RendererState* state);

// Binds the dynamic shadow sets (shadowsetup compute set + shadow geom/sampling set 2); init-only.
// Must run after updateUboDescriptorSets, which owns re-pointing setupSet binding 1 on entity growth
void updateShadowDescriptorSets(VulkanContext* ctx, RendererState* state);

// Finds available memory types appropriate for a given buffer
uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties);

// Helper function to decrease verbosity of transient command calls.
// VK_NULL_HANDLE on failure: nothing is allocated on that arm, and it must not be recorded into.
VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx);

// Returns true if the given format has a stencil component
bool hasStencilComponent(VkFormat format);

// End + submit transient CB started by beginSingleTimeCommands(); false if any step failed.
// The CB and its fence are discharged on every arm.
[[nodiscard]] bool endSingleTimeCommandsChecked(VulkanContext* ctx, VkCommandBuffer commandBuffer);

// endSingleTimeCommandsChecked with the status dropped.
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer);

// Copies data from one GPU buffer to another. False if the one-shot submit never completed.
[[nodiscard]] bool copyBuffer(VulkanContext* ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

// One-shot staged upload: host data -> transient staging buffer -> dstBuffer (device-local).
// False means the copy never reached the destination; the staging buffer is discharged either way.
[[nodiscard]] bool stagingTransfer(VulkanContext* ctx, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize);

// Creates a command buffer
bool createCommandBuffer(VulkanContext* ctx, RendererState* state);

// Creates fences, semaphores, etc.
bool createSyncObjects(VulkanContext* ctx, RendererState* state);

// Frees up memory allocated for monitor info
void cleanupMonitors(Monitors* monitors);

/* More Function Prototypes */

struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount);
const char** getRequiredExtensions(uint32_t* extensionsCount);
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger);

/* Cross-File Helpers */

// Exposed by the instance/ split.
struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

#endif
