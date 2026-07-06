// Layered Power CDF shadow sampling wrapper, shared by flat.frag and transmission.frag. Include AFTER
// the set-2 declaration of `sampler2DArray shadowAtlas`, which both lighting frags declare identically
// — this file references it by name. Returns the same "1 lit .. 0 shadowed" factor the old PCF/MSM
// paths did, so the call sites are unchanged (they still pass a FRUSTUM index; this maps it to the
// frustum's two atlas sublayers internally).

#ifndef ANO_SHADOW_SAMPLE_GLSL
#define ANO_SHADOW_SAMPLE_GLSL

#include "shadow_cdf.glsl"

// Per-frustum sampling viewProjs, packed by shadowsetup.comp beside the fat CullView records the
// cull/geometry stages read. A UBO so the matrix rows become constant-bank operands instead of a
// 16-register per-lane block (the light loop's index is dynamically uniform after scalarization;
// point-light cube faces diverge at most 6 ways). The array bound rides the C-side static assert
// ANO_SHADOW_FRUSTUM_COUNT <= 64 (structs.h); the buffer is allocated at the full 64.
// depthParams (shadowsetup.comp) linearizes light-space ZO depth before the CDF band walk:
// d01 = (x + z*zo) / (1 - y*zo). Ortho = (0,0,1) identity; perspective = (r, 1-r, 0), r = near/far.
// Bands + bias + contactSoft then operate on a linear fraction of the light range for every
// projection type (raw perspective ZO put the whole scene in the last band and stretched the
// contact ramp to meters — the through-the-floor point-light leak).
layout(set = 2, binding = 3) uniform ShadowSampleVPUBO {
    mat4 viewProj[64];
    vec4 depthParams[64];
} shadowVPBuf;

// Hardware-tunable knobs (the sandbox cannot run the renderer; tune visually on the GPU). Both are
// in LINEARIZED depth units — fractions of the light's near..far range (identical to raw ZO for the
// ortho/directional slab, the space they were originally tuned in):
//   DEPTH_BIAS   — constant occluder offset killing residual self-shadow acne; slope-scaled by nDotL.
//   CONTACT_SOFT — within-band soft-step half-width. 0 = hard/exact step at the band mean (crisp
//                  contact, no acne); larger softens the depth-direction contact transition for
//                  low-res, at some contact accuracy (validated stable to ~0.03, frays past ~0.06).
//                  Silhouette-edge softness is independent of this (it comes from the coverage gradient).
const float ANO_CDF_DEPTH_BIAS   = 0.0002;
const float ANO_CDF_CONTACT_SOFT = 0.02;

// Reconstruct the lit factor for `worldPos` against shadow FRUSTUM `frustum` (its two atlas sublayers).
// Outside the map or beyond the far plane returns 1.0 (lit) — no false self-shadowing at seams.
float sampleShadowCDF(uint frustum, vec3 worldPos, float nDotL) {
    vec4 lc = shadowVPBuf.viewProj[frustum] * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    uint base = frustum * uint(ANO_CDF_LAYERS / 2); // ANO_SHADOW_ATLAS_SUBLAYERS: 2 texels per frustum
    vec4 subA = texture(shadowAtlas, vec3(uv, float(base)));
    vec4 subB = texture(shadowAtlas, vec3(uv, float(base + 1u)));
    // Linearize the receiver depth with the frustum's params (must match shadow_depth.frag's encode).
    vec4 dp = shadowVPBuf.depthParams[frustum];
    float zr = (dp.x + dp.z * proj.z) / (1.0 - dp.y * proj.z);
    float depthBias = ANO_CDF_DEPTH_BIAS * (1.0 + (1.0 - nDotL));
    return anoLayeredShadow(subA, subB, zr, depthBias, ANO_CDF_CONTACT_SOFT);
}

#endif
