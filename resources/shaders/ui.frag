#version 450

// Input from the multisampled image
layout(location = 0) in vec4 color;

// Output to the final render target
layout(location = 0) out vec4 finalColor;

void main() {
    // Simply output the color from the multisampled image
    finalColor = color;
}
