#!/usr/bin/env bash
# Compile the engine's GLSL shaders to SPIR-V with Debug Symbols.
# Discovers glslangValidator on its own ($VULKAN_SDK/bin, then PATH)
set -euo pipefail

cd "$(dirname "$0")"

# Discover glslangValidator instead of glslc
if [ -n "${VULKAN_SDK:-}" ] && [ -x "$VULKAN_SDK/bin/glslangValidator" ]; then
    glslang="$VULKAN_SDK/bin/glslangValidator"
else
    glslang="$(command -v glslangValidator || true)"
fi

if [ -z "$glslang" ]; then
    echo "error: glslangValidator not found. Install the Vulkan SDK and source its setup-env.sh" >&2
    echo "       (sets VULKAN_SDK + PATH), or put glslangValidator on PATH." >&2
    exit 1
fi

echo "Using glslangValidator: $glslang"

# Define the shaders to compile
shaders=(
    flat.mesh flat.vert flat.frag transmission.frag additive.frag 
    cull.comp tpsort.comp update.comp scatter.comp lightcull.comp 
    shadowsetup.comp hiz.comp shadow_depth.frag shadowblur.frag 
    tonemap.vert tonemap.frag
)

for shader in "${shaders[@]}"; do
    echo "  $shader -> $shader.spv (Debug)"
    
    # -V: Target Vulkan environment (defaults to standard SPIR-V generation)
    # --target-env vulkan1.2: Matches your original Vulkan API target version
    # -g: Generates the debug symbols that NVIDIA Nsight can read properly
    "$glslang" -V --target-env vulkan1.2 -gVS -o "$shader.spv" "$shader"
done

echo "Debug shaders compiled successfully."
