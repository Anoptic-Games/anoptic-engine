# NIX_LINUX - native Linux through Nix

Branch `nix-anygpu`. Validation machine: Ubuntu 24.04 (glibc 2.39, X11/Cinnamon, `DISPLAY=:0`), NVIDIA open kernel module 590.48.01, RTX 3090 + RTX 3060, Nix 2.18.1, no `/run/opengl-driver` (foreign distro). Closes `docs/nix/nixfuckery.md`: every step ran; leftover failure was **not** a stale CMakeCache; two engine bugs + one packaging hole fixed on the way.

## Verified end state

| Path | Result |
|---|---|
| `nix shell github:numtide/nix-gl-host -c nixglhost ./result/bin/anopticengine` (pure Debug artifact, zero-patch smoke) | Sponza renders on the RTX 3090; validation layers on; loader uses the nixglhost-harvested driver |
| `nix run .#nvidia` (pure Release artifact under pinned nixglhost) | Sponza renders, ~230 fps, correct GPU auto-selection (3090 over 3060) |
| `nix run` (impure dev-shell `build.sh` + auto host-GPU bridge) | Sponza renders after the RUNPATH fix below |
| `nix build .#tests-headless` | 14/14 pass |
| `nix build .#tests-asan` | 13/13 pass |
| `nix build .#tests-tsan` | 13/13 pass |
| `nix build .#tests-full` | 20/20 green: 16 pass, 4 Vulkan device tests **skip by design** (see lavapipe section) |
| Scene content | Spinning viking room and both transmissive candle holders verified on screen via staggered window captures (Sponza temporarily commented out for the isolation shots, then restored) |

Loader evidence (`VK_LOADER_DEBUG=all`): host `/usr/share/vulkan/icd.d` manifests still fail to dlopen under the Nix loader. Engine renders through `~/.cache/nix-gl-host/<hash>/glx/libGLX_nvidia.so.0` (kernel-module-matched userspace harvested at launch). Both discrete GPUs enumerate; engine picks the 3090.

## Root causes

### 1. Host ICDs unloadable under the Nix loader (pre-diagnosed, confirmed at runtime)

Host ICD manifests reference bare sonames (`libGLX_nvidia.so.0`). Nix `ld.so` reads neither `/etc/ld.so.cache` nor `/usr/lib` → every host driver dlopen fails → `VK_ERROR_INCOMPATIBLE_DRIVER`. Proprietary NVIDIA userspace must match the host kernel module (never a build input). Bridge: runtime harvest via `nixglhost`, mesa ICDs via `VK_ADD_DRIVER_FILES`. Confirmed on hardware.

### 2. `glfwInit()` failure in dev-shell builds - real cause (not a stale CMakeCache)

`docs/nix/nixfuckery.md` step 4 blamed a stale CMakeCache. Disproven: from-scratch `build/` rebuild failed the same way. `glfwGetError()` at `src/vulkan_backend/instance/window.c:173`:

```
FATAL Failed to initialize GLFW! (0x00010008: X11: Failed to load Xlib)
```

Mechanism:

- GLFW 3.4 links **none** of its platform libraries; it `dlopen()`s them by bare soname (`libX11.so.6`, `libwayland-client.so.0`, …).
- nixpkgs' ld-wrapper only adds `-rpath` for `-L` dirs whose libs are actually `-l`-linked. dlopen-only libs never enter RUNPATH.
- Nix `ld.so` has no cache/default-path fallback. `readelf -d` on the failing binary: RUNPATH had vulkan-loader, glibc, gcc libs only - no X11, no Wayland.

Same hole the **pure** artifact hit (branch `postFixup` patchelf; `--shrink-rpath` prunes non-`DT_NEEDED`). Flake comment that dev-shell binaries "keep the RUNPATH unshrunk" was wrong: those entries were never present. One mechanism, three victims: installed artifact (fixed earlier via patchelf), dev-shell binaries, sandboxed test executables (both fixed below).

**Fix:** `NIX_LDFLAGS = "-rpath …"` for the dlopen'd libs in the Linux dev shell (`build.sh` output, also runnable under `nixglhost` outside the shell) and `mkEngine` (`checkPhase` tests, before `--shrink-rpath`). ld-wrapper appends after env flags, so it composes with normal linking.

### 3. `tests-full` sandbox failures - three stacked causes

macOS-side claim that `tests-full` "runs real Vulkan device tests on lavapipe with zero host deps" was eval-only. At runtime, four tests (`anotest_vk_lifecycle`, `_compliance_layers`, `_memory`, `_sync` - the ones that call `initVulkan()`) failed for three reasons:

1. **No display server in the build sandbox.** `glfwInit()` needs X11 or Wayland even with lavapipe. Fix: renderer `ctest` under `xvfb-run` (nix Xvfb; still zero host deps).
2. **Same RUNPATH hole as §2** - test executables couldn't dlopen Xlib under Xvfb ("Failed to detect any supported platform"). Same `NIX_LDFLAGS` fix.
3. **Lavapipe cannot run this renderer.** With GLFW/X11 working, device selection rejected llvmpipe. Pinned mesa `vulkaninfo`: `framebufferColorSampleCounts`/`DepthSampleCounts` support 4x, but `framebufferIntegerColorSampleCounts = 1x only`. Renderer draws R32_UINT picking-id at the same MSAA as color (`src/vulkan_backend/instance/attachments.c`), no 1x path, so suitability (`device.c`, "supports only 1x MSAA across the engine's attachment set") correctly refuses. Hardware-capability boundary: **no Nix plumbing makes lavapipe render this engine** until a 1x-MSAA path exists.

