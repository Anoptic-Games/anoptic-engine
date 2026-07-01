#version 450
#extension GL_GOOGLE_include_directive : require

// Power CDF shadow pass fragment stage. The atlas is a filterable color target; this writes the
// per-texel occluder-depth stat vector (min = max = mean = z initially; the separable prefilter
// spreads them). gl_FragCoord.z is the light-space depth in [0,1] (Vulkan ZO, viewport depth 0..1),
// produced identically by flat.mesh and flat.vert under the shadowPass spec constant, so no extra
// varying is threaded across the two geometry paths. The depth attachment (test on) keeps the nearest
// occluder per texel; the prefilter over neighbours builds the (min,max,mean) distribution the CDF fits.
//
// It still declares the same fragment-input interface flat.vert / flat.mesh output (locations 0-4),
// even though it ignores every value: an unconsumed geometry-stage output is only a spec "performance
// warning", but at least one driver dropped the vertex stage's rasterizer output entirely on a
// stage-interface mismatch, leaving the map empty on the ANO_FORCE_NO_MESH_SHADER=1 fallback.
// Declaring the matching inputs makes both stages link with an identical interface.
#include "shadow_cdf.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint inEntityIndex;

layout(location = 0) out vec4 outStats;

void main() {
    outStats = anoEncodeDepthStats(gl_FragCoord.z);
}
