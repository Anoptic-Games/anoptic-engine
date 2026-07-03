/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file render_bridge.h (private to src/)
 * @brief The logic<->render transport: the SPSC rings that carry the protocol,
 *        the bridge struct that completes the opaque AnoRenderBridge handle, the
 *        render->logic event protocol, and the logic-side render-projection
 *        component. Implementation detail of the render_bridge module; consumed
 *        only within src/ (render_bridge itself + the vulkan_backend that drains
 *        commands and emits events). Never include from include/.
 *
 * The PUBLIC contract — the command protocol the producer builds, the opaque
 * AnoRenderBridge, ano_render_submit, and the renderer lifecycle — is
 * include/anoptic_render.h. Design of record: docs/artifacts/VK_BACKEND_INTEROP.md
 * (render side) and docs/artifacts/ECS.md S4-S5 (logic side).
 *
 * Two parallel worlds, one-way streams each direction:
 *   logic master  --RenderCommand-->  render master   (commands ring)
 *   render master --RenderEvent---->  logic master    (events ring)
 *
 * Both directions are strictly single-producer/single-consumer, so the rings are
 * bounded SPSC (acquire/release on head/tail, no CAS). The logic master is the
 * sole command producer, and it emits AFTER the parallel update stage settles, so
 * ordering is total. The render master is the sole event producer.
 *
 * Renderables are named by a stable logical `render_id` assigned by the ECS. The
 * render world privately maps render_id -> physical GPU slot and never exposes
 * GPU slots. Continuous, GPU-parameterized motion (orbit/spin) is sent ONCE as
 * animation parameters and never streamed, since only discrete transitions cross.
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

// The render->logic event protocol (RenderEventKind / RenderEvent / AnoInputEvent), the published
// RenderSnapshot, and the AnoViewState pose are PUBLIC in anoptic_render.h (the logic master, which
// lives outside src/render_bridge, consumes them) — symmetric with the public RenderCommand. Only
// the TRANSPORT (the rings, the published double buffers, this bridge struct) is private here.

// ---------------------------------------------------------------------------
// Logic-side render-projection component (ECS.md S4)
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
// NOT a full mirror of the render world — just enough to detect discrete
// transitions and name the renderable. Continuous motion is captured as `motion`
// (sent once via RFIELD_ANIM), never as per-tick transforms.
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
// Lock-free and wait-free on both ends, with capacity a power of two so index
// wrap is a mask. The producer owns `tail`, the consumer owns `head`.
// Each reads the other's index with acquire and publishes its own with release.
//
// tail (producer) and head (consumer) sit on separate ANO_THREAD_LINE regions.
// The producer's frequent tail-store must not invalidate the line the consumer
// spins on for head (and vice versa), or the ring ping-pongs cache lines.
// ANO_THREAD_LINE (anoptic_memory.h) is the false-sharing isolation distance of
// 128, wide enough to clear x86's adjacent-line prefetch pair and Apple Silicon's
// 128-byte line. A member carries _Alignas(ANO_THREAD_LINE), so the whole struct
// inherits that alignment, so an embedding struct (AnoRenderBridge) gets it for free
// on the stack or in static storage. A HEAP owner must request ANO_THREAD_LINE
// alignment (e.g. mi_heap_malloc_aligned) for the separation to hold.
//
// Minimal purpose-built ring the bridge needs now.
// Migrate to anoptic_collections.h when the generic lock-free collections land
// (notes.md Step 5).
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
// Cross-thread transfer of continuous state where only the newest value matters (the camera snapshot
// render->logic, the camera pose logic->render). A single producer writes the value guarded by an
// even/odd version: even == stable, odd == mid-write. The producer is wait-free; the consumer copies
// the value and retries iff the version moved across its copy, so it never observes a torn value —
// tear-free for ANY payload size and ANY scheduling (no timing assumption; a mid-copy preemption is
// detected and retried, not silently torn). In steady state (one publish per frame/tick, a
// sub-microsecond copy) the consumer retries essentially never. version == 0 means "never published".
// Generic over element size so the one subtle ordering lives in exactly one place.
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
        if (s1 & 1u) continue;      // producer mid-write; re-read (it finishes in nanoseconds)
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

    // Published latest-wins state, each a seqlock with its version on a private cache line so a
    // publish does not invalidate the line the rings spin on. snapshot: render publishes, logic
    // acquires. viewState: logic publishes, render acquires.
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
// ano_render_submit (produces commands), ano_render_poll_event (consumes events),
// ano_render_acquire_snapshot, and ano_render_publish_view are the public endpoints; they are
// defined non-inline in ano_render_bridge.c so the logic master reaches them through the opaque
// handle without seeing this transport.

// --- Render master endpoints (consumes commands + viewstate, produces events + snapshot) ---

// Dequeue one command into `out`. false if no command pending.
static inline bool ano_render_next_command(AnoRenderBridge *bridge, RenderCommand *out)
{
    return ano_spsc_pop(&bridge->commands, out);
}

// Enqueue one event. false if the event ring is full (render must NOT block on this — it runs on
// the same thread as glfwPollEvents; the caller drops coalescible samples and advises via CAPACITY).
static inline bool ano_render_emit_event(AnoRenderBridge *bridge, const RenderEvent *evt)
{
    return ano_spsc_push(&bridge->events, evt);
}

// Publish this frame's view-0 camera snapshot for the logic master.
static inline void ano_render_publish_snapshot(AnoRenderBridge *bridge, const RenderSnapshot *snap)
{
    ano_seqpub_store(&bridge->snapshot, &bridge->snapshotVersion, snap, sizeof *snap);
}

// Read the latest camera pose the logic master published. false (out untouched) before its first
// publish, so the renderer falls back to its built-in camera.
static inline bool ano_render_acquire_view(AnoRenderBridge *bridge, AnoViewState *out)
{
    return ano_seqpub_load(&bridge->viewState, &bridge->viewStateVersion, out, sizeof *out);
}

#endif // ANO_RENDER_BRIDGE_INTERNAL_H
