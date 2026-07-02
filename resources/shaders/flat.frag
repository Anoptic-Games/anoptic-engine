#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

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
    uint pad0;
    uint pad1;
} global;

// Clustered-forward froxel light lists (light-cull pass output). The fragment maps to its
// froxel and loops only [base, base+count) of the index list. See LIGHTING_SCALE.md.
layout(set = 0, binding = 10) readonly buffer ClusterCountSSBO {
    uint clusterLightCount[];
} clusterCountBuf;
layout(set = 0, binding = 11) readonly buffer ClusterIndexSSBO {
    uint clusterLightIndices[];
} clusterIndexBuf;

// --- Dynamic shadows (set 2): GPU-built shadow frustum viewProjs + the moment atlas array + the
// per-light shadow placement. A light that casts projects fragWorldPos into its shadow map and
// reconstructs occlusion from the stored moments (Hamburger 4MSM). (audit 4.7) ---
struct ShadowCullView { mat4 viewProj; vec4 frustumPlanes[6]; };
struct ShadowLightInfo { uint castsShadow; uint baseFrustum; uint frustumCount; uint pad; };
layout(set = 2, binding = 0) readonly buffer ShadowFrustumSSBO { ShadowCullView shadowFrustums[]; } shadowFrustumBuf;
layout(set = 2, binding = 1) uniform sampler2DArray shadowAtlas;
layout(set = 2, binding = 2) readonly buffer ShadowLightInfoSSBO { ShadowLightInfo info[]; } shadowInfoBuf;

// Power CDF reconstruct (sampleShadowCDF) + anoCubeFaceIndex, shared with transmission.frag. References
// shadowAtlas + shadowFrustumBuf declared just above.
#include "shadow_sample.glsl"

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

// Lighting mode (AnoLightingMode, RADIANCE_CASCADES.md). Whether a light's direct occlusion is
// shadow-mapped this frame, vs carried by the radiance cascade field. MUST match the C-side
// lightTypeShadowMapped() that gates the shadow depth render, or a gated-off atlas layer is
// sampled stale. HYBRID keeps directional + spot maps and routes point lights to RC.
const uint ANO_LIGHTING_SHADOWMAP = 0u;
const uint ANO_LIGHTING_HYBRID    = 1u;
const uint ANO_LIGHTING_RC        = 2u;
bool lightUsesShadowMap(uint lightType, uint mode) {
    if (mode == ANO_LIGHTING_SHADOWMAP) return true;
    if (mode == ANO_LIGHTING_RC)        return false;
    return lightType != LIGHT_TYPE_POINT; // ANO_LIGHTING_HYBRID
}

// The fragment no longer reads raw LightData (binding 8) or the transform buffer (binding 1): lightsetup.comp
// consumes those once per frame and hands the fragment a packed LightRuntime record (binding 12, below).
// Both bindings remain in the shared globalSetLayout for the geometry stage / other passes.

// Per-light fragment runtime set, precomputed once per frame by lightsetup.comp: world pose +
// premultiplied radiance + range/cone/type packed into one 64B record. Replaces the former per-fragment
// 80B LightData load + 32B pose + transform derivation. Indexed by the froxel light row. Layout MUST
// match LightRuntime in lightsetup.comp / transmission.frag.
struct LightRuntime {
    vec4 posRange;   // xyz world position, w range
    vec4 dirType;    // xyz world forward,  w float(type)
    vec4 radInner;   // xyz color*intensity, w innerConeCos
    vec4 outer;      // x outerConeCos, yzw reserved
};
layout(set = 0, binding = 12) readonly buffer LightRuntimeSSBO {
    LightRuntime entries[];
} lightRuntimeBuf;

