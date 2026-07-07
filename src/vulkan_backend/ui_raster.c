/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane (docs/ui/ui-render.md): per-frame table buffers on raster-set
// bindings 4-7, the logic-block registry with its compose (layer-sorted, block-local
// refs rebased), and the per-slot refresh. The prim math lives in src/ui and its GLSL
// twin in uicoverage.glsl; the dispatch itself is the text raster's.

#include "vulkan_backend/ui_raster.h"
#include "vulkan_backend/instance/instanceInit.h"
#include "vulkan_backend/text_raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_logging.h>
#include <anoptic_memory.h>
#include <anoptic_ui.h>

// Region layout inside one uiFrameBuffer, in binding order 4-7. Offsets must satisfy
// minStorageBufferOffsetAlignment; the spec caps that limit at 256, which every
// region size below is a multiple of.
#define ANO_UI_PRIM_BYTES  (ANO_UI_MAX_PRIMS * (uint32_t)sizeof(AnoUiPrim))
#define ANO_UI_CLIP_OFF    ANO_UI_PRIM_BYTES
#define ANO_UI_CLIP_BYTES  (ANO_UI_MAX_CLIPS * (uint32_t)sizeof(AnoUiClip))
#define ANO_UI_PAINT_OFF   (ANO_UI_CLIP_OFF + ANO_UI_CLIP_BYTES)
#define ANO_UI_PAINT_BYTES (ANO_UI_MAX_PAINTS * (uint32_t)sizeof(AnoUiPaint))
#define ANO_UI_STOP_OFF    (ANO_UI_PAINT_OFF + ANO_UI_PAINT_BYTES)
#define ANO_UI_STOP_BYTES  (ANO_UI_MAX_STOPS * (uint32_t)sizeof(AnoUiStop))
#define ANO_UI_CURVE_OFF   (ANO_UI_STOP_OFF + ANO_UI_STOP_BYTES)
#define ANO_UI_CURVE_BYTES (ANO_UI_MAX_CURVE_WORDS * 4u)
#define ANO_UI_FRAME_BYTES (ANO_UI_CURVE_OFF + ANO_UI_CURVE_BYTES)

static_assert((ANO_UI_CLIP_OFF % 256u) == 0 && (ANO_UI_PAINT_OFF % 256u) == 0
                  && (ANO_UI_STOP_OFF % 256u) == 0 && (ANO_UI_CURVE_OFF % 256u) == 0,
              "region offsets must satisfy the worst-case storage-buffer alignment");

// Conservative pixel AABB of the pending prims (shadow prims reach 3 sigma; identity
// inv assumption of v0). Inverted bounds when nothing is composed.
static void ui_pending_bounds(RendererState* state)
{
    float b0 = 3.0e38f, b1 = 3.0e38f, b2 = -3.0e38f, b3 = -3.0e38f;
    for (uint32_t i = 0; i < state->uiPendingPrimCount; i++)
    {
        const AnoUiPrim* p = &state->uiPendingPrims[i];
        float pad = p->kind == ANO_UI_SHADOW ? 3.0f * p->param[0] + 1.0f : 1.0f;
        b0 = fminf(b0, p->origin[0] - p->half[0] - pad);
        b1 = fminf(b1, p->origin[1] - p->half[1] - pad);
        b2 = fmaxf(b2, p->origin[0] + p->half[0] + pad);
        b3 = fmaxf(b3, p->origin[1] + p->half[1] + pad);
    }
    state->uiPendingBounds[0] = b0; state->uiPendingBounds[1] = b1;
    state->uiPendingBounds[2] = b2; state->uiPendingBounds[3] = b3;
}

