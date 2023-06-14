#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// Structs

typedef struct VulkanComponents
{
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
} VulkanComponents;

struct QueueFamilyIndices 
{
	bool graphicsPresent;
    uint32_t graphicsFamily;
    bool computePresent;
    uint32_t computeFamily;
    bool transferPresent;
    uint32_t transferFamily;
};

typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
		bool graphics;
		bool compute;
		bool transfer;
		bool float64;
		bool int64;
} DeviceCapabilities;

// Function Prototypes
VkResult createInstance(VkInstance *instance);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice *device, VkQueue *graphicsQueue, VkQueue *computeQueue, struct QueueFamilyIndices* indices);

//Init and cleanup functions

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

    // Create a window surface
    if (createSurface(components->instance, window, &(components->surface)) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create window surface!\n");
        free(components);
        return NULL;
    }

    // Pick physical device
    DeviceCapabilities capabilities;
    components->physicalDevice = VK_NULL_HANDLE;
    struct QueueFamilyIndices indices;
    pickPhysicalDevice(components->instance, &(components->physicalDevice), &capabilities, &indices);
    // Create logical device
    if (createLogicalDevice(components->physicalDevice, &(components->device), &(components->graphicsQueue), &(components->computeQueue), &indices) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create logical device!\n");
        free(components);
        return NULL;
    }
    
    return components;
}

void cleanupVulkan(VulkanComponents* components) // Frees up the previously initialized Vulkan parameters
{
    if (components == NULL) {
        return;
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

// Vulkan component initialization functions

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

struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) { // Extend with more queue family checks as they become relevant
    struct QueueFamilyIndices indices;
    
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
	VkQueueFamilyProperties queueFamilies[queueFamilyCount];
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	for (uint32_t i = 0; i < queueFamilyCount; i++)
	{	//Queue checks go here
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;
			indices.graphicsPresent = true;
		}
		if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			indices.computeFamily = i;
			indices.computePresent = true;
		}
		if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
		{
			indices.transferFamily = i;
			indices.transferPresent = true;
		}
		/*indices.graphicsFamily += queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
		indices.computeFamily += queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT;
		indices.transferFamily += queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT;*/
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
	struct QueueFamilyIndices indices = findQueueFamilies(device);
	capabilities.graphics = indices.graphicsPresent;
	capabilities.compute = indices.computePresent;
	capabilities.transfer = indices.transferPresent;
	return capabilities;
}

bool isDeviceSuitable(VkPhysicalDevice device, VkPhysicalDeviceProperties deviceProperties, VkPhysicalDeviceFeatures deviceFeatures)
{
	struct QueueFamilyIndices indices = findQueueFamilies(device);
	// Add any features as they become necessary
	bool physicalRequirements = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && deviceFeatures.geometryShader && deviceFeatures.shaderFloat64 && deviceFeatures.shaderInt64;
	bool queueRequirements = indices.graphicsPresent;
	return physicalRequirements && queueRequirements;
}

void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice* physicalDevice, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices) // Queries all available devices, and selects one according to our needs.
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

    if (deviceCount == 0) 
    {
        fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
        return;
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
	        if (isDeviceSuitable(devices[i], deviceProperties, deviceFeatures) == true) 
	        {
	        	*physicalDevice = devices[i];
	        	*capabilities = populateCapabilities(devices[i], deviceFeatures);
	        	*indices = findQueueFamilies(devices[i]);
	        }
        }
    }

    if (*physicalDevice == VK_NULL_HANDLE) 
    {
        fprintf(stderr, "Failed to find a suitable GPU!\n");
    }

    free(devices);
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, struct QueueFamilyIndices* indices) // Creates a logical device that can actually do stuff.
{
    // Queue creation information
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

    
    queueCreateInfo.queueFamilyIndex = indices->graphicsFamily; 
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Device features (optional)
    VkPhysicalDeviceFeatures availableFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &availableFeatures);
    VkPhysicalDeviceFeatures deviceFeatures = {.shaderInt64 = availableFeatures.shaderInt64, .shaderFloat64 = availableFeatures.shaderFloat64}; // Add more features as required
    // Logical device creation information
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(physicalDevice, &createInfo, NULL, device) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to create logical device!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Retrieve the created graphics queue
    vkGetDeviceQueue(*device, queueCreateInfo.queueFamilyIndex, 0, graphicsQueue);

    // Retrieve the created compute queue
    vkGetDeviceQueue(*device, queueCreateInfo.queueFamilyIndex, 0, computeQueue);

    return VK_SUCCESS;
}


// Main function

int main()
{
	GLFWwindow *window = initWindow();
	if (window == NULL)
	{
	    // Handle error
	}
	
	VulkanComponents *components = initVulkan(window);
	if (components == NULL)
	{
	    // Handle error
	}

    // Main loop
	while (!glfwWindowShouldClose(window))
	{
        glfwPollEvents();

        // Record and submit commands for graphics and compute operations
        // ...
        // Present the image to the window
        // ...
    }

    // Clean up
    cleanupVulkan(components);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}