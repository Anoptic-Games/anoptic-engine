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

// Hard-required device extensions. VK_EXT_mesh_shader appended dynamically when supported.
static const char* requiredExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME }; // Make dynamic, determined at runtime


struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface) { // Extend with more queue family checks
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

	// First queue family per capability. Compute prefers dedicated (async Hi-Z), else first compute-capable.
	//!TODO Implement these as required further into development
	bool haveDedicatedCompute = false;
	for (uint32_t i = 0; i < queueFamilyCount; i++)
	{	//Queue checks go here
		if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && indices.graphicsPresent == false)
		{
			indices.graphicsFamily = i;
			indices.graphicsPresent = true;
			//printf("Graphics: %d\n", i);
		}
		if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			bool dedicated = (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0;
			if (indices.computePresent == false || (dedicated && !haveDedicatedCompute))
			{
				indices.computeFamily = i;
				indices.computePresent = true;
				haveDedicatedCompute = dedicated;
				//printf("Compute: %d\n", i);
			}
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
				if (indices.presentPresent == false) // Primary present family, usually the graphics family
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

struct DeviceCapabilities populateCapabilities(VkPhysicalDevice device) // Select required capabilities, extend with checks and error states
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
	// True only when VK_EXT_mesh_shader is present and the feature is usable.
	capabilities.meshShader = meshShaderFeatures.meshShader;
	// Test hook forces the vertex-shader fallback path on mesh-capable hardware.
	if (getenv("ANO_FORCE_NO_MESH_SHADER")) capabilities.meshShader = false;
	// Task (amplification) stage for the per-meshlet cull, mesh path only.
	capabilities.taskShader = capabilities.meshShader && meshShaderFeatures.taskShader;

	// Depth-resolve MAX support (a property) lets the Hi-Z build resolve depth to a single farthest sample.
	VkPhysicalDeviceDepthStencilResolveProperties dsResolve = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES };
	VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &dsResolve };
	vkGetPhysicalDeviceProperties2(device, &props2);
	capabilities.depthMaxResolve = (dsResolve.supportedDepthResolveModes & VK_RESOLVE_MODE_MAX_BIT) != 0;
	if (getenv("ANO_FORCE_NO_DEPTH_RESOLVE")) capabilities.depthMaxResolve = false;

	// Vertex-stage gl_Layer for the layered shadow blur. Both features cover whichever SPIR-V capability glslang emits.
	capabilities.shaderOutputLayer = features12.shaderOutputLayer && features12.shaderOutputViewportIndex;
	if (getenv("ANO_FORCE_NO_SHADER_OUTPUT_LAYER")) capabilities.shaderOutputLayer = false;

	// Timeline semaphores for cross-queue ordering in the async Hi-Z build.
	capabilities.timelineSemaphore = features12.timelineSemaphore;

	// fp16 arithmetic selects the *_fp16.frag lighting variants.
	capabilities.shaderFloat16 = features12.shaderFloat16;
	if (getenv("ANO_FORCE_NO_FP16")) capabilities.shaderFloat16 = false;
	ano_log(ANO_INFO, "CDF reconstruct: %s", capabilities.shaderFloat16 ? "fp16" : "fp32 (no shaderFloat16)");

	//Queue family checks
	struct QueueFamilyIndices indices = findQueueFamilies(device, NULL);
	capabilities.graphics = indices.graphicsPresent;
	capabilities.compute = indices.computePresent;
	capabilities.transfer = indices.transferPresent;
	return capabilities;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) { // Rework extensions system, add a definition interface
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
			return false; // Required extension missing
		}
	}

	free(availableExtensions);
	return true; // All found
}

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR *surface) // Extend and integrate with capability checks, expose via interface
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

	// geometryShader and shaderFloat64 intentionally NOT required, nothing consumes them.
	bool physicalRequirements = features2.features.shaderInt64 && features2.features.samplerAnisotropy;

	// Required Vulkan 1.2, dynamic rendering, and mesh shader features
	bool requiredFeatures12 = features12.descriptorIndexing &&
	                          features12.shaderSampledImageArrayNonUniformIndexing &&
	                          features12.runtimeDescriptorArray &&
	                          features12.descriptorBindingPartiallyBound &&
	                          features12.descriptorBindingVariableDescriptorCount &&
	                          features12.descriptorBindingSampledImageUpdateAfterBind;
	bool requiredDynamicRendering = dynamicRenderingFeature.dynamicRendering;
	bool requiredMultiDraw = features2.features.multiDrawIndirect;

	// Mesh shader preferred but NOT required, the fallback path handles its absence.
	if (!requiredFeatures12 || !requiredDynamicRendering || !requiredMultiDraw) {
		ano_log(ANO_WARN, "Device rejected: lacks required Vulkan 1.2, dynamic rendering, or multiDrawIndirect features.");
		return false;
	}
	if (!meshShaderFeatures.meshShader) {
		ano_log(ANO_WARN, "Device lacks VK_EXT_mesh_shader: will use the vertex-shader fallback path.");
		// Vertex fallback packs the draw ordinal into firstInstance, requiring drawIndirectFirstInstance.
		if (!features2.features.drawIndirectFirstInstance) {
			ano_log(ANO_WARN, "Device rejected: also lacks drawIndirectFirstInstance, so the vertex "
			             "fallback path cannot draw correctly.");
			return false;
		}
	}

	// 1x-only sample support cannot render. Mirrors getMaxUsableSampleCount.
	VkPhysicalDeviceVulkan12Properties vk12Props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
	VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vk12Props };
	vkGetPhysicalDeviceProperties2(device, &props2);
	VkSampleCountFlags usableCounts = props2.properties.limits.framebufferColorSampleCounts
	                                & props2.properties.limits.framebufferDepthSampleCounts
	                                & props2.properties.limits.sampledImageDepthSampleCounts
	                                & vk12Props.framebufferIntegerColorSampleCounts;
	if (!(usableCounts & ~(VkSampleCountFlags)VK_SAMPLE_COUNT_1_BIT)) {
		ano_log(ANO_WARN, "Device rejected: supports only 1x MSAA across the engine's attachment set, "
		             "and the renderer has no 1x path.");
		return false;
	}

	return physicalRequirements && queueRequirements && extensionsSupported;
}

