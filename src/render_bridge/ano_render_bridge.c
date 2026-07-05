/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic<->render bridge: ring storage alloc/teardown, plus the producer
 * endpoint (ano_render_submit). The hot-path push/pop and the in-src endpoints
 * stay inlined in the private render_bridge.h, while only the cold init/destroy and
 * the public (non-inline) submit live here. Platform-agnostic and GPU-free,
 * part of anoptic_core. Public contract: include/anoptic_render.h.
 * Design of record: docs/artifacts/VK_BACKEND_INTEROP.md. */

#include "render_bridge.h"

#include <stdint.h>
#include <string.h>

// Guard the events-ring element size (copied per push/pop, sized capacity * this). Held at 32 B.
_Static_assert(sizeof(RenderEvent) <= 32u, "RenderEvent grew past 32 bytes; revisit the events ring");

// Smallest power of two >= v, floor of 2. Returns 0 on overflow (v > 2^31).
static uint32_t next_pow2_u32(uint32_t v)
{
    if (v < 2u) return 2u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u; // wraps to 0 if v was > 2^31
}

bool ano_spsc_init(AnoSpscRing *ring, mi_heap_t *heap, uint32_t capacity_pow2, uint32_t stride)
{
    if (!ring || !heap || stride == 0u) return false;

    uint32_t cap = next_pow2_u32(capacity_pow2);
    if (cap == 0u) return false;                       // capacity overflow
    if ((size_t)cap > SIZE_MAX / stride) return false; // cap*stride overflow

    uint8_t *buffer = mi_heap_calloc(heap, cap, stride);
    if (!buffer) return false;

    atomic_init(&ring->tail, 0u);
    atomic_init(&ring->head, 0u);
    ring->mask   = cap - 1u;
    ring->stride = stride;
    ring->buffer = buffer;
    return true;
}

void ano_spsc_destroy(AnoSpscRing *ring)
{
    if (!ring) return;
    if (ring->buffer) {
        mi_free(ring->buffer);
        ring->buffer = NULL;
    }
    ring->mask   = 0u;
    ring->stride = 0u;
    atomic_store_explicit(&ring->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0u, memory_order_relaxed);
}

bool ano_render_bridge_init(AnoRenderBridge *bridge, mi_heap_t *heap,
                            uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2)
{
    if (!bridge || !heap) return false;
    if (!ano_spsc_init(&bridge->commands, heap, cmd_capacity_pow2, (uint32_t)sizeof(RenderCommand)))
        return false;
    if (!ano_spsc_init(&bridge->events, heap, evt_capacity_pow2, (uint32_t)sizeof(RenderEvent))) {
        ano_spsc_destroy(&bridge->commands);
        return false;
    }
    // Published latest-wins lanes start unpublished (version 0): until logic publishes a pose the
    // renderer uses its built-in camera, and until render publishes a frame logic's acquire fails.
    memset(&bridge->snapshot, 0, sizeof bridge->snapshot);
    memset(&bridge->viewState, 0, sizeof bridge->viewState);
    atomic_init(&bridge->snapshotVersion, 0u);
    atomic_init(&bridge->viewStateVersion, 0u);
    return true;
}

void ano_render_bridge_destroy(AnoRenderBridge *bridge)
{
    if (!bridge) return;
    ano_spsc_destroy(&bridge->commands);
    ano_spsc_destroy(&bridge->events);
}

// Public producer endpoint (anoptic_render.h). Non-inline by design, so the engine
// entry point reaches it through the opaque handle without seeing the ring.
// The in-src consumer/event endpoints stay inlined in render_bridge.h.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd)
{
    return ano_spsc_push(&bridge->commands, cmd);
}

