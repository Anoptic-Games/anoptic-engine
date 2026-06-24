/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <math.h>
#include <mimalloc.h>
#include <mimalloc-override.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"

VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *availableFormats, uint32_t formatCount);
VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *availablePresentModes, uint32_t presentModesCount, uint32_t preferredMode);
VkExtent2D chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities, GLFWwindow* window);

#include "vulkan_backend/vulkanMaster.h"



// Variables

// Hard-required device extensions. VK_EXT_mesh_shader is NOT here: it is optional
// and appended dynamically in createLogicalDevice when the device supports it.
// Devices without it take the vertex-shader fallback path.
static const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME }; // Should absolutely not be here, make dynamic and determined at runtime

// Vulkan component initialization functions

void enumerateMonitors(Monitors* monitors) // Instance creation helper
{
	GLFWmonitor** glfwMonitors = glfwGetMonitors(&(monitors->monitorCount));
	monitors->monitorInfos = mi_malloc(monitors->monitorCount * sizeof(MonitorInfo));
	for (int i = 0; i < monitors->monitorCount; i++) 
	{
		monitors->monitorInfos[i].modes = glfwGetVideoModes(glfwMonitors[i], &(monitors->monitorInfos[i].modeCount));
	}
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) // Called by GLFW on window resize, not part of instance creation but related
{
	static uint32_t count = 0;
	// VulkanContext* ctx = glfwGetWindowUserPointer(window);
	printf("Resize: %d\n", count);
	count++;
	rendererState.framebufferResized = true;
}

// L cycles the lighting mode: shadow maps -> hybrid (RC point + shadow-mapped dir/spot) ->
// radiance cascades. Edge-triggered on press so one keystroke advances one mode. The setter just
// updates the render state; updateCullingBuffers publishes it into the GlobalUBO. See AnoLightingMode
// and docs/artifacts/RADIANCE_CASCADES.md. Runs on the main (render) thread alongside the frame loop.
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)window; (void)scancode; (void)mods;
	if (key == GLFW_KEY_L && action == GLFW_PRESS) {
		AnoLightingMode next = (AnoLightingMode)(((uint32_t)ano_render_get_lighting_mode() + 1u) % (uint32_t)ANO_LIGHTING_MODE_COUNT);
		ano_render_set_lighting_mode(next);
		static const char* const names[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RADIANCE_CASCADES" };
		printf("Lighting mode: %s\n", names[next]);
	}
	// V cycles the RC debug view: 0 off, 1 voxel albedo, 2 gathered irradiance. Only visible in an
	// RC mode (HYBRID/RC), where the volumes are populated. See RADIANCE_CASCADES.md M3.
	if (key == GLFW_KEY_V && action == GLFW_PRESS) {
		rendererState.debugView = (rendererState.debugView + 1u) % 3u;
		static const char* const dbg[3] = { "off", "voxel albedo", "irradiance" };
		printf("RC debug view: %s\n", dbg[rendererState.debugView]);
	}
	// [ and ] scrub the RC GI ambient gain live (calibration knob, RADIANCE_CASCADES.md R13).
	// Hold-repeat enabled (PRESS|REPEAT); clamped to [0, 16].
	if ((key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET) && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		float step = (key == GLFW_KEY_RIGHT_BRACKET) ? 0.25f : -0.25f;
		rendererState.giStrength += step;
		if (rendererState.giStrength < 0.0f) rendererState.giStrength = 0.0f;
		if (rendererState.giStrength > 16.0f) rendererState.giStrength = 16.0f;
		printf("RC GI strength: %.2f\n", (double)rendererState.giStrength);
	}
}

GLFWwindow* initWindow(VulkanContext* ctx, Monitors* monitors) // Initializes a GLFW window, necessary for instance creation but general in scope
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
	
	glfwSetWindowUserPointer(window, &rendererState);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	glfwSetKeyCallback(window, keyCallback); // L cycles the lighting mode (RADIANCE_CASCADES.md)

	return window;
}

VkResult createInstance(VulkanContext* ctx) // Central component of the init process, should be generalized and given packages of selected extensions, parameters etc
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

	#ifdef __APPLE__
	// MoltenVK is a non-conformant (portability) driver. The loader only
	// enumerates it when this flag is set; the matching VK_KHR_portability_enumeration
	// instance extension is requested in getRequiredExtensions().
	createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	#endif

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

	if (vkCreateInstance(&createInfo, NULL, &(ctx->instance)) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create Vulkan instance!\n");
		free(extensions);
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	#ifdef DEBUG_BUILD
		setupDebugMessenger(&(ctx->instance), &(ctx->debugMessenger));
	#endif

	free(extensions);

	return VK_SUCCESS;
}

uint32_t g_ValidationErrors = 0;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( // Validation messenger callback: prints VALIDATION/PERFORMANCE messages, counts WARNING+ into g_ValidationErrors (GENERAL is dropped)
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

void setupDebugMessenger(VkInstance* instance, VkDebugUtilsMessengerEXT* debugMessenger) // Installs the standalone steady-state messenger; instance create/destroy is covered separately via VkInstanceCreateInfo.pNext
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

	#ifdef __APPLE__
	// Pairs with VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR so the loader
	// will surface the MoltenVK portability driver.
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

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device) // Selects capabilities required for the instance, extend with checks and error states
{
	struct DeviceCapabilities capabilities;

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &meshShaderFeatures;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;

	vkGetPhysicalDeviceFeatures2(device, &features2);

	//Device features checks
	capabilities.float64 = features2.features.shaderFloat64;
	capabilities.int64 = features2.features.shaderInt64;
	capabilities.drawIndirectCount = features12.drawIndirectCount;
	// Drivers leave meshShaderFeatures untouched when VK_EXT_mesh_shader is absent,
	// so a true value here implies both the extension and the feature are usable.
	capabilities.meshShader = meshShaderFeatures.meshShader;
	// Test hook: force the vertex-shader fallback path on mesh-capable hardware.
	if (getenv("ANO_FORCE_NO_MESH_SHADER")) capabilities.meshShader = false;

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

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR *surface) // Greatly extend and integrate with device capability checks, expose via interface
{
	struct QueueFamilyIndices indices = findQueueFamilies(device, surface);
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	bool queueRequirements = indices.graphicsPresent;

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {};
	dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	dynamicRenderingFeature.pNext = &meshShaderFeatures;

	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &dynamicRenderingFeature;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;

	vkGetPhysicalDeviceFeatures2(device, &features2);

	// geometryShader and shaderFloat64 are intentionally NOT required: no shader
	// stage or pipeline in the engine consumes them. Apple Silicon / MoltenVK
	// exposes neither (Metal has no geometry-shader stage and no fp64), and
	// gating on them would reject otherwise-capable devices for no reason.
	bool physicalRequirements = features2.features.shaderInt64 && features2.features.samplerAnisotropy;

	// Check specifically required Vulkan 1.2, dynamic rendering, and mesh shader features
	bool requiredFeatures12 = features12.descriptorIndexing &&
	                          features12.shaderSampledImageArrayNonUniformIndexing &&
	                          features12.runtimeDescriptorArray &&
	                          features12.descriptorBindingPartiallyBound &&
	                          features12.descriptorBindingVariableDescriptorCount &&
	                          features12.descriptorBindingSampledImageUpdateAfterBind;
	bool requiredDynamicRendering = dynamicRenderingFeature.dynamicRendering;
	bool requiredMultiDraw = features2.features.multiDrawIndirect;

	// Mesh shader is preferred but NOT required: devices without it render via the
	// vertex-shader fallback path. Everything below is needed by BOTH paths.
	if (!requiredFeatures12 || !requiredDynamicRendering || !requiredMultiDraw) {
		fprintf(stderr, "Device lacks required Vulkan 1.2, dynamic rendering, or multiDrawIndirect features.\n");
		return false;
	}
	if (!meshShaderFeatures.meshShader) {
		fprintf(stderr, "Device lacks VK_EXT_mesh_shader: will use the vertex-shader fallback path.\n");
		// The vertex fallback packs the draw ordinal into VkDrawIndexedIndirectCommand.firstInstance
		// (read as gl_InstanceIndex); a nonzero firstInstance in an indirect draw requires
		// drawIndirectFirstInstance. A device with neither mesh shaders nor this feature would
		// mis-draw, so reject it here instead of failing silently. The mesh path needs neither.
		if (!features2.features.drawIndirectFirstInstance) {
			fprintf(stderr, "Device also lacks drawIndirectFirstInstance: the vertex fallback path "
			                "cannot draw correctly. Device not suitable.\n");
			return false;
		}
	}

	return physicalRequirements && queueRequirements && extensionsSupported;
}

VkSampleCountFlagBits getMaxUsableSampleCount(VulkanContext* ctx)
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &physicalDeviceProperties); // Cached properties should be preferentially used

	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

