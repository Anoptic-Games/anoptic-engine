#version 450

// Depth-only shadow pass: no color attachment, so this writes nothing — depth comes from the
// rasterizer via the geometry stage's gl_Position. It DOES declare the same fragment-input
// interface that flat.vert / flat.mesh output (locations 0-4), even though it ignores every value.
//
// Why: those geometry shaders are shared with the camera path and output the fragment varyings
// unconditionally (a specialization constant can't remove an output declaration). An unconsumed
// geometry-stage output is only a "performance warning" per spec, but drivers are free to handle
// the stage-interface mismatch differently between the vertex and mesh paths — and at least one
// dropped the vertex stage's rasterizer output entirely, leaving the shadow map empty (no shadows
// on the ANO_FORCE_NO_MESH_SHADER=1 fallback). Declaring the matching inputs makes both stages
// link with an identical interface.
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint inEntityIndex;

void main() {}
