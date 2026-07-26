/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic<->render bridge: ring alloc/teardown + non-inline logic endpoints.
 * Hot-path push/pop and render-master endpoints stay inlined in render_bridge.h.
 * Public contract: include/anoptic_render.h. */

#include "render_bridge.h"

#include <stdint.h>
#include <string.h>

#include <anoptic_log.h>


/* SPSC */

// Events-ring element size (copied per push/pop). Cap at 32 B.
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


/* Bridge Lifecycle */

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
    // Latest-wins lanes start at version 0 (unpublished).
    // Lane words: atomic_init, not memset.
    for (size_t i = 0; i < sizeof bridge->snapshot / sizeof bridge->snapshot[0]; ++i)
        atomic_init(&bridge->snapshot[i], 0u);
    for (size_t i = 0; i < sizeof bridge->viewState / sizeof bridge->viewState[0]; ++i)
        atomic_init(&bridge->viewState[i], 0u);
    atomic_init(&bridge->snapshotVersion, 0u);
    atomic_init(&bridge->viewStateVersion, 0u);
    bridge->viewRejectWarned = false;
    return true;
}

// in:  cmd (POD command being dropped)
// out: nothing; frees the render-owned block its kind carries
// inv: switch total over RenderCommandKind (no default). Ownership rides bulk_owned.
void ano_render_command_release(const RenderCommand *cmd)
{
    if (!cmd || !cmd->bulk_owned) return;
    const void *blk = NULL;
    switch (cmd->kind) {
    case RCMD_BULK_CREATE:  blk = cmd->batch;   break;
    case RCMD_BULK_UPDATE:  blk = cmd->update;  break;
    case RCMD_BULK_DESTROY: blk = cmd->destroy; break;
    case RCMD_TEXT_SET:     blk = cmd->text;    break;
    case RCMD_UI_SET:       blk = cmd->ui;      break;
    case RCMD_CREATE:
    case RCMD_UPDATE:
    case RCMD_DESTROY:
    case RCMD_STREAM_TRANSFORMS:
    case RCMD_LIGHT_ATTACH:
    case RCMD_LIGHT_UPDATE:
    case RCMD_LIGHT_DETACH:
    case RCMD_TEXT_CLEAR:
    case RCMD_UI_CLEAR:
        break; // payload rides the command by value
    }
    if (blk) mi_free((void *)blk);
}

// inv: caller quiesces both roles first (main.c joins producer, then teardown on consumer thread).
void ano_render_bridge_destroy(AnoRenderBridge *bridge)
{
    if (!bridge) return;
    // Discharge undelivered payloads (ring frees buffer only).
    RenderCommand cmd;
    while (ano_spsc_pop(&bridge->commands, &cmd))
        ano_render_command_release(&cmd);
    ano_spsc_destroy(&bridge->commands);
    ano_spsc_destroy(&bridge->events); // RenderEvent is wholly inline: nothing to discharge
}


/* Logic Master Endpoints */

// Public producer endpoint (anoptic_render.h). Non-inline via opaque handle.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd)
{
    return ano_spsc_push(&bridge->commands, cmd);
}

// Runtime light endpoints. POD RenderCommand -> command ring. false == full, retry.
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


/* Bulk */

// Pack: ids, pad to ANO_BULK_OVERALIGN, then over-aligned sub-arrays (mat4 / motion / userdata).
#define ANO_BULK_OVERALIGN 16u
_Static_assert(sizeof(mat4) % ANO_BULK_OVERALIGN == 0u
                   && sizeof(AnoMotionDescriptor) % ANO_BULK_OVERALIGN == 0u
                   && sizeof(AnoInstanceData) % ANO_BULK_OVERALIGN == 0u,
               "one pad after the id array carries every over-aligned bulk sub-array only while each is a whole number of ANO_BULK_OVERALIGN units");
_Static_assert(alignof(mat4) <= ANO_BULK_OVERALIGN
                   && alignof(AnoMotionDescriptor) <= ANO_BULK_OVERALIGN
                   && alignof(AnoInstanceData) <= ANO_BULK_OVERALIGN,
               "a bulk sub-array wants stricter alignment than the pack provides");

