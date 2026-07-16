# NIX_LINUX — native Linux through Nix

Branch `nix-anygpu`. Validation machine: Ubuntu 24.04 (glibc 2.39, X11/Cinnamon,
`DISPLAY=:0`), NVIDIA open kernel module 590.48.01, RTX 3090 + RTX 3060, Nix 2.18.1,
no `/run/opengl-driver` (foreign distro). This closes the runbook in
`docs/nix/nixfuckery.md`: every step ran, the leftover failure was **not** a stale
CMakeCache, and two engine bugs plus one packaging hole got fixed on the way.

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

Loader-level evidence (from `VK_LOADER_DEBUG=all` runs): the host `/usr/share/vulkan/icd.d`
manifests still fail to dlopen under the Nix loader, as originally diagnosed. The
engine renders through
`~/.cache/nix-gl-host/<hash>/glx/libGLX_nvidia.so.0` — the kernel-module-matched userspace
harvested at launch. Both discrete GPUs enumerate; the engine picks the 3090.

## Root causes

### 1. Host ICDs unloadable under the Nix loader (pre-diagnosed, confirmed at runtime)

Host ICD manifests reference bare sonames (`libGLX_nvidia.so.0`); Nix's `ld.so` reads
neither `/etc/ld.so.cache` nor `/usr/lib`, so every host driver dlopen fails →
`VK_ERROR_INCOMPATIBLE_DRIVER`. Proprietary NVIDIA userspace must match the host kernel
module, so it can never be a build input. The bridge added earlier on this branch
(runtime harvest via `nixglhost`, mesa ICDs via `VK_ADD_DRIVER_FILES`) is the right
design and now has runtime confirmation on hardware.

### 2. `glfwInit()` failure in dev-shell builds — the real cause (not a stale CMakeCache)

`docs/nix/nixfuckery.md` step 4 blamed a stale CMakeCache. Disproven: a from-scratch
`build/` rebuild failed the same way. The `glfwGetError()` diagnostic at
`src/vulkan_backend/instance/window.c:173` reported:

```
FATAL Failed to initialize GLFW! (0x00010008: X11: Failed to load Xlib)
```

Mechanism:

- GLFW 3.4 links **none** of its platform libraries; it `dlopen()`s them by bare soname
  (`libX11.so.6`, `libwayland-client.so.0`, …).
- nixpkgs' ld-wrapper only adds `-rpath` entries for `-L` directories whose libraries are
  actually `-l`-linked. dlopen-only libraries therefore never enter the binary's RUNPATH.
- Nix's `ld.so` has no cache/default-path fallback, so the dlopen has nowhere to look.
  `readelf -d` on the failing binary showed a RUNPATH containing only vulkan-loader,
  glibc, and gcc libs — no X11, no Wayland.

Same reason the **pure** artifact needed the branch's `postFixup` patchelf
(`--shrink-rpath` prunes non-`DT_NEEDED` entries): the flake comment claiming dev-shell
binaries "keep the RUNPATH unshrunk" was wrong — they never had those entries at all.
One mechanism, three victims: installed artifact (fixed earlier via patchelf), dev-shell
binaries, and sandboxed test executables (both fixed now, below).

**Fix:** inject the dlopen'd library paths at link time via `NIX_LDFLAGS = "-rpath …"`
in both the Linux dev shell (covers `build.sh` output, which then also runs outside the
shell, e.g. directly under `nixglhost`) and `mkEngine` (covers the `checkPhase` test
executables, which run before `--shrink-rpath`). The ld-wrapper appends its own flags
after the env-provided ones, so the addition composes with normal linking.

### 3. `tests-full` sandbox failures — three stacked causes

The macOS-side claim that `tests-full` "runs real Vulkan device tests on lavapipe with
zero host deps" had only been verified at eval level. At runtime, four tests
(`anotest_vk_lifecycle`, `_compliance_layers`, `_memory`, `_sync` — the ones that call
`initVulkan()`) failed for three independent reasons:

1. **No display server in the build sandbox.** `glfwInit()` requires X11 or Wayland even
   with lavapipe. Fix: the renderer test suite's `ctest` now runs under `xvfb-run`
   (nix-provided Xvfb; still zero host dependencies).
2. **The same RUNPATH hole as §2** — test executables couldn't dlopen Xlib even under
   Xvfb ("Failed to detect any supported platform"). Fixed by the same `NIX_LDFLAGS`.
