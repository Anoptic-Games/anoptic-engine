// UI prim evaluator + ABI twins of include/anoptic_ui.h (AnoUiPrim 96 B, AnoUiClip
// 48 B, AnoUiPaint 48 B, AnoUiStop 32 B — offsets pinned by static_asserts there).
// The coverage/shadow math mirrors src/ui/ui_raster_ref.c statement for statement;
// fix bugs THERE first, then re-port. Declares set 0 bindings 4-7 and the set 1
// bindless texture array. Include AFTER textcoverage.glsl (reuses its em_box); the
// including shader needs GL_EXT_nonuniform_qualifier.

// Mirrors AnoUiPrimKind.
const uint UI_RRECT  = 0u;
const uint UI_SHADOW = 1u;
const uint UI_IMAGE  = 2u;
const uint UI_PATH   = 3u;
const uint UI_GLYPHS = 4u;

// Mirrors the flag/blend constants.
const uint UI_BLEND_OVER = 0x0u;
const uint UI_BLEND_ADD  = 0x1u;
const uint UI_BLEND_MASK = 0x3u;
const uint UI_FLAG_INNER = 0x4u;
const uint UI_REF_NONE   = 0xFFFFFFFFu;

// Mirrors the gradient kinds (AnoUiPaint.kind).
const uint UI_GRAD_LINEAR = 0u;
const uint UI_GRAD_RADIAL = 1u;
const uint UI_GRAD_CONIC  = 2u;

// Mirrors AnoUiPrim (96 B).
struct UiPrim {
    vec4  inv;      // 2x2 pixel->prim inverse, rows
    vec2  origin;   // prim center, overlay px, y-down
    uint  kind;
    uint  flags;
    vec2  halfExt;
    vec2  param;    // [0]: border width | sigma | lod
    vec4  radii;    // per-corner (tl, tr, br, bl)
    vec4  color;    // premultiplied linear; tint for IMAGE/GLYPHS
    uint  paintRef;
    uint  clipRef;
    uint  aux0;     // texIndex | curveOffset | glyph first
    uint  aux1;     // curveCount | glyph count
};

// Mirrors AnoUiClip (48 B). rrHalf.x < 0 = no rounded term.
struct UiClip {
    vec4 rect;      // minX, minY, maxX, maxY
    vec2 rrCenter;
    vec2 rrHalf;
    vec4 rrRadii;
};

// Mirrors AnoUiPaint (48 B) + AnoUiStop (32 B); evaluated from step 6 on.
struct UiPaint {
    uint kind;
    uint stopFirst;
    uint stopCount;
    uint flags;
    vec4 xform01;   // 2x3 pixel->gradient space, rows packed
    vec4 xform2;    // z,w padding
};

struct UiStop {
    vec4 color;     // premultiplied linear
    vec4 t;         // x = t, yzw padding
};

layout(std430, set = 0, binding = 4) readonly buffer UiPrims  { UiPrim  uiPrims[];  };
layout(std430, set = 0, binding = 5) readonly buffer UiClips  { UiClip  uiClips[];  };
layout(std430, set = 0, binding = 6) readonly buffer UiPaints { UiPaint uiPaints[]; };
layout(std430, set = 0, binding = 7) readonly buffer UiStops  { UiStop  uiStops[];  };
layout(std430, set = 0, binding = 8) readonly buffer UiCurves { uint    uiCurvePoints[]; };

layout(set = 1, binding = 0) uniform sampler2D uiTextures[];

// Exact signed distance to the rounded box (per-corner radii, y-down quadrant select).
// p relative to the box center. Mirrors ano_ui_ref_sd_rrect.
float ui_sd_rrect(vec2 p, vec2 halfExt, vec4 radii)
{
    float r = p.x >= 0.0 ? (p.y >= 0.0 ? radii.z : radii.y)
                         : (p.y >= 0.0 ? radii.w : radii.x);
    vec2 q = abs(p) - halfExt + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

// Rational erf (Wallace's constants). Mirrors ui_erf in ui_raster_ref.c.
float ui_erf(float x)
{
    float s = x >= 0.0 ? 1.0 : -1.0, a = abs(x);
    float y = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    y *= y;
    return s - s / (y * y);
}

float ui_gaussian(float x, float sigma)
{
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.5066282746310002 * sigma);
}

