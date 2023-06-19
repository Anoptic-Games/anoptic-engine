#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include<unistd.h>

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
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
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