// Recomposes the pending tables from the live blocks: ascending layer (registry
// creation order breaks ties), block-local clip/paint/stop/glyph references rebased,
// scroll folded into every position. A block that would overflow a table budget is
// skipped WHOLE — truncation would corrupt references. Bumps uiVersion.
static void ui_compose(RendererState* state)
{
    if (state->uiPendingPrims == NULL || state->uiPinned)
        return;
    uint32_t order[ANO_UI_MAX_BLOCKS];
    for (uint32_t i = 0; i < state->uiBlockCount; i++)
        order[i] = i;
    for (uint32_t i = 1; i < state->uiBlockCount; i++)
    { // stable insertion sort by layer
        uint32_t v = order[i], key = state->uiBlocks[v].blk->layer;
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && state->uiBlocks[order[j]].blk->layer > key)
        {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    uint32_t np = 0, nc = 0, na = 0, ns = 0, ng = 0, ncw = 0;
    for (uint32_t oi = 0; oi < state->uiBlockCount; oi++)
    {
        const RenderUiBlock* blk = state->uiBlocks[order[oi]].blk;
        if (np + blk->primCount > ANO_UI_MAX_PRIMS || nc + blk->clipCount > ANO_UI_MAX_CLIPS
            || na + blk->paintCount > ANO_UI_MAX_PAINTS || ns + blk->stopCount > ANO_UI_MAX_STOPS
            || ncw + blk->curveCount > ANO_UI_MAX_CURVE_WORDS
            || ng + blk->glyphCount > ANO_UI_MAX_GLYPHS)
        {
            ano_log(ANO_WARN, "UI compose: ui_id %u skipped (table budget).",
                    state->uiBlocks[order[oi]].id);
            continue;
        }
        float sx = blk->scroll[0], sy = blk->scroll[1];
        for (uint32_t i = 0; i < blk->primCount; i++)
        {
            AnoUiPrim p = blk->prims[i];
            p.origin[0] += sx;
            p.origin[1] += sy;
            if (p.clipRef != ANO_UI_REF_NONE)
                p.clipRef += nc;
            if (p.paintRef != ANO_UI_REF_NONE)
                p.paintRef += na;
            if (p.kind == ANO_UI_GLYPHS)
                p.aux0 = ANO_UI_GLYPH_FIRST + ng + p.aux0;
            else if (p.kind == ANO_UI_PATH)
                p.aux0 += ncw; // block-local word offset -> pending-buffer offset
            state->uiPendingPrims[np++] = p;
        }
        for (uint32_t i = 0; i < blk->clipCount; i++)
        {
            AnoUiClip c = blk->clips[i];
            c.rect[0] += sx; c.rect[1] += sy;
            c.rect[2] += sx; c.rect[3] += sy;
            c.rrCenter[0] += sx;
            c.rrCenter[1] += sy;
            state->uiPendingClips[nc++] = c;
        }
        for (uint32_t i = 0; i < blk->paintCount; i++)
        {
            AnoUiPaint pa = blk->paints[i];
            pa.stopFirst += ns;
            // Scroll translates the gradient with content: shift the xform origin so a
            // point at overlay (x+sx) reads the pre-scroll value at x.
            pa.xform[2] -= pa.xform[0] * sx + pa.xform[1] * sy;
            pa.xform[5] -= pa.xform[3] * sx + pa.xform[4] * sy;
            state->uiPendingPaints[na++] = pa;
        }
        memcpy(&state->uiPendingStops[ns], blk->stops, (size_t)blk->stopCount * sizeof(AnoUiStop));
        ns += blk->stopCount;
        memcpy(&state->uiPendingCurves[ncw], blk->curves, (size_t)blk->curveCount * sizeof(uint32_t));
        ncw += blk->curveCount;
        for (uint32_t i = 0; i < blk->glyphCount; i++)
        {
            AnoGlyphInstance g = blk->glyphs[i];
            g.origin[0] += sx;
            g.origin[1] += sy;
            state->uiPendingGlyphs[ng++] = g;
        }
    }
    state->uiPendingPrimCount = np;
    state->uiPendingClipCount = nc;
    state->uiPendingPaintCount = na;
    state->uiPendingStopCount = ns;
    state->uiPendingCurveCount = ncw;
    state->uiPendingGlyphCount = ng;
    ui_pending_bounds(state);
    state->uiVersion++;
}

// Composes the standing demo scene into the pending tables and PINS composition, so
// the self-test canvas stays exactly this regardless of bridge traffic. The live
// variant adds one IMAGE prim (bindless index 0); the self-test stays inside the
// reference evaluator's reach.
static void ui_compose_demo(RendererState* state, bool selftest)
{
    AnoUiBuilder b;
    ano_ui_builder_init(&b, state->uiPendingPrims, 32, state->uiPendingClips, 4,
                        state->uiPendingPaints, ANO_UI_MAX_PAINTS,
                        state->uiPendingStops, ANO_UI_MAX_STOPS);
    ano_ui_builder_curves(&b, state->uiPendingCurves, ANO_UI_MAX_CURVE_WORDS);
    ano_ui_demo_scene(&b, 48.0f, 120.0f);
    if (!selftest)
    {
        float r12[4] = { 12, 12, 12, 12 };
        float white[4] = { 1, 1, 1, 1 };
        ano_ui_image(&b, (float[2]){ 428.0f, 120.0f }, (float[2]){ 568.0f, 225.0f }, r12,
                     0, 0.0f, white, ANO_UI_REF_NONE, 0);
    }
    state->uiPendingPrimCount = b.primCount;
    state->uiPendingClipCount = b.clipCount;
    state->uiPendingPaintCount = b.paintCount;
    state->uiPendingStopCount = b.stopCount;
    state->uiPendingCurveCount = b.curveCount;
    state->uiPendingGlyphCount = 0;
    ui_pending_bounds(state);
    state->uiVersion++;
    state->uiPinned = true;
    ano_log(ANO_INFO, "UI overlay: demo scene composed (%u prims, %u clips%s, pinned)",
            b.primCount, b.clipCount, selftest ? ", self-test" : "");
}

// In: ctx/state after ano_vk_text_init (the shared raster set is created later, in
// ano_vk_text_create_sets; only the buffers must exist by then). Out: true always;
// failure clears uiOverlay and leaves the fallback binding path to ui_write_sets.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
    {
        // No shared dispatch to ride; the gate log already said why.
        state->uiOverlay = false;
        return true;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (!createDataBuffer(ctx, &gpuAllocator, ANO_UI_FRAME_BYTES,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &state->frames[i].uiFrameBuffer, &state->frames[i].uiFrameAlloc))
        {
            ano_log(ANO_WARN, "UI overlay disabled: table buffer creation failed.");
            state->uiOverlay = false;
            return true;
        }
        state->frames[i].uiFrameMapped = state->frames[i].uiFrameAlloc.mapped;
    }

    // Pending compose tables on the text heap (dies with it at teardown).
    if (state->uiOverlay)
    {
        state->uiPendingPrims = mi_heap_malloc(state->textHeap, ANO_UI_PRIM_BYTES);
        state->uiPendingClips = mi_heap_malloc(state->textHeap, ANO_UI_CLIP_BYTES);
        state->uiPendingPaints = mi_heap_malloc(state->textHeap, ANO_UI_PAINT_BYTES);
        state->uiPendingStops = mi_heap_malloc(state->textHeap, ANO_UI_STOP_BYTES);
        state->uiPendingCurves = mi_heap_malloc(state->textHeap, ANO_UI_CURVE_BYTES);
        state->uiPendingGlyphs = mi_heap_malloc(state->textHeap,
                                                ANO_UI_MAX_GLYPHS * sizeof(AnoGlyphInstance));
        if (!state->uiPendingPrims || !state->uiPendingClips || !state->uiPendingPaints
            || !state->uiPendingStops || !state->uiPendingCurves || !state->uiPendingGlyphs)
        {
            ano_log(ANO_WARN, "UI overlay disabled: pending table allocation failed.");
            state->uiPendingPrims = NULL;
            state->uiOverlay = false;
        }
    }
    ano_log(ANO_INFO, "UI overlay: tables resident (%u KiB x%u slots)%s",
            ANO_UI_FRAME_BYTES / 1024u, (unsigned)MAX_FRAMES_IN_FLIGHT,
            state->uiOverlay ? "" : ", compose pinned off");

    // Standing demo (ANO_UI_DEMO); ANO_UI_OPAQUE additionally pins the screenshot
    // self-test: opaque backdrop, full-canvas dispatch, glyph loop skipped.
    bool selftest = getenv("ANO_UI_OPAQUE") != NULL;
    if (selftest)
        state->textFlags |= ANO_TEXT_RASTER_OPAQUE | ANO_TEXT_RASTER_UIONLY | ANO_TEXT_RASTER_NODITHER;
    if (state->uiOverlay && (selftest || getenv("ANO_UI_DEMO") != NULL))
        ui_compose_demo(state, selftest);
    return true;
}

