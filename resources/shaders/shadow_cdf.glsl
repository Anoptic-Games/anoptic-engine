// Layered Power CDF (LVSM). Band k: (coverage, M=coverage*meanDepth). Occ = sum cov * softstep(mean).
// Band edges bucket ENCODE only. Pack: two (cov,M) pairs per RGBA16 (A=band0/1, B=band2/3).
// Uniform splits [k/N,(k+1)/N]. Keep N+packing in sync with structs.h.
// Shared by shadow_depth.frag (MRT write) and shadow_sample.glsl (reconstruct).

#ifndef ANO_SHADOW_CDF_GLSL
#define ANO_SHADOW_CDF_GLSL

// ANO_CDF_FP16: the reconstruct walks the band data in fp16. Override with: -D of *_fp16.frag.spv variants.
#if ANO_CDF_FP16
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#endif

const int   ANO_CDF_LAYERS = 4;
const float ANO_CDF_LAYER_W = 0.25; // 1.0 / ANO_CDF_LAYERS

// Encode z in [0,1] as one-hot (coverage=1, M=z) in its band across subA/subB. Cleared = (0,0,0,0) lit.
void anoEncodeLayered(float z, out vec4 subA, out vec4 subB) {
    subA = vec4(0.0);
    subB = vec4(0.0);
    int k = clamp(int(z * float(ANO_CDF_LAYERS)), 0, ANO_CDF_LAYERS - 1);
    if      (k == 0) { subA.x = 1.0; subA.y = z; }
    else if (k == 1) { subA.z = 1.0; subA.w = z; }
    else if (k == 2) { subB.x = 1.0; subB.y = z; }
    else             { subB.z = 1.0; subB.w = z; }
}

// Lit factor [0,1] from prefiltered subA/subB. zr = receiver depth [0,1].
// occ = sum_k cov_k * smoothstep(mean_k, mean_k + contactSoft, zr - depthBias).
#if ANO_CDF_FP16
// fp16 band walk; zr/bias/smoothstep stay fp32.
float anoLayeredShadow(vec4 subA32, vec4 subB32, float zr, float depthBias, float contactSoft) {
    const float EPS = 1e-4;
    zr -= depthBias;
    // Bands: (coverage_k, M_k).
    f16vec4 subA = f16vec4(subA32);
    f16vec4 subB = f16vec4(subB32);
    float16_t cov[4] = float16_t[4](subA.x, subA.z, subB.x, subB.z);
    float16_t M[4]   = float16_t[4](subA.y, subA.w, subB.y, subB.w);

    float16_t occ = float16_t(0.0);
    for (int k = 0; k < ANO_CDF_LAYERS; ++k) {
        float c = float(cov[k]);
        if (c < EPS) continue;                 // band k empty
        float lo = float(k) * ANO_CDF_LAYER_W;
        if (zr <= lo) continue;                                    // ramp not started
        if (zr >= lo + ANO_CDF_LAYER_W + contactSoft + EPS) {      // ramp complete
            occ += cov[k];
            continue;
        }
        // Soft step from band mean.
        float mean = float(M[k]) / c;
        occ += cov[k] * float16_t(smoothstep(mean, mean + contactSoft + EPS, zr));
    }
    return clamp(1.0 - float(occ), 0.0, 1.0);
}
#else
float anoLayeredShadow(vec4 subA, vec4 subB, float zr, float depthBias, float contactSoft) {
    const float EPS = 1e-4;
    zr -= depthBias;
    // Bands: (coverage_k, M_k).
    float cov[4] = float[4](subA.x, subA.z, subB.x, subB.z);
    float M[4]   = float[4](subA.y, subA.w, subB.y, subB.w);

    float occ = 0.0;
    for (int k = 0; k < ANO_CDF_LAYERS; ++k) {
        float c = cov[k];
        if (c < EPS) continue;                 // band k empty
        float lo = float(k) * ANO_CDF_LAYER_W;
        if (zr <= lo) continue;                                    // ramp not started
        if (zr >= lo + ANO_CDF_LAYER_W + contactSoft + EPS) {      // ramp complete
            occ += c;
            continue;
        }
        // Soft step from band mean.
        float mean = M[k] / c;
        occ += c * smoothstep(mean, mean + contactSoft + EPS, zr);
    }
    return clamp(1.0 - occ, 0.0, 1.0);
}
#endif

// Point-light cube face for direction d = fragWorldPos - lightPos. 0..5 = +X,-X,+Y,-Y,+Z,-Z.
// MUST match shadowsetup.comp's cubeFaceBasis.
uint anoCubeFaceIndex(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x >= 0.0 ? 0u : 1u;
    if (a.y >= a.z)               return d.y >= 0.0 ? 2u : 3u;
    return d.z >= 0.0 ? 4u : 5u;
}

#endif