bool pickPhysicalDevice(VulkanContext* ctx, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice) // Further extend selection logic, split device discovery into dedicated function
{																																				 //   and retain device attributes in public interface for use in UI or logic
	bool foundPreferredDevice = false;
	ctx->deviceCount = 0;

	vkEnumeratePhysicalDevices(ctx->instance, &(ctx->deviceCount), NULL);

	if (ctx->deviceCount == 0) 
	{
		fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
		return false;
	}

	// Allocate memory for the names of every detected device
	ctx->availableDevices = (char**)mi_malloc(sizeof(char*) * ctx->deviceCount);

	VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(1, sizeof(VkPhysicalDevice) * ctx->deviceCount);

	vkEnumeratePhysicalDevices(ctx->instance, &ctx->deviceCount, devices);
	
	VkPhysicalDeviceProperties deviceProperties;
	// VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties memProperties;

	VkDeviceSize maxDedicatedMemory = 0;
	VkDeviceSize maxIntegratedMemory = 0;

	VkPhysicalDevice bestDedicatedDevice = VK_NULL_HANDLE;
	VkPhysicalDevice bestIntegratedDevice = VK_NULL_HANDLE;

	printf("DeviceCount: %d\n", ctx->deviceCount);
	
	for (uint32_t i = 0; i < ctx->deviceCount; i++)
	{
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
		vkGetPhysicalDeviceMemoryProperties(devices[i], &memProperties);
		printf("%d\n", i);

		if (isDeviceSuitable(devices[i], &(ctx->surface)))
		{

			//Select the first preffered device if available and break out of the selection loop
			if (strcmp(deviceProperties.deviceName, preferredDevice) == 0)
			{
				ctx->physicalDevice = devices[i];
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
			(ctx->availableDevices)[i] = (char*)mi_malloc(strlen(deviceProperties.deviceName) +1);
			strcpy((ctx->availableDevices)[i], deviceProperties.deviceName);
			#ifdef DEBUG_BUILD
			printf("%s\n", ctx->availableDevices[i]);
			#endif
		}
	}

	if (foundPreferredDevice)
	{
		ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
		ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
	}
	else
	{

		if (bestDedicatedDevice != VK_NULL_HANDLE)
		{
			ctx->physicalDevice = bestDedicatedDevice;
			ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
			ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
		}
		else if (bestIntegratedDevice != VK_NULL_HANDLE)
		{
			ctx->physicalDevice = bestIntegratedDevice;
			ctx->deviceCapabilities = populateCapabilities(ctx->physicalDevice);
			ctx->queueFamilyIndices = findQueueFamilies(ctx->physicalDevice, &(ctx->surface));
		}
		else
		{
			fprintf(stderr, "Failed to find a suitable GPU!\n");
			free(devices);
			return false;
		}
	}

	ctx->msaaSamples = getMaxUsableSampleCount(ctx);
	printf("MSAA samples used: %d\n", ctx->msaaSamples);

	//printf("Graphics family: %d\nCompute family: %d\nTransfer family: %d\nPresent family: %d\n", (ctx->queueFamilyIndices.graphicsFamily), (ctx->queueFamilyIndices.computeFamily), (ctx->queueFamilyIndices.transferFamily), (ctx->queueFamilyIndices.presentFamily));

	free(devices);
	return true;
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices)
{
	// Query supported features via vkGetPhysicalDeviceFeatures2
	VkPhysicalDeviceMeshShaderFeaturesEXT queryMeshShaderFeatures = {};
	queryMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceDynamicRenderingFeaturesKHR queryDynamicRendering = {};
	queryDynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	queryDynamicRendering.pNext = &queryMeshShaderFeatures;

	VkPhysicalDeviceVulkan11Features queryFeatures11 = {};
	queryFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	queryFeatures11.pNext = &queryDynamicRendering;

	VkPhysicalDeviceVulkan12Features queryFeatures12 = {};
	queryFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	queryFeatures12.pNext = &queryFeatures11;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &queryFeatures12;

	vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.shaderInt64 = features2.features.shaderInt64;
	deviceFeatures.shaderFloat64 = features2.features.shaderFloat64;
	deviceFeatures.samplerAnisotropy = features2.features.samplerAnisotropy;
	deviceFeatures.multiDrawIndirect = features2.features.multiDrawIndirect;
	deviceFeatures.geometryShader = features2.features.geometryShader;
	// Vertex fallback path packs the draw ordinal into VkDrawIndexedIndirectCommand.firstInstance
	// (read as gl_InstanceIndex), which requires this feature for a nonzero value in an indirect draw.
	deviceFeatures.drawIndirectFirstInstance = features2.features.drawIndirectFirstInstance;
	// voxelize.frag imageStores into the 3D voxel volumes from the fragment stage (RADIANCE_CASCADES.md M2).
	deviceFeatures.fragmentStoresAndAtomics = features2.features.fragmentStoresAndAtomics;
	// rc_trace/merge/integrate index the cascade storage-image array by a push-constant level (M4).
	deviceFeatures.shaderStorageImageArrayDynamicIndexing = features2.features.shaderStorageImageArrayDynamicIndexing;

	// We'll have 4 unique queues at the very most
	VkDeviceQueueCreateInfo queueCreateInfos[4];
	uint32_t uniqueQueueFamilies[4] = {indices->graphicsFamily, indices->presentFamily, indices->computeFamily, indices->transferFamily};
	uint32_t queueCount = 0;
	// Function-scoped so its address stays valid until vkCreateDevice; every queue
	// shares priority 1.0. A block-local would dangle once each loop iteration's
	// scope exits, leaving pQueuePriorities pointing at dead stack (stack-use-after-scope).
	const float queuePriority = 1.0f;
	
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
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos[queueCount] = queueCreateInfo;
			queueCount++;
		}
	}
	
	// creating the device
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	// Only request what the device actually supports based on the query
	features12.descriptorIndexing = queryFeatures12.descriptorIndexing;
	features12.shaderSampledImageArrayNonUniformIndexing = queryFeatures12.shaderSampledImageArrayNonUniformIndexing;
	features12.runtimeDescriptorArray = queryFeatures12.runtimeDescriptorArray;
	features12.descriptorBindingPartiallyBound = queryFeatures12.descriptorBindingPartiallyBound;
	features12.descriptorBindingVariableDescriptorCount = queryFeatures12.descriptorBindingVariableDescriptorCount;
	features12.descriptorBindingSampledImageUpdateAfterBind = queryFeatures12.descriptorBindingSampledImageUpdateAfterBind;
	features12.drawIndirectCount = queryFeatures12.drawIndirectCount;

	// Mirror populateCapabilities: the fallback path activates when the feature is
	// absent or the test override forces it off.
	bool meshSupported = queryMeshShaderFeatures.meshShader && !getenv("ANO_FORCE_NO_MESH_SHADER");

	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	meshShaderFeatures.taskShader = queryMeshShaderFeatures.taskShader;
	meshShaderFeatures.meshShader = queryMeshShaderFeatures.meshShader;
	meshShaderFeatures.multiviewMeshShader = VK_FALSE;
	meshShaderFeatures.primitiveFragmentShadingRateMeshShader = VK_FALSE;
	meshShaderFeatures.meshShaderQueries = queryMeshShaderFeatures.meshShaderQueries;

	VkPhysicalDeviceVulkan11Features features11 = {};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.shaderDrawParameters = queryFeatures11.shaderDrawParameters; // flat.vert uses gl_DrawID -> SPIR-V DrawParameters cap

	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {};
	dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
	dynamicRenderingFeature.dynamicRendering = queryDynamicRendering.dynamicRendering;

	// Only chain the mesh-shader feature struct when we will actually enable the
	// extension; chaining it otherwise is invalid usage on non-mesh devices.
	dynamicRenderingFeature.pNext = meshSupported ? (void*)&meshShaderFeatures : NULL;
	features11.pNext = &dynamicRenderingFeature; // 1.1 features -> dynamic rendering -> [mesh shader] -> NULL
	features12.pNext = &features11;

	VkDeviceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &features12;
	createInfo.queueCreateInfoCount = queueCount;
	createInfo.pQueueCreateInfos = queueCreateInfos;
	createInfo.pEnabledFeatures = &deviceFeatures;

	// Build the enabled-extension list: the hard-required set, plus VK_EXT_mesh_shader
	// only when the device supports it (and the fallback is not forced).
	uint32_t requiredExtensionCount = sizeof(requiredExtensions) / sizeof(requiredExtensions[0]);
	const char* enabledExtensions[8];
	uint32_t enabledExtensionCount = 0;
	for (uint32_t i = 0; i < requiredExtensionCount; i++)
		enabledExtensions[enabledExtensionCount++] = requiredExtensions[i];
	if (meshSupported)
		enabledExtensions[enabledExtensionCount++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;
	#ifdef __APPLE__
	// Vulkan spec: when a physical device exposes VK_KHR_portability_subset it
	// MUST be enabled, or vkCreateDevice fails. MoltenVK always exposes it.
	// String literal avoids pulling in the beta-gated vulkan_beta.h header.
	enabledExtensions[enabledExtensionCount++] = "VK_KHR_portability_subset";
	#endif

	createInfo.enabledExtensionCount = enabledExtensionCount;
	createInfo.ppEnabledExtensionNames = enabledExtensions;
	printf("Enabling %u device extensions (mesh shader: %s)\n", enabledExtensionCount, meshSupported ? "yes" : "no");

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


VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *availableFormats, uint32_t formatCount) 
{
    for (uint32_t i = 0; i < formatCount; i++) 
    {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB && availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
        {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *availablePresentModes, uint32_t presentModesCount, uint32_t preferredMode) 
{
    for (uint32_t i = 0; i < presentModesCount; i++) 
    {
        if (availablePresentModes[i] == preferredMode) 
        {
            return availablePresentModes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
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

bool initSwapChain(VulkanContext* ctx, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state) // Selects and initializes a swap chain
{
    struct SwapChainSupportDetails details = querySwapChainSupport(ctx->physicalDevice, &(ctx->surface));

    VkSurfaceFormatKHR chosenFormat = chooseSwapSurfaceFormat(details.formats, details.formatCount);
    VkPresentModeKHR chosenPresentMode = chooseSwapPresentMode(details.presentModes, details.presentModesCount, preferredMode);
    VkExtent2D chosenExtent = chooseSwapExtent(details.capabilities, window);

    uint32_t imageCount = details.capabilities.minImageCount + 1; // It is recommended to request one more image than the minimum
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) 
    { // If maximum image count is 0, it means there is no strict upper limit
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx->surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = chosenExtent;
    createInfo.imageArrayLayers = 1; // Always 1 unless developing stereoscopic 3D
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = ctx->queueFamilyIndices;
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

    if (indices.graphicsFamily != indices.presentFamily) 
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT; // Use concurrent sharing mode if graphics and present queues are different
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } 
    else 
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // Use exclusive mode if graphics and present queues are the same
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = NULL; // Optional
    }

    createInfo.preTransform = details.capabilities.currentTransform; // Don"t apply any pre-transformation to the image (e.g. rotation)
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Ignore alpha channel
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE; // Discard pixels that are obscured, for example because another window is in front of them
    createInfo.oldSwapchain = oldSwapChain; // When a swap chain is recreated, the old one must be passed here to aid in resource transfer

    VkSwapchainKHR swapChain;
    if (vkCreateSwapchainKHR(ctx->device, &createInfo, NULL, &swapChain) != VK_SUCCESS) 
    {
        return false;
    }

    // Get the array of swap chain images
    vkGetSwapchainImagesKHR(ctx->device, swapChain, &imageCount, NULL);
    VkImage* swapChainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, swapChain, &imageCount, swapChainImages);

    state->swapChain = swapChain;
    state->imageFormat = chosenFormat.format;
    state->imageExtent = chosenExtent;
    state->imageCount = imageCount;
    state->images = swapChainImages;

    return true;
}


void cleanupSwapChain(VulkanContext* ctx, RendererState* state)
{
    // Destroy swapchain image views
    for (size_t i = 0; i < state->viewCount; i++) 
    {
        vkDestroyImageView(ctx->device, state->views[i], NULL);
    }
    free(state->views);
    state->views = NULL;
    state->viewCount = 0;

    // Destroy per-view depth views + images
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            if (vr->depthView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->depthView, NULL);
                vr->depthView = VK_NULL_HANDLE;
            }
            if (vr->depthImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->depthImage, NULL);
                vr->depthImage = VK_NULL_HANDLE;
            }
        }
    }


    // Free color image
    if (state->colorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(ctx->device, state->colorImage, NULL);
        state->colorImage = VK_NULL_HANDLE;
    }

    // Free color image memory (managed by swapchainAllocator, so no manual vkFreeMemory)
    if (state->colorImageAlloc.memory != VK_NULL_HANDLE)
    {
        state->colorImageAlloc.memory = VK_NULL_HANDLE;
    }

    // Destroy color image view
    if (state->colorView)
    {
        vkDestroyImageView(ctx->device, state->colorView, NULL);
        state->colorView = VK_NULL_HANDLE;
    }

    // Destroy per-view HDR resolve targets (memory backed by swapchainAllocator, reset below)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            if (vr->hdrColorView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx->device, vr->hdrColorView, NULL);
                vr->hdrColorView = VK_NULL_HANDLE;
            }
            if (vr->hdrColorImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx->device, vr->hdrColorImage, NULL);
                vr->hdrColorImage = VK_NULL_HANDLE;
            }
            vr->hdrColorAlloc.memory = VK_NULL_HANDLE;
        }
    }

    // Do NOT destroy the swapchain here because recreateSwapChain needs oldSwapChain
    if (state->images != NULL) {
        free(state->images);
        state->images = NULL;
    }
    
    
    gpu_alloc_reset(&swapchainAllocator);
}

