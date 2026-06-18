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
#include "vulkan_backend/render_slots.h"
#include <anoptic_render_bridge.h>

#define MAX_FRAMES_IN_FLIGHT 3

#define FALLBACK_MESH_INDEX 0
#define FALLBACK_TEXTURE_INDEX 0

// Sentinel values for optional entity components.
// NO_MESH_INDEX marks a transform-only entity (e.g. a pure light) that the
// culling pass must skip so it draws no geometry. NO_LIGHT_INDEX marks an
// entity that carries no light. Both match the 0xFFFFFFFF "absent" convention
// already used for optional bindless textures in MaterialData.
#define NO_MESH_INDEX  0xFFFFFFFFu
#define NO_LIGHT_INDEX 0xFFFFFFFFu

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
	bool meshShader;            // VK_EXT_mesh_shader present and meshShader feature usable; false selects the vertex-shader fallback path
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
    uint32_t meshIndex;       // index into geometry pool, or NO_MESH_INDEX for transform-only entities
    uint32_t materialIndex;   // index into MaterialSSBO
    uint32_t lightIndex;      // index into the light SSBO, or NO_LIGHT_INDEX if this entity is not a light
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
    // Feature flags identifying which features are active in this material
    uint32_t    features;           // PbrFeatureFlags bitmask
    uint32_t    baseColorTexture;   // Index in bindless array (0 = fallback)
    uint32_t    pad0[2];            // Align baseColorFactor to 16 bytes

    // 1. pbrMetallicRoughness
    float       baseColorFactor[4];
    uint32_t    metallicRoughnessTexture;
    float       metallicFactor;
    float       roughnessFactor;

    // 2. Core Material Properties
    uint32_t    normalTexture;
    float       normalScale;
    uint32_t    occlusionTexture;
    float       occlusionStrength;
    uint32_t    emissiveTexture;
    
    // 3. Emissive Factor (aligned to 16 bytes since offset is 64)
    float       emissiveFactor[4];  // RGB + 1 padding element
    uint32_t    alphaMode;          // 0 = OPAQUE, 1 = MASK, 2 = BLEND
    float       alphaCutoff;
    uint32_t    doubleSided;        // 0 = false, 1 = true

    // 4. KHR_materials_clearcoat
    uint32_t    clearcoatTexture;
    uint32_t    clearcoatRoughnessTexture;
    uint32_t    clearcoatNormalTexture;
    float       clearcoatFactor;
    float       clearcoatRoughnessFactor;

    // 5. KHR_materials_transmission
    uint32_t    transmissionTexture;
    float       transmissionFactor;

    // 6. KHR_materials_volume
    uint32_t    thicknessTexture;
    float       thicknessFactor;
    float       attenuationDistance;
    uint32_t    pad1[3];            // Align attenuationColor to 16 bytes

    float       attenuationColor[4]; // RGB + 1 padding element

    // 7. KHR_materials_ior
    float       ior;

    // 8. KHR_materials_specular
    uint32_t    specularTexture;
    uint32_t    specularColorTexture;
    float       specularFactor;

    float       specularColorFactor[4]; // RGB + 1 padding element

    // 9. KHR_materials_sheen
    uint32_t    sheenColorTexture;
    uint32_t    sheenRoughnessTexture;
    uint32_t    pad2[2];            // Align sheenColorFactor to 16 bytes

    float       sheenColorFactor[4]; // RGB + 1 padding element
    float       sheenRoughnessFactor;

    // 10. KHR_materials_iridescence
    uint32_t    iridescenceTexture;
    uint32_t    iridescenceThicknessTexture;
    float       iridescenceFactor;
    float       iridescenceIor;
    float       iridescenceThicknessMinimum;
    float       iridescenceThicknessMaximum;

    // 11. KHR_materials_anisotropy
    uint32_t    anisotropyTexture;
    float       anisotropyStrength;
    float       anisotropyRotation;

    // 12. KHR_materials_dispersion
    float       dispersion;

    // 13. KHR_materials_diffuse_transmission
    uint32_t    diffuseTransmissionTexture;
    uint32_t    diffuseTransmissionColorTexture;
    float       diffuseTransmissionFactor;
    uint32_t    pad3[2];            // Align diffuseTransmissionColorFactor to 16 bytes

    float       diffuseTransmissionColorFactor[4]; // RGB + 1 padding element

    // 14. KHR_materials_emissive_strength
    float       emissiveStrength;

    uint32_t    pipelineType;

    // Final padding to align structure size to a multiple of 16 (total size = 320 bytes)
    uint32_t    padding[2];
} MaterialData;

