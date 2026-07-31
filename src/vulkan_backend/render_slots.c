/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Render-internal slot authority. Contract: render_slots.h.
 * Pure index bookkeeping, no Vulkan: maps logical render_ids to stable physical
 * GPU slots, recycles freed slots through a frame-gated quarantine. Owned and
 * called by the Vulkan master thread only. No internal synchronization. */

#include "vulkan_backend/render_slots.h"

#include <string.h>
#include <stdlib.h>   // qsort

// Geometric growth of a plain element array. Leaves *arr/*cap untouched on OOM.
static bool ensure_cap(mi_heap_t *heap, void **arr, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap) return true;
    uint32_t newcap = *cap ? *cap : 8u;
    while (newcap < need) {
        if (newcap > UINT32_MAX / 2u) { newcap = need; break; }   // last doubling would wrap
        newcap *= 2u;
    }
    void *p = mi_heap_realloc(heap, *arr, (size_t)newcap * elem);
    if (!p) return false;
    *arr = p;
    *cap = newcap;
    return true;
}

// Grows the logical map to cover render_id, initializing new entries to UNMAPPED.
// in: table, render_id (any value; the UNMAPPED sentinel is out of domain)
// out: false on the sentinel or on OOM, with *arr/*cap untouched
static bool logical_reserve(RenderSlotTable *t, uint32_t render_id)
{
    if (render_id == ANO_RENDER_SLOT_UNMAPPED) return false;   // render_id + 1u would wrap to 0
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

    // Reverse map sized to the physical slot ceiling, all slots free initially.
    table->slotToLogical = static_cast<uint32_t*>(mi_heap_malloc(heap, (size_t)maxSlots * sizeof(uint32_t)));
    if (!table->slotToLogical) return false;
    for (uint32_t i = 0; i < maxSlots; i++) table->slotToLogical[i] = ANO_RENDER_SLOT_UNMAPPED;
    return true;
}

void render_slots_destroy(RenderSlotTable *table)
{
    if (!table) return;
    if (table->logicalToSlot) mi_free(table->logicalToSlot);
    if (table->slotToLogical) mi_free(table->slotToLogical);
    if (table->freeSlots)     mi_free(table->freeSlots);
    if (table->quarantine)    mi_free(table->quarantine);
    memset(table, 0, sizeof(*table));
}

uint32_t render_slots_alloc(RenderSlotTable *t, uint32_t render_id)
{
    if (!logical_reserve(t, render_id)) return ANO_RENDER_SLOT_UNMAPPED;
    if (t->logicalToSlot[render_id] != ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED; // already live

    uint32_t slot;
    if (t->freeCount > 0u) {
        slot = t->freeSlots[--t->freeCount];          // reuse a retired hole
    } else if (t->slotHighWater < t->slotCapacity) {
        slot = t->slotHighWater++;                    // extend the slot space
    } else {
        return ANO_RENDER_SLOT_UNMAPPED;              // at capacity
    }
    t->logicalToSlot[render_id] = slot;
    t->slotToLogical[slot] = render_id;
    return slot;
}

// Unpublishes the first n batch elements. slotHighWater unchanged.
static void alloc_range_rollback(RenderSlotTable *t, const uint32_t *render_ids,
                                 uint32_t base, uint32_t n)
{
    for (uint32_t j = 0; j < n; j++) {
        t->logicalToSlot[render_ids[j]] = ANO_RENDER_SLOT_UNMAPPED;
        t->slotToLogical[base + j]      = ANO_RENDER_SLOT_UNMAPPED;
    }
}

// Contiguous slot range for a bulk spawn from the high-water region.
// in: table, render_ids (count, each unmapped and distinct; UNMAPPED out of domain), count > 0
// out: base slot, or UNMAPPED on sentinel/mapped/duplicate/capacity/OOM
// inv: failures leave mappings untouched; mid-batch refusal rolls back the prefix
//      slotHighWater advances only once the batch is whole
uint32_t render_slots_alloc_range(RenderSlotTable *t, const uint32_t *render_ids, uint32_t count)
{
    if (count == 0u) return ANO_RENDER_SLOT_UNMAPPED;
    if ((uint64_t)t->slotHighWater + count > t->slotCapacity) return ANO_RENDER_SLOT_UNMAPPED;

    uint32_t base = t->slotHighWater;
    for (uint32_t i = 0; i < count; i++) {
        // Sentinel/OOM, or id already mapped (live or intra-batch duplicate).
        if (!logical_reserve(t, render_ids[i]) ||
            t->logicalToSlot[render_ids[i]] != ANO_RENDER_SLOT_UNMAPPED) {
            alloc_range_rollback(t, render_ids, base, i);
            return ANO_RENDER_SLOT_UNMAPPED;
        }
        t->logicalToSlot[render_ids[i]] = base + i;
        t->slotToLogical[base + i] = render_ids[i];
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
    if (newCapacity <= t->slotCapacity) return;
    // Grow the reverse map alongside the slot ceiling. On OOM keep the old ceiling.
    uint32_t *p = static_cast<uint32_t*>(mi_heap_realloc(t->heap, t->slotToLogical, (size_t)newCapacity * sizeof(uint32_t)));
    if (!p) return;
    for (uint32_t i = t->slotCapacity; i < newCapacity; i++) p[i] = ANO_RENDER_SLOT_UNMAPPED;
    t->slotToLogical = p;
    t->slotCapacity = newCapacity;
}

uint32_t render_slots_render_id_of(const RenderSlotTable *t, uint32_t slot)
{
    if (!t || slot >= t->slotCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    return t->slotToLogical[slot];
}

void render_slots_retire(RenderSlotTable *t, uint32_t render_id, uint64_t currentFrame)
{
    uint32_t slot = render_slots_resolve(t, render_id);
    if (slot == ANO_RENDER_SLOT_UNMAPPED) return;

    t->logicalToSlot[render_id] = ANO_RENDER_SLOT_UNMAPPED;   // unmap immediately
    t->slotToLogical[slot] = ANO_RENDER_SLOT_UNMAPPED;        // reverse map: slot now free for picking
    if (!ensure_cap(t->heap, (void **)&t->quarantine, &t->quarantineCapacity,
                    t->quarantineCount + 1u, sizeof(RenderSlotQuarantine))) {
        // Quarantine OOM: leak the slot.
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
        if (out_n >= max) { i++; continue; }                  // ready but no room to report, keep it

        // Free and report together.
        if (!ensure_cap(t->heap, (void **)&t->freeSlots, &t->freeCapacity,
                        t->freeCount + 1u, sizeof(uint32_t))) {
            i++; continue;                                    // free-list OOM: keep quarantined
        }
        t->freeSlots[t->freeCount++] = q->slot;
        if (out_render_ids) out_render_ids[out_n] = q->render_id;
        out_n++;

        *q = t->quarantine[--t->quarantineCount];             // swap-and-pop, recheck this index
    }
    return out_n;
}

// Ascending compare for the free-slot sort.
static int cmp_u32_asc(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

uint32_t render_slots_compact(RenderSlotTable *t)
{
    if (!t || t->freeCount == 0u) return 0u;

    // Ascending: trailing free run is a suffix.
    qsort(t->freeSlots, t->freeCount, sizeof(uint32_t), cmp_u32_asc);

    uint32_t before = t->slotHighWater;
    // Peel each top slot that is free.
    while (t->freeCount > 0u && t->freeSlots[t->freeCount - 1u] == t->slotHighWater - 1u) {
        t->freeCount--;
        t->slotHighWater--;
    }
    return before - t->slotHighWater;
}
