#version 450
#extension GL_EXT_nonuniform_qualifier : require

// SKELETON — not in the CMakeLists shader manifest, not loaded by any pipeline.
// Documents the DECAL DRAW STAGE fragment half (PIPELINE_DECAL). It reconstructs the
// surface under the projector volume (depth read or forward UV), samples the decal
// texture layer, and blends with the record's fade. Paired with decal.vert.

struct DecalRecord {
    mat4 localTransform;
    uint anchorSlot;
    uint textureLayer;
    float fade;
    uint flags;
};

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