// Open-ended per-entity instance channel. packed[0] = RGBA8 tint, packed[1] = flag
// bits (bit 0 enables tint), packed[2..3]/params reserved. All-zero == inert.
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
layout(location = 1) out uint outId; // GPU slot of this fragment, for cursor picking (opaque pass only)

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

    // Per-entity instance channel: modulate the palette material's base color by the
    // packed tint when the slot opts in. Inert for unopted (zero) slots, so this is a
    // no-op for everything that does not set the flag.
    InstanceData inst = instanceBuf.instances[inEntityIndex];
    if ((inst.packed.y & INST_FLAG_TINT) != 0u) {
        baseColor *= unpackUnorm4x8(inst.packed.x);
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

    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) {
        N = -N;
    }
    
    vec3 V = normalize(global.cameraPos.xyz - fragWorldPos);
    
    // -------------------------------------------------------------
    // Direct Lighting Calculation
    // -------------------------------------------------------------
    vec3 accumulatedDirect = vec3(0.0);

    // Map this fragment to its froxel (screen tile x depth slice) and accumulate only the
    // lights the light-cull pass assigned to it. Per-fragment cost tracks local light density,
    // not total light count.
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
        // Single 64B runtime load per light (lightsetup.comp precomputed it): world pose, premultiplied
        // radiance, range/cone/type. Replaces the old 80B LightData + 32B pose loads + transform derivation.
        // The froxel light-cull already excludes disabled lights, so no enabled check is needed here.
        LightRuntime lr = lightRuntimeBuf.entries[i];
        vec3 lightPos = lr.posRange.xyz;
        vec3 lightForward = lr.dirType.xyz;
        float lightRange = lr.posRange.w;
        uint lightType = uint(lr.dirType.w);
        vec3 lightRadiance = lr.radInner.xyz; // color * intensity (premultiplied)
        float innerConeCos = lr.radInner.w;
        float outerConeCos = lr.outer.x;

        vec3 L;
        float attenuation;
        if (lightType == LIGHT_TYPE_DIRECTIONAL) {
            L = -lightForward;        // surface -> light (opposite the travel direction)
            attenuation = 1.0;
        } else {
            vec3 toLight = lightPos - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);
            attenuation = getRangeAttenuation(lightRange, dist);
            if (lightType == LIGHT_TYPE_SPOT) {
                attenuation *= getSpotAttenuation(lightForward, L, innerConeCos, outerConeCos);
            }
        }

        if (attenuation <= 0.0) {
            continue;
        }

        // A light whose direction faces away from the surface contributes exactly zero: the entire BRDF is
        // scaled by NdotL. Skip its shadow taps + BRDF before paying for them (the dominant avoidable cost
        // on shadow-heavy views). nDotL is reused below as the shadow slope bias.
        float nDotL = max(dot(N, L), 0.0);
        if (nDotL <= 0.0) {
            continue;
        }

        // Shadowing: a casting light attenuates radiance by its shadow map. Directional/spot sample
        // their single frustum at baseFrustum; point lights pick one of 6 cube faces by dominant axis.
        // si is read only when the light can actually shadow this frame; otherwise the SSBO load is dead.
        float shadowFactor = 1.0;
        if (lightUsesShadowMap(lightType, global.lightingMode)) {
            ShadowLightInfo si = shadowInfoBuf.info[i];
            if (si.castsShadow != 0u && si.frustumCount > 0u) {
                if (lightType == LIGHT_TYPE_POINT)
                    shadowFactor = sampleShadowCDF(si.baseFrustum + anoCubeFaceIndex(fragWorldPos - lightPos), fragWorldPos, nDotL);
                else
                    shadowFactor = sampleShadowCDF(si.baseFrustum, fragWorldPos, nDotL);
            }
        }

        vec3 radiance = lightRadiance * attenuation * shadowFactor;
        accumulatedDirect += calculatePBR(baseColor.rgb, metallic, roughness, N, V, L) * radiance;
    }

    // -------------------------------------------------------------
    // Ambient & Final Color Assembly
    // -------------------------------------------------------------
    vec3 ambient = vec3(0.05) * baseColor.rgb * occlusion;
    vec3 finalColor = ambient + accumulatedDirect;

    outColor = vec4(finalColor, 1.0);
    outId = inEntityIndex; // 0xFFFFFFFF clear value == no renderable under this pixel
}
