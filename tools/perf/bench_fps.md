# Performance harness & methodology

How we measure anopticengine frame throughput reproducibly. The engine-side instrumentation and the interpretation rules here are platform-agnostic; the *driver* that positions the window and reads the log is per-platform. Windows (`bench_fps_win64.py`) and Linux (`bench_fps_linux.py`) drivers live in this directory; a macOS driver is TODO and must implement the same contract described below.

## Engine-side contract (shared across all targets)

The engine emits two lines to `anoptic.log` (written next to the executable), both release-visible, defined in `src/vulkan_backend/frame/profiling.c`:

- `[frame] <fps> fps <ms> ms wall` — **wall-clock throughput**, logged once per real second by `ano_frame_mark()`, called on the presented-frame path in `drawFrame`. Timestamp-independent: it keeps reporting even when constant swapchain recreation starves the GPU-timestamp reads. This is the number that reflects the whole pipeline (CPU record + submit + present + GPU).
- `[profile mode=<mode>] ... total=<ms> ... (frusta N/42) ... | VRAM MiB: ... swap=<MiB> ...` — **GPU pass time**, the sum of per-pass GPU timestamps (upload+compute+shadow+lighting+composite), printed every `ANO_PROFILE_PRINT_INTERVAL` (120) rendered frames. GPU-timestamp-gated, so it goes silent under a resize storm. `frusta` is shadow-frustum renders/frame; `swap` is swapchain-allocator resident VRAM.

`total` is GPU-pass time only, not wall-clock. They answer different questions; report both.

## Reading the numbers

- `GPUcap = 1000 / total_ms` — the fps the GPU passes alone could sustain.
- `wall/cap = wall_fps / GPUcap` — the bound indicator. Near 1.0 → **GPU-bound**. Well below 1.0 (we treat <0.9 as the line) → **CPU/present-bound**: the GPU is idle part of every frame and the ceiling is the main render thread or present.
- A CPU/present ceiling is not fixed — cutting per-frame CPU work (e.g. fewer shadow frusta to record) raises it. It also is not vsync unless the fps sits on a refresh multiple; uncapped values off the refresh grid mean present/CPU overhead, not vsync.
- **Sanity-check the render resolution against `swap` VRAM, never the window label.** swap scales ~linearly with pixel count; if you think you set 4K but swap matches your 1080p run ×~1, you actually rendered at 1080p. This caught a DPI-mislabel once already.

## What any driver MUST do (portable requirements)

These are the non-negotiables; every platform driver re-implements them with its own primitives:

- **Physical-pixel sizing.** Be DPI-aware (Windows: per-monitor-v2; the compositor equivalents elsewhere) or monitor rects come back in logical/scaled units and you mislabel resolution.
- **Force AND verify foreground.** A background or occluded window mismeasures the GPU passes (the lighting pass alone inflated ~5× in one comparison). Bring the window to the true foreground and confirm it (Windows: `GetForegroundWindow() == hwnd`); flag/skip any data point that isn't front. This is the single biggest correctness trap.
- **Fresh process per data point.** Relaunch for each resolution/config; don't resize-and-reuse (carries swapchain/cache state).
- **Release the log handle.** After killing the engine, retry-wait before deleting `anoptic.log` for the next run — the exiting process still holds it briefly.
- **Drop warmup.** Discard the first couple `[frame]` seconds and the first several profile lines before taking medians.

## Runtime knobs (engine `getenv`)

Pass via the child process environment. Perf-relevant:

- `ANO_SHADOW_BUDGET=N` — cap content-dirty shadow re-renders/frame (0 = unlimited; matrix-dirty renders exempt, so `frusta` floors above N). N=2 on the demo scene cut GPU `total` ~30% and lifted fps ~25–30%.
- `ANO_MSAA=N`, `ANO_HIZ_ON`, `ANO_DEVICE=idx` — MSAA level, Hi-Z toggle, GPU selection.
- `ANO_SHADOW_CACHE_FREEZE`, `ANO_FORCE_NO_SHADOW_CACHE`, `ANO_FORCE_NO_SWEPT` — shadow cache behavior.
- `ANO_FORCE_NO_{ASYNC_HIZ,ASYNC_LC,ASYNC_TEXT,DEPTH_RESOLVE,MESH_SHADER,SHADER_OUTPUT_LAYER,TASK,TEXT,TEXT_WORLD,UI,UI_TILES}` — feature-disable toggles for A/B isolation.
- `ANO_UI_DEMO`, `ANO_TEXT_DEMO`, `ANO_UI_OPAQUE`, `ANO_TEXT_OPAQUE` — demo-content / blend toggles.

## Drivers

- **Windows:** `tools/perf/bench_fps_win64.py` (win32 + pywin32). Modes: default resolution sweep, `--res WxH`, `--no-menu` (static HUD), `--churn` (resize storm), `--env KEY=VAL`. Prints a table with a `front` column — treat any `BG!!` row as invalid.
- **Linux:** `tools/perf/bench_fps_linux.py` (X11/Xwayland). Same modes/flags as the Windows driver. Window discovery by PID (`xdotool search --pid`, `_NET_WM_PID`, 'Vulkan'-title fallback). Forced and verified foreground via `xdotool windowactivate --sync` confirmed against `getactivewindow`. Borderless render surface via `_MOTIF_WM_HINTS` strip + `windowsize --sync`. Menu key via XTEST (`xdotool key m`). X11 hands out physical pixels and the driver commands exact pixel sizes. `swap=` stays the resolution authority. Tools come from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`. Drives X11/Xwayland clients only, never native-Wayland GLFW windows. Its `parse_stream()` also replays a captured `anoptic.log` offline with no X server.
- **macOS (TODO `bench_fps_macos.py`):** Cocoa. Front via `NSRunningApplication.activate` / `osascript`; window geometry via CGWindowList (see the `screenshot-macos` skill for the CGWindow precedent); key via CGEvent.

## Reproducing a benchmark on a second machine

Most "our numbers don't match" reports are an environment mismatch. Before comparing two boxes, confirm in order:

- **Same code.** Both on the same branch/commit (`git rev-parse HEAD` must match). The release-visible `[frame]` and `[profile]` lines only exist past the commit that added them. A silent release log almost always means wrong branch.
- **Clean checkout.** `git pull --recurse-submodules` and `git submodule update --init --recursive`. A phantom or unfetched submodule is a local-clone problem. `.git/config` is per-clone and git never transmits it.
- **Same build.** Same toolchain via the flake. A non-nix build diverges from the reference.
- **Same measurement.** Same driver, same `--res`, foreground-verified (`FRONT`, never `BG!!`), warmup dropped.
- **Compare `swap=` before `total=`.** Differing swap VRAM means differing render resolutions and nothing downstream is comparable. Across GPUs expect `total=` to scale with pixel count while the fixed-atlas shadow pass stays roughly constant.

## Reference baseline

RTX 4090, HEAD scene (Sponza + viking room + candles, SHADOWMAP), menu open, no churn, foreground-verified, 2026-07-09. Default sweep:

```
   res    wallFPS  GPUms  GPUcap  wall/cap  bound
 640x360   814.5   0.757   1321    0.62   CPU/present
1920x1080  722.3   0.836   1196    0.60   CPU/present
3840x2160  680.2   0.880   1136    0.60   CPU/present
```

CPU/present-bound at every resolution on this GPU/scene — the GPU sits ~40% idle each frame; the ceiling is the render thread + present. `ANO_SHADOW_BUDGET=2` (frusta 26→8) shifts 4K to ~852 fps / 0.617 ms, still CPU/present-bound.
