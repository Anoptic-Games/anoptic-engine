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

// Function Prototypes
VkResult createInstance(VkInstance *instance);
VkResult createSurface(VkInstance instance, GLFWwindow *window, VkSurfaceKHR *surface);
void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice);
VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice *device, VkQueue *graphicsQueue, VkQueue *computeQueue);

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
    components->physicalDevice = VK_NULL_HANDLE;
    pickPhysicalDevice(components->instance, &(components->physicalDevice));

    // Create logical device
    if (createLogicalDevice(components->physicalDevice, &(components->device), &(components->graphicsQueue), &(components->computeQueue)) != VK_SUCCESS)
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
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

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

void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice* physicalDevice) // Queries all available devices, and selects one according to our needs.
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
    VkDeviceSize maxMemorySize = 0;

    for (uint32_t i = 0; i < deviceCount; i++) // Iterates through available devices and selects the one with the most VRAM.
    {
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

        if (deviceProperties.limits.bufferImageGranularity > maxMemorySize) 
        {
            maxMemorySize = deviceProperties.limits.bufferImageGranularity;
            *physicalDevice = devices[i];
        }
    }

    if (*physicalDevice == VK_NULL_HANDLE) 
    {
        fprintf(stderr, "Failed to find a suitable GPU!\n");
    }

    free(devices);
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue) // Creates a logical device that can actually do stuff.
{
    // Queue creation information
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0;  // Replace with appropriate queue family index
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Device features (optional)
    VkPhysicalDeviceFeatures deviceFeatures = {};  // Leave features disabled for now

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