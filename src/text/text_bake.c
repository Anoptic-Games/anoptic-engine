/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Glyph-curve bake: FT outlines -> directed monotone quads in em -> packed binary16 shared-vertex stream + directory. Grammar in text_internal.h.
// Per glyph: decompose to quads (double em), fill-right winding, split at per-axis extrema, drop degenerates, quantize halves with controls clamped to endpoint box.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include "anoptic_log.h"
#include "anoptic_memory.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

/* Half pack */

// binary16 round-to-nearest-even, bit-exact. Overflow -> +-inf.

uint16_t ano_half_pack(float v)
{
    uint32_t x;
    memcpy(&x, &v, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    x &= 0x7FFFFFFFu;
    if (x >= 0x47800000u) // >= 65536 after rounding: inf/nan/overflow
        return (uint16_t)(sign | (x > 0x7F800000u ? 0x7E00u : 0x7C00u));
    if (x < 0x38800000u) // subnormal or zero
    {
        if (x < 0x33000000u) // < 2^-25 -> 0
            return (uint16_t)sign;
        uint32_t shift = 126u - (x >> 23); // 14..24, implicit mant -> 10-bit
        uint32_t mant  = (x & 0x7FFFFFu) | 0x800000u;
        uint32_t half  = mant >> shift;
        uint32_t rem   = mant & ((1u << shift) - 1u);
        uint32_t mid   = 1u << (shift - 1u);
        if (rem > mid || (rem == mid && (half & 1u)))
            half++;
        return (uint16_t)(sign | half);
    }
    uint32_t mant = x & 0x7FFFFFu;
    uint32_t half = (((x >> 23) - 112u) << 10) | (mant >> 13);
    uint32_t rem  = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u)))
        half++;
    return (uint16_t)(sign | half);
}

float ano_half_unpack(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t em   = h & 0x7FFFu;
    uint32_t bits;
    if (em >= 0x7C00u) // inf/nan
        bits = sign | 0x7F800000u | ((em & 0x3FFu) << 13);
    else if (em >= 0x0400u) // normal
        bits = sign | ((em + 0x1C000u) << 13);
    else if (em == 0u)
        bits = sign;
    else // subnormal renormalize
    {
        uint32_t e = 113u, m = em;
        while (!(m & 0x400u))
        {
            m <<= 1;
            e--;
        }
        bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
    }
    float out;
    memcpy(&out, &bits, 4);
    return out;
}

/* Quad utilities */

// De Casteljau split at t into l (before) and r (after).
static void quad_split_at(const AnoQuad *q, double t, AnoQuad *l, AnoQuad *r)
{
    double ax = q->x[0] + (q->x[1] - q->x[0]) * t;
    double ay = q->y[0] + (q->y[1] - q->y[0]) * t;
    double bx = q->x[1] + (q->x[2] - q->x[1]) * t;
    double by = q->y[1] + (q->y[2] - q->y[1]) * t;
    double mx = ax + (bx - ax) * t;
    double my = ay + (by - ay) * t;
    *l = (AnoQuad){ .x = { q->x[0], ax, mx }, .y = { q->y[0], ay, my } };
    *r = (AnoQuad){ .x = { mx, bx, q->x[2] }, .y = { my, by, q->y[2] } };
}

int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3])
{
    const double T = 1e-6; // interior-band threshold
    double ts[2];
    int    n = 0;
    for (int axis = 0; axis < 2; axis++)
    {
        const double *c = axis ? q->y : q->x;
        double a = c[0] - 2.0 * c[1] + c[2];
        if (a != 0.0)
        {
            double t = (c[0] - c[1]) / a;
            if (t > T && t < 1.0 - T)
                ts[n++] = t;
        }
    }
    if (n == 2)
    {
        if (ts[0] > ts[1])
        {
            double tmp = ts[0];
            ts[0] = ts[1];
            ts[1] = tmp;
        }
        if (ts[1] - ts[0] < T)
            n = 1;
    }
    if (n == 0)
    {
        out[0] = *q;
        return 1;
    }
    // Successive splits: later t remapped into remaining right piece.
    AnoQuad rest = *q;
    double  consumed = 0.0;
    int     count = 0;
    for (int i = 0; i < n; i++)
    {
        double local = (ts[i] - consumed) / (1.0 - consumed);
        AnoQuad l, r;
        quad_split_at(&rest, local, &l, &r);
        out[count++] = l;
        rest = r;
        consumed = ts[i];
    }
    out[count++] = rest;
    return count;
}

