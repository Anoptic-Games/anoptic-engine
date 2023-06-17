#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include<unistd.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "./structs.h"

// Variables

const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

// Function prototypes

VkResult createInstance(VkInstance *instance);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window);
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);

// Vulkan component initialization functions

GLFWwindow* initWindow() // Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
{
    // Initialize GLFW
    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW!\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);
    
    return window;
}

VkResult createInstance(VkInstance* instance) // Creates a Vulkan instance, selecting and specifying required extensions. It also defines information about our app.
{
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 2);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 3, 2);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Enable validation layers if necessary
    // Here we assume they are not necessary
    createInfo.enabledLayerCount = 0;

    // Enable necessary extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    if (vkCreateInstance(&createInfo, NULL, instance) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

	// Query extensions
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    VkExtensionProperties extensions[extensionCount];
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);

    /*for (uint32_t i = 0; i < extensionCount; i++)
    {
    	printf("%s\n", extensions[i].extensionName);
    }*/

    return VK_SUCCESS;
}

VkResult createSurface(VkInstance instance, GLFWwindow* window, VkSurfaceKHR* surface) // Creates a window surface using GLFW, for our Vulkan instance to draw to.
{
    if (glfwCreateWindowSurface(instance, window, NULL, surface) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create window surface!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface) { // Extend with more queue family checks as they become relevant
    struct QueueFamilyIndices indices = {};
    indices.graphicsPresent = false;
    indices.computePresent = false;
    indices.transferPresent = false;
    indices.presentPresent = false;

    indices.graphicsFamily = 0;
    indices.computeFamily = 0;
    indices.transferFamily = 0;
    indices.presentFamily = 0;
    
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
	VkQueueFamilyProperties queueFamilies[queueFamilyCount];
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

		// For now, we're selecting only the first queue family that satisfies each capability. This is to ensure the same family is used between operations whenever possible, for performance reasons.
		// This may be changed in the future, specifically to allow compute tasks to work on the next frame without impacting rendering.
		// We might also add some extra logic to determine if any queue supports async transfers. If such, we could enable a dedicated transfer queue to further improve concurrency.
		//!TODO Implement these as required further into development
	for (uint32_t i = 0; i < queueFamilyCount; i++)
	{	//Queue checks go here
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && indices.graphicsPresent != true)
		{
			indices.graphicsFamily = i;
			indices.graphicsPresent = true;
			//printf("Graphics: %d\n", i);
		}
		if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)&& indices.computePresent != true)
		{
			indices.computeFamily = i;
			indices.computePresent = true;
			//printf("Compute: %d\n", i);
		}
		if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)&& indices.transferPresent != true)
		{
			indices.transferFamily = i;
			indices.transferPresent = true;
			//printf("Transfer: %d\n", i);
		}

		if (surface != NULL)
		{
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, *surface, &presentSupport);	
			if (presentSupport) 
			{
				if (indices.presentPresent != true) // Makes sure the primary present family gets selected, usually the same family as for graphics
				{
					indices.presentFamily = i;	
				}
				indices.presentPresent = true;
				//printf("Present: %d\n", indices.presentFamily);
			}
		}
	}
		
    return indices;
}

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device, VkPhysicalDeviceFeatures deviceFeatures)
{
	struct DeviceCapabilities capabilities;
	//Device features checks
	capabilities.float64 = deviceFeatures.shaderFloat64;
	capabilities.int64 = deviceFeatures.shaderInt64;
	//Queue family checks
	struct QueueFamilyIndices indices = findQueueFamilies(device, NULL);
	capabilities.graphics = indices.graphicsPresent;
	capabilities.compute = indices.computePresent;
	capabilities.transfer = indices.transferPresent;
	return capabilities;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, /* Other required extensions... */ };
    size_t requiredExtensionsCount = sizeof(requiredExtensions) / sizeof(requiredExtensions[0]);
    
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

    VkExtensionProperties* availableExtensions = (VkExtensionProperties*) malloc(extensionCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    for (size_t i = 0; i < requiredExtensionsCount; ++i) 
    {
        bool found = false;
        for (uint32_t j = 0; j < extensionCount; ++j) 
        {
            if (strcmp(requiredExtensions[i], availableExtensions[j].extensionName) == 0) 
            {
                found = true;
                break;
            }
        }

        if (!found) 
        {
            free(availableExtensions);
            return false; // Required extension not found
        }
    }

    free(availableExtensions);
    return true; // All required extensions found
}

bool isDeviceSuitable(VkPhysicalDevice device, VkPhysicalDeviceFeatures deviceFeatures, VkSurfaceKHR *surface)
{
	struct QueueFamilyIndices indices = findQueueFamilies(device, surface);
	// Add any features as they become necessary
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	//Since the GPU we're selecting is already the most performant one by default, there's no point selecting Discrete only
	bool physicalRequirements = deviceFeatures.geometryShader && deviceFeatures.shaderFloat64 && deviceFeatures.shaderInt64;
	bool queueRequirements = indices.graphicsPresent;
	return physicalRequirements && queueRequirements && extensionsSupported;
}

bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice* physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices) // Queries all available devices, and selects one according to our needs.
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

    if (deviceCount == 0) 
    {
        fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
        return false;
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    VkDeviceSize maxMemorySize = 0;

    for (uint32_t i = 0; i < deviceCount; i++) // Iterates through available devices and selects the one with the most VRAM.
    {
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
        vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);

        if (deviceProperties.limits.bufferImageGranularity > maxMemorySize) 
        {
            maxMemorySize = deviceProperties.limits.bufferImageGranularity;
	        if (isDeviceSuitable(devices[i], deviceFeatures, surface) == true) 
	        {
	        	*physicalDevice = devices[i];
	        	*capabilities = populateCapabilities(devices[i], deviceFeatures);
	        	*indices = findQueueFamilies(devices[i], surface);
	        }
        }
    }

    printf("Graphics family: %d\nCompute family: %d\nTransfer family: %d\nPresent family: %d\n", (indices->graphicsFamily), (indices->computeFamily), (indices->transferFamily), (indices->presentFamily));

    if (*physicalDevice == VK_NULL_HANDLE) 
    {
        fprintf(stderr, "Failed to find a suitable GPU!\n");
        return false;
    }
    printf("%p\n", physicalDevice);

    free(devices);
    return true;
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices) // Creates a logical device that can actually do stuff.
{
    // Device features (optional)
    VkPhysicalDeviceFeatures availableFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &availableFeatures);
    VkPhysicalDeviceFeatures deviceFeatures = {.shaderInt64 = availableFeatures.shaderInt64, .shaderFloat64 = availableFeatures.shaderFloat64}; // Add more features as required

	// We'll have 4 unique queues at the very most
	VkDeviceQueueCreateInfo queueCreateInfos[4];
	uint32_t uniqueQueueFamilies[4] = {indices->graphicsFamily, indices->presentFamily, indices->computeFamily, indices->transferFamily};
	uint32_t queueCount = 0;
	
	for (uint32_t i = 0; i < 4; i++)
	{
	    bool uniqueFamily = true;
	    // Check whether we already have a queue set up for the specific family index
	    for (uint32_t x = 0; x < i; x++)
	    {
	        if (uniqueQueueFamilies[i] == uniqueQueueFamilies[x])
	        {
	            uniqueFamily = false;
	            break;
	        }
	    }
	    if (uniqueFamily == true)
	    {
	        VkDeviceQueueCreateInfo queueCreateInfo = {};
	        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	        queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
	        queueCreateInfo.queueCount = 1;
	        float queuePriority = 1.0f;
	        queueCreateInfo.pQueuePriorities = &queuePriority;
	        queueCreateInfos[queueCount] = queueCreateInfo;
	        queueCount++;
	    }
	}
	
	// creating the device
	VkDeviceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = queueCount;
	createInfo.pQueueCreateInfos = queueCreateInfos;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = 1; // Replace if more extensions are added
	printf("Required extensions: %s\n", requiredExtensions[0]);
	createInfo.ppEnabledExtensionNames = requiredExtensions;

    if (vkCreateDevice(physicalDevice, &createInfo, NULL, device) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create logical device!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
		
	// get queue handles
	vkGetDeviceQueue(*device, indices->graphicsFamily, 0, graphicsQueue);
	vkGetDeviceQueue(*device, indices->presentFamily, 0, presentQueue);
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->computeFamily, 0, computeQueue);
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->transferFamily, 0, transferQueue);
	}

    return VK_SUCCESS;
}

struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface) // Retrieves a record of all available swap chains and their capabilities
{
    struct SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, *surface, &details.capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &details.formatCount, NULL);

    details.formats = (VkSurfaceFormatKHR*) malloc(details.formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &details.formatCount, details.formats);

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &details.presentModesCount, NULL);

    details.presentModes = (VkPresentModeKHR*) malloc(details.presentModesCount * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &details.presentModesCount, details.presentModes);
        
    return details;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window) 
{
	    if (capabilities.currentExtent.width != UINT32_MAX)
	    {
	    	return capabilities.currentExtent;
	    }
	    else
	    {
	    	int width, height;
	    	glfwGetFramebufferSize(window, &width, &height);
	        VkExtent2D actualExtent = {
            	(uint32_t)(width),
            	(uint32_t)(height)
        	};
        	// Clamp our render dimensions between the min and max values
        	actualExtent.width = (actualExtent.width < capabilities.minImageExtent.width) ? capabilities.minImageExtent.width : actualExtent.width;
        	actualExtent.width = (actualExtent.width > capabilities.maxImageExtent.width) ? capabilities.maxImageExtent.width : actualExtent.width;
        	actualExtent.height = (actualExtent.height < capabilities.minImageExtent.height) ? capabilities.minImageExtent.height : actualExtent.height;
        	actualExtent.height = (actualExtent.height > capabilities.maxImageExtent.height) ? capabilities.maxImageExtent.height : actualExtent.height;
        	return actualExtent;
	    }
}

SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window) // Selects and initializes a swap chain
{
	struct SwapChainSupportDetails details;
    details = querySwapChainSupport(device, surface);


    // TODO: Select the best format and present mode

    //Pick a format
	VkSurfaceFormatKHR chosenFormat = { .format = 0, .colorSpace = 0};
    for (uint32_t i = 0; i < details.formatCount; i++)
    {
    	if ((details.formats+i)->format == VK_FORMAT_B8G8R8A8_SRGB && (details.formats+i)->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosenFormat = *(details.formats+i);
		}
    }
    if (chosenFormat.format == 0 && chosenFormat.colorSpace == 0)
    {
    	printf("Failed to select an appropriate image format!\n");
        SwapChainGroup failure = {NULL};
        return failure;
    }

    //Pick a present mode
    VkPresentModeKHR chosenPresentMode;
    uint32_t presentModePresent = 0; // Order of priority for our desired present mode
    for (uint32_t i = 0; i < details.presentModesCount; i++)
    {
    	if (*(details.presentModes+i) == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			chosenPresentMode = *(details.presentModes+i);
			presentModePresent = 4; // highest priority
		}
    	if (*(details.presentModes+i) == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
		{
			if (presentModePresent <3)
			{
				chosenPresentMode = *(details.presentModes+i);
				presentModePresent = 3;
			}
		}
    	if (*(details.presentModes+i) == VK_PRESENT_MODE_FIFO_KHR)
		{
			if (presentModePresent <2)
			{
				chosenPresentMode = *(details.presentModes+i);
				presentModePresent = 2;
			}
		}
    	if (*(details.presentModes+i) == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			if (presentModePresent <1)
			{
				chosenPresentMode = *(details.presentModes+i);
				presentModePresent = 1;
			}
		}
    }
    if (presentModePresent == 0)
    {
    	printf("Failed to select an appropriate present mode!\n");
        SwapChainGroup failure = {NULL};
        return failure;
    }
    printf("Present mode:%d\n", chosenPresentMode);

    // TODO: Initialize the swap chain
    VkExtent2D chosenExtent = chooseSwapExtent(details.capabilities, window);
    uint32_t imageCount = details.capabilities.minImageCount;
    imageCount = (imageCount > 2) ? imageCount : 2; // Two images is more than plenty
    imageCount = (imageCount < details.capabilities.maxImageCount) ? imageCount : details.capabilities.maxImageCount; // this should NEVER happen

    // Now we finally create the swap chain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = *surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = chosenExtent;
    createInfo.imageArrayLayers = 1; // We do NOT support VR
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    struct QueueFamilyIndices indices = findQueueFamilies(device, surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = NULL; // Optional
    }
    createInfo.preTransform = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    
    VkSwapchainKHR swapChain;
    if (vkCreateSwapchainKHR(logicalDevice, &createInfo, NULL, &swapChain) != VK_SUCCESS) {
        printf("Failed to create swap chain!\n");
        SwapChainGroup failure = {NULL};
        return failure;
    }

    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, NULL);
    VkImage *swapChainImages = (VkImage *) malloc(sizeof(VkImage) * imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, swapChainImages);

	SwapChainGroup swapChainGroup = {swapChain, chosenFormat.format, chosenExtent, imageCount, swapChainImages};
    
    return swapChainGroup;
}


ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup)
{
	ImageViewGroup viewGroup = {0, NULL};
	viewGroup.viewCount = imageGroup.imageCount;
	viewGroup.views = (VkImageView *) malloc(sizeof(VkImageView) * viewGroup.viewCount);
	for (uint32_t i = 0; i < imageGroup.imageCount; i++)
	{
		VkImageViewCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = imageGroup.images[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = imageGroup.imageFormat;
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &createInfo, NULL, &viewGroup.views[i]) != VK_SUCCESS) {
		    printf("Failed to create image views!\n");
		    viewGroup.views = NULL; // We'll use this to check for errors
		    return viewGroup;
		}
		
	}
	return viewGroup;
}

void cleanupVulkan(VulkanComponents* components) // Frees up the previously initialized Vulkan parameters
{
    if (components == NULL) {
        return;
    }
    
    if (components->viewGroup.views != NULL)
    {
    	for (uint32_t i = 0; i < components->viewGroup.viewCount; i++)
    	{
    		vkDestroyImageView(components->device, components->viewGroup.views[i], NULL);
    	}
    	free(components->viewGroup.views);
    }
    
    SwapChainGroup failure = {NULL};
    if (components->swapChainGroup.swapChain != failure.swapChain) // !TODO This is not causing a segfault and I have no idea why.
    {
    	vkDestroySwapchainKHR(components->device, components->swapChainGroup.swapChain, NULL);
    	free(components->swapChainGroup.images);
    }

    if (components->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(components->device);
        vkDestroyDevice(components->device, NULL);
    }

    if (components->instance != VK_NULL_HANDLE) {
        if (components->surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(components->instance, components->surface, NULL);
        }
        vkDestroyInstance(components->instance, NULL);
    }
    // Add cleanup steps for swap chain group
    if (components->physicalDevice != NULL)
    {
    	free(components->physicalDevice);
    }

    free(components);
}