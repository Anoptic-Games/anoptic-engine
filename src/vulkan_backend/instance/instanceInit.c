/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"



// Variables

static const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME }; // Should absolutely not be here, make dynamic and determined at runtime

// Vulkan component initialization functions

void enumerateMonitors(Monitors* monitors) // Instance creation helper
{
	GLFWmonitor** glfwMonitors = glfwGetMonitors(&(monitors->monitorCount));
	monitors->monitorInfos = malloc(monitors->monitorCount * sizeof(MonitorInfo));
	for (int i = 0; i < monitors->monitorCount; i++) 
	{
		monitors->monitorInfos[i].modes = glfwGetVideoModes(glfwMonitors[i], &(monitors->monitorInfos[i].modeCount));
	}
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) // Called by GLFW on window resize, not part of instance creation but related
{
	static uint32_t count = 0;
	VulkanComponents* components = glfwGetWindowUserPointer(window);
	printf("Resize: %d\n", count);
	count++;
	components->syncComp.framebufferResized = true;
}

GLFWwindow* initWindow(VulkanComponents* components, Monitors* monitors) // Initializes a GLFW window, necessary for instance creation but general in scope
{
	if (!glfwInit())
	{
		fprintf(stderr, "Failed to initialize GLFW!\n");
		return NULL;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	
	// Choose the monitor
	GLFWmonitor* chosenMonitor = NULL;
	uint32_t monitorIndex = getChosenMonitor();
	if (monitorIndex >= 0 && monitorIndex < monitors->monitorCount)
	{
		GLFWmonitor** glfwMonitors = glfwGetMonitors(NULL);
		chosenMonitor = glfwMonitors[monitorIndex];
	}
	else if (monitorIndex >= 0)
	{ // Default to primary if index is out of range
		chosenMonitor = glfwGetPrimaryMonitor();
	}

	// If borderless fullscreen is requested
	Dimensions2D resolution = getChosenResolution();
	if (getChosenBorderless() && chosenMonitor)
	{
		const GLFWvidmode* mode = glfwGetVideoMode(chosenMonitor);
		resolution.width = mode->width;
		resolution.height = mode->height;
	}

	if (monitorIndex == -1)
	{
		chosenMonitor = NULL;
	}
	
	GLFWwindow *window = glfwCreateWindow((int)resolution.width, (int)resolution.height, "Vulkan", chosenMonitor, NULL);
	
	glfwSetWindowUserPointer(window, components);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

	return window;
}

VkResult createInstance(VulkanComponents* vkComponents) // Central component of the init process, should be generalized and given packages of selected extensions, parameters etc
{
	const char* validationLayers[] = 
	{ //!TODO get this thing out of here as soon as we have a config parser
		"VK_LAYER_KHRONOS_validation",
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

	if (vkCreateInstance(&createInfo, NULL, &(vkComponents->instanceDebug.instance)) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create Vulkan instance!\n");
		free(extensions);
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	#ifdef DEBUG_BUILD
		setupDebugMessenger(&(vkComponents->instanceDebug.instance), &(vkComponents->instanceDebug.debugMessenger));
	#endif

	free(extensions);

	return VK_SUCCESS;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // Used in validation layer activation, the entire setup is currently borked and relies on external overrides
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) 
{

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		return VK_FALSE;
	}
	printf("Validation layer: %s\n", pCallbackData->pMessage);

	return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) 
{
	PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != NULL) 
	{
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	} else
	{
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

void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger) // As before, entire validator creation chain needs revisiting
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



const char** getRequiredExtensions(uint32_t* extensionsCount) // Central component of init, returns extensions required by GLFW + optional validators, should be extended
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	uint32_t totalExtensionCount = glfwExtensionCount;

	#ifdef DEBUG_BUILD
	totalExtensionCount += 1;
	#endif

	const char** extensions = calloc(totalExtensionCount, sizeof(char*));

	for (uint32_t i = 0; i < glfwExtensionCount; i++)
	{
		extensions[i] = strdup(glfwExtensions[i]);
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

	indices.graphicsFamily = UINT32_MAX;
	indices.computeFamily = UINT32_MAX;
	indices.transferFamily = UINT32_MAX;
	indices.presentFamily = UINT32_MAX;
	
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
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && indices.graphicsPresent == false)
		{
			indices.graphicsFamily = i;
			indices.graphicsPresent = true;
			//printf("Graphics: %d\n", i);
		}
		if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && indices.computePresent == false)
		{
			indices.computeFamily = i;
			indices.computePresent = true;
			//printf("Compute: %d\n", i);
		}
		if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && indices.transferPresent == false)
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
				if (indices.presentPresent == false) // Makes sure the primary present family gets selected, usually the same family as for graphics
				{
					indices.presentFamily = i;	
				}
				indices.presentPresent = true;
				//printf("Present: %d\n", indices.presentFamily);
			}
		}
		else
		{
			//printf("Surface invalid in queue selection!\n");
		}
		//printf("Queue family %d flags: %d\n", i, queueFamilies[i].queueFlags);
	}
	//printf("Final output:\n  Graphics Family: %d\n  Compute family: %d
	// \n  Transfer Family: %d\n  Present Family: %d\n\n",
	// indices.graphicsFamily, indices.computeFamily, indices.transferFamily, indices.presentFamily);
	return indices;
}

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device, VkPhysicalDeviceFeatures deviceFeatures) // Selects capabilities required for the instance, extend with checks and error states
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

