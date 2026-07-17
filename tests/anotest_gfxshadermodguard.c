/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: graphics-pipeline builders vs createShaderModule's failure sentinel. Every
// graphics-side builder discards createShaderModule's documented NULL return and bakes the dead
// handle into VkPipelineShaderStageCreateInfo.module for vkCreateGraphicsPipelines: the family
// head flat_init_with_cull mints three unchecked (flat.c:90-:92, consumed at :106/:112/:269 into
// the creates at :244/:261/:296 for all three boot lanes flat/twosided/masked), and the family
// repeats the shape at shadow_pipe.c:52-:53/:162-:163/:214-:215, tonemap.c:55-:56,
// additive.c:68-:69, transmission.c:75-:76, text_raster.c:344/:379-:380/:489-:490 〜 an
// invalid-usage pipeline create (VUID-VkPipelineShaderStageCreateInfo-module, no maintenance5
// fallback chained) instead of the clean boot refusal every loadFile arm one line above proves
// intended, while the producer's own TU guards the identical mint at pipeline.c:104
// (ano_pipeline_task_stage) 〜 the lone guarded site of the twenty-one graphics-side mints
// (docs/BUGS.md, Render / Vulkan backend / Implementation, flat.c:90).
// Harness: compiles the REAL flat.c TU 〜 no GPU device, no loader. loadFile, createShaderModule
// and ano_pipeline_task_stage are contract-faithful link-seam stand-ins (createShaderModule
// mirrors pipeline.c:78-93 verbatim: vkCreateShaderModule failure -> log + return NULL); the vk
// stubs mint handles and keep ledgers 〜 vkCreateShaderModule counts mints and fails on an armed
// call with VK_ERROR_OUT_OF_HOST_MEMORY (the spec-clean failure arm; a corrupt shipped .spv
// reaches the same sentinel on validating drivers), vkCreateGraphicsPipelines records every
// pStages[].module it is handed and models the permissive driver (returns success even for a
// NULL module, so the worst downstream shows: init reports healthy with boot pipelines minted
// from a failed module).
// CONTROL: nothing armed 〜 flat init succeeds, 3 modules minted, 3 pipeline creates consuming 5
// live stage modules, 3 destroys; proves the harness plumbing and that a reject-everything fix
// cannot pass.
// TRIGGER A: module mint #1 (geometry, flat.c:90) fails 〜 the NULL sentinel must never reach
// vkCreateGraphicsPipelines as a stage module and init must report false; fails today on both.
// TRIGGER B: module mint #2 (depth pre-pass geometry, flat.c:91) fails 〜 the prepass consumption
// lane (:269 -> :296) shares the shape.
// TRIGGER C: module mint #3 (fragment, flat.c:92) fails 〜 the last mint site shares the shape.
// A crash is a valid failure signal. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_log.h>
#include <anoptic_memory.h>

#include "vulkan_backend/instance/pipeline.h" // struct Buffer, TaskStageStorage
#include "vulkan_backend/instance/pipelines/flat.h"
#include "vulkan_backend/structs.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgers 〜 module mints/destroys and every pStages[].module handed to a pipeline create */

static uint32_t g_moduleMints;       // successful vkCreateShaderModule returns
static uint32_t g_moduleDestroys;    // non-NULL vkDestroyShaderModule calls
static uint32_t g_failModuleAt;      // 1-based mint attempt to refuse; 0 = never
static uint32_t g_moduleAttempts;    // all vkCreateShaderModule calls, refused or not

static uint32_t g_pipelineCreates;   // vkCreateGraphicsPipelines createInfos consumed
static uint32_t g_stageConsumes;     // stage modules consumed across all creates
static uint32_t g_nullStageConsumes; // consumed stage modules that were NULL or non-ledgered
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

// Mirror of pipeline.c:96-117 including its :104 guard 〜 the family's one checked mint; never
// invoked here (taskCull stays false) but the link seam stays contract-faithful.
bool ano_pipeline_task_stage(VulkanContext* ctx, VkBool32 shadowPass, VkBool32 coneCull,
                             TaskStageStorage* store, VkShaderModule* outModule,
                             VkPipelineShaderStageCreateInfo* stage)
{
    struct Buffer code;
    if (!loadFile("resources/shaders/flat.task.spv", &code)) return false;
    *outModule = createShaderModule(ctx->device, &code);
    ano_aligned_free(code.data);
    if (*outModule == NULL) return false;

    store->entries[0] = (VkSpecializationMapEntry){ .constantID = 0, .offset = 0, .size = sizeof(VkBool32) };
    store->entries[1] = (VkSpecializationMapEntry){ .constantID = 1, .offset = sizeof(VkBool32), .size = sizeof(VkBool32) };
    store->data[0] = shadowPass;
    store->data[1] = coneCull;
    store->spec = (VkSpecializationInfo){ .mapEntryCount = 2, .pMapEntries = store->entries,
        .dataSize = sizeof(store->data), .pData = store->data };

    *stage = (VkPipelineShaderStageCreateInfo){ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_TASK_BIT_EXT, .module = *outModule, .pName = "main",
        .pSpecializationInfo = &store->spec };
    return true;
}


/* Link seams 〜 the vk* entry points flat.c calls (loader not linked) */

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

