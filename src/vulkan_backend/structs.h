/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"

#define MAX_FRAMES_IN_FLIGHT 3

// Structs

// New structs for streamlined state resource management

// New struct for per-frame images
typedef struct FrameImageGroup
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory imageMemory; // This won't be used for the final present images, memory is managed by the swapchain
} FrameImageGroup;



typedef struct ImageViewGroup
{ // Swapchain image views, should be next to the images and memory
	uint32_t viewCount;
	VkImageView* views;
	VkImageView colorView;
} ImageViewGroup;

typedef struct SwapChainGroup
{
	VkSwapchainKHR swapChain;
	VkFormat imageFormat;
	VkExtent2D imageExtent;
	uint32_t imageCount;
	VkImage* images;
	VkDeviceMemory imageMemory[MAX_FRAMES_IN_FLIGHT]; // Not actually used, swapchain image memory managed by Vulkan.
	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
} SwapChainGroup;

typedef struct InstanceDebugComponents
{
    bool enableValidationLayers;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
} InstanceDebugComponents;

typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
} DeviceCapabilities;

typedef struct QueueFamilyIndices // Stores whether different queue families exist, and which queue has been selected for each
{
	bool graphicsPresent;
    uint32_t graphicsFamily;
    bool computePresent;
    uint32_t computeFamily;
    bool transferPresent;
    uint32_t transferFamily;
    bool presentPresent;
    uint32_t presentFamily;
} QueueFamilyIndices;

typedef struct PhysicalDeviceComponents
{
    uint32_t deviceCount;
    char** availableDevices;
    VkPhysicalDevice physicalDevice;
    DeviceCapabilities deviceCapabilities;
    QueueFamilyIndices queueFamilyIndices;
	VkSampleCountFlagBits msaaSamples;
} PhysicalDeviceComponents;

typedef struct DeviceQueueComponents
{ // Necessary for just about every operation and pipeline, may need re-formatting if we go with multiple queues of one type
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue transferQueue;
    VkQueue presentQueue;
} DeviceQueueComponents;

typedef struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formatCount;
    VkSurfaceFormatKHR *formats;
    uint32_t presentModesCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef struct SwapChainComponents
{
    SwapChainGroup swapChainGroup;
    ImageViewGroup viewGroup;

    SwapChainSupportDetails swapChainSupportDetails;
} SwapChainComponents;

typedef struct RenderEntity
{ // To be extended with animation data
    uint32_t meshIndex;
    uint32_t materialIndex;   // index into MaterialSSBO
    mat4 transform;
} RenderEntity;

