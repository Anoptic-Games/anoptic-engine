/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: findMemoryType's type-filter probe typeFilter & (1 << i) walks i up to
// memoryTypeCount - 1 (gpu_alloc.c:12, duplicated verbatim at commands.c:173), and the legal
// domain is VK_MAX_MEMORY_TYPES == 32 with the count adopted raw from
// vkGetPhysicalDeviceMemoryProperties 〜 so a device exposing all 32 memory types evaluates
// 1 << 31, a signed int shifted into its sign bit, UB under the C23 the build mandates
// (CMakeLists.txt:26; 6.5.7 kept the C17 "representable in the result type" wording, and this
// tree's own clang in -std=c23 traps it: "left shift of 1 by 31 places cannot be represented
// in type 'int'"). Device-gated: common codegen wraps to INT_MIN and the & happens to select
// bit 31, which is exactly why it stayed latent
// (docs/BUGS.md, Render / Vulkan backend / Implementation, gpu_alloc.c:12).
// Harness: compiles the REAL gpu_alloc.c TU with -fsanitize=shift -fno-sanitize-recover=shift
// so the abstract-machine UB is a deterministic trap, not codegen luck; link-seam vk stubs mint
// fake device memory and ledger every requested memoryTypeIndex 〜 no GPU device, no loader.
// CONTROL A: 4 types, match at index 2 〜 the finder must mint at type 2, so a
// reject-everything fix cannot pass.
// CONTROL B: filter matches a type whose flags miss the request 〜 clean empty return, no mint.
// CONTROL C: 31 types, match at index 30 〜 the highest legal signed probe, must stay clean.
// TRIGGER: 32 types (VK_MAX_MEMORY_TYPES), the only match at index 31, filter bit 31 only 〜
// today the probe of the last type is the UB shift and the sanitizer halts; a fixed unsigned
// mask must find type 31 and mint there.
// A crash (the shift trap) IS the failure signal. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include "vulkan_backend/gpu_alloc.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static_assert(VK_MAX_MEMORY_TYPES == 32, "the trigger drives the full legal type domain");


/* Ledgers 〜 every device-memory mint gpu_alloc requests */

static uint32_t g_allocCalls;
static uint32_t g_lastTypeIndex;


/* Link seams 〜 the vk* entry points gpu_alloc.c calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    (void)device; (void)pAllocator;
    g_allocCalls++;
    g_lastTypeIndex = pAllocateInfo->memoryTypeIndex;
    *pMemory = (VkDeviceMemory)(uintptr_t)(0xD00Du + g_allocCalls);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
{
    (void)device; (void)memory; (void)offset; (void)size; (void)flags;
    static char g_fakeMap[256];
    *ppData = g_fakeMap;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{ (void)device; (void)memory; }

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)memory; (void)pAllocator; }


/* Phase runner 〜 one gpu_alloc against a hand-built memory-type table, ledgers reset */

// in: count = memoryTypeCount the fake device reports; flagIdx = the one type given flags;
//     flags = that type's propertyFlags; typeFilter = reqs.memoryTypeBits; want = requested props.
// out: the GpuAllocation gpu_alloc returned; allocator torn down before return.
static GpuAllocation run_alloc(uint32_t count, uint32_t flagIdx, VkMemoryPropertyFlags flags, uint32_t typeFilter, VkMemoryPropertyFlags want)
{
    GpuAllocator a = {0};
    a.device = (VkDevice)(uintptr_t)0xDE7Cu;
    a.memProps.memoryTypeCount = count;
    a.memProps.memoryTypes[flagIdx].propertyFlags = flags;
    g_allocCalls = 0;
    g_lastTypeIndex = UINT32_MAX;

    VkMemoryRequirements reqs = { .size = 64u, .alignment = 16u, .memoryTypeBits = typeFilter };
    GpuAllocation got = gpu_alloc(&a, reqs, want);
    gpu_alloc_destroy(&a);
    return got;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const VkMemoryPropertyFlags DEV = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // control A: 4 types, filter bit 2, flags match at index 2 〜 the finder must select type 2
    printf("control A: 4 types, match at index 2\n");
    GpuAllocation ca = run_alloc(4u, 2u, DEV, 1u << 2, DEV);
    CHECK(g_allocCalls == 1u, "control A: exactly one device-memory mint");
    CHECK(g_lastTypeIndex == 2u, "control A: minted at memory type 2");
    CHECK(ca.memory != VK_NULL_HANDLE, "control A: live allocation returned");
    CHECK(ca.size == 64u && ca.offset == 0u, "control A: allocation covers the request");

    // control B: filter matches a type whose flags miss the request 〜 empty return, no mint
    printf("control B: flags miss, clean empty return\n");
    GpuAllocation cb = run_alloc(4u, 1u, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1u << 1, DEV);
    CHECK(g_allocCalls == 0u, "control B: no device-memory mint on a failed find");
    CHECK(cb.memory == VK_NULL_HANDLE, "control B: empty allocation on a failed find");

    // control C: 31 types, match at index 30 〜 the highest legal signed probe (1 << 30)
    printf("control C: 31 types, match at index 30 (legality boundary)\n");
    GpuAllocation cc = run_alloc(31u, 30u, DEV, 1u << 30, DEV);
    CHECK(g_allocCalls == 1u, "control C: exactly one device-memory mint");
    CHECK(g_lastTypeIndex == 30u, "control C: minted at memory type 30");
    CHECK(cc.memory != VK_NULL_HANDLE, "control C: live allocation returned");

    // trigger: 32 types 〜 the full legal domain 〜 with the ONLY match at index 31, so the loop
    // must probe bit 31: today gpu_alloc.c:12 evaluates 1 << 31 and the shift sanitizer halts
    // here; a fixed unsigned mask finds type 31 and mints there
    printf("trigger: 32 types, match only at index 31 (gpu_alloc.c:12 evaluates 1 << 31)\n");
    GpuAllocation tr = run_alloc(32u, 31u, DEV, 0x80000000u, DEV);
    CHECK(g_allocCalls == 1u, "trigger: exactly one device-memory mint");
    CHECK(g_lastTypeIndex == 31u, "trigger: minted at memory type 31");
    CHECK(tr.memory != VK_NULL_HANDLE, "trigger: live allocation returned");

    if (failures) {
        printf("anotest_memtypeshiftguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_memtypeshiftguard: all passed\n");
    return 0;
}
