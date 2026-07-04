/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Glyph-curve bake: FreeType outlines -> directed monotone quadratic Beziers in em
// space -> packed binary16 shared-vertex stream + glyph directory (grammar documented
// in anoptic_text.h; design of record FONT_RENDER.md section 2).
//
// Pipeline per glyph: FT_Outline_Decompose collects contours as quads in double em
// coordinates (lines become midpoint-control quads that hit the shader's linear
// fallback; cubics subdivide within 1e-3 em); winding is normalized to fill-right
// (clockwise outers, y-up) so the coverage sum is positive inside ink; every quad is
// split at its interior per-axis extrema; degenerate pieces drop; packing quantizes
// to half floats, clamping each control into its quantized endpoints' box so the
// monotone sandwich survives quantization exactly.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include "anoptic_logging.h"
#include "anoptic_memory.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// ---------------------------------------------------------------------------------------------
// binary16 conversion (round-to-nearest-even), kept bit-exact and branch-simple; the
// pack side clamps overflow to +-inf, which the SENTINEL encoding relies on never
// occurring for real coordinates (all inputs are a few em at most).

uint16_t ano_half_pack(float v)
{
    uint32_t x;
    memcpy(&x, &v, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    x &= 0x7FFFFFFFu;
    if (x >= 0x47800000u) // >= 65536 after rounding: inf/nan/overflow
        return (uint16_t)(sign | (x > 0x7F800000u ? 0x7E00u : 0x7C00u));
    if (x < 0x38800000u) // below the smallest normal half: subnormal or zero
    {
        if (x < 0x33000000u) // < 2^-25: rounds to zero
            return (uint16_t)sign;
        uint32_t shift = 126u - (x >> 23); // 14..24: implicit-bit mantissa -> 10-bit field
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
        half++; // carry may bump the exponent; that is correct rounding
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
    else // subnormal: renormalize
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

// ---------------------------------------------------------------------------------------------
// Quad utilities.

// De Casteljau split of q at parameter t into l (before) and r (after).
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
    const double T = 1e-6; // interior-band threshold; mirrors the audit oracle
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
    // Successive splits; later parameters remap into the remaining right piece.
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

// Recursive halving; emits a single-quad approximation once the deviation bound
// (sqrt(3)/36 * |p3 - 3c2 + 3c1 - p0|) is within tol or the depth budget is spent.
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
        depth++; // 2^depth pieces never exceed maxOut
    int count = 0;
    cubic_rec(px, py, tolEm, out, &count, depth);
    return count;
}

// ---------------------------------------------------------------------------------------------
// Decompose collector: FreeType callbacks accumulate per-contour quads in double em
// coordinates on the bake's scratch heap. Font-unit integers scale exactly, so double
// equality tests on shared endpoints are reliable.

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
    bool         oom; // sticky; nonzero callback return aborts the decompose
} BakeCollect;

// Doubles capacity to fit need; returns NULL on OOM (caller sets the sticky flag).
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

// Ends the open contour: emits the closing line if the outline did not return to the
// start, and drops the contour entirely if it holds no quads (stray moves).
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
    if (x != c->curX || y != c->curY) // lines ride the shader's linear fallback (q_a == 0)
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

// Reverses a contour's direction in place: quad order flips and each quad swaps p0/p2.
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

// ---------------------------------------------------------------------------------------------
// Packing.

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

// Quantizes one coordinate; outputs the exact float the GPU will unpack.
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

// Emits one glyph's contours into the stream per the header grammar, quantizing to
// halves. Controls clamp into their quantized endpoints' box, so the monotone sandwich
// holds exactly on the GPU-visible values; bbox is exact over everything emitted.
// Returns false on OOM.
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
            // Clamp the control into the quantized endpoint box, then quantize: the
            // result stays inside because the bounds are half-representable.
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

// ---------------------------------------------------------------------------------------------
// Bake entry point. All FreeType work happens here (module thread); temporaries live
// on a scoped scratch heap; only the two result blobs land in the caller's heap.

int ano_text_font_bake(AnoFontId font, uint32_t firstCodepoint, uint32_t lastCodepoint,
                       mi_heap_t *heap, AnoFontBake *out)
{
    FT_Face face = ano_text_face(font);
    if (face == NULL || heap == NULL || out == NULL || firstCodepoint > lastCodepoint
        || lastCodepoint - firstCodepoint >= 4096u)
        return EINVAL;
    memset(out, 0, sizeof *out);

    uint32_t glyphCount = lastCodepoint - firstCodepoint + 1u;
    AnoGlyphEntry *glyphs = mi_heap_zalloc(heap, (size_t)glyphCount * sizeof(AnoGlyphEntry));
    if (glyphs == NULL)
        return ENOMEM;

    mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
    if (scratch == NULL)
        return ENOMEM;

    double    invUpem = 1.0 / (double)face->units_per_EM;
    StreamVec stream = { 0 };

    for (uint32_t i = 0; i < glyphCount; i++)
    {
        AnoGlyphEntry *e = &glyphs[i];
        e->pointOffset = stream.count;

        FT_UInt gidx = FT_Get_Char_Index(face, firstCodepoint + i);
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
                         firstCodepoint + i, (int)err);
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

        // Fill-right convention: the trapezoid sum is positive inside clockwise (y-up)
        // outers; PostScript-wound faces flip. Empty/ambiguous outlines pass through.
        if (col.contourCount > 0
            && FT_Outline_Get_Orientation(outline) == FT_ORIENTATION_POSTSCRIPT)
            for (uint32_t ci = 0; ci < col.contourCount; ci++)
                bake_reverse_contour(&col.contours[ci]);

        // Monotonize each contour; drop pieces that collapsed to a point.
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

    uint32_t *points = NULL;
    if (stream.count > 0)
    {
        points = mi_heap_malloc(heap, (size_t)stream.count * sizeof(uint32_t));
        if (points == NULL)
            return ENOMEM;
        memcpy(points, stream.v, (size_t)stream.count * sizeof(uint32_t));
    }

    out->points         = points;
    out->pointCount     = stream.count;
    out->glyphs         = glyphs;
    out->glyphCount     = glyphCount;
    out->firstCodepoint = firstCodepoint;
    out->ascender       = (float)((double)face->ascender * invUpem);
    out->descender      = (float)((double)face->descender * invUpem);
    out->lineHeight     = (float)((double)face->height * invUpem);
    out->upem           = (uint32_t)face->units_per_EM;
    return 0;
}