// Recursive halving. Emit one quad when deviation (sqrt(3)/36 * |p3-3c2+3c1-p0|) <= tol or depth spent.
static void cubic_rec(const double px[4], const double py[4], double tol, AnoQuad *out,
                      int *count, int depth)
{
    double ex = px[3] - 3.0 * px[2] + 3.0 * px[1] - px[0];
    double ey = py[3] - 3.0 * py[2] + 3.0 * py[1] - py[0];
    double err = 0.0481125224324688 * sqrt(ex * ex + ey * ey);
    if (err <= tol || depth == 0)
    {
        out[*count] = (AnoQuad){
            .x = { px[0], (3.0 * (px[1] + px[2]) - px[0] - px[3]) * 0.25, px[3] },
            .y = { py[0], (3.0 * (py[1] + py[2]) - py[0] - py[3]) * 0.25, py[3] },
        };
        (*count)++;
        return;
    }
    double lx[4], ly[4], rx[4], ry[4];
    double m01x = (px[0] + px[1]) * 0.5, m01y = (py[0] + py[1]) * 0.5;
    double m12x = (px[1] + px[2]) * 0.5, m12y = (py[1] + py[2]) * 0.5;
    double m23x = (px[2] + px[3]) * 0.5, m23y = (py[2] + py[3]) * 0.5;
    double ax = (m01x + m12x) * 0.5, ay = (m01y + m12y) * 0.5;
    double bx = (m12x + m23x) * 0.5, by = (m12y + m23y) * 0.5;
    double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
    lx[0] = px[0]; ly[0] = py[0]; lx[1] = m01x; ly[1] = m01y; lx[2] = ax; ly[2] = ay; lx[3] = mx; ly[3] = my;
    rx[0] = mx; ry[0] = my; rx[1] = bx; ry[1] = by; rx[2] = m23x; ry[2] = m23y; rx[3] = px[3]; ry[3] = py[3];
    cubic_rec(lx, ly, tol, out, count, depth - 1);
    cubic_rec(rx, ry, tol, out, count, depth - 1);
}

int ano_cubic_to_quads(const double px[4], const double py[4], double tolEm, AnoQuad *out,
                       int maxOut)
{
    if (maxOut < 1)
        return -1;
    int depth = 0;
    while ((2 << depth) <= maxOut && depth < 8)
        depth++; // 2^depth pieces <= maxOut
    int count = 0;
    cubic_rec(px, py, tolEm, out, &count, depth);
    return count;
}

/* Decompose collector */

// FT callbacks accumulate per-contour quads in double em on scratch.

typedef struct BakeContour {
    AnoQuad *quads;
    uint32_t count, cap;
} BakeContour;

typedef struct BakeCollect {
    mi_heap_t   *scratch;
    double       invUpem;
    double       curX, curY;
    double       startX, startY;
    bool         open;
    BakeContour *contours;
    uint32_t     contourCount, contourCap;
    bool         oom; // sticky, aborts the decompose
} BakeCollect;

// Grow capacity to fit need. NULL on OOM.
static void *bake_grow(mi_heap_t *heap, void *p, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap)
        return p;
    uint32_t next = *cap ? *cap * 2u : 16u;
    while (next < need)
        next *= 2u;
    void *np = mi_heap_realloc(heap, p, (size_t)next * elem);
    if (np != NULL)
        *cap = next;
    return np;
}

static void bake_push_quad(BakeCollect *c, double x0, double y0, double x1, double y1,
                           double x2, double y2)
{
    if (c->oom || c->contourCount == 0)
        return;
    BakeContour *con = &c->contours[c->contourCount - 1u];
    void *q = bake_grow(c->scratch, con->quads, &con->cap, con->count + 1u, sizeof(AnoQuad));
    if (q == NULL)
    {
        c->oom = true;
        return;
    }
    con->quads = q;
    con->quads[con->count++] = (AnoQuad){ .x = { x0, x1, x2 }, .y = { y0, y1, y2 } };
}

// Close open contour: emit closing line if needed, drop empty.
static void bake_close_contour(BakeCollect *c)
{
    if (!c->open)
        return;
    c->open = false;
    if (c->curX != c->startX || c->curY != c->startY)
    {
        double mx = (c->curX + c->startX) * 0.5, my = (c->curY + c->startY) * 0.5;
        bake_push_quad(c, c->curX, c->curY, mx, my, c->startX, c->startY);
    }
    if (c->contourCount > 0 && c->contours[c->contourCount - 1u].count == 0)
        c->contourCount--;
}

