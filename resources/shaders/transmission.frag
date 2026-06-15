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
    uint      pipelineType;
    uint      padding[2];
};

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materialBuf.materials[inMaterialIndex];
    vec4 texColor = texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    vec4 baseColor = texColor * mat.baseColorFactor;
    
    // Resolve transmission factor
    float transmission = mat.transmissionFactor;
    if (mat.transmissionTexture != 0) {
        transmission *= texture(textures[nonuniformEXT(mat.transmissionTexture)], fragTexCoord).r;
    }
    
    // Resolve thickness
    float thickness = mat.thicknessFactor;
    if (mat.thicknessTexture != 0) {
        thickness *= texture(textures[nonuniformEXT(mat.thicknessTexture)], fragTexCoord).g; // GLTF thickness is in G channel
    }
    
    // Beer-Lambert law for volume absorption
    vec3 transmissionTint = vec3(1.0);
    if (thickness > 0.0 && mat.attenuationDistance > 0.0) {
        vec3 k = -log(mat.attenuationColor.rgb + vec3(1e-5)) / max(mat.attenuationDistance, 0.0001);
        transmissionTint = exp(-k * thickness);
    }
    
    // Flat / Face normal calculation
    vec3 normal = normalize(cross(dFdx(fragWorldPos), dFdy(fragWorldPos)));
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    
    // Single neutral global directional light
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 lightColor = vec3(1.0); // neutral white light
    vec3 ambient = vec3(0.2);
    vec3 lighting = ambient + lightColor * diffuse * 0.8;
    
    vec3 transmissiveColor = baseColor.rgb * transmissionTint;
    vec3 opaqueColor = baseColor.rgb * lighting;
    vec3 finalColor = mix(opaqueColor, transmissiveColor, transmission);
    
    // Glass usually has some transparency
    float alpha = mix(baseColor.a, 0.3, transmission);
    outColor = vec4(finalColor, alpha);
}
