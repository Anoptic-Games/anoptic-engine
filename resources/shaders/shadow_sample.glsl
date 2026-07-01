// Power CDF shadow sampling wrapper, shared by flat.frag and transmission.frag. Include AFTER the
// set-2 declarations (sampler2DArray shadowAtlas + the ShadowFrustumSSBO `shadowFrustumBuf`), which
// both lighting frags declare identically — this file references them by name. Returns the same
// "1 lit .. 0 shadowed" factor the old PCF/MSM paths did, so the call sites are unchanged.

#ifndef ANO_SHADOW_SAMPLE_GLSL
#define ANO_SHADOW_SAMPLE_GLSL

#include "shadow_cdf.glsl"

// Hardware-tunable knobs (the sandbox cannot run the renderer; tune visually on the GPU):
//   DEPTH_BIAS  — constant occluder offset killing residual self-shadow acne; slope-scaled by nDotL.
//   LIGHT_BLEED — optional linstep clamp on the lit factor for any residual leak. Power CDF is
//                 leak-resistant by construction (the min/max/mean CDF fit handles the multi-occluder
//                 silhouette case), so this stays near 0 to preserve penumbra; raise only if a residual
//                 leak shows on hardware. 0 = off (the branch compiles out).
const float ANO_CDF_DEPTH_BIAS  = 0.0002;
const float ANO_CDF_LIGHT_BLEED = 0.0;

// Reconstruct the lit factor for `worldPos` against shadow-frustum/atlas-layer `layer`. Outside the
// map or beyond the far plane returns 1.0 (lit) — no false self-shadowing at seams (matches PCF/MSM).
float sampleShadowCDF(uint layer, vec3 worldPos, float nDotL) {
    vec4 lc = shadowFrustumBuf.shadowFrustums[layer].viewProj * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    vec3 stats = texture(shadowAtlas, vec3(uv, float(layer))).xyz; // (zmin, zmax, zmean), prefiltered
    float depthBias = ANO_CDF_DEPTH_BIAS * (1.0 + (1.0 - nDotL));
    float visibility = anoPowerCDFShadow(stats, proj.z, depthBias);

    // Optional residual light-bleed clamp (off by default; Power CDF is leak-free by construction).
    if (ANO_CDF_LIGHT_BLEED > 0.0)
        visibility = clamp((visibility - ANO_CDF_LIGHT_BLEED) / (1.0 - ANO_CDF_LIGHT_BLEED), 0.0, 1.0);
    return visibility;
}

#endif
