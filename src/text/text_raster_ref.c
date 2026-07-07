/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// CPU reference rasterizer for the Scanline Sweeper coverage math, the scalar mirror
// the GLSL compute shader is written against. All per-pixel
// arithmetic is float32 to predict shader precision. Consumes the baked stream verbatim
// (grammar in anoptic_text.h). No gamma, output is linear coverage like FT_Render_Glyph's.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <math.h>
#include <string.h>

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Single root of a monotone quadratic component hitting `target`, clamped to [0,1],
// via the citardauq form 2c/(-b - sign(span)*sqrt(d)). Caller guarantees c0 != c2.
static float solve_mono(float c0, float c1, float c2, float target)
{
    float span = c2 - c0;
    float a = c0 - 2.0f * c1 + c2;
    float b = 2.0f * (c1 - c0);
    float c = c0 - target;
    float d = fmaxf(b * b - 4.0f * a * c, 0.0f);
    float den = -b - copysignf(sqrtf(d), span);
    if (den == 0.0f) // b == 0 && d == 0: the t=0 extremum endpoint sits on target
        return 0.0f;
    return clampf(2.0f * c / den, 0.0f, 1.0f);
}

// Signed area swept between one monotone quad and the window's right edge, clipped to
// the window ([0,w] x [0,h] window-local). The parameter range is restricted to the
// y-band, split at the x-boundary crossings (monotonicity: at most one each), each piece
// contributing its chord's trapezoid against the right edge with endpoints clamped into
// the window. Pieces outside collapse to zero or a full-width rectangle.
static float curve_area(float x0, float y0, float x1, float y1, float x2, float y2,
                        float w, float h)
{
    if (y0 == y2)
        return 0.0f; // horizontal: sweeps nothing
    if (fmaxf(y0, y2) <= 0.0f || fminf(y0, y2) >= h || fminf(x0, x2) >= w)
        return 0.0f; // below, above, or right of the window

    if (x0 == x2) // vertical fast path: the whole curve sits at x0
    {
        float b = fminf(w, w - x0);
        return (clampf(y2, 0.0f, h) - clampf(y0, 0.0f, h)) * b;
    }

    // Curve-parameter interval inside the y-band, direction-normalized ends.
    float t0 = solve_mono(y0, y1, y2, 0.0f);
    float t1 = solve_mono(y0, y1, y2, h);
    if (t0 > t1)
    {
        float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    // x-boundary crossings, folded into the band.
    float ta = clampf(solve_mono(x0, x1, x2, 0.0f), t0, t1);
    float tb = clampf(solve_mono(x0, x1, x2, w), t0, t1);
    if (ta > tb)
    {
        float tmp = ta;
        ta = tb;
        tb = tmp;
    }

    float ax = x0 - 2.0f * x1 + x2, bx = 2.0f * (x1 - x0);
    float ay = y0 - 2.0f * y1 + y2, by = 2.0f * (y1 - y0);
    float xs = clampf((ax * t0 + bx) * t0 + x0, 0.0f, w);
    float ys = clampf((ay * t0 + by) * t0 + y0, 0.0f, h);
    float area = 0.0f;
    const float ends[3] = { ta, tb, t1 };
    for (int k = 0; k < 3; k++)
    {
        float te = ends[k];
        float xe = clampf((ax * te + bx) * te + x0, 0.0f, w);
        float ye = clampf((ay * te + by) * te + y0, 0.0f, h);
        area += (ye - ys) * (2.0f * w - xs - xe) * 0.5f;
        xs = xe;
        ys = ye;
    }
    return area;
}

static inline float half_lo(uint32_t u) { return ano_half_unpack((uint16_t)(u & 0xFFFFu)); }
static inline float half_hi(uint32_t u) { return ano_half_unpack((uint16_t)(u >> 16)); }

// Unclamped coverage sum for one em-space window: walks the glyph's stream once,
// accumulating every curve's signed swept area, normalized by the window area.
float ano_text_window_sum(const uint32_t *pts, const AnoGlyphEntry *g, float wx, float wy,
                          float w, float h)
{
    uint32_t i = g->pointOffset;
    float p0x = half_lo(pts[i]) - wx, p0y = half_hi(pts[i]) - wy;
    i++;
    float area = 0.0f;
    for (uint32_t c = 0; c < g->curveCount; c++)
    {
        if (pts[i] == ANO_TEXT_POINT_SENTINEL) // contour restart
        {
            i++;
            p0x = half_lo(pts[i]) - wx;
            p0y = half_hi(pts[i]) - wy;
            i++;
        }
        float p1x = half_lo(pts[i]) - wx, p1y = half_hi(pts[i]) - wy;
        i++;
        float p2x = half_lo(pts[i]) - wx, p2y = half_hi(pts[i]) - wy;
        i++;
        area += curve_area(p0x, p0y, p1x, p1y, p2x, p2y, w, h);
        p0x = p2x;
        p0y = p2y;
    }
    return area / (w * h);
}

void ano_text_raster_ref(const uint32_t *points, const AnoGlyphEntry *glyph,
                         float pixelsPerEm, int left, int top, int width, int rows,
                         uint8_t *out, float *maxSumOut)
{
    memset(out, 0, (size_t)width * (size_t)rows);
    float maxSum = 0.0f;
    if (glyph->curveCount > 0)
    {
        float invS = 1.0f / pixelsPerEm;
        for (int r = 0; r < rows; r++)
        {
            float wy = (float)(top - r - 1) * invS;
            for (int c = 0; c < width; c++)
            {
                float wx = (float)(left + c) * invS;
                float sum = ano_text_window_sum(points, glyph, wx, wy, invS, invS);
                maxSum = fmaxf(maxSum, sum);
                // Per-glyph clamp. No gamma here.
                out[r * width + c] = (uint8_t)(clampf(sum, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
    if (maxSumOut != NULL)
        *maxSumOut = maxSum;
}
