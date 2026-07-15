/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic<->render transport: SPSC rings, bridge struct, DisplayState.
 * Private to src/. Never include from include/.
 * Public contract: include/anoptic_render.h.
 *
 *   logic master  --RenderCommand-->  render master   (commands ring)
 *   render master --RenderEvent---->  logic master    (events ring)
 *
 * Both directions: bounded SPSC (acquire peer, release self, no CAS).
 * render_id is the stable logical name; continuous GPU motion goes once via RFIELD_ANIM. */

#ifndef ANO_RENDER_BRIDGE_INTERNAL_H
#define ANO_RENDER_BRIDGE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <mimalloc.h>
#include <anoptic_memory.h> // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <anoptic_math.h>
#include <anoptic_render.h> // command/event protocol + opaque AnoRenderBridge

// Event protocol / RenderSnapshot / AnoViewState are public in anoptic_render.h. Transport only here.


/* DisplayState */

// Dirty bits from the parallel update stage. Graphics-extract consumes them.
typedef enum RenderDirtyBits
{
    RENDER_DIRTY_SPAWN    = 1 << 0, // first time renderable        -> RCMD_CREATE
    RENDER_DIRTY_TELEPORT = 1 << 1, // discontinuous pose change    -> RFIELD_TRANSFORM (NOT continuous motion)
    RENDER_DIRTY_MESH_MAT = 1 << 2, // mesh/material swap           -> RFIELD_MESH_MAT
    RENDER_DIRTY_ANIM     = 1 << 3, // GPU motion parameters changed -> RFIELD_ANIM
    RENDER_DIRTY_LIGHT    = 1 << 4, // light parameters changed     -> RFIELD_LIGHT
    RENDER_DIRTY_DESTROY  = 1 << 5, // renderable should be removed  -> RCMD_DESTROY
    RENDER_DIRTY_USERDATA = 1 << 6, // instance channel changed     -> RFIELD_USERDATA
} RenderDirtyBits;

// ECS component: discrete render transitions + name. GPU motion via `motion` once (RFIELD_ANIM).
typedef struct DisplayState
{
    uint32_t render_id;          // stable logical name while renderable
    mat4     transform;          // base pose; payload for SPAWN / TELEPORT
    AnoMotionDescriptor motion;  // GPU motion descriptor (type + params); ANO_MOTION_STATIC for none
    uint32_t mesh_index;         // geometry pool index, or ANO_RENDER_NO_MESH
    uint32_t material_index;     // material palette index
    uint32_t light_index;        // ANO_RENDER_NO_LIGHT if not a light
    AnoInstanceData instance_data; // packed per-entity channel (tint/flags/scalars); zero == inert
    uint32_t dirty;              // RenderDirtyBits accumulated this tick
} DisplayState;


/* SPSC Ring */

// Bounded SPSC over fixed-size POD. Capacity power of two (mask wrap).
// Producer owns tail, consumer owns head; acquire peer, release self (own cursor relaxed).
// head/tail on separate ANO_THREAD_LINE regions; ring owner must match that align.
typedef struct AnoSpscRing
{
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail; // producer-owned: next write index
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head; // consumer-owned: next read index
    _Alignas(ANO_THREAD_LINE) uint32_t mask;         // capacity - 1 (immutable after init)
    uint32_t                          stride;       // element size in bytes
    uint8_t                          *buffer;       // capacity * stride bytes
} AnoSpscRing;

// in:  ring, heap, capacity_pow2 (rounded up to pow2, >= 2), stride (> 0)
// out: true on success; false on bad args or alloc failure
bool ano_spsc_init(AnoSpscRing *ring, mi_heap_t *heap, uint32_t capacity_pow2, uint32_t stride);

// Releases the ring buffer. Does not release the backing heap.
void ano_spsc_destroy(AnoSpscRing *ring);

// PRODUCER only. Copies `stride` bytes from `elem`. false if full.
static inline bool ano_spsc_push(AnoSpscRing *ring, const void *elem)
{
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if ((tail - head) > ring->mask) // (tail - head) == capacity means full
        return false;
    uint8_t *slot = ring->buffer + (size_t)(tail & ring->mask) * ring->stride;
    for (uint32_t i = 0; i < ring->stride; ++i)
        slot[i] = ((const uint8_t *)elem)[i];
    atomic_store_explicit(&ring->tail, tail + 1u, memory_order_release);
    return true;
}