// Runtime light endpoints (audit 4.7). Build a POD RenderCommand and push it through the same SPSC
// command ring; backpressure contract is ano_render_submit's (false == ring full, retry).
bool ano_render_light_attach(AnoRenderBridge *bridge, uint32_t light_id, uint32_t parent_render_id,
                             const RenderLightParams *params, float ox, float oy, float oz)
{
    RenderCommand c = { .kind = RCMD_LIGHT_ATTACH, .render_id = parent_render_id, .light_id = light_id };
    if (params) c.light = *params;
    c.light_offset[0] = ox; c.light_offset[1] = oy; c.light_offset[2] = oz;
    return ano_spsc_push(&bridge->commands, &c);
}

bool ano_render_light_update(AnoRenderBridge *bridge, uint32_t light_id,
                             const RenderLightParams *params, float ox, float oy, float oz)
{
    return ano_render_light_update_fields(bridge, light_id, params, ox, oy, oz, ANO_LIGHT_FIELD_ALL);
}

bool ano_render_light_update_fields(AnoRenderBridge *bridge, uint32_t light_id,
                                    const RenderLightParams *params, float ox, float oy, float oz,
                                    uint32_t fields)
{
    RenderCommand c = { .kind = RCMD_LIGHT_UPDATE, .light_id = light_id, .light_fields = fields };
    if (params) c.light = *params;
    c.light_offset[0] = ox; c.light_offset[1] = oy; c.light_offset[2] = oz;
    return ano_spsc_push(&bridge->commands, &c);
}

bool ano_render_light_detach(AnoRenderBridge *bridge, uint32_t light_id)
{
    RenderCommand c = { .kind = RCMD_LIGHT_DETACH, .light_id = light_id };
    return ano_spsc_push(&bridge->commands, &c);
}

// Screen-text endpoints (FONT_RENDER.md v0 bridge). `set` packs the block header and the instance copy
// into one render-owned allocation, freed render-side on replace/clear/shutdown. count 0 == clear.
// Backpressure contract is ano_render_submit's.
bool ano_render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
                         const AnoGlyphInstance *instances, uint32_t count)
{
    if (count == 0u)
        return ano_render_text_clear(bridge, text_id);
    if (instances == NULL)
        return true; // invalid pair (count without data): no-op
    if (count > ANO_RENDER_TEXT_MAX)
        count = ANO_RENDER_TEXT_MAX; // clamp to the region
    size_t bytes = sizeof(RenderTextBlock) + (size_t)count * sizeof(AnoGlyphInstance);
    char *blk = mi_malloc(bytes);
    if (blk == NULL)
        return false;
    RenderTextBlock *b = (RenderTextBlock *)blk;
    AnoGlyphInstance *inst = (AnoGlyphInstance *)(blk + sizeof(RenderTextBlock));
    memcpy(inst, instances, (size_t)count * sizeof(AnoGlyphInstance));
    b->count = count;
    b->instances = inst;
    RenderCommand c = { .kind = RCMD_TEXT_SET, .text_id = text_id, .text = b, .bulk_owned = true };
    if (!ano_spsc_push(&bridge->commands, &c)) {
        mi_free(blk);
        return false;
    }
    return true;
}

bool ano_render_text_clear(AnoRenderBridge *bridge, uint32_t text_id)
{
    RenderCommand c = { .kind = RCMD_TEXT_CLEAR, .text_id = text_id };
    return ano_spsc_push(&bridge->commands, &c);
}

// Back-channel logic-master endpoints (anoptic_render.h). Non-inline so the logic master reaches
// them through the opaque handle; the matching render-side producer/consumer halves are the inline
// helpers in render_bridge.h.
bool ano_render_poll_event(AnoRenderBridge *bridge, RenderEvent *out)
{
    return ano_spsc_pop(&bridge->events, out);
}

bool ano_render_acquire_snapshot(AnoRenderBridge *bridge, RenderSnapshot *out)
{
    return ano_seqpub_load(&bridge->snapshot, &bridge->snapshotVersion, out, sizeof *out);
}

void ano_render_publish_view(AnoRenderBridge *bridge, const AnoViewState *view)
{
    ano_seqpub_store(&bridge->viewState, &bridge->viewStateVersion, view, sizeof *view);
}
