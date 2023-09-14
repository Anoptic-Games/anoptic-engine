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
} VulkanComponents;


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
};


#endif