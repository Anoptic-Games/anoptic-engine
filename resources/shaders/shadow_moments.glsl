// Moment shadow maps (Peters & Klein, I3D 2015) — the pure math, no resource access. Shared by the
// moment write (shadow_depth.frag) and the reconstruct (shadow_sample.glsl, included by the lighting
// frags). Storage is 4 optimized power moments in RGBA16_UNORM; reconstruct is Hamburger 4MSM.
//
// All constants are transcribed verbatim from the paper's supplementary Listings 1/2 (the optimized
// 16-bit quantize/de-quantize) and Algorithm 3 (the shadow-intensity solve). The HLSL listings use
// row-vector mul(v, M); the GLSL mat4(...) constructor takes COLUMNS, so each mat4 below is built
// from the HLSL matrix ROWS as its columns — i.e. (M_glsl * v) reproduces mul(v, M_hlsl) exactly.

#ifndef ANO_SHADOW_MOMENTS_GLSL
#define ANO_SHADOW_MOMENTS_GLSL

const float ANO_MSM_QUANT_BIAS = 0.035955884801; // added to / removed from the first optimized moment

// Listing 1: encode a light-space depth in [0,1] to the optimized 4-moment vector stored in the map.
// Output components all lie in [0,1] (UNORM-storable). A normalized linear filter of these values
// stays a valid optimized-moment vector (the transform is affine by design), so the prefilter blur
// runs directly on the stored values.
vec4 anoEncodeMoments(float z) {
    float z2 = z * z;
    vec4 m = vec4(z, z2, z2 * z, z2 * z2);
    const mat4 kOptimize = mat4(
        -2.07224649,    13.7948857237,  0.105877704,    9.7924062118,   // = HLSL row 0
         32.23703778,  -59.4683975703, -1.9077466311,  -33.7652110555,  // = HLSL row 1
        -68.571074599,  82.0359750338,  9.3496555107,   47.9456096605,  // = HLSL row 2
         39.3703274134,-35.364903257,  -6.6543490743,  -23.9728048165); // = HLSL row 3
    vec4 o = kOptimize * m;
    o.x += ANO_MSM_QUANT_BIAS;
    return o;
}

// Listing 2: de-quantize a sampled optimized-moment vector back to raw power moments (b1..b4).
vec4 anoDecodeMoments(vec4 o) {
    o.x -= ANO_MSM_QUANT_BIAS;
    const mat4 kDeOptimize = mat4(
        0.2227744146,  0.1549679261,  0.1451988946,  0.163127443,    // = HLSL row 0
        0.0771972861,  0.1394629426,  0.2120202157,  0.2591432266,    // = HLSL row 1
        0.7926986636,  0.7963415838,  0.7258694464,  0.6539092497,    // = HLSL row 2
        0.0319417555, -0.1722823173, -0.2758014811, -0.3376131734);   // = HLSL row 3
    return kDeOptimize * o;
}

// Algorithm 3 (Hamburger 4MSM): the sharpest CDF lower bound from 4 moments. b = raw moments,
// fragDepth = light-space depth in [0,1]. Returns shadow intensity G in [0,1] (0 lit, 1 occluded).
// alpha biases the moments toward (0.5,0.5,0.5,0.5) for numerical robustness; the paper uses 3e-5
// for 16-bit quantization (vs 2e-6 for float). depthBias is a small constant occluder offset to
// suppress residual self-shadowing.
float anoMSMShadow(vec4 b, float fragDepth, float alpha, float depthBias) {
    b = mix(b, vec4(0.5), alpha);
    float zf = fragDepth - depthBias;

    // LDL^T factorization of the Hankel matrix B = [[1,b1,b2],[b1,b2,b3],[b2,b3,b4]], storing only
    // the non-trivial entries; solve B c = (1, zf, zf^2)^T.
    float L32D22 = -b[0] * b[1] + b[2];
    float D22    = -b[0] * b[0] + b[1];
    float sqDepthVar = -b[1] * b[1] + b[3];
    float D33D22 = dot(vec2(sqDepthVar, -L32D22), vec2(D22, L32D22));
    float invD22 = 1.0 / D22;
    float L32 = L32D22 * invD22;

    vec3 c = vec3(1.0, zf, zf * zf);
    c[1] -= b.x;
    c[2] -= b.y + L32 * c[1];
    c[1] *= invD22;
    c[2] *= D22 / D33D22;
    c[1] -= L32 * c[2];
    c[0] -= dot(c.yz, b.xy);

    // Roots of c0 + c1*z + c2*z^2; z2 <= z3.
    float invC2 = 1.0 / c[2];
    float p = c[1] * invC2;
    float q = c[0] * invC2;
    float r = sqrt(max(p * p * 0.25 - q, 0.0));
    float z2 = -p * 0.5 - r;
    float z3 = -p * 0.5 + r;

    // Three-case shadow intensity (Algorithm 3 steps 4-6) packed into a select, per the paper's
    // reference solve: zf<=z2 -> lit; z2<zf<=z3 -> partial; zf>z3 -> mostly occluded.
    vec4 sw = (z3 < zf) ? vec4(z2, zf, 1.0, 1.0)
            : (z2 < zf) ? vec4(zf, z2, 0.0, 1.0)
                        : vec4(0.0, 0.0, 0.0, 0.0);
    float quotient = (sw[0] * z3 - b[0] * (sw[0] + z3) + b[1]) / ((z3 - sw[1]) * (zf - z2));
    return clamp(sw[2] + sw[3] * quotient, 0.0, 1.0);
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
