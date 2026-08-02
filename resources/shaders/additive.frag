#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "gpu_abi.glsl"

// Additive lane. Stripped flat.frag: no light loop, no shadows. Premultiplied emissive/base, ONE/ONE blend.

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

// Per-entity instance channel. packed[0] = RGBA8 tint, packed[1] bit0 enables it.
const uint INST_FLAG_TINT = 1u;
layout(set = 0, binding = 9) readonly buffer InstanceSSBO {
    InstanceData instances[];
} instanceBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inPackedIndices; // material (high 12 bits) | entity slot (low 20)

layout(location = 0) out vec4 outColor;

void main() {
    uint inMaterialIndex = inPackedIndices >> 20;
    uint inEntityIndex   = inPackedIndices & 0xFFFFFu;
    MaterialData mat = materialBuf.materials[inMaterialIndex];

    vec4 base = mat.baseColorFactor;
    if (mat.baseColorTexture != 0xFFFFFFFF) {
        base *= texture(textures[nonuniformEXT(mat.baseColorTexture)], fragTexCoord);
    }

    // Per-entity tint, inert for unopted (zero) slots.
    InstanceData inst = instanceBuf.instances[inEntityIndex];
    if ((inst.packed.y & INST_FLAG_TINT) != 0u) {
        base *= unpackUnorm4x8(inst.packed.x);
    }

    vec3 emissive = mat.emissiveFactor.rgb * max(mat.emissiveStrength, 1.0);
    if (mat.emissiveTexture != 0xFFFFFFFF) {
        emissive *= texture(textures[nonuniformEXT(mat.emissiveTexture)], fragTexCoord).rgb;
    }

    // Premultiplied additive: alpha acts as intensity.
    vec3 col = (base.rgb + emissive) * base.a;
    outColor = vec4(col, base.a);
}
