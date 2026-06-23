/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file render_slots.h
 * @brief Render-internal slot authority: logical render_id -> physical GPU slot,
 *        stable slots with holes, free-list, and frame-quarantined reuse.
 *
 * PRIVATE to the Vulkan backend (VK_BACKEND_INTEROP.md S4, S9). The logic world
 * never sees a GPU slot — it addresses renderables by render_id only — so this
 * header stays inside src/vulkan_backend/ and is never exposed through include/.
 *
 * Slots are STABLE and may contain holes: the cull pass already compacts visible
 * work, so a dead slot costs one skipped compute invocation and zero draw cost.
 * There is deliberately no swap-and-pop / defragmentation of the per-entity GPU
 * buffers here. The render_id -> slot indirection additionally lets the backend
 * relocate a slot internally (optional) without the logic world noticing.
 *
 * Owned and mutated by the Vulkan master thread only; no synchronization inside.
 */

#ifndef ANO_RENDER_SLOTS_H
#define ANO_RENDER_SLOTS_H

#include <stdint.h>
#include <stdbool.h>
#include <mimalloc.h>   // mi_heap_t: table storage lives in a caller-provided heap

#define ANO_RENDER_SLOT_UNMAPPED 0xFFFFFFFFu

// A slot awaiting frame-in-flight retirement before its index may be reused.
typedef struct RenderSlotQuarantine
{
    uint32_t slot;       // physical GPU slot held out of the free-list
    uint32_t render_id;  // logical name to report back via REVENT_SLOT_RETIRED
    uint64_t safeFrame;  // global frame counter at/after which reuse is safe
} RenderSlotQuarantine;

typedef struct RenderSlotTable
{
    mi_heap_t            *heap;   // backs all table storage; not owned

    // render_id -> gpu slot (ANO_RENDER_SLOT_UNMAPPED == not mapped). Grown on
    // demand; logical ids are dense (ECS recycles them) so this stays a flat
    // indexed map, no hashing.
    uint32_t             *logicalToSlot;
    uint32_t              logicalCapacity;

    // Free-list stack of reusable physical slots (holes below the high-water mark).
    uint32_t             *freeSlots;
    uint32_t              freeCount;
    uint32_t              freeCapacity;

    // Physical slot space. slotHighWater is the cull/animation dispatch bound;
    // dead slots within [0, slotHighWater) self-skip in the shaders.
    uint32_t              slotHighWater;
    uint32_t              slotCapacity;

    // Slots pending reuse until their referencing frames retire.
    RenderSlotQuarantine *quarantine;
    uint32_t              quarantineCount;
    uint32_t              quarantineCapacity;

    uint32_t              framesInFlight; // == MAX_FRAMES_IN_FLIGHT
} RenderSlotTable;

// in:  table, heap (backs all storage), maxSlots (physical slot ceiling, == GPU
//      per-entity buffer capacity), framesInFlight (>= 1)
// out: true on success; false on bad args / allocation failure
// inv: zero-initializes counts; grows logical map / free-list / quarantine lazily.
bool render_slots_init(RenderSlotTable *table, mi_heap_t *heap, uint32_t maxSlots, uint32_t framesInFlight);

// Releases all slot-table storage.
void render_slots_destroy(RenderSlotTable *table);

// Allocates one physical slot for `render_id` (pops the free-list, else extends
// the high-water mark) and records the mapping.
// out: the assigned slot, or ANO_RENDER_SLOT_UNMAPPED on growth failure.
// inv: `render_id` must not already be mapped.
uint32_t render_slots_alloc(RenderSlotTable *table, uint32_t render_id);

// Allocates a CONTIGUOUS range of `count` slots for `render_ids[0..count)` (mass
// spawn, RCMD_BULK_CREATE). Contiguity lets the caller block-write the GPU buffers.
// out: the base slot of the range, or ANO_RENDER_SLOT_UNMAPPED on failure.
uint32_t render_slots_alloc_range(RenderSlotTable *table, const uint32_t *render_ids, uint32_t count);

// Raises the physical slot ceiling to `newCapacity` (no-op if already >=). The
// caller grows the backing GPU buffers to match BEFORE calling this; this only
// lifts the gate that `_alloc` / `_alloc_range` enforce. Never shrinks.
void render_slots_set_capacity(RenderSlotTable *table, uint32_t newCapacity);

// out: the physical slot mapped to `render_id`, or ANO_RENDER_SLOT_UNMAPPED.
uint32_t render_slots_resolve(const RenderSlotTable *table, uint32_t render_id);

// Begins retirement of `render_id`'s slot (after its dead-mark has propagated to
// all frame buffers): unmaps the id and quarantines the slot with
// safeFrame = currentFrame + framesInFlight. Does NOT free the slot yet.
void render_slots_retire(RenderSlotTable *table, uint32_t render_id, uint64_t currentFrame);

// Returns to the free-list every quarantined slot whose safeFrame <= currentFrame,
// writing each freed slot's render_id into `out_render_ids` (for REVENT_SLOT_RETIRED
// emission), up to `max` entries.
// out: the number of slots freed this call (== number of render_ids written).
// inv: if more than `max` slots are ready, the remainder stay quarantined for a
//      later call (no silent drop).
uint32_t render_slots_collect_retired(RenderSlotTable *table, uint64_t currentFrame,
                                       uint32_t *out_render_ids, uint32_t max);

// Lowers slotHighWater past any trailing run of free slots, shrinking the cull/animation
// dispatch bound (entityCount) without VRAM change. Only slots already on the free-list are
// peeled — those are quarantine-expired (no in-flight reader) and unmapped — so NO live slot
// moves and slot indices stay stable (consistent with the no-defrag design). Fragmentation
// below a live slot is left in place; a contiguous bulk despawn at the top is reclaimed
// wholesale. Reaches slotHighWater == 0 when every slot is free (epoch reset). Costs an
// O(freeCount log freeCount) sort, so call it only when the free-list just changed.
// out: number of slots reclaimed from the dispatch bound (0 if the top slot is live).
uint32_t render_slots_compact(RenderSlotTable *table);

#endif // ANO_RENDER_SLOTS_H
