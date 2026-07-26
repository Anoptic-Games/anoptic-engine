#version 450

// Separable box prefilter for layered Power CDF atlas. Bilinear-paired: 5 fetches/pass. X then Y per active sublayer.
// Uniform box mean of 2R+1 texels (R = ANO_CDF_FILTER_RADIUS). Vertex: tonemap.vert fullscreen triangle. UV on texel centres.
// 5 taps: centre alone + 4 outer pairs at midpoints (±1.5, ±3.5). Pair x2 + centre x1, then /(2R+1).
// Offsets/weights hand-derived for even R==4. Regenerate if R changes.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outStats;

layout(set = 0, binding = 0) uniform sampler2DArray src;

// dir = per-tap texel step ((1/dim,0) for X, (0,1/dim) for Y); layer = atlas array sublayer.
layout(push_constant) uniform Push {
    vec2 dir;
    int  layer;
    int  pad;
} pc;

const int ANO_CDF_FILTER_RADIUS = 4; // footprint half-width in texels; larger = softer shadows

void main() {
    float l = float(pc.layer);

    // Centre alone + 4 bilinear pair midpoints (frac 0.5 = two-texel avg each).
    vec4 c  = texture(src, vec3(uv,                l)); // texel  0     (weight 1)
    vec4 p1 = texture(src, vec3(uv + pc.dir * 1.5, l)); // texels +1,+2 (0.5 avg -> weight 2)
    vec4 p2 = texture(src, vec3(uv + pc.dir * 3.5, l)); // texels +3,+4 (0.5 avg -> weight 2)
    vec4 n1 = texture(src, vec3(uv - pc.dir * 1.5, l)); // texels -1,-2 (0.5 avg -> weight 2)
    vec4 n2 = texture(src, vec3(uv - pc.dir * 3.5, l)); // texels -3,-4 (0.5 avg -> weight 2)

    // x2 restores the 8 outer texels from their 4 half-averages; + centre = all 9; /(2R+1) = box mean.
    vec4 pairs = (p1 + n1) + (p2 + n2);
    outStats = (c + 2.0 * pairs) / float(2 * ANO_CDF_FILTER_RADIUS + 1);
}
