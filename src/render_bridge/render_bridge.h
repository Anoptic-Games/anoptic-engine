/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file render_bridge.h (private to src/)
 * @brief The logic<->render transport: the SPSC rings that carry the protocol,
 *        the bridge struct completing the opaque AnoRenderBridge handle, the
 *        render->logic event protocol, and the logic-side render-projection
 *        component. Consumed only within src/. Never include from include/.
 *
 * The PUBLIC contract (command protocol, opaque AnoRenderBridge, ano_render_submit,
 * renderer lifecycle) is include/anoptic_render.h.
 *
 * Two parallel worlds, one-way streams each direction:
 *   logic master  --RenderCommand-->  render master   (commands ring)
 *   render master --RenderEvent---->  logic master    (events ring)
 *
 * Both directions are single-producer/single-consumer, so the rings are bounded
 * SPSC (acquire/release on head/tail, no CAS). The logic master emits commands
 * after the parallel update stage settles, so ordering is total.
 *
 * Renderables are named by a stable logical `render_id` from the ECS. The render
 * world privately maps it to a physical GPU slot. Continuous GPU-parameterized
 * motion is sent ONCE as animation parameters, never streamed.
 */

#ifndef ANO_RENDER_BRIDGE_INTERNAL_H
#define ANO_RENDER_BRIDGE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <mimalloc.h>
#include <anoptic_memory.h> // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <anoptic_math.h>
#include <anoptic_render.h> // command protocol + opaque AnoRenderBridge + ano_render_submit

// The event protocol (RenderEventKind / RenderEvent / AnoInputEvent), the RenderSnapshot, and the
// AnoViewState pose are PUBLIC in anoptic_render.h. Only the TRANSPORT (rings, published double
// buffers, this bridge struct) is private here.

// ---------------------------------------------------------------------------
// Logic-side render-projection component
// ---------------------------------------------------------------------------

// Dirty bits accumulated by systems during the parallel update stage.
// Consumed by the single-threaded graphics-extract pass that emits commands.
typedef enum RenderDirtyBits
{
    RENDER_DIRTY_SPAWN    = 1 << 0, // first time renderable        -> RCMD_CREATE
    RENDER_DIRTY_TELEPORT = 1 << 1, // discontinuous pose change    -> RFIELD_TRANSFORM (NOT continuous motion)
    RENDER_DIRTY_MESH_MAT = 1 << 2, // mesh/material swap           -> RFIELD_MESH_MAT
    RENDER_DIRTY_ANIM     = 1 << 3, // animation parameters changed -> RFIELD_ANIM
    RENDER_DIRTY_LIGHT    = 1 << 4, // light parameters changed     -> RFIELD_LIGHT
    RENDER_DIRTY_DESTROY  = 1 << 5, // renderable should be removed  -> RCMD_DESTROY
    RENDER_DIRTY_USERDATA = 1 << 6, // instance channel changed     -> RFIELD_USERDATA
} RenderDirtyBits;

// Minimal render-relevant projection of an entity, stored as an ECS component.
// Enough to detect discrete transitions and name the renderable. Continuous
// motion is captured as `motion` (sent once via RFIELD_ANIM), never per-tick.
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

// ---------------------------------------------------------------------------
// Bounded SPSC ring
// ---------------------------------------------------------------------------

// Single-producer/single-consumer bounded ring over fixed-size POD elements.
// Lock-free and wait-free both ends, capacity a power of two so index wrap is a mask.
// The producer owns `tail`, the consumer owns `head`. Each reads the other with
// acquire and publishes its own with release.
//
// tail and head sit on separate ANO_THREAD_LINE regions to avoid false sharing.
// ANO_THREAD_LINE (anoptic_memory.h) is the 128-byte isolation distance. A member
// carries _Alignas(ANO_THREAD_LINE) so the whole struct inherits it. A HEAP owner
// must request that alignment (e.g. mi_heap_malloc_aligned) for the separation to hold.
//
// Migrate to anoptic_collections.h when the generic lock-free collections land.
typedef struct AnoSpscRing
{
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail; // producer-owned cursor: next index to write
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head; // consumer-owned cursor: next index to read
    _Alignas(ANO_THREAD_LINE) uint32_t mask;         // capacity - 1 (immutable after init)
    uint32_t                          stride;       // element size in bytes
    uint8_t                          *buffer;       // capacity * stride bytes
} AnoSpscRing;

