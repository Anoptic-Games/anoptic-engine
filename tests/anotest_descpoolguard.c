/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: descriptor-pool sizing vs set-layout demand. createDescriptorPool's combined-image-sampler
// budget (descriptors.c:39) enumerates tonemap/view + 4 shadow + Hi-Z (pyramid+depth)/mip + cull
// bind11 pyramids/view + 1 text overlay 〜 but has no term for the task-cull Hi-Z sampler the global
// layout gains at binding 13 when taskCull is on (layouts.c:123-:128, bindingCount 14 at :149; written
// per view per frame at descriptors.c:483-:496). That is ANO_VIEW_COUNT extra samplers per frame, and
// with the shipped constants (MFIF 3, views 2, hiz mips 16) createDescriptorSets' own allocations then
// consume the sampler budget exactly (219 of 219), so the LAST consumer of the same pool 〜 the text
// overlay sets ano_vk_text_create_sets allocates with tonemapSetLayout (text_raster.c:846/:852) 〜
// gets VK_ERROR_OUT_OF_POOL_MEMORY and the overlay is silently disabled (:862-:864), taking the UI
// overlay riding those sets with it, on exactly the mesh+task-shader hardware where taskCull defaults
// on (vulkanMaster.c:390) (docs/BUGS.md, Render / Vulkan backend / Implementation, descriptors.c:39).
// Harness: compiles the REAL descriptors.c TU 〜 no GPU device, no loader. The vk stubs do
// contract-faithful pool accounting: per-type capacities and maxSets from the real pool create,
// all-or-nothing per allocate call, entries VK_NULL_HANDLE on refusal. Set layouts are spec handles
// mirroring the real binding tables, each cited to its create site; the text consumer mirrors
// text_raster.c:841-:866 batch for batch. CONTROL: taskCull off 〜 the full sequence including both
// text batches fits the pool, so an over-tight harness or a shrunken-pool "fix" cannot pass.
// TRIGGER: the same sequence with taskCull on 〜 fails until the pool budgets the binding-13 samplers.
// A crash is a valid failure signal. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h" // createDescriptorPool / createDescriptorSets, RendererState

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Layout specs 〜 per-type descriptor demand of each real set layout, cited to its create site */

typedef struct { const char* name; uint32_t ubo, ssbo, ssboDyn, cis, simg; } LayoutSpec;

static const LayoutSpec g_globalOff  = { "global(13)",  1, 12, 0, 0, 0 }; // layouts.c:130-:149, taskCull off
static const LayoutSpec g_globalTask = { "global(14)",  1, 12, 0, 1, 0 }; // + binding 13 CIS, layouts.c:123-:128
static const LayoutSpec g_cull       = { "cull",        1, 10, 0, 2, 0 }; // layouts.c:163-:235, bind 11 count = ANO_VIEW_COUNT
static const LayoutSpec g_update     = { "update",      1,  3, 0, 0, 0 }; // compute.c:19-:47
static const LayoutSpec g_scatter    = { "scatter",     0,  2, 1, 0, 0 }; // compute.c:105-:117
static const LayoutSpec g_lightcull  = { "lightcull",   1,  4, 0, 0, 0 }; // compute.c:357-:367
static const LayoutSpec g_lightsetup = { "lightsetup",  0,  3, 0, 0, 0 }; // compute.c:408-:417
static const LayoutSpec g_tonemap    = { "tonemap",     0,  0, 0, 1, 0 }; // tonemap.c:22-:30; text overlay reuses it (text_raster.c:846)
static const LayoutSpec g_hiz        = { "hiz",         0,  0, 0, 2, 1 }; // layouts.c:249-:265
static const LayoutSpec g_shadowSet  = { "shadowsetup", 0,  5, 0, 0, 0 }; // layouts.c:276-:285
static const LayoutSpec g_shadowGeom = { "shadowgeom",  1,  2, 0, 1, 0 }; // layouts.c:294-:313
static const LayoutSpec g_blur       = { "shadowblur",  0,  0, 0, 1, 0 }; // shadow_pipe.c:191-:197
static const LayoutSpec g_textRaster = { "textraster",  0, 10, 0, 0, 1 }; // text_raster.c:299-:312

#define LH(spec) ((VkDescriptorSetLayout)(uintptr_t)&(spec))

// in: layout handle minted by LH; out: its spec.
static const LayoutSpec* spec_of(VkDescriptorSetLayout h) { return (const LayoutSpec*)(uintptr_t)h; }


