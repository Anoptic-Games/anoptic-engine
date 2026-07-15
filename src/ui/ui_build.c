/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI primitive builder: pure packing of AnoUiPrim + side tables into caller arrays.
// No alloc, no state, any thread. docs/ui/ui-render.md §3.2.

#include "anoptic_ui.h"

#include <math.h>


/* Color */

// In: straight sRGB rgba. Out: premultiplied linear.
void ano_ui_color_srgb(const float srgba[4], float out[4])
{
    for (int i = 0; i < 3; i++)
    {
        float c = fminf(fmaxf(srgba[i], 0.0f), 1.0f);
        float lin = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
        out[i] = lin * srgba[3];
    }
    out[3] = srgba[3];
}


/* Builder */

// In: b (bound arrays), counts zeroed here. Caller owns arrays; NULL+cap 0 legal.
void ano_ui_builder_init(AnoUiBuilder *b,
                         AnoUiPrim *prims, uint32_t primCap,
                         AnoUiClip *clips, uint32_t clipCap,
                         AnoUiPaint *paints, uint32_t paintCap,
                         AnoUiStop *stops, uint32_t stopCap)
{
    b->prims = prims;   b->primCap = primCap;   b->primCount = 0;
    b->clips = clips;   b->clipCap = clipCap;   b->clipCount = 0;
    b->paints = paints; b->paintCap = paintCap; b->paintCount = 0;
    b->stops = stops;   b->stopCap = stopCap;   b->stopCount = 0;
    b->curves = NULL;   b->curveCap = 0;        b->curveCount = 0;
}


/* Surface Fold */

// In: prim, isotropic scale s > 0. Geometry/border/sigma scale. IMAGE lod -= log2(s).
// PATH/GLYPHS payloads fold separately.
void ano_ui_prim_scale(AnoUiPrim *p, float s)
{
    p->origin[0] *= s; p->origin[1] *= s;
    p->half[0] *= s;   p->half[1] *= s;
    for (int i = 0; i < 4; i++)
        p->radii[i] *= s;
    if (p->kind == ANO_UI_RRECT || p->kind == ANO_UI_SHADOW)
        p->param[0] *= s;
    else if (p->kind == ANO_UI_IMAGE)
        p->param[0] = fmaxf(0.0f, p->param[0] - log2f(s));
}

// In: clip, scale s > 0. rect + rounded term scale. rrHalf[0] < 0 sentinel stays negative.
void ano_ui_clip_scale(AnoUiClip *c, float s)
{
    for (int i = 0; i < 4; i++)
        c->rect[i] *= s;
    c->rrCenter[0] *= s; c->rrCenter[1] *= s;
    c->rrHalf[0] *= s;   c->rrHalf[1] *= s;
    for (int i = 0; i < 4; i++)
        c->rrRadii[i] *= s;
}

// In: paint, scale s > 0. Linear columns /= s. Translation column untouched.
void ano_ui_paint_scale(AnoUiPaint *p, float s)
{
    p->xform[0] /= s; p->xform[1] /= s;
    p->xform[3] /= s; p->xform[4] /= s;
}


/* Primitives */

// In: b, curve scratch + capacity (NULL/0 detaches).
void ano_ui_builder_curves(AnoUiBuilder *b, uint32_t *curves, uint32_t curveCap)
{
    b->curves = curves;
    b->curveCap = curveCap;
    b->curveCount = 0;
}

// In: radii[4] (tl,tr,br,bl), half[2]. Out: non-neg, <= min(half), CSS adjacent-side clamp.
static void radii_clamp(float radii[4], const float half[2])
{
    float cap = fminf(half[0], half[1]);
    if (cap < 0.0f)
        cap = 0.0f;
    for (int i = 0; i < 4; i++)
        radii[i] = fminf(fmaxf(radii[i], 0.0f), cap);
    float w = 2.0f * half[0], h = 2.0f * half[1];
    float f = 1.0f;
    float sums[4][2] = { { radii[0] + radii[1], w },   // top
                         { radii[3] + radii[2], w },   // bottom
                         { radii[0] + radii[3], h },   // left
                         { radii[1] + radii[2], h } }; // right
    for (int i = 0; i < 4; i++)
        if (sums[i][0] > sums[i][1] && sums[i][0] > 0.0f)
            f = fminf(f, sums[i][1] / sums[i][0]);
    for (int i = 0; i < 4; i++)
        radii[i] *= f;
}