// Records every stage module handed in; models the permissive driver 〜 succeeds even on the
// NULL-module VUID breach so the worst downstream (init reporting healthy) is visible.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    (void)device; (void)pipelineCache; (void)pAllocator;
    static uint32_t mint = 0;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        g_pipelineCreates++;
        for (uint32_t s = 0; s < pCreateInfos[i].stageCount; s++) {
            g_stageConsumes++;
            VkShaderModule m = pCreateInfos[i].pStages[s].module;
            if (m == VK_NULL_HANDLE) {
                g_nullStageConsumes++;
                printf("  ledger: pipeline create #%u stage %u handed module == VK_NULL_HANDLE (VUID-VkPipelineShaderStageCreateInfo-module)\n", g_pipelineCreates, s);
            } else if (!module_live(m)) {
                g_nullStageConsumes++;
                printf("  ledger: pipeline create #%u stage %u handed a non-ledgered module\n", g_pipelineCreates, s);
            }
        }
        pPipelines[i] = (VkPipeline)(uintptr_t)(0x91BE0000u + ++mint);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineLayout = (VkPipelineLayout)(uintptr_t)(0x1A700000u + ++mint); return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineCache = (VkPipelineCache)(uintptr_t)(0xCAC0000u + ++mint); return VK_SUCCESS; }

// Cleanup-path no-ops 〜 ano_pipeline_flat_cleanup links them from the same TU, unused here.
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipelineCache; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipelineLayout; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipeline; (void)pAllocator; }


/* Phase runner 〜 one ano_pipeline_flat_init pass over fresh state, ledgers reset */

static VulkanContext g_ctx;
static RendererState g_state;

// in: failAt = 1-based vkCreateShaderModule call to refuse (0 = none); out: init's verdict.
static bool run_phase(uint32_t failAt)
{
    memset(&g_ctx, 0, sizeof g_ctx);
    memset(&g_state, 0, sizeof g_state);
    g_moduleMints = g_moduleDestroys = g_moduleAttempts = 0;
    g_pipelineCreates = g_stageConsumes = g_nullStageConsumes = g_liveCount = 0;
    g_failModuleAt = failAt;

    g_ctx.device = (VkDevice)(uintptr_t)0xD7;
    g_ctx.deviceCapabilities.meshShader = false;   // vertex fallback lane, taskCull off
    g_ctx.msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    g_state.imageExtent = (VkExtent2D){1920u, 1080u};
    g_state.depthFormat = VK_FORMAT_D32_SFLOAT;

    bool ok = ano_pipeline_flat_init(&g_ctx, &g_state, &g_state.prototypes[PIPELINE_FLAT]);
    free(g_state.prototypes[PIPELINE_FLAT].implementations);
    g_state.prototypes[PIPELINE_FLAT].implementations = NULL;
    return ok;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control: nothing armed 〜 the flat boot lane succeeds and every consumed stage module was a
    // live minted handle, so the harness plumbing is proven and a reject-everything fix cannot pass
    printf("control: no failure armed\n");
    bool ok = run_phase(0u);
    CHECK(ok, "control: ano_pipeline_flat_init succeeds on good input");
    CHECK(g_moduleMints == 3u, "control: 3 shader modules minted (geometry/depth-geometry/fragment)");
    CHECK(g_pipelineCreates == 3u, "control: 3 graphics pipeline creates (opaque/blended/prepass)");
    CHECK(g_stageConsumes == 5u, "control: 5 stage modules consumed (2+2+1)");
    CHECK(g_nullStageConsumes == 0u, "control: every consumed stage module was a live minted handle");
    CHECK(g_moduleDestroys == 3u, "control: every minted module destroyed after pipeline creation");
    printf("control: mints=%u creates=%u stages=%u nullStages=%u destroys=%u ok=%d\n",
           g_moduleMints, g_pipelineCreates, g_stageConsumes, g_nullStageConsumes, g_moduleDestroys, (int)ok);

    // trigger A: the first mint (geometry, flat.c:90) fails 〜 createShaderModule returns its
    // documented NULL sentinel (pipeline.c:89) and the consumer must stop, the way pipeline.c:104
    // stops and the way every loadFile arm one line above already stops
    printf("trigger A: vkCreateShaderModule refuses mint #1 (geometry, flat.c:90)\n");
    ok = run_phase(1u);
    CHECK(g_nullStageConsumes == 0u, "trigger A: a failed shader module must never reach vkCreateGraphicsPipelines as a stage module (flat.c:90 -> :106 -> :244/:261)");
    CHECK(!ok, "trigger A: ano_pipeline_flat_init reports false after a shader-module failure (the loadFile arms' own contract)");

    // trigger B: the second mint (depth pre-pass geometry, flat.c:91) fails 〜 the prepass
    // consumption lane shares the shape
    printf("trigger B: vkCreateShaderModule refuses mint #2 (depth pre-pass geometry, flat.c:91)\n");
    ok = run_phase(2u);
    CHECK(g_nullStageConsumes == 0u, "trigger B: a failed shader module must never reach vkCreateGraphicsPipelines as a stage module (flat.c:91 -> :269 -> :296)");
    CHECK(!ok, "trigger B: ano_pipeline_flat_init reports false after a shader-module failure");

    // trigger C: the third mint (fragment, flat.c:92) fails 〜 the last mint site shares the
    // shape, so the fix must cover the family, not one site
    printf("trigger C: vkCreateShaderModule refuses mint #3 (fragment, flat.c:92)\n");
    ok = run_phase(3u);
    CHECK(g_nullStageConsumes == 0u, "trigger C: a failed shader module must never reach vkCreateGraphicsPipelines as a stage module (flat.c:92 -> :112 -> :244/:261)");
    CHECK(!ok, "trigger C: ano_pipeline_flat_init reports false after a shader-module failure");

    if (failures) {
        printf("anotest_gfxshadermodguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gfxshadermodguard: all passed\n");
    return 0;
}
