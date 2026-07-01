// Layered Power CDF shadow sampling wrapper, shared by flat.frag and transmission.frag. Include AFTER
// the set-2 declarations (sampler2DArray shadowAtlas + the ShadowFrustumSSBO `shadowFrustumBuf`), which
// both lighting frags declare identically — this file references them by name. Returns the same
// "1 lit .. 0 shadowed" factor the old PCF/MSM paths did, so the call sites are unchanged (they still
// pass a FRUSTUM index; this maps it to the frustum's two atlas sublayers internally).

#ifndef ANO_SHADOW_SAMPLE_GLSL
#define ANO_SHADOW_SAMPLE_GLSL

#include "shadow_cdf.glsl"

// Hardware-tunable knobs (the sandbox cannot run the renderer; tune visually on the GPU):
//   DEPTH_BIAS   — constant occluder offset killing residual self-shadow acne; slope-scaled by nDotL.
//   CONTACT_SOFT — within-band soft-step half-width (ZO depth units). 0 = hard/exact step at the band
//                  mean (crisp contact, no acne); larger softens the depth-direction contact transition
//                  for low-res, at some contact accuracy (validated stable to ~0.03, frays past ~0.06).
//                  Silhouette-edge softness is independent of this (it comes from the coverage gradient).
const float ANO_CDF_DEPTH_BIAS   = 0.0002;
const float ANO_CDF_CONTACT_SOFT = 0.02;

// Reconstruct the lit factor for `worldPos` against shadow FRUSTUM `frustum` (its two atlas sublayers).
// Outside the map or beyond the far plane returns 1.0 (lit) — no false self-shadowing at seams.
float sampleShadowCDF(uint frustum, vec3 worldPos, float nDotL) {
    vec4 lc = shadowFrustumBuf.shadowFrustums[frustum].viewProj * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    uint base = frustum * uint(ANO_CDF_LAYERS / 2); // ANO_SHADOW_ATLAS_SUBLAYERS: 2 texels per frustum
    vec4 subA = texture(shadowAtlas, vec3(uv, float(base)));
    vec4 subB = texture(shadowAtlas, vec3(uv, float(base + 1u)));
    float depthBias = ANO_CDF_DEPTH_BIAS * (1.0 + (1.0 - nDotL));
    return anoLayeredShadow(subA, subB, proj.z, depthBias, ANO_CDF_CONTACT_SOFT);
}

#endif
