/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the mid-batch failure return in render_slots_alloc_range. The loop at
// render_slots.c:91-95 publishes each element's forward and reverse mapping as it walks the
// batch (:93/:94) but advances slotHighWater only in the epilogue (:96), so the logical_reserve
// OOM return at :92 reports UNMAPPED while the already-walked prefix stays mapped into the
// un-owned high-water region (docs/BUGS.md, Render / Vulkan backend / Implementation,
// render_slots.c:92). Every other OOM arm in this module explicitly preserves its invariants
// ("Leaves *arr/*cap untouched on OOM" :16, "Quarantine OOM: leak the slot" :132, free-list OOM
// keeps quarantined :154); this one corrupts them. The caller cannot hear it: apply.c:167
// discards the return and its resolve loop at :170 stages GPU uploads, base poses and shadow
// tracking for the phantom prefix slots. The next single create re-hands slot base to a
// different render_id (:75 highWater++), aliasing two live ids onto one physical slot; a
// destroy of the stale id then retires the live owner's slot through quarantine into the
// free-list and strips its reverse mapping, and the slot is handed out a third time while
// still owned 〜 exactly the double allocation the frame-gated quarantine exists to prevent.
// Harness: compiles the REAL render_slots.c TU with the heap tokens (mi_heap_malloc,
// mi_heap_realloc, mi_free) interposed to fail exactly one growth on demand; pure index
// bookkeeping, no GPU device, no loader. The failure arm is contract-faithful to
// mi_heap_realloc: NULL return, old block untouched. Controls prove the good batch and the
// first-element failure both behave, so a reject-every-batch fix cannot pass. A live-block
// ledger audits allocator balance as a future-fix invariant. Fails until a failed batch
// unwinds (or never publishes) its prefix. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vulkan_backend/render_slots.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Interposed heap seams 〜 render_slots.c's mi_heap_malloc / mi_heap_realloc / mi_free arrive
   here via token rename; backed by libc, self-consistent within this executable */

static bool     g_failNextGrow; // one-shot: the next heap realloc returns NULL
static uint32_t g_failedGrows;  // how many injections fired 〜 trigger bookkeeping
static int32_t  g_liveBlocks;   // allocator balance ledger, must return to 0

// in: heap (dummy token, ignored), size; out: live block. Ledger +1.
void *anotest_seam_heap_malloc(mi_heap_t *heap, size_t size)
{
    (void)heap;
    void *p = malloc(size);
    if (p) g_liveBlocks++;
    return p;
}

// in: heap (ignored), old block, new size; out: grown block, or NULL on the injected OOM.
// Contract-faithful to mi_heap_realloc's failure: old block stays valid and untouched.
void *anotest_seam_heap_realloc(mi_heap_t *heap, void *p, size_t newsize)
{
    (void)heap;
    if (g_failNextGrow) { g_failNextGrow = false; g_failedGrows++; return NULL; }
    void *np = realloc(p, newsize);
    if (np && !p) g_liveBlocks++;
    return np;
}

// Ledger -1 on a real block.
void anotest_seam_free(void *p)
{
    if (p) g_liveBlocks--;
    free(p);
}

