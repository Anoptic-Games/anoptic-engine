#version 450

// Radiance cascades M2 — rasterized clipmap voxelization (RADIANCE_CASCADES.md).
// Reuses flat.mesh / flat.vert with shadowPass = true: the geometry stage projects the
// cull-compacted geometry by an axis-aligned orthographic clipmap matrix (3 passes, one per
// dominant axis, so a triangle edge-on to one axis is face-on to another). This fragment maps its
// interpolated world position to a voxel and imageStores the material's albedo + opacity and
// emission. No colour or depth attachment — pure imageStore side effects. M3 marches these voxels.
//
// Static origin-centred clipmap for M2: world [-H,H]^3 -> voxel [0,dim)^3, H = ANO_RC_CLIP_HALF.
// Must match the CPU-built ortho matrices (createRcResources) and ANO_RC_CLIP_HALF_EXTENT (structs.h).
// Camera-following anchoring + a clipmap cull frustum are follow-ons; M2 voxelizes view 0's
// cull-compacted opaque partition, so off-camera geometry is not yet covered.

const float ANO_RC_CLIP_HALF = 8.0;

layout(set = 2, binding = 1, rgba16f) uniform writeonly image3D rcVoxelAlbedo;   // albedo.rgb + opacity.a
layout(set = 2, binding = 2, rgba16f) uniform writeonly image3D rcVoxelEmission; // emission.rgb (a unused)

// MaterialData mirror (set 0, binding 2). Full layout so std430 offsets of the read fields
// (baseColorFactor, emissiveFactor, emissiveStrength) match the C MaterialData byte-for-byte.
struct MaterialData {
    uint  features;
    uint  baseColorTexture;
    uint  pad0[2];
    vec4  baseColorFactor;
    uint  metallicRoughnessTexture;
    float metallicFactor;
    float roughnessFactor;
    uint  normalTexture;
    float normalScale;
    uint  occlusionTexture;
    float occlusionStrength;
    uint  emissiveTexture;
    vec4  emissiveFactor;
    uint  alphaMode;
    float alphaCutoff;
    uint  doubleSided;
    uint  clearcoatTexture;
    uint  clearcoatRoughnessTexture;
    uint  clearcoatNormalTexture;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    uint  transmissionTexture;
    float transmissionFactor;
    uint  thicknessTexture;
    float thicknessFactor;
    float attenuationDistance;
    uint  pad1[3];
    vec4  attenuationColor;
    float ior;
    uint  specularTexture;
    uint  specularColorTexture;
    float specularFactor;
    vec4  specularColorFactor;
    uint  sheenColorTexture;
    uint  sheenRoughnessTexture;
    uint  pad2[2];
    vec4  sheenColorFactor;
    float sheenRoughnessFactor;
    uint  iridescenceTexture;
    uint  iridescenceThicknessTexture;
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThicknessMinimum;
    float iridescenceThicknessMaximum;
    uint  anisotropyTexture;
    float anisotropyStrength;
    float anisotropyRotation;
    float dispersion;
    uint  diffuseTransmissionTexture;
    uint  diffuseTransmissionColorTexture;
    float diffuseTransmissionFactor;
    uint  pad3[2];
    vec4  diffuseTransmissionColorFactor;
    float emissiveStrength;
    uint  pipelineType;
    uint  padding[2];
};

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) flat in uint inEntityIndex;

void main() {
    ivec3 dim = imageSize(rcVoxelAlbedo);
    vec3 t = (fragWorldPos + vec3(ANO_RC_CLIP_HALF)) / (2.0 * ANO_RC_CLIP_HALF); // world -> [0,1]
    if (any(lessThan(t, vec3(0.0))) || any(greaterThanEqual(t, vec3(1.0)))) return; // outside clipmap
    ivec3 p = clamp(ivec3(t * vec3(dim)), ivec3(0), dim - 1);

    MaterialData m = materialBuf.materials[inMaterialIndex];
    vec3 albedo   = m.baseColorFactor.rgb;
    vec3 emission = m.emissiveFactor.rgb * max(m.emissiveStrength, 1.0);

    imageStore(rcVoxelAlbedo,   p, vec4(albedo, 1.0)); // a = 1 marks an occupied voxel
    imageStore(rcVoxelEmission, p, vec4(emission, 0.0));
}
