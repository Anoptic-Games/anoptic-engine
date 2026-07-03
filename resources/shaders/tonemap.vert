#version 450

// Vertex-bufferless fullscreen triangle. Three vertices (gl_VertexIndex 0,1,2) expand to a
// triangle that fully covers the [-1,1] clip rectangle; uv is the matching [0,1] texture
// coordinate. uv.y = 0 maps to clip y = -1 (Vulkan's top), which is texture row 0 (the top of
// the HDR resolve as the geometry pass rasterized it), so no vertical flip is needed.
layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
