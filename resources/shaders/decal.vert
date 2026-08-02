#version 450
#extension GL_GOOGLE_include_directive : require

#include "gpu_abi.glsl"

// SKELETON. Not in CMakeLists shader manifest. Not loaded by any pipeline.
// PIPELINE_DECAL vertex: one instanced unit cube per DecalRecord (structs.h),
// host * localTransform. DecalPool: global LRU budget.
// Activate: manifest + PIPELINE_DECAL prototype + g_framePasses DecalPool draw.

layout(set = 0, binding = 0) uniform GlobalUBO { GlobalData global; };

layout(set = 0, binding = 1) readonly buffer TransformSSBO { mat4 transforms[]; } transformBuf;
// NOTE: set 0 binding 12 is LightRuntimeSSBO in live globalSetLayout (flat.frag/transmission.frag).
// Renumber to a free binding (>= 13) when wiring PIPELINE_DECAL.
layout(set = 0, binding = 12) readonly buffer DecalSSBO    { DecalRecord decals[]; } decalBuf;

layout(location = 0) in vec3 inUnitCubePos; // [-0.5,0.5]^3 projector volume

layout(location = 0) flat out uint outDecalIndex;

void main() {
    DecalRecord d = decalBuf.decals[gl_InstanceIndex];
    mat4 host = transformBuf.transforms[d.anchorSlot]; // ride the live host pose
    gl_Position = global.proj * global.view * host * d.localTransform * vec4(inUnitCubePos, 1.0);
    outDecalIndex = gl_InstanceIndex;
}
