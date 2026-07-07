#!/usr/bin/env bash
# Compile the engine's GLSL shaders to SPIR-V with Debug Symbols.
# Discovers glslangValidator in $VULKAN_SDK/bin, then PATH.
set -euo pipefail

cd "$(dirname "$0")"

# Discover glslangValidator
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

# Discover shaders
for shader in *.mesh *.vert *.frag *.comp *.task; do
    case "$shader" in
        skinned.mesh|pose.comp|decal.vert|decal.frag) continue ;;
    esac
    echo "  $shader -> $shader.spv (Debug)"
    "$glslang" -V --target-env vulkan1.2 -gVS -o "$shader.spv" "$shader"
done

# Compile the missing debug variants
echo "  flat.mesh -DANO_DEPTH_ONLY -> flat_depth.mesh.spv (Debug)"
"$glslang" -V --target-env vulkan1.2 -gVS -DANO_DEPTH_ONLY -o flat_depth.mesh.spv flat.mesh

echo "  flat.vert -DANO_DEPTH_ONLY -> flat_depth.vert.spv (Debug)"
"$glslang" -V --target-env vulkan1.2 -gVS -DANO_DEPTH_ONLY -o flat_depth.vert.spv flat.vert

echo "  flat.mesh -DANO_TASK_CULL -> flat_task.mesh.spv (Debug)"
"$glslang" -V --target-env vulkan1.2 -gVS -DANO_TASK_CULL -o flat_task.mesh.spv flat.mesh

echo "  flat.mesh -DANO_TASK_CULL -DANO_DEPTH_ONLY -> flat_depth_task.mesh.spv (Debug)"
"$glslang" -V --target-env vulkan1.2 -gVS -DANO_TASK_CULL -DANO_DEPTH_ONLY -o flat_depth_task.mesh.spv flat.mesh

echo "  hiz.comp -DRESOLVED_DEPTH -> hiz_resolve.comp.spv (Debug)"
"$glslang" -V --target-env vulkan1.2 -gVS -DRESOLVED_DEPTH -o hiz_resolve.comp.spv hiz.comp

echo "Debug shaders compiled successfully."
