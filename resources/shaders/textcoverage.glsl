// Scanline Sweeper coverage math shared by textraster.comp and textworld.frag.
// Mirrors src/text/text_raster_ref.c statement for statement. Declares the three
// glyph-data buffers (set 0, bindings 0/1/2). The including shader adds its own extras.

// Mirrors AnoGlyphEntry (32 B).
struct GlyphEntry {
    uint  pointOffset;
    uint  curveCount;
    vec2  bboxMin;
    vec2  bboxMax;
    float advance;
    uint  flags;
};

// Mirrors AnoGlyphInstance, 48 B. Offsets pinned by static_asserts in anoptic_text.h.
struct GlyphInstance {
    vec4 inv;     // 2x2 pixel->em inverse, rows: em = (dot(inv.xy,d), dot(inv.zw,d))
    vec4 color;   // premultiplied linear RGBA
    vec2 origin;  // baseline pen, text-space pixels (y-down)
    uint glyphID;
    uint flags;
};

layout(std430, set = 0, binding = 0) readonly buffer CurvePoints { uint points[]; };
layout(std430, set = 0, binding = 1) readonly buffer GlyphDirectory { GlyphEntry glyphs[]; };
layout(std430, set = 0, binding = 2) readonly buffer FrameData { GlyphInstance instances[]; };

const uint SENTINEL = 0x7C007C00u; // contour separator: both halves +inf

// Single root of a monotone quadratic component hitting `target`, clamped to [0,1],
// citardauq form 2c/(-b - sign(span)*sqrt(d)).
float solve_mono(float c0, float c1, float c2, float target)
{
    float span = c2 - c0;
    float a = c0 - 2.0 * c1 + c2;
    float b = 2.0 * (c1 - c0);
    float c = c0 - target;
    float d = max(b * b - 4.0 * a * c, 0.0);
    float den = -b - (span < 0.0 ? -sqrt(d) : sqrt(d));
    if (den == 0.0)
        return 0.0;
    return clamp(2.0 * c / den, 0.0, 1.0);
}

// Signed area swept between one monotone quad and the window's right edge, clipped to
// [0,w]x[0,h]. Curve coordinates arrive window-local. Mirrors curve_area() in text_raster_ref.c.
float curve_area(float x0, float y0, float x1, float y1, float x2, float y2,
                 float w, float h)
{
    if (y0 == y2)
        return 0.0;
    if (max(y0, y2) <= 0.0 || min(y0, y2) >= h || min(x0, x2) >= w)
        return 0.0;

    if (x0 == x2)
    {
        float b = min(w, w - x0);
        return (clamp(y2, 0.0, h) - clamp(y0, 0.0, h)) * b;
    }

    float t0 = solve_mono(y0, y1, y2, 0.0);
    float t1 = solve_mono(y0, y1, y2, h);
    if (t0 > t1)
    {
        float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    float ta = clamp(solve_mono(x0, x1, x2, 0.0), t0, t1);
    float tb = clamp(solve_mono(x0, x1, x2, w), t0, t1);
    if (ta > tb)
    {
        float tmp = ta;
        ta = tb;
        tb = tmp;
    }

    float ax = x0 - 2.0 * x1 + x2, bx = 2.0 * (x1 - x0);
    float ay = y0 - 2.0 * y1 + y2, by = 2.0 * (y1 - y0);
    float xs = clamp((ax * t0 + bx) * t0 + x0, 0.0, w);
    float ys = clamp((ay * t0 + by) * t0 + y0, 0.0, h);
    float area = 0.0;
    float ends[3] = float[3](ta, tb, t1);
    for (int k = 0; k < 3; k++)
    {
        float te = ends[k];
        float xe = clamp((ax * te + bx) * te + x0, 0.0, w);
        float ye = clamp((ay * te + by) * te + y0, 0.0, h);
        area += (ye - ys) * (2.0 * w - xs - xe) * 0.5;
        xs = xe;
        ys = ye;
    }
    return area;
}

// Unclamped coverage sum for one em-space window at wpos, size wdim: one stream walk
// (grammar in anoptic_text.h), normalized by window area. Mirrors ano_text_window_sum().
float window_sum(GlyphEntry g, vec2 wpos, vec2 wdim)
{
    uint i = g.pointOffset;
    vec2 p0 = unpackHalf2x16(points[i]) - wpos;
    i++;
    float area = 0.0;
    for (uint c = 0u; c < g.curveCount; c++)
    {
        if (points[i] == SENTINEL) // contour restart
        {
            i++;
            p0 = unpackHalf2x16(points[i]) - wpos;
            i++;
        }
        vec2 p1 = unpackHalf2x16(points[i]) - wpos;
        i++;
        vec2 p2 = unpackHalf2x16(points[i]) - wpos;
        i++;
        area += curve_area(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, wdim.x, wdim.y);
        p0 = p2;
    }
    return area / (wdim.x * wdim.y);
}

// Text-space rect corners (relative to instance origin) -> em-space bbox under the
// instance's pixel->em transform, the footprint parallelogram's bounds.
void em_box(vec4 inv, vec2 rMin, vec2 rMax, out vec2 lo, out vec2 hi)
{
    vec2 c10 = vec2(rMax.x, rMin.y);
    vec2 c01 = vec2(rMin.x, rMax.y);
    vec2 e00 = vec2(dot(inv.xy, rMin), dot(inv.zw, rMin));
    vec2 e11 = vec2(dot(inv.xy, rMax), dot(inv.zw, rMax));
    vec2 e10 = vec2(dot(inv.xy, c10), dot(inv.zw, c10));
    vec2 e01 = vec2(dot(inv.xy, c01), dot(inv.zw, c01));
    lo = min(min(e00, e11), min(e10, e01));
    hi = max(max(e00, e11), max(e10, e01));
}

// Premultiplied contribution of one instance over the text-space window [rMin, rMax]:
// per-glyph coverage clamp times instance color.
vec4 shade_window(uint idx, vec2 rMin, vec2 rMax)
{
    GlyphInstance gi = instances[idx];
    GlyphEntry g = glyphs[gi.glyphID];
    if (g.curveCount == 0u)
        return vec4(0.0);
    vec2 lo, hi;
    em_box(gi.inv, rMin - gi.origin, rMax - gi.origin, lo, hi);
    if (lo.x >= g.bboxMax.x || hi.x <= g.bboxMin.x || lo.y >= g.bboxMax.y || hi.y <= g.bboxMin.y)
        return vec4(0.0);
    float cov = clamp(window_sum(g, lo, hi - lo), 0.0, 1.0);
    return gi.color * cov;
}
