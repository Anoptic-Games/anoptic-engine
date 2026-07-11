/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic<->render bridge: ring storage alloc/teardown, plus the producer
 * endpoint (ano_render_submit). The hot-path push/pop and the in-src endpoints
 * stay inlined in the private render_bridge.h, while only the cold init/destroy and
 * the public (non-inline) submit live here. Platform-agnostic and GPU-free,
 * part of anoptic_core. Public contract: include/anoptic_render.h. */

#include "render_bridge.h"

#include <stdint.h>
#include <string.h>

#include <anoptic_log.h>

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

// Public producer endpoint (anoptic_render.h). Non-inline, reached through the opaque handle.
// The in-src consumer/event endpoints stay inlined in render_bridge.h.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd)
{
    return ano_spsc_push(&bridge->commands, cmd);
}

// Runtime light endpoints. Build a POD RenderCommand and push it through the SPSC command ring.
// Backpressure contract is ano_render_submit's (false == ring full, retry).
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

// Screen-text endpoints (v0 bridge). `set` packs the block header and the instance copy
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

// Replays the evaluators' curve-stream walk for one PATH prim (aux0 = first word,
// aux1 = monotone-quad count): a start word, then per quad an optional
// SENTINEL + restart word ahead of the control + end words. Bounds every read the
// walk will make. The builder's own bakes always pass.
static bool ui_path_walk_valid(const uint32_t *curves, uint32_t curveCount,
                               uint32_t off, uint32_t quads)
{
    if (quads == 0u || off >= curveCount || curves[off] == ANO_UI_CURVE_SENTINEL)
        return false;
    uint32_t i = off + 1u;
    for (uint32_t c = 0; c < quads; c++) {
        if (i >= curveCount)
            return false; // separator-test read
        if (curves[i] == ANO_UI_CURVE_SENTINEL) {
            i++;
            if (i >= curveCount || curves[i] == ANO_UI_CURVE_SENTINEL)
                return false; // contour restart must be a point
            i++;
        }
        if (i + 1u >= curveCount)
            return false; // control + end reads
        i += 2u;
    }
    return true;
}

// Block-local reference validation for one UI prim. Invalid blocks drop producer-side
// (returning true).
static bool ui_prim_valid(const AnoUiPrim *p, uint32_t clips, uint32_t paints, uint32_t glyphs,
                          const uint32_t *curves, uint32_t curveCount)
{
    if (p->clipRef != ANO_UI_REF_NONE && p->clipRef >= clips)
        return false;
    if (p->paintRef != ANO_UI_REF_NONE && p->paintRef >= paints)
        return false;
    if (p->kind == ANO_UI_GLYPHS && (p->aux0 > glyphs || p->aux1 > glyphs - p->aux0))
        return false;
    if (p->kind == ANO_UI_PATH && !ui_path_walk_valid(curves, curveCount, p->aux0, p->aux1))
        return false;
    return true;
}

// UI endpoints (v0 bridge). `set` packs the builder's tables + glyph labels into one
// render-owned allocation, freed render-side on replace/clear/shutdown. Empty == clear.
bool ano_render_ui_set(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t layer,
                       const AnoUiBuilder *ui,
                       const AnoGlyphInstance *glyphs, uint32_t glyphCount)
{
    if (ui == NULL || ui->primCount == 0u)
        return ano_render_ui_clear(bridge, ui_id);
    if (ui->primCount > ANO_RENDER_UI_MAX_PRIMS || ui->clipCount > ANO_RENDER_UI_MAX_CLIPS
        || ui->paintCount > ANO_RENDER_UI_MAX_PAINTS || ui->stopCount > ANO_RENDER_UI_MAX_STOPS
        || ui->curveCount > ANO_RENDER_UI_MAX_CURVES
        || glyphCount > ANO_RENDER_UI_MAX_GLYPHS || (glyphCount > 0u && glyphs == NULL)) {
        ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (per-block caps or bad glyph pair).", ui_id);
        return true;
    }
    for (uint32_t i = 0; i < ui->primCount; i++) {
        if (!ui_prim_valid(&ui->prims[i], ui->clipCount, ui->paintCount, glyphCount,
                           ui->curves, ui->curveCount)) {
            ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (prim %u invalid).", ui_id, i);
            return true;
        }
    }
    size_t primB = (size_t)ui->primCount * sizeof(AnoUiPrim);
    size_t clipB = (size_t)ui->clipCount * sizeof(AnoUiClip);
    size_t paintB = (size_t)ui->paintCount * sizeof(AnoUiPaint);
    size_t stopB = (size_t)ui->stopCount * sizeof(AnoUiStop);
    size_t curveB = (size_t)ui->curveCount * sizeof(uint32_t);
    size_t glyphB = (size_t)glyphCount * sizeof(AnoGlyphInstance);
    char *blk = mi_malloc(sizeof(RenderUiBlock) + primB + clipB + paintB + stopB + curveB + glyphB);
    if (blk == NULL)
        return false;
    RenderUiBlock *b = (RenderUiBlock *)blk;
    char *at = blk + sizeof(RenderUiBlock);
    b->layer = layer;
    b->surface = ANO_UI_SURFACE_OVERLAY;
    b->scroll[0] = 0.0f;
    b->scroll[1] = 0.0f;
    b->primCount = ui->primCount;
    b->clipCount = ui->clipCount;
    b->paintCount = ui->paintCount;
    b->stopCount = ui->stopCount;
    b->curveCount = ui->curveCount;
    b->glyphCount = glyphCount;
    b->prims = (const AnoUiPrim *)at;
    memcpy(at, ui->prims, primB);
    at += primB;
    b->clips = (const AnoUiClip *)at;
    if (clipB) memcpy(at, ui->clips, clipB);
    at += clipB;
    b->paints = (const AnoUiPaint *)at;
    if (paintB) memcpy(at, ui->paints, paintB);
    at += paintB;
    b->stops = (const AnoUiStop *)at;
    if (stopB) memcpy(at, ui->stops, stopB);
    at += stopB;
    b->curves = (const uint32_t *)at;
    if (curveB) memcpy(at, ui->curves, curveB);
    at += curveB;
    b->glyphs = (const AnoGlyphInstance *)at;
    if (glyphB) memcpy(at, glyphs, glyphB);
    RenderCommand c = { .kind = RCMD_UI_SET, .ui_id = ui_id, .ui = b, .bulk_owned = true };
    if (!ano_spsc_push(&bridge->commands, &c)) {
        mi_free(blk);
        return false;
    }
    return true;
}

bool ano_render_ui_clear(AnoRenderBridge *bridge, uint32_t ui_id)
{
    RenderCommand c = { .kind = RCMD_UI_CLEAR, .ui_id = ui_id };
    return ano_spsc_push(&bridge->commands, &c);
}

// Back-channel logic-master endpoints (anoptic_render.h). Non-inline, reached through the opaque handle.
// The matching render-side producer/consumer halves are the inline helpers in render_bridge.h.
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