/* Pool ledger 〜 contract-faithful per-type accounting for the one pool under audit */

enum { T_UBO, T_SSBO, T_DYN, T_CIS, T_SIMG, T_N };
static const char* g_typeName[T_N] = { "UNIFORM_BUFFER", "STORAGE_BUFFER", "STORAGE_DYNAMIC", "COMBINED_IMAGE_SAMPLER", "STORAGE_IMAGE" };

static struct {
    bool     live;
    uint32_t maxSets, setsUsed;
    uint32_t cap[T_N], used[T_N];
} g_pool;
static uint32_t g_unknownTypes;   // pool sizes / layout demand in a type this harness does not model
static uint32_t g_setMint;        // fake set handle counter
static char     g_refusal[256];   // first refused batch, for the report

// in: a Vulkan descriptor type; out: harness type index, or -1 (counted as unknown).
static int type_index(VkDescriptorType t)
{
    switch (t) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:         return T_UBO;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:         return T_SSBO;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return T_DYN;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return T_CIS;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:          return T_SIMG;
    default: g_unknownTypes++;                      return -1;
    }
}

// Prints the pool's per-type used/capacity table for the current phase.
static void pool_report(const char* phase)
{
    printf("%s: sets %u/%u", phase, g_pool.setsUsed, g_pool.maxSets);
    for (int t = 0; t < T_N; t++) printf(", %s %u/%u", g_typeName[t], g_pool.used[t], g_pool.cap[t]);
    printf("\n");
}


/* Link seams 〜 globals + helpers descriptors.c references (real definitions live in vulkanMaster.c / components.c) */

RendererState rendererState;

uint32_t ano_draw_partition_count(void) { return 1u; }


/* Link seams 〜 the vk* entry points descriptors.c calls (loader not linked) */

// Records the real formula's capacities; the accounting below enforces them.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{
    (void)device; (void)pAllocator;
    memset(&g_pool, 0, sizeof g_pool);
    g_pool.live = true;
    g_pool.maxSets = pCreateInfo->maxSets;
    for (uint32_t i = 0; i < pCreateInfo->poolSizeCount; i++) {
        int t = type_index(pCreateInfo->pPoolSizes[i].type);
        if (t >= 0) g_pool.cap[t] += pCreateInfo->pPoolSizes[i].descriptorCount;
    }
    *pDescriptorPool = (VkDescriptorPool)(uintptr_t)0xB001;
    return VK_SUCCESS;
}

