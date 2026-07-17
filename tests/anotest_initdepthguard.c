/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the FATAL-log-instead-of-return arms in initVulkan. createDepthResources' failure arm
// at vulkanMaster.c:503-506 and createHiZResources' at :509-512 log ANO_FATAL "Quitting init" and
// fall through 〜 ano_log(ANO_FATAL) is ano_log_write, which formats a record and returns 0;
// nothing in src/log aborts 〜 with no unInitVulkan and no return, while every sibling arm in the
// same function proves the intended contract with unInitVulkan(); return false; (:444-:446,
// :452-:454, :518-:520, :524-:526, ...). So a failed depth or Hi-Z creation keeps initializing:
// the layout/pipeline steps build against a depthFormat never set, updateHiZDescriptorSets (:664)
// writes the never-created hizSampledView/hizMipViews into live per-mip sets, the hizValidOrdinal
// warmup gate stays 0 because attachments.c:185 runs only on success, and initVulkan returns true
// (:687) 〜 a "healthy" renderer with no depth attachment and a Hi-Z cull sampling a pyramid that
// does not exist. createColorResources directly above (:501) cannot even report failure (void,
// the author's own "// TODO: make bool + check") (docs/BUGS.md, Render / Vulkan backend /
// Implementation, vulkanMaster.c:505).
// Harness: compiles the REAL vulkanMaster.c TU and satisfies its link seams with stubs 〜 no GPU
// device, no loader, no glfw. Every init helper records its step into an ordered trace; the
// controllable failure arms are contract-faithful (createDepthResources' cleanest real arm is
// findDepthFormat returning VK_FORMAT_UNDEFINED at attachments.c:63, which touches nothing).
// Controls prove the good path boots start-to-finish in order and that a guarded sibling arm
// (global layout) reports false, tears down, and runs nothing further 〜 so a reject-everything
// fix cannot pass. Fix-agnostic invariants: a failed mandatory resource step means initVulkan
// does not report success, does not run init to completion, and no descriptor write consumes the
// absent Hi-Z resources. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"   // RendererState, rendererState extern, instanceInit + vulkanConfig decls
#include "vulkan_backend/backend.h"        // ctx extern
#include "vulkan_backend/gpu_alloc.h"      // GpuAllocator
#include "vulkan_backend/frame/frame.h"    // drawFrame seams, anoperf accumulator
#include "vulkan_backend/scene_buffers.h"  // ano_vk_create_scene_resources, createFallbackResources
#include "vulkan_backend/render_api.h"     // ano_render_load_scene_assets
#include "vulkan_backend/bridge/bridge.h"  // render_apply_commands
#include "vulkan_backend/light_registry.h" // light_registry_init/destroy
#include "vulkan_backend/text_raster.h"    // ano_vk_text_* seams
#include "vulkan_backend/ui_raster.h"      // ano_vk_ui_* seams

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ordered step trace 〜 which init helpers ran, in what order */

enum {
    ST_INSTANCE = 1, ST_GEOM_POOL, ST_SWAPCHAIN, ST_IMAGE_VIEWS, ST_CMD_POOL, ST_COLOR,
    ST_DEPTH, ST_HIZ, ST_LAY_GLOBAL, ST_LAY_CULL, ST_LAY_MATERIAL, ST_BINDLESS,
    ST_PIPELINES, ST_TONEMAP, ST_TEXT, ST_UI, ST_SHADOW, ST_SAMPLER, ST_FALLBACK,
    ST_SCENE, ST_ASSETS, ST_SLOTS, ST_BRIDGE, ST_UBO, ST_DESC_POOL, ST_DESC_SETS,
    ST_UPD_UBO, ST_UPD_TONEMAP, ST_UPD_HIZ, ST_UPD_CLUSTER, ST_UPD_SHADOW,
    ST_CMDBUF, ST_SYNC, ST_TEARDOWN
};

#define MAX_TRACE 64
static int g_trace[MAX_TRACE];
static int g_traceLen;

/* Failure injection + minted-resource state */
static bool g_failDepth;
static bool g_failHiZ;
static bool g_failGlobalLayout;
static bool g_depthLive;
static bool g_hizLive;

