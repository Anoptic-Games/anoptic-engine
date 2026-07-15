/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// render_id -> GPU slot: stable holes, free-list, frame-quarantined reuse.
// PRIVATE to vulkan_backend. Render-thread only.

#ifndef ANO_RENDER_SLOTS_H
#define ANO_RENDER_SLOTS_H

#include <stdint.h>
#include <stdbool.h>
#include <mimalloc.h>   // mi_heap_t: table storage lives in a caller-provided heap

#define ANO_RENDER_SLOT_UNMAPPED 0xFFFFFFFFu

/* Slot Table Types */

// A slot awaiting frame-in-flight retirement before its index may be reused.
typedef struct RenderSlotQuarantine
{
    uint32_t slot;       // physical GPU slot held out of the free-list
    uint32_t render_id;  // logical name to report back via REVENT_SLOT_RETIRED
    uint64_t safeFrame;  // global frame counter at/after which reuse is safe
} RenderSlotQuarantine;

typedef struct RenderSlotTable
{
    mi_heap_t            *heap;   // backs all table storage, not owned

    // render_id -> gpu slot (ANO_RENDER_SLOT_UNMAPPED == unmapped). Flat, grown on demand.
    uint32_t             *logicalToSlot;
    uint32_t              logicalCapacity;

    // slot -> render_id (inverse). Sized slotCapacity. Picking maps slot -> render_id.
    uint32_t             *slotToLogical;

    // Free-list of reusable physical slots (holes below high-water).
    uint32_t             *freeSlots;
    uint32_t              freeCount;
    uint32_t              freeCapacity;

    // slotHighWater = cull/animation dispatch bound. Dead slots in range self-skip.
    uint32_t              slotHighWater;
    uint32_t              slotCapacity;

    // Slots pending reuse until referencing frames retire.
    RenderSlotQuarantine *quarantine;
    uint32_t              quarantineCount;
    uint32_t              quarantineCapacity;

    uint32_t              framesInFlight; // == MAX_FRAMES_IN_FLIGHT
} RenderSlotTable;

/* API */

// in: table, heap, maxSlots (== GPU per-entity capacity), framesInFlight (>= 1)
// out: true on success
// inv: counts zeroed; maps grow lazily
bool render_slots_init(RenderSlotTable *table, mi_heap_t *heap, uint32_t maxSlots, uint32_t framesInFlight);

void render_slots_destroy(RenderSlotTable *table);

// Alloc one slot (free-list else high-water). out: slot or UNMAPPED. inv: render_id unmapped.
uint32_t render_slots_alloc(RenderSlotTable *table, uint32_t render_id);

// Contiguous range for RCMD_BULK_CREATE. out: base slot or UNMAPPED.
uint32_t render_slots_alloc_range(RenderSlotTable *table, const uint32_t *render_ids, uint32_t count);

// Raise slot ceiling (caller grows GPU buffers first). Never shrinks.
void render_slots_set_capacity(RenderSlotTable *table, uint32_t newCapacity);

// out: slot for render_id, or UNMAPPED.
uint32_t render_slots_resolve(const RenderSlotTable *table, uint32_t render_id);

// out: render_id for slot, or UNMAPPED if free/quarantined/OOR.
uint32_t render_slots_render_id_of(const RenderSlotTable *table, uint32_t slot);

// Unmap + quarantine (safeFrame = currentFrame + framesInFlight). Does not free yet.
void render_slots_retire(RenderSlotTable *table, uint32_t render_id, uint64_t currentFrame);

// Free quarantined slots with safeFrame <= currentFrame into out_render_ids (cap max).
// inv: overflow stays quarantined (no silent drop).
uint32_t render_slots_collect_retired(RenderSlotTable *table, uint64_t currentFrame,
                                       uint32_t *out_render_ids, uint32_t max);

// Peel trailing free slots from slotHighWater. Live slots stay put. out: reclaimed count.
uint32_t render_slots_compact(RenderSlotTable *table);

#endif // ANO_RENDER_SLOTS_H
