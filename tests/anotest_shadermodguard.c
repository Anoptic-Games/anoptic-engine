/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: compute-pipeline init vs createShaderModule's failure sentinel. ano_vk_init_compute
// (compute.c) discards createShaderModule's documented NULL return at all nine consumption sites
// (:83/:152/:197/:254/:262/:326/:386/:443/:478) and feeds the dead handle straight into
// vkCreateComputePipelines as stage.module (:97/:166/:222/:292/:348/:400/:457/:492) with no
// maintenance5 shader-module-create-info chained anywhere 〜 an invalid-usage pipeline create
// (VUID-VkPipelineShaderStageCreateInfo-module) instead of the clean boot refusal the same
// function's loadFile arms prove intended (`if (!loadFile(...)) return false;` one line above
// every site), while the producer's own module documents the check the consumer skips:
// pipeline.c:89 mints the NULL and pipeline.c:104 (`if (*outModule == NULL) return false;`)
// guards the identical mint in ano_pipeline_task_stage (docs/BUGS.md, Render / Vulkan backend /
// Implementation, compute.c:83).
// Harness: compiles the REAL compute.c TU 〜 no GPU device, no loader. loadFile and
// createShaderModule are contract-faithful link-seam stand-ins (createShaderModule mirrors
// pipeline.c:78-93 verbatim: vkCreateShaderModule failure -> log + return NULL); the vk stubs
// mint handles and keep ledgers 〜 vkCreateShaderModule counts mints and fails on an armed call
// with VK_ERROR_OUT_OF_HOST_MEMORY (the spec-clean failure arm; a corrupt shipped .spv reaches
// the same sentinel on validating drivers), vkCreateComputePipelines records every stage.module
// it is handed and models the permissive driver (returns success even for the NULL module, so
// the worst downstream shows: init reports healthy with a pipeline minted from a failed module).
// CONTROL: nothing armed 〜 init succeeds, 8 modules minted, 9 pipeline creates all with live
// non-NULL modules, 8 destroys; proves the harness plumbing and that a reject-everything fix
// cannot pass.
// TRIGGER A: module mint #1 (update.comp) fails 〜 the NULL sentinel must never reach
// vkCreateComputePipelines as stage.module and init must report false; fails today on both.
// TRIGGER B: module mint #8 (shadowsetup.comp) fails 〜 the last site shares the shape.
// A crash is a valid failure signal. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_log.h>
#include <anoptic_memory.h>

#include "vulkan_backend/instance/pipeline.h" // ano_vk_init_compute, struct Buffer
#include "vulkan_backend/structs.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgers 〜 module mints/destroys and every stage.module handed to a pipeline create */

static uint32_t g_moduleMints;       // successful vkCreateShaderModule returns
static uint32_t g_moduleDestroys;    // non-NULL vkDestroyShaderModule calls
static uint32_t g_failModuleAt;      // 1-based mint attempt to refuse; 0 = never
static uint32_t g_moduleAttempts;    // all vkCreateShaderModule calls, refused or not

static uint32_t g_pipelineCreates;   // vkCreateComputePipelines calls
static uint32_t g_nullModuleCreates; // creates whose stage.module was VK_NULL_HANDLE
static VkShaderModule g_live[64];    // live minted module handles
static uint32_t g_liveCount;

// out: true iff h is a currently-live minted module handle.
static bool module_live(VkShaderModule h)
{
    for (uint32_t i = 0; i < g_liveCount; i++) if (g_live[i] == h) return true;
    return false;
}


/* Link seams 〜 shader acquisition stand-ins (contract-faithful to pipeline.c) */

// Stand-in for pipeline.c:41 〜 hands back a small fake word stream; validity of the words is
// exactly what loadFile never checks, the module mint is the first validator.
bool loadFile(const char* filename, struct Buffer* buffer)
{
    (void)filename;
    buffer->size = 16u;
    buffer->data = ano_aligned_malloc(buffer->size, alignof(uint32_t));
    if (buffer->data == NULL) return false;
    memset(buffer->data, 0x5A, buffer->size);
    return true;
}

// Verbatim mirror of pipeline.c:78-93 〜 the producer contract under test's consumer:
// vkCreateShaderModule failure -> log + return NULL (pipeline.c:89).
VkShaderModule createShaderModule(VkDevice device, struct Buffer* code)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code->size;
    createInfo.pCode = (uint32_t *) code->data;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS)
    {
        ano_log(ANO_ERROR, "Failed to create shader module!");
        return NULL;
    }

    return shaderModule;
}