// Appends one step id to the ordered trace.
static void mark(int step)
{
    if (g_traceLen < MAX_TRACE) g_trace[g_traceLen++] = step;
}

// First trace index of step, or -1.
static int idx_of(int step)
{
    for (int i = 0; i < g_traceLen; i++) if (g_trace[i] == step) return i;
    return -1;
}

// True when step appears anywhere in the trace.
static bool ran(int step)
{
    return idx_of(step) >= 0;
}

// Tears down any prior run, then zeroes the TU globals and the trace for a fresh phase.
static void fresh_world(void)
{
    unInitVulkan(); // idempotent through the stubs; discharges a prior run's real renderHeap
    memset(&rendererState, 0, sizeof rendererState);
    memset(&ctx, 0, sizeof ctx);
    g_traceLen = 0;
    g_depthLive = false;
    g_hizLive = false;
}


/* Link seams 〜 globals the TU (and its headers) reference from objects this exe does not link */

uint32_t g_ValidationErrors = 0;
anoperf_accumulator_t g_perfAcc;

// Never reached (drawFrame never runs); resets the window like the real flush.
void anoperf_flush(anoperf_accumulator_t* acc) { acc->count = 0; }


/* Link seams 〜 vulkanConfig.c */

bool requestPresentMode(VkPresentModeKHR presentMode) { (void)presentMode; return true; }
bool setResolution(Dimensions2D dimensions) { (void)dimensions; return true; }
bool setMonitor(uint32_t index) { (void)index; return true; }
bool setBorderless(bool borderless) { (void)borderless; return true; }
char* getChosenDevice(void) { return NULL; }
VkPresentModeKHR getChosenPresentMode(void) { return VK_PRESENT_MODE_FIFO_KHR; }


/* Link seams 〜 instance/ init helpers (real definitions live in anoptic_render, not linked) */

VkResult createInstance(VulkanContext* c) { mark(ST_INSTANCE); c->instance = (VkInstance)(uintptr_t)0x11; return VK_SUCCESS; }
void cleanupVulkan(VulkanContext* c) { (void)c; mark(ST_TEARDOWN); }
GLFWwindow* initWindow(VulkanContext* c, Monitors* monitors) { (void)c; (void)monitors; return (GLFWwindow*)(uintptr_t)0x12; }
void enumerateMonitors(Monitors* monitors) { (void)monitors; }
void cleanupMonitors(Monitors* monitors) { (void)monitors; }
VkResult createSurface(VkInstance instance, GLFWwindow* window, VkSurfaceKHR* surface)
{ (void)instance; (void)window; *surface = (VkSurfaceKHR)(uintptr_t)0x13; return VK_SUCCESS; }

// out: capabilities all-false 〜 pins asyncHiz/asyncLc/taskCull off, so init walks the plain lane.
bool pickPhysicalDevice(VulkanContext* c, DeviceCapabilities* capabilities, struct QueueFamilyIndices* indices, char* preferredDevice)
{
    (void)preferredDevice;
    c->physicalDevice = (VkPhysicalDevice)(uintptr_t)0x14;
    c->deviceCapabilities = (DeviceCapabilities){0};
    *capabilities = (DeviceCapabilities){0};
    *indices = (QueueFamilyIndices){ .graphicsPresent = true, .presentPresent = true };
    c->msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    return true;
}

VkResult createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice* device, VkQueue* graphicsQueue, VkQueue* computeQueue, VkQueue* transferQueue, VkQueue* presentQueue, struct QueueFamilyIndices* indices)
{
    (void)physicalDevice; (void)indices;
    *device = (VkDevice)(uintptr_t)0x15;
    *graphicsQueue = *computeQueue = *transferQueue = *presentQueue = (VkQueue)(uintptr_t)0x16;
    return VK_SUCCESS;
}

