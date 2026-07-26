#!/usr/bin/env bash
# Refresh committed SPIR-V fallbacks from every GLSL source here.
# Hand-run after shader edits, then commit the .spv. Normal builds use CMake.
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
# Glob shader sources. Skip blocked stubs. Shared .glsl never matches.
for shader in *.mesh *.vert *.frag *.comp *.task; do
    case "$shader" in
        skinned.mesh|pose.comp|decal.vert|decal.frag) continue ;;
    esac
    echo "  $shader -> $shader.spv"
    "$glslc" --target-env=vulkan1.2 "$shader" -o "$shader.spv"
done

# Named variants (must mirror CMake): depth-only, task-cull flat, resolved-depth Hi-Z.
echo "  flat.mesh -DANO_DEPTH_ONLY -> flat_depth.mesh.spv"
"$glslc" --target-env=vulkan1.2 -DANO_DEPTH_ONLY flat.mesh -o flat_depth.mesh.spv
echo "  flat.vert -DANO_DEPTH_ONLY -> flat_depth.vert.spv"
"$glslc" --target-env=vulkan1.2 -DANO_DEPTH_ONLY flat.vert -o flat_depth.vert.spv
echo "  flat.mesh -DANO_TASK_CULL -> flat_task.mesh.spv"
"$glslc" --target-env=vulkan1.2 -DANO_TASK_CULL flat.mesh -o flat_task.mesh.spv
echo "  flat.mesh -DANO_TASK_CULL -DANO_DEPTH_ONLY -> flat_depth_task.mesh.spv"
"$glslc" --target-env=vulkan1.2 -DANO_TASK_CULL -DANO_DEPTH_ONLY flat.mesh -o flat_depth_task.mesh.spv
echo "  hiz.comp -DRESOLVED_DEPTH -> hiz_resolve.comp.spv"
"$glslc" --target-env=vulkan1.2 -DRESOLVED_DEPTH hiz.comp -o hiz_resolve.comp.spv
echo "Shaders compiled."