/* Link seams 〜 the vk* entry points compute.c calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    g_moduleAttempts++;
    if (g_failModuleAt != 0u && g_moduleAttempts == g_failModuleAt)
        return VK_ERROR_OUT_OF_HOST_MEMORY; // out-param untouched: undefined on error per spec
    VkShaderModule h = (VkShaderModule)(uintptr_t)(0x5AD00000u + ++g_moduleMints);
    if (g_liveCount < sizeof g_live / sizeof g_live[0]) g_live[g_liveCount++] = h;
    *pShaderModule = h;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (shaderModule == VK_NULL_HANDLE) return; // legal no-op
    g_moduleDestroys++;
    for (uint32_t i = 0; i < g_liveCount; i++)
        if (g_live[i] == shaderModule) { g_live[i] = g_live[--g_liveCount]; break; }
}

// Records every stage.module handed in; models the permissive driver 〜 succeeds even on the
// NULL-module VUID breach so the worst downstream (init reporting healthy) is visible.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    (void)device; (void)pipelineCache; (void)pAllocator;
    static uint32_t mint = 0;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        g_pipelineCreates++;
        if (pCreateInfos[i].stage.module == VK_NULL_HANDLE) {
            g_nullModuleCreates++;
            printf("  ledger: pipeline create #%u handed stage.module == VK_NULL_HANDLE (VUID-VkPipelineShaderStageCreateInfo-module)\n", g_pipelineCreates);
        } else if (!module_live(pCreateInfos[i].stage.module)) {
            g_nullModuleCreates++;
            printf("  ledger: pipeline create #%u handed a non-ledgered stage.module\n", g_pipelineCreates);
        }
        pPipelines[i] = (VkPipeline)(uintptr_t)(0x91BE0000u + ++mint);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pSetLayout = (VkDescriptorSetLayout)(uintptr_t)(0xDE5C0000u + ++mint); return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineLayout = (VkPipelineLayout)(uintptr_t)(0x1A700000u + ++mint); return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineCache = (VkPipelineCache)(uintptr_t)(0xCAC0000u + ++mint); return VK_SUCCESS; }


/* Phase runner 〜 one ano_vk_init_compute pass over fresh state, ledgers reset */

static VulkanContext g_ctx;
static RendererState g_state;

// in: failAt = 1-based vkCreateShaderModule call to refuse (0 = none); out: init's verdict.
static bool run_phase(uint32_t failAt)
{
    memset(&g_ctx, 0, sizeof g_ctx);
    memset(&g_state, 0, sizeof g_state);
    g_moduleMints = g_moduleDestroys = g_moduleAttempts = 0;
    g_pipelineCreates = g_nullModuleCreates = g_liveCount = 0;
    g_failModuleAt = failAt;

    g_ctx.device = (VkDevice)(uintptr_t)0xD7;
    g_ctx.deviceCapabilities.meshShader = false;
    g_ctx.deviceCapabilities.depthMaxResolve = false; // 8 mints, 9 pipeline creates
    g_ctx.msaaSamples = VK_SAMPLE_COUNT_4_BIT;

    return ano_vk_init_compute(&g_ctx, &g_state);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control: nothing armed 〜 the full init succeeds and every pipeline create consumed a live
    // minted module, so the harness plumbing is proven and a reject-everything fix cannot pass
    printf("control: no failure armed\n");
    bool ok = run_phase(0u);
    CHECK(ok, "control: ano_vk_init_compute succeeds on good input");
    CHECK(g_moduleMints == 8u, "control: 8 shader modules minted (update/scatter/cull/hiz/tpsort/lightcull/lightsetup/shadowsetup)");
    CHECK(g_pipelineCreates == 9u, "control: 9 compute pipeline creates (hiz has two implementations)");
    CHECK(g_nullModuleCreates == 0u, "control: every stage.module was a live minted handle");
    CHECK(g_moduleDestroys == 8u, "control: every minted module destroyed after pipeline creation");
    printf("control: mints=%u creates=%u nullCreates=%u destroys=%u ok=%d\n",
           g_moduleMints, g_pipelineCreates, g_nullModuleCreates, g_moduleDestroys, (int)ok);

    // trigger A: the first mint (update.comp, compute.c:83) fails 〜 createShaderModule returns
    // its documented NULL sentinel (pipeline.c:89) and the consumer must stop, the way
    // pipeline.c:104 stops and the way every loadFile arm one line above already stops
    printf("trigger A: vkCreateShaderModule refuses mint #1 (update.comp)\n");
    ok = run_phase(1u);
    CHECK(g_nullModuleCreates == 0u, "trigger A: a failed shader module must never reach vkCreateComputePipelines as stage.module (compute.c:83 -> :97)");
    CHECK(!ok, "trigger A: ano_vk_init_compute reports false after a shader-module failure (the loadFile arms' own contract)");

    // trigger B: the last mint (shadowsetup.comp, compute.c:478) fails 〜 the ninth consumption
    // site shares the shape, so the fix must cover the family, not one site
    printf("trigger B: vkCreateShaderModule refuses mint #8 (shadowsetup.comp)\n");
    ok = run_phase(8u);
    CHECK(g_nullModuleCreates == 0u, "trigger B: a failed shader module must never reach vkCreateComputePipelines as stage.module (compute.c:478 -> :492)");
    CHECK(!ok, "trigger B: ano_vk_init_compute reports false after a shader-module failure");

    if (failures) {
        printf("anotest_shadermodguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_shadermodguard: all passed\n");
    return 0;
}
