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
    uint lightCount;
    vec4 cameraPos;
    float cameraNear;
    float cameraFar;
    float screenWidth;
    float screenHeight;
    uint clusterDimX;
    uint clusterDimY;
    uint clusterDimZ;
    uint maxLightsPerCluster;
    uint lightingMode;   // AnoLightingMode (RADIANCE_CASCADES.md); gates shadow sampling below
    uint debugView;      // RC debug visualization selector (0 = off)
    float giStrength;    // RC GI ambient gain (runtime-tunable)
    uint pad1;
} global;

// Clustered-forward froxel light lists (see flat.frag / LIGHTING_SCALE.md). Transparent
// fragments bin into their froxel exactly like opaque ones, so transmission needs no separate
// light path.
layout(set = 0, binding = 10) readonly buffer ClusterCountSSBO {
    uint clusterLightCount[];
} clusterCountBuf;
layout(set = 0, binding = 11) readonly buffer ClusterIndexSSBO {
    uint clusterLightIndices[];
} clusterIndexBuf;

// RC voxel albedo (debug, 12) + cascade-0 irradiance (GI ambient, 13); mirrors flat.frag.
layout(set = 0, binding = 12) uniform sampler3D rcVoxelAlbedo;
layout(set = 0, binding = 13) uniform sampler3D rcIrradiance;
const float ANO_RC_CLIP_HALF = 8.0;
vec3 anoRcUv(vec3 w) { return (w + vec3(ANO_RC_CLIP_HALF)) / (2.0 * ANO_RC_CLIP_HALF); }

// --- Dynamic shadows (set 2), mirrors flat.frag (audit 4.7) ---
struct ShadowCullView { mat4 viewProj; vec4 frustumPlanes[6]; };
struct ShadowLightInfo { uint castsShadow; uint baseFrustum; uint frustumCount; uint pad; };
layout(set = 2, binding = 0) readonly buffer ShadowFrustumSSBO { ShadowCullView shadowFrustums[]; } shadowFrustumBuf;
layout(set = 2, binding = 1) uniform sampler2DArrayShadow shadowAtlas;
layout(set = 2, binding = 2) readonly buffer ShadowLightInfoSSBO { ShadowLightInfo info[]; } shadowInfoBuf;

float sampleShadowPCF(uint layer, vec3 worldPos, float nDotL) {
    vec4 lc = shadowFrustumBuf.shadowFrustums[layer].viewProj * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float bias = max(0.0015 * (1.0 - nDotL), 0.0004);
    float ref = proj.z - bias;
    vec2 texel = 1.0 / vec2(textureSize(shadowAtlas, 0).xy);
    float sum = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            sum += texture(shadowAtlas, vec4(uv + vec2(dx, dy) * texel, float(layer), ref));
    return sum / 9.0;
}

// Point-light cube face for direction d = fragWorldPos - lightPos. 0..5 = +X,-X,+Y,-Y,+Z,-Z; MUST
// match shadowsetup.comp's cubeFaceBasis so layer baseFrustum+face reprojects the fragment in-range.
uint cubeFaceIndex(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x >= 0.0 ? 0u : 1u;
    if (a.y >= a.z)               return d.y >= 0.0 ? 2u : 3u;
    return d.z >= 0.0 ? 4u : 5u;
}

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

// ---------------------------------------------------------------------------
// Punctual lights (KHR_lights_punctual style). A light's world position and
// direction are NOT stored in LightData; they are derived from its driving
// entity's live transform (transforms[transformIndex]) so GPU animation applies.
// ---------------------------------------------------------------------------
const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT       = 1u;
const uint LIGHT_TYPE_SPOT        = 2u;

// Lighting mode (AnoLightingMode, RADIANCE_CASCADES.md). MUST match the C-side
// lightTypeShadowMapped() gating the shadow depth render and flat.frag's lightUsesShadowMap.
const uint ANO_LIGHTING_SHADOWMAP = 0u;
const uint ANO_LIGHTING_HYBRID    = 1u;
const uint ANO_LIGHTING_RC        = 2u;
bool lightUsesShadowMap(uint lightType, uint mode) {
    if (mode == ANO_LIGHTING_SHADOWMAP) return true;
    if (mode == ANO_LIGHTING_RC)        return false;
    return lightType != LIGHT_TYPE_POINT; // ANO_LIGHTING_HYBRID
}

