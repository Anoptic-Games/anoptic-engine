/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory.h>
#include <anoptic_log.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"


VkResult createInstance(VulkanContext* ctx) // Central component of the init process
{
	const char* validationLayers[] = 
	{ //!TODO move to config parser
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

	#ifdef __APPLE__
	// MoltenVK portability driver requires this flag
	createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	#endif

	// Validation layers disabled
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
		ano_log(ANO_WARN, "Validation layers requested, but not available!");
	}
	else
	{
		createInfo.enabledLayerCount = (uint32_t) validationCount;
		createInfo.ppEnabledLayerNames = validationLayers;
		populateDebugMessengerCreateInfo(&debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
		ano_log(ANO_INFO, "Enabled validation layers!");
	}
	#endif

    VkResult result = vkCreateInstance(&createInfo, NULL, &(ctx->instance));
	if (result != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create Vulkan instance! Error code: %d", result);
		free(extensions);
		return result; // Returning the actual error is usually better than hardcoding VK_ERROR_INITIALIZATION_FAILED
	}

	#ifdef DEBUG_BUILD
		setupDebugMessenger(&(ctx->instance), &(ctx->debugMessenger));
	#endif

	free(extensions);

	return VK_SUCCESS;
}

uint32_t g_ValidationErrors = 0;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // Validation messenger callback
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) 
{

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		return VK_FALSE;
	}
	// Route by layer severity
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		ano_log(ANO_ERROR, "Validation layer: %s", pCallbackData->pMessage);
	else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		ano_log(ANO_WARN, "Validation layer: %s", pCallbackData->pMessage);
	else
		ano_debug_log(ANO_INFO, "Validation layer: %s", pCallbackData->pMessage);

	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		g_ValidationErrors++;
	}

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

void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger) // Installs the standalone steady-state messenger
{
	#ifndef DEBUG_BUILD
	return;
	#endif
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
	populateDebugMessengerCreateInfo(&createInfo);
	if (CreateDebugUtilsMessengerEXT(*instance, &createInfo, NULL, debugMessenger) != VK_SUCCESS)
	{
		ano_log(ANO_WARN, "Failed to set up debug messenger!");
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



const char** getRequiredExtensions(uint32_t* extensionsCount) // Returns extensions required by GLFW + optional validators
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	uint32_t totalExtensionCount = glfwExtensionCount;

	#ifdef DEBUG_BUILD
	totalExtensionCount += 1;
	#endif

	#ifdef __APPLE__
	// MoltenVK portability extension
	totalExtensionCount += 1;
	#endif

	const char** extensions = calloc(totalExtensionCount, sizeof(char*));

	uint32_t idx = 0;
	for (uint32_t i = 0; i < glfwExtensionCount; i++)
	{
		extensions[idx++] = strdup(glfwExtensions[i]);
	}

	#ifdef DEBUG_BUILD
	extensions[idx++] = strdup(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	#endif

	#ifdef __APPLE__
	extensions[idx++] = strdup(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	#endif

	*extensionsCount = totalExtensionCount;

	return extensions;
}

VkResult createSurface(VkInstance instance, GLFWwindow* window, VkSurfaceKHR* surface) // Creates a GLFW window surface
{
	if (glfwCreateWindowSurface(instance, window, NULL, surface) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create window surface!");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	return VK_SUCCESS;
}


bool checkValidationLayerSupport(const char* validationLayers[], size_t validationCount)
{ // Central to init
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);

	VkLayerProperties availableLayers[layerCount];
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
	uint32_t availableLayerCount = sizeof(availableLayers) / sizeof(availableLayers[0]);

	for (uint32_t z = 0; z < availableLayerCount; z++) 
	{
		ano_debug_log(ANO_INFO, "Layer %u: %s", z, availableLayers[z].layerName);
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

