# 2026-07-11 FPS benchmark (feature-crashreport)

Default resolution sweep on an RTX 4090 at the head of feature-crashreport (commit 3e6259e, which carries the logging refactor b85e213 and the crash-report work). Harness default `ANO_SHADOW_BUDGET=2` (content-dirty shadow re-renders capped, frusta 8). Wall-clock throughput and GPU-pass time at each resolution, borderless windowed, SHADOWMAP. Compared against 2026-07-11-bench2.md (same config on feature-profiling) below.

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (Zen 3), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8655, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: Anoptic at commit 3e6259e (branch feature-crashreport)
- Build: Release -O3, CMake + Ninja, clang 22.1.8 (MSYS2 clang64, target x86_64-w64-windows-gnu)
- Renderer: Vulkan, mesh shaders on, MSAA 4x, present mode VK_PRESENT_MODE_IMMEDIATE_KHR (no vsync, uncapped), render loop unthrottled
- Scene: default HEAD demo scene, lighting mode SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: tools/perf/bench_fps_win64.py, default sweep, 45 s per point, warmup dropped, per-point medians, foreground-verified

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), the Windows 11 compositor. Frames are DWM-composited; the engine never takes exclusive fullscreen.

## Mode

Borderless windowed (WS_POPUP) at the screen origin, client area sized to each resolution in physical pixels (per-monitor DPI-aware v2). At 3840x2160 the window covers the whole native desktop (borderless fullscreen); the smaller rows are borderless sub-windows.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 640x360 | 41.4 | 883.6 | 313.0 | 289.5 | 4.503 | 0.589 | 1698 | 0.52 | 8.0 | CPU/present |
| 960x540 | 97.6 | 849.3 | 313.1 | 288.9 | 4.394 | 0.632 | 1582 | 0.54 | 8.0 | CPU/present |
| 1280x720 | 164.7 | 787.6 | 304.0 | 282.6 | 4.626 | 0.695 | 1439 | 0.55 | 8.0 | CPU/present |
| 1920x1080 | 360.1 | 738.9 | 334.3 | 292.4 | 4.635 | 0.804 | 1244 | 0.60 | 8.0 | CPU/present |
| 2560x1440 | 642.3 | 575.5 | 266.3 | 251.7 | 6.892 | 1.071 | 934 | 0.61 | 8.0 | CPU/present |
| 3840x2160 | 1406.3 | 420.2 | 216.3 | 196.4 | 8.232 | 2.311 | 433 | 0.95 | 8.0 | GPU |

For textbuffer viewing convenience:
```
┌───────────┬──────────┬──────────┬────────┬──────────┬────────┬────────┬─────────┬──────────┬────────┬─────────────┐
│    res    │ swap MiB │ wall fps │ 1% low │ 0.1% low │ max ms │ GPU ms │ GPU cap │ wall/cap │ frusta │    bound    │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 640x360   │ 41.4     │ 883.6    │ 313.0  │ 289.5    │ 4.503  │ 0.589  │ 1698    │ 0.52     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 960x540   │ 97.6     │ 849.3    │ 313.1  │ 288.9    │ 4.394  │ 0.632  │ 1582    │ 0.54     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 1280x720  │ 164.7    │ 787.6    │ 304.0  │ 282.6    │ 4.626  │ 0.695  │ 1439    │ 0.55     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 1920x1080 │ 360.1    │ 738.9    │ 334.3  │ 292.4    │ 4.635  │ 0.804  │ 1244    │ 0.60     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 2560x1440 │ 642.3    │ 575.5    │ 266.3  │ 251.7    │ 6.892  │ 1.071  │ 934     │ 0.61     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 3840x2160 │ 1406.3   │ 420.2    │ 216.3  │ 196.4    │ 8.232  │ 2.311  │ 433     │ 0.95     │ 8.0    │ GPU         │
└───────────┴──────────┴──────────┴────────┴──────────┴────────┴────────┴─────────┴──────────┴────────┴─────────────┘
```

