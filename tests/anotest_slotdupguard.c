/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the duplicate-CREATE mapping overwrite. apply.c:125 forwards cmd.render_id into
// render_slots_alloc with no mapped-check on either side of the seam, and the alloc contract's
// one invariant "render_id unmapped" (render_slots.h:66) is enforced by nobody: alloc stores
// blind (render_slots.c:79-:80), so a duplicate CREATE of a live id mints a second physical
// slot, overwrites the forward map, and leaves the old slot's reverse entry holding the id 〜
// the old slot is stranded live forever (not free-listed, not quarantined, unreachable by
// resolve; compact's peel loop at :181 never sees it) and render_slots_render_id_of(oldSlot)
// keeps answering the id (:120), so the pick readback (profiling.c:174) misattributes the ghost
// to a render_id the ECS may have retired and recycled (docs/BUGS.md, Render / Vulkan backend /
// Interface-level, apply.c:125). The light sibling on the same bridge guards exactly this shape
// (light_registry.c:89 refuses double-attach; apply.c:250 drops it).
// Harness: compiles the REAL render_slots.c TU with the heap tokens (mi_heap_malloc,
// mi_heap_realloc, mi_free) interposed onto libc; pure index bookkeeping, no GPU device, no
// loader. Controls prove fresh creates, distinct-id allocation, and the LEGAL retire -> collect
// -> re-create recycle of a seen-before id, so a reject-any-known-id fix cannot pass. The
// trigger allocs an id that is still mapped: under the bug a second slot is minted (CHECK on
// the returned slot), the forward/reverse bijection breaks (CHECK per live reverse entry), and
// after every id is retired, collected, and compacted the stranded slot pins slotHighWater and
// still answers its old id (CHECKs on the teardown state). A live-block ledger audits allocator
// balance as a future-fix invariant. Fails until the seam rejects (or idempotently resolves) a
// CREATE whose render_id is already mapped. Exit 0 == pass.

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

static int32_t g_liveBlocks; // allocator balance ledger, must return to 0

// in: heap (dummy token, ignored), size; out: live block. Ledger +1.
void *anotest_seam_heap_malloc(mi_heap_t *heap, size_t size)
{
    (void)heap;
    void *p = malloc(size);
    if (p) g_liveBlocks++;
    return p;
}

// in: heap (ignored), old block, new size; out: grown block, or NULL on real OOM.
void *anotest_seam_heap_realloc(mi_heap_t *heap, void *p, size_t newsize)
{
    (void)heap;
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

    // control: fresh creates map forward and reverse
    RenderSlotTable t;
    CHECK(render_slots_init(&t, heap, 8, 2), "init (maxSlots 8, fif 2)");
    CHECK(render_slots_alloc(&t, 5) == 0, "control: id 5 -> slot 0");
    CHECK(render_slots_resolve(&t, 5) == 0, "control: forward map holds");
    CHECK(render_slots_render_id_of(&t, 0) == 5, "control: reverse map holds");
    CHECK(render_slots_alloc(&t, 9) == 1, "control: distinct id 9 -> distinct slot 1");

    // control: the LEGAL recycle 〜 a retired-and-collected id re-creates fine
    // (REVENT_SLOT_RETIRED means the ECS may recycle the id, anoptic_render.h:556)
    render_slots_retire(&t, 9, 0);           // safeFrame = 0 + 2
    CHECK(render_slots_resolve(&t, 9) == ANO_RENDER_SLOT_UNMAPPED, "control: retire unmaps immediately");
    uint32_t collected[8];
    CHECK(render_slots_collect_retired(&t, 2, collected, 8) == 1, "control: quarantine elapses at safeFrame");
    CHECK(collected[0] == 9, "control: the retired id is reported");
    CHECK(render_slots_alloc(&t, 9) == 1, "control: seen-before id 9 re-creates into the recycled hole");
    CHECK(render_slots_resolve(&t, 9) == 1, "control: recycled mapping holds");

    // trigger: duplicate CREATE 〜 id 5 is still mapped to slot 0; apply.c:125 forwards such an id
    // raw and render_slots.c:79 overwrites the forward map while slot 0's reverse entry keeps 5
    printf("trigger: render_slots_alloc(5) while id 5 is live on slot 0 (apply.c:125 / render_slots.c:79)\n");
    fflush(stdout);
    uint32_t s2 = render_slots_alloc(&t, 5);
    CHECK(s2 == ANO_RENDER_SLOT_UNMAPPED || s2 == 0, "duplicate create must be rejected or resolve to the existing slot, not mint a second one");

    // invariant: every live reverse entry round-trips through the forward map
    for (uint32_t slot = 0; slot < t.slotHighWater; slot++) {
        uint32_t rid = render_slots_render_id_of(&t, slot);
        if (rid == ANO_RENDER_SLOT_UNMAPPED) continue;
        CHECK(render_slots_resolve(&t, rid) == slot, "forward/reverse bijection holds for every live slot");
    }

    // consequence: retire EVERY live id, run quarantine out, compact 〜 the table must empty;
    // under the bug slot 0 is stranded (never freed, never peeled) and still answers id 5
    render_slots_retire(&t, 5, 10);
    render_slots_retire(&t, 9, 10);
    uint32_t drained = render_slots_collect_retired(&t, 1000, collected, 8);
    CHECK(drained >= 2, "teardown: every retired id collects");
    render_slots_compact(&t);
    CHECK(t.slotHighWater == 0, "teardown: every physical slot reclaimed once every render_id is retired");
    CHECK(render_slots_render_id_of(&t, 0) == ANO_RENDER_SLOT_UNMAPPED, "teardown: no reverse entry survives its id's retirement 〜 a survivor misattributes picks (profiling.c:174)");
    render_slots_destroy(&t);

    // future-fix invariant: every heap block the table minted was discharged
    CHECK(g_liveBlocks == 0, "allocator ledger balances after destroy");

    if (failures) {
        printf("anotest_slotdupguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_slotdupguard: all passed\n");
    return 0;
}