struct LightData {
    vec3  color;
    float intensity;
    float range;
    float innerConeCos;
    float outerConeCos;
    uint  type;
    uint  transformIndex;
    uint  enabled;
    uint  pad0;
    uint  pad1;
};

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
} transformBuf;

layout(set = 0, binding = 8) readonly buffer LightSSBO {
    LightData lights[];
} lightBuf;

// Open-ended per-entity instance channel (matches flat.frag). packed[0] = RGBA8
// tint, packed[1] = flags (bit 0 enables tint), packed[2..3]/params reserved.
const uint INST_FLAG_TINT = 1u;
struct InstanceData {
    uvec4 packed;
    vec4  params;
};
layout(set = 0, binding = 9) readonly buffer InstanceSSBO {
    InstanceData instances[];
} instanceBuf;

// glTF range-based attenuation: inverse-square with a smooth window cutoff.
// range <= 0 means unbounded (pure inverse-square).
float getRangeAttenuation(float range, float dist) {
    float invSqr = 1.0 / max(dist * dist, 0.0001);
    if (range <= 0.0) {
        return invSqr;
    }
    float f = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return f * f * invSqr;
}

// Spot cone falloff. spotForward is the light's aim; L points surface->light.
float getSpotAttenuation(vec3 spotForward, vec3 L, float innerConeCos, float outerConeCos) {
    float cosAngle = dot(spotForward, -L);
    return smoothstep(outerConeCos, innerConeCos, cosAngle);
}

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in uint inEntityIndex;

layout(location = 0) out vec4 outColor;

vec3 calculatePBRDirect(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L, float transmission) {
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

    vec3 diffuse = (kD * albedo / 3.14159) * NdotL;
    vec3 specularReflection = specular * NdotL;

    return diffuse * (1.0 - transmission) + specularReflection;
}