void recreateSwapChain(VulkanContext* ctx, GLFWwindow* window)
{
	// Wait until the device is completely idle before tearing down any resources
	vkDeviceWaitIdle(ctx->device);

	// This is completely unecessary and introduces a bug on reinit.
	// for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
	// 	vkDestroySemaphore(ctx->device, rendererState.frames[i].imageAvailable, NULL);
	// 	vkDestroySemaphore(ctx->device, rendererState.frames[i].renderFinished, NULL);
		
	// 	VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	// 	vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &rendererState.frames[i].imageAvailable);
	// 	vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &rendererState.frames[i].renderFinished);
	// }

    // First, clean up the previous swapchain
	cleanupSwapChain(ctx, &rendererState);
    
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0)
	{
		printf("Sleeping!\n");
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}
    // Explicitly update these to ensure swapchain recreation has the correct values
    rendererState.imageExtent.width = width;
    rendererState.imageExtent.height = height;
	// Save outdated swapchain
	VkSwapchainKHR oldSwapChain = rendererState.swapChain;
	rendererState.swapChain = VK_NULL_HANDLE;

	initSwapChain(ctx, window, getChosenPresentMode(), oldSwapChain, &rendererState);

	// Destroy old swapchain
	vkDestroySwapchainKHR(ctx->device, oldSwapChain, NULL);

	if(rendererState.swapChain == VK_NULL_HANDLE)
	{
		printf("Swap chain re-creation error, exiting!\n");
		cleanupVulkan(ctx);
		exit(1);
	}
	createImageViews(ctx, &rendererState);
	if (rendererState.views == NULL)
	{
		printf("View group re-creation error, exiting!\n");
		cleanupVulkan(ctx);
		exit(1);
	}

	createColorResources(ctx);

	createDepthResources(ctx, &rendererState);
	if (rendererState.frames[0].views[0].depthView == NULL)
	{
		printf("Depth resources re-creation error, exiting!\n");
		cleanupVulkan(ctx);
		exit(1);
	}

	// The per-view HDR resolve views were recreated above; rebind the tonemap sets to them.
	updateTonemapDescriptorSets(ctx, &rendererState);

	vkResetCommandPool(ctx->device, rendererState.commandPool, 0);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) // Clear fences prior to resuming render
	{
		vkResetFences(ctx->device, 1, &(rendererState.frames[i].frameFence));
        rendererState.frames[i].frameSubmitted = false;
	}

	rendererState.framebufferResized = false;
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


