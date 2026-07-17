/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the copy-failure arm of the one-shot staged upload. stagingTransfer guards
// copyBuffer at commands.c:201 (docs/BUGS.md, Render / Vulkan backend / Implementation), but
// copyBuffer returns true unconditionally (:267) because the single-time-command pair beneath
// it discards every VkResult it sees (vkAllocateCommandBuffers :222, vkBeginCommandBuffer :228,
// vkEndCommandBuffer :235, vkCreateFence :240, vkQueueSubmit :247, vkWaitForFences :248) 〜 the
// arm is dead code: a copy that never executed reports success to the text-bake uploads
// (text_raster.c:639/:644) whose ok-chain exists to hear exactly this, and a failed
// command-buffer allocation's undefined out-param handle rides vkBeginCommandBuffer /
// vkCmdCopyBuffer / vkQueueSubmit as invalid-handle VUID breaches. The arm's own body is wrong
// for the day a fix arms it 〜 returning at :204 skips the :208 vkDestroyBuffer and leaks the
// transient staging buffer.
// Harness: compiles the REAL commands.c TU into this executable and satisfies its link seams
// (the vk* entry points, gpu_alloc, findQueueFamilies, vertex math, the allocator/renderer
// globals) with stubs that capture every buffer create/destroy, the recorded copy region, and
// each submit 〜 no GPU device, no loader. Controls prove a clean upload stages the payload,
// records the right copy, submits once, and discharges the staging buffer, so a fix that
// refuses every transfer cannot pass. Differential and fix-agnostic: propagating the failing
// VkResult out of copyBuffer (or endSingleTimeCommands) while still discharging the staging
// buffer on the failure path makes every CHECK pass. Exit 0 == pass.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h"
#include "vulkan_backend/gpu_alloc.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// A failed vkAllocateCommandBuffers leaves *pCommandBuffers undefined; the stub's error arm writes this.
#define POISON_CB ((VkCommandBuffer)(uintptr_t)0xDEADD00Dull)


/* Captures + failure schedule */

static VkBuffer     g_bufCreated[8];   // every handle vkCreateBuffer returned this scenario
static uint32_t     g_bufCreateCount;
static uint32_t     g_bufCreateTotal;  // monotonic handle id, never reset
static VkDeviceSize g_lastCreateSize;  // size of the last vkCreateBuffer
static VkBuffer     g_destroyed[8];    // every handle handed to vkDestroyBuffer
static uint32_t     g_destroyCount;

static uint32_t     g_copyCount;       // vkCmdCopyBuffer recordings
static VkBuffer     g_lastCopySrc;
static VkBuffer     g_lastCopyDst;
static VkDeviceSize g_lastCopySize;

static uint32_t     g_submitCount;
static VkResult     g_submitResult;    // what vkQueueSubmit returns
static bool         g_allocCbFail;     // vkAllocateCommandBuffers fails with a poison out-param
static bool         g_poisonConsumed;  // a recording/submit entry point received POISON_CB

static GpuAllocator* g_allocFrom;      // allocator handed to gpu_alloc
static uint8_t       g_stage[4096];    // host memory backing the staged allocation

// Clears every capture and the failure schedule.
static void reset_captures(void)
{
    memset(g_bufCreated, 0, sizeof g_bufCreated);
    g_bufCreateCount = 0;
    g_lastCreateSize = 0;
    memset(g_destroyed, 0, sizeof g_destroyed);
    g_destroyCount = 0;
    g_copyCount = 0;
    g_lastCopySrc = VK_NULL_HANDLE;
    g_lastCopyDst = VK_NULL_HANDLE;
    g_lastCopySize = 0;
    g_submitCount = 0;
    g_submitResult = VK_SUCCESS;
    g_allocCbFail = false;
    g_poisonConsumed = false;
    g_allocFrom = NULL;
    memset(g_stage, 0, sizeof g_stage);
}

// True when h appears among the recorded buffer destroys.
static bool was_destroyed(VkBuffer h)
{
    for (uint32_t i = 0; i < g_destroyCount; i++)
        if (g_destroyed[i] == h) return true;
    return false;
}


/* Link seams 〜 globals + engine helpers commands.c externs (vulkanMaster.c / gpu_alloc.c /
   device.c / vertex.c deliberately not linked) */

RendererState rendererState;
GpuAllocator gpuAllocator;
GpuAllocator stagingAllocator;

struct QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR *surface)
{ (void)device; (void)surface; struct QueueFamilyIndices q; memset(&q, 0, sizeof q); return q; }

void translate(float mat[4][4], float x, float y, float z) { (void)mat; (void)x; (void)y; (void)z; }
void rotateMatrix(float mat[4][4], char axis, float angle) { (void)mat; (void)axis; (void)angle; }

// Records which allocator fed the transfer and hands back a host-backed mapped region.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
    (void)props;
    g_allocFrom = alloc;
    GpuAllocation a = { .memory = (VkDeviceMemory)(uintptr_t)0x70, .offset = 0, .size = reqs.size,
                        .mapped = (reqs.size <= sizeof g_stage) ? g_stage : NULL };
    return a;
}


