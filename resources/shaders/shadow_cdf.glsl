// Power CDF shadow maps. The atlas stores, per texel, the min / max / mean occluder depth over the
// prefilter footprint (see shadowblur.frag); reconstruction fits a power-law CDF F(t) = t^beta to that
// distribution and evaluates visibility. Unlike moment maps this is inherently free of the light leak
// that multiple distant occluders produce across a silhouette (the near/far bimodal footprint is
// exactly what the fit is built for), and 16-bit storage is sufficient.
//
// Shared by the atlas write (shadow_depth.frag) and the reconstruct (shadow_sample.glsl, included by
// the lighting frags). No resource access here — pure math.

#ifndef ANO_SHADOW_CDF_GLSL
#define ANO_SHADOW_CDF_GLSL

// Encode a light-space depth z in [0,1] to the per-texel stat vector written into the atlas. The
// render is depth-tested, so z is the nearest occluder; (min,max,mean) all start equal at z and the
// separable prefilter spreads them over the footprint. .a is unused (kept for RGBA16 storage).
vec4 anoEncodeDepthStats(float z) {
    return vec4(z, z, z, 1.0);
}

// Power CDF visibility. stats = (zmin, zmax, zmean) sampled from the (prefiltered) atlas; zr = receiver
// light-space depth in [0,1]; depthBias = small occluder offset vs residual self-shadow. Returns the
// lit factor in [0,1] (1 lit, 0 fully occluded) — the same contract the PCF / MSM paths used.
//
// v(t) = (1 - t^beta) / (1 - tbar^beta) for t > tbar, else 1, with t = (zr-zmin)/dz,
// beta = (zmean-zmin)/(zmax-zmean), tbar = beta/(beta+1) = (zmean-zmin)/dz. The t <= tbar branch is the
// planar self-shadow bias (V = 1 when the receiver sits at/above the mean occluder depth). A near-zero
// depth spread (single occluder / flat region / fully-lit far clear) degenerates to a hard step.
float anoPowerCDFShadow(vec3 stats, float zr, float depthBias) {
    const float EPS = 1e-4;
    float zmin = stats.x, zmax = stats.y, zmean = stats.z;
    zr -= depthBias;

    float dz = zmax - zmin;
    if (dz < EPS) return zr > zmean ? 0.0 : 1.0;         // no depth spread -> hard shadow test

    float t    = clamp((zr - zmin) / dz, 0.0, 1.0);
    float tbar = clamp((zmean - zmin) / dz, 0.0, 1.0);   // == beta/(beta+1)
    if (t <= tbar) return 1.0;                            // receiver at/above mean -> lit (self-shadow bias)

    float beta = (zmean - zmin) / max(zmax - zmean, EPS);
    if (beta < EPS) return 0.0;                           // occluders collapsed to zmin -> shadowed
    beta = min(beta, 1e4);                                // pow() stability

    float denom = 1.0 - pow(tbar, beta);
    if (denom < EPS) return 1.0;                          // mean at the far extreme -> negligible occlusion
    float v = (1.0 - pow(t, beta)) / denom;
    return clamp(v, 0.0, 1.0);
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