bool createImageViews(VulkanContext* ctx, RendererState* state)
{
    state->views = (VkImageView*)malloc(state->imageCount * sizeof(VkImageView));
    state->viewCount = state->imageCount;

    for (uint32_t i = 0; i < state->imageCount; i++) 
    {
        state->views[i] = createImageView(ctx->device, state->images[i], state->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
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

bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, buffer) != VK_SUCCESS)
	{
		printf("Failed to create data buffer!");
		return false;
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(ctx->device, *buffer, &memRequirements);

	*allocation = gpu_alloc(allocator, memRequirements, properties);
	if (allocation->memory == VK_NULL_HANDLE) {
		vkDestroyBuffer(ctx->device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE; // don't leave a dangling handle for teardown to double-free
		return false;
	}
	vkBindBufferMemory(ctx->device, *buffer, allocation->memory, allocation->offset);

	return true;
}



bool createUniformBuffers(VulkanContext* ctx, RendererState* state)
{ // Central to init, specific to perspective uniforms (world translation, rotation and projection)
	VkDeviceSize bufferSize = sizeof(GlobalUBO);

	// One camera UBO per view per frame: each view has its own view/proj/froxel params.
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			GpuAllocation alloc;
			if (!createDataBuffer(ctx, &gpuAllocator, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &(rendererState.frames[i].views[v].uniformBuffer), &alloc))
			{
				printf("Failed to create uniform buffer!");
				return false;
			}

			rendererState.frames[i].views[v].uniformMapped = alloc.mapped;
		}
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

bool updateUniformBuffer(VulkanContext* ctx, RendererState* state)
{ // Changes the perspective (camera) parameters applied to the world-space for a given frame. Should be generalized with its parameters exposed via an interface
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	static uint64_t startTime = 0;
	static uint32_t frameCount = 0;

	time = ano_timestamp_us();
	if (startTime == 0) {
		startTime = time;
		oldTime = time;
	}

	float deltaTime = (time - oldTime) / 1000000.0f;
	float elapsedTime = (time - startTime) / 1000000.0f;

	float center[] = {0.0f, 0.15f, 0.0f}; // both cameras look at the scene origin
	float up[]     = {0.0f, 1.0f, 0.0f};  // world is unflipped

	// Shared projection params. Both views render at full resolution (the inset is composited
	// smaller), so they share the screen extent and froxel grid; only the camera differs.
	float fov = 45.0f; // degrees
	float aspect = (float)state->imageExtent.width / (float)state->imageExtent.height;
	float near = 0.1f;
	float far = 100.0f;

	// Build each view's camera (audit 4.8). View 0 is the main camera; view 1 is an orbiting
	// "second feed" composited as a picture-in-picture inset, proving the multi-view path.
	for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
	{
		GlobalUBO* u = &state->uboData[v];
		u->time = elapsedTime;
		u->deltaTime = deltaTime;
		u->frameCount = frameCount;

		float eye[3];
		if (v == 0) {
			eye[0] = 0.0f; eye[1] = 0.9f; eye[2] = 3.5f;   // main: up and back
		} else {
			// Inset: orbit the scene at a higher angle so the feed is obviously live + distinct.
			float a = elapsedTime * 0.5f;
			float r = 4.0f;
			eye[0] = r * sinf(a);
			eye[1] = 2.5f;
			eye[2] = r * cosf(a);
		}

		lookAt(u->view, eye, center, up);

		// Publish the camera world position so the fragment stage doesn't have to
		// recover it via a per-fragment inverse(view).
		u->cameraPos[0] = eye[0];
		u->cameraPos[1] = eye[1];
		u->cameraPos[2] = eye[2];
		u->cameraPos[3] = 1.0f;

		perspective(u->proj, fov, aspect, near, far);

		// Clustered-forward froxel params: near/far + screen size (the light-cull pass and the
		// fragment shader reconstruct froxels from these), and the fixed grid dimensions.
		u->cameraNear = near;
		u->cameraFar = far;
		u->screenWidth = (float)state->imageExtent.width;
		u->screenHeight = (float)state->imageExtent.height;
		u->clusterDimX = ANO_CLUSTER_X;
		u->clusterDimY = ANO_CLUSTER_Y;
		u->clusterDimZ = ANO_CLUSTER_Z;
		u->maxLightsPerCluster = ANO_CLUSTER_MAX_LIGHTS;

		memcpy(state->frames[state->frameIndex].views[v].uniformMapped, u, sizeof(GlobalUBO));
	}
	frameCount++;

	oldTime = time;

	return true;
}

bool updateMeshTransforms(VulkanContext* ctx, RenderEntity* entity, float move)
{
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	time = ano_timestamp_us();
	static float angle = 0.0f;
	const float pi = 3.14159265359f;

	// Initialize transform matrix to identity
	for(int i = 0; i < 4; i++)
	{
		for(int j = 0; j < 4; j++)
		{
			entity->transform[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	translate(entity->transform, move, 0.0f, 0.0f);
	rotateMatrix(entity->transform, 'Y', angle);

	// Update angle for next frame
	angle += ((float)(time - oldTime)) * 0.000001f;
	if (angle > 2.0f * pi)
	{
		angle = 0.0f;
	}
	oldTime = time;

	return true;
}

VkFormat findSupportedFormat(VulkanContext* ctx, const VkFormat* candidates, uint32_t candidateCount, VkImageTiling tiling, VkFormatFeatureFlags features)
{ // Returns a device-supported format from a list of candidates, currently only used in pipeline images but should be used every time an image is created
	for (int i = 0; i < candidateCount; i++)
	{
		VkFormat format = candidates[i];
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice, format, &props);

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
VkFormat findDepthFormat(VulkanContext* ctx)
{
	VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
	return(findSupportedFormat(ctx, &candidates[0], sizeof(candidates)/sizeof(VkFormat),
								VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
}

bool hasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


bool createDepthResources(VulkanContext* ctx, RendererState* state)
{
	VkFormat depthFormat = findDepthFormat(ctx);
	if (depthFormat == VK_FORMAT_UNDEFINED)
	{
		printf("No compatible depth formats detected!\n");
		return false;
	}
	state->depthFormat = depthFormat;

	// One depth target per view per frame: each view's frustum has its own visibility/depth.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			ViewResources* vr = &state->frames[i].views[v];
			if (!createImage(ctx, &swapchainAllocator, state->imageExtent.width,
				state->imageExtent.height, 1, ctx->msaaSamples, state->depthFormat,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							 &vr->depthImage, &vr->depthAlloc, false))
			{
				printf("Failed to create depth resource for frame %d view %u!\n", i, v);
				return false;
			}

			vr->depthView = createImageView(ctx->device, vr->depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

			if(!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->depthImage, depthFormat,
									  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1))
			{
				printf("Failed to transition depth buffer layout for frame %d view %u!\n", i, v);
				return false;
			}
		}
	}
	return true;
}

void createColorResources(VulkanContext* ctx) //TODO: This probably should be generalized later?
{
	// Geometry renders into an HDR float MSAA target (was the swapchain LDR format), so
	// many-light dynamic range is preserved; a fullscreen tonemap pass encodes to the
	// swapchain afterwards. The MSAA target stays transient (resolved, never stored).
	VkFormat colorFormat = ANO_HDR_COLOR_FORMAT;

	createImage(ctx, &swapchainAllocator, rendererState.imageExtent.width, rendererState.imageExtent.height,
		1, ctx->msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &rendererState.colorImage, &rendererState.colorImageAlloc, false);
	rendererState.colorView = createImageView(ctx->device, rendererState.colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	if (!transitionImageLayout(ctx, VK_NULL_HANDLE, rendererState.colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1))
	{
		printf("Failed to transition color image layout!\n");
	}

	// Single-sample HDR resolve target, one per view per frame: resolve destination
	// (COLOR_ATTACHMENT) for that view's MSAA HDR pass, and composite source (SAMPLED). Not
	// transient — the composite reads it back. Seeded to SHADER_READ_ONLY; recordCommandBuffer
	// re-transitions per frame. The shared MSAA color above is reused across views sequentially.
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			ViewResources* vr = &rendererState.frames[i].views[v];
			createImage(ctx, &swapchainAllocator, rendererState.imageExtent.width, rendererState.imageExtent.height,
				1, VK_SAMPLE_COUNT_1_BIT, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vr->hdrColorImage, &vr->hdrColorAlloc, false);
			vr->hdrColorView = createImageView(ctx->device, vr->hdrColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
			if (!transitionImageLayout(ctx, VK_NULL_HANDLE, vr->hdrColorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1))
			{
				printf("Failed to transition HDR resolve image layout!\n");
			}
		}
	}
}

bool createDescriptorPool(VulkanContext* ctx, RendererState* state)
{ // Central to init
	// Per-view sets (global, light-cull, tonemap) scale by ANO_VIEW_COUNT; the view-independent
	// sets (cull, update, scatter) are one per frame. Counts below are per frame, times margin.
	//   UBO/frame:     global(1) + light-cull(1) per view, + cull(1) + update(1) shared
	//   SSBO/frame:    global(11: bindings 1-11) + light-cull(4) per view, + cull(8)+update(3)+scatter(2) shared
	//   SAMPLER/frame: tonemap(1) per view
	//   sets/frame:    (global+light-cull+tonemap) per view + cull+update+scatter shared
	VkDescriptorPoolSize poolSize[5] = {};
	poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize[0].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (2u * ANO_VIEW_COUNT + 4u);
	// Shadows (audit 4.7) add per frame: cull binding 9 (1 SSBO) + shadowsetup set (4 SSBO) +
	// shadow geom set (2 SSBO + 1 sampler), and 2 extra sets.
	poolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize[1].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (15u * ANO_VIEW_COUNT + 16u + 7u);
	poolSize[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	poolSize[2].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 1; // scatter binding 1: xform ring slice
	poolSize[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	// tonemap (per view) + shadow atlas + RC voxel (binding 12) + RC irradiance (binding 13) per global set.
	poolSize[3].descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * (3u * ANO_VIEW_COUNT + 2u);
	// Radiance cascades (RADIANCE_CASCADES.md): storage images for the ×1 shared volumes (M1: the
	// scene voxel; headroom for the cascade ping-pong / irradiance / history added in M2-M4).
	poolSize[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSize[4].descriptorCount = 16u;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 5;
	poolInfo.pPoolSizes = poolSize;
	// + 4 non-per-frame RC sets (×1 shared volumes): M2 allocates 1 (rcVoxelizeSet), M3-M4 the rest.
	poolInfo.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT * (3u * ANO_VIEW_COUNT + 6u) + 4u;

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &(rendererState.globalDescriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}

bool createBindlessTextureArray(VulkanContext* ctx, RendererState* state)
{
	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = rendererState.bindlessTextures.maxTextures;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &rendererState.bindlessTextures.pool) != VK_SUCCESS)
	{
		printf("Failed to create bindless texture descriptor pool!\n");
		return false;
	}

	VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo = {};
	variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
	variableInfo.descriptorSetCount = 1;
	variableInfo.pDescriptorCounts = &rendererState.bindlessTextures.maxTextures;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = &variableInfo;
	allocInfo.descriptorPool = rendererState.bindlessTextures.pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &rendererState.bindlessTextures.layout;

	if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &rendererState.bindlessTextures.set) != VK_SUCCESS)
	{
		printf("Failed to allocate bindless texture descriptor set!\n");
		return false;
	}

	return true;
}

bool createDescriptorSets(VulkanContext* ctx, RendererState* state)
{ // Central to init, !TODO modify this to account for multiple descriptor sets, for multiple meshes
	// Global, light-cull and tonemap sets are per view per frame (each binds that view's
	// camera UBO / froxel lists / HDR target); cull, update and scatter are one per frame.
	enum { PERVIEW_SETS = MAX_FRAMES_IN_FLIGHT * ANO_VIEW_COUNT };

	VkDescriptorSetLayout layouts[PERVIEW_SETS];
	for (int i = 0; i < PERVIEW_SETS; ++i)
	{
		layouts[i] = rendererState.globalSetLayout;
	}
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.descriptorPool = rendererState.globalDescriptorPool;
	allocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
	allocInfo.pSetLayouts = layouts;

		VkDescriptorSet globalSetsTemp[PERVIEW_SETS];
	if (vkAllocateDescriptorSets(ctx->device, &allocInfo, globalSetsTemp) != VK_SUCCESS)
	{
        printf("Failed to allocate global descriptor sets!\n");
		return false;
	}
	for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++)
		for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
			rendererState.frames[i].views[v].globalSet = globalSetsTemp[i*ANO_VIEW_COUNT + v];

    VkDescriptorSetLayout cullLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        cullLayouts[i] = rendererState.culling.setLayout;
    }
    VkDescriptorSetAllocateInfo cullAllocInfo = {};
    cullAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    cullAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    cullAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    cullAllocInfo.pSetLayouts = cullLayouts;

        VkDescriptorSet cullSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &cullAllocInfo, cullSetsTemp) != VK_SUCCESS)
    {
        printf("Failed to allocate cull descriptor sets!\n");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].cullSet = cullSetsTemp[i];

    VkDescriptorSetLayout updateLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        updateLayouts[i] = rendererState.updateSetLayout;
    }
    VkDescriptorSetAllocateInfo updateAllocInfo = {};
    updateAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    updateAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    updateAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    updateAllocInfo.pSetLayouts = updateLayouts;

    VkDescriptorSet updateSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &updateAllocInfo, updateSetsTemp) != VK_SUCCESS)
    {
        printf("Failed to allocate update descriptor sets!\n");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].updateSet = updateSetsTemp[i];

    VkDescriptorSetLayout scatterLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        scatterLayouts[i] = rendererState.scatterSetLayout;
    }
    VkDescriptorSetAllocateInfo scatterAllocInfo = {};
    scatterAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    scatterAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    scatterAllocInfo.descriptorSetCount = (uint32_t)(MAX_FRAMES_IN_FLIGHT);
    scatterAllocInfo.pSetLayouts = scatterLayouts;

    VkDescriptorSet scatterSetsTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &scatterAllocInfo, scatterSetsTemp) != VK_SUCCESS)
    {
        printf("Failed to allocate scatter descriptor sets!\n");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) rendererState.frames[i].scatterSet = scatterSetsTemp[i];

    VkDescriptorSetLayout lightcullLayouts[PERVIEW_SETS];
    for (int i = 0; i < PERVIEW_SETS; ++i)
    {
        lightcullLayouts[i] = rendererState.lightcullSetLayout;
    }
    VkDescriptorSetAllocateInfo lightcullAllocInfo = {};
    lightcullAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightcullAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    lightcullAllocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
    lightcullAllocInfo.pSetLayouts = lightcullLayouts;

    VkDescriptorSet lightcullSetsTemp[PERVIEW_SETS];
    if (vkAllocateDescriptorSets(ctx->device, &lightcullAllocInfo, lightcullSetsTemp) != VK_SUCCESS)
    {
        printf("Failed to allocate light-cull descriptor sets!\n");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++)
        for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
            rendererState.frames[i].views[v].lightcullSet = lightcullSetsTemp[i*ANO_VIEW_COUNT + v];

    // Tonemap set (one per view per frame): samples that view's HDR resolve target. The image
    // view is (re)bound by updateTonemapDescriptorSets, which also runs after a swapchain resize
    // since the per-view hdrColorView handles change there.
    VkDescriptorSetLayout tonemapLayouts[PERVIEW_SETS];
    for (int i = 0; i < PERVIEW_SETS; ++i)
    {
        tonemapLayouts[i] = rendererState.tonemapSetLayout;
    }
    VkDescriptorSetAllocateInfo tonemapAllocInfo = {};
    tonemapAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    tonemapAllocInfo.descriptorPool = rendererState.globalDescriptorPool;
    tonemapAllocInfo.descriptorSetCount = (uint32_t)PERVIEW_SETS;
    tonemapAllocInfo.pSetLayouts = tonemapLayouts;

    VkDescriptorSet tonemapSetsTemp[PERVIEW_SETS];
    if (vkAllocateDescriptorSets(ctx->device, &tonemapAllocInfo, tonemapSetsTemp) != VK_SUCCESS)
    {
        printf("Failed to allocate tonemap descriptor sets!\n");
        return false;
    }
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        for(uint32_t v=0; v<ANO_VIEW_COUNT; v++)
            rendererState.frames[i].views[v].tonemapSet = tonemapSetsTemp[i*ANO_VIEW_COUNT + v];
    }

    // Shadow sets (audit 4.7): one shadowsetup set + one shadow geom/sampling set per frame.
    VkDescriptorSetLayout setupLayouts[MAX_FRAMES_IN_FLIGHT], geomLayouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) { setupLayouts[i] = rendererState.shadowSetupSetLayout; geomLayouts[i] = rendererState.shadowGeomSetLayout; }
    VkDescriptorSetAllocateInfo setupAlloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rendererState.globalDescriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = setupLayouts };
    VkDescriptorSet setupTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &setupAlloc, setupTemp) != VK_SUCCESS) { printf("Failed to allocate shadowsetup sets!\n"); return false; }
    VkDescriptorSetAllocateInfo geomAlloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rendererState.globalDescriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = geomLayouts };
    VkDescriptorSet geomTemp[MAX_FRAMES_IN_FLIGHT];
    if (vkAllocateDescriptorSets(ctx->device, &geomAlloc, geomTemp) != VK_SUCCESS) { printf("Failed to allocate shadow geom sets!\n"); return false; }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) { rendererState.frames[i].shadow.setupSet = setupTemp[i]; rendererState.frames[i].shadow.geomSet = geomTemp[i]; }

	return true;
}

