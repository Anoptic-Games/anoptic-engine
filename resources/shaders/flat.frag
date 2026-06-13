#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct MaterialData {
    uint  albedoIndex;
    uint  normalIndex;
    float roughness;
    float emissive;
    vec4  color;
};

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint inMaterialIndex;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materialBuf.materials[inMaterialIndex];
    vec4 texColor = texture(textures[nonuniformEXT(mat.albedoIndex)], fragTexCoord);
    
    // We apply base color if we have a tint, or just default to texColor
    outColor = texColor; // For now
}
