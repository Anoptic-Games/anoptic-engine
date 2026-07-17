/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: flat_init_with_cull's failure-path resource orphans. The function discharges its
// three ano_aligned_malloc'd shader-code buffers and minted VkShaderModules only in the success
// epilogue (flat.c:301-:309), so every failure return after the first acquisition orphans
// whatever is live: :79 strands geomShaderCode, :88 both geometry buffers, :101 all three
// buffers plus the three modules minted at :90-:92, and the pipeline-create arms
// :244/:261/:296 strand three buffers plus three-to-four modules 〜 all stack locals no code
// outside the function can reach (ano_pipeline_flat_cleanup never sees them, and the boot
// caller returns false without even calling it, pipeline.c:128-:141); the author's own TODO
// names the missing "garbo removers for the shader buffers and modules" (pipeline.c:122)
// (docs/BUGS.md, Render / Vulkan backend / Implementation, flat.c:244).
// Harness: compiles the REAL flat.c TU 〜 no GPU device, no loader. loadFile/createShaderModule/
// ano_pipeline_task_stage are contract-faithful link-seam stand-ins mirroring
// pipeline.c:41/:78-93/:96-117; ano_aligned_malloc/ano_aligned_free are interposed in this TU
// (shadowing the anoptic_core member, forwarding to mi_malloc_aligned/mi_free) with a live-set
// ledger, and the vk stubs ledger shader-module mints/destroys; vkCreateGraphicsPipelines and
// loadFile carry armed Nth-call refusals (VK_ERROR_OUT_OF_HOST_MEMORY / a missing .spv 〜 the
// spec-clean arms these returns exist to handle).
// CONTROL: nothing armed 〜 init succeeds, 3 buffers minted and consumed live by 3 module mints,
// 3 pipeline creates consume 5 live stage modules, and at return every buffer is freed and
// every module destroyed; proves the epilogue and the harness plumbing, so a reject-everything
// fix cannot pass.
// After each armed phase the failed proto gets a best-effort ano_pipeline_flat_cleanup before
// measuring, so a fix that adopts ownership into the prototype for teardown passes too 〜 the
// invariant is fix-agnostic: no buffer or module left both live and unreachable.
// TRIGGER A: pipeline create #1 refused (flat.c:244) 〜 today 3 buffers + 3 modules orphan.
// TRIGGER B: loadFile #2 refused (flat.c:79) 〜 today geomShaderCode orphans.
// TRIGGER C: pipeline create #3 refused (prepass lane, flat.c:296) 〜 shares A's shape.
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


/* Allocator seam 〜 these definitions shadow anoptic_core's memalign member (never pulled: both
   its symbols resolve here first), so every ano_aligned block the flat TU mints or frees crosses
   this ledger. Bounded live set; overflow counts as failure. */

#define TRACK_MAX 32
static void    *g_liveBlk[TRACK_MAX];
static size_t   g_liveBlkBytes[TRACK_MAX];
static uint32_t g_liveBlkCount;
static uint32_t g_alignedMints;   // successful ano_aligned_malloc returns
static uint32_t g_alignedFrees;   // non-NULL ano_aligned_free calls
static bool     g_trackOverflow;

// in: size/alignment per the anoptic_memory.h contract  out: tracked block from mi_malloc_aligned
void* ano_aligned_malloc(size_t size, size_t alignment)
{
    void *p = mi_malloc_aligned(size, alignment);
    if (p == NULL) return NULL;
    g_alignedMints++;
    if (g_liveBlkCount >= TRACK_MAX) { g_trackOverflow = true; return p; }
    g_liveBlk[g_liveBlkCount] = p;
    g_liveBlkBytes[g_liveBlkCount] = size;
    g_liveBlkCount++;
    return p;
}

// in: block or NULL  out: untracked, forwarded to mi_free
void ano_aligned_free(void* ptr)
{
    if (ptr != NULL) {
        g_alignedFrees++;
        for (uint32_t i = 0; i < g_liveBlkCount; i++)
            if (g_liveBlk[i] == ptr) {
                g_liveBlkCount--;
                g_liveBlk[i] = g_liveBlk[g_liveBlkCount];
                g_liveBlkBytes[i] = g_liveBlkBytes[g_liveBlkCount];
                break;
            }
    }
    mi_free(ptr);
}

// out: count of live ano_aligned blocks, each printed 〜 after cleanup these are unreachable
static uint32_t orphaned_blocks(void)
{
    for (uint32_t i = 0; i < g_liveBlkCount; i++)
        printf("  orphaned shader-code buffer %p (%" PRIuMAX " B)\n",
               g_liveBlk[i], (uintmax_t)g_liveBlkBytes[i]);
    return g_liveBlkCount;
}

