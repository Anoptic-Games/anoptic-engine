#version 450

// World-space Scanline Sweeper lane: per-fragment analytic coverage over the shared
// glyph buffers. The em-space window is the fragment's panel-space footprint from
// screen derivatives, so the AA adapts to distance and obliquity for free (grazing
// views widen the window and the text self-filters instead of shimmering). Output is
// premultiplied; the pipeline blends src-over onto the HDR target.

#include "textcoverage.glsl"

layout(push_constant) uniform TextWorldPush {
    mat4  mvp;
    vec4  panel;
    uvec2 range;  // first instance index, instance count
} pc;

layout(location = 0) in vec2 panelPos;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 fw = abs(dFdx(panelPos)) + abs(dFdy(panelPos));
    vec2 rMin = panelPos - 0.5 * fw;
    vec2 rMax = panelPos + 0.5 * fw;
    vec4 acc = vec4(0.0);
    for (uint i = 0u; i < pc.range.y; i++)
    {
        vec4 src = shade_window(pc.range.x + i, rMin, rMax);
        acc = src + acc * (1.0 - src.a);
    }
    outColor = acc;
}