VkSampleCountFlagBits getMaxUsableSampleCount(VulkanContext* ctx)
{
	// framebufferIntegerColorSampleCounts lives in VkPhysicalDeviceVulkan12Properties, queried via properties2.
	VkPhysicalDeviceVulkan12Properties vk12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
	VkPhysicalDeviceProperties2 physicalDeviceProperties2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vk12 };
	vkGetPhysicalDeviceProperties2(ctx->physicalDevice, &physicalDeviceProperties2);
	VkPhysicalDeviceProperties physicalDeviceProperties = physicalDeviceProperties2.properties;

	// Fold in sampledImageDepthSampleCounts (sampled Hi-Z depth) and framebufferIntegerColorSampleCounts (R32_UINT picking id).
	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts
	                          & physicalDeviceProperties.limits.framebufferDepthSampleCounts
	                          & physicalDeviceProperties.limits.sampledImageDepthSampleCounts
	                          & vk12.framebufferIntegerColorSampleCounts;

	// Honor the configured MSAA preference over the device max, clamped to support and raised to 2. ANO_MSAA overrides.
	uint32_t preferred = getChosenMsaaSamples();
	const char* msaaEnv = getenv("ANO_MSAA");
	if (msaaEnv) preferred = (uint32_t)atoi(msaaEnv);
	if (preferred < 2u) { ano_log(ANO_WARN, "MSAA preference %u below minimum, using 2x", preferred); preferred = 2u; }
	VkSampleCountFlags mask = 0;
	for (uint32_t s = 2u; s <= preferred && s <= 64u; s <<= 1) mask |= s; // sample flags are their counts
	// Preference window empty, take any supported >=2x count.
	VkSampleCountFlags preferredCounts = counts & mask;
	counts = preferredCounts ? preferredCounts : (counts & ~(VkSampleCountFlags)VK_SAMPLE_COUNT_1_BIT);

	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