// Adopts a logic-submitted block (RCMD_UI_SET), replacing ui_id's contents. Ownership
// of blk transfers here, freed on replace/clear/teardown or drop. Render thread only.
void ano_vk_ui_block_set(RendererState* state, uint32_t ui_id, const RenderUiBlock* blk)
{
    if (blk == NULL)
        return;
    if (!state->uiOverlay || state->uiPendingPrims == NULL)
    {
        mi_free((void*)blk);
        return;
    }
    for (uint32_t i = 0; i < state->uiBlockCount; i++)
    {
        if (state->uiBlocks[i].id == ui_id)
        {
            mi_free((void*)state->uiBlocks[i].blk);
            state->uiBlocks[i].blk = blk;
            ui_compose(state);
            return;
        }
    }
    if (state->uiBlockCount >= ANO_UI_MAX_BLOCKS)
    {
        ano_log(ANO_WARN, "UI bridge: block registry full (%u); ui_id %u dropped.",
                ANO_UI_MAX_BLOCKS, ui_id);
        mi_free((void*)blk);
        return;
    }
    state->uiBlocks[state->uiBlockCount].id = ui_id;
    state->uiBlocks[state->uiBlockCount].blk = blk;
    state->uiBlockCount++;
    ui_compose(state);
}

// Removes a block (RCMD_UI_CLEAR), idempotent. Order-preserving compaction.
void ano_vk_ui_block_clear(RendererState* state, uint32_t ui_id)
{
    for (uint32_t i = 0; i < state->uiBlockCount; i++)
    {
        if (state->uiBlocks[i].id != ui_id)
            continue;
        mi_free((void*)state->uiBlocks[i].blk);
        for (uint32_t j = i + 1; j < state->uiBlockCount; j++)
            state->uiBlocks[j - 1] = state->uiBlocks[j];
        state->uiBlockCount--;
        if (state->uiOverlay && state->uiPendingPrims != NULL)
            ui_compose(state);
        return;
    }
}

