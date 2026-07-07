#version 450
#extension GL_GOOGLE_include_directive : require
#if ANO_ALPHA_MASK
#extension GL_EXT_nonuniform_qualifier : require
#endif

// Layered Power CDF shadow pass fragment stage. Writes the nearest occluder's one-hot into the depth band containing z, across two MRT color targets.
#include "shadow_cdf.glsl"

// Per-frustum depth linearization (mirrors shadow_sample.glsl).
layout(set = 2, binding = 3) uniform ShadowSampleVPUBO {
    mat4 viewProj[64];
    vec4 depthParams[64];
} shadowVPBuf;

layout(push_constant) uniform PushConstants {
    uint transformBaseOffset;
    uint shadowFrustumIndex;
} pc;

#if ANO_ALPHA_MASK
// Alpha-tested caster variant (glTF alphaMode MASK). MaterialData mirrors flat.frag.
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
layout(location = 2) flat in uint inPackedIndices; // material | entity slot
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
    vec4 dp = shadowVPBuf.depthParams[pc.shadowFrustumIndex];
    float d01 = (dp.x + dp.z * gl_FragCoord.z) / (1.0 - dp.y * gl_FragCoord.z);
    anoEncodeLayered(d01, outSubA, outSubB);
}