// out: true iff p lands inside a currently-live tracked block
static bool in_live_block(const void *p)
{
    const char *c = (const char *)p;
    for (uint32_t i = 0; i < g_liveBlkCount; i++) {
        const char *b = (const char *)g_liveBlk[i];
        if (c >= b && c < b + g_liveBlkBytes[i]) return true;
    }
    return false;
}


/* Module ledger 〜 mints/destroys and every pStages[].module a pipeline create consumes */

static uint32_t g_moduleMints;       // successful vkCreateShaderModule returns
static uint32_t g_moduleDestroys;    // non-NULL vkDestroyShaderModule calls
static uint32_t g_mintFromLive;      // mints whose pCode sat in a live tracked buffer
static VkShaderModule g_liveMod[16]; // live minted module handles
static uint32_t g_liveModCount;

static uint32_t g_pipelineCreates;   // vkCreateGraphicsPipelines createInfos consumed
static uint32_t g_failCreateAt;      // 1-based create attempt to refuse; 0 = never
static uint32_t g_nullStageConsumes; // consumed stage modules that were NULL or non-ledgered

// out: true iff h is a currently-live minted module handle
static bool module_live(VkShaderModule h)
{
    for (uint32_t i = 0; i < g_liveModCount; i++) if (g_liveMod[i] == h) return true;
    return false;
}

// out: count of live minted modules, each printed 〜 after cleanup these are unreachable
static uint32_t orphaned_modules(void)
{
    for (uint32_t i = 0; i < g_liveModCount; i++)
        printf("  orphaned VkShaderModule %p\n", (void *)g_liveMod[i]);
    return g_liveModCount;
}


/* Link seams 〜 shader acquisition stand-ins (contract-faithful to pipeline.c) */

static uint32_t g_loadCalls;   // all loadFile calls
static uint32_t g_failLoadAt;  // 1-based loadFile call to refuse; 0 = never

// Stand-in for pipeline.c:41 〜 mints through the interposed ano_aligned_malloc exactly like the
// real loadFile (:55); the armed refusal is the missing-.spv arm (:44-:48, nothing minted).
bool loadFile(const char* filename, struct Buffer* buffer)
{
    (void)filename;
    g_loadCalls++;
    if (g_failLoadAt != 0u && g_loadCalls == g_failLoadAt) return false;
    buffer->size = 16u;
    buffer->data = ano_aligned_malloc(buffer->size, alignof(uint32_t));
    if (buffer->data == NULL) return false;
    memset(buffer->data, 0x5A, buffer->size);
    return true;
}

// Verbatim mirror of pipeline.c:78-93.
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

// Mirror of pipeline.c:96-117 〜 never invoked here (taskCull stays false) but the link seam
// stays contract-faithful; note the task lane would add a fourth orphaned module at :244+.
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
    (void)device; (void)pAllocator;
    if (in_live_block(pCreateInfo->pCode)) g_mintFromLive++; // consumption order proof
    VkShaderModule h = (VkShaderModule)(uintptr_t)(0x0F1A0000u + ++g_moduleMints);
    if (g_liveModCount < sizeof g_liveMod / sizeof g_liveMod[0]) g_liveMod[g_liveModCount++] = h;
    *pShaderModule = h;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (shaderModule == VK_NULL_HANDLE) return; // legal no-op
    g_moduleDestroys++;
    for (uint32_t i = 0; i < g_liveModCount; i++)
        if (g_liveMod[i] == shaderModule) { g_liveMod[i] = g_liveMod[--g_liveModCount]; break; }
}

