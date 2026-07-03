#!/usr/bin/env bash
# Refresh the COMMITTED SPIR-V fallbacks (used when a build has no glslc) from every
# GLSL source in this directory. Normal builds compile shaders into the build tree via
# CMake; run this by hand after editing a shader, then commit the regenerated .spv.
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
# Glob every shader source so this list can never desync from what exists on disk
# (a hand-maintained list here once silently omitted flat.vert).
for shader in *.mesh *.vert *.frag *.comp; do
    echo "  $shader -> $shader.spv"
    "$glslc" --target-env=vulkan1.2 "$shader" -o "$shader.spv"
done
echo "Shaders compiled."
