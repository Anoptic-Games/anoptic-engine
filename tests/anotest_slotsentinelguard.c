/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the sentinel-id wrap in logical_reserve. render_slots.c:35 grows the logical map
// to need = render_id + 1u, so the module's own sentinel 0xFFFFFFFF (ANO_RENDER_SLOT_UNMAPPED,
// render_slots.h:16) wraps need to 0, ensure_cap's need <= *cap arm (:19) reports success with
// nothing allocated, the UNMAPPED-init loop runs zero times, and render_slots_alloc consumes a
// physical slot then stores it at logicalToSlot[0xFFFFFFFF] (:79) 〜 a wild write ~16 GiB past
// the map (or from a NULL map on a fresh table) on the render master thread (docs/BUGS.md,
// Render / Vulkan backend / Interface-level, render_slots.c:35). Nothing upstream excludes the
// sentinel: render_id is the producer's namespace (anoptic_render.h:399), ano_render_submit is
// a bare ring push (ano_render_bridge.c:102), and apply.c:125/:167 forward the id raw; the
// resolve twin guards this exact domain edge (render_slots.c:102), alloc does not.
// Harness: compiles the REAL render_slots.c TU with the heap tokens (mi_heap_malloc,
// mi_heap_realloc, mi_free) interposed onto libc; pure index bookkeeping, no GPU device, no
// loader. Controls prove small, grown, and large LEGAL ids all allocate and resolve, and that
// resolve already rejects the sentinel, so a reject-everything fix cannot pass. The trigger
// calls render_slots_alloc(0xFFFFFFFF): under the bug the wild store crashes (the crash IS the
// failure signal, controls print+flush first); if the store lands in mapped memory the CHECKs
// fail on the returned slot and the consumed high-water. A live-block ledger audits allocator
// balance as a future-fix invariant. Fails until the sentinel is rejected at the alloc seam.
// Exit 0 == pass.

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

    // control: legal ids allocate, resolve, and reverse-map across a growth
    RenderSlotTable t;
    CHECK(render_slots_init(&t, heap, 8, 2), "init (maxSlots 8, fif 2)");
    CHECK(render_slots_alloc(&t, 3) == 0, "control: id 3 -> slot 0");
    CHECK(render_slots_resolve(&t, 3) == 0, "control: forward map holds");
    CHECK(render_slots_render_id_of(&t, 0) == 3, "control: reverse map holds");
    CHECK(t.logicalCapacity >= 4, "control: logical map covers the id it reserved");
    CHECK(render_slots_resolve(&t, 7) == ANO_RENDER_SLOT_UNMAPPED, "control: fresh map entries init UNMAPPED");
    CHECK(render_slots_alloc(&t, 9) == 1, "control: id 9 grows the map and takes slot 1");
    CHECK(render_slots_resolve(&t, 9) == 1, "control: forward map holds across growth");

    // control: a LARGE legal id is fine 〜 the map is grown on demand; only the sentinel is out of domain
    CHECK(render_slots_alloc(&t, 4096) == 2, "control: large legal id 4096 -> slot 2");
    CHECK(render_slots_resolve(&t, 4096) == 2, "control: large id resolves");
    CHECK(t.logicalCapacity >= 4097, "control: growth covered the large id");

    // control: resolve already rejects the sentinel at this exact domain edge (render_slots.c:102)
    CHECK(render_slots_resolve(&t, ANO_RENDER_SLOT_UNMAPPED) == ANO_RENDER_SLOT_UNMAPPED, "control: resolve guards the sentinel");

    // trigger: alloc the sentinel id 〜 logical_reserve's render_id + 1u wraps to 0 (render_slots.c:35),
    // ensure_cap(need 0) succeeds without growing, and :79 stores through logicalToSlot[0xFFFFFFFF]
    printf("trigger: render_slots_alloc(ANO_RENDER_SLOT_UNMAPPED) 〜 a crash here IS the bug (render_slots.c:35)\n");
    fflush(stdout);
    uint32_t s = render_slots_alloc(&t, ANO_RENDER_SLOT_UNMAPPED);
    CHECK(s == ANO_RENDER_SLOT_UNMAPPED, "sentinel id must be rejected, not allocated");
    CHECK(t.slotHighWater == 3, "sentinel alloc must not consume a physical slot");

    // the table must remain serviceable: the next legal id takes the next high-water slot
    CHECK(render_slots_alloc(&t, 20) == 3, "next legal alloc continues at slot 3");
    CHECK(render_slots_render_id_of(&t, 3) == 20, "slot 3 owned by the post-trigger alloc");
    render_slots_destroy(&t);

    // future-fix invariant: every heap block the table minted was discharged
    CHECK(g_liveBlocks == 0, "allocator ledger balances after destroy");

    if (failures) {
        printf("anotest_slotsentinelguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_slotsentinelguard: all passed\n");
    return 0;
}
