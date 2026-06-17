/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for the render-side slot authority (render_slots.h): stable slot
 * assignment, logical->slot resolution, contiguous bulk ranges, capacity limits,
 * and the frame-gated quarantine -> recycle -> retirement-report path. Pure
 * logic, no Vulkan device required. Exit 0 == pass. */

#include <stdio.h>
#include <mimalloc.h>
#include "vulkan_backend/render_slots.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static void test_bulk_range(mi_heap_t *heap)
{
    RenderSlotTable t;
    CHECK(render_slots_init(&t, heap, 8, 3), "init (maxSlots 8, fif 3)");

    uint32_t ids[3] = { 100, 101, 102 };
    uint32_t base = render_slots_alloc_range(&t, ids, 3);
    CHECK(base == 0, "bulk range base == 0");
    CHECK(render_slots_resolve(&t, 101) == 1, "range resolve 101 -> 1");
    CHECK(render_slots_resolve(&t, 102) == 2, "range resolve 102 -> 2");
    CHECK(t.slotHighWater == 3, "high-water == 3 after range");

    CHECK(render_slots_alloc(&t, 103) == 3, "single alloc continues at 3");

    render_slots_destroy(&t);
}

static void test_lifecycle(mi_heap_t *heap)
{
    RenderSlotTable t;
    CHECK(render_slots_init(&t, heap, 4, 2), "init (maxSlots 4, fif 2)");

    CHECK(render_slots_alloc(&t, 10) == 0, "alloc 10 -> slot 0");
    CHECK(render_slots_alloc(&t, 11) == 1, "alloc 11 -> slot 1");
    CHECK(render_slots_alloc(&t, 12) == 2, "alloc 12 -> slot 2");
    CHECK(render_slots_resolve(&t, 11) == 1, "resolve 11 -> 1");
    CHECK(render_slots_resolve(&t, 99) == ANO_RENDER_SLOT_UNMAPPED, "resolve unknown -> UNMAPPED");

    // --- retire + frame-gated quarantine ---
    render_slots_retire(&t, 11, /*currentFrame*/ 5);
    CHECK(render_slots_resolve(&t, 11) == ANO_RENDER_SLOT_UNMAPPED, "retired id unmapped immediately");

    uint32_t freed[4];
    CHECK(render_slots_collect_retired(&t, 5, freed, 4) == 0, "nothing retired before safeFrame");
    CHECK(render_slots_collect_retired(&t, 6, freed, 4) == 0, "still in flight at frame 6");
    uint32_t n = render_slots_collect_retired(&t, 7, freed, 4); // safeFrame = 5 + 2 = 7
    CHECK(n == 1 && freed[0] == 11, "slot retired at safeFrame, reports render_id 11");

    // --- freed slot recycles before extending the high-water mark ---
    CHECK(render_slots_alloc(&t, 20) == 1, "alloc 20 reuses freed slot 1");
    CHECK(render_slots_alloc(&t, 21) == 3, "alloc 21 -> slot 3 (high-water)");
    CHECK(t.slotHighWater == 4, "high-water reached capacity");
    CHECK(render_slots_alloc(&t, 22) == ANO_RENDER_SLOT_UNMAPPED, "alloc at capacity -> UNMAPPED");
    uint32_t one[1] = { 30 };
    CHECK(render_slots_alloc_range(&t, one, 1) == ANO_RENDER_SLOT_UNMAPPED, "range at capacity -> UNMAPPED");

    // --- collect honors `max`: ready entries beyond the cap stay quarantined ---
    render_slots_retire(&t, 20, 10); // slot 1, safeFrame 12
    render_slots_retire(&t, 21, 10); // slot 3, safeFrame 12
    CHECK(render_slots_collect_retired(&t, 12, freed, 1) == 1, "collect caps at max=1");
    CHECK(render_slots_collect_retired(&t, 12, freed, 4) == 1, "remaining ready entry collected next call");
    CHECK(render_slots_collect_retired(&t, 12, freed, 4) == 0, "quarantine drained");

    render_slots_destroy(&t);
}

int main(void)
{
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "heap creation");

    test_bulk_range(heap);
    test_lifecycle(heap);

    mi_heap_destroy(heap);

    if (failures == 0) { printf("anotest_render_slots: all checks passed\n"); return 0; }
    printf("anotest_render_slots: %d check(s) failed\n", failures);
    return 1;
}
