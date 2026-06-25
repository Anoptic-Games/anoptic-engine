#version 450

// Separable Gaussian prefilter for the moment shadow atlas. Run twice per active layer (X then Y).
// The stored optimized-moment encoding is an affine transform of the raw moments, so a normalized
// linear filter of the stored values is itself a valid optimized-moment vector — i.e. blurring here
// is exactly "filter then reconstruct", which is what turns moment maps into soft shadows. Vertex
// stage is the shared fullscreen triangle (tonemap.vert), giving uv in [0,1].

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outMoments;

layout(set = 0, binding = 0) uniform sampler2DArray src;

// dir = per-tap texel step ((1/dim,0) for X, (0,1/dim) for Y); layer = atlas array layer.
layout(push_constant) uniform Push {
    vec2 dir;
    int  layer;
    int  pad;
} pc;

// 9-tap Gaussian (sigma ~2), matching the paper's 9x9 kernel split into two passes. Weights are
// pre-normalized so the affine-filter property holds (sum == 1). Softness is the radius/sigma knob.
const float W[5] = float[5](0.20416369, 0.18017382, 0.12383154, 0.06628225, 0.02763055);

void main() {
    float l = float(pc.layer);
    vec4 sum = texture(src, vec3(uv, l)) * W[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = pc.dir * float(i);
        sum += texture(src, vec3(uv + off, l)) * W[i];
        sum += texture(src, vec3(uv - off, l)) * W[i];
    }
    outMoments = sum;
}
