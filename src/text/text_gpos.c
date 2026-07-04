/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// GPOS PairPos kerning reader (shaper v1, FONT_RENDER.md section 4). Parses a raw GPOS
// table -- untrusted bytes, every read bounds-checked, malformed input fails soft --
// and accumulates horizontal 'kern' xAdvance adjustments for a slot range into a dense
// FUnit matrix. Deliberately FreeType-free: the bake hands in the blob and the
// slot->glyph-id map, and the white-box test hands in synthetic tables.
//
// Semantics (matching the reference behavior of real shapers on real fonts):
//   - script 'latn', falling back to 'DFLT'; that script's default LangSys only.
//   - feature tag 'kern'; its lookups apply in LookupList order and ACCUMULATE.
//   - within one lookup, subtables are tried in order; the FIRST one that applies to a
//     pair wins (format 1 with the pair listed, or format 2 with the first glyph
//     covered -- class 0 rows/columns apply with their value, including zero).
//   - lookup type 2 (PairPos) direct or wrapped in type 9 (Extension); value read is
//     ValueRecord1.xAdvance. LookupFlag is ignored: mark filtering cannot affect
//     directly adjacent pairs from a plain codepoint range.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <errno.h>

typedef struct GposCtx {
    const uint8_t *p;
    uint32_t       len;
} GposCtx;

// Bounds-checked big-endian reads; a failed read poisons *ok and returns 0.
static uint32_t g16(const GposCtx *g, uint32_t off, bool *ok)
{
    if (off + 2u > g->len || off + 2u < off)
    {
        *ok = false;
        return 0;
    }
    return (uint32_t)g->p[off] << 8 | g->p[off + 1u];
}

static uint32_t g32(const GposCtx *g, uint32_t off, bool *ok)
{
    if (off + 4u > g->len || off + 4u < off)
    {
        *ok = false;
        return 0;
    }
    return (uint32_t)g->p[off] << 24 | (uint32_t)g->p[off + 1u] << 16
         | (uint32_t)g->p[off + 2u] << 8 | g->p[off + 3u];
}

// Coverage-table index of gid, or -1 when not covered. Unknown formats poison *ok.
static int32_t cov_index(const GposCtx *g, uint32_t off, uint32_t gid, bool *ok)
{
    uint32_t fmt = g16(g, off, ok);
    uint32_t n = g16(g, off + 2u, ok);
    if (!*ok)
        return -1;
    if (fmt == 1u)
    {
        uint32_t lo = 0, hi = n;
        while (lo < hi)
        {
            uint32_t mid = lo + (hi - lo) / 2u;
            uint32_t v = g16(g, off + 4u + 2u * mid, ok);
            if (!*ok)
                return -1;
            if (v < gid)
                lo = mid + 1u;
            else
                hi = mid;
        }
        if (lo < n)
        {
            uint32_t v = g16(g, off + 4u + 2u * lo, ok);
            if (*ok && v == gid)
                return (int32_t)lo;
        }
        return -1;
    }
    if (fmt == 2u)
    {
        for (uint32_t r = 0; r < n; r++)
        {
            uint32_t ro = off + 4u + 6u * r;
            uint32_t start = g16(g, ro, ok), end = g16(g, ro + 2u, ok);
            uint32_t base = g16(g, ro + 4u, ok);
            if (!*ok)
                return -1;
            if (gid >= start && gid <= end)
                return (int32_t)(base + gid - start);
        }
        return -1;
    }
    *ok = false;
    return -1;
}

// ClassDef class of gid; glyphs not listed are class 0 (spec default).
static uint32_t class_of(const GposCtx *g, uint32_t off, uint32_t gid, bool *ok)
{
    if (off == 0u)
        return 0;
    uint32_t fmt = g16(g, off, ok);
    if (!*ok)
        return 0;
    if (fmt == 1u)
    {
        uint32_t start = g16(g, off + 2u, ok), n = g16(g, off + 4u, ok);
        if (!*ok || gid < start || gid - start >= n)
            return 0;
        return g16(g, off + 6u + 2u * (gid - start), ok);
    }
    if (fmt == 2u)
    {
        uint32_t n = g16(g, off + 2u, ok);
        for (uint32_t r = 0; *ok && r < n; r++)
        {
            uint32_t ro = off + 4u + 6u * r;
            uint32_t start = g16(g, ro, ok), end = g16(g, ro + 2u, ok);
            if (*ok && gid >= start && gid <= end)
                return g16(g, ro + 4u, ok);
        }
        return 0;
    }
    *ok = false;
    return 0;
}

