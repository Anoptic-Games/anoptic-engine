#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include<unistd.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "./graphics/instanceInit.c"


// Variables


struct VulkanGarbage vulkanGarbage = { NULL, NULL}; // THROW OUT WHEN YOU'RE DONE WITH IT

// Function Prototypes

VkResult createInstance(VkInstance *instance);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window);
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);


// Assorted utility functions

void unInitVulkan() // A celebration
{
	if (vulkanGarbage.window)
	{
		glfwDestroyWindow(vulkanGarbage.window);
		glfwTerminate();
	}
	if (vulkanGarbage.components)
	{
		cleanupVulkan(vulkanGarbage.components);
	}
	return;
}

//Init and cleanup functions

VulkanComponents* initVulkan(GLFWwindow* window) // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{
    VulkanComponents* components = (VulkanComponents*) malloc(sizeof(VulkanComponents));
    if(components == NULL) {
        fprintf(stderr, "Failed to allocate memory for Vulkan components!\n");
        return NULL;
    }

    // Initialize Vulkan
    if (createInstance(&(components->instance)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        free(components);
        return NULL;
    }
    vulkanGarbage.components = components;

    // Create a window surface
    if (createSurface(components->instance, window, &(components->surface)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create window surface!\n");
        unInitVulkan();
        return NULL;
    }
    vulkanGarbage.components = components;

    // Pick physical device
    DeviceCapabilities capabilities;
    components->physicalDevice = VK_NULL_HANDLE;
    struct QueueFamilyIndices indices;
    if (!pickPhysicalDevice(components->instance, &(components->physicalDevice), &(components->surface), &capabilities, &indices))
    {
    	fprintf(stderr, "Quitting init: physical device failure!\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;
    
    // Create logical device
    if (createLogicalDevice(components->physicalDevice, &(components->device), &(components->graphicsQueue), &(components->computeQueue), &(components->transferQueue), &(components->presentQueue), &indices) != VK_SUCCESS)
    {
        fprintf(stderr, "Quitting init: logical device failure!\n");
        unInitVulkan();
        return NULL;
    }
    vulkanGarbage.components = components;

    components->swapChainGroup = initSwapChain(components->physicalDevice, components->device, &(components->surface), window); // Initialize a swap chain
    if (components->swapChainGroup.swapChain == NULL)
    {
    	printf("Quitting init: swap chain failure.\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;
    
    components->viewGroup = createImageViews(components->device, components->swapChainGroup);
    if (components->viewGroup.views == NULL)
    {
    	printf("Quitting init: image view failure.\n");
    	unInitVulkan();
    	return NULL;
    }
    vulkanGarbage.components = components;
    
    return components;
}