/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include "gpu_alloc.h"
#include <stdbool.h>
#include <string.h>


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "vulkan_backend/vertex/vertex.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"

#define MAX_FRAMES_IN_FLIGHT 3

#define FALLBACK_MESH_INDEX 0
#define FALLBACK_TEXTURE_INDEX 0

// Structs

// New structs for streamlined state resource management






typedef struct DeviceCapabilities // Add queue families, device extensions etc as they're implemented into compute tasks and render functions
{
	bool graphics;
	bool compute;
	bool transfer;
	bool float64;
	bool int64;
	bool drawIndirectCount;
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



typedef struct RenderEntity
{ // To be extended with animation data
    uint32_t meshIndex;
    uint32_t materialIndex;   // index into MaterialSSBO
    mat4 transform;
} RenderEntity;

typedef struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formatCount;
    VkSurfaceFormatKHR *formats;
    uint32_t presentModesCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef struct VulkanContext
{
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    bool                     enableValidationLayers;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         physicalDevice;
    uint32_t                 deviceCount;
    char**                   availableDevices;
    DeviceCapabilities       deviceCapabilities;
    QueueFamilyIndices       queueFamilyIndices;
    VkSampleCountFlagBits    msaaSamples;
    VkDevice                 device;
    VkQueue                  graphicsQueue;
    VkQueue                  computeQueue;
    VkQueue                  transferQueue;
    VkQueue                  presentQueue;
} VulkanContext;

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
	struct VulkanContext *ctx;
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

typedef struct AngularVelocityBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    Vector4*        mapped[MAX_FRAMES_IN_FLIGHT];  // xyz = axis * speed, w = unused
    uint32_t        capacity;   
    uint32_t        count;      
} AngularVelocityBuffer;

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

typedef enum DeletionResourceType {
    RESOURCE_TYPE_GEOMETRY_MESH,
    RESOURCE_TYPE_BINDLESS_TEXTURE
} DeletionResourceType;

typedef struct DeletionTask {
    DeletionResourceType type;
    uint32_t handle;
} DeletionTask;

typedef struct DeletionQueue {
    DeletionTask* tasks;
    uint32_t count;
    uint32_t capacity;
} DeletionQueue;

typedef struct CullingBuffers {
    CullUboBuffer           ubo;

    // Per-entity culling input
    VkBuffer                entityBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           entityAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   entityMapped[MAX_FRAMES_IN_FLIGHT];

    // Mesh draw parameters (firstIndex, indexCount, vertexOffset per mesh)
    VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];

    // Bounding volumes for frustum testing
    VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written draw count (atomic counter output from cull shader)
    VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;


    // Capacity tracking
    uint32_t                maxEntities;
} CullingBuffers;

typedef struct PerFrameResources
{
    // Synchronization
    VkSemaphore         imageAvailable;
    VkSemaphore         renderFinished;
    VkFence             frameFence;
    bool                frameSubmitted;

    // Command recording
    VkCommandBuffer     commandBuffer;

    // Global UBO (view/proj)
    VkBuffer            uniformBuffer;
    GpuAllocation       uniformAlloc;
    void*               uniformMapped;

    // Depth attachment
    VkImage             depthImage;
    GpuAllocation       depthAlloc;
    VkImageView         depthView;

    // Descriptor sets
    VkDescriptorSet     globalSet;
    VkDescriptorSet     cullSet;
    VkDescriptorSet     updateSet;

    // Deferred resource deletion
    DeletionQueue       deletionQueue;
} PerFrameResources;

typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;

    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    uint32_t                imageCount;
    VkImage*                images;
    VkImage                 colorImage;
    GpuAllocation           colorImageAlloc;
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
    VkFormat                depthFormat;

    // Pipeline system (Stage 0+)
    PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

    // Descriptor infrastructure (to be populated per-stage)
    VkDescriptorPool        globalDescriptorPool;
    VkDescriptorSetLayout   globalSetLayout;        // Set 0

    // Geometry
    GeometryPool            globalGeometryPool;
    RenderPrimitives        primitives;

    TransformBuffer         transformBuffer;
    TransformBuffer         initialTransformBuffer;
    AngularVelocityBuffer   angularVelocityBuffer;
    MaterialBuffer          materialBuffer;
    IndirectDrawBuffer      indirectBuffer;
    BindlessTextureArray    bindlessTextures;
    
    VkDescriptorSetLayout   updateSetLayout;

    // Fallback resources
    VkImage                 fallbackImage;
    VkImageView             fallbackImageView;



    // Culling system
    CullingBuffers          culling;
} RendererState;


#endif
