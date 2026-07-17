/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the support-query heap orphaned by initSwapChain. querySwapChainSupport callocs two
// arrays into a by-value SwapChainSupportDetails (formats swapchain.c:33, presentModes :38);
// initSwapChain consumes them (:110-:113) and returns on both arms (:157 failure, :178 success)
// without freeing either, and the struct is a discarded local no other backend code references
// (docs/BUGS.md, Render / Vulkan backend / Implementation) 〜 so the boot call
// (vulkanMaster.c:449) orphans both blocks and every resize recreation (recreateSwapChain,
// swapchain.c:353) orphans two more, unbounded accrual under a window-drag resize storm.
// Harness: compiles the REAL swapchain.c TU into this executable, satisfies its link seams
// (vk*/glfw entry points, engine helpers, rendererState) with stubs, and interposes the TU's
// allocator tokens (mi_calloc/mi_malloc/mi_free, which the mimalloc-override macros route every
// calloc/malloc/free through) to audit the TU's heap balance 〜 no GPU device, no loader.
// Controls prove the call succeeds and actually consumed the queried data (chosen format,
// preferred present mode, caps-derived image count all carried into the create info), so a fix
// that skips the query or frees before use cannot pass. The trigger asks only that every block
// the TU allocated during the call is freed or owned by state at return 〜 fix-agnostic: freeing
// in initSwapChain, freeing in a reshaped querySwapChainSupport, or dropping the heap entirely
// for fixed arrays all pass. Exit 0 == pass.

#include <anoptic_memory.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Allocator seam 〜 the -Dmi_*=anotest_seam_* tokens rename every declaration the headers made,
   so the real allocator entry points are redeclared here and the seams forward to them. */

#undef mi_malloc
#undef mi_calloc
#undef mi_free
void *mi_malloc(size_t size);
void *mi_calloc(size_t count, size_t size);
void  mi_free(void *p);

// Live-set tracker over the swapchain TU's allocations. Bounded; overflow counts as failure.
#define TRACK_MAX 64
static void    *g_live[TRACK_MAX];
static size_t   g_liveBytes[TRACK_MAX];
static uint32_t g_liveCount;
static bool     g_trackOverflow;

static void track_add(void *p, size_t bytes)
{
    if (g_liveCount >= TRACK_MAX) { g_trackOverflow = true; return; }
    g_live[g_liveCount] = p;
    g_liveBytes[g_liveCount] = bytes;
    g_liveCount++;
}

static void track_remove(void *p)
{
    for (uint32_t i = 0; i < g_liveCount; i++) {
        if (g_live[i] != p)
            continue;
        g_liveCount--;
        g_live[i] = g_live[g_liveCount];
        g_liveBytes[i] = g_liveBytes[g_liveCount];
        return;
    }
}

// in: n bytes  out: fresh block, tracked
void *anotest_seam_malloc(size_t n)
{
    void *p = mi_malloc(n);
    if (p) track_add(p, n);
    return p;
}

// in: count*size zeroed  out: fresh block, tracked
void *anotest_seam_calloc(size_t count, size_t size)
{
    void *p = mi_calloc(count, size);
    if (p) track_add(p, count * size);
    return p;
}

// in: block or NULL  out: forwarded to the real free, untracked
void anotest_seam_free(void *p)
{
    if (p) track_remove(p);
    mi_free(p);
}

// Live blocks not owned by state (state->images / state->views are stored, freed by cleanup).
// Prints each orphan.
static uint32_t leaked_count(const RendererState *state)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_liveCount; i++) {
        if (g_live[i] == (void *)state->images || g_live[i] == (void *)state->views)
            continue;
        printf("  orphaned block %p (%" PRIuMAX " B)\n", g_live[i], (uintmax_t)g_liveBytes[i]);
        n++;
    }
    return n;
}


/* Captures */

static VkSwapchainCreateInfoKHR g_swapInfo;   // last swapchain create info seen
static uint32_t                 g_swapCreates;
static const void              *g_lastFormats; // array pointers the driver fills landed in
static const void              *g_lastModes;


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

// Fixed caps: min 2, max 3, currentExtent 640x480 (chooseSwapExtent takes the direct arm).
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    (void)physicalDevice; (void)surface;
    memset(pSurfaceCapabilities, 0, sizeof *pSurfaceCapabilities);
    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = 3;
    pSurfaceCapabilities->currentExtent = (VkExtent2D){ 640, 480 };
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    return VK_SUCCESS;
}