bool checkDeviceExtensionSupport(VkPhysicalDevice device) { // Rework extensions system entirely, add interface for defining them and modify this logic to work with that
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

bool isDeviceSuitable(VkPhysicalDevice device, VkPhysicalDeviceFeatures deviceFeatures, VkSurfaceKHR *surface) // Greatly extend and integrate with device capability checks, expose via interface
{
	struct QueueFamilyIndices indices = findQueueFamilies(device, surface);
	// Add any features as they become necessary
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	//Since the GPU we're selecting is already the most performant one by default, there's no point selecting Discrete only
	bool physicalRequirements = deviceFeatures.geometryShader && deviceFeatures.shaderFloat64 && deviceFeatures.shaderInt64;
	bool queueRequirements = indices.graphicsPresent;

	return physicalRequirements && queueRequirements && extensionsSupported && deviceFeatures.samplerAnisotropy;
}

VkSampleCountFlagBits getMaxUsableSampleCount(VulkanComponents* components)
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(components->physicalDeviceComp.physicalDevice, &physicalDeviceProperties); // Cached properties should be preferentially used

	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

bool pickPhysicalDevice(VulkanComponents* components, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice) // Further extend selection logic, split device discovery into dedicated function
{																																				 //   and retain device attributes in public interface for use in UI or logic
	bool foundPreferredDevice = false;
	components->physicalDeviceComp.deviceCount = 0;

	vkEnumeratePhysicalDevices(components->instanceDebug.instance, &(components->physicalDeviceComp.deviceCount), NULL);

	if (components->physicalDeviceComp.deviceCount == 0) 
	{
		fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
		return false;
	}

	// Allocate memory for the names of every detected device
	components->physicalDeviceComp.availableDevices = (char**)malloc(sizeof(char*) * components->physicalDeviceComp.deviceCount);

	VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(1, sizeof(VkPhysicalDevice) * components->physicalDeviceComp.deviceCount);

	vkEnumeratePhysicalDevices(components->instanceDebug.instance, &components->physicalDeviceComp.deviceCount, devices);
	
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties memProperties;

	VkDeviceSize maxDedicatedMemory = 0;
	VkDeviceSize maxIntegratedMemory = 0;

	VkPhysicalDevice bestDedicatedDevice = VK_NULL_HANDLE;
	VkPhysicalDevice bestIntegratedDevice = VK_NULL_HANDLE;

	printf("DeviceCount: %d\n", components->physicalDeviceComp.deviceCount);
	
	for (uint32_t i = 0; i < components->physicalDeviceComp.deviceCount; i++)
	{
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
		vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);
		vkGetPhysicalDeviceMemoryProperties(devices[i], &memProperties);
		printf("%d\n", i);

		if (isDeviceSuitable(devices[i], deviceFeatures, &(components->surface)))
		{

			//Select the first preffered device if available and break out of the selection loop
			if (strcmp(deviceProperties.deviceName, preferredDevice) == 0)
			{
				components->physicalDeviceComp.physicalDevice = devices[i];
				foundPreferredDevice = true;
				break;
			}

		
			//!TODO Extend the logic to select the heap that's DEVICE_LOCAL
			VkDeviceSize currentMemorySize = memProperties.memoryHeaps[0].size;

			if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && currentMemorySize > maxDedicatedMemory)
			{
				bestDedicatedDevice = devices[i];
				maxDedicatedMemory = currentMemorySize;
			}
			else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && currentMemorySize > maxIntegratedMemory)
			{
				bestIntegratedDevice = devices[i];
				maxIntegratedMemory = currentMemorySize;
			}
			(components->physicalDeviceComp.availableDevices)[i] = (char*)malloc(strlen(deviceProperties.deviceName) +1);
			strcpy((components->physicalDeviceComp.availableDevices)[i], deviceProperties.deviceName);
			#ifdef DEBUG_BUILD
			printf("%s\n", components->physicalDeviceComp.availableDevices[i]);
			#endif
		}
	}

	if (foundPreferredDevice)
	{
		vkGetPhysicalDeviceFeatures(components->physicalDeviceComp.physicalDevice, &deviceFeatures);
		components->physicalDeviceComp.deviceCapabilities = populateCapabilities(components->physicalDeviceComp.physicalDevice, deviceFeatures);
		components->physicalDeviceComp.queueFamilyIndices = findQueueFamilies(components->physicalDeviceComp.physicalDevice, &(components->surface));
	}
	else
	{

		if (bestDedicatedDevice != VK_NULL_HANDLE)
		{
			components->physicalDeviceComp.physicalDevice = bestDedicatedDevice;
			vkGetPhysicalDeviceFeatures(components->physicalDeviceComp.physicalDevice, &deviceFeatures);
			components->physicalDeviceComp.deviceCapabilities = populateCapabilities(components->physicalDeviceComp.physicalDevice, deviceFeatures);
			components->physicalDeviceComp.queueFamilyIndices = findQueueFamilies(components->physicalDeviceComp.physicalDevice, &(components->surface));
		}
		else if (bestIntegratedDevice != VK_NULL_HANDLE)
		{
			components->physicalDeviceComp.physicalDevice = bestIntegratedDevice;
			vkGetPhysicalDeviceFeatures(components->physicalDeviceComp.physicalDevice, &deviceFeatures);
			components->physicalDeviceComp.deviceCapabilities = populateCapabilities(components->physicalDeviceComp.physicalDevice, deviceFeatures);
			components->physicalDeviceComp.queueFamilyIndices = findQueueFamilies(components->physicalDeviceComp.physicalDevice, &(components->surface));
		}
		else
		{
			fprintf(stderr, "Failed to find a suitable GPU!\n");
			free(devices);
			return false;
		}
	}

	components->physicalDeviceComp.msaaSamples = getMaxUsableSampleCount(components);
	printf("MSAA samples used: %d\n", components->physicalDeviceComp.msaaSamples);

	//printf("Graphics family: %d\nCompute family: %d\nTransfer family: %d\nPresent family: %d\n", (components->physicalDeviceComp.queueFamilyIndices.graphicsFamily), (components->physicalDeviceComp.queueFamilyIndices.computeFamily), (components->physicalDeviceComp.queueFamilyIndices.transferFamily), (components->physicalDeviceComp.queueFamilyIndices.presentFamily));

	free(devices);
	return true;
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices) // Creates a logical device that can actually do stuff.
{ // Central init component, may need re-visiting for queue selection
	// Device features (optional)
	VkPhysicalDeviceFeatures availableFeatures;
	vkGetPhysicalDeviceFeatures(physicalDevice, &availableFeatures);
	VkPhysicalDeviceFeatures deviceFeatures = {.shaderInt64 = availableFeatures.shaderInt64, .shaderFloat64 = availableFeatures.shaderFloat64,
												.samplerAnisotropy = availableFeatures.samplerAnisotropy}; // Add more features as required

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
		if (uniqueFamily)
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
	if (*graphicsQueue == NULL)
	{
		printf("Failed to acquire graphics queue!\n");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	vkGetDeviceQueue(*device, indices->presentFamily, 0, presentQueue);
	printf("PresentQueue: %p\n", presentQueue);
	if (*presentQueue == NULL)
	{
		printf("Failed to acquire present queue!\n");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->computeFamily, 0, computeQueue);
		if (*computeQueue == NULL)
		{
			printf("Failed to acquire compute queue!\n");
			return VK_ERROR_INITIALIZATION_FAILED;	
		}
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->transferFamily, 0, transferQueue);
		if (*transferQueue == NULL)
		{
			printf("Failed to acquire transfer queue!\n");
			return VK_ERROR_INITIALIZATION_FAILED;	
		}
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
{ // Central init component, also used during re-sizes, should work fine as-is but may be extended if needed for arbitrary resolution rendering and scaling
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}
		else
		{
			int width, height;
			if (getChosenBorderless())
			{
				// For borderless mode, use the primary monitor's resolution
				GLFWmonitor* primary = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(primary);
				width = mode->width;
				height = mode->height;
			} else
			{
				// Otherwise, use the larger of the window size or primary monitor's resolution
				GLFWmonitor* primary = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = glfwGetVideoMode(primary);
				glfwGetWindowSize(window, &width, &height);
				width = (width > mode->width) ? width : mode->width;
				height = (height > mode->height) ? height : mode->height;
			}
			VkExtent2D actualExtent =
			{
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

SwapChainGroup initSwapChain(VulkanComponents *components, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain) // Selects and initializes a swap chain
{ // Central init component, extend/split logic for different color spaces (HDR rendering). Also cache driver query results
	struct SwapChainSupportDetails details;
	if (components->swapChainComp.swapChainSupportDetails.formatCount) // If the details are already populated, fetch the local version
	{
		details = components->swapChainComp.swapChainSupportDetails;
	} else
	{
		details = querySwapChainSupport(components->physicalDeviceComp.physicalDevice, &components->surface);
	}
	

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

	VkPresentModeKHR chosenPresentMode;
	bool preferredModeFound = false;
	uint32_t presentModePriority = 0; 
	
	for (uint32_t i = 0; i < details.presentModesCount; i++) 
	{
		VkPresentModeKHR currentMode = details.presentModes[i];
	
		// If the preferred mode is found, use it and break out of loop
		if (currentMode == preferredMode) 
		{
			chosenPresentMode = preferredMode;
			preferredModeFound = true;
			break;
		}
	
		// Priority based system
		uint32_t currentPriority = 0;
		switch (currentMode) 
		{
			case VK_PRESENT_MODE_MAILBOX_KHR:
				currentPriority = 4;
				break;
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
				currentPriority = MAX_FRAMES_IN_FLIGHT;
				break;
			case VK_PRESENT_MODE_FIFO_KHR:
				currentPriority = 2;
				break;
			case VK_PRESENT_MODE_IMMEDIATE_KHR:
				currentPriority = 1;
				break;
			default:
				break;
		}
	
		if (currentPriority > presentModePriority) 
		{
			presentModePriority = currentPriority;
			chosenPresentMode = currentMode;
		}
	}
	
	if (!preferredModeFound && presentModePriority == 0) 
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
	createInfo.surface = components->surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = chosenFormat.format;
	createInfo.imageColorSpace = chosenFormat.colorSpace;
	createInfo.imageExtent = chosenExtent;
	createInfo.imageArrayLayers = 1; // We DO support VR Half Life Alyx 2
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	struct QueueFamilyIndices indices;
	if (components->physicalDeviceComp.queueFamilyIndices.graphicsPresent) // Use local copy if available
	{
		indices = components->physicalDeviceComp.queueFamilyIndices;
	} else
	{
		indices = findQueueFamilies(components->physicalDeviceComp.physicalDevice, &components->surface);
	}
	uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};
	if (indices.graphicsFamily != indices.presentFamily)
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; // Optional
		createInfo.pQueueFamilyIndices = NULL; // Optional
	}
	createInfo.preTransform = details.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = chosenPresentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = oldSwapChain;


	
	VkSwapchainKHR swapChain;
	VkResult result = vkCreateSwapchainKHR(components->deviceQueueComp.device, &createInfo, NULL, &swapChain);
	if ( result != VK_SUCCESS)
	{
		printf("Failed to create swap chain - error code %d!\n", result);
		SwapChainGroup failure = {NULL};
		return failure;
	}

	vkGetSwapchainImagesKHR(components->deviceQueueComp.device, swapChain, &imageCount, NULL);
	VkImage *swapChainImages = (VkImage *) calloc(imageCount, sizeof(VkImage));
	vkGetSwapchainImagesKHR(components->deviceQueueComp.device, swapChain, &imageCount, swapChainImages);

	SwapChainGroup swapChainGroup = {swapChain, chosenFormat.format, chosenExtent, imageCount, swapChainImages};
	
	return swapChainGroup;
}

void cleanupSwapChain(VulkanComponents* components, VkDevice device, SwapChainGroup* swapGroup, FrameBufferGroup* frameGroup, ImageViewGroup* viewGroup)
{
    // Destroy framebuffers
    for (size_t i = 0; i < frameGroup->bufferCount; i++) 
    {
        vkDestroyFramebuffer(device, frameGroup->buffers[i], NULL);
    }

    // Destroy swapchain image views
    for (size_t i = 0; i < viewGroup->viewCount; i++) 
    {
        vkDestroyImageView(device, viewGroup->views[i], NULL);
    }

    // Destroy depth image views
    if (components->renderComp.buffers.depthView) 
    {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
        {
            if (components->renderComp.buffers.depthView[i] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, components->renderComp.buffers.depthView[i], NULL);
                components->renderComp.buffers.depthView[i] = VK_NULL_HANDLE;
            }
        }
    }

    // Destroy depth images
    if (components->renderComp.buffers.depth) 
    {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
        {
            if (components->renderComp.buffers.depth[i] != VK_NULL_HANDLE)
            {
                vkDestroyImage(device, components->renderComp.buffers.depth[i], NULL);
                components->renderComp.buffers.depth[i] = VK_NULL_HANDLE;
            }
        }
    }

    // Free depth memory (important!)
    if (components->renderComp.buffers.depthMemory) 
    {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
        {
            if (components->renderComp.buffers.depthMemory[i] != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, components->renderComp.buffers.depthMemory[i], NULL);
                components->renderComp.buffers.depthMemory[i] = VK_NULL_HANDLE;
            }
        }
    }

    // Free color image
    if (components->swapChainComp.swapChainGroup.colorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, components->swapChainComp.swapChainGroup.colorImage, NULL);
        components->swapChainComp.swapChainGroup.colorImage = VK_NULL_HANDLE;
    }

    // Free color image memory
    if (components->swapChainComp.swapChainGroup.colorImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, components->swapChainComp.swapChainGroup.colorImageMemory, NULL);
        components->swapChainComp.swapChainGroup.colorImageMemory = VK_NULL_HANDLE;
    }

    // Destroy color image view
    if (viewGroup->colorView) 
    {
        vkDestroyImageView(device, viewGroup->colorView, NULL);
    }

    // Free UI image
    if (components->swapChainComp.swapChainGroup.uiImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, components->swapChainComp.swapChainGroup.uiImage, NULL);
        components->swapChainComp.swapChainGroup.uiImage = VK_NULL_HANDLE;
    }

    // Free UI image memory
    if (components->swapChainComp.swapChainGroup.uiImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, components->swapChainComp.swapChainGroup.uiImageMemory, NULL);
        components->swapChainComp.swapChainGroup.uiImageMemory = VK_NULL_HANDLE;
    }

    // Destroy UI image view
    if (viewGroup->uiView) 
    {
        vkDestroyImageView(device, viewGroup->uiView, NULL);
    }
}

