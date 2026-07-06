#version 450
#extension GL_GOOGLE_include_directive : require
#if ANO_ALPHA_MASK
#extension GL_EXT_nonuniform_qualifier : require
#endif

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

#if ANO_ALPHA_MASK
// Alpha-tested caster variant (glTF alphaMode MASK: foliage, chains). The geometry stage is the
// ANO_DEPTH_MASKED compile (position + uv + packed indices, location 0 gap intentional): sample
// baseColor.a and discard below the cutoff so cutout casters shadow by silhouette, not quad.
// MaterialData mirrors flat.frag (full struct for the std430 stride).
struct MaterialData {
    uint      features;
    uint      baseColorTexture;
    uint      pad0[2];
    vec4      baseColorFactor;
    uint      metallicRoughnessTexture;
    float     metallicFactor;
    float     roughnessFactor;
    uint      normalTexture;
    float     normalScale;
    uint      occlusionTexture;
    float     occlusionStrength;
    uint      emissiveTexture;
    vec4      emissiveFactor;
    uint      alphaMode;
    float     alphaCutoff;
    uint      doubleSided;
    uint      clearcoatTexture;
    uint      clearcoatRoughnessTexture;
    uint      clearcoatNormalTexture;
    float     clearcoatFactor;
    float     clearcoatRoughnessFactor;
    uint      transmissionTexture;
    float     transmissionFactor;
    uint      thicknessTexture;
    float     thicknessFactor;
    float     attenuationDistance;
    uint      pad1[3];
    vec4      attenuationColor;
    float     ior;
    uint      specularTexture;
    uint      specularColorTexture;
    float     specularFactor;
    vec4      specularColorFactor;
    uint      sheenColorTexture;
    uint      sheenRoughnessTexture;
    uint      pad2[2];
    vec4      sheenColorFactor;
    float     sheenRoughnessFactor;
    uint      iridescenceTexture;
    uint      iridescenceThicknessTexture;
    float     iridescenceFactor;
    float     iridescenceIor;
    float     iridescenceThicknessMinimum;
    float     iridescenceThicknessMaximum;
    uint      anisotropyTexture;
    float     anisotropyStrength;
    float     anisotropyRotation;
    float     dispersion;
    uint      diffuseTransmissionTexture;
    uint      diffuseTransmissionColorTexture;
    float     diffuseTransmissionFactor;
    uint      pad3[2];
    vec4      diffuseTransmissionColorFactor;
    float     emissiveStrength;
    uint      pipelineType;
    uint      padding[2];
};
layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;
layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inPackedIndices; // material (high 12 bits) | entity slot (low 20)
#endif

layout(location = 0) out vec4 outSubA; // bands 0/1: (cov0,M0,cov1,M1)
layout(location = 1) out vec4 outSubB; // bands 2/3: (cov2,M2,cov3,M3)

void main() {
#if ANO_ALPHA_MASK
    MaterialData mat = materialBuf.materials[inPackedIndices >> 20];
    float a = mat.baseColorFactor.a;
    if (mat.baseColorTexture != 0xFFFFFFFF) {
        a *= texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord).a;
    }
    if (a < mat.alphaCutoff) {
        discard;
    }
#endif
    anoEncodeLayered(gl_FragCoord.z, outSubA, outSubB);
}
