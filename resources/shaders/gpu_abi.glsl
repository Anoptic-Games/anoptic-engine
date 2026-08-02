#ifndef ANO_GPU_ABI_GLSL
#define ANO_GPU_ABI_GLSL

// Shared CPU/GPU records. src/vulkan_backend/gpu_abi_schema.c reflects the C++ declarations,
// parses these declarations at consteval time and rejects names, scalar domains, offsets or strides that drift.

struct EntityInfo {
    uint meshIndex;
    uint materialIndex;
};

struct PackedVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct MeshData {
    uint meshletCount;
    uint meshletOffset;
    uint uniqueVerticesOffset;
    uint trianglesOffset;
    uint vertexOffset;
    uint classicIndexCount;
    uint classicFirstIndex;
    uint boundsOffset;
    uint lodCount;
};

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

struct GlobalData {
    mat4  view;
    mat4  proj;
    float time;
    float deltaTime;
    uint  frameCount;
    uint  lightCount;
    vec4  cameraPos;
    float cameraNear;
    float cameraFar;
    float screenWidth;
    float screenHeight;
    uint  clusterDimX;
    uint  clusterDimY;
    uint  clusterDimZ;
    uint  maxLightsPerCluster;
    uint  lightingMode;
    uint  debugView;
    uint  pad0;
    uint  pad1;
    mat4  viewProj;
    mat4  invVPPixel;
    vec4  frustumPlanes[6];
    mat4  prevViewProj;
    vec4  hizParams;
    vec4  hizProj;
};

struct CullView {
    mat4 viewProj;
    vec4 frustumPlanes[6];
};

struct CullData {
    CullView views[2];
    uint     viewCount;
    uint     entityCount;
    uint     maxEntities;
    uint     drawSlotCount;
    uvec4    drawSlotOf[4];
    uvec4    specialSlots;
    vec4     viewCullParams[2];
    int      shadowLodBias;
    int      _hizPad0;
    int      _hizPad1;
    int      _hizPad2;
    mat4     prevViewProj[2];
    vec4     hizParams[2];
    vec4     hizProj[2];
    uvec4    taskParams;
};

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
    vec3  localOffset;
    uint  pad2;
    vec3  localDir;
    uint  pad3;
};

struct LightRuntime {
    vec4 posRange;
    vec4 dirType;
    vec4 radInner;
    vec4 outer;
};

struct InstanceData {
    uvec4 packed;
    vec4  params;
};

struct ShadowLightInfo {
    uint castsShadow;
    uint baseFrustum;
    uint frustumCount;
    uint pad;
};

struct ShadowFrustumConfig {
    uint lightIndex;
    uint lightType;
    uint faceIndex;
    uint live;
};

struct MotionDescriptor {
    uint  type;
    uint  flags;
    float epoch;
    float _pad;
    vec4  p0;
    vec4  p1;
};

struct DecalRecord {
    mat4  localTransform;
    uint  anchorSlot;
    uint  textureLayer;
    float fade;
    uint  flags;
};

struct SkinInstanceState {
    uint  rigId;
    uint  clipA;
    uint  clipB;
    float blendStart;
    float blendDuration;
    float clipStartPhase;
};

#endif