void recreateSwapChain(VulkanComponents* components, GLFWwindow* window)
{
    // First, clean up the previous swapchain
	cleanupSwapChain(components, components->deviceQueueComp.device, &(components->swapChainComp.swapChainGroup), &(components->swapChainComp.framebufferGroup), &(components->swapChainComp.viewGroup));
    
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0)
	{
		printf("Sleeping!\n");
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}
    // Explicitly update these to ensure swapchain recreation has the correct values
    components->swapChainComp.swapChainGroup.imageExtent.width = width;
    components->swapChainComp.swapChainGroup.imageExtent.height = height;

	if (!components->syncComp.skipCheck)
	{
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
		{
			vkWaitForFences(components->deviceQueueComp.device, 1, &(components->syncComp.inFlightFence[i]), VK_TRUE, UINT64_MAX);
		}
	}


	// Save outdated swapchain
	VkSwapchainKHR oldSwapChain = components->swapChainComp.swapChainGroup.swapChain;
	components->swapChainComp.swapChainGroup.swapChain = VK_NULL_HANDLE;


	SwapChainGroup failure = {NULL};
	//!TODO Change the last element to the desired present mode
	components->swapChainComp.swapChainGroup = initSwapChain(components, window, getChosenPresentMode(), oldSwapChain);

	// Destroy old swapchain
	vkDestroySwapchainKHR(components->deviceQueueComp.device, oldSwapChain, NULL);

	if(components->swapChainComp.swapChainGroup.swapChain == failure.swapChain)
	{
		printf("Swap chain re-creation error, exiting!\n");
		cleanupVulkan(components);
		exit(1);
	}
	components->swapChainComp.viewGroup = createImageViews(components->deviceQueueComp.device, components->swapChainComp.swapChainGroup);
	if (components->swapChainComp.viewGroup.views == NULL)
	{
		printf("View group re-creation error, exiting!\n");
		cleanupVulkan(components);
		exit(1);
	}

	createColorResources(components);

	createDepthResources(components);
	if (components->renderComp.buffers.depthView[0] == NULL)
	{
		printf("Depth resources re-creation error, exiting!\n");
		cleanupVulkan(components);
		exit(1);
	}

	if (!createFramebuffers(components))
	{
		printf("Framebuffer re-creation error, exiting!\n");
		cleanupVulkan(components);
		exit(1);
	}

	

	vkResetCommandPool(components->deviceQueueComp.device, components->cmdComp.commandPool, 0);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) // Clear fences prior to resuming render
	{
		vkResetFences(components->deviceQueueComp.device, 1, &(components->syncComp.inFlightFence[i]));
	}
	components->syncComp.skipCheck = MAX_FRAMES_IN_FLIGHT; // Skip semaphore waits

	components->syncComp.framebufferResized = false;

}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{ // Central init component, should be extended to allow for runtime mipmap definition and potential 3D texture support
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	if (vkCreateImageView(device, &viewInfo, NULL, &imageView) != VK_SUCCESS)
	{
		printf("Failed to create image view!\n");
	}

	return imageView;
}