// Copies pending tables into this slot's mapped buffers when stale and publishes the
// slot-current counts/bounds the record path pushes. Glyph labels land in the text
// frame buffer's UI region [ANO_UI_GLYPH_FIRST, +ANO_UI_MAX_GLYPHS) — above the
// pending and world regions, so neither the plain glyph loop nor the world draw
// touches them.
void ano_vk_ui_frame_refresh(RendererState* state, uint32_t frameIndex)
{
    if (!state->uiOverlay || state->uiPendingPrims == NULL)
        return;
    PerFrameResources* fr = &state->frames[frameIndex];
    if (fr->uiSlotVersion != state->uiVersion && fr->uiFrameMapped != NULL)
    {
        uint8_t* dst = fr->uiFrameMapped;
        memcpy(dst, state->uiPendingPrims,
               (size_t)state->uiPendingPrimCount * sizeof(AnoUiPrim));
        memcpy(dst + ANO_UI_CLIP_OFF, state->uiPendingClips,
               (size_t)state->uiPendingClipCount * sizeof(AnoUiClip));
        memcpy(dst + ANO_UI_PAINT_OFF, state->uiPendingPaints,
               (size_t)state->uiPendingPaintCount * sizeof(AnoUiPaint));
        memcpy(dst + ANO_UI_STOP_OFF, state->uiPendingStops,
               (size_t)state->uiPendingStopCount * sizeof(AnoUiStop));
        memcpy(dst + ANO_UI_CURVE_OFF, state->uiPendingCurves,
               (size_t)state->uiPendingCurveCount * sizeof(uint32_t));
        if (fr->textFrameMapped != NULL)
            memcpy((AnoGlyphInstance*)fr->textFrameMapped + ANO_UI_GLYPH_FIRST,
                   state->uiPendingGlyphs,
                   (size_t)state->uiPendingGlyphCount * sizeof(AnoGlyphInstance));
        fr->uiSlotVersion = state->uiVersion;
    }
    state->uiPrimCount = state->uiPendingPrimCount;
    state->uiClipCount = state->uiPendingClipCount;
    state->uiPaintCount = state->uiPendingPaintCount;
    for (int k = 0; k < 4; k++)
        state->uiBounds[k] = state->uiPendingBounds[k];
}

// Writes bindings 4-7 of every slot's shared raster set. When a slot's table buffer
// is absent (creation failure), the bindings point at the slot's text frame buffer:
// any valid SSBO keeps the set legal, and pinned-zero counts mean it is never read.
void ano_vk_ui_write_sets(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];
        VkDescriptorBufferInfo infos[5];
        if (fr->uiFrameBuffer != VK_NULL_HANDLE)
        {
            infos[0] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, 0, ANO_UI_PRIM_BYTES };
            infos[1] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_CLIP_OFF, ANO_UI_CLIP_BYTES };
            infos[2] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_PAINT_OFF, ANO_UI_PAINT_BYTES };
            infos[3] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_STOP_OFF, ANO_UI_STOP_BYTES };
            infos[4] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_CURVE_OFF, ANO_UI_CURVE_BYTES };
        }
        else
        {
            for (int k = 0; k < 5; k++)
                infos[k] = (VkDescriptorBufferInfo){ fr->textFrameBuffer, 0, VK_WHOLE_SIZE };
        }
        VkWriteDescriptorSet writes[5] = {};
        for (uint32_t w = 0; w < 5; ++w)
        {
            writes[w].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[w].dstSet = fr->textRasterSet;
            writes[w].dstBinding = 4 + w;
            writes[w].descriptorCount = 1;
            writes[w].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[w].pBufferInfo = &infos[w];
        }
        vkUpdateDescriptorSets(ctx->device, 5, writes, 0, NULL);
    }
}

void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < state->uiBlockCount; i++)
        mi_free((void*)state->uiBlocks[i].blk);
    state->uiBlockCount = 0;
    // Pending tables die with the text heap (torn down in ano_vk_text_destroy).
    state->uiPendingPrims = NULL;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (state->frames[i].uiFrameBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx->device, state->frames[i].uiFrameBuffer, NULL);
            state->frames[i].uiFrameBuffer = VK_NULL_HANDLE;
            state->frames[i].uiFrameMapped = NULL;
        }
    }
}
