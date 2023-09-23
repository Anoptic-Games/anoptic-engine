/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */


#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>


// TODO: Figure out why this is here AND in main
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/structs.h"

#include "vulkan_backend/pipeline.h"


// Variables

const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

// Function prototypes

VkResult createInstance(VkInstance *instance, VkDebugUtilsMessengerEXT *debugMessenger);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
bool pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, VkSurfaceKHR *surface, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices);
struct SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR *surface);
SwapChainGroup initSwapChain(VkPhysicalDevice device, VkDevice logicalDevice, VkSurfaceKHR *surface, GLFWwindow* window);
ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup);
bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount);
const char** getRequiredExtensions(uint32_t* extensionsCount);
void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger);
bool createFramebuffers(VkDevice device, FrameBufferGroup* frameBufferGroup, ImageViewGroup viewGroup, SwapChainGroup swapGroup, VkRenderPass renderPass);
void cleanupVulkan(VulkanComponents* components);
static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

// Vulkan component initialization functions

GLFWwindow* initWindow(VulkanComponents* components) // Initializes a pointer to a GLFW window, returns a window pointer or NULL on failure
{
    // Initialize GLFW
    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW!\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);
    glfwSetWindowUserPointer(window, components);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    return window;
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
	VulkanComponents* components = glfwGetWindowUserPointer(window);
	components->framebufferResized = true;
}

VkResult createInstance(VkInstance* instance, VkDebugUtilsMessengerEXT *debugMessenger) // Creates a Vulkan instance, selecting and specifying required extensions. It also defines information about our app.
{
	const char* validationLayers[] = 
	{ //!TODO get this thing out of here as soon as we have a config parser
	    "VK_LAYER_KHRONOS_validation"
	};
	size_t validationCount = sizeof(validationLayers) / sizeof(validationLayers[0]);
	

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
    uint32_t extensionCount = 0;
    const char** extensions;
    extensions = getRequiredExtensions(&extensionCount);
    
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

	createInfo.enabledLayerCount = 0;
	createInfo.pNext = NULL;
	
	#ifdef DEBUG_BUILD
    if (!checkValidationLayerSupport(validationLayers, validationCount))
    {
    	printf("Validation layers requested, but not available!\n");
    }
    else
    {
    	createInfo.enabledLayerCount = (uint32_t) validationCount;
    	createInfo.ppEnabledLayerNames = validationLayers;
    	populateDebugMessengerCreateInfo(&debugCreateInfo);
    	createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    	printf("Enabled validation layers!\n");
    }
	#endif

    if (vkCreateInstance(&createInfo, NULL, instance) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        free(extensions);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    #ifdef DEBUG_BUILD
    	setupDebugMessenger(instance, debugMessenger);
    #endif

	// Query extensions
	// Completely forgot what the following block is for, probably nothing important
	/*
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    VkExtensionProperties extensions[extensionCount];
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);*/

    /*for (uint32_t i = 0; i < extensionCount; i++)
    {
    	printf("%s\n", extensions[i].extensionName);
    }*/

    free(extensions);

    return VK_SUCCESS;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
{

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		return VK_FALSE;
	}
    printf("validation layer: %s\n", pCallbackData->pMessage);

    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) 
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL) 
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo) 
{
    createInfo->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo->pfnUserCallback = debugCallback;
}

void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger) 
{
	#ifndef DEBUG_BUILD
    return;
	#endif
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
	populateDebugMessengerCreateInfo(&createInfo);
	if (CreateDebugUtilsMessengerEXT(*instance, &createInfo, NULL, debugMessenger) != VK_SUCCESS)
	{
		printf("Failed to set up debug messenger!\n");
	}
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) 
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL) 
    {
        func(instance, debugMessenger, pAllocator);
    }
}



