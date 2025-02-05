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

typedef struct FrameBufferGroup
{
	uint32_t bufferCount;
	VkFramebuffer* buffers;
} FrameBufferGroup;


typedef struct ImageViewGroup
{
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
{
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
{
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
	VkFormat depthFormat;
	VkImage depth[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory depthMemory[MAX_FRAMES_IN_FLIGHT];
	VkImageView depthView[MAX_FRAMES_IN_FLIGHT];
} BufferComponents;


typedef struct RenderComponents
{
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSetLayout meshDescriptorSetLayout; // Move to per-mesh-type struct
	VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];
	VkDescriptorPool descriptorPool;
	VkDescriptorPool meshDescriptorPool;
	UniformComponents uniform;
    VkPipeline graphicsPipeline;
	VkSampler textureSampler;
	BufferComponents buffers;
} RenderComponents;

typedef struct SynchronizationComponents
{
    VkSemaphore imageAvailableSemaphore[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphore[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFence[MAX_FRAMES_IN_FLIGHT];
	bool frameSubmitted[MAX_FRAMES_IN_FLIGHT]; // Used to keep track of which frames have been used, mitigates resize crash
    uint32_t frameIndex;
    bool framebufferResized;
    uint32_t skipCheck;
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
{
	uint32_t sampler;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
} GlyphTexture;


#endif