// in:  bridge, batch (caller-owned; only mask-named arrays are read)
// out: ACCEPTED with one render-owned block enqueued, or a refusal
// inv: NULL batch before count; zero count before any array touch
AnoRenderSubmitResult ano_render_submit_bulk_update(AnoRenderBridge *bridge, const RenderUpdateBatch *batch)
{
    if (batch == NULL)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    if (batch->count == 0u)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED); // documented no-op
    uint32_t count = batch->count, fields = batch->fields;
    // Absent array for a named field -> INVALID. RFIELD_LIGHT is not bulk.
    if (batch->render_ids == NULL
        || ((fields & RFIELD_TRANSFORM) && batch->transforms == NULL)
        || ((fields & RFIELD_ANIM)      && batch->motion == NULL)
        || ((fields & RFIELD_MESH_MAT)  && (batch->mesh == NULL || batch->material == NULL))
        || ((fields & RFIELD_USERDATA)  && batch->instance_data == NULL))
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    // Checked size: add_array / align_up refuse overflow.
    size_t bytes = sizeof(RenderUpdateBatch);
    if (!ano_size_add_array(&bytes, count, sizeof(uint32_t)) // ids
        || !ano_size_align_up(&bytes, ANO_BULK_OVERALIGN)    // pad
        || ((fields & RFIELD_TRANSFORM) && !ano_size_add_array(&bytes, count, sizeof(mat4)))
        || ((fields & RFIELD_ANIM)      && !ano_size_add_array(&bytes, count, sizeof(AnoMotionDescriptor)))
        || ((fields & RFIELD_USERDATA)  && !ano_size_add_array(&bytes, count, sizeof(AnoInstanceData)))
        || ((fields & RFIELD_MESH_MAT)  && (!ano_size_add_array(&bytes, count, sizeof(uint32_t))
                                            || !ano_size_add_array(&bytes, count, sizeof(uint32_t)))))
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    char *blk = mi_malloc(bytes);
    if (!blk)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_OOM);
    RenderUpdateBatch *b = (RenderUpdateBatch *)blk;
    *b = (RenderUpdateBatch){ .count = count, .fields = fields };
    char *cur = blk + sizeof(RenderUpdateBatch);
    b->render_ids = (uint32_t *)cur;
    memcpy(cur, batch->render_ids, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    // Pad to ANO_BULK_OVERALIGN (same order as the size pass).
    cur = blk + (((size_t)(cur - blk) + (ANO_BULK_OVERALIGN - 1u)) & ~(size_t)(ANO_BULK_OVERALIGN - 1u));
    if (fields & RFIELD_TRANSFORM) {
        b->transforms = (mat4 *)cur;
        memcpy(cur, batch->transforms, (size_t)count * sizeof(mat4)); cur += (size_t)count * sizeof(mat4);
    }
    if (fields & RFIELD_ANIM) {
        b->motion = (AnoMotionDescriptor *)cur;
        memcpy(cur, batch->motion, (size_t)count * sizeof(AnoMotionDescriptor)); cur += (size_t)count * sizeof(AnoMotionDescriptor);
    }
    if (fields & RFIELD_USERDATA) {
        b->instance_data = (AnoInstanceData *)cur;
        memcpy(cur, batch->instance_data, (size_t)count * sizeof(AnoInstanceData)); cur += (size_t)count * sizeof(AnoInstanceData);
    }
    if (fields & RFIELD_MESH_MAT) {
        b->mesh = (uint32_t *)cur;
        memcpy(cur, batch->mesh, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
        b->material = (uint32_t *)cur;
        memcpy(cur, batch->material, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    }
    RenderCommand cmd = { .kind = RCMD_BULK_UPDATE, .update = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) {
        ano_render_command_release(&cmd); // failed push: retire block
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED);
}

// Mass despawn into one render-owned block. Zero count before id read.
AnoRenderSubmitResult ano_render_submit_bulk_destroy(AnoRenderBridge *bridge, const uint32_t *render_ids, uint32_t count)
{
    if (count == 0u)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED); // documented no-op
    if (render_ids == NULL)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    size_t bytes = sizeof(RenderDestroyBatch);
    if (!ano_size_add_array(&bytes, count, sizeof(uint32_t)))
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    char *blk = mi_malloc(bytes);
    if (!blk)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_OOM);
    RenderDestroyBatch *b = (RenderDestroyBatch *)blk;
    uint32_t *ids = (uint32_t *)(blk + sizeof(RenderDestroyBatch));
    memcpy(ids, render_ids, (size_t)count * sizeof(uint32_t));
    b->count = count;
    b->render_ids = ids;
    RenderCommand cmd = { .kind = RCMD_BULK_DESTROY, .destroy = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) {
        ano_render_command_release(&cmd);
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED);
}


/* Screen Text */

// Clamp caps packed size at compile time.
_Static_assert(ANO_RENDER_TEXT_MAX <= (SIZE_MAX - sizeof(RenderTextBlock)) / sizeof(AnoGlyphInstance),
               "a full screen-text block must fit size_t");