// Mesh-shader capability, the primary device-ranking key.
static bool deviceHasMeshShader(VkPhysicalDevice device)
{
	VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures = {};
	meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &meshFeatures;

	vkGetPhysicalDeviceFeatures2(device, &features2);
	return meshFeatures.meshShader;
}

// ASCII-only case fold. Non-ASCII bytes pass through unfolded.
static char asciiLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Caseless substring match for the ANO_DEVICE override.
static bool nameContainsCaseless(const char* haystack, const char* needle)
{
	size_t needleLen = strlen(needle);
	if (needleLen == 0)
		return false;
	for (; *haystack; haystack++)
	{
		size_t j = 0;
		while (j < needleLen && haystack[j] &&
		       asciiLower(haystack[j]) == asciiLower(needle[j]))
			j++;
		if (j == needleLen)
			return true;
	}
	return false;
}

// Largest DEVICE_LOCAL heap.
static VkDeviceSize maxDeviceLocalHeapSize(const VkPhysicalDeviceMemoryProperties* memProperties)
{
	VkDeviceSize maxSize = 0;
	for (uint32_t h = 0; h < memProperties->memoryHeapCount; h++)
	{
		if ((memProperties->memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
		    memProperties->memoryHeaps[h].size > maxSize)
		{
			maxSize = memProperties->memoryHeaps[h].size;
		}
	}
	return maxSize;
}

bool pickPhysicalDevice(VulkanContext* ctx, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice) // Extend selection logic, split out device discovery, retain attributes for UI
{
	bool foundPreferredDevice = false;
	// ANO_DEVICE (caseless name substring) pins the adapter. Suitability checks still apply.
	const char* envDevice = getenv("ANO_DEVICE");
	ctx->deviceCount = 0;

	vkEnumeratePhysicalDevices(ctx->instance, &(ctx->deviceCount), NULL);

	if (ctx->deviceCount == 0) 
	{
		ano_log(ANO_FATAL, "Failed to find GPUs with Vulkan support!");
		return false;
	}

	// Names of every detected device. Zeroed so unwritten slots free() as no-ops.
	ctx->availableDevices = (char**)mi_calloc(ctx->deviceCount, sizeof(char*));

	VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(1, sizeof(VkPhysicalDevice) * ctx->deviceCount);

	vkEnumeratePhysicalDevices(ctx->instance, &ctx->deviceCount, devices);
	
	VkPhysicalDeviceProperties deviceProperties;
	// VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties memProperties;

	VkDeviceSize maxDedicatedMemory = 0;
	VkDeviceSize maxIntegratedMemory = 0;

	VkPhysicalDevice bestDedicatedDevice = VK_NULL_HANDLE;
	VkPhysicalDevice bestIntegratedDevice = VK_NULL_HANDLE;
	bool bestDedicatedMesh = false;
	bool bestIntegratedMesh = false;

	ano_log(ANO_INFO, "DeviceCount: %d", ctx->deviceCount);
	
	static const char* deviceTypeNames[] = { "other", "integrated", "discrete", "virtual", "cpu" };

	for (uint32_t i = 0; i < ctx->deviceCount; i++)
	{
		vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
		vkGetPhysicalDeviceMemoryProperties(devices[i], &memProperties);
		// Log full device identity.
		ano_log(ANO_INFO, "Device %u: %s (%s, mesh shader: %s)", i, deviceProperties.deviceName,
		             deviceProperties.deviceType <= VK_PHYSICAL_DEVICE_TYPE_CPU
		                 ? deviceTypeNames[deviceProperties.deviceType] : "unknown",
		             deviceHasMeshShader(devices[i]) ? "yes" : "no");

		if (isDeviceSuitable(devices[i], &(ctx->surface)))
		{

			// Select the first preferred device, if any.
			if (strcmp(deviceProperties.deviceName, preferredDevice) == 0 ||
			    (envDevice && nameContainsCaseless(deviceProperties.deviceName, envDevice)))
			{
				ctx->physicalDevice = devices[i];
				foundPreferredDevice = true;
				break;
			}

		
			// Rank suitable devices by mesh-shader capability first, then DEVICE_LOCAL memory.
			VkDeviceSize currentMemorySize = maxDeviceLocalHeapSize(&memProperties);
			bool currentMesh = deviceHasMeshShader(devices[i]);

			if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
			    ((currentMesh && !bestDedicatedMesh) ||
			     (currentMesh == bestDedicatedMesh && currentMemorySize > maxDedicatedMemory)))
			{
				bestDedicatedDevice = devices[i];
				bestDedicatedMesh = currentMesh;
				maxDedicatedMemory = currentMemorySize;
			}
			else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
			         ((currentMesh && !bestIntegratedMesh) ||
			          (currentMesh == bestIntegratedMesh && currentMemorySize > maxIntegratedMemory)))
			{
				bestIntegratedDevice = devices[i];
				bestIntegratedMesh = currentMesh;
				maxIntegratedMemory = currentMemorySize;
			}
			(ctx->availableDevices)[i] = (char*)mi_malloc(strlen(deviceProperties.deviceName) +1);
			strcpy((ctx->availableDevices)[i], deviceProperties.deviceName);
			ano_debug_log(ANO_INFO, "Device %u is suitable: %s", i, ctx->availableDevices[i]);
		}
	}

	if (envDevice && !foundPreferredDevice)
	{
		ano_log(ANO_WARN, "ANO_DEVICE=\"%s\" matched no suitable device; falling back to automatic selection.", envDevice);
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
			ano_log(ANO_FATAL, "Failed to find a suitable GPU!");
			free(devices);
			return false;
		}
	}

	// Log the selected device.
	VkPhysicalDeviceProperties chosenProperties;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice, &chosenProperties);
	ano_log(ANO_INFO, "Selected device: %s", chosenProperties.deviceName);

	ctx->msaaSamples = getMaxUsableSampleCount(ctx);
	ano_log(ANO_INFO, "MSAA samples used: %d", ctx->msaaSamples);

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
	// Vertex fallback packs the draw ordinal into firstInstance, requiring this feature.
	deviceFeatures.drawIndirectFirstInstance = features2.features.drawIndirectFirstInstance;
	// Two color attachments with differing per-attachment blend state require independentBlend.
	deviceFeatures.independentBlend = features2.features.independentBlend;

	// At most 4 unique queues
	VkDeviceQueueCreateInfo queueCreateInfos[4];
	uint32_t uniqueQueueFamilies[4] = {indices->graphicsFamily, indices->presentFamily, indices->computeFamily, indices->transferFamily};
	uint32_t queueCount = 0;
	// Function-scoped so its address stays valid until vkCreateDevice. Every queue shares priority 1.0.
	const float queuePriority = 1.0f;
	
	for (uint32_t i = 0; i < 4; i++)
	{
		bool uniqueFamily = true;
		// Skip family indices already added
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
	
	// Create the device
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	// Request only what the device supports
	features12.descriptorIndexing = queryFeatures12.descriptorIndexing;
	features12.shaderSampledImageArrayNonUniformIndexing = queryFeatures12.shaderSampledImageArrayNonUniformIndexing;
	features12.runtimeDescriptorArray = queryFeatures12.runtimeDescriptorArray;
	features12.descriptorBindingPartiallyBound = queryFeatures12.descriptorBindingPartiallyBound;
	features12.descriptorBindingVariableDescriptorCount = queryFeatures12.descriptorBindingVariableDescriptorCount;
	features12.descriptorBindingSampledImageUpdateAfterBind = queryFeatures12.descriptorBindingSampledImageUpdateAfterBind;
	features12.drawIndirectCount = queryFeatures12.drawIndirectCount;
	// Vertex-stage gl_Layer for the layered shadow blur, both features cover glslang's emitted capability.
	features12.shaderOutputLayer = queryFeatures12.shaderOutputLayer;
	features12.shaderOutputViewportIndex = queryFeatures12.shaderOutputViewportIndex;
	// Async Hi-Z ordering.
	features12.timelineSemaphore = queryFeatures12.timelineSemaphore;
	// fp16 CDF reconstruct, enabled when present, gated by deviceCapabilities.shaderFloat16.
	features12.shaderFloat16 = queryFeatures12.shaderFloat16;

	// Fallback path activates when the feature is absent or the override forces it off.
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

	// Chain the mesh-shader feature struct only when enabling the extension.
	dynamicRenderingFeature.pNext = meshSupported ? (void*)&meshShaderFeatures : NULL;
	features11.pNext = &dynamicRenderingFeature; // 1.1 features -> dynamic rendering -> [mesh shader] -> NULL
	features12.pNext = &features11;

	VkDeviceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &features12;
	createInfo.queueCreateInfoCount = queueCount;
	createInfo.pQueueCreateInfos = queueCreateInfos;
	createInfo.pEnabledFeatures = &deviceFeatures;

	// Build the enabled-extension list, required set plus VK_EXT_mesh_shader when supported.
	uint32_t requiredExtensionCount = sizeof(requiredExtensions) / sizeof(requiredExtensions[0]);
	const char* enabledExtensions[8];
	uint32_t enabledExtensionCount = 0;
	for (uint32_t i = 0; i < requiredExtensionCount; i++)
		enabledExtensions[enabledExtensionCount++] = requiredExtensions[i];
	if (meshSupported)
		enabledExtensions[enabledExtensionCount++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;
	#ifdef __APPLE__
	// VK_KHR_portability_subset MUST be enabled when exposed. String literal avoids vulkan_beta.h.
	enabledExtensions[enabledExtensionCount++] = "VK_KHR_portability_subset";
	#endif

	createInfo.enabledExtensionCount = enabledExtensionCount;
	createInfo.ppEnabledExtensionNames = enabledExtensions;
	ano_log(ANO_INFO, "Enabling %u device extensions (mesh shader: %s)", enabledExtensionCount, meshSupported ? "yes" : "no");

	if (vkCreateDevice(physicalDevice, &createInfo, NULL, device) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create logical device!");
		return VK_ERROR_INITIALIZATION_FAILED;
	}
		
	// Queue handles
	vkGetDeviceQueue(*device, indices->graphicsFamily, 0, graphicsQueue);
	if (*graphicsQueue == NULL)
	{
		ano_log(ANO_FATAL, "Failed to acquire graphics queue!");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	vkGetDeviceQueue(*device, indices->presentFamily, 0, presentQueue);
	ano_debug_log(ANO_INFO, "PresentQueue: %p", (void*)presentQueue);
	if (*presentQueue == NULL)
	{
		ano_log(ANO_FATAL, "Failed to acquire present queue!");
		return VK_ERROR_INITIALIZATION_FAILED;	
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->computeFamily, 0, computeQueue);
		if (*computeQueue == NULL)
		{
			ano_log(ANO_FATAL, "Failed to acquire compute queue!");
			return VK_ERROR_INITIALIZATION_FAILED;	
		}
	}
	if (indices->computePresent)
	{
		vkGetDeviceQueue(*device, indices->transferFamily, 0, transferQueue);
		if (*transferQueue == NULL)
		{
			ano_log(ANO_FATAL, "Failed to acquire transfer queue!");
			return VK_ERROR_INITIALIZATION_FAILED;	
		}
	}

	return VK_SUCCESS;
}

