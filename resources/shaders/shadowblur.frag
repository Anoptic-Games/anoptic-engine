#version 450

// Separable min/max/mean prefilter for the Power CDF shadow atlas. Run twice per active layer (X then
// Y). Channels: R = min occluder depth over the footprint, G = max, B = mean. min/max are morphological
// (unweighted) and mean is a uniform box average; all three are separable, so a 2D footprint reduces to
// two 1D passes exactly (2D box min = min of row-mins; box mean = mean of row-means, equal counts).
// The footprint radius sets shadow softness. Reconstruction (shadow_cdf.glsl) fits a power-law CDF to
// (min,max,mean), which is inherently leak-free across the near/far depth discontinuity at a silhouette
// — so no depth-aware / bilateral weighting is needed (that was the moment map's leak workaround).
// Vertex stage is the shared fullscreen triangle (tonemap.vert), giving uv in [0,1]; the per-tap step
// lands on exact texel centres, so the linear sampler returns unfiltered stored values.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outStats;

layout(set = 0, binding = 0) uniform sampler2DArray src;

// dir = per-tap texel step ((1/dim,0) for X, (0,1/dim) for Y); layer = atlas array layer.
layout(push_constant) uniform Push {
    vec2 dir;
    int  layer;
    int  pad;
} pc;

const int ANO_CDF_FILTER_RADIUS = 4; // footprint half-width in texels; larger = softer shadows

void main() {
    float l   = float(pc.layer);
    vec4  c   = texture(src, vec3(uv, l));
    float mn  = c.r;
    float mx  = c.g;
    float sum = c.b;
    for (int i = 1; i <= ANO_CDF_FILTER_RADIUS; ++i) {
        vec2 off = pc.dir * float(i);
        vec4 sp = texture(src, vec3(uv + off, l));
        vec4 sn = texture(src, vec3(uv - off, l));
        mn   = min(mn, min(sp.r, sn.r));
        mx   = max(mx, max(sp.g, sn.g));
        sum += sp.b + sn.b;
    }
    float mean = sum / float(2 * ANO_CDF_FILTER_RADIUS + 1);
    outStats = vec4(mn, mx, mean, 1.0);
}
