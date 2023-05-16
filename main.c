#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Shader code for a simple line
const char *vertexShaderSource = R"(
#version 450
layout(location = 0) in vec2 pos;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char *fragmentShaderSource = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

const char *computeShaderSource = R"(
#version 450
layout(local_size_x = 1) in;
layout(binding = 0) buffer Buffer {
    int data[];
} buf;
void main() {
    buf.data[0] += buf.data[1];
}
)";

VkShaderModule createShaderModule(VkDevice device, const char *source) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = strlen(source);
    createInfo.pCode = (const uint32_t *)source;

    VkShaderModule shaderModule;
    vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);
    return shaderModule;
}

// Function Prototypes
void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice);
void createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice *device, VkQueue *graphicsQueue, VkQueue *computeQueue);
void createSwapChain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkSwapchainKHR *swapChain, VkExtent2D *swapChainExtent);
void createImageViews(VkDevice device, VkSwapchainKHR swapChain, uint32_t *imageViewCount, VkImageView **imageViews);

int main() {
    // Initialize GLFW
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);

    // Initialize Vulkan
    VkInstance instance;
    {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "VulkanApp";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;
        createInfo.enabledLayerCount = 0;

        vkCreateInstance(&createInfo, NULL, &instance);
    }

    // Create a window surface
    VkSurfaceKHR surface;
    {
        glfwCreateWindowSurface(instance, window, NULL, &surface);
    }

    // Initialize the rest of the Vulkan pipeline
    // ...

    // Set up physical device
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    pickPhysicalDevice(instance, &physicalDevice);

    // Create shader modules
    VkDevice device; // Replace this with your initialized Vulkan device
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    createLogicalDevice(physicalDevice, &device, &graphicsQueue, &computeQueue);
    VkShaderModule vertexShaderModule = createShaderModule(device, vertexShaderSource);
    VkShaderModule fragmentShaderModule = createShaderModule(device, fragmentShaderSource);
    VkShaderModule computeShaderModule = createShaderModule(device, computeShaderSource);

    // Set up graphics and compute pipelines
    // ...
    // Create swap chain
    /*VkSwapchainKHR swapChain;
    VkExtent2D swapChainExtent;
    createSwapChain(physicalDevice, device, surface, &swapChain, &swapChainExtent);

    // Create image views
    uint32_t imageViewCount;
    VkImageView *imageViews;
    createImageViews(device, swapChain, &imageViewCount, &imageViews);*/


    // Main loop
        while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Record and submit commands for graphics and compute operations
        // ...

        // Present the image to the window
        // ...

    }

    // Wait for device to finish all operations before cleaning up
    vkDeviceWaitIdle(device);

    // Clean up shader modules
    vkDestroyShaderModule(device, vertexShaderModule, NULL);
    vkDestroyShaderModule(device, fragmentShaderModule, NULL);
    vkDestroyShaderModule(device, computeShaderModule, NULL);

    // Clean up the rest of the Vulkan pipeline
    // ...

    // Destroy surface and instance
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    // Terminate GLFW
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice *physicalDevice) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        fprintf(stderr, "Failed to find GPUs with Vulkan support!\n");
        exit(EXIT_FAILURE);
    }

    VkPhysicalDevice *devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);

        // Check for device suitability here, e.g., required extensions, graphics and compute queue families, etc.
        // For simplicity, we'll just pick the first device we find
        *physicalDevice = devices[i];
        break;
    }

    if (*physicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "Failed to find a suitable GPU!\n");
        exit(EXIT_FAILURE);
    }

    free(devices);
}

void createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice *device, VkQueue *graphicsQueue, VkQueue *computeQueue) {
    // Find queue families
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);

    int graphicsQueueFamilyIndex = -1;
    int computeQueueFamilyIndex = -1;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && graphicsQueueFamilyIndex == -1) {
            graphicsQueueFamilyIndex = i;
        }

        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT && computeQueueFamilyIndex == -1) {
            computeQueueFamilyIndex = i;
        }

        if (graphicsQueueFamilyIndex != -1 && computeQueueFamilyIndex != -1) {
            break;
        }
    }

    if (graphicsQueueFamilyIndex == -1 || computeQueueFamilyIndex == -1) {
        fprintf(stderr, "Failed to find suitable queue families!\n");
        exit(EXIT_FAILURE);
    }

    free(queueFamilies);

    // Create logical device
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfos[2] = {};
    queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfos[0].queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfos[0].queueCount = 1;
    queueCreateInfos[0].pQueuePriorities = &queuePriority;

    queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfos[1].queueFamilyIndex = computeQueueFamilyIndex;
    queueCreateInfos[1].queueCount = 1;
    queueCreateInfos[1].pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.queueCreateInfoCount = (graphicsQueueFamilyIndex == computeQueueFamilyIndex) ? 1 : 2;
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Enable any required device extensions here, e.g., VK_KHR_SWAPCHAIN_EXTENSION_NAME
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = NULL;

    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(physicalDevice, &createInfo, NULL, device) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create logical device!\n");
        exit(EXIT_FAILURE);
    }

    // Get graphics and compute queues
    vkGetDeviceQueue(*device, graphicsQueueFamilyIndex, 0, graphicsQueue);
    vkGetDeviceQueue(*device, computeQueueFamilyIndex, 0, computeQueue);
}

void createSwapChain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkSwapchainKHR *swapChain, VkExtent2D *swapChainExtent) {
    // Query surface capabilities and supported formats
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);
    VkSurfaceFormatKHR *formats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats);

    // Choose a suitable surface format
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = formats[i];
            break;
        }
    }

    free(formats);

    // Choose a suitable present mode
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);
    VkPresentModeKHR *presentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes);

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        } else if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    free(presentModes);

    // Create the swap chain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = capabilities.minImageCount + 1;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = capabilities.currentExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, NULL, swapChain) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create swap chain!\n");
        exit(EXIT_FAILURE);
    }

    // Store swap chain extent for later use
    *swapChainExtent = createInfo.imageExtent;
}

void createImageViews(VkDevice device, VkSwapchainKHR swapChain, uint32_t *imageViewCount, VkImageView **imageViews) {
    // Get swap chain images
    vkGetSwapchainImagesKHR(device, swapChain, imageViewCount, NULL);
    VkImage *swapChainImages = malloc(*imageViewCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(device, swapChain, imageViewCount, swapChainImages);

    // Create image views
    *imageViews = malloc(*imageViewCount * sizeof(VkImageView));

    for (uint32_t i = 0; i < *imageViewCount; i++) {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, NULL, &((*imageViews)[i])) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create image view!\n");
            exit(EXIT_FAILURE);
        }
    }

    free(swapChainImages);
}