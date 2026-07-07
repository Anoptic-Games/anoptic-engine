#version 450
#extension GL_GOOGLE_include_directive : require

// World-space per-glyph quads: each fragment integrates its glyph over the footprint window from screen derivatives. Output is premultiplied.

#include "textcoverage.glsl"

layout(push_constant) uniform TextWorldPush {
    mat4  mvp;
    vec4  panel;
    vec2  viewport;
    uvec2 range;  // first index, count
} pc;

layout(location = 0) in vec2 panelPos;
layout(location = 1) flat in uint instIdx;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 fw = abs(dFdx(panelPos)) + abs(dFdy(panelPos));
    outColor = shade_window(instIdx, panelPos - 0.5 * fw, panelPos + 0.5 * fw);
}