// Binds the clustered-forward froxel buffers: global set bindings 10/11 (fragment reads) and
// the light-cull compute set bindings 0-4 (in: UBO/transforms/lights, out: count/index). The
// cluster buffers are fixed-size and not extent-dependent, so this runs once at init only.
void updateClusterDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    // Per view per frame: each view's froxel lists + camera UBO. transforms/lights are shared.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            VkDescriptorBufferInfo countInfo = { vr->clusterLightCountBuffer, 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo indexInfo = { vr->clusterLightIndexBuffer, 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo uboInfo   = { vr->uniformBuffer, 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo xformInfo = { state->transformBuffer.buffer[i], 0, VK_WHOLE_SIZE };
            VkDescriptorBufferInfo lightInfo = { state->lightBuffer.device, 0, VK_WHOLE_SIZE };

            // Global set 12 = RC scene voxel albedo sampler (debug view + GI). ×1 shared image,
            // sampled in SHADER_READ after the voxelize pass; the fragment only reads it when an RC
            // mode is active (so its UNDEFINED layout in SHADOWMAP mode is never accessed).
            VkDescriptorImageInfo rcVoxelInfo = { state->rcSampler, state->rcVoxelAlbedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo rcIrrInfo   = { state->rcSampler, state->rcIrradianceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet writes[9] = {};
            // Global set: 10 = cluster light count, 11 = cluster light index (fragment-read).
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = vr->globalSet;
            writes[0].dstBinding = 10;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &countInfo;
            writes[1] = writes[0];
            writes[1].dstBinding = 11;
            writes[1].pBufferInfo = &indexInfo;
            // Light-cull set: 0 UBO, 1 transforms, 2 lights, 3 count(out), 4 index(out).
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = vr->lightcullSet;
            writes[2].dstBinding = 0;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo = &uboInfo;
            writes[3] = writes[2];
            writes[3].dstBinding = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo = &xformInfo;
            writes[4] = writes[3];
            writes[4].dstBinding = 2;
            writes[4].pBufferInfo = &lightInfo;
            writes[5] = writes[3];
            writes[5].dstBinding = 3;
            writes[5].pBufferInfo = &countInfo;
            writes[6] = writes[3];
            writes[6].dstBinding = 4;
            writes[6].pBufferInfo = &indexInfo;
            // Global set 12: RC voxel albedo sampler (debug). 13: irradiance field sampler (GI ambient).
            writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[7].dstSet = vr->globalSet;
            writes[7].dstBinding = 12;
            writes[7].descriptorCount = 1;
            writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[7].pImageInfo = &rcVoxelInfo;
            writes[8] = writes[7];
            writes[8].dstBinding = 13;
            writes[8].pImageInfo = &rcIrrInfo;

            vkUpdateDescriptorSets(ctx->device, 9, writes, 0, NULL);
        }
    }
}

