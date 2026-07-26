#version 450

// HDR -> swapchain encode. Sample HDR resolve + filmic tonemap.
// Output LINEAR; swapchain _SRGB applies sRGB on store.
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// ACES filmic approx (Narkowicz 2015). HDR radiance -> [0,1].
vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(hdrColor, uv).rgb;
    outColor = vec4(acesFilmic(hdr), 1.0);
}
