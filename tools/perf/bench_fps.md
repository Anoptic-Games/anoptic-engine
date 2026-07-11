# Performance harness & methodology

How we measure anopticengine frame throughput reproducibly. The engine-side instrumentation and the interpretation rules here are platform-agnostic; the *driver* that positions the window and reads the log is per-platform. Windows (`bench_fps_win64.py`) and Linux (`bench_fps_linux.py`) drivers live in this directory; a macOS driver is TODO and must implement the same contract described below.

## Engine-side contract (shared across all targets)

The engine emits three lines to `logs/<session-stamp>_ano.log` (written under the executable's directory; the logging refactor in b85e213 replaced the old fixed `anoptic.log`), all release-visible, defined in `src/vulkan_backend/frame/profiling.c`. The drivers discover the per-run log by snapshotting the `logs/` dir before launch and tailing whichever `*_ano.log` the fresh process creates. Wall-clock timing is tallied in a single `anoperf_accumulator_t` — one `ano_timestamp_us` read per presented frame in `ano_frame_mark()` (inlined in `frame.h`), the dt against the previous frame's stamp stored into a fixed window, no extra clock read — and every **ANO_PERF_WINDOW_FRAMES** = 128 presented frames the accumulator flushes: sorted once, logged as a `[frame]` + `[frametime]` pair, reset. Nothing is ever logged per frame; 128 = 2^7 and spans ~2 s of frames at a 60 fps target, ~1 s at 120.

- `[frame] <fps> fps <ms> ms wall` — **wall-clock throughput** over the flush window, from `anoperf_flush()` on the presented-frame path in `drawFrame`. Timestamp-independent: it keeps reporting even when constant swapchain recreation starves the GPU-timestamp reads. This is the number that reflects the whole pipeline (CPU record + submit + present + GPU).
- `[frametime] n=128 min=<ms> p50=<ms> p90=<ms> p99=<ms> p999=<ms> max=<ms>` — **per-frame frametime distribution**: exact percentiles over the window's 128 per-frame wall dts, sorted at flush. This is the genuine stutter signal none of the `[frame]` line's window mean can show. At n=128 the `p999` column interpolates between the two worst frames, i.e. it reads as the window's worst-frame statistic; `max` is the worst single frame outright.
- `[profile mode=<mode>] ... total=<ms> ... (frusta N/42) ... | VRAM MiB: ... swap=<MiB> ...` — **GPU pass time**, the sum of per-pass GPU timestamps (upload+compute+shadow+lighting+composite), printed every `ANO_PERF_WINDOW_FRAMES` (128) rendered frames. GPU-timestamp-gated, so it goes silent under a resize storm. `frusta` is shadow-frustum renders/frame; `swap` is swapchain-allocator resident VRAM.

`total` is GPU-pass time only, not wall-clock. They answer different questions; report both.

## Reading the numbers

- `GPUcap = 1000 / total_ms` — the fps the GPU passes alone could sustain.
- `wall/cap = wall_fps / GPUcap` — the bound indicator. Near 1.0 → **GPU-bound**. Well below 1.0 (we treat <0.9 as the line) → **CPU/present-bound**: the GPU is idle part of every frame and the ceiling is the main render thread or present.
- A CPU/present ceiling is not fixed — cutting per-frame CPU work (e.g. fewer shadow frusta to record) raises it. It also is not vsync unless the fps sits on a refresh multiple; uncapped values off the refresh grid mean present/CPU overhead, not vsync.
- **Sanity-check the render resolution against `swap` VRAM, never the window label.** swap scales ~linearly with pixel count; if you think you set 4K but swap matches your 1080p run ×~1, you actually rendered at 1080p. This caught a DPI-mislabel once already.

## Sampling granularity: fps vs frametime

Flush windows are frame-count-fixed, not time-fixed: one `[frame]` + `[frametime]` pair per 128 presented frames, so line cadence scales with fps — ~0.16 s per window at a bench-speed 800 fps, ~1.07 s at 120, ~2.13 s at 60. A `--dur 45` run carries roughly `45·fps/128` windows (~280 at 800 fps, ~21 at 60). Drivers therefore drop warmup by elapsed time — the first `WARMUP_S` (2 s) worth of windows, each window's span derived from its own fps — never by a fixed line count. There is no per-frame frametime stream.

What that means for the table: the `avgFPS` and `p50` columns are computed over these per-window fps samples, so they describe throughput and window-to-window consistency (did one window tank?), not frame-to-frame stutter.

True frametime-stability metrics come from the companion `[frametime]` line: percentiles are exact within each 128-frame window, and the driver folds the windows into the table's `1%low`/`0.1%low`/`maxms` columns — the lows are the standard game-bench pair, `1% = 1000/p99_ms` and `0.1% = 1000/p999_ms`, each percentile taken as the median across windows (robust to one noisy window); `maxms` is the worst single-frame spike over the whole run. Percentiles can't be meaningfully averaged, hence median-across-windows rather than a pooled recompute. One caveat at this window size: per-window `p999` saturates toward the window max, so the derived 0.1% lows track the typical worst-frame-per-128 — a stable stutter indicator, but mildly optimistic against a true pooled whole-run p999 at very high fps; `maxms` still catches the absolute spike. These are the columns to watch for stutter; `avgFPS`/`p50` remain the throughput headline.

## What any driver MUST do (portable requirements)

These are the non-negotiables; every platform driver re-implements them with its own primitives:

- **Physical-pixel sizing.** Be DPI-aware (Windows: per-monitor-v2; the compositor equivalents elsewhere) or monitor rects come back in logical/scaled units and you mislabel resolution.
- **Force AND verify foreground.** A background or occluded window mismeasures the GPU passes (the lighting pass alone inflated ~5× in one comparison). Bring the window to the true foreground and confirm it (Windows: `GetForegroundWindow() == hwnd`); flag/skip any data point that isn't front. This is the single biggest correctness trap.
- **Fresh process per data point.** Relaunch for each resolution/config; don't resize-and-reuse (carries swapchain/cache state).
- **Fresh log per run.** Each launch writes its own `logs/<session-stamp>_ano.log`, so there is nothing to delete between runs; snapshot the `logs/` dir before launch and tail the file that appears. (On Windows the exiting process still holds its handle briefly — don't try to remove it.)
- **Drop warmup.** Discard the first ~2 s worth of `[frame]`/`[frametime]` windows (by elapsed time, not line count — cadence scales with fps) and the first several profile lines before taking medians.

## Runtime knobs (engine `getenv`)

Pass via the child process environment. Perf-relevant:

- `ANO_SHADOW_BUDGET=N` — cap content-dirty shadow re-renders/frame (0 = unlimited; matrix-dirty renders exempt, so `frusta` floors above N). N=2 on the demo scene cut GPU `total` ~30% and lifted fps ~25–30%.
- `ANO_MSAA=N`, `ANO_HIZ_ON`, `ANO_DEVICE=idx` — MSAA level, Hi-Z toggle, GPU selection.
- `ANO_SHADOW_CACHE_FREEZE`, `ANO_FORCE_NO_SHADOW_CACHE`, `ANO_FORCE_NO_SWEPT` — shadow cache behavior.
- `ANO_FORCE_NO_{ASYNC_HIZ,ASYNC_LC,ASYNC_TEXT,DEPTH_RESOLVE,MESH_SHADER,SHADER_OUTPUT_LAYER,TASK,TEXT,TEXT_WORLD,UI,UI_TILES}` — feature-disable toggles for A/B isolation.
- `ANO_UI_DEMO`, `ANO_TEXT_DEMO`, `ANO_UI_OPAQUE`, `ANO_TEXT_OPAQUE` — demo-content / blend toggles.

## Drivers

- **Windows:** `tools/perf/bench_fps_win64.py` (win32 + pywin32). Modes: default resolution sweep, `--res WxH`, `--no-menu` (static HUD), `--churn` (resize storm), `--env KEY=VAL`. Prints one table row per data point with a `front` column — treat any `BG!!` row as invalid. The row carries `avgFPS`/`p50` over the per-window `[frame]` samples, the `1%low`/`0.1%low`/`maxms` frametime columns, and the GPU-pass columns (see Sampling granularity); default `--dur` is 45 s. Rows paste straight into `docs/benchmarks/template.md`.
- **Linux:** `tools/perf/bench_fps_linux.py` (X11/Xwayland). Same modes/flags as the Windows driver. Window discovery by PID (`xdotool search --pid`, `_NET_WM_PID`, 'Vulkan'-title fallback). Forced and verified foreground via `xdotool windowactivate --sync` confirmed against `getactivewindow`. Borderless render surface via `_MOTIF_WM_HINTS` strip + `windowsize --sync`. Menu key via XTEST (`xdotool key m`). X11 hands out physical pixels and the driver commands exact pixel sizes. `swap=` stays the resolution authority. Tools come from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`. Drives X11/Xwayland clients only, never native-Wayland GLFW windows. Its `parse_stream()` also replays a captured `_ano.log` offline with no X server.
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