// Shared prim tail: identity transform, box from min/max, slot bump.
// Returns claimed index, ANO_UI_REF_NONE when full.
static uint32_t prim_push(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                          uint32_t kind, uint32_t flags, uint32_t paintRef, uint32_t clipRef)
{
    if (b->primCount >= b->primCap)
        return ANO_UI_REF_NONE;
    uint32_t idx = b->primCount++;
    AnoUiPrim *p = &b->prims[idx];
    p->inv[0] = 1.0f; p->inv[1] = 0.0f; p->inv[2] = 0.0f; p->inv[3] = 1.0f;
    p->origin[0] = 0.5f * (rectMin[0] + rectMax[0]);
    p->origin[1] = 0.5f * (rectMin[1] + rectMax[1]);
    p->half[0] = 0.5f * (rectMax[0] - rectMin[0]);
    p->half[1] = 0.5f * (rectMax[1] - rectMin[1]);
    p->kind = kind;
    p->flags = flags;
    p->param[0] = 0.0f; p->param[1] = 0.0f;
    for (int i = 0; i < 4; i++) {
        p->radii[i] = 0.0f;
        p->color[i] = 0.0f;
    }
    p->paintRef = paintRef;
    p->clipRef = clipRef;
    p->aux0 = 0;
    p->aux1 = 0;
    return idx;
}

// In: min<=max box, radii, color, borderWidth (>=0: 0=fill else ring). Out: index or NONE.
uint32_t ano_ui_rrect(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], const float color[4], float borderWidth,
                      uint32_t paintRef, uint32_t clipRef, uint32_t flags)
{
    uint32_t idx = prim_push(b, rectMin, rectMax, ANO_UI_RRECT, flags, paintRef, clipRef);
    if (idx == ANO_UI_REF_NONE)
        return idx;
    AnoUiPrim *p = &b->prims[idx];
    for (int i = 0; i < 4; i++) {
        p->radii[i] = radii ? radii[i] : 0.0f;
        p->color[i] = color[i];
    }
    radii_clamp(p->radii, p->half);
    p->param[0] = fmaxf(borderWidth, 0.0f);
    return idx;
}

// In: rrect box, uniform cornerRadius, sigma (clamped >= 1e-3), color.
// Cull pad for later lanes: half + 3*sigma + 1px AA.
uint32_t ano_ui_shadow(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                       float cornerRadius, float sigma, const float color[4],
                       uint32_t clipRef, uint32_t flags)
{
    uint32_t idx = prim_push(b, rectMin, rectMax, ANO_UI_SHADOW, flags, ANO_UI_REF_NONE, clipRef);
    if (idx == ANO_UI_REF_NONE)
        return idx;
    AnoUiPrim *p = &b->prims[idx];
    float r[4] = { cornerRadius, cornerRadius, cornerRadius, cornerRadius };
    radii_clamp(r, p->half);
    for (int i = 0; i < 4; i++) {
        p->radii[i] = r[0];
        p->color[i] = color[i];
    }
    p->param[0] = fmaxf(sigma, 1e-3f);
    return idx;
}

// In: box, radii, bindless tex index, lod, tint. Full texture maps to box (uv 0..1).
uint32_t ano_ui_image(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], uint32_t texIndex, float lod,
                      const float tint[4], uint32_t clipRef, uint32_t flags)
{
    uint32_t idx = prim_push(b, rectMin, rectMax, ANO_UI_IMAGE, flags, ANO_UI_REF_NONE, clipRef);
    if (idx == ANO_UI_REF_NONE)
        return idx;
    AnoUiPrim *p = &b->prims[idx];
    for (int i = 0; i < 4; i++) {
        p->radii[i] = radii ? radii[i] : 0.0f;
        p->color[i] = tint[i];
    }
    radii_clamp(p->radii, p->half);
    p->param[0] = fmaxf(lod, 0.0f);
    p->aux0 = texIndex;
    return idx;
}

// In: conservative bbox (logical), curveOffset + monotone-quad count, color.
uint32_t ano_ui_path(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                     uint32_t curveOffset, uint32_t curveCount, const float color[4],
                     uint32_t paintRef, uint32_t clipRef, uint32_t flags)
{
    uint32_t idx = prim_push(b, bboxMin, bboxMax, ANO_UI_PATH, flags, paintRef, clipRef);
    if (idx == ANO_UI_REF_NONE)
        return idx;
    AnoUiPrim *p = &b->prims[idx];
    for (int i = 0; i < 4; i++)
        p->color[i] = color[i];
    p->aux0 = curveOffset;
    p->aux1 = curveCount;
    return idx;
}

// In: shaped-text bbox (logical), AnoGlyphInstance range, tint.
uint32_t ano_ui_glyphs(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                       uint32_t first, uint32_t count, const float tint[4],
                       uint32_t clipRef, uint32_t flags)
{
    uint32_t idx = prim_push(b, bboxMin, bboxMax, ANO_UI_GLYPHS, flags, ANO_UI_REF_NONE, clipRef);
    if (idx == ANO_UI_REF_NONE)
        return idx;
    AnoUiPrim *p = &b->prims[idx];
    for (int i = 0; i < 4; i++)
        p->color[i] = tint[i];
    p->aux0 = first;
    p->aux1 = count;
    return idx;
}