// CONSUMER only. Copies next element into `out` (>= stride bytes). false if empty.
static inline bool ano_spsc_pop(AnoSpscRing *ring, void *out)
{
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head == tail) // empty
        return false;
    const uint8_t *slot = ring->buffer + (size_t)(head & ring->mask) * ring->stride;
    for (uint32_t i = 0; i < ring->stride; ++i)
        ((uint8_t *)out)[i] = slot[i];
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return true;
}


/* Seqlock */

// Latest-wins epoch. Even version == stable, odd == mid-write; version 0 == unpublished.
// One producer per version; readers retry if version moved across the copy.
static inline void ano_seqpub_store(void *value, _Atomic uint64_t *version, const void *v, size_t stride)
{
    uint64_t s = atomic_load_explicit(version, memory_order_relaxed); // producer-owned version
    atomic_store_explicit(version, s + 1u, memory_order_relaxed);     // odd marker
    atomic_thread_fence(memory_order_release);                        // odd before value writes
    for (size_t i = 0; i < stride; ++i) ((uint8_t *)value)[i] = ((const uint8_t *)v)[i];
    atomic_store_explicit(version, s + 2u, memory_order_release);     // even + publish
}

// false (out untouched) if unpublished. Else copies an untorn value.
static inline bool ano_seqpub_load(const void *value, const _Atomic uint64_t *version, void *out, size_t stride)
{
    for (;;) {
        uint64_t s1 = atomic_load_explicit(version, memory_order_acquire);
        if (s1 == 0u) return false; // never published
        if (s1 & 1u) continue;      // mid-write
        for (size_t i = 0; i < stride; ++i) ((uint8_t *)out)[i] = ((const uint8_t *)value)[i];
        atomic_thread_fence(memory_order_acquire);                    // value reads before recheck
        uint64_t s2 = atomic_load_explicit(version, memory_order_relaxed);
        if (s1 == s2) return true;  // version unmoved
    }
}


/* Bridge */

// Completes the opaque AnoRenderBridge declared in anoptic_render.h.
struct AnoRenderBridge
{
    AnoSpscRing commands; // logic -> render (RenderCommand)
    AnoSpscRing events;   // render -> logic (RenderEvent)

    // Latest-wins seqlocks; each version on its own cache line.
    // snapshot: render publishes, logic acquires. viewState: logic publishes, render acquires.
    RenderSnapshot snapshot;
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t snapshotVersion;
    AnoViewState   viewState;
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t viewStateVersion;
};

// in:  bridge, heap, cmd_capacity_pow2, evt_capacity_pow2
// out: true on success; false on allocation failure
// inv: both rings allocate from `heap`; destroy the bridge before releasing it.
bool ano_render_bridge_init(AnoRenderBridge *bridge, mi_heap_t *heap,
                            uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

void ano_render_bridge_destroy(AnoRenderBridge *bridge);

// Non-inline logic endpoints (submit/lights/text/ui + poll/snapshot/view): ano_render_bridge.c.


/* Render Master Endpoints */

// Dequeue one command into `out`. false if empty.
static inline bool ano_render_next_command(AnoRenderBridge *bridge, RenderCommand *out)
{
    return ano_spsc_pop(&bridge->commands, out);
}

// Enqueue one event. false if full (render must NOT block: drop coalescible samples, advise via CAPACITY).
static inline bool ano_render_emit_event(AnoRenderBridge *bridge, const RenderEvent *evt)
{
    return ano_spsc_push(&bridge->events, evt);
}

// Publish this frame's view-0 camera snapshot for the logic master.
static inline void ano_render_publish_snapshot(AnoRenderBridge *bridge, const RenderSnapshot *snap)
{
    ano_seqpub_store(&bridge->snapshot, &bridge->snapshotVersion, snap, sizeof *snap);
}

// Read latest logic-published camera pose. false (untouched) before first publish.
static inline bool ano_render_acquire_view(AnoRenderBridge *bridge, AnoViewState *out)
{
    return ano_seqpub_load(&bridge->viewState, &bridge->viewStateVersion, out, sizeof *out);
}

#endif // ANO_RENDER_BRIDGE_INTERNAL_H
