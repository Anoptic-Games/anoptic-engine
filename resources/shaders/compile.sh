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
# (a hand-maintained list here once silently omitted flat.vert). Blocked-out stubs
# that don't compile standalone yet are skipped explicitly; shared .glsl includes
# never match the glob.
for shader in *.mesh *.vert *.frag *.comp *.task; do
    case "$shader" in
        skinned.mesh|pose.comp|decal.vert|decal.frag) continue ;;
    esac
    echo "  $shader -> $shader.spv"
    "$glslc" --target-env=vulkan1.2 "$shader" -o "$shader.spv"
done

# Variant compiles the renderer loads by name (must mirror the CMake shader rules):
# depth-only + task-launched flat geometry, and the resolved-depth Hi-Z reduce.
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