static int bake_move_to(const FT_Vector *to, void *user)
{
    BakeCollect *c = user;
    bake_close_contour(c);
    void *p = bake_grow(c->scratch, c->contours, &c->contourCap, c->contourCount + 1u,
                        sizeof(BakeContour));
    if (p == NULL)
    {
        c->oom = true;
        return 1;
    }
    c->contours = p;
    c->contours[c->contourCount++] = (BakeContour){ 0 };
    c->startX = c->curX = to->x * c->invUpem;
    c->startY = c->curY = to->y * c->invUpem;
    c->open = true;
    return 0;
}

static int bake_line_to(const FT_Vector *to, void *user)
{
    BakeCollect *c = user;
    double x = to->x * c->invUpem, y = to->y * c->invUpem;
    if (x != c->curX || y != c->curY)
        bake_push_quad(c, c->curX, c->curY, (c->curX + x) * 0.5, (c->curY + y) * 0.5, x, y);
    c->curX = x;
    c->curY = y;
    return c->oom ? 1 : 0;
}

static int bake_conic_to(const FT_Vector *control, const FT_Vector *to, void *user)
{
    BakeCollect *c = user;
    double cx = control->x * c->invUpem, cy = control->y * c->invUpem;
    double x = to->x * c->invUpem, y = to->y * c->invUpem;
    bake_push_quad(c, c->curX, c->curY, cx, cy, x, y);
    c->curX = x;
    c->curY = y;
    return c->oom ? 1 : 0;
}

static int bake_cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to,
                         void *user)
{
    BakeCollect *c = user;
    double px[4] = { c->curX, c1->x * c->invUpem, c2->x * c->invUpem, to->x * c->invUpem };
    double py[4] = { c->curY, c1->y * c->invUpem, c2->y * c->invUpem, to->y * c->invUpem };
    AnoQuad parts[16];
    int n = ano_cubic_to_quads(px, py, 1e-3, parts, 16);
    for (int i = 0; i < n; i++)
        bake_push_quad(c, parts[i].x[0], parts[i].y[0], parts[i].x[1], parts[i].y[1],
                       parts[i].x[2], parts[i].y[2]);
    c->curX = px[3];
    c->curY = py[3];
    return c->oom ? 1 : 0;
}

// Reverse contour in place: flip quad order, swap each p0/p2.
static AnoQuad quad_reversed(AnoQuad q)
{
    return (AnoQuad){ .x = { q.x[2], q.x[1], q.x[0] }, .y = { q.y[2], q.y[1], q.y[0] } };
}

static void bake_reverse_contour(BakeContour *con)
{
    uint32_t n = con->count;
    for (uint32_t i = 0; i < n / 2u; i++)
    {
        AnoQuad a = con->quads[i];
        con->quads[i]          = quad_reversed(con->quads[n - 1u - i]);
        con->quads[n - 1u - i] = quad_reversed(a);
    }
    if (n & 1u)
        con->quads[n / 2u] = quad_reversed(con->quads[n / 2u]);
}

/* Packing */

typedef struct StreamVec {
    uint32_t *v;
    uint32_t  count, cap;
} StreamVec;

static bool stream_push(mi_heap_t *heap, StreamVec *s, uint32_t val)
{
    void *p = bake_grow(heap, s->v, &s->cap, s->count + 1u, sizeof(uint32_t));
    if (p == NULL)
        return false;
    s->v = p;
    s->v[s->count++] = val;
    return true;
}

// Quantize one coordinate. *q = exact float the GPU unpacks.
static uint16_t bake_quant(double v, float *q)
{
    uint16_t h = ano_half_pack((float)v);
    *q = ano_half_unpack(h);
    return h;
}

typedef struct BakeBBox {
    float minX, minY, maxX, maxY;
    bool  any;
} BakeBBox;

static void bbox_add(BakeBBox *b, float x, float y)
{
    if (!b->any)
    {
        b->minX = b->maxX = x;
        b->minY = b->maxY = y;
        b->any = true;
        return;
    }
    b->minX = fminf(b->minX, x);
    b->maxX = fmaxf(b->maxX, x);
    b->minY = fminf(b->minY, y);
    b->maxY = fmaxf(b->maxY, y);
}

