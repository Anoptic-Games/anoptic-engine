#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// New uniform buffer object for additional model transformations
layout(set = 1, binding = 0) uniform ExtraModelTransforms {
    mat4 translation;
    mat4 rotation;
    mat4 scale;
} extraTransforms;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    // Apply the extra model transformations
    mat4 modelTransform = extraTransforms.translation * extraTransforms.rotation * extraTransforms.scale;

    // Combine the original model matrix with the new transformations
    gl_Position = ubo.proj * ubo.view * (ubo.model * modelTransform) * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
