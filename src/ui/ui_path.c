/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI path baker: arbitrary contours (lines + quadratic Beziers) -> directed monotone
// quadratic Beziers in the shared sweeper stream, exactly the text bake's output
// grammar (p0 (p1 p2)+ per contour, ANO_UI_CURVE_SENTINEL between contours). Points
// are packed binary16 in prim-LOCAL space (origin at the path's bbox center).
// binary16 holds ~sub-pixel precision to a few hundred px of half-extent.
//
// Fill is nonzero-winding and caller-winding-independent: the total signed area picks
// a global orientation, an outer contour of either winding fills, and oppositely
// wound inner contours punch holes.

#include <stddef.h>

#include "anoptic_ui.h"
#include "ui_path.h"

static_assert(sizeof(AnoQuad) == 48 && offsetof(AnoQuad, y) == 24,
              "AnoQuad ABI must match src/text/text_internal.h");

// Pre-split quad budget per fill.
#define UI_PATH_MAX_QUADS 512

// One center-local point -> stream word (binary16 x in low half, y in high, the
// unpackHalf2x16 order).
static uint32_t pack_pt(double x, double y)
{
    return (uint32_t)ano_half_pack((float)x) | ((uint32_t)ano_half_pack((float)y) << 16);
}

// In: count packed point words (in == out is legal), isotropic surface scale s > 0.
// Out: each binary16 pair scaled. Contour sentinels (+inf halves) survive untouched.
void ano_ui_curves_scale(const uint32_t *in, uint32_t *out, uint32_t count, float s)
{
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t w = in[i];
        out[i] = (uint32_t)ano_half_pack(ano_half_unpack((uint16_t)(w & 0xFFFFu)) * s)
               | ((uint32_t)ano_half_pack(ano_half_unpack((uint16_t)(w >> 16)) * s) << 16);
    }
}

// A quad traversed backwards: swap endpoints, keep the control.
static AnoQuad quad_rev(const AnoQuad *q)
{
    return (AnoQuad){ { q->x[2], q->x[1], q->x[0] }, { q->y[2], q->y[1], q->y[0] } };
}

// Emits one quad's monotone pieces as (p1 p2) pairs into curves[*w] (p0 chains from the
// prior point). Returns false on capacity overflow. Bumps *segs by the piece count.
static bool emit_quad(uint32_t *curves, uint32_t *w, uint32_t cap, uint32_t *segs,
                      const AnoQuad *q)
{
    AnoQuad parts[3];
    int np = ano_quad_split_monotone(q, parts);
    for (int i = 0; i < np; i++)
    {
        if (*w + 2 > cap)
            return false;
        curves[(*w)++] = pack_pt(parts[i].x[1], parts[i].y[1]);
        curves[(*w)++] = pack_pt(parts[i].x[2], parts[i].y[2]);
        (*segs)++;
    }
    return true;
}

