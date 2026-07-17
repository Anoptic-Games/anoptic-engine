/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the transfer-queue arm of logical-device bring-up. createLogicalDevice guards its
// transfer-queue fetch with indices->computePresent (device.c:663) 〜 a copy-paste of the compute
// block directly above it 〜 instead of transferPresent, and the queue-create loop at :537 feeds
// uniqueQueueFamilies from transferFamily with no presence check at all (docs/BUGS.md,
// Render / Vulkan backend / Implementation). findQueueFamilies (same file, :69) reports
// transferPresent=false / transferFamily=UINT32_MAX for a family table that omits TRANSFER_BIT
// 〜 spec-legal: the transfer capability implied by graphics or compute is optional to report 〜
// and createLogicalDevice then both creates a queue on family UINT32_MAX inside vkCreateDevice
// and calls vkGetDeviceQueue(UINT32_MAX), a VUID breach twice over on real hardware.
// Harness: compiles the REAL device.c TU into this executable and satisfies its link seams
// (the vk* entry points, getChosenMsaaSamples) with stubs that serve a synthetic queue-family
// table and capture every family index requested at vkCreateDevice and vkGetDeviceQueue 〜 no
// GPU device, no loader. Controls prove a transfer-capable table rides the same seams with the
// transfer queue really fetched, so a fix that deletes the fetch cannot pass. Differential and
// fix-agnostic: guarding the fetch with transferPresent, skipping the sentinel at queue-create,
// or folding the implied transfer capability inside findQueueFamilies all make every CHECK pass.
// Exit 0 == pass.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Synthetic queue-family table served to findQueueFamilies */

static VkQueueFamilyProperties g_fam[4];
static uint32_t                g_famCount;
static VkBool32                g_presentable[4]; // per-family surface support


/* Captures */

static uint32_t g_createFam[16];   // queueFamilyIndex of every VkDeviceQueueCreateInfo
static uint32_t g_createFamCount;
static struct { uint32_t family; VkQueue* out; } g_fetch[16]; // every vkGetDeviceQueue request
static uint32_t g_fetchCount;

// Clears the per-scenario captures.
static void reset_captures(void)
{
    memset(g_createFam, 0, sizeof g_createFam);
    g_createFamCount = 0;
    memset(g_fetch, 0, sizeof g_fetch);
    g_fetchCount = 0;
}

// True when every captured family index names a real family (< g_famCount).
static bool families_in_range(const uint32_t* fams, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (fams[i] >= g_famCount) return false;
    return true;
}


/* Link seams 〜 config extern (vulkanConfig.c deliberately not linked) */

uint32_t getChosenMsaaSamples() { return 4; }


/* Link seams 〜 the vk* entry points device.c references (loader not linked) */

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
{
    (void)physicalDevice;
    if (pQueueFamilyProperties)
        memcpy(pQueueFamilyProperties, g_fam, g_famCount * sizeof g_fam[0]);
    *pQueueFamilyPropertyCount = g_famCount;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported)
{
    (void)physicalDevice; (void)surface;
    *pSupported = (queueFamilyIndex < g_famCount) ? g_presentable[queueFamilyIndex] : VK_FALSE;
    return VK_SUCCESS;
}

// All-zero feature/property answers: createLogicalDevice only mirrors what is reported.
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
{ (void)physicalDevice; (void)pFeatures; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
{ (void)physicalDevice; memset(&pProperties->properties, 0, sizeof pProperties->properties); }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{ (void)physicalDevice; memset(pProperties, 0, sizeof *pProperties); }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{ (void)physicalDevice; memset(pMemoryProperties, 0, sizeof *pMemoryProperties); }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{ (void)physicalDevice; (void)pLayerName; (void)pProperties; *pPropertyCount = 0; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices)
{ (void)instance; (void)pPhysicalDevices; *pPhysicalDeviceCount = 0; return VK_SUCCESS; }

// Records every requested queue-create family, succeeds regardless.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    (void)physicalDevice; (void)pAllocator;
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount && g_createFamCount < 16; i++)
        g_createFam[g_createFamCount++] = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
    *pDevice = (VkDevice)(uintptr_t)0x40;
    return VK_SUCCESS;
}

