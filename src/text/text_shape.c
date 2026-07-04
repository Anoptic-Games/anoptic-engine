/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Shaper (FONT_RENDER.md step 4 + v1 kerning): UTF-8 decode, cmap-by-range lookup,
// horizontal advances with GPOS pair kerning, newline handling. Pure functions over an
// immutable AnoFontBake -- no FreeType, no module state, callable from any thread.
// Ligatures, marks, and bidi are explicit non-goals.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <math.h>

// Strict UTF-8 decode; see text_internal.h for the contract. The resync rule: a
// structurally complete sequence consumes fully even when its VALUE is rejected
// (overlong/surrogate/out-of-range); broken structure consumes one byte.
uint32_t ano_utf8_next(const char *s, uint32_t len, uint32_t *consumed)
{
    const uint8_t *b = (const uint8_t *)s;
    *consumed = 1;
    uint8_t lead = b[0];
    if (lead < 0x80)
        return lead;

    uint32_t cp, tail;
    if ((lead & 0xE0) == 0xC0)
    {
        cp = lead & 0x1Fu;
        tail = 1;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        cp = lead & 0x0Fu;
        tail = 2;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        cp = lead & 0x07u;
        tail = 3;
    }
    else
        return 0xFFFD; // stray continuation or invalid lead

    if (tail >= len)
        return 0xFFFD; // truncated at the buffer end
    for (uint32_t i = 1; i <= tail; i++)
    {
        if ((b[i] & 0xC0) != 0x80)
            return 0xFFFD; // broken continuation: resync on the next byte
        cp = (cp << 6) | (b[i] & 0x3Fu);
    }
    *consumed = tail + 1;

    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        return 0xFFFD;
    if ((tail == 1 && cp < 0x80u) || (tail == 2 && cp < 0x800u) || (tail == 3 && cp < 0x10000u))
        return 0xFFFD; // overlong encoding
    return cp;
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

uint32_t ano_text_shape(const AnoFontBake *bake, const char *utf8, uint32_t len,
                        float sizePx, const float origin[2], const float color[4],
                        AnoGlyphInstance *out, uint32_t cap, float *penOut)
{
    if (bake == NULL || utf8 == NULL || origin == NULL || color == NULL || sizePx <= 0.0f)
        return 0;

    float penX = origin[0], penY = origin[1];
    float lineStep = bake->lineHeight * sizePx;
    uint32_t needed = 0, emitted = 0;
    uint32_t prevSlot = UINT32_MAX; // pair-kern chain; newline/gap resets it

    for (uint32_t i = 0; i < len;)
    {
        uint32_t used;
        uint32_t cp = ano_utf8_next(utf8 + i, len - i, &used);
        i += used;
        if (cp == '\r')
            continue;
        if (cp == '\n')
        {
            penX = origin[0];
            penY += lineStep;
            prevSlot = UINT32_MAX;
            continue;
        }
        if (cp < bake->firstCodepoint || cp - bake->firstCodepoint >= bake->glyphCount)
        {
            penX += ANO_TEXT_GAP_EM * sizePx;
            prevSlot = UINT32_MAX;
            continue;
        }
        uint32_t slot = cp - bake->firstCodepoint;
        if (prevSlot != UINT32_MAX)
            penX += ano_text_kern(bake, prevSlot, slot) * sizePx;
        prevSlot = slot;
        const AnoGlyphEntry *e = &bake->glyphs[slot];
        if (e->curveCount > 0)
        {
            needed++;
            if (out != NULL && emitted < cap)
            {
                out[emitted++] = (AnoGlyphInstance){
                    .inv     = { 1.0f / sizePx, 0.0f, 0.0f, -1.0f / sizePx },
                    .color   = { color[0], color[1], color[2], color[3] },
                    .origin  = { penX, penY },
                    .glyphID = slot,
                    .flags   = 0,
                };
            }
        }
        penX += e->advance * sizePx;
    }
    if (penOut != NULL)
    {
        penOut[0] = penX;
        penOut[1] = penY;
    }
    return needed;
}

void ano_text_measure(const AnoFontBake *bake, const char *utf8, uint32_t len,
                      float sizePx, float *width, float *height)
{
    float w = 0.0f, maxW = 0.0f;
    uint32_t lines = 0;
    if (bake != NULL && utf8 != NULL && sizePx > 0.0f && len > 0)
    {
        lines = 1;
        uint32_t prevSlot = UINT32_MAX; // mirrors ano_text_shape's pair chain exactly
        for (uint32_t i = 0; i < len;)
        {
            uint32_t used;
            uint32_t cp = ano_utf8_next(utf8 + i, len - i, &used);
            i += used;
            if (cp == '\r')
                continue;
            if (cp == '\n')
            {
                maxW = fmaxf(maxW, w);
                w = 0.0f;
                lines++;
                prevSlot = UINT32_MAX;
                continue;
            }
            if (cp < bake->firstCodepoint || cp - bake->firstCodepoint >= bake->glyphCount)
            {
                w += ANO_TEXT_GAP_EM * sizePx;
                prevSlot = UINT32_MAX;
                continue;
            }
            uint32_t slot = cp - bake->firstCodepoint;
            if (prevSlot != UINT32_MAX)
                w += ano_text_kern(bake, prevSlot, slot) * sizePx;
            prevSlot = slot;
            w += bake->glyphs[slot].advance * sizePx;
        }
        maxW = fmaxf(maxW, w);
    }
    if (width != NULL)
        *width = maxW;
    if (height != NULL)
        *height = (float)lines * (bake != NULL ? bake->lineHeight : 0.0f) * sizePx;
}
