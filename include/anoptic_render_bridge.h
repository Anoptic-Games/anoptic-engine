/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


/// TODO: This has all got to GO. 

/*

We move everything actually called by main() to anoptic_render.h (and ONLY those called by main).
Absolutely 0 inline function definitions.
Function signatures and types they use only.
Everything implementation-related goes in the appropriate src/ section.

*/


/**
 * @file anoptic_render_bridge.h
 * @brief The logic<->render boundary: command/event protocol, the SPSC rings
 *        that carry them, and the logic-side render-projection component.
 *
 * Design of record: docs/artifacts/VK_BACKEND_INTEROP.md (render side) and
 * docs/artifacts/ECS.md S4-S5 (logic side).
 *
 * Two parallel worlds, one-way streams each direction:
 *   logic master  --RenderCommand-->  render master   (commands ring)
 *   render master --RenderEvent---->  logic master    (events ring)
 *
 * Both directions are strictly single-producer/single-consumer, so the rings are
 * bounded SPSC (acquire/release on head/tail, no CAS). The logic master is the
 * sole command producer (it emits AFTER the parallel update stage settles, so
 * ordering is total); the render master is the sole event producer.
 *
 * Renderables are named by a stable logical `render_id` assigned by the ECS. The
 * render world privately maps render_id -> physical GPU slot; this header never
 * exposes GPU slots. Continuous, GPU-parameterized motion (orbit/spin) is sent
 * ONCE as animation parameters and never streamed; only discrete transitions
 * cross the bridge.
 */

#ifndef ANOPTIC_RENDER_BRIDGE_H
#define ANOPTIC_RENDER_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <mimalloc.h>
#include <anoptic_math.h>

// Absent-attribute sentinels, shared by logic and render. A renderable with
// ANO_RENDER_NO_MESH carries no geometry (the cull pass skips it — e.g. a pure
// light); ANO_RENDER_NO_LIGHT marks a renderable that drives no light.
#define ANO_RENDER_NO_MESH  0xFFFFFFFFu
#define ANO_RENDER_NO_LIGHT 0xFFFFFFFFu

// ---------------------------------------------------------------------------
// Light parameters (transport form)
// ---------------------------------------------------------------------------

// glTF KHR_lights_punctual model. This is the clean transport struct: photometric
// parameters only. World position/direction are NOT here — the render world
// derives them from the driving renderable's live transform, so animated lights
// need no per-frame light traffic. (The renderer translates this into its private
// std430 GPU LightData, which adds the resolved transform/slot link.)
typedef enum RenderLightType
{
    RENDER_LIGHT_DIRECTIONAL = 0,
    RENDER_LIGHT_POINT       = 1,
    RENDER_LIGHT_SPOT        = 2,
} RenderLightType;

typedef struct RenderLightParams
{
    float           color[3];     // linear RGB, normalized (intensity carries magnitude)
    float           intensity;    // candela-like (point/spot) or lux-like (directional)
    float           range;        // attenuation cutoff; <= 0 == unbounded (ignored for directional)
    float           innerConeCos; // spot inner cone half-angle cosine
    float           outerConeCos; // spot outer cone half-angle cosine
    RenderLightType type;
} RenderLightParams;

// ---------------------------------------------------------------------------
// Commands: logic -> render
// ---------------------------------------------------------------------------

typedef enum RenderCommandKind
{
    RCMD_CREATE,       // new renderable; carries full initial state
    RCMD_UPDATE,       // discrete change(s) to an existing renderable (see `fields`)
    RCMD_DESTROY,      // remove a renderable (render_id only)
    RCMD_BULK_CREATE,  // contiguous batch of new renderables (mass spawn); see `batch`
} RenderCommandKind;

// Which payload fields a CREATE/UPDATE carries. A single UPDATE may set several
// bits: that is the "<=1 message per entity per tick" invariant made literal.
typedef enum RenderFieldBits
{
    RFIELD_TRANSFORM = 1 << 0, // teleport: rewrite the BASE pose (initialTransform), never the GPU-output transform
    RFIELD_MESH_MAT  = 1 << 1, // mesh and/or material index
    RFIELD_ANIM      = 1 << 2, // GPU animation parameters (establishes/changes continuous motion)
    RFIELD_LIGHT     = 1 << 3, // light photometric parameters
} RenderFieldBits;

// Initial-state batch referenced by RCMD_BULK_CREATE. The producer hands ownership
// of `transforms`/`anim`/`mesh`/`material`/`render_ids` (arena-allocated, length
// `count`) to the render master, which block-writes a contiguous slot range and
// releases the batch when consumed.
typedef struct RenderCreateBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names
    const mat4     *transforms;  // [count] base poses
    const Vector4  *anim;        // [count] animation params (xyz = axis*speed, w = orbit flag)
    const uint32_t *mesh;        // [count] geometry pool indices (ANO_RENDER_NO_MESH allowed)
    const uint32_t *material;    // [count] material palette indices
} RenderCreateBatch;

// POD, fixed-size, copied by value through the ring. ~fat (holds a mat4) but
// CREATE needs it; UPDATE only reads the fields flagged in `fields`.
typedef struct RenderCommand
{
    RenderCommandKind kind;
    uint32_t          render_id;        // logical name; valid for CREATE/UPDATE/DESTROY
    uint32_t          fields;           // RenderFieldBits, for CREATE/UPDATE

    mat4              transform;        // base pose (CREATE, or UPDATE | RFIELD_TRANSFORM)
    Vector4           angular_velocity; // anim params (CREATE, or UPDATE | RFIELD_ANIM)
    uint32_t          mesh_index;       // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          material_index;   // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          light_index;      // ANO_RENDER_NO_LIGHT if not a light
    RenderLightParams light;            // CREATE (if light) or UPDATE | RFIELD_LIGHT

    const RenderCreateBatch *batch;     // RCMD_BULK_CREATE only
} RenderCommand;