// ValueRecord size in bytes: every set bit of the low byte is one int16 field.
static uint32_t vr_size(uint32_t vf)
{
    uint32_t n = 0;
    for (uint32_t b = vf & 0xFFu; b != 0; b >>= 1)
        n += b & 1u;
    return 2u * n;
}

// ValueRecord1.xAdvance (bit 0x0004), skipping the fields declared before it.
static int32_t vr_xadvance(const GposCtx *g, uint32_t off, uint32_t vf, bool *ok)
{
    if ((vf & 0x4u) == 0)
        return 0;
    uint32_t skip = vr_size(vf & 0x3u);
    return (int16_t)g16(g, off + skip, ok);
}

// Tries one PairPos subtable for (gid1, gid2). True = the subtable applies and
// *kernOut holds its xAdvance (possibly 0); false = not covered / pair absent, try
// the next subtable. Malformed data poisons *ok.
static bool pairpos_apply(const GposCtx *g, uint32_t off, uint32_t gid1, uint32_t gid2,
                          int32_t *kernOut, bool *ok)
{
    uint32_t fmt = g16(g, off, ok);
    uint32_t covOff = g16(g, off + 2u, ok);
    uint32_t vf1 = g16(g, off + 4u, ok), vf2 = g16(g, off + 6u, ok);
    if (!*ok)
        return false;
    int32_t ci = cov_index(g, off + covOff, gid1, ok);
    if (!*ok || ci < 0)
        return false;
    uint32_t sz1 = vr_size(vf1), sz2 = vr_size(vf2);

    if (fmt == 1u)
    {
        uint32_t psCount = g16(g, off + 8u, ok);
        if (!*ok || (uint32_t)ci >= psCount)
            return false;
        uint32_t psOff = off + g16(g, off + 10u + 2u * (uint32_t)ci, ok);
        uint32_t pvCount = g16(g, psOff, ok);
        if (!*ok)
            return false;
        uint32_t rec = 2u + sz1 + sz2;
        for (uint32_t k = 0; k < pvCount; k++)
        {
            uint32_t ro = psOff + 2u + k * rec;
            uint32_t sg = g16(g, ro, ok);
            if (!*ok)
                return false;
            if (sg == gid2)
            {
                *kernOut = vr_xadvance(g, ro + 2u, vf1, ok);
                return *ok;
            }
        }
        return false;
    }
    if (fmt == 2u)
    {
        uint32_t cd1 = g16(g, off + 8u, ok), cd2 = g16(g, off + 10u, ok);
        uint32_t c1n = g16(g, off + 12u, ok), c2n = g16(g, off + 14u, ok);
        if (!*ok)
            return false;
        uint32_t c1 = class_of(g, cd1 ? off + cd1 : 0u, gid1, ok);
        uint32_t c2 = class_of(g, cd2 ? off + cd2 : 0u, gid2, ok);
        if (!*ok || c1 >= c1n || c2 >= c2n)
            return false;
        uint32_t rec = sz1 + sz2;
        *kernOut = vr_xadvance(g, off + 16u + (c1 * c2n + c2) * rec, vf1, ok);
        return *ok;
    }
    *ok = false;
    return false;
}

// The default LangSys of script 'latn' (falling back to 'DFLT'), or 0 when absent.
static uint32_t find_langsys(const GposCtx *g, uint32_t slOff, bool *ok)
{
    uint32_t best = 0;
    uint32_t n = g16(g, slOff, ok);
    for (uint32_t i = 0; *ok && i < n; i++)
    {
        uint32_t ro = slOff + 2u + 6u * i;
        uint32_t tag = g32(g, ro, ok);
        uint32_t scriptOff = g16(g, ro + 4u, ok);
        if (!*ok)
            return 0;
        if (tag != 0x6C61746Eu && tag != 0x44464C54u) // 'latn' / 'DFLT'
            continue;
        uint32_t dls = g16(g, slOff + scriptOff, ok);
        if (!*ok)
            return 0;
        if (dls == 0)
            continue;
        best = slOff + scriptOff + dls;
        if (tag == 0x6C61746Eu)
            break; // 'latn' wins over 'DFLT'
    }
    return best;
}

#define GPOS_MAX_LOOKUPS 16u
#define GPOS_MAX_SUBS    32u