// Points each frame's tonemap set at its current HDR resolve view. Separate from
// updateUboDescriptorSets because it must also run after a swapchain resize, where the
// per-frame hdrColorView handles are destroyed and recreated.
// Binds the dynamic shadow sets (audit 4.7), per frame: the shadowsetup compute set (config +
// transforms + lights in, frustums out) and the shadow geom/sampling set (frustum viewProjs, the
// depth atlas array sampler, per-light info). Cluster buffers / config are fixed, so init-only.
void updateShadowDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        ShadowResources* sh = &state->frames[i].shadow;
        VkDescriptorBufferInfo cfgI  = { state->shadowFrustumConfigBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo xfI   = { state->transformBuffer.buffer[i], 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo ltI   = { state->lightBuffer.device, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo frI   = { sh->frustumBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo infoI = { state->shadowLightInfoBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo  atI   = { state->shadowSampler, sh->arrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkWriteDescriptorSet w[7] = {};
        // shadowsetup set (0 config, 1 transforms, 2 lights, 3 frustums-out) — all storage.
        for (int j = 0; j < 4; ++j) {
            w[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[j].dstSet = sh->setupSet;
            w[j].dstBinding = (uint32_t)j;
            w[j].descriptorCount = 1;
            w[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        w[0].pBufferInfo = &cfgI; w[1].pBufferInfo = &xfI; w[2].pBufferInfo = &ltI; w[3].pBufferInfo = &frI;

        // shadow geom/sampling set (0 frustum viewProjs, 1 atlas array sampler, 2 per-light info).
        w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[4].dstSet = sh->geomSet; w[4].dstBinding = 0; w[4].descriptorCount = 1;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[4].pBufferInfo = &frI;
        w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[5].dstSet = sh->geomSet; w[5].dstBinding = 1; w[5].descriptorCount = 1;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[5].pImageInfo = &atI;
        w[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[6].dstSet = sh->geomSet; w[6].dstBinding = 2; w[6].descriptorCount = 1;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[6].pBufferInfo = &infoI;

        vkUpdateDescriptorSets(ctx->device, 7, w, 0, NULL);
    }
}

void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state)
{
    // Per view per frame: the composite samples each view's HDR resolve into its destination rect.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
        {
            ViewResources* vr = &state->frames[i].views[v];
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = vr->hdrColorView;
            imageInfo.sampler = state->textureSampler;

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = vr->tonemapSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
        }
    }
}



void updateUboDescriptorSets(VulkanContext* ctx, RendererState* state)
{ // Central to init, must be called on asset uploads. Should look into decoupling this somewhat so that entity assets can be managed dynamically.

	// Update scene-wide UBO descriptors
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = rendererState.frames[i].views[0].uniformBuffer; // rebound per view below; view 0 for the shared compute sets
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(GlobalUBO);

		VkDescriptorBufferInfo ssboInfo = {};
		ssboInfo.buffer = rendererState.transformBuffer.buffer[i];
		ssboInfo.offset = 0;
		ssboInfo.range = sizeof(mat4) * rendererState.transformBuffer.capacity;

		VkDescriptorBufferInfo materialInfo = {};
		materialInfo.buffer = rendererState.materialBuffer.buffer[i];
		materialInfo.offset = 0;
		materialInfo.range = sizeof(MaterialData) * rendererState.materialBuffer.capacity;

		VkDescriptorBufferInfo vertexBufferInfo = {};
		vertexBufferInfo.buffer = rendererState.globalGeometryPool.vertexBuffer;
		vertexBufferInfo.offset = 0;
		vertexBufferInfo.range = rendererState.globalGeometryPool.vertexCapacity;

		VkDescriptorBufferInfo indexBufferInfo = {};
		indexBufferInfo.buffer = rendererState.globalGeometryPool.indexBuffer;
		indexBufferInfo.offset = 0;
		indexBufferInfo.range = rendererState.globalGeometryPool.indexCapacity;

		uint32_t maxMeshes = 1024;
		VkDescriptorBufferInfo globalMeshInfo = {};
		globalMeshInfo.buffer = rendererState.culling.meshDataBuffer[i];
		globalMeshInfo.offset = 0;
		globalMeshInfo.range = sizeof(uint32_t) * 4 * maxMeshes;

		VkDescriptorBufferInfo compactedEntityIndicesInfo = {};
		compactedEntityIndicesInfo.buffer = rendererState.culling.compactedEntityIndicesBuffer[i];
		compactedEntityIndicesInfo.offset = 0;
		compactedEntityIndicesInfo.range = sizeof(uint32_t) * rendererState.culling.maxEntities * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;

		VkDescriptorBufferInfo lightInfo = {};
		lightInfo.buffer = rendererState.lightBuffer.device;        // ×1 device-local (SlotUpload)
		lightInfo.offset = 0;
		lightInfo.range = sizeof(LightData) * rendererState.lightBuffer.capacity;

		VkDescriptorBufferInfo instanceDataInfo = {};
		instanceDataInfo.buffer = rendererState.instanceDataBuffer.device;  // ×1 device-local (SlotUpload)
		instanceDataInfo.offset = 0;
		instanceDataInfo.range = sizeof(AnoInstanceData) * rendererState.instanceDataBuffer.capacity;

		VkWriteDescriptorSet descriptorWrites[10] = {};

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[0].dstBinding = 0;   // Corresponds to binding in shader.
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pBufferInfo = &ssboInfo;

		descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[2].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[2].dstBinding = 2;
		descriptorWrites[2].dstArrayElement = 0;
		descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[2].descriptorCount = 1;
		descriptorWrites[2].pBufferInfo = &materialInfo;

		VkDescriptorBufferInfo entityInfo = {};
		entityInfo.buffer = rendererState.culling.entity.device;   // ×1 device-local (SlotUpload)
		entityInfo.offset = 0;
		entityInfo.range = sizeof(uint32_t) * 2 * rendererState.culling.maxEntities;

		descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[3].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[3].dstBinding = 3;
		descriptorWrites[3].dstArrayElement = 0;
		descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[3].descriptorCount = 1;
		descriptorWrites[3].pBufferInfo = &entityInfo;

		descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[4].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[4].dstBinding = 4;
		descriptorWrites[4].dstArrayElement = 0;
		descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[4].descriptorCount = 1;
		descriptorWrites[4].pBufferInfo = &vertexBufferInfo;

		descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[5].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[5].dstBinding = 5;
		descriptorWrites[5].dstArrayElement = 0;
		descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[5].descriptorCount = 1;
		descriptorWrites[5].pBufferInfo = &indexBufferInfo;

		descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[6].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[6].dstBinding = 6;
		descriptorWrites[6].dstArrayElement = 0;
		descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[6].descriptorCount = 1;
		descriptorWrites[6].pBufferInfo = &globalMeshInfo;

		descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[7].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[7].dstBinding = 7;
		descriptorWrites[7].dstArrayElement = 0;
		descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[7].descriptorCount = 1;
		descriptorWrites[7].pBufferInfo = &compactedEntityIndicesInfo;

		descriptorWrites[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[8].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[8].dstBinding = 8;
		descriptorWrites[8].dstArrayElement = 0;
		descriptorWrites[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[8].descriptorCount = 1;
		descriptorWrites[8].pBufferInfo = &lightInfo;

		descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[9].dstSet = rendererState.frames[i].views[0].globalSet;
		descriptorWrites[9].dstBinding = 9;
		descriptorWrites[9].dstArrayElement = 0;
		descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[9].descriptorCount = 1;
		descriptorWrites[9].pBufferInfo = &instanceDataInfo;

		// Global set is per view (audit 4.8): bindings 1-9 are shared scene SSBOs (same buffer
		// for every view); binding 0 rebinds to each view's camera UBO. Bindings 10/11 (cluster
		// lists) are written separately by updateClusterDescriptorSets.
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			bufferInfo.buffer = rendererState.frames[i].views[v].uniformBuffer;
			for (int k = 0; k < 10; k++) descriptorWrites[k].dstSet = rendererState.frames[i].views[v].globalSet;
			vkUpdateDescriptorSets(ctx->device, 10, descriptorWrites, 0, NULL);
		}
		// Shared compute sets below that need a GlobalUBO use view 0 (time/deltaTime are
		// view-independent): restore bufferInfo after the per-view writes left it at the last view.
		bufferInfo.buffer = rendererState.frames[i].views[0].uniformBuffer;

        // Update cull sets
        VkDescriptorBufferInfo cullUboInfo = {};
        cullUboInfo.buffer = rendererState.culling.ubo.buffer[i];
        cullUboInfo.offset = 0;
        cullUboInfo.range = sizeof(CullUBO);

        VkDescriptorBufferInfo meshDataInfo = {};
        meshDataInfo.buffer = rendererState.culling.meshDataBuffer[i];
        meshDataInfo.offset = 0;
        meshDataInfo.range = sizeof(uint32_t) * 8 * maxMeshes;

        VkDescriptorBufferInfo meshBoundsInfo = {};
        meshBoundsInfo.buffer = rendererState.culling.meshBoundsBuffer[i];
        meshBoundsInfo.offset = 0;
        meshBoundsInfo.range = sizeof(float) * 4 * maxMeshes;

        VkDescriptorBufferInfo indirectInfo = {};
        indirectInfo.buffer = rendererState.indirectBuffer.buffer[i];
        indirectInfo.offset = 0;
        // Range MUST use the same max command stride createIndirectDrawBuffer allocated with: the
        // vertex fallback writes VkDrawIndexedIndirectCommand (20B) > mesh's VkDrawMeshTasksIndirectCommandEXT
        // (12B). A mesh-stride range under-covers the buffer, leaving the high (shadow) partitions outside
        // the descriptor — the cull's OOB writes are dropped and the fallback casts no shadows.
        VkDeviceSize indirectCmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
            ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
        indirectInfo.range = indirectCmdStride * rendererState.indirectBuffer.capacity * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;

        VkDescriptorBufferInfo countInfo = {};
        countInfo.buffer = rendererState.culling.drawCountBuffer[i];
        countInfo.offset = 0;
        countInfo.range = sizeof(uint32_t) * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;

        VkDescriptorBufferInfo compactedEntityIndicesCullInfo = {};
        compactedEntityIndicesCullInfo.buffer = rendererState.culling.compactedEntityIndicesBuffer[i];
        compactedEntityIndicesCullInfo.offset = 0;
        compactedEntityIndicesCullInfo.range = sizeof(uint32_t) * rendererState.culling.maxEntities * ano_draw_pipeline_count() * ANO_FRUSTUM_COUNT;

        VkDescriptorBufferInfo materialCullInfo = {};
        materialCullInfo.buffer = rendererState.materialBuffer.buffer[i];
        materialCullInfo.offset = 0;
        materialCullInfo.range = sizeof(MaterialData) * rendererState.materialBuffer.capacity;

        // Binding 9: the GPU-built shadow frustums (audit 4.7), per frame.
        VkDescriptorBufferInfo shadowFrustumCullInfo = {};
        shadowFrustumCullInfo.buffer = rendererState.frames[i].shadow.frustumBuffer;
        shadowFrustumCullInfo.offset = 0;
        shadowFrustumCullInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet cullWrites[10] = {};
        for(int j=0; j<10; ++j) {
            cullWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cullWrites[j].dstSet = rendererState.frames[i].cullSet;
            cullWrites[j].dstBinding = j;
            cullWrites[j].dstArrayElement = 0;
            cullWrites[j].descriptorCount = 1;
            if (j == 0) cullWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            else cullWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        cullWrites[0].pBufferInfo = &cullUboInfo;
        cullWrites[1].pBufferInfo = &ssboInfo; // TransformSSBO
        cullWrites[2].pBufferInfo = &entityInfo;
        cullWrites[3].pBufferInfo = &meshDataInfo;
        cullWrites[4].pBufferInfo = &meshBoundsInfo;
        cullWrites[5].pBufferInfo = &indirectInfo;
        cullWrites[6].pBufferInfo = &countInfo;
        cullWrites[7].pBufferInfo = &compactedEntityIndicesCullInfo;
        cullWrites[8].pBufferInfo = &materialCullInfo;
        cullWrites[9].pBufferInfo = &shadowFrustumCullInfo;

        vkUpdateDescriptorSets(ctx->device, 10, cullWrites, 0, NULL);

        // Update Compute Descriptor Sets
        VkDescriptorBufferInfo motionInfo = {};
        motionInfo.buffer = rendererState.motionBuffer.device;     // ×1 device-local (SlotUpload)
        motionInfo.offset = 0;
        motionInfo.range = sizeof(AnoMotionDescriptor) * rendererState.motionBuffer.capacity;

        VkDescriptorBufferInfo initialTransformInfo = {};
        initialTransformInfo.buffer = rendererState.initialTransformBuffer.device; // ×1 device-local (SlotUpload)
        initialTransformInfo.offset = 0;
        initialTransformInfo.range = sizeof(mat4) * rendererState.initialTransformBuffer.capacity;

        VkWriteDescriptorSet updateWrites[4] = {};
        for(int j=0; j<4; ++j) {
            updateWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            updateWrites[j].dstSet = rendererState.frames[i].updateSet;
            updateWrites[j].dstBinding = j;
            updateWrites[j].dstArrayElement = 0;
            updateWrites[j].descriptorCount = 1;
            if (j == 0) updateWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            else updateWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        updateWrites[0].pBufferInfo = &bufferInfo; // GlobalUBO
        updateWrites[1].pBufferInfo = &ssboInfo;   // TransformSSBO
        updateWrites[2].pBufferInfo = &motionInfo; // MotionSSBO
        updateWrites[3].pBufferInfo = &initialTransformInfo;

        vkUpdateDescriptorSets(ctx->device, 4, updateWrites, 0, NULL);

        // Scatter set (streamed transforms): 0 resolved slots (per-frame), 1 the xform
        // ring (DYNAMIC — range is one slice; recordCommandBuffer selects the published
        // slice by dynamic offset), 2 the live transform buffer it scatters into.
        VkDescriptorBufferInfo streamSlotInfo = {};
        streamSlotInfo.buffer = rendererState.transformStream.slotBuffer[i];
        streamSlotInfo.offset = 0;
        streamSlotInfo.range = sizeof(uint32_t) * rendererState.transformStream.capacity;

        VkDescriptorBufferInfo streamXformInfo = {};
        streamXformInfo.buffer = rendererState.transformStream.xformRing;
        streamXformInfo.offset = 0;                                       // dynamic offset added at bind
        streamXformInfo.range = rendererState.transformStream.sliceStride; // one slice

        VkWriteDescriptorSet scatterWrites[3] = {};
        for (int j = 0; j < 3; ++j) {
            scatterWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            scatterWrites[j].dstSet = rendererState.frames[i].scatterSet;
            scatterWrites[j].dstBinding = (uint32_t)j;
            scatterWrites[j].dstArrayElement = 0;
            scatterWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            scatterWrites[j].descriptorCount = 1;
        }
        scatterWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        scatterWrites[0].pBufferInfo = &streamSlotInfo;
        scatterWrites[1].pBufferInfo = &streamXformInfo;
        scatterWrites[2].pBufferInfo = &ssboInfo; // same TransformSSBO update writes

        vkUpdateDescriptorSets(ctx->device, 3, scatterWrites, 0, NULL);
	}
}



/*bool createDescriptorPool(VulkanContext* ctx)
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

	if (vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &(ctx->renderComp.descriptorPool)) != VK_SUCCESS)
	{
		printf("Failed to create descriptor pool!\n");
		return false;
	}

	return true;
}*/





uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{ // Central to init, also used externally post-init
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProperties);

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


bool stagingTransfer(VulkanContext* ctx, const void* data, VkBuffer dstBuffer, VkDeviceSize bufferSize)
{ // Not central to init
	// Create staging buffer
	VkBuffer stagingBuffer;
	GpuAllocation stagingAlloc;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (!createDataBuffer(ctx, &stagingAllocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, properties, &stagingBuffer, &stagingAlloc)) 
	{
		printf("Failed to create staging buffer!");
		return false;
	}

	// Map the staging buffer's memory, copy the data, and then unmap
	void* mappedMemory = stagingAlloc.mapped;
	memcpy(mappedMemory, data, bufferSize);

	// Copy data from staging buffer to destination buffer
	if (!copyBuffer(ctx, stagingBuffer, dstBuffer, bufferSize))
	{
		printf("Failed to copy buffers!");
		return false;
	}

	// Cleanup staging buffer
	vkDestroyBuffer(ctx->device, stagingBuffer, NULL);

	return true;
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx)
{ // Used in init, also external
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = rendererState.commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(ctx->device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer)
{ // Used in init, also external
	vkEndCommandBuffer(commandBuffer);

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	vkCreateFence(ctx->device, &fenceInfo, NULL, &fence);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, fence);
	vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
	vkDestroyFence(ctx->device, fence, NULL);

	vkFreeCommandBuffers(ctx->device, rendererState.commandPool, 1, &commandBuffer);
}


bool copyBuffer(VulkanContext* ctx, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{ // Used in init, also external
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(ctx);
	
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(ctx, commandBuffer);

	return true;
}

bool createCommandBuffer(VulkanContext* ctx, RendererState* state)
{ // Central to init
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = rendererState.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	for (uint32_t i =0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkAllocateCommandBuffers(ctx->device, &allocInfo, &(rendererState.frames[i].commandBuffer)) != VK_SUCCESS) 
		{
			printf("Failed to allocate command buffers!\n");
			return false;
		}
	}

	return true;
}

bool createSyncObjects(VulkanContext* ctx, RendererState* state) 
{ // Central to init
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;  

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		if (vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &(rendererState.frames[i].imageAvailable)) != VK_SUCCESS ||
			vkCreateSemaphore(ctx->device, &semaphoreInfo, NULL, &(rendererState.frames[i].renderFinished)) != VK_SUCCESS ||
			vkCreateFence(ctx->device, &fenceInfo, NULL, &(rendererState.frames[i].frameFence)) != VK_SUCCESS)
			{
			printf("Failed to create semaphores!\n");
			return false;
		}
	}

	// GPU timestamp profiling (RADIANCE_CASCADES.md §8). One query pool of ANO_TS_COUNT timestamps
	// per frame in flight. Gated on the graphics queue family's timestampValidBits + a usable period;
	// when unsupported the whole timing path is a no-op and rendering is unaffected.
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(ctx->physicalDevice, &props);
		state->timestampPeriodNs = props.limits.timestampPeriod;

		uint32_t qfCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qfCount, NULL);
		VkQueueFamilyProperties qfProps[qfCount];
		vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qfCount, qfProps);
		uint32_t gf = ctx->queueFamilyIndices.graphicsFamily;
		state->timestampValidBits = (gf < qfCount) ? qfProps[gf].timestampValidBits : 0u;
		if (state->timestampPeriodNs <= 0.0f) state->timestampValidBits = 0u;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			state->frames[i].timestampPool = VK_NULL_HANDLE;
			if (state->timestampValidBits) {
				VkQueryPoolCreateInfo qpi = {};
				qpi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
				qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
				qpi.queryCount = ANO_TS_COUNT;
				if (vkCreateQueryPool(ctx->device, &qpi, NULL, &state->frames[i].timestampPool) != VK_SUCCESS) {
					printf("Failed to create timestamp query pool; profiling disabled.\n");
					state->timestampValidBits = 0u; // disable profiling, keep rendering
				}
			}
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



void cleanupVulkan(VulkanContext* ctx) // Frees up the previously initialized Vulkan parameters
{
	// !TODO ADD INTERMEDIARY FUNCTION TO DESTROY ENTITY STRUCT ASSETS, CALL HERE
	// !TODO Also texture samplers, image views, etc etc I basically gave up at this point since everything's gonna have to be generalized regardless
	if (ctx == NULL)
	{
		return;
	}

	vkDeviceWaitIdle(ctx->device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        flush_deletion_queue(ctx, &rendererState, i);
        if (rendererState.frames[i].deletionQueue.tasks) {
            free(rendererState.frames[i].deletionQueue.tasks);
            rendererState.frames[i].deletionQueue.tasks = NULL;
            rendererState.frames[i].deletionQueue.capacity = 0;
        }
    }

	cleanupSwapChain(ctx, &rendererState);
	if (rendererState.swapChain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(ctx->device, rendererState.swapChain, NULL);
	}

	if(rendererState.entities != NULL)
	{
        free(rendererState.entities);
        rendererState.entities = NULL;
	}

    for (uint32_t i = 0; i < rendererState.primitives.textureCount; i++)
    {
        if (rendererState.primitives.textureBuffers[i].textureImageView)
            vkDestroyImageView(ctx->device, rendererState.primitives.textureBuffers[i].textureImageView, NULL);
        if (rendererState.primitives.textureBuffers[i].textureImage)
            vkDestroyImage(ctx->device, rendererState.primitives.textureBuffers[i].textureImage, NULL);

    }
    ano_vk_cleanup_primitives(&rendererState.primitives);

    if (rendererState.fallbackImageView) vkDestroyImageView(ctx->device, rendererState.fallbackImageView, NULL);
    if (rendererState.fallbackImage) vkDestroyImage(ctx->device, rendererState.fallbackImage, NULL);

	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if(rendererState.transformBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.transformBuffer.buffer[i], NULL);
		// initialTransform/motion/instanceData are SlotUploads now (destroyed below).
		if(rendererState.transformStream.slotBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.transformStream.slotBuffer[i], NULL);
		if(rendererState.materialBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.materialBuffer.buffer[i], NULL);
		// lightBuffer is a SlotUpload now (destroyed below).
		if(rendererState.indirectBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.indirectBuffer.buffer[i], NULL);
		// culling.entity is a SlotUpload now (destroyed below).
		if(rendererState.culling.meshDataBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.meshDataBuffer[i], NULL);
		if(rendererState.culling.meshBoundsBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.meshBoundsBuffer[i], NULL);
		if(rendererState.culling.drawCountBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.drawCountBuffer[i], NULL);
		if(rendererState.culling.compactedEntityIndicesBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.compactedEntityIndicesBuffer[i], NULL);
		if(rendererState.culling.ubo.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.ubo.buffer[i], NULL);
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
			if(rendererState.frames[i].views[v].clusterLightCountBuffer)
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].clusterLightCountBuffer, NULL);
			if(rendererState.frames[i].views[v].clusterLightIndexBuffer)
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].clusterLightIndexBuffer, NULL);
		}
		// Dynamic shadow resources (audit 4.7): per-frame frustum buffer + depth atlas + views.
		ShadowResources* sh = &rendererState.frames[i].shadow;
		if (sh->frustumBuffer) vkDestroyBuffer(ctx->device, sh->frustumBuffer, NULL);
		if (sh->arrayView) vkDestroyImageView(ctx->device, sh->arrayView, NULL);
		for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
			if (sh->layerView[s]) vkDestroyImageView(ctx->device, sh->layerView[s], NULL);
		if (sh->atlasImage) vkDestroyImage(ctx->device, sh->atlasImage, NULL);
	}
	// Static shadow config/info buffers (once).
	if (rendererState.shadowFrustumConfigBuffer) vkDestroyBuffer(ctx->device, rendererState.shadowFrustumConfigBuffer, NULL);
	if (rendererState.shadowLightInfoBuffer) vkDestroyBuffer(ctx->device, rendererState.shadowLightInfoBuffer, NULL);

	// Radiance-cascade resources (×1 shared; image backing memory freed with rcAllocator below,
	// the frustum buffer's with gpuAllocator).
	if (rendererState.rcVoxelAlbedoView) vkDestroyImageView(ctx->device, rendererState.rcVoxelAlbedoView, NULL);
	if (rendererState.rcVoxelAlbedo) vkDestroyImage(ctx->device, rendererState.rcVoxelAlbedo, NULL);
	if (rendererState.rcVoxelEmissionView) vkDestroyImageView(ctx->device, rendererState.rcVoxelEmissionView, NULL);
	if (rendererState.rcVoxelEmission) vkDestroyImage(ctx->device, rendererState.rcVoxelEmission, NULL);
	if (rendererState.rcVoxelizeFrustumBuffer) vkDestroyBuffer(ctx->device, rendererState.rcVoxelizeFrustumBuffer, NULL);
	if (rendererState.rcIrradianceView) vkDestroyImageView(ctx->device, rendererState.rcIrradianceView, NULL);
	if (rendererState.rcIrradiance) vkDestroyImage(ctx->device, rendererState.rcIrradiance, NULL);
	for (uint32_t c = 0; c < ANO_RC_CASCADE_COUNT; c++) {
		if (rendererState.rcCascadeView[c]) vkDestroyImageView(ctx->device, rendererState.rcCascadeView[c], NULL);
		if (rendererState.rcCascade[c]) vkDestroyImage(ctx->device, rendererState.rcCascade[c], NULL);
	}
	if (rendererState.rcSampler) vkDestroySampler(ctx->device, rendererState.rcSampler, NULL);

	// SlotUpload buffers: ×1 device-local authoritative + per-frame host-visible delta staging
	// + the malloc'd copy-region lists. (Backing memory itself is freed wholesale with the
	// gpu allocator; here we destroy the VkBuffer handles and free the region arrays.)
	{
		SlotUpload* ups[5] = { &rendererState.initialTransformBuffer, &rendererState.motionBuffer,
			&rendererState.instanceDataBuffer, &rendererState.lightBuffer, &rendererState.culling.entity };
		for (uint32_t u = 0; u < 5; u++) {
			if (ups[u]->device) vkDestroyBuffer(ctx->device, ups[u]->device, NULL);
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (ups[u]->staging[i]) vkDestroyBuffer(ctx->device, ups[u]->staging[i], NULL);
				if (ups[u]->regions[i]) free(ups[u]->regions[i]);
			}
		}
	}

	// Single (non-per-frame) streamed-transform ring.
	if(rendererState.transformStream.xformRing)
		vkDestroyBuffer(ctx->device, rendererState.transformStream.xformRing, NULL);

	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			if(rendererState.frames[i].views[v].uniformBuffer)
			{
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].uniformBuffer, NULL);
			}
		}
	}

    ano_vk_cleanup_pipelines(ctx, &rendererState);

	#ifdef DEBUG_BUILD
	DestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debugMessenger, NULL);
	#endif
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (rendererState.frames[i].imageAvailable)
		{
			vkDestroySemaphore(ctx->device, rendererState.frames[i].imageAvailable, NULL);
		}
		if (rendererState.frames[i].renderFinished)
		{
			vkDestroySemaphore(ctx->device, rendererState.frames[i].renderFinished, NULL);
		}
		if (rendererState.frames[i].frameFence)
		{
			vkDestroyFence(ctx->device, rendererState.frames[i].frameFence, NULL);
		}
		if (rendererState.frames[i].timestampPool)
		{
			vkDestroyQueryPool(ctx->device, rendererState.frames[i].timestampPool, NULL);
		}

	}

	
	if (rendererState.commandPool != NULL)
	{
		vkDestroyCommandPool(ctx->device, rendererState.commandPool, NULL);
	}	



	if (rendererState.textureSampler != NULL)
	{
		vkDestroySampler(ctx->device, rendererState.textureSampler, NULL);
	}
	
	if (ctx->device != VK_NULL_HANDLE) 
	{
		vkDeviceWaitIdle(ctx->device);

		ano_vk_cleanup_geometry_pool(&rendererState.globalGeometryPool, ctx->device);
		gpu_alloc_destroy(&gpuAllocator);
		gpu_alloc_destroy(&swapchainAllocator);
		gpu_alloc_destroy(&stagingAllocator);
		gpu_alloc_destroy(&textureAllocator);
		gpu_alloc_destroy(&rcAllocator);

		vkDestroyDevice(ctx->device, NULL);
	}

	if (ctx->availableDevices != NULL)
	{
		for (uint32_t i = 0; i < ctx->deviceCount; i++)
		{
			free(ctx->availableDevices[i]);
			ctx->availableDevices[i] = NULL;
		}
		free(ctx->availableDevices);
		ctx->availableDevices = NULL;
	}

	if (ctx->instance != VK_NULL_HANDLE) 
	{
		if (ctx->surface != VK_NULL_HANDLE) 
		{
			vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
		}
		vkDestroyInstance(ctx->instance, NULL);
	}

}
