/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the failure arm of the central image-view helper. createImageView only logs when
// vkCreateImageView fails and falls through to `return imageView` (swapchain.c:428, docs/BUGS.md,
// Render / Vulkan backend / Implementation) 〜 a local the failed call's contract leaves with
// undefined contents (Vulkan output params are undefined on error), so the caller receives
// indeterminate stack bytes as a live VkImageView and has no failure channel to consult. All ten
// call sites store it directly (swapchain.c:439, attachments.c:86/:108/:153/:199/:210/:226/:238,
// text_raster.c:806, texture.c:457); createImageViews returns true unconditionally (:441), so
// recreateSwapChain's views==NULL guard (:365) tests only the malloc, and cleanupSwapChain (:187)
// hands the garbage straight to vkDestroyImageView 〜 an invalid-handle VUID breach on teardown.
// Harness: compiles the REAL swapchain.c TU into this executable and satisfies its link seams
// (the vk*/glfw entry points, vulkanConfig/instance/text_raster helpers, rendererState) with
// stubs; the failing vkCreateImageView writes a spec-legal poison out-param and the destroy stub
// records every handle it is given 〜 no GPU device, no loader. Controls prove successful
// creation returns the driver's handle with the arguments carried through and cleanup destroys
// exactly the created views, so a fix that returns NULL always or refuses every view cannot
// pass. Differential and fix-agnostic: returning VK_NULL_HANDLE on failure (with or without a
// false arm in createImageViews) makes every CHECK pass. Exit 0 == pass.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// A failed vkCreateImageView leaves *pView undefined; this is the value the stub's error arm writes.
#define POISON ((VkImageView)(uintptr_t)0xDEADD00Dull)


/* Captures */

static uint32_t              g_createCalls;   // vkCreateImageView calls since reset
static uint32_t              g_failMask;      // bit i set -> the i-th call since reset fails
static VkImageViewCreateInfo g_lastInfo;      // last create info seen
static VkImageView           g_destroyed[16]; // every handle handed to vkDestroyImageView
static uint32_t              g_destroyCount;

// Clears every capture and the failure schedule.
static void reset_captures(void)
{
    g_createCalls = 0;
    g_failMask = 0;
    memset(&g_lastInfo, 0, sizeof g_lastInfo);
    memset(g_destroyed, 0, sizeof g_destroyed);
    g_destroyCount = 0;
}

// True when h appears among the recorded destroys.
static bool was_destroyed(VkImageView h)
{
    for (uint32_t i = 0; i < g_destroyCount; i++)
        if (g_destroyed[i] == h) return true;
    return false;
}


/* Link seams 〜 globals + engine helpers swapchain.c externs (vulkanMaster.c / vulkanConfig.c /
   attachments.c / text_raster.c / gpu_alloc.c deliberately not linked) */

RendererState rendererState;
GpuAllocator swapchainAllocator;

bool getChosenBorderless() { return false; }
VkPresentModeKHR getChosenPresentMode() { return VK_PRESENT_MODE_FIFO_KHR; }
void cleanupVulkan(VulkanContext* ctx) { (void)ctx; }
void createColorResources(VulkanContext* ctx) { (void)ctx; }
bool createDepthResources(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; return true; }
bool createHiZResources(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; return true; }
void updateHiZDescriptorSets(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; }
void updateTonemapDescriptorSets(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; }
void ano_vk_text_destroy_overlay(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; }
void ano_vk_text_update_sets(VulkanContext* ctx, RendererState* state) { (void)ctx; (void)state; }
void gpu_alloc_reset(GpuAllocator* alloc) { (void)alloc; }


/* Link seams 〜 the glfw entry points swapchain.c references (glfw not linked) */

GLFWmonitor* glfwGetPrimaryMonitor(void) { return NULL; }
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor* monitor) { (void)monitor; static const GLFWvidmode m = { .width = 640, .height = 480 }; return &m; }
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height) { (void)window; *width = 640; *height = 480; }
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) { (void)window; *width = 640; *height = 480; }
void glfwWaitEvents(void) {}


/* Link seams 〜 the vk* entry points swapchain.c calls (loader not linked) */

// Success: hands back a distinct per-call handle. Failure (per g_failMask): returns an error
// and writes POISON 〜 spec-legal, output params are undefined when a command fails.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
    (void)device; (void)pAllocator;
    g_lastInfo = *pCreateInfo;
    uint32_t n = g_createCalls++;
    if (g_failMask & (1u << n)) { *pView = POISON; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *pView = (VkImageView)(uintptr_t)(0x1000 + n);
    return VK_SUCCESS;
}

