#!/usr/bin/env bash
# Compile the engine's GLSL shaders to SPIR-V.
# Discovers glslc on its own ($VULKAN_SDK/bin, then PATH)
set -euo pipefail

cd "$(dirname "$0")"

if [ -n "${VULKAN_SDK:-}" ] && [ -x "$VULKAN_SDK/bin/glslc" ]; then
    glslc="$VULKAN_SDK/bin/glslc"
else
    glslc="$(command -v glslc || true)"
fi

if [ -z "$glslc" ]; then
    echo "error: glslc not found. Install the Vulkan SDK and source its setup-env.sh" >&2
    echo "       (sets VULKAN_SDK + PATH), or put glslc on PATH." >&2
    exit 1
fi

echo "Using glslc: $glslc"
for shader in flat.vert flat.frag cull.comp update.comp; do
    echo "  $shader -> $shader.spv"
    "$glslc" "$shader" -o "$shader.spv"
done
echo "Shaders compiled."
