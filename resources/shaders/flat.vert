#version 460

// Fallback geometry stage for devices without VK_EXT_mesh_shader.
// Per-vertex half of flat.mesh: same SSBOs, same outputs.

// ---------------------------------------------------------------------------
// Resources & bindings
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    float time;
    float deltaTime;
    uint frameCount;
    uint lightCount;
    vec4 cameraPos;
} global;

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
} transformBuf;

struct EntityInfo {
    uint meshIndex;
    uint materialIndex;
};

layout(set = 0, binding = 3) readonly buffer EntitySSBO {
    EntityInfo entities[];
} entityBuf;

struct PackedVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer {
    PackedVertex vertices[];
} vertexBuf;

layout(set = 0, binding = 7) readonly buffer CompactedEntityIndices {
    uint entityIndices[];
} compactedBuf;

layout(push_constant) uniform PushConstants {
    uint transformBaseOffset;
    uint shadowFrustumIndex; // depth pass: shadow frustum viewProj index
} pc;

// Shadow pass selector: project by a light's shadow frustum (set 2) instead of the camera.
layout(constant_id = 0) const bool shadowPass = false;

struct CullView { mat4 viewProj; vec4 frustumPlanes[6]; };
layout(set = 2, binding = 0) readonly buffer ShadowFrustumSSBO { CullView shadowFrustums[]; } shadowBuf;

// cull.comp packs the LOD level into the top 3 bits, strip them to recover the entity index.
#define ANO_ENTITY_INDEX_MASK 0x1FFFFFFFu

// EQUAL-test contract (mirrors flat.mesh): invariant pins position codegen so both compiles produce bit-identical clip positions.
invariant gl_Position;

#ifndef ANO_DEPTH_ONLY
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint outMaterialIndex;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) flat out uint outEntityIndex; // slot index -> per-entity instance channel
#endif

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void main() {
    // Entity lookup as flat.mesh. gl_DrawID unavailable on MoltenVK, cull packs the draw ordinal into firstInstance (gl_InstanceIndex).
    uint entityIndex = compactedBuf.entityIndices[pc.transformBaseOffset + uint(gl_InstanceIndex)] & ANO_ENTITY_INDEX_MASK;
    EntityInfo entity = entityBuf.entities[entityIndex];

    // Programmable vertex pulling. gl_VertexIndex = global index into the shared vertex mega-buffer.
    PackedVertex v = vertexBuf.vertices[gl_VertexIndex];
    vec3 position = vec3(v.px, v.py, v.pz);

    mat4 model = transformBuf.transforms[entityIndex];
    vec4 worldPos = model * vec4(position, 1.0);

    gl_Position      = shadowPass ? (shadowBuf.shadowFrustums[pc.shadowFrustumIndex].viewProj * worldPos)
                                  : (global.proj * global.view * worldPos);
#ifndef ANO_DEPTH_ONLY
    // Inverse-transpose normal matrix: correct under non-uniform / negative / sheared scale.
    fragNormal       = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);
    fragTexCoord     = vec2(v.u, v.v);
    outMaterialIndex = entity.materialIndex;
    fragWorldPos     = worldPos.xyz;
    outEntityIndex   = entityIndex;
#endif
}