int ano_gpos_extract_kerns(const uint8_t *gpos, uint32_t len, const uint32_t *slotGids,
                           uint32_t slotCount, int32_t *dense)
{
    if (gpos == NULL || slotGids == NULL || dense == NULL || slotCount == 0 || slotCount > 0xFFFFu)
        return EINVAL;
    GposCtx g = { gpos, len };
    bool ok = true;

    uint32_t slOff = g16(&g, 4u, &ok);
    uint32_t flOff = g16(&g, 6u, &ok);
    uint32_t llOff = g16(&g, 8u, &ok);
    if (!ok)
        return EIO;

    uint32_t lsOff = find_langsys(&g, slOff, &ok);
    if (!ok)
        return EIO;
    if (lsOff == 0)
        return 0; // no latn/DFLT default LangSys: nothing to kern

    // LangSys feature indices -> 'kern' features -> lookup indices (dedup, ascending:
    // lookups apply in LookupList order regardless of listing order).
    uint32_t kernLookups[GPOS_MAX_LOOKUPS];
    uint32_t kernLookupCount = 0;
    uint32_t featCount = g16(&g, lsOff + 4u, &ok);
    for (uint32_t f = 0; ok && f < featCount; f++)
    {
        uint32_t fi = g16(&g, lsOff + 6u + 2u * f, &ok);
        uint32_t fro = flOff + 2u + 6u * fi;
        uint32_t tag = g32(&g, fro, &ok);
        uint32_t featOff = g16(&g, fro + 4u, &ok);
        if (!ok)
            return EIO;
        if (tag != 0x6B65726Eu) // 'kern'
            continue;
        uint32_t lookupCount = g16(&g, flOff + featOff + 2u, &ok);
        for (uint32_t l = 0; ok && l < lookupCount; l++)
        {
            uint32_t li = g16(&g, flOff + featOff + 4u + 2u * l, &ok);
            bool seen = false;
            for (uint32_t k = 0; k < kernLookupCount; k++)
                if (kernLookups[k] == li)
                    seen = true;
            if (!seen && kernLookupCount < GPOS_MAX_LOOKUPS)
                kernLookups[kernLookupCount++] = li;
        }
    }
    if (!ok)
        return EIO;
    for (uint32_t a = 1; a < kernLookupCount; a++) // insertion sort: ascending order
        for (uint32_t b = a; b > 0 && kernLookups[b - 1u] > kernLookups[b]; b--)
        {
            uint32_t t = kernLookups[b];
            kernLookups[b] = kernLookups[b - 1u];
            kernLookups[b - 1u] = t;
        }

    for (uint32_t k = 0; k < kernLookupCount; k++)
    {
        uint32_t lo = llOff + g16(&g, llOff + 2u + 2u * kernLookups[k], &ok);
        uint32_t type = g16(&g, lo, &ok);
        uint32_t subCount = g16(&g, lo + 4u, &ok);
        if (!ok)
            return EIO;
        if (type != 2u && type != 9u)
            continue; // some other positioning under 'kern': not pair kerning
        if (subCount > GPOS_MAX_SUBS)
            subCount = GPOS_MAX_SUBS;

        // Resolve subtable offsets up front (unwrapping type 9 Extension wrappers).
        uint32_t subs[GPOS_MAX_SUBS];
        uint32_t nsubs = 0;
        for (uint32_t s = 0; ok && s < subCount; s++)
        {
            uint32_t so = lo + g16(&g, lo + 6u + 2u * s, &ok);
            if (type == 9u)
            {
                uint32_t innerType = g16(&g, so + 2u, &ok);
                uint32_t innerOff = g32(&g, so + 4u, &ok);
                if (!ok)
                    return EIO;
                if (innerType != 2u)
                    continue;
                so += innerOff;
            }
            subs[nsubs++] = so;
        }
        if (!ok)
            return EIO;

        // First applying subtable wins per pair; lookups accumulate into the matrix.
        for (uint32_t s1 = 0; s1 < slotCount; s1++)
        {
            if (slotGids[s1] > 0xFFFFu)
                continue;
            for (uint32_t s2 = 0; s2 < slotCount; s2++)
            {
                if (slotGids[s2] > 0xFFFFu)
                    continue;
                for (uint32_t s = 0; s < nsubs; s++)
                {
                    int32_t kern = 0;
                    bool applied = pairpos_apply(&g, subs[s], slotGids[s1], slotGids[s2],
                                                 &kern, &ok);
                    if (!ok)
                        return EIO;
                    if (applied)
                    {
                        dense[s1 * slotCount + s2] += kern;
                        break;
                    }
                }
            }
        }
    }
    return 0;
}