typedef struct BufferComponents 
{
	RenderEntity* entities;
	uint32_t entityCount;
	VkBuffer uniform[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory uniformMemory[MAX_FRAMES_IN_FLIGHT];
	void* uniformMapped[MAX_FRAMES_IN_FLIGHT];
	VkFormat depthFormat; // All depth resources should live next to the swapchain stuff
	VkImage depth[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory depthMemory[MAX_FRAMES_IN_FLIGHT];
	VkImageView depthView[MAX_FRAMES_IN_FLIGHT];
} BufferComponents;


typedef struct RenderComponents
{
    GlobalUBO uniform; // This comes from vertex.h, should probably have a buffer of length n = swap count, move to swapchain
	VkSampler textureSampler;   // Also many of these, maybe create whole struct for resource access formats
	BufferComponents buffers; // This entire thing should probably be moved to swapchain
} RenderComponents;

typedef struct SynchronizationComponents
{
    VkSemaphore imageAvailableSemaphore[MAX_FRAMES_IN_FLIGHT]; // All frame sync objects should also be in swapchain, they're per-frame
    VkSemaphore renderFinishedSemaphore[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFence[MAX_FRAMES_IN_FLIGHT];
	bool frameSubmitted[MAX_FRAMES_IN_FLIGHT]; // Used to keep track of which frames have been used, mitigates resize crash | Probably outdated, keeping for now
    uint32_t frameIndex; // Move to swapchain
    uint32_t imageIndex; // Used to track submitted frames for presentation, move to swapchain
    bool framebufferResized; // Swapchain
} SynchronizationComponents;

typedef struct CommandComponents
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer[MAX_FRAMES_IN_FLIGHT];
} CommandComponents;

typedef struct VulkanComponents
{
    InstanceDebugComponents instanceDebug;
    PhysicalDeviceComponents physicalDeviceComp;
    DeviceQueueComponents deviceQueueComp;
    SwapChainComponents swapChainComp;
    RenderComponents renderComp;
    SynchronizationComponents syncComp;
    CommandComponents cmdComp;
    VkSurfaceKHR surface;
} VulkanComponents;

typedef struct Dimensions2D
{
	uint32_t width;
	uint32_t height;
} Dimensions2D;

typedef struct WindowParameters
{
    uint32_t width;
    uint32_t height;
    uint32_t monitorIndex;        // Desired monitor index for fullscreen, -1 for windowed
    bool borderless;         // True for borderless, false otherwise
    // ... other parameters
} WindowParameters;

typedef struct VulkanSettings
{
	char* preferredDevice; // Physical GPU to use for rendering
	uint32_t preferredMode;	// Frame present mode
} VulkanSettings;

typedef struct MonitorInfo 
{
    const GLFWvidmode* modes;    // Video modes supported by the monitor
    int modeCount;               // Number of video modes supported
} MonitorInfo;

typedef struct Monitors 
{
    MonitorInfo* monitorInfos;   // Array of MonitorInfo for each monitor
    int monitorCount;           // Total number of monitors
} Monitors;


struct VulkanGarbage //All the various stuff that needs to be thrown out
{
	struct VulkanComponents *components;
	GLFWwindow *window;
	Monitors *monitors;
};

typedef struct TransformBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    mat4*           mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} TransformBuffer;

typedef struct MaterialData
{
    uint32_t    albedoIndex;        // index into bindless texture array
    uint32_t    normalIndex;        // 0 = no normal map
    float       roughness;          // non-PBR: controls stylized specular falloff
    float       emissive;           // emissive intensity multiplier
    float       color[4];           // tint color (RGBA)
} MaterialData;

typedef struct MaterialBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    MaterialData*   mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} MaterialBuffer;

typedef struct BindlessTextureArray
{
    VkDescriptorPool        pool;
    VkDescriptorSetLayout   layout;
    VkDescriptorSet         set;            // ONE set, holds ALL textures
    uint32_t                maxTextures;    // upper bound (e.g. 4096)
    uint32_t                textureCount;   // current count
    VkSampler               defaultSampler; // shared linear/repeat sampler
} BindlessTextureArray;

typedef struct CullUBO
{
    mat4 viewProj;
    Vector4 frustumPlanes[6];
    uint32_t entityCount;
    uint32_t padding[3];
} CullUBO;

typedef struct CullUboBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    CullUBO*        mapped[MAX_FRAMES_IN_FLIGHT];
} CullUboBuffer;

typedef struct IndirectDrawBuffer
{
    VkBuffer                        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation                   allocs[MAX_FRAMES_IN_FLIGHT];
    VkDrawIndexedIndirectCommand*   mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t                        capacity;
    uint32_t                        drawCount[MAX_FRAMES_IN_FLIGHT];
} IndirectDrawBuffer;

typedef struct RendererState
{
    // Pipeline system (Stage 0+)
    PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

    // Descriptor infrastructure (to be populated per-stage)
    VkDescriptorPool        globalDescriptorPool;
    VkDescriptorSetLayout   globalSetLayout;        // Set 0
    VkDescriptorSet         globalSets[MAX_FRAMES_IN_FLIGHT];

    // Synchronization — lifted from SynchronizationComponents
    VkSemaphore             imageAvailable[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore             renderFinished[MAX_FRAMES_IN_FLIGHT];
    VkFence                 frameFence[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    // Geometry
    GeometryPool            globalGeometryPool;
    RenderPrimitives        primitives;

    TransformBuffer         transformBuffer;
    MaterialBuffer          materialBuffer;
    IndirectDrawBuffer      indirectBuffer;
    CullUboBuffer           cullUboBuffer;
    BindlessTextureArray    bindlessTextures;

    // Culling system (Stage 6)
    VkDescriptorSetLayout   cullSetLayout;
    VkDescriptorSet         cullSets[MAX_FRAMES_IN_FLIGHT];
    VkBuffer                entityBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           entityAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   entityMapped[MAX_FRAMES_IN_FLIGHT];
    VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];
    VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];
    VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];

    RenderEntity*           entities;
    uint32_t                entityCount;
} RendererState;

#endif
