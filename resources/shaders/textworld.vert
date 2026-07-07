#version 450
#extension GL_GOOGLE_include_directive : require

// World-space text panel, one padded per-glyph quad per instance.
// Emits panel-space text coords (pixels y-down) plus the flat instance index.

#include "textcoverage.glsl"

layout(push_constant) uniform TextWorldPush {
    mat4  mvp;      // proj * view * model
    vec4  panel;    // panel pixel W,H | panel world W,H
    vec2  viewport; // this view's viewport W,H in screen px
    uvec2 range;    // first instance index, instance count
} pc;

layout(location = 0) out vec2 panelPos;
layout(location = 1) flat out uint instIdx;

const float PAD_PX  = 1.5;  // footprint window + MSAA slack
const float PAD_MAX = 64.0; // panel px

const vec2 uvs[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                            vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

// Panel-space text coords (pixels y-down) -> model-local quad position.
vec3 panel_local(vec2 t)
{
    return vec3((t.x / pc.panel.x - 0.5) * pc.panel.z,
                (0.5 - t.y / pc.panel.y) * pc.panel.w, 0.0);
}

void main()
{
    uint idx = pc.range.x + uint(gl_InstanceIndex);
    instIdx = idx;
    GlyphInstance gi = instances[idx];
    GlyphEntry g = glyphs[gi.glyphID];

    // Glyph em bbox -> text-space AABB about the pen, interval arithmetic per axis.
    float det = gi.inv.x * gi.inv.w - gi.inv.y * gi.inv.z;
    vec2 tMin = gi.origin, tMax = gi.origin;
    if (g.curveCount != 0u && det != 0.0)
    {
        vec2 colX = vec2(gi.inv.w, -gi.inv.z) / det;
        vec2 colY = vec2(-gi.inv.y, gi.inv.x) / det;
        vec2 a = colX * g.bboxMin.x, b = colX * g.bboxMax.x;
        vec2 c = colY * g.bboxMin.y, d = colY * g.bboxMax.y;
        tMin += min(a, b) + min(c, d);
        tMax += max(a, b) + max(c, d);

        // Pad = PAD_PX screen px in panel units, panel-axis NDC derivatives at the quad center.
        vec4 cc = pc.mvp * vec4(panel_local(0.5 * (tMin + tMax)), 1.0);
        float ws = (cc.w < 0.0) ? min(cc.w, -1e-6) : max(cc.w, 1e-6);
        vec2 ndc = cc.xy / ws;
        float kx = pc.panel.z / pc.panel.x; // world units per panel px
        float ky = pc.panel.w / pc.panel.y;
        vec2 halfVp = 0.5 * pc.viewport;
        float sppx = length((pc.mvp[0].xy - ndc * pc.mvp[0].w) * (kx / ws) * halfVp);
        float sppy = length((pc.mvp[1].xy - ndc * pc.mvp[1].w) * (ky / ws) * halfVp);
        vec2 pad = min(vec2(PAD_PX) / max(vec2(sppx, sppy), 1e-6), PAD_MAX);
        // Clamp to the panel rect, collapse a fully-outside glyph.
        tMin = max(tMin - pad, vec2(0.0));
        tMax = min(tMax + pad, pc.panel.xy);
        tMax = max(tMax, tMin);
    }

    vec2 t = mix(tMin, tMax, uvs[gl_VertexIndex]);
    panelPos = t;
    gl_Position = pc.mvp * vec4(panel_local(t), 1.0);
}