/* Paints */

// Copy stops sorted ascending by t, write paint header. NONE if full or stopCount 0.
static uint32_t paint_push(AnoUiBuilder *b, uint32_t kind, const float xform[6],
                           const AnoUiStop *stops, uint32_t stopCount)
{
    if (stopCount == 0 || b->paintCount >= b->paintCap || b->stopCount + stopCount > b->stopCap)
        return ANO_UI_REF_NONE;
    uint32_t stopFirst = b->stopCount;
    for (uint32_t i = 0; i < stopCount; i++)
        b->stops[stopFirst + i] = stops[i];
    for (uint32_t i = 1; i < stopCount; i++) {
        AnoUiStop key = b->stops[stopFirst + i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && b->stops[stopFirst + j].t > key.t) {
            b->stops[stopFirst + j + 1] = b->stops[stopFirst + j];
            j--;
        }
        b->stops[stopFirst + j + 1] = key;
    }
    b->stopCount += stopCount;
    uint32_t idx = b->paintCount++;
    AnoUiPaint *pa = &b->paints[idx];
    pa->kind = kind;
    pa->stopFirst = stopFirst;
    pa->stopCount = stopCount;
    pa->flags = 0;
    for (int i = 0; i < 6; i++)
        pa->xform[i] = xform[i];
    pa->pad[0] = pa->pad[1] = 0.0f;
    return idx;
}

// In: axis endpoints + stops. t = dot(p-p0,d)/|d|^2. Zero-length axis -> t = 0.
uint32_t ano_ui_paint_linear(AnoUiBuilder *b, const float p0[2], const float p1[2],
                             const AnoUiStop *stops, uint32_t stopCount)
{
    float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
    float l2 = dx * dx + dy * dy;
    float inv = l2 > 0.0f ? 1.0f / l2 : 0.0f;
    float xform[6] = { dx * inv, dy * inv, -(p0[0] * dx + p0[1] * dy) * inv, 0.0f, 0.0f, 0.0f };
    return paint_push(b, ANO_UI_GRAD_LINEAR, xform, stops, stopCount);
}

// In: center + radius + stops. g = (p-center)/radius, t = |g|. radius <= 0 -> t = 0.
uint32_t ano_ui_paint_radial(AnoUiBuilder *b, const float center[2], float radius,
                             const AnoUiStop *stops, uint32_t stopCount)
{
    float inv = radius > 0.0f ? 1.0f / radius : 0.0f;
    float xform[6] = { inv, 0.0f, -center[0] * inv, 0.0f, inv, -center[1] * inv };
    return paint_push(b, ANO_UI_GRAD_RADIAL, xform, stops, stopCount);
}

// In: center, startAngle (radians), stops. xform rotates startAngle onto g.x.
// Evaluator: atan2(g.y,g.x)/2pi + 0.5.
uint32_t ano_ui_paint_conic(AnoUiBuilder *b, const float center[2], float startAngle,
                            const AnoUiStop *stops, uint32_t stopCount)
{
    float c = cosf(startAngle), s = sinf(startAngle);
    float xform[6] = { c, s, -(c * center[0] + s * center[1]),
                       -s, c, -(-s * center[0] + c * center[1]) };
    return paint_push(b, ANO_UI_GRAD_CONIC, xform, stops, stopCount);
}


/* Clips */

// In: clip rect; optional rounded term (rrMin NULL = rect only). Out: clip index or NONE.
uint32_t ano_ui_clip(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                     const float rrMin[2], const float rrMax[2], const float rrRadii[4])
{
    if (b->clipCount >= b->clipCap)
        return ANO_UI_REF_NONE;
    uint32_t idx = b->clipCount++;
    AnoUiClip *c = &b->clips[idx];
    c->rect[0] = rectMin[0]; c->rect[1] = rectMin[1];
    c->rect[2] = rectMax[0]; c->rect[3] = rectMax[1];
    if (rrMin && rrMax) {
        c->rrCenter[0] = 0.5f * (rrMin[0] + rrMax[0]);
        c->rrCenter[1] = 0.5f * (rrMin[1] + rrMax[1]);
        c->rrHalf[0] = 0.5f * (rrMax[0] - rrMin[0]);
        c->rrHalf[1] = 0.5f * (rrMax[1] - rrMin[1]);
        for (int i = 0; i < 4; i++)
            c->rrRadii[i] = rrRadii ? rrRadii[i] : 0.0f;
        radii_clamp(c->rrRadii, c->rrHalf);
    } else {
        c->rrCenter[0] = 0.0f; c->rrCenter[1] = 0.0f;
        c->rrHalf[0] = -1.0f;  c->rrHalf[1] = -1.0f; // sentinel: no rounded term
        for (int i = 0; i < 4; i++)
            c->rrRadii[i] = 0.0f;
    }
    return idx;
}
