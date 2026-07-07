/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI reference evaluator: scalar mirror of the GPU prim math (uicoverage.glsl ports
// from THIS file statement for statement — the text_raster_ref.c discipline).
// Coverage model: continuous-domain, one value per unit pixel window. Straight rrect
// edges are exact box-filter coverage (the SDF ramp equals the half-plane trapezoid
// there); corner arcs carry the documented curvature approximation. Shadows are the
// Wallace closed form: erf along x, 4-sample Gaussian quadrature along y (CC0,
// madebyevan.com/shaders/fast-rounded-rectangle-shadows). docs/ui/ui-render.md §3.3-3.6.

#include "anoptic_ui.h"

#include <math.h>

static float clamp01(float x)
{
    return fminf(fmaxf(x, 0.0f), 1.0f);
}

// In: p relative to box center (prim space, y-down), half extents, per-corner radii
// (tl,tr,br,bl) pre-clamped by the builder. Out: exact Euclidean signed distance.
float ano_ui_ref_sd_rrect(const float p[2], const float half[2], const float radii[4])
{
    // y-down quadrant: x<0,y<0 tl; x>=0,y<0 tr; x>=0,y>=0 br; x<0,y>=0 bl.
    float r = p[0] >= 0.0f ? (p[1] >= 0.0f ? radii[2] : radii[1])
                           : (p[1] >= 0.0f ? radii[3] : radii[0]);
    float qx = fabsf(p[0]) - half[0] + r;
    float qy = fabsf(p[1]) - half[1] + r;
    float mx = fmaxf(qx, 0.0f), my = fmaxf(qy, 0.0f);
    return fminf(fmaxf(qx, qy), 0.0f) + sqrtf(mx * mx + my * my) - r;
}

// Abramowitz-Stegun-style rational erf (Wallace's constants, a3 dropped): max error
// well under 1/255 across the range shadows sample.
static float ui_erf(float x)
{
    float s = x >= 0.0f ? 1.0f : -1.0f, a = fabsf(x);
    float y = 1.0f + (0.278393f + (0.230389f + 0.078108f * (a * a)) * a) * a;
    y *= y;
    return s - s / (y * y);
}

static float ui_gaussian(float x, float sigma)
{
    return expf(-(x * x) / (2.0f * sigma * sigma)) / (2.5066282746310002f * sigma);
}

// Closed-form blur of the rounded box along x for the row at offset y from center.
// Only called with rows inside the box's y extent (the quadrature clamp guarantees it).
static float shadow_x(float x, float y, float sigma, float corner, const float half[2])
{
    float delta = fminf(half[1] - corner - fabsf(y), 0.0f);
    float curved = half[0] - corner + sqrtf(fmaxf(0.0f, corner * corner - delta * delta));
    float k = 0.70710678f / sigma;
    return 0.5f * (ui_erf((x + curved) * k) - ui_erf((x - curved) * k));
}

// In: p relative to box center, half extents, uniform corner radius, sigma >= 1e-3
// (builder contract). Out: blurred-box intensity in [0,1] at the point; the blur IS
// the anti-aliasing, so no window integration is layered on top.
float ano_ui_ref_shadow(const float p[2], const float half[2], float corner, float sigma)
{
    // 4-sample Gaussian quadrature over the y offsets whose rows intersect the box,
    // truncated at 3 sigma. Empty range degenerates to step 0 -> value 0.
    float low = p[1] - half[1], high = p[1] + half[1];
    float start = fminf(fmaxf(-3.0f * sigma, low), high);
    float end = fminf(fmaxf(3.0f * sigma, low), high);
    float step = (end - start) / 4.0f;
    float y = start + step * 0.5f;
    float value = 0.0f;
    for (int i = 0; i < 4; i++) {
        value += shadow_x(p[0], p[1] - y, sigma, corner, half) * ui_gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

// Window coverage of one clip entry in overlay space: exact rect overlap (window area
// is 1, so the 1D-overlap product IS the coverage) times the rounded term's ramp.
// Invalid non-NONE refs fail CLOSED (0): a truncated table must not leak content
// outside its intended bounds.
static float clip_cov(const AnoUiScene *s, uint32_t ref, float px, float py)
{
    if (ref >= s->clipCount)
        return 0.0f;
    const AnoUiClip *c = &s->clips[ref];
    float ox = fmaxf(0.0f, fminf(px + 1.0f, c->rect[2]) - fmaxf(px, c->rect[0]));
    float oy = fmaxf(0.0f, fminf(py + 1.0f, c->rect[3]) - fmaxf(py, c->rect[1]));
    float cov = ox * oy;
    if (c->rrHalf[0] >= 0.0f) {
        float q[2] = { px + 0.5f - c->rrCenter[0], py + 0.5f - c->rrCenter[1] };
        cov *= clamp01(0.5f - ano_ui_ref_sd_rrect(q, c->rrHalf, c->rrRadii));
    }
    return cov;
}

// In: scene, prim index, window origin (px,py). Out: the prim's premultiplied
// contribution over [px,px+1)x[py,py+1), clip applied. IMAGE/PATH/GLYPHS are zero
// here — they validate in their own lanes (texture sampling, curve walk, glyph walk).
void ano_ui_ref_shade(const AnoUiScene *s, uint32_t prim, float px, float py, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (prim >= s->primCount)
        return;
    const AnoUiPrim *p = &s->prims[prim];
    // Window center through the prim transform (rows, the glyph-inv convention).
    float dx = px + 0.5f - p->origin[0], dy = py + 0.5f - p->origin[1];
    float l[2] = { p->inv[0] * dx + p->inv[1] * dy, p->inv[2] * dx + p->inv[3] * dy };
    float cov;
    switch (p->kind) {
    case ANO_UI_RRECT: {
        float d = ano_ui_ref_sd_rrect(l, p->half, p->radii);
        cov = clamp01(0.5f - d);
        float w = p->param[0];
        if (w > 0.0f)
            cov -= clamp01(0.5f - (d + w)); // ring: outer minus eroded
        break;
    }
    case ANO_UI_SHADOW: {
        float a = ano_ui_ref_shadow(l, p->half, p->radii[0], p->param[0]);
        if (p->flags & ANO_UI_FLAG_INNER) {
            float d = ano_ui_ref_sd_rrect(l, p->half, p->radii);
            a = (1.0f - a) * clamp01(0.5f - d); // blur of the complement, masked inside
        }
        cov = a;
        break;
    }
    default:
        return;
    }
    if (p->clipRef != ANO_UI_REF_NONE)
        cov *= clip_cov(s, p->clipRef, px, py);
    for (int i = 0; i < 4; i++)
        out[i] = p->color[i] * cov;
}

// Painter's-order register blend at one pixel: ascending index, src over acc (later
// prims on top — the textraster.comp loop). ADD accumulates rgb only, so glows light
// without occluding. Result is UNCLAMPED premultiplied linear; quantizers clamp.
void ano_ui_ref_eval(const AnoUiScene *s, float px, float py, float out[4])
{
    float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0; i < s->primCount; i++) {
        float src[4];
        ano_ui_ref_shade(s, i, px, py, src);
        if ((s->prims[i].flags & ANO_UI_BLEND_MASK) == ANO_UI_BLEND_ADD) {
            acc[0] += src[0];
            acc[1] += src[1];
            acc[2] += src[2];
        } else {
            for (int k = 0; k < 4; k++)
                acc[k] = src[k] + acc[k] * (1.0f - src[3]);
        }
    }
    for (int k = 0; k < 4; k++)
        out[k] = acc[k];
}
