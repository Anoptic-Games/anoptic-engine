/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/vertex/vertex.h"

#define MAX_FRAMES_IN_FLIGHT 3

// Structs

// New structs for streamlined state resource management

//!TODO investigate which methods are really required and practical
// Virtual table type with common operations.

/*
typedef struct VulkanResourceVTable
{
    // Invalidate this resource (e.g. mark its cached state as stale).
    void (*invalidate)(VulkanResource *self);
    // Update or re-create the resource if needed.
    void (*update)(VulkanResource *self);
    // Clean up and free any allocated memory.
    void (*destroy)(VulkanResource *self);
    // (Other operations can be added as needed.)
} VulkanResourceVTable;

// Base wrapper struct.
struct VulkanResource
{
    VulkanResourceVTable *vtable;  // pointer to virtual methods
    // Forward dependency buffer:
    VulkanResource **dependencies; // dynamic array (buffer) of dependents
    size_t dependencyCount;        // number of dependents registered
    size_t dependencyCapacity;     // capacity of the dependency array
    // Underlying Vulkan handle (for example, a VkBuffer, VkImage, etc.)
    void *vkHandle;
    // Other state data (cache info, flags, etc.)
};
*/

//!NOTE We may defer destruction of out-of-date resources to preserve frame timing
//!NOTE Dependencies may be tracked via cyclic pointers, allowing invalidated resources to update upstream dependency entries

// New struct for per-frame images
typedef struct FrameImageGroup
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory imageMemory; // This won't be used for the final present images, memory is managed by the swapchain
} FrameImageGroup;

typedef struct FrameBufferGroup
{
	uint32_t bufferCount;
	VkFramebuffer* buffers; // Necessary for the swapchain
} FrameBufferGroup;


typedef struct ImageViewGroup
{ // Swapchain image views, should be next to the images and memory
	uint32_t viewCount;
	VkImageView* views;
	VkImageView colorView;
    VkImageView uiView;
} ImageViewGroup;

typedef struct SwapChainGroup
{
	VkSwapchainKHR swapChain;
	VkFormat imageFormat;
	VkExtent2D imageExtent;
	uint32_t imageCount;
	VkImage* images;
	VkDeviceMemory imageMemory[MAX_FRAMES_IN_FLIGHT]; // Not actually used, swapchain image memory managed by Vulkan.
	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
    VkImage uiImage;
    VkDeviceMemory uiImageMemory;
} SwapChainGroup;

typedef struct InstanceDebugComponents
{
    bool enableValidationLayers;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
} InstanceDebugComponents;

typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
} DeviceCapabilities;

typedef struct QueueFamilyIndices // Stores whether different queue families exist, and which queue has been selected for each
{
	bool graphicsPresent;
    uint32_t graphicsFamily;
    bool computePresent;
    uint32_t computeFamily;
    bool transferPresent;
    uint32_t transferFamily;
    bool presentPresent;
    uint32_t presentFamily;
} QueueFamilyIndices;

typedef struct PhysicalDeviceComponents
{
    uint32_t deviceCount;
    char** availableDevices;
    VkPhysicalDevice physicalDevice;
    DeviceCapabilities deviceCapabilities;
    QueueFamilyIndices queueFamilyIndices;
	VkSampleCountFlagBits msaaSamples;
} PhysicalDeviceComponents;

typedef struct DeviceQueueComponents
{ // Necessary for just about every operation and pipeline, may need re-formatting if we go with multiple queues of one type
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue transferQueue;
    VkQueue presentQueue;
} DeviceQueueComponents;

typedef struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formatCount;
    VkSurfaceFormatKHR *formats;
    uint32_t presentModesCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef struct SwapChainComponents
{
    SwapChainGroup swapChainGroup;
    ImageViewGroup viewGroup;
    FrameBufferGroup framebufferGroup;
    SwapChainSupportDetails swapChainSupportDetails;
} SwapChainComponents;

typedef struct EntityBuffer
{ // To be extended with animation data
    VkBuffer vertex;
    VkDeviceMemory vertexMemory;
	uint32_t indexCount;
    VkBuffer index;
    VkDeviceMemory indexMemory;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkDescriptorSet meshDescriptorSet;
	// Transformation data
	VkBuffer transform;
	VkDeviceMemory transformMemory;
	void* transformMapped;
} EntityBuffer;