bool initSwapChain(VulkanContext* c, GLFWwindow* window, uint32_t preferredMode, VkSwapchainKHR oldSwapChain, RendererState* state)
{
    (void)c; (void)window; (void)preferredMode; (void)oldSwapChain;
    mark(ST_SWAPCHAIN);
    state->swapChain = (VkSwapchainKHR)(uintptr_t)0x17;
    state->imageExtent = (VkExtent2D){ 800, 600 };
    state->viewExtent[0] = (VkExtent2D){ 800, 600 };
    state->viewExtent[1] = (VkExtent2D){ 200, 150 };
    return true;
}

void recreateSwapChain(VulkanContext* c, GLFWwindow* window) { (void)c; (void)window; }

bool createImageViews(VulkanContext* c, RendererState* state)
{
    (void)c;
    mark(ST_IMAGE_VIEWS);
    static VkImageView s_fakeViews[8];
    state->views = s_fakeViews;
    return true;
}

bool createCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkCommandPool* commandPool)
{ (void)device; (void)physicalDevice; (void)surface; mark(ST_CMD_POOL); *commandPool = (VkCommandPool)(uintptr_t)0x18; return true; }

void createColorResources(VulkanContext* c) { (void)c; mark(ST_COLOR); }

// Contract-faithful failure: the cleanest real arm (findDepthFormat VK_FORMAT_UNDEFINED,
// attachments.c:63-67) returns false having touched nothing.
bool createDepthResources(VulkanContext* c, RendererState* state)
{
    (void)c;
    mark(ST_DEPTH);
    if (g_failDepth) return false;
    state->depthFormat = VK_FORMAT_D32_SFLOAT;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            state->frames[i].views[v].depthImage = (VkImage)(uintptr_t)0x19;
            state->frames[i].views[v].depthView = (VkImageView)(uintptr_t)0x1A;
        }
    g_depthLive = true;
    return true;
}

// Contract-faithful failure: the real first-image arm (attachments.c:143-151) has already
// stamped view 0's pyramid dimensions when createImageShared refuses; no image exists.
bool createHiZResources(VulkanContext* c, RendererState* state)
{
    (void)c;
    mark(ST_HIZ);
    if (g_failHiZ) {
        state->frames[0].views[0].hizWidth = 400;
        state->frames[0].views[0].hizHeight = 300;
        state->frames[0].views[0].hizMipCount = 9;
        return false;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &state->frames[i].views[v];
            vr->hizImage = (VkImage)(uintptr_t)0x1B;
            vr->hizSampledView = (VkImageView)(uintptr_t)0x1C;
            vr->hizMipCount = 9;
            for (uint32_t m = 0; m < vr->hizMipCount; m++) vr->hizMipViews[m] = (VkImageView)(uintptr_t)0x1D;
        }
    state->hizValidOrdinal = state->timelineOrdinal + 2u; // success-only, like attachments.c:185
    g_hizLive = true;
    return true;
}

void updateHiZDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UPD_HIZ); }
bool createDescriptorPool(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_DESC_POOL); return true; }
bool createBindlessTextureArray(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_BINDLESS); return true; }
bool createDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_DESC_SETS); return true; }
void updateUboDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UPD_UBO); }
void updateTonemapDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UPD_TONEMAP); }
void updateClusterDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UPD_CLUSTER); }
void updateShadowDescriptorSets(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UPD_SHADOW); }
VkCommandBuffer beginSingleTimeCommands(VulkanContext* c) { (void)c; return (VkCommandBuffer)(uintptr_t)0x1E; }
void endSingleTimeCommands(VulkanContext* c, VkCommandBuffer commandBuffer) { (void)c; (void)commandBuffer; }
bool createUniformBuffers(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UBO); return true; }
bool createCommandBuffer(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_CMDBUF); return true; }
bool createSyncObjects(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_SYNC); return true; }


/* Link seams 〜 layouts, pipelines, overlays, scene (anoptic_render, not linked) */

