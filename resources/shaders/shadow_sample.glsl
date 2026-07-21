// Layered Power CDF shadow sampling wrapper, shared by flat.frag and transmission.frag.
// Include AFTER the set-2 declaration of `sampler2DArray shadowAtlas`.
// Returns a "1 lit .. 0 shadowed" factor for a FRUSTUM index.

#ifndef ANO_SHADOW_SAMPLE_GLSL
#define ANO_SHADOW_SAMPLE_GLSL

#include "shadow_cdf.glsl"

// Per-frustum sampling viewProjs, packed by shadowsetup.comp. Bound rides ANO_SHADOW_FRUSTUM_COUNT <= 64 (structs.h), allocated at 64.
// depthParams linearizes light-space ZO depth for the CDF band walk.
// d01 = (x + z*zo) / (1 - y*zo). Ortho = (0,0,1), perspective = (r, 1-r, 0), r = near/far.
layout(set = 2, binding = 3) uniform ShadowSampleVPUBO {
    mat4 viewProj[64];
    vec4 depthParams[64];
} shadowVPBuf;

// Hardware-tunable knobs in LINEARIZED depth units, fractions of the light's near..far range.
//   DEPTH_BIAS   〜 constant occluder offset killing residual self-shadow acne, slope-scaled by nDotL.
//   CONTACT_SOFT 〜 soft-step width above each band's mean occluder depth. 0 = hard step at the mean.
const float ANO_CDF_DEPTH_BIAS   = 0.0002;
const float ANO_CDF_CONTACT_SOFT = 0.2;

// Lit factor for `worldPos` against shadow FRUSTUM `frustum`. Outside the map or beyond far returns 1.0 (lit).
float sampleShadowCDF(uint frustum, vec3 worldPos, float nDotL) {
    vec4 lc = shadowVPBuf.viewProj[frustum] * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    uint base = frustum * uint(ANO_CDF_LAYERS / 2); // 2 texels per frustum
    vec4 subA = texture(shadowAtlas, vec3(uv, float(base)));
    vec4 subB = texture(shadowAtlas, vec3(uv, float(base + 1u)));
    // Linearize receiver depth with the frustum's params.
    vec4 dp = shadowVPBuf.depthParams[frustum];
    float zr = (dp.x + dp.z * proj.z) / (1.0 - dp.y * proj.z);
    float depthBias = ANO_CDF_DEPTH_BIAS * (1.0 + (1.0 - nDotL));
    return anoLayeredShadow(subA, subB, zr, depthBias, ANO_CDF_CONTACT_SOFT);
}

#endif