// ---------------------------------------------------------------------------
// Events: render -> logic
// ---------------------------------------------------------------------------

typedef enum RenderEventKind
{
    REVENT_SLOT_RETIRED, // render_id's GPU slot has cleared all frames in flight; ECS may recycle the id
    REVENT_CAPACITY,     // render-side capacity advisory / backpressure (render_id unused)
} RenderEventKind;

typedef struct RenderEvent
{
    RenderEventKind kind;
    uint32_t        render_id;
} RenderEvent;

// ---------------------------------------------------------------------------
// Logic-side render-projection component (ECS.md S4)
// ---------------------------------------------------------------------------

// Dirty bits accumulated by systems during the parallel update stage; consumed by
// the single-threaded graphics-extract pass that emits commands.
typedef enum RenderDirtyBits
{
    RENDER_DIRTY_SPAWN    = 1 << 0, // first time renderable        -> RCMD_CREATE
    RENDER_DIRTY_TELEPORT = 1 << 1, // discontinuous pose change    -> RFIELD_TRANSFORM (NOT continuous motion)
    RENDER_DIRTY_MESH_MAT = 1 << 2, // mesh/material swap           -> RFIELD_MESH_MAT
    RENDER_DIRTY_ANIM     = 1 << 3, // animation parameters changed -> RFIELD_ANIM
    RENDER_DIRTY_LIGHT    = 1 << 4, // light parameters changed     -> RFIELD_LIGHT
    RENDER_DIRTY_DESTROY  = 1 << 5, // renderable should be removed  -> RCMD_DESTROY
} RenderDirtyBits;

// Minimal render-relevant projection of an entity, stored as an ECS component.
// NOT a full mirror of the render world — just enough to detect discrete
// transitions and name the renderable. Continuous motion is captured as
// `angular_velocity` (sent once via RFIELD_ANIM), never as per-tick transforms.
typedef struct DisplayState
{
    uint32_t render_id;          // stable logical name while renderable
    mat4     transform;          // base pose; payload for SPAWN / TELEPORT
    Vector4  angular_velocity;   // xyz = axis*speed, w = orbit flag
    uint32_t mesh_index;         // geometry pool index, or ANO_RENDER_NO_MESH
    uint32_t material_index;     // material palette index
    uint32_t light_index;        // ANO_RENDER_NO_LIGHT if not a light
    uint32_t dirty;              // RenderDirtyBits accumulated this tick
} DisplayState;

// ---------------------------------------------------------------------------
// Bounded SPSC ring
// ---------------------------------------------------------------------------

// Hardware cache-line size assumed for false-sharing avoidance (x86-64 / arm64).
#define ANO_CACHE_LINE 64

// Single-producer/single-consumer bounded ring over fixed-size POD elements.
// Lock-free, wait-free on both ends; capacity is a power of two so index
// wrap is a mask. The producer owns `tail`, the consumer owns `head`; each reads
// the other's index with acquire and publishes its own with release.
//
// tail (producer) and head (consumer) sit on separate cache lines: the producer's
// frequent tail-store must not invalidate the line the consumer spins on for head
// (and vice versa), or the ring degrades into a cache-line ping-pong. Because a
// member carries _Alignas(64), the whole struct aligns to 64 — so an embedding
// struct (AnoRenderBridge) inherits 64-byte alignment automatically on the stack
// or in static storage. A HEAP-allocated owner must request 64-byte alignment
// (e.g. mi_heap_malloc_aligned) for the separation to hold.
//
// This is the minimal purpose-built ring the bridge needs now; it should migrate
// to anoptic_collections.h when the generic lock-free collections land (notes.md
// Step 5).
typedef struct AnoSpscRing
{
    _Alignas(ANO_CACHE_LINE) _Atomic uint32_t tail; // producer-owned cursor: next index to write
    _Alignas(ANO_CACHE_LINE) _Atomic uint32_t head; // consumer-owned cursor: next index to read
    _Alignas(ANO_CACHE_LINE) uint32_t mask;         // capacity - 1 (immutable after init)
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
// The bridge
// ---------------------------------------------------------------------------

typedef struct AnoRenderBridge
{
    AnoSpscRing commands; // logic -> render (RenderCommand)
    AnoSpscRing events;   // render -> logic (RenderEvent)
} AnoRenderBridge;

// in:  bridge, heap, cmd_capacity_pow2, evt_capacity_pow2
// out: true on success; false on allocation failure
// inv: both rings allocate from `heap`; destroy the bridge before releasing it.
bool ano_render_bridge_init(AnoRenderBridge *bridge, mi_heap_t *heap,
                            uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

void ano_render_bridge_destroy(AnoRenderBridge *bridge);

// --- Logic master endpoint (produces commands, consumes events) ---

// Enqueue one command. false if the command ring is full.
static inline bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd)
{
    return ano_spsc_push(&bridge->commands, cmd);
}

// Dequeue one event into `out`. false if no event pending.
static inline bool ano_render_poll_event(AnoRenderBridge *bridge, RenderEvent *out)
{
    return ano_spsc_pop(&bridge->events, out);
}

// --- Render master endpoint (consumes commands, produces events) ---

// Dequeue one command into `out`. false if no command pending.
static inline bool ano_render_next_command(AnoRenderBridge *bridge, RenderCommand *out)
{
    return ano_spsc_pop(&bridge->commands, out);
}

// Enqueue one event. false if the event ring is full.
static inline bool ano_render_emit_event(AnoRenderBridge *bridge, const RenderEvent *evt)
{
    return ano_spsc_push(&bridge->events, evt);
}

#endif // ANOPTIC_RENDER_BRIDGE_H