// Pack one glyph's contours per header grammar to halves. Controls clamp to quantized endpoint box. bbox exact. False on OOM.
static bool bake_pack_glyph(mi_heap_t *scratch, StreamVec *stream, const BakeContour *cons,
                            uint32_t conCount, AnoGlyphEntry *e)
{
    BakeBBox bb = { 0 };
    uint32_t curves = 0;
    bool     firstContour = true;

    for (uint32_t ci = 0; ci < conCount; ci++)
    {
        const BakeContour *con = &cons[ci];
        if (con->count == 0)
            continue;
        if (!firstContour && !stream_push(scratch, stream, ANO_TEXT_POINT_SENTINEL))
            return false;
        firstContour = false;

        float    q0x, q0y;
        uint16_t hx = bake_quant(con->quads[0].x[0], &q0x);
        uint16_t hy = bake_quant(con->quads[0].y[0], &q0y);
        if (!stream_push(scratch, stream, (uint32_t)hx | ((uint32_t)hy << 16)))
            return false;
        bbox_add(&bb, q0x, q0y);

        for (uint32_t qi = 0; qi < con->count; qi++)
        {
            const AnoQuad *q = &con->quads[qi];
            float q2x, q2y;
            uint16_t h2x = bake_quant(q->x[2], &q2x);
            uint16_t h2y = bake_quant(q->y[2], &q2y);
            // Clamp control to quantized endpoint box, then quantize.
            float c1x = (float)q->x[1];
            float c1y = (float)q->y[1];
            float lox = fminf(q0x, q2x), hix = fmaxf(q0x, q2x);
            float loy = fminf(q0y, q2y), hiy = fmaxf(q0y, q2y);
            c1x = c1x < lox ? lox : (c1x > hix ? hix : c1x);
            c1y = c1y < loy ? loy : (c1y > hiy ? hiy : c1y);
            float q1x, q1y;
            uint16_t h1x = bake_quant(c1x, &q1x);
            uint16_t h1y = bake_quant(c1y, &q1y);
            if (!stream_push(scratch, stream, (uint32_t)h1x | ((uint32_t)h1y << 16)))
                return false;
            if (!stream_push(scratch, stream, (uint32_t)h2x | ((uint32_t)h2y << 16)))
                return false;
            bbox_add(&bb, q1x, q1y);
            bbox_add(&bb, q2x, q2y);
            q0x = q2x;
            q0y = q2y;
            curves++;
        }
    }

    e->curveCount = curves;
    e->bboxMin[0] = bb.any ? bb.minX : 0.0f;
    e->bboxMin[1] = bb.any ? bb.minY : 0.0f;
    e->bboxMax[0] = bb.any ? bb.maxX : 0.0f;
    e->bboxMax[1] = bb.any ? bb.maxY : 0.0f;
    return true;
}

/* Kern extraction */

// Per-face GPOS PairPos into shared dense matrix, compact to key-sorted pairs on caller heap. Missing GPOS -> skip face. Malformed -> warn, keep any partial dense already written. OOM only hard error. Oversized bakes (>1024 slots) skip.

