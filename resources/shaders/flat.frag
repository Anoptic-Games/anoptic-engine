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

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    float time;
    float deltaTime;
    uint frameCount;
    uint padding;
} global;

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

vec3 calculatePBR(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L) {
    // Clamp roughness to prevent collapsing specular highlights (roughness = 0.0 -> specular = 0.0)
    roughness = max(roughness, 0.04);

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - HdotV, 0.0, 1.0), 5.0);

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = (NdotH * NdotH * (alpha2 - 1.0) + 1.0);
    float D = alpha2 / (3.14159 * denom * denom);

    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G1V * G1L;

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / 3.14159 + specular) * NdotL;
}

void main() {
    MaterialData mat = materialBuf.materials[inMaterialIndex];
    vec4 baseColor = mat.baseColorFactor;
    if (mat.baseColorTexture != 0xFFFFFFFF) {
        baseColor *= texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    }
    
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if (mat.metallicRoughnessTexture != 0xFFFFFFFF) {
        vec4 orm = texture(textures[nonuniformEXT(mat.metallicRoughnessTexture)], fragTexCoord);
        roughness *= orm.g;
        metallic *= orm.b;
    }
    
    float occlusion = 1.0;
    if (mat.occlusionTexture != 0xFFFFFFFF) {
        occlusion = texture(textures[nonuniformEXT(mat.occlusionTexture)], fragTexCoord).r;
    }

    // Quick Test: Swap dFdx and dFdy to flip the normal outward
    vec3 N = normalize(cross(dFdy(fragWorldPos), dFdx(fragWorldPos)));
    if (!gl_FrontFacing) {
        N = -N;
    }
    
    mat4 invView = inverse(global.view);
    vec3 cameraPos = invView[3].xyz;
    vec3 V = normalize(cameraPos - fragWorldPos);
    
    // -------------------------------------------------------------
    // Direct Lighting Calculation
    // -------------------------------------------------------------
    vec3 accumulatedDirect = vec3(0.0);

    // 1. Directional light contribution
    vec3 L_dir = normalize(vec3(0.5, 1.0, 0.3));
    accumulatedDirect += calculatePBR(baseColor.rgb, metallic, roughness, N, V, L_dir) * vec3(0.4);

    // 2. Multiple Point lights setup
    struct PointLight {
        vec3 position;
        vec3 color;
        float intensity;
    };

    // Define 4 distinct point lights (you can adjust positions, colors, and intensities here)
    PointLight lights[4];
    
    // Original Light (Warm Yellow-White)
    lights[0] = PointLight(vec3(0.0, 1.5, 1.2), vec3(1.0, 0.95, 0.8), 5.0);
    
    // Light 2 (Cool Blue - opposite side)
    lights[1] = PointLight(vec3(-2.0, 2.0, -1.0), vec3(0.4, 0.6, 1.0), 4.0);
    
    // Light 3 (Subtle Red rim/accent light)
    lights[2] = PointLight(vec3(2.0, 0.5, 0.0), vec3(1.0, 0.3, 0.3), 3.5);
    
    // Light 4 (Greenish/Cyan fill light from below)
    lights[3] = PointLight(vec3(0.0, -1.0, 1.0), vec3(0.3, 1.0, 0.8), 2.0);

    // Loop through and accumulate all point lights
    for (int i = 0; i < 4; i++) {
        vec3 L_point = lights[i].position - fragWorldPos;
        float dist = length(L_point);
        L_point = normalize(L_point);
        
        // Attenuation calculation
        float attenuation = 1.0 / (dist * dist + 0.1);
        vec3 pointLightColor = lights[i].color * lights[i].intensity;
        
        accumulatedDirect += calculatePBR(baseColor.rgb, metallic, roughness, N, V, L_point) * pointLightColor * attenuation;
    }

    // -------------------------------------------------------------
    // Ambient & Final Color Assembly
    // -------------------------------------------------------------
    vec3 ambient = vec3(0.05) * baseColor.rgb * occlusion;
    vec3 finalColor = ambient + accumulatedDirect;
    
    // NOTE: Your original shader calculates PBR lighting but overrides the output 
    // to visualize the normal map. 
    // To see the lights working, swap the comment on the two lines below!
    
    // vec3 normalColor = N * 0.5 + 0.5;
    // outColor = vec4(normalColor, 1.0); // Normal map visualization
    
    outColor = vec4(finalColor, 1.0); // Actual lit PBR result
}
