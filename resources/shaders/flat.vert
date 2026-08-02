#version 460
#extension GL_GOOGLE_include_directive : require

#include "gpu_abi.glsl"

// Fallback geometry stage for devices without VK_EXT_mesh_shader.
// Per-vertex half of flat.mesh, same SSBOs and outputs.

// Compile modes mirror flat.mesh.
#if !defined(ANO_DEPTH_ONLY) && !defined(ANO_DEPTH_MASKED)
    #define ANO_WANT_SHADE 1
#endif
#if defined(ANO_WANT_SHADE) || defined(ANO_DEPTH_MASKED)
    #define ANO_WANT_UV 1
#endif

// ---------------------------------------------------------------------------
// Resources & bindings
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform GlobalUBO { GlobalData global; };

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
} transformBuf;

layout(set = 0, binding = 3) readonly buffer EntitySSBO {
    EntityInfo entities[];
} entityBuf;

layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer {
    PackedVertex vertices[];
} vertexBuf;

layout(set = 0, binding = 7) readonly buffer CompactedEntityIndices {
    uint entityIndices[];
} compactedBuf;

layout(push_constant) uniform PushConstants {
    uint transformBaseOffset;
    uint shadowFrustumIndex; // shadow frustum viewProj index
} pc;

// Shadow pass projects by a light's shadow frustum instead of the camera.
layout(constant_id = 0) const bool shadowPass = false;

layout(set = 2, binding = 0) readonly buffer ShadowFrustumSSBO { CullView shadowFrustums[]; } shadowBuf;

// Strip LOD level from top 3 bits to recover the entity index.
#define ANO_ENTITY_INDEX_MASK 0x1FFFFFFFu

// Pins position codegen for bit-identical clip positions.
invariant gl_Position;

// Packs material and entity slot into one flat scalar, world position reconstructed fragment-side.
#ifdef ANO_WANT_SHADE
layout(location = 0) out vec3 fragNormal;
#endif
#ifdef ANO_WANT_UV
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint outPackedIndices;
#endif

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void main() {
    // Entity lookup as flat.mesh, draw ordinal in gl_InstanceIndex.
    uint entityIndex = compactedBuf.entityIndices[pc.transformBaseOffset + uint(gl_InstanceIndex)] & ANO_ENTITY_INDEX_MASK;
    EntityInfo entity = entityBuf.entities[entityIndex];

    // Programmable vertex pulling, gl_VertexIndex indexes the shared vertex buffer.
    PackedVertex v = vertexBuf.vertices[gl_VertexIndex];
    vec3 position = vec3(v.position[0], v.position[1], v.position[2]);

    // Affine transform + premultiplied viewProj.
    mat4 model = transformBuf.transforms[entityIndex];
    vec3 worldPos = mat3(model) * position + model[3].xyz;

    gl_Position      = shadowPass ? (shadowBuf.shadowFrustums[pc.shadowFrustumIndex].viewProj * vec4(worldPos, 1.0))
                                  : (global.viewProj * vec4(worldPos, 1.0));
#ifdef ANO_WANT_SHADE
    // Inverse-transpose normal matrix, correct under non-uniform scale.
    fragNormal       = transpose(inverse(mat3(model))) * vec3(v.normal[0], v.normal[1], v.normal[2]);
#endif
#ifdef ANO_WANT_UV
    fragTexCoord     = vec2(v.texCoord[0], v.texCoord[1]);
    outPackedIndices = (entity.materialIndex << 20) | entityIndex; // material < 4096, slot < 2^20
#endif
}