typedef struct MaterialBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    MaterialData*   mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} MaterialBuffer;

// ---------------------------------------------------------------------------
// Lighting
//
// Punctual light sources following the glTF KHR_lights_punctual model, mirroring
// the engine's glTF-based MaterialData convention. A light is an entity
// component: its world-space position and direction are NOT stored here, they
// are derived in the fragment shader from the driving entity's live transform
// (transforms[transformIndex]) so GPU animation (orbit/spin) applies for free.
// Only photometric parameters and the transform link live in this struct.
//
// LightData is laid out as 3 x vec4 (48 bytes) for std430. The leading vec3 +
// float pack into one 16-byte row (standard std430 vec3+scalar packing); the C
// layout below matches byte-for-byte.
// ---------------------------------------------------------------------------
typedef enum LightType
{
    LIGHT_TYPE_DIRECTIONAL = 0, // infinitely distant, uses direction only
    LIGHT_TYPE_POINT       = 1, // omnidirectional, uses position + range
    LIGHT_TYPE_SPOT        = 2, // cone, uses position + direction + range + cones
} LightType;

typedef struct LightData
{
    // row 0
    float       color[3];       // linear RGB, normalized (intensity carries magnitude)
    float       intensity;      // brightness multiplier (candela-like for point/spot, lux-like for directional)
    // row 1
    float       range;          // attenuation cutoff distance; <= 0 means unbounded (ignored for directional)
    float       innerConeCos;   // cosine of spot inner cone half-angle (full intensity within)
    float       outerConeCos;   // cosine of spot outer cone half-angle (zero intensity beyond)
    uint32_t    type;           // LightType
    // row 2
    uint32_t    transformIndex; // entity/transform index that drives world position + direction
    uint32_t    enabled;        // 0 = ignored, 1 = active
    uint32_t    pad0;
    uint32_t    pad1;
} LightData; // 48 bytes

typedef struct LightBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    LightData*      mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max lights
    uint32_t        count;      // current light count
} LightBuffer;

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
    uint32_t maxEntities;
    uint32_t padding[2];
} CullUBO;

typedef struct CullUboBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    CullUBO*        mapped[MAX_FRAMES_IN_FLIGHT];
} CullUboBuffer;

typedef struct IndirectDrawBuffer
{
    VkBuffer                            buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation                       allocs[MAX_FRAMES_IN_FLIGHT];
    VkDrawMeshTasksIndirectCommandEXT*  mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t                            capacity;
    uint32_t                            drawCount[MAX_FRAMES_IN_FLIGHT];
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

    // GPU-written compacted entity indices (1-to-1 mapping for visible elements)
    VkBuffer                compactedEntityIndicesBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           compactedEntityIndicesAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               compactedEntityIndicesMapped[MAX_FRAMES_IN_FLIGHT];

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;


    // Capacity tracking
    uint32_t                maxEntities;
} CullingBuffers;

// A command buffered until it has been applied to every frame-in-flight copy of
// the mapped GPU buffers. pendingFrameMask starts at (1<<MAX_FRAMES_IN_FLIGHT)-1
// and clears one bit per frame the command is applied to.
typedef struct PendingRenderCommand
{
    RenderCommand cmd;
    uint32_t      pendingFrameMask;
} PendingRenderCommand;

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
    LightBuffer             lightBuffer;
    IndirectDrawBuffer      indirectBuffer;
    BindlessTextureArray    bindlessTextures;
    
    VkDescriptorSetLayout   updateSetLayout;

    // Fallback resources
    VkImage                 fallbackImage;
    VkImageView             fallbackImageView;



    // Culling system
    CullingBuffers          culling;

    // ECS <-> render bridge (VK_BACKEND_INTEROP.md). The render master owns the
    // slot authority and consumes discrete state-transition commands; per-entity
    // GPU layout is keyed off render_slots, never the logic-side entity index.
    mi_heap_t              *renderHeap;     // backs slot table + bridge rings + pending list
    RenderSlotTable         slots;          // logical render_id -> stable GPU slot
    AnoRenderBridge         bridge;         // logic->render commands, render->logic events
    uint64_t                globalFrame;    // monotonic frame counter for slot quarantine
    PendingRenderCommand   *pending;        // commands still propagating across frames in flight
    uint32_t                pendingCount;
    uint32_t                pendingCapacity;
} RendererState;


#endif
