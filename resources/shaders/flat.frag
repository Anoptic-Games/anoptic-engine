#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct MaterialData {
    uint      features;
    uint      baseColorTexture;
    uint      pad0[2];            // Align baseColorFactor to 16 bytes
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
    uint      pad1[3];            // Align attenuationColor to 16 bytes
    vec4      attenuationColor;
    float     ior;
    uint      specularTexture;
    uint      specularColorTexture;
    float     specularFactor;
    vec4      specularColorFactor;
    uint      sheenColorTexture;
    uint      sheenRoughnessTexture;
    uint      pad2[2];            // Align sheenColorFactor to 16 bytes
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
    uint      pad3[2];            // Align diffuseTransmissionColorFactor to 16 bytes
    vec4      diffuseTransmissionColorFactor;
    float     emissiveStrength;
    uint      padding[3];
};

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materialBuf.materials[inMaterialIndex];
    vec4 texColor = texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    
    // We apply base color if we have a tint, or just default to texColor
    outColor = texColor * mat.baseColorFactor;
}
