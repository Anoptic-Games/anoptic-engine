/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: silent ownership drop at the texture registration seam. ano_vk_register_texture
// (components.c:72) is the ONLY route a loaded texture's VkImage/VkImageView/GpuAllocation take
// into the teardown registry 〜 cleanupVulkan walks primitives.textureBuffers and nothing else
// destroys the loaded views/images (cleanup.c:64-:71) 〜 yet the function returns void and its
// realloc-failure arm (:76-:78) logs ANO_ERROR and drops the TextureData record, so the glTF
// caller cannot hear the refusal and proceeds to bindless-register and draw the view
// (ano_GltfParser.c:282-:288) whose objects now orphan permanently: still resident, still
// sampled, unreachable at shutdown (docs/BUGS.md, Render / Vulkan backend /
// Interlink-Composition, components.c:72).
// Harness: compiles the REAL components.c TU with its allocator tokens interposed onto libc
// (anoptic_memory.h maps the TU's realloc/free to mi_realloc/mi_free; the build renames those to
// the anotest_seam_* countdown shims), so the Nth registry growth fails exactly like a real
// out-of-memory realloc 〜 NULL return, old block untouched. No GPU device, no loader. The caller
// model is contract-faithful to ano_GltfParser.c:277-:288: mint handles, register, publish
// bindless, never look back. The teardown model is cleanup.c:64-:72 verbatim: discharge what the
// registry holds, then ano_vk_cleanup_primitives. Controls prove healthy registration lands every
// record in order with usageCount zeroed, teardown discharges every minted handle, and the
// registry heap balances 〜 so a reject-everything fix cannot pass. Fix-agnostic invariant: after
// shutdown, every handle minted for a registered texture has been discharged 〜 satisfied by
// pre-reserved capacity, by an observable failure channel whose caller discharges, or by any
// registration that cannot silently drop. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/components.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Allocator seam 〜 components.c's realloc/free tokens land here, routed to libc */

static int      g_failReallocAt;   // Nth seam realloc fails; 0 = healthy
static uint32_t g_liveBlocks;      // registry heap blocks outstanding

// in: p, n as realloc
// out: libc realloc result, or NULL when the armed countdown hits zero (old block untouched)
void* anotest_seam_realloc(void* p, size_t n)
{
    if (g_failReallocAt > 0 && --g_failReallocAt == 0) return NULL;
    void* q = realloc(p, n);
    if (q && !p) g_liveBlocks++;
    return q;
}

// in: p as free
void anotest_seam_free(void* p)
{
    if (p) g_liveBlocks--;
    free(p);
}


/* Mint ledger 〜 every fake GPU object handed to the registration seam */

#define MAX_TEX 32
static VkImage     g_img[MAX_TEX];
static VkImageView g_view[MAX_TEX];
static bool        g_imgLive[MAX_TEX];
static bool        g_viewLive[MAX_TEX];
static bool        g_bindless[MAX_TEX];  // published to the bindless set by the caller model
static uint32_t    g_texCount;
static uint32_t    g_doubleDestroys;     // whole-run invariant
static uint32_t    g_unknownDestroys;    // whole-run invariant

// Resets the mint ledger between scenarios (heap balance and destroy counters run whole-run).
static void ledger_reset(void)
{
    memset(g_imgLive, 0, sizeof g_imgLive);
    memset(g_viewLive, 0, sizeof g_viewLive);
    memset(g_bindless, 0, sizeof g_bindless);
    g_texCount = 0;
}

// in: prims 〜 the registry under test
// Contract-faithful to ano_GltfParser.c:277-:288: mint image+view, hand the ownership record to
// ano_vk_register_texture (void 〜 nothing to check), then bindless-publish the view regardless.
static void model_gltf_load_texture(RenderPrimitives* prims)
{
    uint32_t i = g_texCount++;
    g_img[i]  = (VkImage)(uintptr_t)(0xA000u + i);
    g_view[i] = (VkImageView)(uintptr_t)(0xB000u + i);
    g_imgLive[i] = g_viewLive[i] = true;

    TextureData td = {0};
    td.textureImage = g_img[i];
    td.textureImageView = g_view[i];
    td.usageCount = 77u; // adopted record 〜 register must zero it
    ano_vk_register_texture(prims, td);

    g_bindless[i] = true; // bindless_register_texture(view, sampler) 〜 the caller heard nothing to skip
}

// in: v 〜 a view handle the teardown walk destroys; discharges its ledger entry
static void destroy_view(VkImageView v)
{
    for (uint32_t t = 0; t < g_texCount; t++) {
        if (g_view[t] == v) {
            if (!g_viewLive[t]) g_doubleDestroys++;
            g_viewLive[t] = false;
            return;
        }
    }
    g_unknownDestroys++;
}

