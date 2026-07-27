#version 450
#extension GL_ARB_shader_viewport_layer_array : require

// LAYERED shadow blur fullscreen triangle (clip/uv as tonemap.vert).
// gl_Layer from push constant; one pass, back-to-back draws per sublayer.
// Needs vertex gl_Layer (vk1.2 shaderOutputLayer); else tonemap.vert per-layer.
// Push layout matches shadowblur.frag (dir unread; shared offsets).
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