uint32_t ano_ui_path_fill(AnoUiBuilder *b, const AnoUiPathSeg *segs, uint32_t segCount,
                          const float color[4], uint32_t paintRef, uint32_t clipRef,
                          uint32_t flags)
{
    if (b->curves == NULL || segCount == 0 || b->primCount >= b->primCap)
        return ANO_UI_REF_NONE;

    // Pass A: absolute-pixel quads, contour boundaries, bbox. Lines become straight
    // quads (control at the midpoint); each contour auto-closes to its opening point.
    AnoQuad q[UI_PATH_MAX_QUADS];
    uint32_t cstart[UI_PATH_MAX_QUADS + 1]; // first quad index of each contour
    uint32_t qn = 0, cn = 0;
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    double cx = 0, cy = 0, sx = 0, sy = 0; // current point, contour start
    bool open = false;

    for (uint32_t i = 0; i < segCount; i++)
    {
        const AnoUiPathSeg *sg = &segs[i];
        if (sg->kind == ANO_UI_SEG_MOVE)
        {
            if (open && (cx != sx || cy != sy))
            {
                if (qn >= UI_PATH_MAX_QUADS)
                    return ANO_UI_REF_NONE;
                q[qn++] = (AnoQuad){ { cx, 0.5 * (cx + sx), sx }, { cy, 0.5 * (cy + sy), sy } };
            }
            cstart[cn++] = qn;
            cx = sx = sg->p[0];
            cy = sy = sg->p[1];
            open = true;
        }
        else
        {
            if (!open) // a segment before any MOVE opens a contour at the current origin
            {
                cstart[cn++] = qn;
                open = true;
            }
            double ctrlx, ctrly, nx, ny;
            if (sg->kind == ANO_UI_SEG_QUAD)
            {
                ctrlx = sg->p[0]; ctrly = sg->p[1];
                nx = sg->p[2]; ny = sg->p[3];
            }
            else // LINE
            {
                nx = sg->p[0]; ny = sg->p[1];
                ctrlx = 0.5 * (cx + nx); ctrly = 0.5 * (cy + ny);
            }
            if (qn >= UI_PATH_MAX_QUADS)
                return ANO_UI_REF_NONE;
            q[qn++] = (AnoQuad){ { cx, ctrlx, nx }, { cy, ctrly, ny } };
            cx = nx; cy = ny;
        }
        // bbox over endpoints and controls (conservative hull bound).
        for (uint32_t k = (i == 0 ? 0 : qn - 1); k < qn; k++)
            for (int j = 0; j < 3; j++)
            {
                if (q[k].x[j] < minx) minx = q[k].x[j];
                if (q[k].x[j] > maxx) maxx = q[k].x[j];
                if (q[k].y[j] < miny) miny = q[k].y[j];
                if (q[k].y[j] > maxy) maxy = q[k].y[j];
            }
    }
    if (open && (cx != sx || cy != sy))
    {
        if (qn >= UI_PATH_MAX_QUADS)
            return ANO_UI_REF_NONE;
        q[qn] = (AnoQuad){ { cx, 0.5 * (cx + sx), sx }, { cy, 0.5 * (cy + sy), sy } };
        for (int j = 0; j < 3; j++)
        {
            if (q[qn].x[j] < minx) minx = q[qn].x[j];
            if (q[qn].x[j] > maxx) maxx = q[qn].x[j];
            if (q[qn].y[j] < miny) miny = q[qn].y[j];
            if (q[qn].y[j] > maxy) maxy = q[qn].y[j];
        }
        qn++;
    }
    cstart[cn] = qn;
    if (qn == 0 || maxx <= minx || maxy <= miny)
        return ANO_UI_REF_NONE;

    // Shift to prim-local (bbox center), accumulate the endpoint shoelace for orientation.
    double ccx = 0.5 * (minx + maxx), ccy = 0.5 * (miny + maxy);
    double area = 0.0;
    for (uint32_t k = 0; k < qn; k++)
    {
        for (int j = 0; j < 3; j++) { q[k].x[j] -= ccx; q[k].y[j] -= ccy; }
        area += q[k].x[0] * q[k].y[2] - q[k].x[2] * q[k].y[0];
    }
    // curve_area integrates fill as positive when the outer contour has NON-POSITIVE
    // endpoint shoelace in this y-down local frame (pinned by anotest_ui's filled-square
    // oracle). Reverse everything if the caller wound it the other way. Holes stay opposite.
    bool reverse = area > 0.0;

    // Pass C: emit into the builder's curve scratch from curveCount; commit only on success.
    uint32_t *curves = b->curves;
    uint32_t cap = b->curveCap;
    uint32_t base = b->curveCount;
    uint32_t w = base;
    uint32_t curveSegs = 0;
    for (uint32_t c = 0; c < cn; c++)
    {
        uint32_t lo = cstart[c], hi = cstart[c + 1];
        if (hi == lo)
            continue;
        if (c != 0)
        {
            if (w + 1 > cap)
                return ANO_UI_REF_NONE;
            curves[w++] = ANO_UI_CURVE_SENTINEL;
        }
        // Contour start point (p0), then each quad's monotone pieces in traversal order.
        if (w + 1 > cap)
            return ANO_UI_REF_NONE;
        if (!reverse)
        {
            curves[w++] = pack_pt(q[lo].x[0], q[lo].y[0]);
            for (uint32_t k = lo; k < hi; k++)
                if (!emit_quad(curves, &w, cap, &curveSegs, &q[k]))
                    return ANO_UI_REF_NONE;
        }
        else
        {
            AnoQuad first = quad_rev(&q[hi - 1]);
            curves[w++] = pack_pt(first.x[0], first.y[0]);
            for (uint32_t k = hi; k-- > lo;)
            {
                AnoQuad r = quad_rev(&q[k]);
                if (!emit_quad(curves, &w, cap, &curveSegs, &r))
                    return ANO_UI_REF_NONE;
            }
        }
    }
    if (curveSegs == 0)
        return ANO_UI_REF_NONE;

    b->curveCount = w; // commit
    float mn[2] = { (float)minx, (float)miny }, mx[2] = { (float)maxx, (float)maxy };
    return ano_ui_path(b, mn, mx, base, curveSegs, color, paintRef, clipRef, flags);
}