// Contract-faithful: all-or-nothing per call, VK_ERROR_OUT_OF_POOL_MEMORY once any per-type
// capacity or maxSets would be exceeded, all entries VK_NULL_HANDLE on refusal.
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
{
    (void)device;
    uint32_t need[T_N] = {0};
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        const LayoutSpec* s = spec_of(pAllocateInfo->pSetLayouts[i]);
        need[T_UBO] += s->ubo; need[T_SSBO] += s->ssbo; need[T_DYN] += s->ssboDyn;
        need[T_CIS] += s->cis; need[T_SIMG] += s->simg;
    }
    bool fits = g_pool.live && g_pool.setsUsed + pAllocateInfo->descriptorSetCount <= g_pool.maxSets;
    for (int t = 0; t < T_N; t++) if (g_pool.used[t] + need[t] > g_pool.cap[t]) fits = false;
    if (!fits) {
        if (g_refusal[0] == '\0') {
            const LayoutSpec* s0 = spec_of(pAllocateInfo->pSetLayouts[0]);
            snprintf(g_refusal, sizeof g_refusal, "refused batch: %u x %s (first shortfall: %s)",
                     pAllocateInfo->descriptorSetCount, s0->name,
                     g_pool.setsUsed + pAllocateInfo->descriptorSetCount > g_pool.maxSets ? "maxSets" :
                     g_pool.used[T_CIS] + need[T_CIS] > g_pool.cap[T_CIS] ? g_typeName[T_CIS] : "see table");
        }
        for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) pDescriptorSets[i] = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    g_pool.setsUsed += pAllocateInfo->descriptorSetCount;
    for (int t = 0; t < T_N; t++) g_pool.used[t] += need[t];
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
        pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)(0x5E70000u + ++g_setMint);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{
    (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites; (void)descriptorCopyCount; (void)pDescriptorCopies;
}


/* The text consumer 〜 mirrors ano_vk_text_create_sets' two batches (text_raster.c:841-:866) */

// out: true iff both batches fit; false is the seam where the real code disables the overlay (:862-:864).
static bool text_create_sets_mirror(void)
{
    VkDescriptorSetLayout rasterLayouts[MAX_FRAMES_IN_FLIGHT], overlayLayouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rasterLayouts[i]  = LH(g_textRaster);                 // :845
        overlayLayouts[i] = rendererState.tonemapSetLayout;   // :846
    }
    VkDescriptorSet out[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = rendererState.globalDescriptorPool; // :852
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    VkDescriptorSetLayout* setLayouts[2] = { rasterLayouts, overlayLayouts };
    for (int s = 0; s < 2; s++) {
        allocInfo.pSetLayouts = setLayouts[s];
        if (vkAllocateDescriptorSets((VkDevice)(uintptr_t)0x77, &allocInfo, out) != VK_SUCCESS)
            return false; // text_raster.c:862-:864 〜 "Text overlay disabled", textOverlay = false
    }
    return true;
}


/* Phase runner 〜 the real init sequence (vulkanMaster.c:654 then :667) in one feature configuration */

// in: taskCull; out: pool create / init sets / text sets results.
// inv: fresh rendererState and pool ledger per phase; layout handles mirror what init built by :654.
static void run_phase(bool taskCull, bool* poolOk, bool* setsOk, bool* textOk)
{
    memset(&rendererState, 0, sizeof rendererState);
    memset(&g_pool, 0, sizeof g_pool);
    g_refusal[0] = '\0';
    rendererState.taskCull = taskCull;
    rendererState.globalSetLayout      = taskCull ? LH(g_globalTask) : LH(g_globalOff);
    rendererState.culling.setLayout    = LH(g_cull);
    rendererState.updateSetLayout      = LH(g_update);
    rendererState.scatterSetLayout     = LH(g_scatter);
    rendererState.lightcullSetLayout   = LH(g_lightcull);
    rendererState.lightsetupSetLayout  = LH(g_lightsetup);
    rendererState.tonemapSetLayout     = LH(g_tonemap);
    rendererState.hizSetLayout         = LH(g_hiz);
    rendererState.shadowSetupSetLayout = LH(g_shadowSet);
    rendererState.shadowGeomSetLayout  = LH(g_shadowGeom);
    rendererState.shadowBlurSetLayout  = LH(g_blur);

    VulkanContext c = {0};
    c.device = (VkDevice)(uintptr_t)0x77;
    *poolOk = createDescriptorPool(&c, &rendererState);
    *setsOk = *poolOk && createDescriptorSets(&c, &rendererState);
    *textOk = *setsOk && text_create_sets_mirror();
    pool_report(taskCull ? "taskCull=on " : "taskCull=off");
    if (g_refusal[0]) printf("  %s\n", g_refusal);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    bool poolOk, setsOk, textOk;

    // control: taskCull off 〜 the pool's own comment budgets every consumer below, and they all fit,
    // so the harness accounting is not reject-everything and a shrunken pool cannot pass either
    run_phase(false, &poolOk, &setsOk, &textOk);
    CHECK(poolOk, "control: createDescriptorPool succeeds");
    CHECK(setsOk, "control: createDescriptorSets fits the pool with taskCull off");
    CHECK(textOk, "control: the text raster+overlay sets fit the pool with taskCull off");
    CHECK(g_unknownTypes == 0, "control: no descriptor type outside the harness model");

    // trigger: taskCull on 〜 the global layout gains binding 13 (1 sampler per view set,
    // layouts.c:149) but the pool formula has no term for it (descriptors.c:39), so the same
    // consumer sequence must still fit and does not: the overlay batch is refused and the real
    // caller silently disables the text overlay (text_raster.c:862-:864)
    printf("trigger: taskCull=on 〜 expect the binding-13 samplers to exhaust the CIS budget before the text overlay sets\n");
    run_phase(true, &poolOk, &setsOk, &textOk);
    CHECK(poolOk, "trigger: createDescriptorPool succeeds");
    CHECK(setsOk, "trigger: createDescriptorSets fits the pool with taskCull on");
    CHECK(textOk, "trigger: pool covers the text overlay sets with taskCull on (descriptors.c:39 omits the global binding-13 samplers)");
    CHECK(g_unknownTypes == 0, "trigger: no descriptor type outside the harness model");

    if (failures) {
        printf("anotest_descpoolguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_descpoolguard: all passed\n");
    return 0;
}