typedef struct BufferComponents 
{
	EntityBuffer* entities;
	uint32_t entityCount;
	VkBuffer uniform[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory uniformMemory[MAX_FRAMES_IN_FLIGHT];
	void* uniformMapped[MAX_FRAMES_IN_FLIGHT];
	VkFormat depthFormat; // All depth resources should live next to the swapchain stuff
	VkImage depth[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory depthMemory[MAX_FRAMES_IN_FLIGHT];
	VkImageView depthView[MAX_FRAMES_IN_FLIGHT];
} BufferComponents;


typedef struct RenderComponents
{
    VkRenderPass renderPass; // We'll have multiples of these three
    VkPipelineLayout pipelineLayout;
	VkDescriptorSetLayout descriptorSetLayout; // This is only for the UBO, swapchain-adjacent rendering
	VkDescriptorSetLayout meshDescriptorSetLayout; // Move to per-mesh-type struct
	VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT]; // These descriptors deal with scene-wide parameters, move to swapchain
	VkDescriptorPool descriptorPool;
	VkDescriptorPool meshDescriptorPool;
	UniformComponents uniform; // This comes from vertex.h, should probably have a buffer of length n = swap count, move to swapchain
    VkPipeline graphicsPipeline; // We'll have many of these
	VkSampler textureSampler;   // Also many of these, maybe create whole struct for resource access formats
	BufferComponents buffers; // This entire thing should probably be moved to swapchain
} RenderComponents;

typedef struct SynchronizationComponents
{
    VkSemaphore imageAvailableSemaphore[MAX_FRAMES_IN_FLIGHT]; // All frame sync objects should also be in swapchain, they're per-frame
    VkSemaphore renderFinishedSemaphore[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFence[MAX_FRAMES_IN_FLIGHT];
	bool frameSubmitted[MAX_FRAMES_IN_FLIGHT]; // Used to keep track of which frames have been used, mitigates resize crash | Probably outdated, keeping for now
    uint32_t frameIndex; // Move to swapchain
    uint32_t imageIndex; // Used to track submitted frames for presentation, move to swapchain
    bool framebufferResized; // Swapchain
    uint32_t skipCheck; // Almost certainly out of date
} SynchronizationComponents;

typedef struct CommandComponents
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer[MAX_FRAMES_IN_FLIGHT];
} CommandComponents;

typedef struct VulkanComponents
{
    InstanceDebugComponents instanceDebug;
    PhysicalDeviceComponents physicalDeviceComp;
    DeviceQueueComponents deviceQueueComp;
    SwapChainComponents swapChainComp;
    RenderComponents renderComp;
    SynchronizationComponents syncComp;
    CommandComponents cmdComp;
    VkSurfaceKHR surface;
} VulkanComponents;

typedef struct Dimensions2D
{
	uint32_t width;
	uint32_t height;
} Dimensions2D;

typedef struct WindowParameters
{
    uint32_t width;
    uint32_t height;
    uint32_t monitorIndex;        // Desired monitor index for fullscreen, -1 for windowed
    bool borderless;         // True for borderless, false otherwise
    // ... other parameters
} WindowParameters;

typedef struct VulkanSettings
{
	char* preferredDevice; // Physical GPU to use for rendering
	uint32_t preferredMode;	// Frame present mode
} VulkanSettings;

typedef struct MonitorInfo 
{
    const GLFWvidmode* modes;    // Video modes supported by the monitor
    int modeCount;               // Number of video modes supported
} MonitorInfo;

typedef struct Monitors 
{
    MonitorInfo* monitorInfos;   // Array of MonitorInfo for each monitor
    int monitorCount;           // Total number of monitors
} Monitors;


struct VulkanGarbage //All the various stuff that needs to be thrown out
{
	struct VulkanComponents *components;
	GLFWwindow *window;
	Monitors *monitors;
};

typedef struct GlyphTexture
{ // ??? This must be doog's doing
	uint32_t sampler;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
} GlyphTexture;


#endif
