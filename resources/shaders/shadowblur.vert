#version 450
#extension GL_ARB_shader_viewport_layer_array : require

// Fullscreen triangle for the LAYERED shadow blur: same clip/uv expansion as tonemap.vert, plus
// gl_Layer routed from the push constant, so one render pass (the atlas/temp array bound as a
// layered color attachment) blurs every active sublayer with back-to-back draws — no per-layer
// passes or barriers. Requires vertex-stage gl_Layer (vk1.2 shaderOutputLayer); devices without it
// bind tonemap.vert instead and render per-layer views. Block layout must match shadowblur.frag's
// push_constant block (dir is unread here but keeps the offsets shared).
layout(push_constant) uniform Push {
    vec2 dir;
    int  layer;
    int  pad;
} pc;

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    gl_Layer = pc.layer;
}
