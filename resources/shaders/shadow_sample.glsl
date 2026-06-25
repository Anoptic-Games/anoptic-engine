// Moment-shadow-map sampling wrapper, shared by flat.frag and transmission.frag. Include AFTER the
// set-2 declarations (sampler2DArray shadowAtlas + the ShadowFrustumSSBO `shadowFrustumBuf`), which
// both lighting frags declare identically — this file references them by name. Returns the same
// "1 lit .. 0 shadowed" factor the old PCF path did, so the call sites are unchanged.

#ifndef ANO_SHADOW_SAMPLE_GLSL
#define ANO_SHADOW_SAMPLE_GLSL

#include "shadow_moments.glsl"

// Hardware-tunable knobs (validated visually on the GPU; the sandbox cannot run the renderer):
//   ALPHA       — moment bias toward (0.5)^4; 3e-5 is the paper's value for 16-bit quantization.
//   DEPTH_BIAS  — constant occluder offset killing residual self-shadow; slope-scaled by nDotL.
//   LIGHT_BLEED — linstep cutoff on visibility: clamps the low-visibility tail (leaked light inside
//                 shadows, worst in the blur's seam band) to fully shadowed. Raise to clip seam
//                 bleed; too high eats legitimate penumbra. This is the primary anti-bleed knob.
const float ANO_MSM_ALPHA       = 3e-5;
const float ANO_MSM_DEPTH_BIAS  = 0.0002;
const float ANO_MSM_LIGHT_BLEED = 0.40;

// Reconstruct the lit factor for `worldPos` against shadow-frustum/atlas-layer `layer`. Outside the
// map or beyond the far plane returns 1.0 (lit) — no false self-shadowing at seams (matches PCF).
float sampleShadowMSM(uint layer, vec3 worldPos, float nDotL) {
    vec4 lc = shadowFrustumBuf.shadowFrustums[layer].viewProj * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    vec4 b = anoDecodeMoments(texture(shadowAtlas, vec3(uv, float(layer))));
    float depthBias = ANO_MSM_DEPTH_BIAS * (1.0 + (1.0 - nDotL));
    float g = anoMSMShadow(b, proj.z, ANO_MSM_ALPHA, depthBias);
    // Light-bleed reduction is a linstep on VISIBILITY (the lit factor), not on g. It pushes the
    // low-visibility tail to zero — leaked light sitting inside a shadow, e.g. where the prefilter
    // blur averaged an occluder's moments against the far-plane clear at a silhouette and the solve
    // returned a spurious partial g — while leaving genuinely-lit texels intact. (Applying it to g
    // and inverting would brighten partial shadows: the wrong direction, and the knob would fight you.)
    float visibility = 1.0 - g;
    visibility = clamp((visibility - ANO_MSM_LIGHT_BLEED) / (1.0 - ANO_MSM_LIGHT_BLEED), 0.0, 1.0);
    return visibility;
}

#endif