bool ano_vk_init_global_layout(VulkanContext* c, RendererState* state)
{ (void)c; (void)state; mark(ST_LAY_GLOBAL); return !g_failGlobalLayout; }
bool ano_vk_init_cull_layout(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_LAY_CULL); return true; }
bool ano_vk_init_material_layouts(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_LAY_MATERIAL); return true; }
bool ano_vk_init_pipelines(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_PIPELINES); return true; }
bool ano_vk_init_tonemap(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_TONEMAP); return true; }
bool ano_vk_init_shadow(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_SHADOW); return true; }
bool ano_vk_text_init(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_TEXT); return true; }
void ano_vk_text_create_sets(VulkanContext* c, RendererState* state) { (void)c; (void)state; }
void ano_vk_text_frame_refresh(RendererState* state, uint32_t frameIndex) { (void)state; (void)frameIndex; }
void ano_vk_text_submit_async(VulkanContext* c, RendererState* state, uint32_t frameIndex, uint64_t ordinal)
{ (void)c; (void)state; (void)frameIndex; (void)ordinal; }
bool ano_vk_ui_init(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_UI); return true; }
void ano_vk_ui_frame_refresh(RendererState* state, uint32_t frameIndex) { (void)state; (void)frameIndex; }
bool createTextureSampler(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_SAMPLER); return true; }
bool createFallbackResources(VulkanContext* c, RendererState* state) { (void)c; (void)state; mark(ST_FALLBACK); return true; }
bool ano_vk_create_scene_resources(void) { mark(ST_SCENE); return true; }
bool ano_render_load_scene_assets(void) { mark(ST_ASSETS); return true; }
bool ano_vk_init_geometry_pool(GeometryPool* pool, GpuAllocator* alloc, VkDevice device, uint32_t graphicsFamily, uint32_t transferFamily)
{ (void)pool; (void)alloc; (void)device; (void)graphicsFamily; (void)transferFamily; mark(ST_GEOM_POOL); return true; }
void geometry_pool_free(GeometryPool* pool, uint32_t meshIndex) { (void)pool; (void)meshIndex; }


/* Link seams 〜 bridge / slots / lights (render_slots.c, ano_render_bridge.c, light_registry.c not linked) */

bool render_slots_init(RenderSlotTable* table, mi_heap_t* heap, uint32_t maxSlots, uint32_t framesInFlight)
{ (void)table; (void)heap; (void)maxSlots; (void)framesInFlight; mark(ST_SLOTS); return true; }
void render_slots_destroy(RenderSlotTable* table) { (void)table; }
bool ano_render_bridge_init(AnoRenderBridge* bridge, mi_heap_t* heap, uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2)
{ (void)bridge; (void)heap; (void)cmd_capacity_pow2; (void)evt_capacity_pow2; mark(ST_BRIDGE); return true; }
void ano_render_bridge_destroy(AnoRenderBridge* bridge) { (void)bridge; }
void light_registry_init(LightRegistry* r, uint32_t base, uint32_t capacity, uint32_t framesInFlight)
{ (void)r; (void)base; (void)capacity; (void)framesInFlight; }
void light_registry_destroy(LightRegistry* r) { (void)r; }
void render_apply_commands(RendererState* state, uint32_t frameIndex) { (void)state; (void)frameIndex; }


/* Link seams 〜 drawFrame's frame/ helpers (never executed, linked only) */

void recordHiZCompute(uint32_t frameIndex) { (void)frameIndex; }
void recordLightcullCompute(uint32_t frameIndex) { (void)frameIndex; }
void recordCommandBuffer(uint32_t imageIndex) { (void)imageIndex; }
bool ano_frame_submit(uint64_t ordinal) { (void)ordinal; return true; }
bool updateUniformBuffer(VulkanContext* c, RendererState* state) { (void)c; (void)state; return true; }
void updateCullingBuffers(VulkanContext* c, RendererState* state, uint32_t frameIndex) { (void)c; (void)state; (void)frameIndex; }
void updateTransformBuffer(VulkanContext* c, RendererState* state, uint32_t frameIndex) { (void)c; (void)state; (void)frameIndex; }
void ano_collect_frame_stats(uint32_t frameIndex) { (void)frameIndex; }
void ano_collect_pick(uint32_t frameIndex) { (void)frameIndex; }