// Packs header + instances into one render-owned block. count 0 -> clear.
AnoRenderSubmitResult ano_render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
                                          const AnoGlyphInstance *instances, uint32_t count)
{
    if (count == 0u)
        return ano_render_text_clear(bridge, text_id);
    if (instances == NULL)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID); // count without data
    if (count > ANO_RENDER_TEXT_MAX)
        count = ANO_RENDER_TEXT_MAX; // clamp to the region
    size_t bytes = sizeof(RenderTextBlock) + (size_t)count * sizeof(AnoGlyphInstance);
    char *blk = mi_malloc(bytes);
    if (blk == NULL)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_OOM);
    RenderTextBlock *b = (RenderTextBlock *)blk;
    AnoGlyphInstance *inst = (AnoGlyphInstance *)(blk + sizeof(RenderTextBlock));
    memcpy(inst, instances, (size_t)count * sizeof(AnoGlyphInstance));
    b->count = count;
    b->instances = inst;
    RenderCommand c = { .kind = RCMD_TEXT_SET, .text_id = text_id, .text = b, .bulk_owned = true };
    if (!ano_spsc_push(&bridge->commands, &c)) {
        ano_render_command_release(&c); // failed push: retire block
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED);
}

// Push TEXT_CLEAR. ACCEPTED or BACKPRESSURE.
AnoRenderSubmitResult ano_render_text_clear(AnoRenderBridge *bridge, uint32_t text_id)
{
    RenderCommand c = { .kind = RCMD_TEXT_CLEAR, .text_id = text_id };
    return ano_spsc_push(&bridge->commands, &c)
               ? ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED)
               : ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
}


/* UI */

// Bounds PATH prim curve walk (aux0 = stream offset, aux1 = monotone-quad count):
// start word, then per quad optional SENTINEL + restart ahead of control + end.
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

// Block-local ref check for one UI prim, including the referenced paint's stop window.
// Failure -> INVALID (deterministic).
static bool ui_prim_valid(const AnoUiPrim *p, uint32_t clips, uint32_t paints, uint32_t glyphs,
                          const uint32_t *curves, uint32_t curveCount,
                          const AnoUiPaint *paintTab, uint32_t stops)
{
    if (p->clipRef != ANO_UI_REF_NONE && p->clipRef >= clips)
        return false;
    if (p->paintRef != ANO_UI_REF_NONE) {
        if (p->paintRef >= paints)
            return false;
        const AnoUiPaint *pa = &paintTab[p->paintRef];
        // Window by subtraction: stopFirst + stopCount.
        if (pa->stopFirst > stops || pa->stopCount > stops - pa->stopFirst)
            return false;
    }
    if (p->kind == ANO_UI_GLYPHS && (p->aux0 > glyphs || p->aux1 > glyphs - p->aux0))
        return false;
    if (p->kind == ANO_UI_PATH && !ui_path_walk_valid(curves, curveCount, p->aux0, p->aux1))
        return false;
    return true;
}

// Cap refusals bound packed size at compile time.
_Static_assert((size_t)ANO_RENDER_UI_MAX_PRIMS  * sizeof(AnoUiPrim)
             + (size_t)ANO_RENDER_UI_MAX_CLIPS  * sizeof(AnoUiClip)
             + (size_t)ANO_RENDER_UI_MAX_PAINTS * sizeof(AnoUiPaint)
             + (size_t)ANO_RENDER_UI_MAX_STOPS  * sizeof(AnoUiStop)
             + (size_t)ANO_RENDER_UI_MAX_CURVES * sizeof(uint32_t)
             + (size_t)ANO_RENDER_UI_MAX_GLYPHS * sizeof(AnoGlyphInstance)
               <= SIZE_MAX - sizeof(RenderUiBlock),
               "a maximal UI block must fit size_t");

