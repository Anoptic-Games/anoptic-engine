#version 450

// SKELETON — not in the CMakeLists shader manifest, not loaded by any pipeline.
// Documents the DECAL DRAW STAGE vertex half (PIPELINE_DECAL). Decals are their own
// primitive, not a per-entity attribute: one instanced unit cube per DecalRecord
// (structs.h), anchored to a host by render slot. The cube is placed by the host's
// live transform composed with the decal's localTransform, so a decal rides a moving
// host for free. The pool is a global budget with LRU recycling — unbounded over
// time, fixed in memory.
//
// To activate: add to the manifest + a PIPELINE_DECAL prototype + a g_framePasses
// entry drawing the DecalPool as instanced unit cubes (a deferred-style projection
// pass, or a forward UV-overlay variant keyed by DecalRecord.flags).

struct DecalRecord {
    mat4 localTransform;
    uint anchorSlot;
    uint textureLayer;
    float fade;
    uint flags;
};

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4  view;
    mat4  proj;
    float time;
    float deltaTime;
    uint  frameCount;
    uint  lightCount;
    vec4  cameraPos;
} global;

layout(set = 0, binding = 1) readonly buffer TransformSSBO { mat4 transforms[]; } transformBuf;
layout(set = 0, binding = 12) readonly buffer DecalSSBO    { DecalRecord decals[]; } decalBuf;

layout(location = 0) in vec3 inUnitCubePos; // [-0.5,0.5]^3 projector volume

layout(location = 0) flat out uint outDecalIndex;

void main() {
    DecalRecord d = decalBuf.decals[gl_InstanceIndex];
    mat4 host = transformBuf.transforms[d.anchorSlot]; // ride the live host pose
    gl_Position = global.proj * global.view * host * d.localTransform * vec4(inUnitCubePos, 1.0);
    outDecalIndex = gl_InstanceIndex;
}