/* Link seams 〜 the vk* entry points commands.c references (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    (void)device; (void)pAllocator;
    g_lastCreateSize = pCreateInfo->size;
    VkBuffer h = (VkBuffer)(uintptr_t)(0x2000 + g_bufCreateTotal++);
    if (g_bufCreateCount < 8) g_bufCreated[g_bufCreateCount] = h;
    g_bufCreateCount++;
    *pBuffer = h;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    (void)device; (void)buffer;
    pMemoryRequirements->size = g_lastCreateSize;
    pMemoryRequirements->alignment = 16;
    pMemoryRequirements->memoryTypeBits = 1;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (g_destroyCount < 8) g_destroyed[g_destroyCount] = buffer;
    g_destroyCount++;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)buffer; (void)memory; (void)memoryOffset; return VK_SUCCESS; }

// Success: a distinct live-looking handle. Failure (per g_allocCbFail): an error and a poison
// out-param 〜 spec-legal, output params are undefined when a command fails.
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
{
    (void)device; (void)pAllocateInfo;
    if (g_allocCbFail) { *pCommandBuffers = POISON_CB; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *pCommandBuffers = (VkCommandBuffer)(uintptr_t)0x3000;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{ (void)pBeginInfo; if (commandBuffer == POISON_CB) g_poisonConsumed = true; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{ if (commandBuffer == POISON_CB) g_poisonConsumed = true; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
{
    if (commandBuffer == POISON_CB) g_poisonConsumed = true;
    g_copyCount++;
    g_lastCopySrc = srcBuffer;
    g_lastCopyDst = dstBuffer;
    g_lastCopySize = (regionCount > 0) ? pRegions[0].size : 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pFence = (VkFence)(uintptr_t)0x60; return VK_SUCCESS; }

// Returns the scheduled result and flags a poisoned command buffer riding the submit.
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    (void)queue; (void)fence;
    g_submitCount++;
    for (uint32_t s = 0; s < submitCount; s++)
        for (uint32_t c = 0; c < pSubmits[s].commandBufferCount; c++)
            if (pSubmits[s].pCommandBuffers[c] == POISON_CB) g_poisonConsumed = true;
    return g_submitResult;
}

// A fence whose submit failed never signals; device loss is what a real wait reports.
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{
    (void)device; (void)fenceCount; (void)pFences; (void)waitAll; (void)timeout;
    return (g_submitResult == VK_SUCCESS) ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)fence; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{ (void)device; (void)commandPool; (void)commandBufferCount; (void)pCommandBuffers; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pCommandPool = (VkCommandPool)(uintptr_t)0x80; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{ (void)physicalDevice; memset(pMemoryProperties, 0, sizeof *pMemoryProperties); }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{ (void)physicalDevice; memset(pProperties, 0, sizeof *pProperties); }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
{ (void)physicalDevice; (void)pQueueFamilyProperties; *pQueueFamilyPropertyCount = 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pSemaphore = (VkSemaphore)(uintptr_t)0x90; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pQueryPool = (VkQueryPool)(uintptr_t)0xA0; return VK_SUCCESS; }


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static VulkanContext ctx; // zeroed; the seams consult device/graphicsQueue only
    ctx.device = (VkDevice)(uintptr_t)0x40;
    ctx.graphicsQueue = (VkQueue)(uintptr_t)0x50;
    rendererState.commandPool = (VkCommandPool)(uintptr_t)0x80;

    uint8_t payload[64];
    for (uint32_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(0xA5 ^ i);
    VkBuffer dst = (VkBuffer)(uintptr_t)0x999;

    // control: a clean upload stages the payload, records the right copy, submits once, and
    // discharges the transient staging buffer, so a fix that refuses every transfer cannot pass
    reset_captures();
    CHECK(stagingTransfer(&ctx, payload, dst, sizeof payload), "control: clean staged upload reports success");
    CHECK(g_allocFrom == &stagingAllocator, "control: staging memory drawn from the staging allocator");
    CHECK(memcmp(g_stage, payload, sizeof payload) == 0, "control: payload staged into the mapped allocation");
    CHECK(g_copyCount == 1 && g_lastCopySrc == g_bufCreated[0] && g_lastCopyDst == dst, "control: one copy recorded, staging -> destination");
    CHECK(g_lastCopySize == sizeof payload, "control: copy region carries the full payload size");
    CHECK(g_submitCount == 1, "control: exactly one one-shot submit");
    CHECK(was_destroyed(g_bufCreated[0]), "control: transient staging buffer discharged after a clean upload");

    // trigger: the one-shot submit fails 〜 the copy never executed, and copyBuffer has no way
    // to say so (:267 unconditional true), so stagingTransfer's :201 arm cannot fire
    printf("trigger: vkQueueSubmit VK_ERROR_DEVICE_LOST through stagingTransfer\n");
    reset_captures();
    g_submitResult = VK_ERROR_DEVICE_LOST;
    bool ok = stagingTransfer(&ctx, payload, dst, sizeof payload);
    if (ok)
        printf("  (submit returned VK_ERROR_DEVICE_LOST and the copy never executed, yet stagingTransfer reported success)\n");
    CHECK(!ok, "trigger: a failed one-shot copy submit must surface as stagingTransfer failure");
    CHECK(g_bufCreateCount == 0 || was_destroyed(g_bufCreated[0]), "invariant: the transient staging buffer reaches vkDestroyBuffer on the failure path too");

    // trigger: the command-buffer allocation fails 〜 begin ignores the error (:222) and the
    // undefined out-param handle rides the recording entry points and the submit
    printf("trigger: vkAllocateCommandBuffers failure through stagingTransfer\n");
    reset_captures();
    g_allocCbFail = true;
    ok = stagingTransfer(&ctx, payload, dst, sizeof payload);
    if (g_poisonConsumed)
        printf("  (the failed allocation's undefined command-buffer handle was recorded/submitted 〜 invalid-handle VUID)\n");
    CHECK(!ok, "trigger: a failed command-buffer allocation must surface as stagingTransfer failure");
    CHECK(!g_poisonConsumed, "trigger: no vk entry point may consume the undefined command-buffer handle");
    CHECK(g_bufCreateCount == 0 || was_destroyed(g_bufCreated[0]), "invariant: the staging buffer is discharged when the allocation fails");

    if (failures) {
        printf("anotest_stagingcopyguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_stagingcopyguard: all passed\n");
    return 0;
}