// Packs tables + glyphs into one render-owned block.
// Zero prims -> clear. NULL builder -> INVALID.
AnoRenderSubmitResult ano_render_ui_set(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t layer,
                                        const AnoUiBuilder *ui,
                                        const AnoGlyphInstance *glyphs, uint32_t glyphCount)
{
    if (ui == NULL)
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    if (ui->primCount == 0u)
        return ano_render_ui_clear(bridge, ui_id);
    // Caps, glyph pair, or nonzero count over a NULL table -> INVALID.
    if (ui->primCount > ANO_RENDER_UI_MAX_PRIMS || ui->clipCount > ANO_RENDER_UI_MAX_CLIPS
        || ui->paintCount > ANO_RENDER_UI_MAX_PAINTS || ui->stopCount > ANO_RENDER_UI_MAX_STOPS
        || ui->curveCount > ANO_RENDER_UI_MAX_CURVES
        || glyphCount > ANO_RENDER_UI_MAX_GLYPHS || (glyphCount > 0u && glyphs == NULL)
        || ui->prims == NULL
        || (ui->clipCount  > 0u && ui->clips  == NULL)
        || (ui->paintCount > 0u && ui->paints == NULL)
        || (ui->stopCount  > 0u && ui->stops  == NULL)
        || (ui->curveCount > 0u && ui->curves == NULL)) {
        ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (per-block caps or a count over an absent table).", ui_id);
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
    }
    for (uint32_t i = 0; i < ui->primCount; i++) {
        if (!ui_prim_valid(&ui->prims[i], ui->clipCount, ui->paintCount, glyphCount,
                           ui->curves, ui->curveCount, ui->paints, ui->stopCount)) {
            ano_log(ANO_WARN, "UI bridge: ui_id %u dropped (prim %u invalid).", ui_id, i);
            return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
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
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_OOM);
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
        ano_render_command_release(&c); // failed push: retire block
        return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED);
}

// Push UI_CLEAR. ACCEPTED or BACKPRESSURE.
AnoRenderSubmitResult ano_render_ui_clear(AnoRenderBridge *bridge, uint32_t ui_id)
{
    RenderCommand c = { .kind = RCMD_UI_CLEAR, .ui_id = ui_id };
    return ano_spsc_push(&bridge->commands, &c)
               ? ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED)
               : ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_BACKPRESSURE);
}


/* Back-Channel */

// Logic-master endpoints (anoptic_render.h). Non-inline via opaque handle.
bool ano_render_poll_event(AnoRenderBridge *bridge, RenderEvent *out)
{
    return ano_spsc_pop(&bridge->events, out);
}

bool ano_render_acquire_snapshot(AnoRenderBridge *bridge, RenderSnapshot *out)
{
    return ano_seqpub_load(bridge->snapshot, &bridge->snapshotVersion, out, sizeof *out);
}

// Accept-form compares need float fields.
_Static_assert(_Generic(((AnoViewState *)0)->eye[0],    float: 1, default: 0)
                   && _Generic(((AnoViewState *)0)->center[0], float: 1, default: 0)
                   && _Generic(((AnoViewState *)0)->up[0],     float: 1, default: 0)
                   && _Generic(((AnoViewState *)0)->fovYDeg,   float: 1, default: 0),
               "view pose guard assumes float eye/center/up/fovYDeg");
_Static_assert(sizeof ((AnoViewState *)0)->eye    == 3u * sizeof(float)
                   && sizeof ((AnoViewState *)0)->center == 3u * sizeof(float)
                   && sizeof ((AnoViewState *)0)->up     == 3u * sizeof(float),
               "view pose guard reads 3-component eye/center/up");

// in:  view (logic-published camera pose)
// out: true if lookAt(eye, center, up) yields a finite orthonormal basis
// inv: no sqrt, no divide. Accept-form: NaN/inf fails every compare -> reject.
static bool view_pose_valid(const AnoViewState *view)
{
    const float eps2 = 1e-8f; // sin(theta)^2 floor: ~0.006 deg off-parallel
    float d[3] = { view->center[0] - view->eye[0],
                   view->center[1] - view->eye[1],
                   view->center[2] - view->eye[2] };
    const float *u = view->up;
    float c[3] = { d[1] * u[2] - d[2] * u[1],
                   d[2] * u[0] - d[0] * u[2],
                   d[0] * u[1] - d[1] * u[0] };
    float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    float u2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    float c2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    // |d x u|^2 > eps2 |d|^2 |u|^2 rejects eye==center, zero up, up parallel to forward, NaN/inf.
    if (!(c2 > eps2 * d2 * u2)) return false;
    return view->fovYDeg > 0.0f && view->fovYDeg < 180.0f; // the other NaN inlet into proj
}

// in:  bridge, view (logic-owned camera pose)
// out: nothing; degenerate pose dropped, last accepted stands
// inv: single producer. Validity checked here once.
void ano_render_publish_view(AnoRenderBridge *bridge, const AnoViewState *view)
{
    if (!view_pose_valid(view)) {
        if (!bridge->viewRejectWarned) {
            bridge->viewRejectWarned = true;
            ano_log(ANO_WARN, "Render bridge: degenerate camera pose rejected at seq %llu "
                              "(coincident eye/center, zero or parallel up, bad fovY, or "
                              "non-finite field); previous pose stands.",
                    (unsigned long long)view->seq);
        }
        return;
    }
    ano_seqpub_store(bridge->viewState, &bridge->viewStateVersion, view, sizeof *view);
}