static int bake_kerns(mi_heap_t *scratch, mi_heap_t *heap, const AnoGlyphEntry *glyphs,
                      uint32_t glyphCount, FT_Face *slotFace, const uint32_t *slotCp,
                      const double *slotInvUpem, AnoFontBake *out)
{
    if (glyphCount > 1024u)
    {
        ano_log(ANO_WARN, "text: kern extraction skipped (bake %u > 1024 slots)", glyphCount);
        return 0;
    }
    uint32_t *slotGids = mi_heap_malloc(scratch, (size_t)glyphCount * sizeof(uint32_t));
    int32_t  *dense = mi_heap_zalloc(scratch, (size_t)glyphCount * glyphCount * sizeof(int32_t));
    if (slotGids == NULL || dense == NULL)
        return ENOMEM;

    // One pass per distinct face: build gid map, then null its slots out of slotFace.
    for (uint32_t f = 0; f < glyphCount; f++)
    {
        FT_Face face = slotFace[f];
        if (face == NULL)
            continue; // already handled
        for (uint32_t i = 0; i < glyphCount; i++)
            slotGids[i] = (slotFace[i] == face && !(glyphs[i].flags & ANO_GLYPH_MISSING))
                              ? (uint32_t)FT_Get_Char_Index(face, slotCp[i])
                              : 0xFFFFFFFFu;
        for (uint32_t i = f; i < glyphCount; i++)
            if (slotFace[i] == face)
                slotFace[i] = NULL;

        FT_ULong glen = 0;
        if (FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, NULL, &glen) != FT_Err_Ok || glen < 10u)
            continue; // no GPOS
        uint8_t *blob = mi_heap_malloc(scratch, glen);
        if (blob == NULL)
            return ENOMEM;
        if (FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, blob, &glen) != FT_Err_Ok)
            continue;
        if (ano_gpos_extract_kerns(blob, (uint32_t)glen, slotGids, glyphCount, dense) != 0)
            ano_log(ANO_WARN, "text: malformed GPOS table; kerning skipped for one face");
    }

    uint32_t nz = 0;
    for (uint32_t i = 0; i < glyphCount * glyphCount; i++)
        nz += dense[i] != 0;
    if (nz == 0)
        return 0;
    AnoKernPair *pairs = mi_heap_malloc(heap, (size_t)nz * sizeof(AnoKernPair));
    if (pairs == NULL)
        return ENOMEM;
    uint32_t w = 0;
    for (uint32_t s1 = 0; s1 < glyphCount; s1++) // s1-major -> keys sorted
        for (uint32_t s2 = 0; s2 < glyphCount; s2++)
        {
            int32_t v = dense[s1 * glyphCount + s2];
            if (v != 0) // shared face: left slot's upem converts
                pairs[w++] = (AnoKernPair){ .key = s1 << 16 | s2,
                                            .xAdvance = (float)((double)v * slotInvUpem[s1]) };
        }
    out->kerns = pairs;
    out->kernCount = nz;
    return 0;
}

/* Bake entry */

// Module thread. Scratch for temps. Result blobs on caller heap.

