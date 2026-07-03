#version 460

// Fallback geometry stage for devices without VK_EXT_mesh_shader.
// This is the per-vertex half of flat.mesh: same SSBOs, same outputs. The cull
// compute pass emits VkDrawIndexedIndirectCommands and the hardware index/vertex
// fetch expands the mesh the way the mesh shader expanded meshlets, so here we only
// transform one vertex. flat.frag / transmission.frag consume the outputs unchanged.

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
    uint shadowFrustumIndex; // depth pass only: which shadow frustum's viewProj to project by
} pc;

// Depth-only shadow pass selector: when true, project by a light's shadow frustum (set 2) into
// the shadow atlas instead of the camera. The fragment stage is the trivial depth-only shader.
layout(constant_id = 0) const bool shadowPass = false;

struct CullView { mat4 viewProj; vec4 frustumPlanes[6]; };
layout(set = 2, binding = 0) readonly buffer ShadowFrustumSSBO { CullView shadowFrustums[]; } shadowBuf;

// cull.comp packs the LOD level into the top 3 bits of the compacted entity index. The fallback path
// gets its geometry from the indirect command (the selected level's index range), so here we only
// strip the level bits to recover the entity index for the transform / material lookup.
#define ANO_ENTITY_INDEX_MASK 0x1FFFFFFFu

// EQUAL-test contract (mirrors flat.mesh): the depth pre-pass runs the ANO_DEPTH_ONLY compile of
// this source and the opaque pass EQUAL-tests against its depth — invariant pins the position
// codegen so both compiles produce bit-identical clip positions.
invariant gl_Position;

#ifndef ANO_DEPTH_ONLY
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint outMaterialIndex;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) flat out uint outEntityIndex; // slot index -> per-entity instance channel
#endif

void main() {
    // Same entity lookup as flat.mesh, but Metal/MoltenVK has no DrawIndex builtin so
    // gl_DrawID is unavailable here. The cull pass packs each draw's ordinal into
    // firstInstance instead; instanceCount is always 1, so gl_InstanceIndex equals it.
    uint entityIndex = compactedBuf.entityIndices[pc.transformBaseOffset + uint(gl_InstanceIndex)] & ANO_ENTITY_INDEX_MASK;
    EntityInfo entity = entityBuf.entities[entityIndex];

    // Programmable vertex pulling. gl_VertexIndex = command.vertexOffset + indexBuffer[i],
    // i.e. the global vertex index into the shared vertex mega-buffer.
    PackedVertex v = vertexBuf.vertices[gl_VertexIndex];
    vec3 position = vec3(v.px, v.py, v.pz);

    mat4 model = transformBuf.transforms[entityIndex];
    vec4 worldPos = model * vec4(position, 1.0);

    gl_Position      = shadowPass ? (shadowBuf.shadowFrustums[pc.shadowFrustumIndex].viewProj * worldPos)
                                  : (global.proj * global.view * worldPos);
#ifndef ANO_DEPTH_ONLY
    // Inverse-transpose normal matrix: correct under non-uniform / negative / sheared scale.
    // Equals mat3(model) for rotation + uniform scale (the common case). docs/math_conventions.md.
    fragNormal       = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);
    fragTexCoord     = vec2(v.u, v.v);
    outMaterialIndex = entity.materialIndex;
    fragWorldPos     = worldPos.xyz;
    outEntityIndex   = entityIndex;
#endif
}
