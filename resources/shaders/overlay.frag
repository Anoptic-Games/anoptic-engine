#version 450

// Text/UI overlay composite: passthrough sample of the compute-rastered
// overlay, premultiplied linear RGBA. Fixed-function blend does src-over.
// Shares tonemap.vert's fullscreen triangle and set layout.

layout(set = 0, binding = 0) uniform sampler2D overlayTex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(overlayTex, uv);
}
