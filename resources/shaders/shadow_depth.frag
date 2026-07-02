#version 450
#extension GL_GOOGLE_include_directive : require

// Layered Power CDF shadow pass fragment stage. Writes the nearest occluder's one-hot (coverage=1, M=z)
// into the depth band containing z, across two MRT color targets (the two atlas sublayers). gl_FragCoord.z
// is the light-space depth in [0,1] (Vulkan ZO, viewport depth 0..1), produced identically by flat.mesh
// and flat.vert under the shadowPass spec constant, so no extra varying is threaded across the two
// geometry paths. The depth attachment (test on) keeps the nearest occluder per texel; the box prefilter
// then averages these one-hots into per-band (coverage, coverage*meanDepth).
//
// The geometry stage is now the ANO_DEPTH_ONLY compile of flat.mesh / flat.vert, which emits NO
// user outputs — so this stage declares no inputs and the interfaces match exactly on both
// geometry paths. (History: when the fat modules fed this pass, the inputs had to be declared to
// mirror locations 0-4, because one driver dropped the vertex stage's rasterizer output entirely
// on a stage-interface mismatch. Matched-empty linkage avoids both the mismatch and the dead ISBE
// payload.)
#include "shadow_cdf.glsl"

layout(location = 0) out vec4 outSubA; // bands 0/1: (cov0,M0,cov1,M1)
layout(location = 1) out vec4 outSubB; // bands 2/3: (cov2,M2,cov3,M3)

void main() {
    anoEncodeLayered(gl_FragCoord.z, outSubA, outSubB);
}