const char** getRequiredExtensions(uint32_t* extensionsCount)
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    uint32_t totalExtensionCount = glfwExtensionCount;

    #ifdef DEBUG_BUILD
    totalExtensionCount += 1;
    #endif

    const char** extensions = calloc(totalExtensionCount, sizeof(char*));

    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        extensions[i] = _strdup(glfwExtensions[i]);
    }

    #ifdef DEBUG_BUILD
    extensions[glfwExtensionCount] = strdup(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif

    *extensionsCount = totalExtensionCount;

    return extensions;
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
    //!TODO add error-case returns and associated checking to all invoking functions
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

    VkExtensionProperties* availableExtensions = (VkExtensionProperties*) calloc(1, extensionCount * sizeof(VkExtensionProperties));
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

    VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(1, sizeof(VkPhysicalDevice) * deviceCount);
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

    details.formats = (VkSurfaceFormatKHR*) calloc(1, details.formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &details.formatCount, details.formats);

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &details.presentModesCount, NULL);

    details.presentModes = (VkPresentModeKHR*) calloc(1, details.presentModesCount * sizeof(VkPresentModeKHR));
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
            break;
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
    //printf("Present mode:%d\n", chosenPresentMode);

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
    createInfo.imageArrayLayers = 1; // We DO support VR Half Life Alyx 2
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
    VkResult result = vkCreateSwapchainKHR(logicalDevice, &createInfo, NULL, &swapChain);
    if ( result != VK_SUCCESS) {
        printf("Failed to create swap chain - error code %d!\n", result);
        SwapChainGroup failure = {NULL};
        return failure;
    }

    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, NULL);
    VkImage *swapChainImages = (VkImage *) calloc(imageCount, sizeof(VkImage));
    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, swapChainImages);

	SwapChainGroup swapChainGroup = {swapChain, chosenFormat.format, chosenExtent, imageCount, swapChainImages};
    
    return swapChainGroup;
}

void cleanupSwapChain(VkDevice device, SwapChainGroup* swapGroup, FrameBufferGroup* frameGroup, ImageViewGroup* viewGroup)
{
    for (size_t i = 0; i < frameGroup->bufferCount; i++) 
    {
        vkDestroyFramebuffer(device, frameGroup->buffers[i], NULL);
    }

    for (size_t i = 0; i < viewGroup->viewCount; i++) 
    {
        vkDestroyImageView(device, viewGroup->views[i], NULL);
    }

    vkDestroySwapchainKHR(device, swapGroup->swapChain, NULL);
}

void recreateSwapChain(VulkanComponents* components, GLFWwindow* window)
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        printf("Sleeping!\n");
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    if (!components->skipCheck)
    {
        for (uint32_t i = 0; i < 3; i++) 
        {
            vkWaitForFences(components->device, 1, &(components->inFlightFence[i]), VK_TRUE, UINT64_MAX);
        }
    }


    cleanupSwapChain(components->device, &(components->swapChainGroup), &(components->framebufferGroup), &(components->viewGroup));
	SwapChainGroup failure = {NULL};
	components->swapChainGroup = initSwapChain(components->physicalDevice, components->device, &(components->surface), window);
    if(components->swapChainGroup.swapChain == failure.swapChain)
	{
		printf("Swap chain re-creation error, exiting!\n");
		cleanupVulkan(components);
		exit(1);
	}
    components->viewGroup = createImageViews(components->device, components->swapChainGroup);
    if (components->viewGroup.views == NULL)
    {
    	printf("View group re-creation error, exiting!\n");
    	cleanupVulkan(components);
    	exit(1);
    }
    if (createFramebuffers(components->device, &(components->framebufferGroup), components->viewGroup, components->swapChainGroup, components->renderPass) != true)
    {
    	printf("Framebuffer re-creation error, exiting!\n");
    	cleanupVulkan(components);
    	exit(1);
    }

    vkResetCommandPool(components->device, components->commandPool, 0);
    for (int i = 0; i < 3; i++) // Clear fences prior to resuming render
    {
        vkResetFences(components->device, 1, &(components->inFlightFence[i]));
    }
    components->skipCheck = 3; // Skip semaphore waits 

    components->framebufferResized = false;

}


ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup)
{
	ImageViewGroup viewGroup = {0, NULL};
	viewGroup.viewCount = imageGroup.imageCount;
	viewGroup.views = (VkImageView *) calloc(1, sizeof(VkImageView) * viewGroup.viewCount);
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

bool createFramebuffers(VkDevice device, FrameBufferGroup* frameBufferGroup, ImageViewGroup viewGroup, SwapChainGroup swapGroup, VkRenderPass renderPass)
{
    frameBufferGroup->bufferCount = viewGroup.viewCount;
    frameBufferGroup->buffers = (VkFramebuffer*) calloc(1, sizeof(VkFramebuffer) * frameBufferGroup->bufferCount);

    for (uint32_t i = 0; i < viewGroup.viewCount; i++) 
    {
        VkImageView attachments[] = 
        {
            viewGroup.views[i]
        };
    
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapGroup.imageExtent.width;
        framebufferInfo.height = swapGroup.imageExtent.height;
        framebufferInfo.layers = 1;
    
        if (vkCreateFramebuffer(device, &framebufferInfo, NULL, (frameBufferGroup->buffers+i)) != VK_SUCCESS) 
        {
            printf("Failed to create framebuffer!\n");
            return false;
        }
    }
    return true;
}

bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool)
{
	struct QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, &surface);
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	
	if (vkCreateCommandPool(device, &poolInfo, NULL, commandPool) != VK_SUCCESS) 
	{
	    printf("Failed to create command pool!\n");
	    return false;
	}
	return true;
}

