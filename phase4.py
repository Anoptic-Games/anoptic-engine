import os
import re

src_dir = '/home/cris/Documents/anoptic-engine/src'

structs_h = os.path.join(src_dir, 'vulkan_backend/structs.h')
with open(structs_h, 'r') as f:
    content = f.read()

# Replace VulkanComponents with VulkanContext and remove its sub-components
# Actually, I'll just write the new structs.h directly or use regex.
vulkan_context_def = """typedef struct VulkanContext
{
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    bool                     enableValidationLayers;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         physicalDevice;
    DeviceCapabilities       deviceCapabilities;
    QueueFamilyIndices       queueFamilyIndices;
    VkSampleCountFlagBits    msaaSamples;
    VkDevice                 device;
    VkQueue                  graphicsQueue;
    VkQueue                  computeQueue;
    VkQueue                  transferQueue;
    VkQueue                  presentQueue;
} VulkanContext;
"""

renderer_state_additions = """
    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    uint32_t                imageCount;
    VkImage*                images;
    VkImage                 colorImage;
    VkDeviceMemory          colorImageMemory; // fix in phase 5
    VkImageView             colorView;
    uint32_t                viewCount;
    VkImageView*            views;

    // Command pool
    VkCommandPool           commandPool;

    // Render data
    GlobalUBO               uboData;
    VkSampler               textureSampler;
    RenderEntity*           entities;
    uint32_t                entityCount;
"""

# Let's replace the whole block from ImageViewGroup to VulkanComponents
# But wait, we still need DeviceCapabilities and QueueFamilyIndices
# Let's just remove ImageViewGroup, SwapChainGroup, InstanceDebugComponents, PhysicalDeviceComponents, DeviceQueueComponents, SwapChainSupportDetails, SwapChainComponents, BufferComponents, RenderComponents, CommandComponents, VulkanComponents.
# This is tricky with regex. Let's do it manually using replace.

# We will handle structs.h replacement carefully.