3. **Lavapipe cannot run this renderer, structurally.** With GLFW and X11 finally
   working, device selection rejected llvmpipe. `vulkaninfo` against the pinned mesa:
   `framebufferColorSampleCounts`/`DepthSampleCounts` support 4x, but
   `framebufferIntegerColorSampleCounts = 1x only`. The renderer draws its R32_UINT
   picking-id attachment at the same MSAA count as color
   (`src/vulkan_backend/instance/attachments.c`), and has no 1x path, so the
   suitability check (`device.c`, "supports only 1x MSAA across the engine's attachment
   set") correctly refuses the device. This is a hardware-capability boundary, not a
   packaging bug: **no amount of Nix plumbing makes lavapipe render this engine** until
   a 1x-MSAA render path exists.

**Fix for (3):** honest skips. `initVulkan()` now sets `g_AnoVkNoSuitableGpu` when it
fails at physical-device selection; the four device tests return exit code 77 in that
case and their CTest entries carry `SKIP_RETURN_CODE 77`. The sandbox suite reports
16 pass + 4 skip = green, still exercises GLFW init, instance creation, and device
enumeration on every run, and will automatically run the full tests the day the device
can support the renderer (or on any dev machine with a real GPU). On such machines an
`initVulkan()` failure still **fails** the tests — the skip fires only on the
no-suitable-device path.

Supporting change: the `tests-full` derivation exports `VK_LAYER_PATH`, since
`anotest_vk_compliance_layers`/`_sync` assert that validation layers intercept an
intentional error and the sandbox has no layer discovery of its own.

### 4. Engine bug: CPU-class Vulkan devices were never selectable

`pickPhysicalDevice()` ranked only `DISCRETE_GPU` and `INTEGRATED_GPU` candidates. A
suitable device of any other type (`CPU` — lavapipe, `VIRTUAL_GPU` — VMs) could pass
`isDeviceSuitable()` and still hard-fail init with "Failed to find a suitable GPU!".
That silently contradicted the `.#anygpu` target's lavapipe-fallback intent and would
break VM guests. Fixed with a third, last-resort ranking bucket (same mesh-then-memory
ordering) plus a WARN when it engages.

### 5. Engine bug: init-failure teardown crashed the process

When `initVulkan()` fails early (e.g. no suitable device), `unInitVulkan()` →
`cleanupVulkan()` called `vkDeviceWaitIdle(ctx->device)` with `device == VK_NULL_HANDLE`
— a loader crash (SIGABRT with a blackbox record, plus a loader
`VUID-vkDeviceWaitIdle-device-parameter` error). Every "no GPU" situation became a
crash, which is why the sandbox failures read as "Subprocess aborted" and hid the real
message. Fixed by guarding the wait (the rest of the teardown was already handle-guarded);
the no-device path now exits cleanly.

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

Not changed: `build.sh`, engine CMake, any renderer code paths — the render libs stay
dlopen'd (no new `DT_NEEDED`), so non-Nix builds and Wayland-only end-user systems are
unaffected.

## Known limitations and non-goals

- **`.#anygpu` + lavapipe**: the bundled mesa ICDs give real AMD/Intel/NVK hardware a
  working driver on any distro (untested here — this box is NVIDIA-only), and lavapipe
  now *enumerates and gets selected* — but the renderer itself refuses it for the
  integer-MSAA reason above. Lavapipe remains an init/enumeration substrate, not a
  pixel substrate, until a 1x-MSAA path exists.
- **Store runs drop file logs** (`logs/` beside a read-only store binary,
  `src/filesystem/filesystem.c:40`). Pre-existing engine issue, explicitly out of scope
  per `docs/nix/nixfuckery.md`; stderr logging works.
- **"Not responding" dialogs** during the ~7 s blocking asset load are expected and
  harmless; the window recovers once frames flow.
- The escape hatch (`nixGL`) was not needed: host glibc 2.39 < the pin's 2.42, so
  `nixglhost` works as designed.
- The viking room renders untextured (white) with the public `assets-free` pack; that is
  an asset-pack matter, not a Linux-target issue.
- A `build.stale-preclean/` directory (the pre-fix build tree, moved aside during the
  clean-rebuild experiment) is left at the repo root and can be deleted.

## Runbook for the next machine

```sh
cat /proc/driver/nvidia/version         # NVIDIA kernel module present?
nix run .#nvidia                        # pure Release on the host NVIDIA driver
nix run                                 # impure dev build + auto bridge (mesa/NVIDIA)
nix build .#tests-headless .#tests-asan .#tests-tsan .#tests-full --no-link -L
VK_LOADER_DEBUG=all nix run .#nvidia    # loader-level proof if anything misbehaves
```

Success signature in the loader log: a `nix-gl-host` cache path as the driver for the
selected device, and your discrete GPU in the engine's device table.

If your config is cursed and that doesn't work, just use Nix okay? (You already are.)
