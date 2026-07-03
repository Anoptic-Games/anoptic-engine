#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Additive lane (audit 4.7). Stripped flat.frag: no clustered-forward light loop, no shadow
// sampling. Emits a premultiplied emissive/base contribution; the pipeline blends ONE/ONE, which is
// commutative, so the cull pass's arbitrary draw order is correct (no sort needed). Shares the FLAT
// geometry stage outputs (locations 0..4).

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

// Per-entity instance channel (matches flat.frag). packed[0] = RGBA8 tint, packed[1] bit0 enables it.
const uint INST_FLAG_TINT = 1u;
struct InstanceData {
    uvec4 packed;
    vec4  params;
};
layout(set = 0, binding = 9) readonly buffer InstanceSSBO {
    InstanceData instances[];
} instanceBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in uint inEntityIndex;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materialBuf.materials[inMaterialIndex];

    vec4 base = mat.baseColorFactor;
    if (mat.baseColorTexture != 0xFFFFFFFF) {
        base *= texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    }

    // Per-entity tint, inert for unopted (zero) slots.
    InstanceData inst = instanceBuf.instances[inEntityIndex];
    if ((inst.packed.y & INST_FLAG_TINT) != 0u) {
        base *= unpackUnorm4x8(inst.packed.x);
    }

    vec3 emissive = mat.emissiveFactor.rgb * max(mat.emissiveStrength, 1.0);
    if (mat.emissiveTexture != 0xFFFFFFFF) {
        emissive *= texture(textures[nonuniformEXT(mat.emissiveTexture)], fragTexCoord).rgb;
    }

    // Premultiplied additive: contribution scales by alpha so authoring alpha acts as intensity.
    // ONE/ONE blend then sums these in any order without artifacts.
    vec3 col = (base.rgb + emissive) * base.a;
    outColor = vec4(col, base.a);
}