bool createCommandBuffer(VulkanComponents* components)
{
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = components->commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

    for (uint32_t i =0; i<3; i++)
    {
        if (vkAllocateCommandBuffers(components->device, &allocInfo, &(components->commandBuffer[i])) != VK_SUCCESS) 
	    {
	        printf("Failed to allocate command buffers!\n");
	        return false;
	    }
    }

	return true;
}

bool createSyncObjects(VulkanComponents* components) 
{
    for (uint32_t i = 0; i<3; i++)
    {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;  

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(components->device, &semaphoreInfo, NULL, &(components->imageAvailableSemaphore[i])) != VK_SUCCESS ||
            vkCreateSemaphore(components->device, &semaphoreInfo, NULL, &(components->renderFinishedSemaphore[i])) != VK_SUCCESS ||
            vkCreateFence(components->device, &fenceInfo, NULL, &(components->inFlightFence[i])) != VK_SUCCESS) {
            printf("Failed to create semaphores!\n");
            return false;
        }
    }

    return true;
}

bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount)
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);

    VkLayerProperties availableLayers[layerCount];
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    uint32_t availableLayerCount = sizeof(availableLayers) / sizeof(availableLayers[0]);

    for (uint32_t z = 0; z < availableLayerCount; z++) 
    {
     printf("Layer %d: %s\n", z, availableLayers[z].layerName);
    }

    for(size_t i = 0; i < validationCount; i++) 
    {
        bool layerFound = false;

    	
        for (uint32_t x = 0; x < availableLayerCount; x++) 
        {
            if (strcmp(validationLayers[i], availableLayers[x].layerName) == 0) 
            {
                layerFound = true;
                break;
            }
        }
    
        if (!layerFound) 
        {
            return false;
        }
    }
    
    return true;
}


void cleanupVulkan(VulkanComponents* components) // Frees up the previously initialized Vulkan parameters
{
    if (components == NULL) {
        return;
    }

    for (uint32_t i = 0; i < 3; i++)  // We wanna make sure all rendering is finished before we destroy anything
    {  
        vkWaitForFences(components->device, 1, &(components->inFlightFence[i]), VK_TRUE, UINT64_MAX);
    }

    cleanupSwapChain(components->device, &(components->swapChainGroup), &(components->framebufferGroup), &(components->viewGroup));

    #ifdef DEBUG_BUILD
    DestroyDebugUtilsMessengerEXT(components->instance, components->debugMessenger, NULL);
	#endif
    for (uint32_t i = 0; i<3; i++)
    {
        if (components->imageAvailableSemaphore[i])
        {
            vkDestroySemaphore(components->device, components->imageAvailableSemaphore[i], NULL);
        }
        if (components->renderFinishedSemaphore[i])
        {
            vkDestroySemaphore(components->device, components->renderFinishedSemaphore[i], NULL);
        }
        if (components->inFlightFence[i])
        {
            vkDestroyFence(components->device, components->inFlightFence[i], NULL);
        }

    }

    
    if (components->commandPool != NULL)
    {
        vkDestroyCommandPool(components->device, components->commandPool, NULL);
    }    

    if (components->renderPass != NULL)
    {
    	vkDestroyRenderPass(components->device, components->renderPass, NULL);
    }

    if (components->pipelineLayout != NULL)
    {
    	vkDestroyPipelineLayout(components->device, components->pipelineLayout, NULL);
    }

    if (components->graphicsPipeline != NULL)
    {
    	vkDestroyPipeline(components->device, components->graphicsPipeline, NULL);
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

    free(components);
}