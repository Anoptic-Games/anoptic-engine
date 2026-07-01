// Layered Power CDF shadow maps (LVSM-style; the escalation power_cdf.md §4 prescribes). The single
// (min,max,mean) fit leaks where one filter footprint straddles a near occluder and a far background:
// (min,max,mean) cannot represent that bimodal near/far distribution, so the fit over-brightens. The
// fix is to partition light-space depth into ANO_SHADOW_CDF_LAYERS bands and store, per band, two
// LINEARLY-FILTERABLE quantities: coverage (fraction of the footprint whose nearest occluder is in the
// band) and M = coverage*meanDepth. A receiver's occlusion is then the cumulative coverage of all bands
// nearer than it, plus a soft within-band term for its own band — so a far background simply lands in a
// different band and can never contaminate the near one. Soft penumbra comes from the spatially filtered
// coverage gradient, not within-band depth interpolation (validated off-GPU: bleed/thin/solid all exact).
//
// Storage packs two (coverage, M) pairs per RGBA16 texel (band0/1 in sublayer A, band2/3 in sublayer B),
// so the atlas has ANO_SHADOW_ATLAS_SUBLAYERS array layers per frustum. Splits are uniform: band k spans
// [k/N, (k+1)/N]. Keep N + the packing in sync with structs.h (ANO_SHADOW_CDF_LAYERS / ATLAS_SUBLAYERS).
//
// Shared by the atlas write (shadow_depth.frag, MRT) and the reconstruct (shadow_sample.glsl). Pure math.

#ifndef ANO_SHADOW_CDF_GLSL
#define ANO_SHADOW_CDF_GLSL

const int   ANO_CDF_LAYERS = 4;
const float ANO_CDF_LAYER_W = 0.25; // 1.0 / ANO_CDF_LAYERS

// Encode a nearest-occluder depth z in [0,1] into the two MRT sublayer texels: a one-hot (coverage=1,
// M=z) in the band containing z, zero elsewhere. The depth test keeps z = nearest occluder; the box
// prefilter then averages these into per-band (coverage, coverage*meanDepth). Cleared (no-occluder)
// texels stay (0,0,0,0) = coverage 0 = lit.
void anoEncodeLayered(float z, out vec4 subA, out vec4 subB) {
    subA = vec4(0.0);
    subB = vec4(0.0);
    int k = clamp(int(z * float(ANO_CDF_LAYERS)), 0, ANO_CDF_LAYERS - 1);
    if      (k == 0) { subA.x = 1.0; subA.y = z; }
    else if (k == 1) { subA.z = 1.0; subA.w = z; }
    else if (k == 2) { subB.x = 1.0; subB.y = z; }
    else             { subB.z = 1.0; subB.w = z; }
}

// Layered visibility. subA/subB = the two prefiltered sublayer texels (band pairs); zr = receiver
// light-space depth in [0,1]; depthBias = small occluder offset vs self-shadow acne; contactSoft = the
// within-band soft-step half-width in ZO depth units (0 = hard/exact step at the band mean; larger =
// softer contact for low-res, at some contact accuracy). Returns the lit factor in [0,1] (1 lit, 0
// occluded) — the same contract the PCF/MSM/single-CDF paths used.
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
        // Receiver's own band: soft step rising from the band's mean occluder depth. Asymmetric (0 AT the
        // mean) so a solid occluder gives no self-shadow acne; the coverage weight keeps thin occluders faint.
        float mean = M[k] / c;
        occ += c * smoothstep(mean, mean + contactSoft + EPS, zr);
    }
    return clamp(1.0 - occ, 0.0, 1.0);
}

// Point-light cube face for direction d = fragWorldPos - lightPos. 0..5 = +X,-X,+Y,-Y,+Z,-Z; MUST
// match shadowsetup.comp's cubeFaceBasis so layer baseFrustum+face reprojects the fragment in-range.
uint anoCubeFaceIndex(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x >= 0.0 ? 0u : 1u;
    if (a.y >= a.z)               return d.y >= 0.0 ? 2u : 3u;
    return d.z >= 0.0 ? 4u : 5u;
}

#endif