All six rows foreground-verified. wall fps is the median per-window throughput from the [frame] line (ANO_PERF_WINDOW_FRAMES = 128 presented frames per window). 1% low and 0.1% low are 1000/p99 and 1000/p999 from the [frametime] line, each percentile the median across windows; max ms is the run's worst single frame. GPU ms is the median GPU-pass total (upload + compute + shadow + lighting + composite); GPU cap = 1000 / GPU ms; wall/cap at or above 0.9 is GPU-bound, below is CPU/present-bound; swap MiB is the swapchain allocator's resident VRAM.

Resolution check: swap VRAM tracks pixel count linearly, ~170-188 MiB per megapixel across the sweep. The 4K row's swap is 3.9x the 1080p row against a 4.0x pixel ratio, so every row rendered at its true labeled resolution; swap, not the window label, is the authority and rules out a DPI-scaled mislabel.

## vs feature-profiling baseline (2026-07-11-bench2.md)

Same config, same machine, one branch apart (feature-profiling 781158e -> feature-crashreport 3e6259e). This branch reads ~20-30% lower wall fps at every resolution, with GPU-pass time up correspondingly:

| res | wall fps (profiling -> crashreport) | GPU ms (profiling -> crashreport) |
| --- | --- | --- |
| 640x360 | 1074.5 -> 883.6 (-18%) | 0.509 -> 0.589 |
| 960x540 | 1022.3 -> 849.3 (-17%) | 0.546 -> 0.632 |
| 1280x720 | 959.0 -> 787.6 (-18%) | 0.611 -> 0.695 |
| 1920x1080 | 838.7 -> 738.9 (-12%) | 0.753 -> 0.804 |
| 2560x1440 | 693.3 -> 575.5 (-17%) | 0.941 -> 1.071 |
| 3840x2160 | 593.5 -> 420.2 (-29%) | 1.655 -> 2.311 |

For textbuffer viewing convenience:
```
┌───────────┬─────────────────────────────────────┬───────────────────────────────────┐
│    res    │ wall fps (profiling -> crashreport) │ GPU ms (profiling -> crashreport) │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 640x360   │ 1074.5 -> 883.6 (-18%)              │ 0.509 -> 0.589                    │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 960x540   │ 1022.3 -> 849.3 (-17%)              │ 0.546 -> 0.632                    │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 1280x720  │ 959.0 -> 787.6 (-18%)               │ 0.611 -> 0.695                    │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 1920x1080 │ 838.7 -> 738.9 (-12%)               │ 0.753 -> 0.804                    │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 2560x1440 │ 693.3 -> 575.5 (-17%)               │ 0.941 -> 1.071                    │
├───────────┼─────────────────────────────────────┼───────────────────────────────────┤
│ 3840x2160 │ 593.5 -> 420.2 (-29%)               │ 1.655 -> 2.311                    │
└───────────┴─────────────────────────────────────┴───────────────────────────────────┘
```

The 1% / 0.1% lows are also markedly worse (e.g. 4K 553.7 -> 216.3 fps 1% low). Both GPU-pass time and frametime consistency regressed, so this is not purely a CPU/present-side cost. This warrants investigation before merge; candidates in order of suspicion: the logging refactor (b85e213) adding drain/IO contention on the render thread, unaccounted background load during this run, or GPU thermal/clock state. The two runs are not perfectly controlled (this run used the default 45 s/point vs bench2's 15 s/point, and machine state differed), so re-run both back-to-back on this branch and its merge-base before treating the delta as a hard regression.

## Harness note

This run required a driver fix. The logging refactor (b85e213) moved the release log from a fixed `anoptic.log` next to the exe to `logs/<session-stamp>_ano.log`, so `bench_fps_win64.py` (which deletes and tails `anoptic.log`) parsed nothing and printed an all-zero sweep. The driver now snapshots `build/Release/logs/` before launch and tails whichever `*_ano.log` the fresh process creates. That change is local and uncommitted; the engine-side contract in `tools/perf/bench_fps.md` still documents `anoptic.log` and should be reconciled (either an env knob to force a fixed log path, or update the doc + Linux driver to match).