int ano_text_font_bake_ranges(const AnoBakeRange *ranges, uint32_t rangeCount,
                              mi_heap_t *heap, AnoFontBake *out)
{
    if (ranges == NULL || rangeCount == 0 || heap == NULL || out == NULL)
        return EINVAL;
    uint64_t glyphCount = 0;
    for (uint32_t r = 0; r < rangeCount; r++)
    {
        if (ano_text_face(ranges[r].font) == NULL || ranges[r].first > ranges[r].last)
            return EINVAL;
        if (r > 0 && ranges[r].first <= ranges[r - 1u].last)
            return EINVAL; // sorted ascending + disjoint
        glyphCount += (uint64_t)ranges[r].last - ranges[r].first + 1u;
    }
    if (glyphCount > 4096u)
        return EINVAL;
    memset(out, 0, sizeof *out);

    AnoGlyphEntry *glyphs = mi_heap_zalloc(heap, (size_t)glyphCount * sizeof(AnoGlyphEntry));
    AnoGlyphRange *map = mi_heap_malloc(heap, (size_t)rangeCount * sizeof(AnoGlyphRange));
    if (glyphs == NULL || map == NULL)
        return ENOMEM;

    mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
    if (scratch == NULL)
        return ENOMEM;

    // Per-slot face/codepoint/upem for glyph loop + kern pass.
    FT_Face  *slotFace = mi_heap_malloc(scratch, (size_t)glyphCount * sizeof(FT_Face));
    uint32_t *slotCp = mi_heap_malloc(scratch, (size_t)glyphCount * sizeof(uint32_t));
    double   *slotInvUpem = mi_heap_malloc(scratch, (size_t)glyphCount * sizeof(double));
    if (slotFace == NULL || slotCp == NULL || slotInvUpem == NULL)
        return ENOMEM;
    uint32_t slot = 0;
    for (uint32_t r = 0; r < rangeCount; r++)
    {
        FT_Face face = ano_text_face(ranges[r].font);
        double  inv = 1.0 / (double)face->units_per_EM;
        map[r] = (AnoGlyphRange){ .first = ranges[r].first, .last = ranges[r].last,
                                  .slotBase = slot };
        for (uint32_t cp = ranges[r].first;; cp++)
        {
            slotFace[slot] = face;
            slotCp[slot] = cp;
            slotInvUpem[slot] = inv;
            slot++;
            if (cp == ranges[r].last)
                break;
        }
    }

    StreamVec stream = { 0 };

    for (uint32_t i = 0; i < (uint32_t)glyphCount; i++)
    {
        AnoGlyphEntry *e = &glyphs[i];
        FT_Face face = slotFace[i];
        double  invUpem = slotInvUpem[i];
        e->pointOffset = stream.count;

        FT_UInt gidx = FT_Get_Char_Index(face, slotCp[i]);
        if (gidx == 0)
        {
            e->flags = ANO_GLYPH_MISSING;
            continue;
        }
        FT_Error err = FT_Load_Glyph(face, gidx,
            FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM);
        if (err != FT_Err_Ok || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        {
            ano_log(ANO_WARN, "text: glyph U+%04X failed to load as an outline (err %d)",
                         slotCp[i], (int)err);
            e->flags = ANO_GLYPH_MISSING;
            continue;
        }
        e->advance = (float)((double)face->glyph->metrics.horiAdvance * invUpem);

        FT_Outline *outline = &face->glyph->outline;
        BakeCollect col = { .scratch = scratch, .invUpem = invUpem };
        FT_Outline_Funcs funcs = {
            .move_to = bake_move_to, .line_to = bake_line_to,
            .conic_to = bake_conic_to, .cubic_to = bake_cubic_to,
        };
        err = FT_Outline_Decompose(outline, &funcs, &col);
        bake_close_contour(&col);
        if (col.oom)
            return ENOMEM;
        if (err != FT_Err_Ok)
            return EIO;

        // Fill-right: PostScript-wound faces flip. Empty/ambiguous pass through.
        if (col.contourCount > 0
            && FT_Outline_Get_Orientation(outline) == FT_ORIENTATION_POSTSCRIPT)
            for (uint32_t ci = 0; ci < col.contourCount; ci++)
                bake_reverse_contour(&col.contours[ci]);

        // Monotonize each contour, drop collapsed points.
        for (uint32_t ci = 0; ci < col.contourCount; ci++)
        {
            BakeContour *con = &col.contours[ci];
            AnoQuad *mono = NULL;
            uint32_t mcount = 0, mcap = 0;
            for (uint32_t qi = 0; qi < con->count; qi++)
            {
                AnoQuad parts[3];
                int np = ano_quad_split_monotone(&con->quads[qi], parts);
                for (int p = 0; p < np; p++)
                {
                    if (parts[p].x[0] == parts[p].x[2] && parts[p].y[0] == parts[p].y[2])
                        continue;
                    void *g = bake_grow(scratch, mono, &mcap, mcount + 1u, sizeof(AnoQuad));
                    if (g == NULL)
                        return ENOMEM;
                    mono = g;
                    mono[mcount++] = parts[p];
                }
            }
            con->quads = mono;
            con->count = mcount;
        }

        if (!bake_pack_glyph(scratch, &stream, col.contours, col.contourCount, e))
            return ENOMEM;
    }

    // bake_kerns consumes slotFace destructively.
    int kerr = bake_kerns(scratch, heap, glyphs, (uint32_t)glyphCount, slotFace, slotCp,
                          slotInvUpem, out);
    if (kerr != 0)
        return kerr;

    uint32_t *points = NULL;
    if (stream.count > 0)
    {
        points = mi_heap_malloc(heap, (size_t)stream.count * sizeof(uint32_t));
        if (points == NULL)
            return ENOMEM;
        memcpy(points, stream.v, (size_t)stream.count * sizeof(uint32_t));
    }

    FT_Face metricsFace = ano_text_face(ranges[0].font);
    double  metricsInv = 1.0 / (double)metricsFace->units_per_EM;
    out->points     = points;
    out->pointCount = stream.count;
    out->glyphs     = glyphs;
    out->glyphCount = (uint32_t)glyphCount;
    out->ranges     = map;
    out->rangeCount = rangeCount;
    out->ascender   = (float)((double)metricsFace->ascender * metricsInv);
    out->descender  = (float)((double)metricsFace->descender * metricsInv);
    out->lineHeight = (float)((double)metricsFace->height * metricsInv);
    out->upem       = (uint32_t)metricsFace->units_per_EM;
    return 0;
}

int ano_text_font_bake(AnoFontId font, uint32_t firstCodepoint, uint32_t lastCodepoint,
                       mi_heap_t *heap, AnoFontBake *out)
{
    AnoBakeRange range = { .font = font, .first = firstCodepoint, .last = lastCodepoint };
    return ano_text_font_bake_ranges(&range, 1, heap, out);
}
