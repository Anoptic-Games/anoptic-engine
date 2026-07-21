#version 450

// Separable box prefilter for the layered Power CDF atlas, bilinear-paired: 5 fetches/pass (was 9). Run
// twice per active atlas sublayer (X then Y). Every channel is a linearly-filterable quantity (per-band
// coverage, or coverage*meanDepth), so the footprint is a plain uniform box average of the 2R+1 texels 〜
// no min/max, and it stays separable (2D box mean = mean of row-means, equal counts). The footprint radius
// sets shadow softness (the coverage gradient across a silhouette is the penumbra). Vertex stage is the
// shared fullscreen triangle (tonemap.vert); uv lands on exact texel centres (target is 1:1 with source).
//
// The 9 box texels (offsets -4..+4, R = ANO_CDF_FILTER_RADIUS = 4) are gathered in ceil((2R+1)/2) = 5
// fetches instead of 9: the CENTRE texel is sampled alone, and each adjacent OUTER PAIR is fetched as ONE
// linear sample at the pair's shared midpoint (offsets +-1.5, +-3.5 texels). dir is exactly one texel step
// (1/ANO_SHADOW_DIM, a power of two) and uv is a texel centre, so a midpoint lands exactly on a texel
// boundary: the subtexel fraction is exactly 0.5 and the LINEAR sampler returns the exact 0.5/0.5 average
// of the two texels. Weighting each pair tap x2 and the centre x1 reproduces the sum of all 9 texels;
// /(2R+1) recovers the box mean. This equals the old point-sampled /9 sum under a full-precision reference
// filter; on the R16G16B16A16_UNORM atlas the hardware does the 0.5 blend in fixed point, so it matches to
// within one UNORM16 code (visually identical, and the box mean is already a 16-bit quantity). Edge extent
// is unchanged (the outer taps still reach texel +-4); with CLAMP_TO_EDGE both members of an out-of-range
// pair take the same edge texel, so their 0.5 average equals what the two old point taps would have
// summed. The 5 fetches are independent (balanced-tree reduce, no serial sum+= chain), so the pass is no
// longer latency-bound on the dependent adds either.
//
// The explicit tap layout assumes R is EVEN (=4) so every outer texel pairs cleanly. ANO_CDF_FILTER_RADIUS
// now feeds only the 2R+1 normaliser; the offsets/weights below are hand-derived for R==4 and must be
// regenerated if R changes (odd R leaves one lone outer texel per side -> ceil((2R+1)/2) taps, still exact).

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

    // Centre alone + 4 bilinear pair-midpoints; each midpoint tap (frac 0.5) returns the average of its
    // two box texels, so one fetch covers two.
    vec4 c  = texture(src, vec3(uv,                l)); // texel  0     (weight 1)
    vec4 p1 = texture(src, vec3(uv + pc.dir * 1.5, l)); // texels +1,+2 (0.5 avg -> weight 2)
    vec4 p2 = texture(src, vec3(uv + pc.dir * 3.5, l)); // texels +3,+4 (0.5 avg -> weight 2)
    vec4 n1 = texture(src, vec3(uv - pc.dir * 1.5, l)); // texels -1,-2 (0.5 avg -> weight 2)
    vec4 n2 = texture(src, vec3(uv - pc.dir * 3.5, l)); // texels -3,-4 (0.5 avg -> weight 2)

    // x2 restores the 8 outer texels from their 4 half-averages; + centre = all 9; /(2R+1) = box mean.
    vec4 pairs = (p1 + n1) + (p2 + n2);
    outStats = (c + 2.0 * pairs) / float(2 * ANO_CDF_FILTER_RADIUS + 1);
}