// in:  ring, heap, capacity_pow2 (rounded up to a power of two, >= 2), stride (> 0)
// out: true on success; false on bad args or allocation failure
bool ano_spsc_init(AnoSpscRing *ring, mi_heap_t *heap, uint32_t capacity_pow2, uint32_t stride);

// Releases the ring buffer. Does not release the backing heap.
void ano_spsc_destroy(AnoSpscRing *ring);

// PRODUCER only. Copies `stride` bytes from `elem` into the ring.
// out: false if the ring is full (caller decides: drop, spin, or grow upstream).
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

// CONSUMER only. Copies the next element into `out` (>= stride bytes).
// out: false if the ring is empty.
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

// ---------------------------------------------------------------------------
// Lock-free latest-wins seqlock (epoch publication)
// ---------------------------------------------------------------------------
// Cross-thread transfer of continuous state where only the newest value matters (camera snapshot
// render->logic, camera pose logic->render). A single producer writes the value guarded by an
// even/odd version: even == stable, odd == mid-write. The consumer copies the value and retries iff
// the version moved across its copy, so it never observes a torn value, at any payload size or
// scheduling. version == 0 means "never published". Generic over element size.
static inline void ano_seqpub_store(void *value, _Atomic uint64_t *version, const void *v, size_t stride)
{
    uint64_t s = atomic_load_explicit(version, memory_order_relaxed); // single producer owns version
    atomic_store_explicit(version, s + 1u, memory_order_relaxed);     // enter write (odd)
    atomic_thread_fence(memory_order_release);                        // odd marker before the value writes
    for (size_t i = 0; i < stride; ++i) ((uint8_t *)value)[i] = ((const uint8_t *)v)[i];
    atomic_store_explicit(version, s + 2u, memory_order_release);     // exit write (even) + publish writes
}

// out: false (out untouched) if nothing published yet. Otherwise copies a CONSISTENT (untorn) value.
static inline bool ano_seqpub_load(const void *value, const _Atomic uint64_t *version, void *out, size_t stride)
{
    for (;;) {
        uint64_t s1 = atomic_load_explicit(version, memory_order_acquire);
        if (s1 == 0u) return false; // never published
        if (s1 & 1u) continue;      // producer mid-write, re-read
        for (size_t i = 0; i < stride; ++i) ((uint8_t *)out)[i] = ((const uint8_t *)value)[i];
        atomic_thread_fence(memory_order_acquire);                    // value reads before the recheck
        uint64_t s2 = atomic_load_explicit(version, memory_order_relaxed);
        if (s1 == s2) return true;  // version unmoved across the copy -> consistent
    }
}

// ---------------------------------------------------------------------------
// The bridge
// ---------------------------------------------------------------------------

// Completes the opaque AnoRenderBridge declared in anoptic_render.h.
struct AnoRenderBridge
{
    AnoSpscRing commands; // logic -> render (RenderCommand)
    AnoSpscRing events;   // render -> logic (RenderEvent)

    // Published latest-wins state, each a seqlock with its version on a private cache line.
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

// --- Logic master endpoints (anoptic_render.h) ---
// ano_render_submit, ano_render_poll_event, ano_render_acquire_snapshot, and
// ano_render_publish_view are public, defined non-inline in ano_render_bridge.c.

// --- Render master endpoints (consumes commands + viewstate, produces events + snapshot) ---

// Dequeue one command into `out`. false if no command pending.
static inline bool ano_render_next_command(AnoRenderBridge *bridge, RenderCommand *out)
{
    return ano_spsc_pop(&bridge->commands, out);
}

// Enqueue one event. false if the event ring is full (render must NOT block, it drops coalescible samples and advises via CAPACITY).
static inline bool ano_render_emit_event(AnoRenderBridge *bridge, const RenderEvent *evt)
{
    return ano_spsc_push(&bridge->events, evt);
}

// Publish this frame's view-0 camera snapshot for the logic master.
static inline void ano_render_publish_snapshot(AnoRenderBridge *bridge, const RenderSnapshot *snap)
{
    ano_seqpub_store(&bridge->snapshot, &bridge->snapshotVersion, snap, sizeof *snap);
}

// Read the latest camera pose the logic master published. false (out untouched) before its first publish.
static inline bool ano_render_acquire_view(AnoRenderBridge *bridge, AnoViewState *out)
{
    return ano_seqpub_load(&bridge->viewState, &bridge->viewStateVersion, out, sizeof *out);
}

#endif // ANO_RENDER_BRIDGE_INTERNAL_H