**Fix for (3):** honest skips. `initVulkan()` sets `g_AnoVkNoSuitableGpu` on physical-device selection failure; the four device tests return 77; CTest entries carry `SKIP_RETURN_CODE 77`. Suite: 16 pass + 4 skip = green; still exercises GLFW init, instance creation, and device enumeration; full tests run when the device can support the renderer (or on a real-GPU machine). On such machines `initVulkan()` failure still **fails** - skip only on the no-suitable-device path.

Supporting: `tests-full` exports `VK_LAYER_PATH` (`anotest_vk_compliance_layers`/`_sync` assert validation layers intercept an intentional error; sandbox has no layer discovery).

### 4. Engine bug: CPU-class Vulkan devices were never selectable

`pickPhysicalDevice()` ranked only `DISCRETE_GPU` and `INTEGRATED_GPU`. A suitable `CPU` (lavapipe) or `VIRTUAL_GPU` (VMs) could pass `isDeviceSuitable()` and still hard-fail with "Failed to find a suitable GPU!". Contradicted `.#anygpu` lavapipe-fallback intent; would break VM guests. Fixed: third last-resort ranking bucket (same mesh-then-memory ordering) + WARN when it engages.

### 5. Engine bug: init-failure teardown crashed the process

Early `initVulkan()` failure → `unInitVulkan()` → `cleanupVulkan()` called `vkDeviceWaitIdle(ctx->device)` with `device == VK_NULL_HANDLE` - loader crash (SIGABRT + blackbox, plus `VUID-vkDeviceWaitIdle-device-parameter`). Every "no GPU" path became "Subprocess aborted" and hid the real message. Fixed: guard the wait (rest of teardown was already handle-guarded); no-device path exits cleanly.

## Changes on disk

| File | Change |
|---|---|
| `flake.nix` | `xvfb-run` around `ctest` for Linux renderer test suites; `VK_LAYER_PATH` in the `tests-full` env; `NIX_LDFLAGS -rpath` for the dlopen'd render libs in `mkEngine` and the Linux dev shell (list factored as `shellRenderLibs`); corrected the postFixup comment |
| `src/vulkan_backend/instance/window.c` | `glfwGetError()` code + description in the `glfwInit` FATAL (kept: it is the diagnostic that cracked §2) |
| `src/vulkan_backend/instance/device.c` | fallback ranking bucket for suitable non-discrete/non-integrated devices |
| `src/vulkan_backend/instance/cleanup.c` | guard `vkDeviceWaitIdle` against `VK_NULL_HANDLE` device |
| `src/vulkan_backend/vulkanMaster.c` | `g_AnoVkNoSuitableGpu` flag, set on physical-device-selection failure |
| `src/vulkan_backend/instance/instance.c` | (pre-existing local change kept) log the actual `VkResult` from `vkCreateInstance` and return it instead of a hardcoded code |
| `tests/anotest_vk_{lifecycle,compliance_layers,memory,sync}.c` | return 77 (skip) when init failed for lack of a suitable device |
| `tests/CMakeLists.txt` | `SKIP_RETURN_CODE 77` on those four tests |

Not changed: `build.sh`, engine CMake, any renderer code paths - render libs stay dlopen'd (no new `DT_NEEDED`); non-Nix builds and Wayland-only end-user systems unaffected.

## Known limitations and non-goals

- **`.#anygpu` + lavapipe**: bundled mesa ICDs give real AMD/Intel/NVK hardware a working driver on any distro (untested here - this box is NVIDIA-only). Lavapipe now enumerates and gets selected, but the renderer refuses it (integer-MSAA). Lavapipe remains init/enumeration substrate, not pixel substrate, until a 1x-MSAA path exists.
- **Store runs drop file logs** (`logs/` beside a read-only store binary, `src/filesystem/filesystem.c:40`). Pre-existing; out of scope per `docs/nix/nixfuckery.md`. stderr logging works.
- **"Not responding" dialogs** during the ~7 s blocking asset load are expected and harmless; window recovers once frames flow.
- Escape hatch (`nixGL`) not needed: host glibc 2.39 < pin's 2.42, so `nixglhost` works as designed.
- Viking room renders untextured (white) with public `assets-free` pack; asset-pack matter, not Linux-target.
- `build.stale-preclean/` (pre-fix build tree, moved aside during clean-rebuild) left at repo root; deletable.

## Runbook for the next machine

```sh
cat /proc/driver/nvidia/version         # NVIDIA kernel module present?
nix run .#nvidia                        # pure Release on the host NVIDIA driver
nix run                                 # impure dev build + auto bridge (mesa/NVIDIA)
nix build .#tests-headless .#tests-asan .#tests-tsan .#tests-full --no-link -L
VK_LOADER_DEBUG=all nix run .#nvidia    # loader-level proof if anything misbehaves
```

Success signature in the loader log: a `nix-gl-host` cache path as the driver for the selected device, and your discrete GPU in the engine's device table.

If your config is cursed and that doesn't work, just use Nix okay? (You already are.)
