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
} ImageViewGroup;

typedef struct SwapChainGroup
{
	VkSwapchainKHR swapChain;
	VkFormat imageFormat;
	VkExtent2D imageExtent;
	uint32_t imageCount;
	VkImage* images;
} SwapChainGroup;

typedef struct VulkanComponents // All details of our Vulkan instance
{
	bool enableValidationLayers;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue transferQueue;
    VkQueue presentQueue;
    SwapChainGroup swapChainGroup;
    ImageViewGroup viewGroup;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    FrameBufferGroup framebufferGroup;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer[3];
    VkSemaphore imageAvailableSemaphore[3];
    VkSemaphore renderFinishedSemaphore[3];
    VkFence inFlightFence[3];
    uint32_t frameIndex;
    bool framebufferResized; // Signals window resizing
    uint32_t skipCheck; //Prevents semaphore waits after swapchain changes for the set number of frames
    VkDebugUtilsMessengerEXT debugMessenger;
	uint32_t deviceCount;
	char** availableDevices;
} VulkanComponents;


typedef struct WindowParameters {
    int width;
    int height;
    int monitorIndex;        // Desired monitor index for fullscreen, -1 for windowed
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

extern VulkanSettings vulkanSettings;

struct QueueFamilyIndices // Stores whether different queue families exist, and which queue has been selected for each
{
	bool graphicsPresent;
    uint32_t graphicsFamily;
    bool computePresent;
    uint32_t computeFamily;
    bool transferPresent;
    uint32_t transferFamily;
    bool presentPresent;
    uint32_t presentFamily;
};

typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
} DeviceCapabilities;

struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formatCount;
    VkSurfaceFormatKHR *formats;
    uint32_t presentModesCount;
    VkPresentModeKHR *presentModes;
};


struct VulkanGarbage //All the various stuff that needs to be thrown out
{
	struct VulkanComponents *components;
	GLFWwindow *window;
	Monitors *monitors;
};


#endif
