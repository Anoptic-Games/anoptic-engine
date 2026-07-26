#version 450

// Fullscreen triangle from gl_VertexIndex 0,1,2. No VBO.
// uv [0,1] <-> clip [-1,1]. uv.y=0 is Vulkan top (tex row 0). No Y flip.
layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