// Three formats; the engine's B8G8R8A8_SRGB pick sits at index 1 so the array must be read.
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    (void)physicalDevice; (void)surface;
    *pSurfaceFormatCount = 3;
    if (pSurfaceFormats) {
        pSurfaceFormats[0] = (VkSurfaceFormatKHR){ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pSurfaceFormats[1] = (VkSurfaceFormatKHR){ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        pSurfaceFormats[2] = (VkSurfaceFormatKHR){ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        g_lastFormats = pSurfaceFormats;
    }
    return VK_SUCCESS;
}

// Two modes; MAILBOX at index 1 so honoring the preference proves the array was read.
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
    (void)physicalDevice; (void)surface;
    *pPresentModeCount = 2;
    if (pPresentModes) {
        pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
        pPresentModes[1] = VK_PRESENT_MODE_MAILBOX_KHR;
        g_lastModes = pPresentModes;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    (void)device; (void)pAllocator;
    g_swapInfo = *pCreateInfo;
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)(0x30 + g_swapCreates++);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
{
    (void)device; (void)swapchain;
    if (!pSwapchainImages) { *pSwapchainImageCount = 3; return VK_SUCCESS; }
    for (uint32_t i = 0; i < *pSwapchainImageCount && i < 3; i++)
        pSwapchainImages[i] = (VkImage)(uintptr_t)(0x61 + i);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)swapchain; (void)pAllocator; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pView = (VkImageView)(uintptr_t)0x1000; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)imageView; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)image; (void)pAllocator; }

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{ (void)device; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{ (void)device; (void)commandPool; (void)flags; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
{ (void)device; (void)fenceCount; (void)pFences; return VK_SUCCESS; }


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static VulkanContext ctx;
    ctx.device = (VkDevice)(uintptr_t)0x40;
    ctx.physicalDevice = (VkPhysicalDevice)(uintptr_t)0x50;
    ctx.surface = (VkSurfaceKHR)(uintptr_t)0x70;
    static RendererState state;

    // control: the boot-shaped call (vulkanMaster.c:449) succeeds and demonstrably consumed the
    // queried support data, so a fix that skips the query or frees before use cannot pass
    bool ok = initSwapChain(&ctx, NULL, VK_PRESENT_MODE_MAILBOX_KHR, VK_NULL_HANDLE, &state);
    CHECK(ok, "control: initSwapChain succeeds against the stubbed driver");
    CHECK(state.swapChain != VK_NULL_HANDLE, "control: swapchain handle stored");
    CHECK(g_swapInfo.imageFormat == VK_FORMAT_B8G8R8A8_SRGB
              && g_swapInfo.imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
          "control: surface format chosen out of the queried formats array");
    CHECK(g_swapInfo.presentMode == VK_PRESENT_MODE_MAILBOX_KHR,
          "control: preferred present mode honored out of the queried modes array");
    CHECK(g_swapInfo.minImageCount == 3, "control: image count = caps.min + 1 under caps.max");
    CHECK(state.imageExtent.width == 640 && state.imageExtent.height == 480,
          "control: extent taken from caps.currentExtent");
    CHECK(state.imageCount == 3 && state.images != NULL, "control: swapchain images fetched and stored");
    CHECK(g_lastFormats != NULL && g_lastModes != NULL, "control: both support arrays were driver-filled");
    CHECK(!g_trackOverflow, "control: allocation tracker within bounds");

    // trigger: heap balance at return 〜 every block the TU allocated must be freed or owned by
    // state; today formats (3*8 B) and presentModes (2*4 B) are orphaned locals
    printf("trigger: heap balance after the boot initSwapChain\n");
    uint32_t leaked = leaked_count(&state);
    CHECK(leaked == 0, "trigger: initSwapChain must discharge querySwapChainSupport's heap (swapchain.c:110 leaks formats + presentModes)");

    // trigger: one resize recreation cycle (recreateSwapChain, swapchain.c:353) 〜 cleanup then
    // re-init, exactly the resize order; the leak accrues per cycle
    printf("trigger: one resize recreation cycle\n");
    cleanupSwapChain(&ctx, &state);
    VkSwapchainKHR old = state.swapChain;
    state.swapChain = VK_NULL_HANDLE;
    bool ok2 = initSwapChain(&ctx, NULL, VK_PRESENT_MODE_MAILBOX_KHR, old, &state);
    CHECK(ok2, "control: recreation initSwapChain succeeds");
    CHECK(g_swapInfo.oldSwapchain == old, "control: old swapchain carried into the create info");
    uint32_t leaked2 = leaked_count(&state);
    if (leaked2 > leaked)
        printf("  (each recreation orphans %u more block(s) 〜 unbounded accrual across resizes)\n",
               leaked2 - leaked);
    CHECK(leaked2 == 0, "trigger: a resize recreation cycle must not accrue support-array leaks");

    if (failures) {
        printf("anotest_swapleakguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_swapleakguard: all passed\n");
    return 0;
}
