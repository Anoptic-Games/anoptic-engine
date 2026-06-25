#version 450
#extension GL_GOOGLE_include_directive : require

// Moment shadow pass fragment stage. The atlas is now a filterable color target, so this writes the
// optimized 4-moment encoding of the fragment's light-space depth. gl_FragCoord.z is that depth in
// [0,1] (Vulkan ZO, viewport depth 0..1) and is produced identically by flat.mesh and flat.vert
// under the shadowPass spec constant, so no extra varying is threaded across the two geometry paths.
// A depth attachment (depth test on) keeps the nearest occluder per texel; the moments here describe
// that nearest depth, and the separable blur over neighbours builds the distribution MSM reconstructs.
//
// It still declares the same fragment-input interface flat.vert / flat.mesh output (locations 0-4),
// even though it ignores every value: an unconsumed geometry-stage output is only a spec "performance
// warning", but at least one driver dropped the vertex stage's rasterizer output entirely on a
// stage-interface mismatch, leaving the map empty on the ANO_FORCE_NO_MESH_SHADER=1 fallback.
// Declaring the matching inputs makes both stages link with an identical interface.
#include "shadow_moments.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint inEntityIndex;

layout(location = 0) out vec4 outMoments;

void main() {
    outMoments = anoEncodeMoments(gl_FragCoord.z);
}