ImageViewGroup createImageViews(VkDevice device, SwapChainGroup imageGroup)
{ // Central init component, only applies to display swapchain images
	ImageViewGroup viewGroup = {0, NULL};
	viewGroup.viewCount = imageGroup.imageCount;
	viewGroup.views = (VkImageView *) calloc(1, sizeof(VkImageView) * viewGroup.viewCount);
	for (uint32_t i = 0; i < imageGroup.imageCount; i++)
	{
		viewGroup.views[i] = createImageView(device, imageGroup.images[i], imageGroup.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);		
	}
	return viewGroup;
}

bool createFramebuffers(VulkanComponents* components)
{ // Central init component
    if (components->swapChainComp.framebufferGroup.buffers)
    { // Deallocate previous buffer
        free(components->swapChainComp.framebufferGroup.buffers);
    }
	components->swapChainComp.framebufferGroup.bufferCount = components->swapChainComp.viewGroup.viewCount;
	components->swapChainComp.framebufferGroup.buffers = (VkFramebuffer*) calloc(1, sizeof(VkFramebuffer) * components->swapChainComp.framebufferGroup.bufferCount);

	for (uint32_t i = 0; i < components->swapChainComp.viewGroup.viewCount; i++) 
	{
		VkImageView attachments[] = 
		{
			components->swapChainComp.viewGroup.colorView,
			components->renderComp.buffers.depthView[i],
			components->swapChainComp.viewGroup.views[i]
		};
	
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = components->renderComp.renderPass;
		framebufferInfo.attachmentCount = sizeof(attachments)/sizeof(VkImageView);
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = components->swapChainComp.swapChainGroup.imageExtent.width;
		framebufferInfo.height = components->swapChainComp.swapChainGroup.imageExtent.height;
		framebufferInfo.layers = 1;
	
		if (vkCreateFramebuffer(components->deviceQueueComp.device, &framebufferInfo, NULL, (components->swapChainComp.framebufferGroup.buffers+i)) != VK_SUCCESS) 
		{
			printf("Failed to create framebuffer!\n");
			return false;
		}
	}
	return true;
}

bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool)
{ // Central init component
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

bool allocateBuffer(VulkanComponents* components, VkBuffer buffer, VkMemoryPropertyFlags properties, VkDeviceMemory* bufferMemory)
{
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(components->deviceQueueComp.device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(components, memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(components->deviceQueueComp.device, &allocInfo, NULL, bufferMemory) != VK_SUCCESS)
	{
		printf("Failed to allocate buffer memory!");
		return false;
	}

	vkBindBufferMemory(components->deviceQueueComp.device, buffer, *bufferMemory, 0);
	return true;
}

bool createDataBuffer(VulkanComponents* components, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, buffer) != VK_SUCCESS)
	{
		printf("Failed to create data buffer!");
		return false;
	}

	if (!allocateBuffer(components, *buffer, properties, bufferMemory))
	{
		// Clean up the created buffer before returning
		vkDestroyBuffer(components->deviceQueueComp.device, *buffer, NULL);
		return false;
	}

	return true;
}

bool createVertexBuffer(VulkanComponents* components, uint32_t vertexCount, VkBuffer* vertex, VkDeviceMemory* vertexMemory)
{ // Not core to init, mostly meant for actual rendering
	VkDeviceSize bufferSize = sizeof(Vertex) * vertexCount;

	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, properties, vertex, vertexMemory)) 
	{
		printf("Failed to create vertex buffer!");
		return false;
	}
	
	return true;
}

bool createIndexBuffer(VulkanComponents* components, uint32_t indexCount, VkBuffer* index, VkDeviceMemory* indexMemory)
{ // Ditto
	VkDeviceSize bufferSize = sizeof(uint16_t) * indexCount;
	

	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, properties, index, indexMemory)) 
	{
		printf("Failed to create index buffer!");
		return false;
	}
	
	return true;

}

bool createUniformBuffers(VulkanComponents* components)
{ // Central to init, specific to perspective uniforms (world translation, rotation and projection)
	VkDeviceSize bufferSize = sizeof(UniformComponents);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{

		if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			 &(components->renderComp.buffers.uniform[i]), &(components->renderComp.buffers.uniformMemory[i]))) 
		{
			printf("Failed to create uniform buffer!");
			return false;
		}
		printf("!!UBO!! UBO buffer: %p\n", components->renderComp.buffers.uniform[i]);

		vkMapMemory(components->deviceQueueComp.device, components->renderComp.buffers.uniformMemory[i], 0, bufferSize, 0, &(components->renderComp.buffers.uniformMapped[i]));
		printf("!!UBO!! Created mapped UBO buffer at: %p\n", components->renderComp.buffers.uniformMapped[i]);
	}

	return true;
}

bool createTransformBuffers(VulkanComponents* components)
{ // !TODO transition this for dynamic mesh instantiation
	VkDeviceSize bufferSize = sizeof(ModelTransforms);

	for (size_t i = 0; i < components->renderComp.buffers.entityCount; i++)
	{

		if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			 &(components->renderComp.buffers.entities[i].transform), &(components->renderComp.buffers.entities[i].transformMemory))) 
		{
			printf("Failed to create uniform buffer!");
			return false;
		}
		printf("!!MESHTRANSFORM!! Transform uniform buffer: %p\n", components->renderComp.buffers.entities[i].transform);

		vkMapMemory(components->deviceQueueComp.device, components->renderComp.buffers.entities[i].transformMemory, 0, bufferSize, 0, &(components->renderComp.buffers.entities[i].transformMapped));
		printf("!!MESHTRANSFORM!! Created mapped memory buffer at %p\n", components->renderComp.buffers.entities[i].transformMapped);
	}
	
	return true;
}

void printMatrix(float mat[4][4])
{ // Debug function previously used to sanity-check matrix operation results. Can prolly be removed
	printf("Matrix:\n");
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			printf("%f ", mat[i][j]);
		}
		printf("\n");
	}
}

bool updateUniformBuffer(VulkanComponents* components)
{ // Changes the perspective (camera) parameters applied to the world-space for a given frame. Should be generalized with its parameters exposed via an interface
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	for(int i = 0; i < 4; i++)
		for(int j = 0; j < 4; j++)
			components->renderComp.uniform.model[i][j] = (i == j) ? 1.0f : 0.0f;

	rotateMatrix(components->renderComp.uniform.model, 'Y', angle);

	float eye[] = {0.0f, 0.9f, 3.5f};  // Move camera up and back
	float center[] = {0.0f, 0.15f, 0.0f}; // Camera looks at the origin
	float up[] = {0.0f, -1.0f, 0.0f};  // World is flipped // TODO: Maybe unflip the world

	lookAt(components->renderComp.uniform.view, eye, center, up);

	float fov = 45.0f; // Field of View in degrees
	float aspect = (float)components->swapChainComp.swapChainGroup.imageExtent.width / (float)components->swapChainComp.swapChainGroup.imageExtent.height;
	float near = 0.1f;
	float far = 100.0f;
	perspective(components->renderComp.uniform.proj, fov, aspect, near, far);
	
	//printf("!!UBO!! Mapped UBO buffer on value update: %p\n", components->renderComp.buffers.uniformMapped[components->syncComp.frameIndex]);
	memcpy(components->renderComp.buffers.uniformMapped[components->syncComp.frameIndex], &(components->renderComp.uniform), sizeof(components->renderComp.uniform));

	angle += ((float)(time-oldTime)) * 0.000000f;
	if(angle > 2.0f * pi)
	{
		angle = 0.0f;	
	}
	oldTime = time;

	return true;
}