// Records every handle the engine asks to destroy.
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (g_destroyCount < 16) g_destroyed[g_destroyCount] = imageView;
    g_destroyCount++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)image; (void)pAllocator; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{ (void)physicalDevice; (void)surface; memset(pSurfaceCapabilities, 0, sizeof *pSurfaceCapabilities); return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{ (void)physicalDevice; (void)surface; (void)pSurfaceFormats; *pSurfaceFormatCount = 0; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{ (void)physicalDevice; (void)surface; (void)pPresentModes; *pPresentModeCount = 0; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pSwapchain = (VkSwapchainKHR)(uintptr_t)0x30; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
{ (void)device; (void)swapchain; (void)pSwapchainImages; *pSwapchainImageCount = 0; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)swapchain; (void)pAllocator; }

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{ (void)device; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{ (void)device; (void)commandPool; (void)flags; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
{ (void)device; (void)fenceCount; (void)pFences; return VK_SUCCESS; }


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static VulkanContext ctx; // zeroed; only .device is consulted by the seams
    ctx.device = (VkDevice)(uintptr_t)0x40;
    VkImage imgA = (VkImage)(uintptr_t)0x60;

    // control: successful creation returns the handle the driver wrote and carries the
    // arguments through, so a fix that returns NULL always or refuses every view cannot pass
    reset_captures();
    VkImageView v = createImageView(ctx.device, imgA, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 5);
    CHECK(v == (VkImageView)(uintptr_t)0x1000, "control: success returns the driver-written handle");
    CHECK(g_lastInfo.image == imgA && g_lastInfo.format == VK_FORMAT_B8G8R8A8_SRGB, "control: image/format carried into the create info");
    CHECK(g_lastInfo.subresourceRange.levelCount == 5 && g_lastInfo.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT, "control: mip levels + aspect carried through");

    // control: the swapchain view build stores one live handle per image and cleanup
    // destroys exactly those views
    static RendererState state; // zeroed; every optional teardown field is VK_NULL_HANDLE
    state.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    state.imageCount = 3;
    state.images = (VkImage*)malloc(3 * sizeof(VkImage));
    for (uint32_t i = 0; i < 3; i++) state.images[i] = (VkImage)(uintptr_t)(0x61 + i);
    CHECK(createImageViews(&ctx, &state), "control: view build over 3 images reports success");
    CHECK(state.viewCount == 3 && state.views != NULL, "control: 3 views stored");
    VkImageView built[3] = {0};
    for (uint32_t i = 0; i < 3 && state.views; i++) {
        built[i] = state.views[i];
        CHECK(state.views[i] != VK_NULL_HANDLE && state.views[i] != POISON, "control: stored view is a live handle");
    }
    cleanupSwapChain(&ctx, &state);
    CHECK(g_destroyCount == 3, "control: cleanup destroys exactly the created views");
    for (uint32_t i = 0; i < 3; i++)
        CHECK(was_destroyed(built[i]), "control: each created view reaches vkDestroyImageView");

    // trigger: a failing vkCreateImageView leaves the out-param undefined and the helper's
    // error arm only logs 〜 the indeterminate local comes back as the handle
    printf("trigger: vkCreateImageView failure arm through createImageView\n");
    reset_captures();
    g_failMask = 0x1;
    VkImageView t = createImageView(ctx.device, imgA, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    if (t == POISON)
        printf("  (failed creation returned the driver's undefined out-param verbatim)\n");
    CHECK(t == VK_NULL_HANDLE, "trigger: failed image-view creation must return VK_NULL_HANDLE, not an indeterminate handle");

    // trigger: one failure amid the swapchain view build 〜 createImageViews has no failure
    // channel to hear it, and teardown walks the stored garbage into vkDestroyImageView
    printf("trigger: image 1 of 3 fails during createImageViews\n");
    reset_captures();
    g_failMask = 0x2; // second view fails
    state.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    state.imageCount = 3;
    state.images = (VkImage*)malloc(3 * sizeof(VkImage));
    for (uint32_t i = 0; i < 3; i++) state.images[i] = (VkImage)(uintptr_t)(0x61 + i);
    bool ok = createImageViews(&ctx, &state);
    bool poisonStored = false;
    if (ok && state.views)
        for (uint32_t i = 0; i < state.viewCount; i++)
            if (state.views[i] == POISON) poisonStored = true;
    if (ok && poisonStored)
        printf("  (createImageViews reported success holding an indeterminate handle at views[1])\n");
    CHECK(!ok || !poisonStored, "trigger: createImageViews must not report success while holding an indeterminate handle");
    if (ok) {
        cleanupSwapChain(&ctx, &state);
        if (was_destroyed(POISON))
            printf("  (cleanupSwapChain handed the garbage handle to vkDestroyImageView 〜 invalid-handle VUID)\n");
        CHECK(!was_destroyed(POISON), "trigger: no indeterminate handle may reach vkDestroyImageView");
    }

    if (failures) {
        printf("anotest_imageviewguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_imageviewguard: all passed\n");
    return 0;
}