// Closed-form blur of the rounded box along x for the row at offset y from center.
float ui_shadow_x(float x, float y, float sigma, float corner, vec2 halfExt)
{
    float delta = min(halfExt.y - corner - abs(y), 0.0);
    float curved = halfExt.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    float k = 0.70710678 / sigma;
    return 0.5 * (ui_erf((x + curved) * k) - ui_erf((x - curved) * k));
}

// Gaussian-blurred rounded box at p (relative to center): erf along x, 4-sample
// quadrature along y truncated at 3 sigma. Mirrors ano_ui_ref_shadow.
float ui_shadow(vec2 p, vec2 halfExt, float corner, float sigma)
{
    float low = p.y - halfExt.y, high = p.y + halfExt.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float stepY = (end - start) / 4.0;
    float y = start + stepY * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++)
    {
        value += ui_shadow_x(p.x, p.y - y, sigma, corner, halfExt) * ui_gaussian(y, sigma) * stepY;
        y += stepY;
    }
    return value;
}

// Window coverage of one clip entry in overlay space: exact rect overlap times the
// rounded term's ramp. Invalid non-NONE refs fail CLOSED. Mirrors clip_cov.
float ui_clip_cov(uint ref, uint clipCount, vec2 pxTL)
{
    if (ref >= clipCount)
        return 0.0;
    UiClip c = uiClips[ref];
    float ox = max(0.0, min(pxTL.x + 1.0, c.rect.z) - max(pxTL.x, c.rect.x));
    float oy = max(0.0, min(pxTL.y + 1.0, c.rect.w) - max(pxTL.y, c.rect.y));
    float cov = ox * oy;
    if (c.rrHalf.x >= 0.0)
        cov *= clamp(0.5 - ui_sd_rrect(pxTL + 0.5 - c.rrCenter, c.rrHalf, c.rrRadii), 0.0, 1.0);
    return cov;
}

// Interpolated stop color at t, premultiplied linear, clamped to the end stops (CSS
// pad). Mirrors ui_stop_color in ui_raster_ref.c.
vec4 ui_stop_color(uint first, uint count, float t)
{
    if (t <= uiStops[first].t.x)
        return uiStops[first].color;
    uint last = first + count - 1u;
    if (t >= uiStops[last].t.x)
        return uiStops[last].color;
    for (uint i = 0u; i + 1u < count; i++)
    {
        float t0 = uiStops[first + i].t.x, t1 = uiStops[first + i + 1u].t.x;
        if (t < t1)
        {
            float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0;
            return mix(uiStops[first + i].color, uiStops[first + i + 1u].color, f);
        }
    }
    return uiStops[last].color;
}

// Resolved fill at overlay pixel px modulated by base. NONE returns base; an out-of-
// range paint/stop range fails CLOSED. Mirrors ano_ui_ref_paint.
vec4 ui_paint_eval(uint paintRef, uint paintCount, vec2 px, vec4 base)
{
    if (paintRef == UI_REF_NONE)
        return base;
    if (paintRef >= paintCount)
        return vec4(0.0);
    UiPaint pa = uiPaints[paintRef];
    if (pa.stopCount == 0u)
        return vec4(0.0);
    vec2 g = vec2(pa.xform01.x * px.x + pa.xform01.y * px.y + pa.xform01.z,
                  pa.xform01.w * px.x + pa.xform2.x * px.y + pa.xform2.y);
    float t = pa.kind == UI_GRAD_RADIAL ? length(g)
            : pa.kind == UI_GRAD_CONIC  ? atan(g.y, g.x) * 0.15915494309189535 + 0.5
                                        : g.x;
    return ui_stop_color(pa.stopFirst, pa.stopCount, t) * base;
}

// Path coverage over the window at wpos size wdim (prim-local): one stream walk from
// word `off` over curveCount monotone quads (SENTINEL restarts a contour), normalized by
// window area. Reuses curve_area/SENTINEL from textcoverage.glsl. Mirrors ui_path_sum.
float ui_path_sum(uint off, uint curveCount, vec2 wpos, vec2 wdim)
{
    uint i = off;
    vec2 p0 = unpackHalf2x16(uiCurvePoints[i]) - wpos;
    i++;
    float area = 0.0;
    for (uint c = 0u; c < curveCount; c++)
    {
        if (uiCurvePoints[i] == SENTINEL)
        {
            i++;
            p0 = unpackHalf2x16(uiCurvePoints[i]) - wpos;
            i++;
        }
        vec2 p1 = unpackHalf2x16(uiCurvePoints[i]) - wpos;
        i++;
        vec2 p2 = unpackHalf2x16(uiCurvePoints[i]) - wpos;
        i++;
        area += curve_area(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, wdim.x, wdim.y);
        p0 = p2;
    }
    return area / (wdim.x * wdim.y);
}

