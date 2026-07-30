# Performance harness & methodology

How we measure anopticengine frame throughput. Engine instrumentation and interpretation are platform-agnostic; the *driver* (window position + log read) is per-platform. Drivers in this directory, same contract: Windows `bench_fps_win64.py`, Linux `bench_fps_linux.py`, macOS `bench_fps_macos.py`.

## Engine-side contract (shared across all targets)

The engine emits three release-visible lines to `logs/<session-stamp>_ano.log` (under the executable dir; b85e213 replaced fixed `anoptic.log`), defined in `src/vulkan_backend/frame/profiling.c`. Drivers snapshot `logs/` before launch and tail the new `*_ano.log`.

Wall-clock: one `anoperf_accumulator_t`, one `ano_timestamp_us` per presented frame in `ano_frame_mark()` (inlined in `frame.h`), dt into a fixed window, no extra clock read. Every **ANO_PERF_WINDOW_FRAMES** = 128 presented frames: sort once, log `[frame]` + `[frametime]`, reset. No per-frame logging. 128 = 2^7 (~2 s at 60 fps, ~1 s at 120).

- `[frame] <fps> fps <ms> ms wall`: **wall-clock throughput** over the flush window, from `anoperf_flush()` on the presented-frame path in `drawFrame`. Timestamp-independent; keeps reporting when swapchain recreation starves GPU-timestamp reads. Whole pipeline (CPU record + submit + present + GPU).
- `[frametime] n=128 min=<ms> p50=<ms> p90=<ms> p99=<ms> p999=<ms> max=<ms>`: **per-frame frametime distribution**. Exact percentiles over the window's 128 wall dts, sorted at flush. Stutter signal the `[frame]` window mean cannot show. At n=128, `p999` interpolates the two worst frames (window worst-frame statistic); `max` is the single worst frame.
- `[profile mode=<mode> res=<WxH>] ... total=<ms> ... (frusta N/42) ... | VRAM MiB: ... swap=<MiB> ...`: **GPU pass time**, sum of per-pass GPU timestamps (upload+compute+shadow+lighting+composite), every `ANO_PERF_WINDOW_FRAMES` (128) rendered frames. GPU-timestamp-gated; silent under a resize storm. `res` = realized swapchain extent (render-resolution ground truth; drivers' `render` column beside `target`); `frusta` = shadow-frustum renders/frame; `swap` = swapchain-allocator resident VRAM.

`total` is GPU-pass time only, not wall-clock. Report both.

## Reading the numbers

- `GPUcap = 1000 / total_ms`: fps the GPU passes alone could sustain.
- `wall/cap = wall_fps / GPUcap`: bound indicator. Near 1.0 → **GPU-bound**. Well below 1.0 (<0.9) → **CPU/present-bound** (GPU idle part of every frame; ceiling is main render thread or present). `?` bound = no profile window survived warmup (run too short). Driver exits nonzero on any `?` row (churn excepted: profile line timestamp-gated silent by design); rerun with longer `--dur`.
- A CPU/present ceiling is not fixed: cutting per-frame CPU work (e.g. fewer shadow frusta) raises it. Not vsync unless fps sits on a refresh multiple; uncapped values off the refresh grid mean present/CPU overhead.
- **Rendered resolution is `res=`, never the window label.** Drivers tabulate `[profile]` `res=` as `render`. Compare rows by `render`, not `target`. `swap` VRAM cross-check: scales ~linearly with `res=` pixel count. `render`/`swap` disagreement = engine accounting bug; `target`/`render` disagreement = display or window system altered the request. Caught a DPI-mislabel and an AppKit height-clamp this way.

## Sampling granularity: fps vs frametime

Flush windows are frame-count-fixed, not time-fixed: one `[frame]` + `[frametime]` pair per 128 presented frames. Cadence scales with fps: ~0.16 s/window at 800 fps, ~1.07 s at 120, ~2.13 s at 60. A `--dur 45` run carries roughly `45·fps/128` windows (~280 at 800 fps, ~21 at 60). Drivers drop warmup by elapsed time: first `WARMUP_S` (2 s) of windows (each window's span from its own fps), never by fixed line count. No per-frame frametime stream.

Table columns `avgFPS` / `p50` are over these per-window fps samples: throughput and window-to-window consistency, not frame-to-frame stutter.

Frametime stability comes from `[frametime]`: exact percentiles within each 128-frame window. Driver folds windows into `1%low` / `0.1%low` / `maxms`:
- `1% = 1000/p99_ms`, `0.1% = 1000/p999_ms`, each percentile as median across windows (robust to one noisy window)
- `maxms` = worst single-frame spike over the whole run
- Percentiles: median-across-windows, not pooled recompute
- Caveat: per-window `p999` saturates toward window max, so 0.1% lows track typical worst-frame-per-128 (stable stutter indicator, mildly optimistic vs pooled whole-run p999 at very high fps); `maxms` still catches the absolute spike

Watch `1%low` / `0.1%low` / `maxms` for stutter; `avgFPS` / `p50` for throughput.

## What any driver MUST do (portable requirements)

Non-negotiables; every platform driver re-implements with its own primitives:

- **Physical-pixel sizing.** Be DPI-aware (Windows: per-monitor-v2; compositor equivalents elsewhere) or monitor rects come back in logical/scaled units and you mislabel resolution.
- **Derive the sweep from the measured display.** Query the actual display, filter the standard ladder to points it can realize, top with display-max. Never request a point the display cannot render at its label; a silently clamped window produces a lying row name. Print display facts (native panel, mode, scale, largest realizable framebuffer) above the table.
- **Tabulate the realized extent.** `render` from `[profile]` `res=`; `target` is only the request. Compare rows by `render`.
- **Force AND verify foreground.** Background/occluded windows mismeasure GPU passes (lighting alone inflated ~5× in one comparison). Bring to true foreground and confirm (Windows: `GetForegroundWindow() == hwnd`); flag/skip any point that isn't front. Biggest correctness trap.
- **Fresh process per data point.** Relaunch per resolution/config; don't resize-and-reuse (carries swapchain/cache state).
- **Exactly 30 seconds per point.** 30 s unless `--dur` overrides. Short of GPU profile windows → nonzero exit, whole run fails. Standing env every platform: `ANO_SHADOW_BUDGET=2`; macOS also always `MTL_HUD_ENABLED=1 MTL_HUD_VISIBLE=1` (Metal HUD is standard mac config, echoed in `ENV_VARS`).
- **Fresh log per run.** Each launch writes `logs/<session-stamp>_ano.log`; nothing to delete between runs. Snapshot `logs/` before launch, tail the new file. (Windows: exiting process holds its handle briefly; don't try to remove it.)
- **Drop warmup.** Discard first ~2 s of `[frame]`/`[frametime]` windows (by elapsed time, not line count; cadence scales with fps) and the first several profile lines before medians.

## Runtime knobs (engine `getenv`)

Pass via the child process environment. Perf-relevant:

- `ANO_SHADOW_BUDGET=N`: cap content-dirty shadow re-renders/frame (0 = unlimited; matrix-dirty renders exempt, so `frusta` floors above N). N=2 on the demo scene cut GPU `total` ~30% and lifted fps ~25–30%.
- `ANO_MSAA=N`, `ANO_HIZ_ON`, `ANO_DEVICE=idx`: MSAA level, Hi-Z toggle, GPU selection.
- `ANO_SHADOW_CACHE_FREEZE`, `ANO_FORCE_NO_SHADOW_CACHE`, `ANO_FORCE_NO_SWEPT`: shadow cache behavior.
- `ANO_FORCE_NO_{ASYNC_HIZ,ASYNC_LC,ASYNC_TEXT,DEPTH_RESOLVE,MESH_SHADER,SHADER_OUTPUT_LAYER,TASK,TEXT,TEXT_WORLD,UI,UI_TILES}`: feature-disable toggles for A/B isolation.
- `ANO_UI_DEMO`, `ANO_TEXT_DEMO`, `ANO_UI_OPAQUE`, `ANO_TEXT_OPAQUE`: demo-content / blend toggles.
- `ANO_RES=WxH`, `ANO_POS=XxY`, `ANO_FLOAT`, `ANO_MENU`: launch-time window size / position (screen coordinates), float above normal windows, HUD menu open at boot. For drivers where external resize, raise, and key injection are permission-gated (macOS Accessibility); other drivers keep manipulating the window externally.

## Drivers

- **Windows:** `tools/perf/bench_fps_win64.py` (win32 + pywin32). Modes: default resolution sweep, `--res WxH`, `--no-menu` (static HUD), `--churn` (resize storm), `--env KEY=VAL`, and `--compare-exe PATH` (paired executable comparison). Sweep from primary monitor (`GetSystemMetrics` under per-monitor-DPI-v2, physical pixels): ladder points that fit, topped by display-native; `--churn` storms from native base. One table row per point with `front` and `render` columns: treat any `BG!!` as invalid, compare by `render`. Row carries `avgFPS`/`p50` over per-window `[frame]` samples, `1%low`/`0.1%low`/`maxms` frametime columns, and GPU-pass columns (see Sampling granularity); default `--dur` is exactly 30 s. Rows paste into `docs/benchmarks/template.md`.
- **Linux:** `tools/perf/bench_fps_linux.py` (X11/Xwayland). Same modes/flags as Windows. Window discovery by PID (`xdotool search --pid`, `_NET_WM_PID`, 'Vulkan'-title fallback). Forced/verified foreground via `xdotool windowactivate --sync` confirmed against `getactivewindow`. Borderless surface via `_MOTIF_WM_HINTS` strip + `windowsize --sync`. Menu key via XTEST (`xdotool key m`). X11 device pixels; sweep from `xdotool getdisplaygeometry` (falls back to unfiltered ladder with warning if query fails; `render` column still tells the truth). `res=` = realized extent, `swap=` = cross-check. Tools from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`. X11/Xwayland clients only, never native-Wayland GLFW. `parse_stream()` also replays a captured `_ano.log` offline with no X server.
- **macOS:** `tools/perf/bench_fps_macos.py` (pyobjc Quartz + Cocoa, via Nix: `nix-shell -p "python3.withPackages (ps: [ps.pyobjc-framework-Quartz ps.pyobjc-framework-Cocoa])"`). External resize (AXUIElement) and key injection (CGEventPost) are Accessibility-gated, so the driver uses engine launch knobs: `ANO_RES` sizes, `ANO_POS` places (from measured display), `ANO_MENU` opens HUD, `ANO_FLOAT` floats above normal windows. Every mac run also carries `MTL_HUD_ENABLED=1 MTL_HUD_VISIBLE=1` (Metal HUD, standard mac config, echoed in `ENV_VARS`). Prints display facts above the table: physical panel (native-flagged CGDisplayMode), desktop mode in points, backing scale, largest realizable window framebuffer (visible frame minus title bar, times scale). On a scaled Retina desktop the swapchain backing store can exceed the panel; WindowServer downsamples. Sweep derives from those facts; `--res` values are framebuffer pixels, divided by `backingScaleFactor` for the points `ANO_RES` takes. `res=` = realized extent, `swap=` = cross-check; over-display request gets AppKit height-clamp, warned, named by its render. `front` = active or <5% occluded by CGWindowList z-order (macOS 14 cooperative activation denies focus grabs to background scripts; full occlusion vs free at 640x360 benched within 1%, so floating window satisfies the gate). Window discovery by PID via `CGWindowListCopyWindowInfo` (`screenshot-macos` CGWindow precedent). `--churn` unsupported (live external resize needs Accessibility). `parse_stream()` also replays a captured session log offline.

## Cross-revision comparisons

Do not subtract two revision-wide Windows sweeps run one after the other. Session drift and Desktop Window Manager work can make the sweep order look like an engine effect. Use `--exe <A> --compare-exe <B>`; the driver runs fresh processes as adjacent pairs at each resolution in ABBA/BAAB blocks, so A and B occupy first position equally. The default is six pairs per resolution; `--pairs` must be an even integer at least two. Resize-storm `--churn` is a stress mode without the stable GPU profile required by this comparison and cannot be combined with `--compare-exe`.

The summary is B minus A. It computes the mean paired FPS difference and a two-sided Student-t 95% confidence interval over the adjacent pairs, scaled by mean A FPS. `B faster` or `B slower` requires the entire interval to exclude zero; an interval containing zero is `neutral`, not a measured regression or gain.

Comparison mode samples Windows `GPU Engine` counters once per second and reports `DWM%`, the sum of utilization across every DWM GPU-engine instance. This is an exposure diagnostic, not a normalization factor and not directly comparable to the engine's GPU-pass milliseconds. Missing DWM counters, a background run, a missing post-warmup GPU profile, or an A/B render-extent mismatch invalidates the comparison and returns nonzero.

Keep every raw pair row and the paired summary in the benchmark record. Record both executable commits/builds, the pair count, the order contract, duration, environment, display mode, and compositor state.

Run `python tools/perf/bench_fps_win64_test.py` after changing the comparison scheduler or statistics. The deterministic cases pin first-position balance, linear-drift cancellation, confidence-interval direction, and missing-counter propagation.

## Reproducing a benchmark on a second machine

Most "our numbers don't match" reports are an environment mismatch. Before comparing two boxes, confirm in order:

- **Same code.** Same branch/commit (`git rev-parse HEAD` must match). Release-visible `[frame]` and `[profile]` lines only exist past the commit that added them. Silent release log almost always means wrong branch.
- **Clean checkout.** `git pull --recurse-submodules` and `git submodule update --init --recursive`. Phantom/unfetched submodule is a local-clone problem. `.git/config` is per-clone; git never transmits it.
- **Same build.** Same toolchain via the flake. A non-nix build diverges from the reference.
- **Same measurement.** Same driver, same `--res`, foreground-verified (`FRONT`, never `BG!!`), warmup dropped.
- **Compare `swap=` before `total=`.** Differing swap VRAM means differing render resolutions; nothing downstream is comparable. Across GPUs expect `total=` to scale with pixel count while the fixed-atlas shadow pass stays roughly constant.

## Reference baseline

RTX 4090, HEAD scene (Sponza + viking room + candles, SHADOWMAP), menu open, no churn, foreground-verified, 2026-07-09. Default sweep:

```
   res    wallFPS  GPUms  GPUcap  wall/cap  bound
 640x360   814.5   0.757   1321    0.62   CPU/present
1920x1080  722.3   0.836   1196    0.60   CPU/present
3840x2160  680.2   0.880   1136    0.60   CPU/present
```

CPU/present-bound at every resolution on this GPU/scene: GPU ~40% idle each frame; ceiling is render thread + present. `ANO_SHADOW_BUDGET=2` (frusta 26→8) shifts 4K to ~852 fps / 0.617 ms, still CPU/present-bound.
