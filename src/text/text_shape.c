/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Shaper (FONT_RENDER.md step 4): UTF-8 rune iteration, slot lookup, horizontal
// advances with pair kerning, newline handling. Pure functions over an immutable
// AnoFontBake, callable from any thread. Ligatures, marks, and bidi are non-goals.

#include "anoptic_text.h"
#include "anoptic_strings_utf.h"
#include "text/text_internal.h"

#include <math.h>

uint32_t ano_text_bake_slot(const AnoFontBake *bake, uint32_t codepoint)
{
    uint32_t lo = 0, hi = bake->rangeCount;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (bake->ranges[mid].last < codepoint)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo >= bake->rangeCount || codepoint < bake->ranges[lo].first)
        return ANO_TEXT_SLOT_NONE;
    return bake->ranges[lo].slotBase + (codepoint - bake->ranges[lo].first);
}

float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot)
{
    if (bake == NULL || bake->kernCount == 0
        || leftSlot >= bake->glyphCount || rightSlot >= bake->glyphCount)
        return 0.0f;
    uint32_t key = leftSlot << 16 | rightSlot;
    uint32_t lo = 0, hi = bake->kernCount;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (bake->kerns[mid].key < key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return (lo < bake->kernCount && bake->kerns[lo].key == key) ? bake->kerns[lo].xAdvance
                                                                : 0.0f;
}

// The single pen walk behind shape/measure x plain/runs. Assumes validated args.
// One pen crosses run boundaries untouched. The pair-kern chain survives a boundary
// iff the size is unchanged. Returns the total instance count. Optionally reports the
// pen, the widest line, started-line count, and the last run's line step.
static uint32_t shape_core(const AnoFontBake *bake, anostr_t text,
                           const AnoTextRun *runs, uint32_t runCount,
                           const float origin[2], AnoGlyphInstance *out, uint32_t cap,
                           float *penOut, float *maxWOut, uint32_t *linesOut,
                           float *endStepOut)
{
    size_t total = anostr_len(text);
    float penX = origin[0], penY = origin[1];
    float maxW = 0.0f;
    uint32_t lines = total > 0 ? 1u : 0u;
    uint32_t needed = 0, emitted = 0;
    size_t runEnd = runs[0].byteCount;
    uint32_t runIdx = 0;
    uint32_t prevSlot = UINT32_MAX; // pair-kern chain, broken by newline/gap/size change
    float prevSize = 0.0f;          // the sizePx that shaped prevSlot

    for (size_t i = 0; i < total;)
    {
        while (i >= runEnd && runIdx + 1 < runCount)
        {
            runIdx++;
            runEnd += runs[runIdx].byteCount;
        }
        float sizePx = runs[runIdx].sizePx; // the lead byte's run styles the codepoint
        anorune_t cp = anostr_rune_next(text, &i);
        if (cp == '\r')
            continue;
        if (cp == '\n')
        {
            maxW = fmaxf(maxW, penX - origin[0]);
            penX = origin[0];
            penY += bake->lineHeight * sizePx;
            lines++;
            prevSlot = UINT32_MAX;
            continue;
        }
        uint32_t slot = ano_text_bake_slot(bake, cp);
        if (slot == ANO_TEXT_SLOT_NONE)
        {
            penX += ANO_TEXT_GAP_EM * sizePx;
            prevSlot = UINT32_MAX;
            continue;
        }
        if (prevSlot != UINT32_MAX && sizePx == prevSize)
            penX += ano_text_kern(bake, prevSlot, slot) * sizePx;
        prevSlot = slot;
        prevSize = sizePx;
        const AnoGlyphEntry *e = &bake->glyphs[slot];
        if (e->curveCount > 0)
        {
            needed++;
            if (out != NULL && emitted < cap)
            {
                out[emitted++] = (AnoGlyphInstance){
                    .inv     = { 1.0f / sizePx, 0.0f, 0.0f, -1.0f / sizePx },
                    .color   = { runs[runIdx].color[0], runs[runIdx].color[1],
                                 runs[runIdx].color[2], runs[runIdx].color[3] },
                    .origin  = { penX, penY },
                    .glyphID = slot,
                    .flags   = 0,
                };
            }
        }
        penX += e->advance * sizePx;
    }
    maxW = fmaxf(maxW, penX - origin[0]);
    if (penOut != NULL)
    {
        penOut[0] = penX;
        penOut[1] = penY;
    }
    if (maxWOut != NULL)
        *maxWOut = maxW;
    if (linesOut != NULL)
        *linesOut = lines;
    if (endStepOut != NULL)
        *endStepOut = bake->lineHeight * runs[runCount - 1].sizePx;
    return needed;
}

// Rejects NULL runs, an empty run list, any non-positive size, and a byteCount sum
// that disagrees with the text's byte length.
static bool runs_valid(const AnoTextRun *runs, uint32_t runCount, anostr_t text)
{
    if (runs == NULL || runCount == 0)
        return false;
    uint64_t sum = 0;
    for (uint32_t r = 0; r < runCount; r++)
    {
        if (runs[r].sizePx <= 0.0f)
            return false;
        sum += runs[r].byteCount;
    }
    return sum == anostr_len(text);
}

uint32_t ano_text_shape(const AnoFontBake *bake, anostr_t text,
                        float sizePx, const float origin[2], const float color[4],
                        AnoGlyphInstance *out, uint32_t cap, float *penOut)
{
    if (bake == NULL || origin == NULL || color == NULL || sizePx <= 0.0f)
        return 0;
    AnoTextRun run = { .byteCount = (uint32_t)anostr_len(text), .sizePx = sizePx,
                       .color = { color[0], color[1], color[2], color[3] } };
    return shape_core(bake, text, &run, 1, origin, out, cap, penOut, NULL, NULL, NULL);
}

uint32_t ano_text_shape_runs(const AnoFontBake *bake, anostr_t text,
                             const AnoTextRun *runs, uint32_t runCount,
                             const float origin[2],
                             AnoGlyphInstance *out, uint32_t cap, float *penOut)
{
    if (bake == NULL || origin == NULL || !runs_valid(runs, runCount, text))
        return 0;
    return shape_core(bake, text, runs, runCount, origin, out, cap, penOut,
                      NULL, NULL, NULL);
}

void ano_text_measure(const AnoFontBake *bake, anostr_t text,
                      float sizePx, float *width, float *height)
{
    float maxW = 0.0f;
    uint32_t lines = 0;
    if (bake != NULL && sizePx > 0.0f && anostr_len(text) > 0)
    {
        AnoTextRun run = { .byteCount = (uint32_t)anostr_len(text), .sizePx = sizePx };
        const float zero[2] = { 0.0f, 0.0f };
        shape_core(bake, text, &run, 1, zero, NULL, 0, NULL, &maxW, &lines, NULL);
    }
    if (width != NULL)
        *width = maxW;
    if (height != NULL)
        *height = (float)lines * (bake != NULL ? bake->lineHeight : 0.0f) * sizePx;
}

void ano_text_measure_runs(const AnoFontBake *bake, anostr_t text,
                           const AnoTextRun *runs, uint32_t runCount,
                           float *width, float *height)
{
    float maxW = 0.0f, h = 0.0f;
    if (bake != NULL && runs_valid(runs, runCount, text) && anostr_len(text) > 0)
    {
        const float zero[2] = { 0.0f, 0.0f };
        float pen[2], endStep;
        shape_core(bake, text, runs, runCount, zero, NULL, 0, pen, &maxW, NULL,
                   &endStep);
        h = pen[1] + endStep;
    }
    if (width != NULL)
        *width = maxW;
    if (height != NULL)
        *height = h;
}
