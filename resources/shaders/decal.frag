#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "gpu_abi.glsl"

// SKELETON: not in CMakeLists shader manifest, not loaded by any pipeline.
// PIPELINE_DECAL frag: reconstruct surface (depth or UV), sample decal layer, blend fade.
// Pair: decal.vert.

// NOTE: set 0 binding 12 is now LightRuntimeSSBO in the live globalSetLayout (flat.frag/transmission.frag).
// This decal skeleton is inert (no compiled pipeline); renumber to a free binding (>= 13) when
// PIPELINE_DECAL is actually wired against globalSetLayout.
layout(set = 0, binding = 12) readonly buffer DecalSSBO { DecalRecord decals[]; } decalBuf;
layout(set = 1, binding = 0) uniform sampler2DArray decalAtlas; // decal texture layers

layout(location = 0) flat in uint inDecalIndex;
layout(location = 0) out vec4 outColor;

void main() {
    DecalRecord d = decalBuf.decals[inDecalIndex];
    // SKELETON: project to find the decal UV (depth-reconstruct or interpolated),
    // sample decalAtlas at layer d.textureLayer, premultiply by d.fade, blend.
    outColor = vec4(0.0);
}
