#version 450

// Separable box prefilter for the layered Power CDF atlas. Run twice per active atlas sublayer (X then
// Y). Every channel is a linearly-filterable quantity (per-band coverage, or coverage*meanDepth), so the
// footprint is a plain uniform box average of the 4 channels — no min/max, and it stays separable (2D box
// mean = mean of row-means, equal counts). The footprint radius sets shadow softness (the coverage
// gradient across a silhouette is the penumbra). Vertex stage is the shared fullscreen triangle
// (tonemap.vert), giving uv in [0,1]; the per-tap step lands on exact texel centres, so the linear
// sampler returns unfiltered stored values.

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
    float l   = float(pc.layer);
    vec4  sum = texture(src, vec3(uv, l));
    for (int i = 1; i <= ANO_CDF_FILTER_RADIUS; ++i) {
        vec2 off = pc.dir * float(i);
        sum += texture(src, vec3(uv + off, l));
        sum += texture(src, vec3(uv - off, l));
    }
    outStats = sum / float(2 * ANO_CDF_FILTER_RADIUS + 1);
}
