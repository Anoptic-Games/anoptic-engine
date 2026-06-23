/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Render-internal slot authority. Contract + design: render_slots.h and
 * docs/artifacts/VK_BACKEND_INTEROP.md S4/S9. Pure index bookkeeping, no Vulkan:
 * maps logical render_ids to stable physical GPU slots, recycles freed slots
 * through a frame-gated quarantine. Owned and called by the Vulkan master thread
 * only — no internal synchronization. */

#include "vulkan_backend/render_slots.h"

#include <string.h>
#include <stdlib.h>   // qsort, for the trailing-run peel in render_slots_compact

// Geometric growth of a plain element array. Leaves *arr/*cap untouched on OOM.
static bool ensure_cap(mi_heap_t *heap, void **arr, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap) return true;
    uint32_t newcap = *cap ? *cap : 8u;
    while (newcap < need) newcap *= 2u;
    void *p = mi_heap_realloc(heap, *arr, (size_t)newcap * elem);
    if (!p) return false;
    *arr = p;
    *cap = newcap;
    return true;
}

// Grows the logical map to cover render_id, initializing new entries to UNMAPPED.
static bool logical_reserve(RenderSlotTable *t, uint32_t render_id)
{
    if (render_id < t->logicalCapacity) return true;
    uint32_t old = t->logicalCapacity;
    if (!ensure_cap(t->heap, (void **)&t->logicalToSlot, &t->logicalCapacity,
                    render_id + 1u, sizeof(uint32_t)))
        return false;
    for (uint32_t i = old; i < t->logicalCapacity; i++)
        t->logicalToSlot[i] = ANO_RENDER_SLOT_UNMAPPED;
    return true;
}

bool render_slots_init(RenderSlotTable *table, mi_heap_t *heap, uint32_t maxSlots, uint32_t framesInFlight)
{
    if (!table || !heap || maxSlots == 0u || framesInFlight == 0u) return false;
    memset(table, 0, sizeof(*table));
    table->heap           = heap;
    table->slotCapacity   = maxSlots;
    table->framesInFlight = framesInFlight;
    return true;
}

void render_slots_destroy(RenderSlotTable *table)
{
    if (!table) return;
    if (table->logicalToSlot) mi_free(table->logicalToSlot);
    if (table->freeSlots)     mi_free(table->freeSlots);
    if (table->quarantine)    mi_free(table->quarantine);
    memset(table, 0, sizeof(*table));
}

uint32_t render_slots_alloc(RenderSlotTable *t, uint32_t render_id)
{
    if (!logical_reserve(t, render_id)) return ANO_RENDER_SLOT_UNMAPPED;

    uint32_t slot;
    if (t->freeCount > 0u) {
        slot = t->freeSlots[--t->freeCount];          // reuse a retired hole
    } else if (t->slotHighWater < t->slotCapacity) {
        slot = t->slotHighWater++;                    // extend the slot space
    } else {
        return ANO_RENDER_SLOT_UNMAPPED;              // at capacity
    }
    t->logicalToSlot[render_id] = slot;
    return slot;
}

uint32_t render_slots_alloc_range(RenderSlotTable *t, const uint32_t *render_ids, uint32_t count)
{
    if (count == 0u) return ANO_RENDER_SLOT_UNMAPPED;
    // A contiguous range only comes cleanly from the high-water region (free-list
    // holes are not contiguous), so bulk spawn extends the high-water mark.
    if ((uint64_t)t->slotHighWater + count > t->slotCapacity) return ANO_RENDER_SLOT_UNMAPPED;

    uint32_t base = t->slotHighWater;
    for (uint32_t i = 0; i < count; i++) {
        if (!logical_reserve(t, render_ids[i])) return ANO_RENDER_SLOT_UNMAPPED;
        t->logicalToSlot[render_ids[i]] = base + i;
    }
    t->slotHighWater = base + count;
    return base;
}

uint32_t render_slots_resolve(const RenderSlotTable *t, uint32_t render_id)
{
    if (render_id >= t->logicalCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    return t->logicalToSlot[render_id];
}

void render_slots_set_capacity(RenderSlotTable *t, uint32_t newCapacity)
{
    if (newCapacity > t->slotCapacity) t->slotCapacity = newCapacity;
}

void render_slots_retire(RenderSlotTable *t, uint32_t render_id, uint64_t currentFrame)
{
    uint32_t slot = render_slots_resolve(t, render_id);
    if (slot == ANO_RENDER_SLOT_UNMAPPED) return;

    t->logicalToSlot[render_id] = ANO_RENDER_SLOT_UNMAPPED;   // unmap immediately
    if (!ensure_cap(t->heap, (void **)&t->quarantine, &t->quarantineCapacity,
                    t->quarantineCount + 1u, sizeof(RenderSlotQuarantine))) {
        // Quarantine OOM: leak the slot rather than risk reuse-while-in-flight.
        return;
    }
    t->quarantine[t->quarantineCount++] = (RenderSlotQuarantine){
        .slot = slot, .render_id = render_id,
        .safeFrame = currentFrame + t->framesInFlight,
    };
}

uint32_t render_slots_collect_retired(RenderSlotTable *t, uint64_t currentFrame,
                                       uint32_t *out_render_ids, uint32_t max)
{
    uint32_t out_n = 0u;
    uint32_t i = 0u;
    while (i < t->quarantineCount) {
        RenderSlotQuarantine *q = &t->quarantine[i];
        if (q->safeFrame > currentFrame) { i++; continue; }   // still in flight
        if (out_n >= max) { i++; continue; }                  // ready but no room to report; keep it

        // Free + report together (never free a slot we can't report retired).
        if (!ensure_cap(t->heap, (void **)&t->freeSlots, &t->freeCapacity,
                        t->freeCount + 1u, sizeof(uint32_t))) {
            i++; continue;                                    // free-list OOM: keep quarantined
        }
        t->freeSlots[t->freeCount++] = q->slot;
        if (out_render_ids) out_render_ids[out_n] = q->render_id;
        out_n++;

        *q = t->quarantine[--t->quarantineCount];             // swap-and-pop; recheck this index
    }
    return out_n;
}

// Ascending compare for the free-slot sort below. Subtraction would overflow on uint32_t.
static int cmp_u32_asc(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

uint32_t render_slots_compact(RenderSlotTable *t)
{
    if (!t || t->freeCount == 0u) return 0u;

    // Sort ascending so the trailing contiguous free run is a suffix; the non-trailing
    // holes (below some live slot) remain as a still-valid, still-sorted prefix.
    qsort(t->freeSlots, t->freeCount, sizeof(uint32_t), cmp_u32_asc);

    uint32_t before = t->slotHighWater;
    // Peel each top slot that is free. The freeCount>0 guard makes the highWater==0
    // epoch-reset terminus safe (no freeSlots[-1], no slotHighWater-1 underflow read).
    while (t->freeCount > 0u && t->freeSlots[t->freeCount - 1u] == t->slotHighWater - 1u) {
        t->freeCount--;
        t->slotHighWater--;
    }
    return before - t->slotHighWater;
}
