import os
import re

dir_path = "/home/cris/Documents/anoptic-engine/src"

replacements = [
    # InstanceDebugComponents
    (r"(components|comps)->instanceDebug\.instance", r"\1->instance"),
    (r"components\.instanceDebug\.instance", r"ctx.instance"),
    (r"(components|comps)->instanceDebug\.debugMessenger", r"\1->debugMessenger"),
    (r"components\.instanceDebug\.debugMessenger", r"ctx.debugMessenger"),
    (r"(components|comps)->instanceDebug\.enableValidationLayers", r"\1->enableValidationLayers"),
    (r"components\.instanceDebug\.enableValidationLayers", r"ctx.enableValidationLayers"),
    
    # PhysicalDeviceComponents
    (r"(components|comps)->physicalDeviceComp\.physicalDevice", r"\1->physicalDevice"),
    (r"components\.physicalDeviceComp\.physicalDevice", r"ctx.physicalDevice"),
    (r"(components|comps)->physicalDeviceComp\.msaaSamples", r"\1->msaaSamples"),
    (r"components\.physicalDeviceComp\.msaaSamples", r"ctx.msaaSamples"),
    (r"(components|comps)->physicalDeviceComp\.deviceCapabilities", r"\1->deviceCapabilities"),
    (r"components\.physicalDeviceComp\.deviceCapabilities", r"ctx.deviceCapabilities"),
    (r"(components|comps)->physicalDeviceComp\.queueFamilyIndices", r"\1->queueFamilyIndices"),
    (r"components\.physicalDeviceComp\.queueFamilyIndices", r"ctx.queueFamilyIndices"),
    (r"(components|comps)->physicalDeviceComp\.availableDevices", r"\1->availableDevices"),
    (r"components\.physicalDeviceComp\.availableDevices", r"ctx.availableDevices"),
    (r"(components|comps)->physicalDeviceComp\.deviceCount", r"\1->deviceCount"),
    (r"components\.physicalDeviceComp\.deviceCount", r"ctx.deviceCount"),

    # DeviceQueueComponents
    (r"(components|comps)->deviceQueueComp\.device", r"\1->device"),
    (r"components\.deviceQueueComp\.device", r"ctx.device"),
    (r"(components|comps)->deviceQueueComp\.graphicsQueue", r"\1->graphicsQueue"),
    (r"components\.deviceQueueComp\.graphicsQueue", r"ctx.graphicsQueue"),
    (r"(components|comps)->deviceQueueComp\.presentQueue", r"\1->presentQueue"),
    (r"components\.deviceQueueComp\.presentQueue", r"ctx.presentQueue"),
    (r"(components|comps)->deviceQueueComp\.computeQueue", r"\1->computeQueue"),
    (r"components\.deviceQueueComp\.computeQueue", r"ctx.computeQueue"),
    (r"(components|comps)->deviceQueueComp\.transferQueue", r"\1->transferQueue"),
    (r"components\.deviceQueueComp\.transferQueue", r"ctx.transferQueue"),

    # Surface
    (r"(components|comps)->surface", r"\1->surface"),
    (r"components\.surface", r"ctx.surface"),

    # SwapChainGroup
    (r"(components|comps)->swapChainComp\.swapChainGroup\.swapChain", r"state->swapChain"),
    (r"components\.swapChainComp\.swapChainGroup\.swapChain", r"rendererState.swapChain"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.imageFormat", r"state->imageFormat"),
    (r"components\.swapChainComp\.swapChainGroup\.imageFormat", r"rendererState.imageFormat"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.imageExtent", r"state->imageExtent"),
    (r"components\.swapChainComp\.swapChainGroup\.imageExtent", r"rendererState.imageExtent"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.images", r"state->images"),
    (r"components\.swapChainComp\.swapChainGroup\.images", r"rendererState.images"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.imageCount", r"state->imageCount"),
    (r"components\.swapChainComp\.swapChainGroup\.imageCount", r"rendererState.imageCount"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.colorImageMemory", r"state->colorImageMemory"),
    (r"components\.swapChainComp\.swapChainGroup\.colorImageMemory", r"rendererState.colorImageMemory"),
    (r"(components|comps)->swapChainComp\.swapChainGroup\.colorImage", r"state->colorImage"),
    (r"components\.swapChainComp\.swapChainGroup\.colorImage", r"rendererState.colorImage"),

    # ImageViewGroup
    (r"(components|comps)->swapChainComp\.viewGroup\.views", r"state->views"),
    (r"components\.swapChainComp\.viewGroup\.views", r"rendererState.views"),
    (r"(components|comps)->swapChainComp\.viewGroup\.viewCount", r"state->viewCount"),
    (r"components\.swapChainComp\.viewGroup\.viewCount", r"rendererState.viewCount"),
    (r"(components|comps)->swapChainComp\.viewGroup\.colorView", r"state->colorView"),
    (r"components\.swapChainComp\.viewGroup\.colorView", r"rendererState.colorView"),

    # RenderComponents
    (r"(components|comps)->renderComp\.uniform", r"state->uboData"),
    (r"components\.renderComp\.uniform", r"rendererState.uboData"),
    (r"(components|comps)->renderComp\.textureSampler", r"state->textureSampler"),
    (r"components\.renderComp\.textureSampler", r"rendererState.textureSampler"),
    (r"(components|comps)->renderComp\.buffers\.entities", r"state->entities"),
    (r"components\.renderComp\.buffers\.entities", r"rendererState.entities"),
    (r"(components|comps)->renderComp\.buffers\.entityCount", r"state->entityCount"),
    (r"components\.renderComp\.buffers\.entityCount", r"rendererState.entityCount"),
    (r"(components|comps)->renderComp\.buffers\.depthFormat", r"state->depthFormat"),
    (r"components\.renderComp\.buffers\.depthFormat", r"rendererState.depthFormat"),

    # CommandComponents
    (r"(components|comps)->cmdComp\.commandPool", r"state->commandPool"),
    (r"components\.cmdComp\.commandPool", r"rendererState.commandPool"),

    # Signatures: change VulkanComponents to VulkanContext
    (r"VulkanComponents\*", r"VulkanContext*"),
    (r"VulkanComponents\s+components", r"VulkanContext ctx"),
    (r"VulkanComponents\s+\*components", r"VulkanContext* ctx"),

    # Swap variables
    (r"&components", r"&ctx"),
    (r"components\.", r"ctx."),
    (r"comps->", r"ctx->"), # Some places used comps
    (r"components->", r"ctx->"),
]

for root, _, files in os.walk(dir_path):
    for f in files:
        if not (f.endswith(".c") or f.endswith(".h")): continue
        filepath = os.path.join(root, f)
        with open(filepath, "r") as file:
            content = file.read()
        
        orig = content
        for pattern, replacement in replacements:
            content = re.sub(pattern, replacement, content)
            
        if content != orig:
            print(f"Updated {filepath}")
            with open(filepath, "w") as file:
                file.write(content)
