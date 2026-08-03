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
#include <mimalloc.h>
#include <anoptic_math.h>
#include <anoptic_render.h> // command/event protocol + opaque AnoRenderBridge
#include <anoptic_threads_typed.h>

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


/* Checked Sizing */

// in:  *bytes (running total), count, stride (> 0)
// out: true with *bytes += count*stride; false leaves *bytes untouched
// inv: overflow-checked; neither product nor sum formed out of range
static inline bool ano_size_add_array(size_t *bytes, uint32_t count, size_t stride)
{
    if ((size_t)count > SIZE_MAX / stride) return false;
    size_t add = (size_t)count * stride;
    if (add > SIZE_MAX - *bytes) return false;
    *bytes += add;
    return true;
}

// in:  *bytes (running offset), align (power of two)
// out: true with *bytes rounded up to align; false leaves *bytes untouched
// inv: packer must align SIZE and CURSOR with the same call
static inline bool ano_size_align_up(size_t *bytes, size_t align)
{
    size_t pad = (align - (*bytes & (align - 1u))) & (align - 1u);
    if (pad > SIZE_MAX - *bytes) return false;
    *bytes += pad;
    return true;
}


/* Bridge */

static_assert(ano::TransportData<RenderCommand>);
static_assert(ano::TransportData<RenderEvent>);
static_assert(ano::TransportData<RenderSnapshot>);
static_assert(ano::TransportData<AnoViewState>);

// Completes the opaque AnoRenderBridge declared in anoptic_render.h.
struct AnoRenderBridge
{
    ano::SpscRing<RenderCommand> commands; // logic -> render
    ano::SpscRing<RenderEvent> events;     // render -> logic

    ano::SeqPub<RenderSnapshot> snapshot; // render -> logic
    ano::SeqPub<AnoViewState> viewState; // logic -> render
    bool viewRejectWarned; // publisher-private: degenerate-pose warning once (never read by render)
};

// in:  bridge, heap, cmd_capacity_pow2, evt_capacity_pow2
// out: true on success; false on allocation failure
// inv: both rings allocate from `heap`; destroy the bridge before releasing it.
bool ano_render_bridge_init(AnoRenderBridge *bridge, mi_heap_t *heap,
                            uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

// Drains and discharges any command still enqueued, then releases both ring buffers.
void ano_render_bridge_destroy(AnoRenderBridge *bridge);

// in:  cmd, a command being DROPPED before any consumer adopts its payload
// out: nothing; releases the render-owned block if any
// inv: sole decode of RenderCommand ownership. Drop paths only (not after registry handoff). Honors bulk_owned.
void ano_render_command_release(const RenderCommand *cmd);

// Non-inline logic endpoints (submit/lights/text/ui + poll/snapshot/view): ano_render_bridge.c.


/* Render Master Endpoints */

// Dequeue one command into `out`. false if empty.
static inline bool ano_render_next_command(AnoRenderBridge *bridge, RenderCommand *out)
{
    return bridge->commands.pop(*out);
}

// Enqueue one event. false if full (render must NOT block: drop coalescible samples, advise via CAPACITY).
static inline bool ano_render_emit_event(AnoRenderBridge *bridge, const RenderEvent *evt)
{
    return bridge->events.push(*evt);
}

// Publish this frame's view-0 camera snapshot for the logic master.
static inline void ano_render_publish_snapshot(AnoRenderBridge *bridge, const RenderSnapshot *snap)
{
    bridge->snapshot.publish(*snap);
}

// Read latest logic-published camera pose. false (untouched) before first publish.
static inline bool ano_render_acquire_view(AnoRenderBridge *bridge, AnoViewState *out)
{
    return bridge->viewState.acquire(*out);
}

#endif // ANO_RENDER_BRIDGE_INTERNAL_H
