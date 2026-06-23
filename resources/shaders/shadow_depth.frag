#version 450

// Depth-only shadow pass: the pipeline has no color attachment, so this writes nothing — depth is
// produced by the rasterizer from the geometry stage's gl_Position. Present (rather than a
// no-fragment pipeline) to keep depth-bias + early-Z behaviour uniform across drivers/MoltenVK.
void main() {}