// Premultiplied contribution of prim idx over the unit window at pxTL. Mirrors
// ano_ui_ref_shade for RRECT/SHADOW/PATH; IMAGE additionally samples the bindless array
// (outside the CPU reference's reach — the self-test scene carries no images).
// GLYPHS lands in its own range walk. paintCount bounds the gradient fail-closed.
vec4 ui_shade(uint idx, vec2 pxTL, uint clipCount, uint paintCount)
{
    UiPrim p = uiPrims[idx];
    vec2 d = pxTL + 0.5 - p.origin;
    vec2 l = vec2(dot(p.inv.xy, d), dot(p.inv.zw, d));
    float cov;
    vec4 texel = vec4(1.0);
    if (p.kind == UI_RRECT || p.kind == UI_IMAGE)
    {
        float sd = ui_sd_rrect(l, p.halfExt, p.radii);
        cov = clamp(0.5 - sd, 0.0, 1.0);
        if (p.kind == UI_RRECT)
        {
            float w = p.param.x;
            if (w > 0.0)
                cov -= clamp(0.5 - (sd + w), 0.0, 1.0); // ring: outer minus eroded
        }
        else
        {
            vec2 uv = l / (2.0 * p.halfExt) + 0.5;
            texel = textureLod(uiTextures[nonuniformEXT(p.aux0)], uv, p.param.x);
            texel.rgb *= texel.a; // straight-alpha source -> premultiplied
        }
    }
    else if (p.kind == UI_SHADOW)
    {
        float a = ui_shadow(l, p.halfExt, p.radii.x, p.param.x);
        if ((p.flags & UI_FLAG_INNER) != 0u)
        {
            float sd = ui_sd_rrect(l, p.halfExt, p.radii);
            a = (1.0 - a) * clamp(0.5 - sd, 0.0, 1.0); // blur of the complement, masked inside
        }
        cov = a;
    }
    else if (p.kind == UI_PATH)
    {
        // Prim-local window box (identity inv in v0), swept over the shared curve stream.
        vec2 lo, hi;
        em_box(p.inv, pxTL - p.origin, pxTL + 1.0 - p.origin, lo, hi);
        cov = clamp(ui_path_sum(p.aux0, p.aux1, lo, hi - lo), 0.0, 1.0);
    }
    else if (p.kind == UI_GLYPHS)
    {
        // Glyph labels z-interleaved with the prims: walk the range in register-blend
        // order (shade_window bbox-rejects cheaply), tint, clip. aux0 was rebased at
        // compose into the frame buffer's UI glyph region.
        vec4 gacc = vec4(0.0);
        for (uint g = 0u; g < p.aux1; g++)
        {
            vec4 s = shade_window(p.aux0 + g, pxTL, pxTL + 1.0);
            gacc = s + gacc * (1.0 - s.a);
        }
        float cc = p.clipRef != UI_REF_NONE ? ui_clip_cov(p.clipRef, clipCount, pxTL) : 1.0;
        return gacc * p.color * cc;
    }
    else
    {
        return vec4(0.0);
    }
    if (p.clipRef != UI_REF_NONE)
        cov *= ui_clip_cov(p.clipRef, clipCount, pxTL);
    // Fill = gradient paint or the prim color (NONE), times any image texel.
    return ui_paint_eval(p.paintRef, paintCount, pxTL + 0.5, p.color) * texel * cov;
}

// Does prim idx's padded box touch the screen rect [rMin,rMax]? Shadow prims pad by
// their 3-sigma reach, everything else by the 1 px AA ramp.
bool ui_box_hits(uint idx, vec2 rMin, vec2 rMax)
{
    UiPrim p = uiPrims[idx];
    float pad = (p.kind == UI_SHADOW) ? 3.0 * p.param.x + 1.0 : 1.0;
    vec2 lo, hi;
    em_box(p.inv, rMin - p.origin, rMax - p.origin, lo, hi);
    vec2 ext = p.halfExt + pad;
    return lo.x < ext.x && hi.x > -ext.x && lo.y < ext.y && hi.y > -ext.y;
}
