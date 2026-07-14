# nixfuckery

tl;dr: Nix is foobar on the system you're reading it on. Your job is to fix it.

Note from Claude Fable (matei3d's session, macOS, 2026-07-12) to Claude Fable on cris's machine. 

Subject: the nix Linux renderer path — what works, what was wrong, what is now fixed, and what you do next. Branch `nix-anygpu`, based on `feature-crashreport` at c9e99f2.

## Working today

- macOS: full pipeline. Pure `nix build` renders via MoltenVK, the dev shell drives `build.sh`, benchmarks ran on it today.
- WSL→Windows: `nix build .#release-wsl` cross-compiles the Windows renderer exe via MinGW ucrt64; `nix run -- 3` runs headless in-guest.
- Linux CI: `tests-headless|asan|tsan` run hermetically; `tests-full` runs real Vulkan device tests on lavapipe with zero host deps.
- Toolchain: clang 22.1.8 + lld everywhere, submodule pins gated at `nix run`, one lockfile. No drift.
- Foreign-distro Linux renderer was the one gap. Root cause confirmed at loader level, bridged on this branch.

## What was wrong

cris's `VK_LOADER_DEBUG=all` run settled it. The loader finds all nine host ICD manifests in `/usr/share/vulkan/icd.d`, then every dlopen fails: host manifests use bare sonames (`libGLX_nvidia.so.0`) and nix's `ld.so` reads neither `/etc/ld.so.cache` nor `/usr/lib`, so no driver file is ever located → `VK_ERROR_INCOMPATIBLE_DRIVER` (-9). Second wall behind it: transitive deps of host driver DSOs. Structural fact under both: proprietary NVIDIA userspace must match the host kernel module, so it can never be a build input. The fix injects drivers at runtime.

Dead theories: manifest masking (all found), glibc symbol direction (the pin's 2.42 is newer than the host's), the shell wrapper (standard makeWrapper, injects VK_LAYER_PATH, host layers enumerate fine).

## Fixed on this branch

- `nix run` auto-bridge: `/run/opengl-driver` present → NixOS, untouched. Absent → store mesa ICDs staged via `VK_ADD_DRIVER_FILES`; `/proc/driver/nvidia/version` present → exec through `nixglhost`, which harvests the host NVIDIA userspace at runtime, kernel-module-matched.
- `nix run .#nvidia` / `.#nvidia-debug`: the pure store artifact under nixglhost. Independent of `build.sh`, and the pure artifact already passes GLFW on this machine. Shortest route to pixels.
- `nix build .#anygpu`: mesa ICDs baked into a pure variant. AMD/Intel/NVK hardware on any distro, lavapipe fallback.

Verified: evals on x86_64-linux and aarch64-darwin, drv wrapper contents, mesa manifest names against the pinned 26.1.3 NAR, scripts shellcheck-clean. Runtime validation is your job.

## Runbook

1. `cat /proc/driver/nvidia/version`.
2. Zero-patch smoke test: `nix shell github:numtide/nix-gl-host -c nixglhost ./result/bin/anopticengine`. Sponza on the GeForce validates the whole design.
3. `nix run .#nvidia`.
4. `rm -rf build && nix run`. The wipe matters: the glfwInit failure is unexplained, env is ruled out (X11, `DISPLAY=:0`, no platform hint at `window.c:173`), prime suspect is a stale CMakeCache (`build.sh` only resets on generator or source-root mismatch).
5. glfwInit still failing after a clean build → add a temporary `glfwGetError(&desc)` log at the `window.c:173` FATAL.
6. `VK_LOADER_DEBUG=all` at every step. Success: the nixglhost manifest in the driver scan, a discrete GPU in the device table.

## Escape hatch

`nixglhost` glibc version errors mean the host glibc is newer than the pin's 2.42. Then: `NIXPKGS_ALLOW_UNFREE=1 nix run --impure github:nix-community/nixGL#nixVulkanNvidia -- ./result/bin/anopticengine` — nixGL fetches NVIDIA userspace at the detected version. Report which one works.

## Do not chase

- Store runs drop file logs: exe-dir `logs/` on a read-only prefix (`src/filesystem/filesystem.c:40`). Engine bug, fix pending, stderr works.
- Shipping to players is a separate old-glibc / Steam Linux Runtime pipeline. Nix is the dev, CI, and validation substrate.

Run the matrix, report verbatim. One gap, bridged. Get it working.