// Records every (family, out) fetch, hands back a live-looking handle.
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
{
    (void)device; (void)queueIndex;
    if (g_fetchCount < 16) { g_fetch[g_fetchCount].family = queueFamilyIndex; g_fetch[g_fetchCount].out = pQueue; g_fetchCount++; }
    *pQueue = (VkQueue)(uintptr_t)(0x50 + queueFamilyIndex);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    VkPhysicalDevice phys = (VkPhysicalDevice)(uintptr_t)0x10;
    VkSurfaceKHR surf = (VkSurfaceKHR)(uintptr_t)0x20;
    VkDevice dev;
    VkQueue gq, cq, tq, pq;

    // control: a transfer-capable table 〜 dedicated compute on family 1, transfer on family 0 〜
    // resolves all four families and really fetches the transfer queue from the transfer family,
    // so a fix that deletes the fetch or refuses every device cannot pass
    g_famCount = 2;
    g_fam[0] = (VkQueueFamilyProperties){ .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, .queueCount = 4 };
    g_fam[1] = (VkQueueFamilyProperties){ .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, .queueCount = 2 };
    g_presentable[0] = VK_TRUE; g_presentable[1] = VK_FALSE;

    QueueFamilyIndices qc = findQueueFamilies(phys, &surf);
    CHECK(qc.graphicsPresent && qc.graphicsFamily == 0, "control: graphics resolves to family 0");
    CHECK(qc.computePresent && qc.computeFamily == 1, "control: dedicated compute family preferred");
    CHECK(qc.transferPresent && qc.transferFamily == 0, "control: transfer resolves to family 0");
    CHECK(qc.presentPresent && qc.presentFamily == 0, "control: present resolves to family 0");

    reset_captures();
    gq = cq = tq = pq = NULL;
    CHECK(createLogicalDevice(phys, &dev, &gq, &cq, &tq, &pq, &qc) == VK_SUCCESS, "control: device creation succeeds");
    CHECK(families_in_range(g_createFam, g_createFamCount), "control: every created queue names a real family");
    bool inRange = true;
    for (uint32_t i = 0; i < g_fetchCount; i++) if (g_fetch[i].family >= g_famCount) inRange = false;
    CHECK(inRange, "control: every fetched queue names a real family");
    bool transferFetched = false;
    for (uint32_t i = 0; i < g_fetchCount; i++)
        if (g_fetch[i].out == &tq && g_fetch[i].family == qc.transferFamily) transferFetched = true;
    CHECK(transferFetched, "control: transfer queue fetched from the transfer family");
    CHECK(tq != NULL, "control: transfer queue handle written");

    // trigger: a spec-legal table that omits TRANSFER_BIT 〜 one GRAPHICS|COMPUTE family 〜 rides
    // findQueueFamilies' UINT32_MAX sentinel into createLogicalDevice, whose queue-create loop
    // consumes transferFamily unguarded (:537) and whose transfer fetch is armed by the WRONG
    // sibling flag computePresent (:663)
    printf("trigger: TRANSFER_BIT-less family table through createLogicalDevice\n");
    g_famCount = 1;
    g_fam[0] = (VkQueueFamilyProperties){ .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, .queueCount = 4 };
    g_presentable[0] = VK_TRUE;

    QueueFamilyIndices qt = findQueueFamilies(phys, &surf);
    CHECK(qt.graphicsPresent && qt.computePresent && qt.presentPresent, "trigger: graphics/compute/present resolve on family 0");
    CHECK(!qt.transferPresent || qt.transferFamily < g_famCount, "invariant: a present transfer family names a real family");

    reset_captures();
    gq = cq = tq = pq = NULL;
    CHECK(createLogicalDevice(phys, &dev, &gq, &cq, &tq, &pq, &qt) == VK_SUCCESS, "trigger: device creation call completes");
    for (uint32_t i = 0; i < g_createFamCount; i++)
        if (g_createFam[i] >= g_famCount)
            printf("  (vkCreateDevice asked for a queue on family %" PRIu32 " of a %" PRIu32 "-family device)\n", g_createFam[i], g_famCount);
    CHECK(families_in_range(g_createFam, g_createFamCount), "trigger: no queue created on a nonexistent family (sentinel must not reach vkCreateDevice)");
    inRange = true;
    for (uint32_t i = 0; i < g_fetchCount; i++)
        if (g_fetch[i].family >= g_famCount) {
            printf("  (vkGetDeviceQueue asked for family %" PRIu32 " of a %" PRIu32 "-family device)\n", g_fetch[i].family, g_famCount);
            inRange = false;
        }
    CHECK(inRange, "trigger: no queue fetched from a nonexistent family (fetch must honor transferPresent)");
    bool graphicsFetched = false;
    for (uint32_t i = 0; i < g_fetchCount; i++)
        if (g_fetch[i].out == &gq && g_fetch[i].family == qt.graphicsFamily) graphicsFetched = true;
    CHECK(graphicsFetched, "trigger: graphics queue still fetched from its real family");

    if (failures) {
        printf("anotest_transferqueueguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_transferqueueguard: all passed\n");
    return 0;
}
