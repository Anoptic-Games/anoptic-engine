#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
} global;

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
} transformBuf;

layout(push_constant) uniform PushConstants {
    uint transformBaseOffset;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint outMaterialIndex;

void main() {
    uint entityIndex = pc.transformBaseOffset + gl_InstanceIndex;
    mat4 model = transformBuf.transforms[entityIndex];

    gl_Position = global.proj * global.view * model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    outMaterialIndex = entityIndex;
}
