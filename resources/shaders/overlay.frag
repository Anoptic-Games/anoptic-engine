#version 450

// Text/UI overlay composite (FONT_RENDER.md): samples the compute-rastered overlay
// (premultiplied linear RGBA) over the tonemapped swapchain. The src-over happens in
// fixed-function blend (ONE, ONE_MINUS_SRC_ALPHA); this stage is a passthrough sample.
// Shares tonemap.vert's fullscreen triangle and the tonemap set layout.

layout(set = 0, binding = 0) uniform sampler2D overlayTex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(overlayTex, uv);
}
