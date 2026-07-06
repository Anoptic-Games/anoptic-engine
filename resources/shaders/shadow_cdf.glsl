// Layered Power CDF shadow maps (LVSM-style). Partition light-space depth into ANO_SHADOW_CDF_LAYERS
// bands, store per band two linearly-filterable quantities: coverage (fraction of the footprint whose
// nearest occluder is in the band) and M = coverage*meanDepth. Occlusion = cumulative coverage of all
// bands nearer than the receiver, plus a soft within-band term for its own band. Soft penumbra comes
// from the filtered coverage gradient.
//
// Storage packs two (coverage, M) pairs per RGBA16 texel (band0/1 in sublayer A, band2/3 in sublayer B).
// Uniform splits: band k spans [k/N, (k+1)/N]. Keep N + packing in sync with structs.h.
//
// Shared by the atlas write (shadow_depth.frag, MRT) and the reconstruct (shadow_sample.glsl). Pure math.

#ifndef ANO_SHADOW_CDF_GLSL
#define ANO_SHADOW_CDF_GLSL

// ANO_CDF_FP16 (a -D of the *_fp16.frag.spv variants, selected at pipeline creation when the
// device has shaderFloat16): the reconstruct walks the band data in fp16, halving the standing
// texture-return registers. Encode and the fp32 reconstruct are untouched.
#if ANO_CDF_FP16
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#endif

const int   ANO_CDF_LAYERS = 4;
const float ANO_CDF_LAYER_W = 0.25; // 1.0 / ANO_CDF_LAYERS

// Encode a nearest-occluder depth z in [0,1] into the two MRT sublayer texels: a one-hot (coverage=1,
// M=z) in the band containing z, zero elsewhere. The box prefilter averages these into per-band
// (coverage, coverage*meanDepth). Cleared texels stay (0,0,0,0) = lit.
void anoEncodeLayered(float z, out vec4 subA, out vec4 subB) {
    subA = vec4(0.0);
    subB = vec4(0.0);
    int k = clamp(int(z * float(ANO_CDF_LAYERS)), 0, ANO_CDF_LAYERS - 1);
    if      (k == 0) { subA.x = 1.0; subA.y = z; }
    else if (k == 1) { subA.z = 1.0; subA.w = z; }
    else if (k == 2) { subB.x = 1.0; subB.y = z; }
    else             { subB.z = 1.0; subB.w = z; }
}

// Layered visibility. subA/subB = the two prefiltered sublayer texels (band pairs), zr = receiver
// light-space depth in [0,1], depthBias = occluder offset vs self-shadow acne, contactSoft = within-band
// soft-step half-width in ZO depth units (0 = hard step at the band mean). Returns the lit factor in [0,1].
#if ANO_CDF_FP16
// fp16 band walk: the filtered (coverage, M) pairs are UNORM16-sourced [0,1] values, well inside
// fp16 range. The receiver depth zr, the bias, and the contact smoothstep STAY fp32 — fp16 ulp
// near 1.0 (~4.9e-4) would swallow the 2e-4 depth bias; mean is promoted before the smoothstep
// for the same reason. Residual: mean itself is fp16-quantized (<= 1 ulp), which can dip up to
// ~1.5% of coverage into the contact ramp on far-band receivers — below visibility.
float anoLayeredShadow(vec4 subA32, vec4 subB32, float zr, float depthBias, float contactSoft) {
    const float EPS = 1e-4;
    zr -= depthBias;
    // Unpack the 4 bands: (coverage_k, M_k).
    f16vec4 subA = f16vec4(subA32);
    f16vec4 subB = f16vec4(subB32);
    float16_t cov[4] = float16_t[4](subA.x, subA.z, subB.x, subB.z);
    float16_t M[4]   = float16_t[4](subA.y, subA.w, subB.y, subB.w);

    float16_t occ = float16_t(0.0);
    for (int k = 0; k < ANO_CDF_LAYERS; ++k) {
        float c = float(cov[k]);
        if (c < EPS) continue;                 // no occluders of this footprint in band k
        float lo = float(k) * ANO_CDF_LAYER_W;
        float hi = lo + ANO_CDF_LAYER_W;
        if (zr >= hi) { occ += cov[k]; continue; } // whole band is nearer than the receiver -> fully occludes
        if (zr <= lo) continue;                // whole band is behind the receiver -> does not occlude
        // Receiver's own band: soft step rising from the band's mean occluder depth. Coverage weight keeps thin occluders faint.
        float mean = float(M[k]) / c;
        occ += cov[k] * float16_t(smoothstep(mean, mean + contactSoft + EPS, zr));
    }
    return clamp(1.0 - float(occ), 0.0, 1.0);
}
#else
float anoLayeredShadow(vec4 subA, vec4 subB, float zr, float depthBias, float contactSoft) {
    const float EPS = 1e-4;
    zr -= depthBias;
    // Unpack the 4 bands: (coverage_k, M_k).
    float cov[4] = float[4](subA.x, subA.z, subB.x, subB.z);
    float M[4]   = float[4](subA.y, subA.w, subB.y, subB.w);

    float occ = 0.0;
    for (int k = 0; k < ANO_CDF_LAYERS; ++k) {
        float c = cov[k];
        if (c < EPS) continue;                 // no occluders of this footprint in band k
        float lo = float(k) * ANO_CDF_LAYER_W;
        float hi = lo + ANO_CDF_LAYER_W;
        if (zr >= hi) { occ += c; continue; }  // whole band is nearer than the receiver -> fully occludes
        if (zr <= lo) continue;                // whole band is behind the receiver -> does not occlude
        // Receiver's own band: soft step rising from the band's mean occluder depth. Coverage weight keeps thin occluders faint.
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