// Armed Nth-call refusal with VK_ERROR_OUT_OF_HOST_MEMORY 〜 the spec-clean failure arm; failed
// entries get VK_NULL_HANDLE per the pipeline-creation contract.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    (void)device; (void)pipelineCache; (void)pAllocator;
    static uint32_t mint = 0;
    for (uint32_t i = 0; i < createInfoCount; i++) {
        g_pipelineCreates++;
        for (uint32_t s = 0; s < pCreateInfos[i].stageCount; s++) {
            VkShaderModule m = pCreateInfos[i].pStages[s].module;
            if (m == VK_NULL_HANDLE || !module_live(m)) g_nullStageConsumes++;
        }
        if (g_failCreateAt != 0u && g_pipelineCreates == g_failCreateAt) {
            pPipelines[i] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        pPipelines[i] = (VkPipeline)(uintptr_t)(0x91BE0000u + ++mint);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineLayout = (VkPipelineLayout)(uintptr_t)(0x1A700000u + ++mint); return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
{ (void)device; (void)pCreateInfo; (void)pAllocator; static uint32_t mint = 0; *pPipelineCache = (VkPipelineCache)(uintptr_t)(0xCAC0000u + ++mint); return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipelineCache; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipelineLayout; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pipeline; (void)pAllocator; }


/* Phase runner 〜 one ano_pipeline_flat_init pass over fresh state, ledgers reset, best-effort
   real teardown before measuring so ownership-adoption fixes pass */

static VulkanContext g_ctx;
static RendererState g_state;

// in: failLoadAt / failCreateAt = 1-based armed refusals (0 = none)  out: init's verdict
static bool run_phase(uint32_t failLoadAt, uint32_t failCreateAt)
{
    memset(&g_ctx, 0, sizeof g_ctx);
    memset(&g_state, 0, sizeof g_state);
    g_liveBlkCount = g_alignedMints = g_alignedFrees = 0;
    g_moduleMints = g_moduleDestroys = g_mintFromLive = g_liveModCount = 0;
    g_pipelineCreates = g_nullStageConsumes = g_loadCalls = 0;
    g_failLoadAt = failLoadAt;
    g_failCreateAt = failCreateAt;

    g_ctx.device = (VkDevice)(uintptr_t)0xD7;
    g_ctx.deviceCapabilities.meshShader = false;   // vertex fallback lane, taskCull off
    g_ctx.msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    g_state.imageExtent = (VkExtent2D){1920u, 1080u};
    g_state.depthFormat = VK_FORMAT_D32_SFLOAT;

    bool ok = ano_pipeline_flat_init(&g_ctx, &g_state, &g_state.prototypes[PIPELINE_FLAT]);
    ano_pipeline_flat_cleanup(&g_ctx, &g_state, &g_state.prototypes[PIPELINE_FLAT]);
    return ok;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control: nothing armed 〜 the epilogue discharges everything, so the ledgers prove the
    // harness plumbing and a reject-everything fix cannot pass
    printf("control: no failure armed\n");
    bool ok = run_phase(0u, 0u);
    CHECK(ok, "control: ano_pipeline_flat_init succeeds on good input");
    CHECK(g_alignedMints == 3u, "control: 3 shader-code buffers minted through loadFile");
    CHECK(g_moduleMints == 3u && g_mintFromLive == 3u, "control: 3 modules minted, each consuming a live code buffer");
    CHECK(g_pipelineCreates == 3u && g_nullStageConsumes == 0u, "control: 3 pipeline creates, every consumed stage module live");
    CHECK(g_alignedFrees == 3u && g_liveBlkCount == 0u, "control: every code buffer freed at return");
    CHECK(g_moduleDestroys == 3u && g_liveModCount == 0u, "control: every minted module destroyed at return");
    CHECK(!g_trackOverflow, "control: allocation tracker within bounds");
    printf("control: blkMints=%u blkFrees=%u modMints=%u modDestroys=%u creates=%u ok=%d\n",
           g_alignedMints, g_alignedFrees, g_moduleMints, g_moduleDestroys, g_pipelineCreates, (int)ok);

    // trigger A: pipeline create #1 refused (flat.c:244) 〜 the return must not orphan the three
    // code buffers and three modules the function acquired (the epilogue frees only at :301-:309)
    printf("trigger A: vkCreateGraphicsPipelines refuses create #1 (flat.c:244)\n");
    ok = run_phase(0u, 1u);
    CHECK(!ok, "trigger A: init reports false after a pipeline-create failure");
    CHECK(orphaned_blocks() == 0u, "trigger A: no shader-code buffer left live and unreachable after failure + cleanup (flat.c:244 return skips the :301-:303 frees)");
    CHECK(orphaned_modules() == 0u, "trigger A: no shader module left live and unreachable after failure + cleanup (flat.c:244 return skips the :305-:309 destroys)");

    // trigger B: loadFile #2 refused (flat.c:79) 〜 the first buffer is already minted and the
    // return strands it before any module exists
    printf("trigger B: loadFile refuses call #2 (flat.c:79)\n");
    ok = run_phase(2u, 0u);
    CHECK(!ok, "trigger B: init reports false after a shader-load failure");
    CHECK(orphaned_blocks() == 0u, "trigger B: no shader-code buffer left live and unreachable after failure + cleanup (flat.c:79 return strands geomShaderCode)");
    CHECK(orphaned_modules() == 0u, "trigger B: no shader module left live and unreachable");

    // trigger C: pipeline create #3 refused (prepass lane, flat.c:296) 〜 the deepest arm shares
    // A's shape, so the fix must cover every return, not one site
    printf("trigger C: vkCreateGraphicsPipelines refuses create #3 (flat.c:296)\n");
    ok = run_phase(0u, 3u);
    CHECK(!ok, "trigger C: init reports false after a pipeline-create failure");
    CHECK(orphaned_blocks() == 0u, "trigger C: no shader-code buffer left live and unreachable after failure + cleanup (flat.c:296 return skips the :301-:303 frees)");
    CHECK(orphaned_modules() == 0u, "trigger C: no shader module left live and unreachable after failure + cleanup (flat.c:296 return skips the :305-:309 destroys)");

    if (failures) {
        printf("anotest_flatorphanguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_flatorphanguard: all passed\n");
    return 0;
}