bool updateMeshTransforms(VulkanComponents* components, EntityBuffer* entity, float move)
{
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	// Initialize ModelTransforms structure
	ModelTransforms meshTransforms;

	// Initialize translation, rotation, and scale matrices to identity
	for(int i = 0; i < 4; i++)
	{
		for(int j = 0; j < 4; j++)
		{
			meshTransforms.translation[i][j] = (i == j) ? 1.0f : 0.0f;
			meshTransforms.rotation[i][j] = (i == j) ? 1.0f : 0.0f;
			meshTransforms.scale[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	translate(meshTransforms.translation, move, 0.0f, 0.0f);

	// Apply rotation to the mesh's transform
	rotateMatrix(meshTransforms.rotation, 'Y', angle);

	// TODO: Apply translation and scaling if needed
	// Example: translateMatrix(meshTransforms.translation, x, y, z);
	// Example: scaleMatrix(meshTransforms.scale, scaleX, scaleY, scaleZ);

	// Map the buffer and copy the transformation data
	//printf("!!MESHTRANSFORM!! Mapped transform buffer on value update: %p\n", entity->transformMapped);
	memcpy(entity->transformMapped, &meshTransforms, sizeof(meshTransforms));

	// Update angle for next frame
	angle += ((float)(time - oldTime)) * 0.000001f;
	if (angle > 2.0f * pi)
	{
		angle = 0.0f;
	}
	oldTime = time;

	return true;
}

VkFormat findSupportedFormat(VulkanComponents* components, const VkFormat* candidates, uint32_t candidateCount, VkImageTiling tiling, VkFormatFeatureFlags features)
{ // Returns a device-supported format from a list of candidates, currently only used in pipeline images but should be used every time an image is created
	for (int i = 0; i < candidateCount; i++)
	{
		VkFormat format = candidates[i];
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(components->physicalDeviceComp.physicalDevice, format, &props);

		// TODO: Figure out if both cases are really necessary (currently identical results)
		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
		{
			return format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
		{
			return format;
		}
	}
	printf("Failed to find a suitable format!\n");
	return VK_FORMAT_UNDEFINED;
}


// !TODO move this to a bottom-level library
VkFormat findDepthFormat(VulkanComponents* components)
{
	VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
	return(findSupportedFormat(components, &candidates[0], sizeof(candidates)/sizeof(VkFormat),
								VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
}

bool hasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


bool createDepthResources(VulkanComponents* components)
{
	VkFormat depthFormat = findDepthFormat(components);
	if (depthFormat == VK_FORMAT_UNDEFINED)
	{
		printf("No compatible depth formats detected!\n");
		return false;
	}
	components->renderComp.buffers.depthFormat = depthFormat;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
	{
		if (!createImage(components, components->swapChainComp.swapChainGroup.imageExtent.width, 
						 components->swapChainComp.swapChainGroup.imageExtent.height, 1,
						 components->physicalDeviceComp.msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL,
						 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						 &components->renderComp.buffers.depth[i], &components->renderComp.buffers.depthMemory[i], false))
		{
			printf("Failed to create depth resource for frame %d!\n", i);
			return false;
		}

		components->renderComp.buffers.depthView[i] = createImageView(components->deviceQueueComp.device, 
																	  components->renderComp.buffers.depth[i], 
																	  depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

		if(!transitionImageLayout(components, components->renderComp.buffers.depth[i], depthFormat, 
								  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1))
		{
			printf("Failed to transition depth buffer layout for frame %d!\n", i);
			return false;
		}
	}
	return true;
}

void createColorResources(VulkanComponents* components) //TODO: This probably should be generalized later?
{
	VkFormat colorFormat = components->swapChainComp.swapChainGroup.imageFormat;

	createImage(components, components->swapChainComp.swapChainGroup.imageExtent.width, components->swapChainComp.swapChainGroup.imageExtent.height,
				1, components->physicalDeviceComp.msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &components->swapChainComp.swapChainGroup.colorImage, &components->swapChainComp.swapChainGroup.colorImageMemory, false);
	components->swapChainComp.viewGroup.colorView = createImageView(components->deviceQueueComp.device, components->swapChainComp.swapChainGroup.colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    // UI??
	createImage(components, components->swapChainComp.swapChainGroup.imageExtent.width, components->swapChainComp.swapChainGroup.imageExtent.height,
				1, components->physicalDeviceComp.msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &components->swapChainComp.swapChainGroup.uiImage, &components->swapChainComp.swapChainGroup.uiImageMemory, false);
	components->swapChainComp.viewGroup.uiView = createImageView(components->deviceQueueComp.device, components->swapChainComp.swapChainGroup.uiImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

bool createDescriptorPool(VulkanComponents* components)
{ // Central to init
	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSize) / sizeof(VkDescriptorPoolSize));
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	if (vkCreateDescriptorPool(components->deviceQueueComp.device, &poolInfo, NULL, &(components->renderComp.descriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}

bool createMeshDescriptorPool(VulkanComponents* components)
{ // Central to init
	VkDescriptorPoolSize poolSizes[2] = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = (uint32_t)components->renderComp.buffers.entityCount;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = (uint32_t)components->renderComp.buffers.entityCount;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(VkDescriptorPoolSize));
	poolInfo.pPoolSizes = &poolSizes;
	poolInfo.maxSets = (uint32_t)components->renderComp.buffers.entityCount;

	if (vkCreateDescriptorPool(components->deviceQueueComp.device, &poolInfo, NULL, &(components->renderComp.meshDescriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}

bool createDescriptorSets(VulkanComponents* components)
{ // Central to init, !TODO modify this to account for multiple descriptor sets, for multiple meshes
	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		layouts[i] = components->renderComp.descriptorSetLayout;
	}
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.descriptorPool = components->renderComp.descriptorPool;
	allocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
	allocInfo.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(components->deviceQueueComp.device, &allocInfo, components->renderComp.descriptorSets) != VK_SUCCESS)
	{
		printf("Failed to allocate descriptor sets!\n");	
		return false;
	}


	return true;
}

bool createMeshDescriptorSets(VulkanComponents* components)
{
	// Allocate entity descriptor sets
	size_t entityCount = components->renderComp.buffers.entityCount;

	VkDescriptorSetLayout* layouts = calloc(entityCount, sizeof(VkDescriptorSetLayout));

	for (uint32_t i = 0; i < entityCount; i++)
	{
		layouts[i] = components->renderComp.meshDescriptorSetLayout;
	}

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.descriptorPool = components->renderComp.meshDescriptorPool;
	allocInfo.descriptorSetCount = entityCount;
	allocInfo.pSetLayouts = layouts;

	VkDescriptorSet* tempSets = calloc(entityCount, sizeof(VkDescriptorSet));

	printf("!!DEBUG!!\nMesh descriptors: %d\n", entityCount);

	if (vkAllocateDescriptorSets(components->deviceQueueComp.device, &allocInfo, tempSets) != VK_SUCCESS)
	{
		printf("Failed to allocate mesh descriptor sets!\n");
		return false;
	}

	for (uint32_t j = 0; j < entityCount; j++)
	{
		components->renderComp.buffers.entities[j].meshDescriptorSet = tempSets[j];
	}

	free(layouts);
	free(tempSets);

	return true;
}

void updateUboDescriptorSets(VulkanComponents* components)
{ // Central to init, must be called on asset uploads. Should look into decoupling this somewhat so that entity assets can be managed dynamically.

	// Update scene-wide UBO descriptors
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = components->renderComp.buffers.uniform[i];
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformComponents);
		printf("!!UBO!! UBO buffer on descriptor update: %p\n", components->renderComp.buffers.uniform[i]);

		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = components->renderComp.descriptorSets[i];
		descriptorWrite.dstBinding = 0;   // Corresponds to binding in shader.
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(components->deviceQueueComp.device, 1, &descriptorWrite, 0, NULL);
	}
}

void updateMeshDescriptorSets(VulkanComponents* components)
{ // Central to init, must be called on asset uploads. Should look into decoupling this somewhat so that entity assets can be managed dynamically.

	// Update scene-wide UBO descriptors

	size_t entityCount = components->renderComp.buffers.entityCount;
	VkDescriptorImageInfo* imageInfos = (VkDescriptorImageInfo*)calloc(entityCount, sizeof(VkDescriptorImageInfo));

	for (size_t i = 0; i < entityCount; i++)
	{
		imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos[i].imageView = components->renderComp.buffers.entities[i].textureImageView;
		imageInfos[i].sampler = components->renderComp.textureSampler;
	}

	VkWriteDescriptorSet* descriptorWrites = (VkWriteDescriptorSet*)calloc(entityCount*2, sizeof(VkWriteDescriptorSet));

	VkDescriptorBufferInfo* bufferInfos = (VkDescriptorBufferInfo*)calloc(entityCount, sizeof(VkDescriptorBufferInfo));

	for (size_t j = 0; j < entityCount; j++)
	{
		bufferInfos[j].buffer = components->renderComp.buffers.entities[j].transform;
		bufferInfos[j].offset = 0;
		bufferInfos[j].range = sizeof(ModelTransforms);
		printf("!!MESHTRANSFORM!! Transform uniform buffer on descriptor update: %p\n", components->renderComp.buffers.entities[j].transform);

		descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[j].dstSet = components->renderComp.buffers.entities[j].meshDescriptorSet;
		descriptorWrites[j].dstBinding = 0;   // Corresponds to binding in shader.
		descriptorWrites[j].dstArrayElement = 0;
		descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[j].descriptorCount = 1;
		descriptorWrites[j].pBufferInfo = &bufferInfos[j];
	}

	for (size_t j = 0; j < entityCount; j++)
	{
		descriptorWrites[j+entityCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[j+entityCount].dstSet = components->renderComp.buffers.entities[j].meshDescriptorSet;
		descriptorWrites[j+entityCount].dstBinding = 1;
		descriptorWrites[j+entityCount].dstArrayElement = 0;
		descriptorWrites[j+entityCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[j+entityCount].descriptorCount = 1;
		descriptorWrites[j+entityCount].pImageInfo = &imageInfos[j];
	}

	vkUpdateDescriptorSets(components->deviceQueueComp.device, entityCount*2, descriptorWrites, 0, NULL);

	free(imageInfos);
	free(descriptorWrites);
}

/*bool createDescriptorPool(VulkanComponents* components)
{ // Central to init
	VkDescriptorPoolSize poolSizes[2] = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(VkDescriptorPoolSize));
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT;

	if (vkCreateDescriptorPool(components->deviceQueueComp.device, &poolInfo, NULL, &(components->renderComp.descriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}*/





uint32_t findMemoryType(VulkanComponents* components, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{ // Central to init, also used externally post-init
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(components->physicalDeviceComp.physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	
	printf("Failed to find suitable memory type!");
	return UINT32_MAX;
}


bool stagingTransfer(VulkanComponents* components, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize)
{ // Not central to init
	// Create staging buffer
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (!createDataBuffer(components, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, properties, &stagingBuffer, &stagingBufferMemory)) 
	{
		printf("Failed to create staging buffer!");
		return false;
	}

	// Map the staging buffer's memory, copy the data, and then unmap
	void* mappedMemory;
	vkMapMemory(components->deviceQueueComp.device, stagingBufferMemory, 0, bufferSize, 0, &mappedMemory);
	memcpy(mappedMemory, data, bufferSize);
	vkUnmapMemory(components->deviceQueueComp.device, stagingBufferMemory);

	// Copy data from staging buffer to destination buffer
	if (!copyBuffer(components, stagingBuffer, dstBuffer, bufferSize))
	{
		printf("Failed to copy buffers!");
		return false;
	}

	// Cleanup staging buffer
	vkDestroyBuffer(components->deviceQueueComp.device, stagingBuffer, NULL);
	vkFreeMemory(components->deviceQueueComp.device, stagingBufferMemory, NULL);

	return true;
}

VkCommandBuffer beginSingleTimeCommands(VulkanComponents* components)
{ // Used in init, also external
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = components->cmdComp.commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(components->deviceQueueComp.device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void endSingleTimeCommands(VulkanComponents* components, VkCommandBuffer commandBuffer)
{ // Used in init, also external
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(components->deviceQueueComp.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(components->deviceQueueComp.graphicsQueue);

	vkFreeCommandBuffers(components->deviceQueueComp.device, components->cmdComp.commandPool, 1, &commandBuffer);
}


bool copyBuffer(VulkanComponents* components, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{ // Used in init, also external
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(components);
	
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(components, commandBuffer);

	return true;
}

bool createCommandBuffer(VulkanComponents* components)
{ // Central to init
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = components->cmdComp.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	for (uint32_t i =0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkAllocateCommandBuffers(components->deviceQueueComp.device, &allocInfo, &(components->cmdComp.commandBuffer[i])) != VK_SUCCESS) 
		{
			printf("Failed to allocate command buffers!\n");
			return false;
		}
	}

	return true;
}

bool createSyncObjects(VulkanComponents* components) 
{ // Central to init
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;  

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		if (vkCreateSemaphore(components->deviceQueueComp.device, &semaphoreInfo, NULL, &(components->syncComp.imageAvailableSemaphore[i])) != VK_SUCCESS ||
			vkCreateSemaphore(components->deviceQueueComp.device, &semaphoreInfo, NULL, &(components->syncComp.renderFinishedSemaphore[i])) != VK_SUCCESS ||
			vkCreateFence(components->deviceQueueComp.device, &fenceInfo, NULL, &(components->syncComp.inFlightFence[i])) != VK_SUCCESS)
			{
			printf("Failed to create semaphores!\n");
			return false;
		}
	}

	return true;
}

bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount)
{ // Central to init, review during validation layer fix
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

void cleanupMonitors(Monitors* monitors)
{
	if (monitors->monitorInfos)
	{
		free(monitors->monitorInfos);
		monitors->monitorInfos = NULL;
		monitors->monitorCount = 0;
	}
}



void cleanupVulkan(VulkanComponents* components) // Frees up the previously initialized Vulkan parameters
{
	// !TODO ADD INTERMEDIARY FUNCTION TO DESTROY ENTITY STRUCT ASSETS, CALL HERE
	// !TODO Also texture samplers, image views, etc etc I basically gave up at this point since everything's gonna have to be generalized regardless
	if (components == NULL)
	{
		return;
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)  // We wanna make sure all rendering is finished before we destroy anything
	{  
		vkWaitForFences(components->deviceQueueComp.device, 1, &(components->syncComp.inFlightFence[i]), VK_TRUE, UINT64_MAX);
	}

	cleanupSwapChain(components, components->deviceQueueComp.device, &(components->swapChainComp.swapChainGroup), &(components->swapChainComp.framebufferGroup), &(components->swapChainComp.viewGroup));

	if(components->renderComp.buffers.entities[0].vertex)
	{
		vkDestroyBuffer(components->deviceQueueComp.device, components->renderComp.buffers.entities[0].vertex, NULL);
	}

	if(components->renderComp.buffers.entities[0].vertexMemory)
	{
		vkFreeMemory(components->deviceQueueComp.device, components->renderComp.buffers.entities[0].vertexMemory, NULL);
	}

	if(components->renderComp.buffers.entities[0].index)
	{
		vkDestroyBuffer(components->deviceQueueComp.device, components->renderComp.buffers.entities[0].vertex, NULL);
	}

	if(components->renderComp.buffers.entities[0].indexMemory)
	{
		vkFreeMemory(components->deviceQueueComp.device, components->renderComp.buffers.entities[0].vertexMemory, NULL);
	}

	if(components->renderComp.descriptorPool)
	{
		vkDestroyDescriptorPool(components->deviceQueueComp.device, components->renderComp.descriptorPool, NULL);
		vkDestroyDescriptorSetLayout(components->deviceQueueComp.device, components->renderComp.descriptorSetLayout, NULL);
	}

	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if(components->renderComp.buffers.uniform[i])
		{
			vkDestroyBuffer(components->deviceQueueComp.device, components->renderComp.buffers.uniform[i], NULL);
		}
		if(components->renderComp.buffers.uniformMemory[i])
		{
			vkFreeMemory(components->deviceQueueComp.device, components->renderComp.buffers.uniformMemory[i], NULL);
		}
	}

	#ifdef DEBUG_BUILD
	DestroyDebugUtilsMessengerEXT(components->instanceDebug.instance, components->instanceDebug.debugMessenger, NULL);
	#endif
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (components->syncComp.imageAvailableSemaphore[i])
		{
			vkDestroySemaphore(components->deviceQueueComp.device, components->syncComp.imageAvailableSemaphore[i], NULL);
		}
		if (components->syncComp.renderFinishedSemaphore[i])
		{
			vkDestroySemaphore(components->deviceQueueComp.device, components->syncComp.renderFinishedSemaphore[i], NULL);
		}
		if (components->syncComp.inFlightFence[i])
		{
			vkDestroyFence(components->deviceQueueComp.device, components->syncComp.inFlightFence[i], NULL);
		}

	}

	
	if (components->cmdComp.commandPool != NULL)
	{
		vkDestroyCommandPool(components->deviceQueueComp.device, components->cmdComp.commandPool, NULL);
	}	

	if (components->renderComp.renderPass != NULL)
	{
		vkDestroyRenderPass(components->deviceQueueComp.device, components->renderComp.renderPass, NULL);
	}

	if (components->renderComp.pipelineLayout != NULL)
	{
		vkDestroyPipelineLayout(components->deviceQueueComp.device, components->renderComp.pipelineLayout, NULL);
	}

	if (components->renderComp.descriptorSetLayout != NULL)
	{
		vkDestroyDescriptorSetLayout(components->deviceQueueComp.device, components->renderComp.descriptorSetLayout, NULL);
	}

	if (components->renderComp.graphicsPipeline != NULL)
	{
		vkDestroyPipeline(components->deviceQueueComp.device, components->renderComp.graphicsPipeline, NULL);
	}
	
	if (components->deviceQueueComp.device != VK_NULL_HANDLE) 
	{
		vkDeviceWaitIdle(components->deviceQueueComp.device);
		vkDestroyDevice(components->deviceQueueComp.device, NULL);
	}

	if (components->physicalDeviceComp.availableDevices != NULL)
	{
		for (uint32_t i = 0; i < components->physicalDeviceComp.deviceCount; i++)
		{
			free(components->physicalDeviceComp.availableDevices[i]);
			components->physicalDeviceComp.availableDevices[i] = NULL;
		}
		free(components->physicalDeviceComp.availableDevices);
		components->physicalDeviceComp.availableDevices = NULL;
	}

	if (components->instanceDebug.instance != VK_NULL_HANDLE) 
	{
		if (components->surface != VK_NULL_HANDLE) 
		{
			vkDestroySurfaceKHR(components->instanceDebug.instance, components->surface, NULL);
		}
		vkDestroyInstance(components->instanceDebug.instance, NULL);
	}

}