// Opaque non-NULL heap token; the table only stores and forwards it to the seams above.
static mi_heap_t *test_heap(void)
{
    static unsigned char token;
    return (mi_heap_t *)(void *)&token;
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    mi_heap_t *heap = test_heap();

    // control: a good bulk range publishes a bijective mapping and advances the high-water
    RenderSlotTable t;
    CHECK(render_slots_init(&t, heap, 8, 2), "init (maxSlots 8, fif 2)");
    uint32_t ids[3] = { 10, 11, 12 };
    CHECK(render_slots_alloc_range(&t, ids, 3) == 0, "control: bulk range base 0");
    for (uint32_t i = 0; i < 3; i++) {
        CHECK(render_slots_resolve(&t, ids[i]) == i, "control: forward map holds");
        CHECK(render_slots_render_id_of(&t, i) == ids[i], "control: reverse map holds");
    }
    CHECK(t.slotHighWater == 3, "control: high-water advanced over the batch");
    CHECK(render_slots_alloc(&t, 13) == 3, "control: next single alloc continues past the batch");
    render_slots_destroy(&t);

    // control: a growth failure on the batch's FIRST element leaves the table untouched
    RenderSlotTable t2;
    CHECK(render_slots_init(&t2, heap, 8, 2), "init t2");
    uint32_t first[2] = { 100, 101 };
    g_failNextGrow = true;
    CHECK(render_slots_alloc_range(&t2, first, 2) == ANO_RENDER_SLOT_UNMAPPED, "control: first-element growth failure reports UNMAPPED");
    CHECK(g_failedGrows == 1, "control: injection fired once");
    CHECK(t2.slotHighWater == 0, "control: high-water untouched by the failed batch");
    CHECK(render_slots_alloc(&t2, 200) == 0, "control: table still allocates cleanly after the failure");
    CHECK(render_slots_render_id_of(&t2, 0) == 200, "control: slot 0 owned by the post-failure alloc");
    render_slots_destroy(&t2);

    // trigger: a MID-batch growth failure 〜 render_slots.c:92 returns UNMAPPED after :93-:94
    // already published element 0's mappings into the un-owned high-water region
    printf("trigger: mid-batch logical_reserve failure in render_slots_alloc_range (render_slots.c:92)\n");
    fflush(stdout);
    RenderSlotTable t3;
    CHECK(render_slots_init(&t3, heap, 8, 2), "init t3");
    CHECK(render_slots_alloc(&t3, 0) == 0, "prime: id 0 -> slot 0, logical map covers 0..7");
    uint32_t batch[2] = { 1, 100 }; // id 1 fits the map; id 100 forces the failing growth
    g_failNextGrow = true;
    CHECK(render_slots_alloc_range(&t3, batch, 2) == ANO_RENDER_SLOT_UNMAPPED, "failed batch reports UNMAPPED");
    CHECK(g_failedGrows == 2, "trigger: injection fired");
    CHECK(t3.slotHighWater == 1, "high-water still below the batch region");

    // a failed batch must publish nothing 〜 holds only when the prefix is unwound
    CHECK(render_slots_resolve(&t3, 1) == ANO_RENDER_SLOT_UNMAPPED, "failed batch publishes no forward mapping for its prefix");
    CHECK(render_slots_render_id_of(&t3, 1) == ANO_RENDER_SLOT_UNMAPPED, "failed batch publishes no reverse mapping for its prefix");

    // aliasing: the next single create is handed slot 1 (highWater++) 〜 the stale prefix must not share it
    uint32_t s50 = render_slots_alloc(&t3, 50);
    CHECK(s50 == 1, "next single alloc extends the high-water to slot 1");
    uint32_t s1 = render_slots_resolve(&t3, 1);
    CHECK(s1 == ANO_RENDER_SLOT_UNMAPPED || s1 != s50, "stale prefix id must not alias the new owner's slot");

    // escalation: destroying the failed-batch id must not touch the live owner's slot
    render_slots_retire(&t3, 1, /*currentFrame*/ 0);
    CHECK(render_slots_render_id_of(&t3, s50) == 50, "retiring a failed-batch id must not strip the live owner's reverse mapping");
    uint32_t freed[4];
    CHECK(render_slots_collect_retired(&t3, /*currentFrame*/ 2, freed, 4) == 0, "retiring a failed-batch id must not feed a live slot to the free-list");
    uint32_t s60 = render_slots_alloc(&t3, 60);
    CHECK(s60 != s50, "a live slot must never be handed out twice");

    // fix-agnostic invariant: forward and reverse maps agree for every live id
    CHECK(render_slots_resolve(&t3, 50) == s50 && render_slots_render_id_of(&t3, s50) == 50, "live id 50 keeps a bijective mapping");
    render_slots_destroy(&t3);

    // future-fix invariant: every heap block the tables minted was discharged
    CHECK(g_liveBlocks == 0, "allocator ledger balances after destroy");

    if (failures) {
        printf("anotest_slotrangeguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_slotrangeguard: all passed\n");
    return 0;
}