// in: m 〜 an image handle the teardown walk destroys; discharges its ledger entry
static void destroy_image(VkImage m)
{
    for (uint32_t t = 0; t < g_texCount; t++) {
        if (g_img[t] == m) {
            if (!g_imgLive[t]) g_doubleDestroys++;
            g_imgLive[t] = false;
            return;
        }
    }
    g_unknownDestroys++;
}

// in: prims 〜 the registry under test
// cleanup.c:64-:72 verbatim: destroy what primitives.textureBuffers holds, then release the registry.
static void model_cleanup(RenderPrimitives* prims)
{
    for (uint32_t i = 0; i < prims->textureCount; i++) {
        if (prims->textureBuffers[i].textureImageView) destroy_view(prims->textureBuffers[i].textureImageView);
        if (prims->textureBuffers[i].textureImage)     destroy_image(prims->textureBuffers[i].textureImage);
    }
    ano_vk_cleanup_primitives(prims);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control: nine healthy registrations 〜 growth 0->8->16 succeeds, every record lands in order
    static RenderPrimitives good;
    for (int i = 0; i < 9; i++) model_gltf_load_texture(&good);
    CHECK(good.textureCount == 9u, "control: all nine registrations resident");
    CHECK(good.textureCapacity >= 9u, "control: registry capacity grew to cover the batch");
    bool orderOk = true, usageOk = true;
    for (uint32_t i = 0; i < good.textureCount && i < g_texCount; i++) {
        if (good.textureBuffers[i].textureImageView != g_view[i] ||
            good.textureBuffers[i].textureImage != g_img[i]) orderOk = false;
        if (good.textureBuffers[i].usageCount != 0u) usageOk = false;
    }
    CHECK(orderOk, "control: records stored in registration order");
    CHECK(usageOk, "control: adopted usageCount zeroed");
    model_cleanup(&good);
    bool allDischarged = true;
    for (uint32_t t = 0; t < g_texCount; t++)
        if (g_imgLive[t] || g_viewLive[t]) allDischarged = false;
    CHECK(allDischarged, "control: teardown discharges every minted handle");
    CHECK(good.textureBuffers == NULL && good.textureCount == 0u && good.textureCapacity == 0u, "control: registry zeroed after cleanup");
    CHECK(g_liveBlocks == 0u, "control: registry heap balanced");

    // trigger: the 9th registration's growth realloc (8 -> 16) refuses 〜 the record is dropped
    // with the caller unable to hear it (void return), the view stays bindless-published, and
    // the cleanup walk can never reach the orphaned handles
    printf("trigger: OOM on the second registry growth 〜 expect texture #9's image+view undischarged at shutdown\n");
    fflush(stdout);
    ledger_reset();
    static RenderPrimitives world;
    g_failReallocAt = 2; // realloc #1: 0->8 mint; realloc #2: 8->16 growth, injected to fail
    for (int i = 0; i < 9; i++) model_gltf_load_texture(&world);
    CHECK(g_failReallocAt == 0, "trigger: injection consumed");
    printf("step: registry holds %u of %u loaded textures after the refused growth\n", world.textureCount, g_texCount);
    bool prefixOk = true;
    for (uint32_t i = 0; i < world.textureCount && i < 8u && i < g_texCount; i++)
        if (world.textureBuffers[i].textureImageView != g_view[i]) prefixOk = false;
    CHECK(prefixOk, "trigger: resident prefix intact after the refused growth (realloc contract: old block untouched)");
    model_cleanup(&world);
    uint32_t orphans = 0;
    for (uint32_t t = 0; t < g_texCount; t++) {
        if (g_imgLive[t] || g_viewLive[t]) {
            orphans++;
            printf("  orphan: texture %" PRIu32 " image %p view %p 〜 bindless-published: %s, unreachable at shutdown\n",
                   t, (void*)g_img[t], (void*)g_view[t], g_bindless[t] ? "yes" : "no");
        }
    }
    CHECK(orphans == 0, "no loaded texture orphans at shutdown after a refused registration (components.c:76 drops the only ownership record cleanup.c:64 can reach)");
    CHECK(g_liveBlocks == 0u, "trigger: registry heap balanced");

    // whole-run ledger invariants 〜 a fix must not double-destroy or destroy handles never minted
    CHECK(g_doubleDestroys == 0, "no handle destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unminted handle destroyed");

    if (failures) {
        printf("anotest_texregisterguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_texregisterguard: all passed\n");
    return 0;
}