void main() {
    // Debug views (RADIANCE_CASCADES.md M3); mirrors flat.frag. 1 = voxel albedo, 2 = irradiance.
    if (global.lightingMode != ANO_LIGHTING_SHADOWMAP) {
        if (global.debugView == 1u) {
            vec4 vox = texture(rcVoxelAlbedo, anoRcUv(fragWorldPos));
            outColor = vec4(vox.a > 0.5 ? vox.rgb : vec3(1.0, 0.0, 0.0), 1.0);
            return;
        }
        if (global.debugView == 2u) {
            outColor = vec4(texture(rcIrradiance, anoRcUv(fragWorldPos)).rgb, 1.0);
            return;
        }
    }

    MaterialData mat = materialBuf.materials[inMaterialIndex];
    vec4 baseColor = mat.baseColorFactor;
    if (mat.baseColorTexture != 0xFFFFFFFF) {
        baseColor *= texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    }

    // Per-entity instance channel: modulate base color by the packed tint when the
    // slot opts in. Inert for unopted (zero) slots.
    InstanceData inst = instanceBuf.instances[inEntityIndex];
    if ((inst.packed.y & INST_FLAG_TINT) != 0u) {
        baseColor *= unpackUnorm4x8(inst.packed.x);
    }

    // Resolve transmission factor
    float transmission = mat.transmissionFactor;
    if (mat.transmissionTexture != 0xFFFFFFFF) {
        transmission *= texture(textures[nonuniformEXT(mat.transmissionTexture)], fragTexCoord).r;
    }
    
    // Resolve thickness
    float thickness = mat.thicknessFactor;
    if (mat.thicknessTexture != 0xFFFFFFFF) {
        thickness *= texture(textures[nonuniformEXT(mat.thicknessTexture)], fragTexCoord).g; // GLTF thickness is in G channel
    }
    
    // Beer-Lambert law for volume absorption
    vec3 transmissionTint = vec3(1.0);
    if (thickness > 0.0 && mat.attenuationDistance > 0.0) {
        vec3 k = -log(mat.attenuationColor.rgb + vec3(1e-5)) / max(mat.attenuationDistance, 0.0001);
        transmissionTint = exp(-k * thickness);
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

    vec3 normal = normalize(fragNormal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    
    vec3 V = normalize(global.cameraPos.xyz - fragWorldPos);
    
    // Ambient & transmissive color contributions (evaluated once)
    vec3 ambient = (global.lightingMode != ANO_LIGHTING_SHADOWMAP
                    ? texture(rcIrradiance, anoRcUv(fragWorldPos)).rgb * global.giStrength
                    : vec3(0.05)) * baseColor.rgb * occlusion * (1.0 - transmission);
    vec3 transmissive = baseColor.rgb * transmissionTint * transmission;
    
    // Clustered forward: accumulate only this fragment's froxel lights (see flat.frag).
    vec3 accumulatedDirect = vec3(0.0);
    uint tileX = uint(clamp(gl_FragCoord.x / global.screenWidth, 0.0, 0.99999) * float(global.clusterDimX));
    uint tileY = uint(clamp(gl_FragCoord.y / global.screenHeight, 0.0, 0.99999) * float(global.clusterDimY));
    float viewZ = (global.view * vec4(fragWorldPos, 1.0)).z;
    float zDist = max(-viewZ, global.cameraNear);
    float zSliceF = log(zDist / global.cameraNear) / log(global.cameraFar / global.cameraNear);
    uint slice = uint(clamp(zSliceF, 0.0, 0.99999) * float(global.clusterDimZ));
    uint clusterIdx = (slice * global.clusterDimY + tileY) * global.clusterDimX + tileX;
    uint lightListBase = clusterIdx * global.maxLightsPerCluster;
    uint clusterCount = clusterCountBuf.clusterLightCount[clusterIdx];

    for (uint c = 0u; c < clusterCount; c++) {
        uint i = clusterIndexBuf.clusterLightIndices[lightListBase + c];
        LightData light = lightBuf.lights[i];
        if (light.enabled == 0u) {
            continue;
        }

        // Derive world placement from the light's driving entity transform.
        mat4 lightXform = transformBuf.transforms[light.transformIndex];
        vec3 lightPos = lightXform[3].xyz;
        vec3 lightForward = normalize(-lightXform[2].xyz); // entity local -Z is forward

        vec3 L;
        float attenuation;
        if (light.type == LIGHT_TYPE_DIRECTIONAL) {
            L = -lightForward;        // surface -> light (opposite the travel direction)
            attenuation = 1.0;
        } else {
            vec3 toLight = lightPos - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = getRangeAttenuation(light.range, dist);
            if (light.type == LIGHT_TYPE_SPOT) {
                attenuation *= getSpotAttenuation(lightForward, L, light.innerConeCos, light.outerConeCos);
            }
        }

        if (attenuation <= 0.0) {
            continue;
        }

        float shadowFactor = 1.0;
        ShadowLightInfo si = shadowInfoBuf.info[i];
        if (lightUsesShadowMap(light.type, global.lightingMode) && si.castsShadow != 0u && si.frustumCount > 0u) {
            float nDotL = max(dot(normal, L), 0.0);
            if (light.type == LIGHT_TYPE_POINT)
                shadowFactor = sampleShadowPCF(si.baseFrustum + cubeFaceIndex(fragWorldPos - lightPos), fragWorldPos, nDotL);
            else
                shadowFactor = sampleShadowPCF(si.baseFrustum, fragWorldPos, nDotL);
        }

        vec3 radiance = light.color * light.intensity * attenuation * shadowFactor;
        accumulatedDirect += calculatePBRDirect(baseColor.rgb, metallic, roughness, normal, V, L, transmission) * radiance;
    }

    vec3 finalColor = ambient + transmissive + accumulatedDirect;
    
    // Glass usually has some transparency
    float alpha = mix(baseColor.a, 0.3, transmission);
    outColor = vec4(finalColor, alpha);
}