/* Link seams 〜 the vk* entry points the TU calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{ (void)device; (void)fenceCount; (void)pFences; (void)waitAll; (void)timeout; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout)
{ (void)device; (void)pWaitInfo; (void)timeout; return VK_SUCCESS; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{ (void)device; (void)pName; return NULL; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{ (void)physicalDevice; memset(pMemoryProperties, 0, sizeof *pMemoryProperties); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pCommandPool = (VkCommandPool)(uintptr_t)0x1F; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
{ (void)device; (void)pAllocateInfo; *pCommandBuffers = (VkCommandBuffer)(uintptr_t)0x20; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{ (void)commandBuffer; (void)dstBuffer; (void)dstOffset; (void)size; (void)data; }
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{ (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)fence; *pImageIndex = 0; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{ (void)commandBuffer; (void)flags; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{ (void)queue; (void)pPresentInfo; return VK_SUCCESS; }


/* Link seams 〜 the glfw entry points the TU references (glfw not linked, never executed) */

void glfwDestroyWindow(GLFWwindow* window) { (void)window; }
void glfwTerminate(void) { }
int glfwWindowShouldClose(GLFWwindow* window) { (void)window; return 1; }


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    bool ok;

    // control: a guarded sibling arm (global layout, vulkanMaster.c:516-521) reports false,
    // tears down, and runs nothing further 〜 failure reporting works through this exact seam
    fresh_world();
    g_failGlobalLayout = true;
    ok = initVulkan();
    g_failGlobalLayout = false;
    CHECK(!ok, "sibling arm: initVulkan reports global-layout failure");
    CHECK(ran(ST_TEARDOWN), "sibling arm: unInitVulkan tears down before returning");
    CHECK(!ran(ST_UPD_HIZ) && !ran(ST_SYNC), "sibling arm: nothing runs past the failed arm");

    // control: the good path boots start-to-finish, in order, and reports success
    fresh_world();
    ok = initVulkan();
    CHECK(ok, "good path: initVulkan reports success");
    CHECK(ran(ST_DEPTH) && ran(ST_HIZ) && ran(ST_UPD_HIZ) && ran(ST_SYNC), "good path: full init sequence runs");
    CHECK(idx_of(ST_DEPTH) < idx_of(ST_HIZ) && idx_of(ST_HIZ) < idx_of(ST_LAY_GLOBAL), "good path: depth then Hi-Z precede the layouts");
    CHECK(!ran(ST_TEARDOWN), "good path: no teardown");
    CHECK(g_depthLive && g_hizLive, "good path: depth + Hi-Z resources minted");

    // trigger: depth-resource failure 〜 unfixed, vulkanMaster.c:505 logs FATAL "Quitting init"
    // and falls through with no unInitVulkan and no return, so init keeps going and reports true
    printf("trigger: depth-resource failure 〜 expect FATAL-log fall-through at vulkanMaster.c:503-506\n");
    fflush(stdout);
    fresh_world();
    g_failDepth = true;
    ok = initVulkan();
    g_failDepth = false;
    CHECK(!ok, "initVulkan must not report success after depth-resource failure (vulkanMaster.c:505 falls through)");
    CHECK(!ran(ST_UPD_HIZ), "no descriptor write consumes the absent depth/Hi-Z resources (vulkanMaster.c:664)");
    CHECK(!ran(ST_SYNC), "init must not run to completion past the failed depth arm");

    // trigger: Hi-Z failure with depth healthy 〜 unfixed, vulkanMaster.c:511 falls through the
    // same way, and the hizValidOrdinal warmup gate stays 0 (attachments.c:185 is success-only)
    printf("trigger: Hi-Z resource failure 〜 expect FATAL-log fall-through at vulkanMaster.c:509-512\n");
    fflush(stdout);
    fresh_world();
    g_failHiZ = true;
    ok = initVulkan();
    g_failHiZ = false;
    CHECK(!ok, "initVulkan must not report success after Hi-Z resource failure (vulkanMaster.c:511 falls through)");
    CHECK(!ran(ST_UPD_HIZ), "no descriptor write consumes the never-created Hi-Z pyramid (vulkanMaster.c:664)");
    CHECK(!ran(ST_SYNC), "init must not run to completion past the failed Hi-Z arm");

    fresh_world(); // discharge the last run's real renderHeap

    if (failures) {
        printf("anotest_initdepthguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_initdepthguard: all passed\n");
    return 0;
}
