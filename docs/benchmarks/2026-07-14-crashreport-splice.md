# 2026-07-14 FPS benchmark (feature-crashreport, post-splice)

Default resolution sweep on an RTX 4090 at the head of feature-crashreport (commit 4abd042, the blackbox intdiv test splice; engine code identical to f9f22fa "Crisp tables", two commits past the 2026-07-11 run's 3e6259e and carrying the c9e99f2 Perfbench refactor). Harness default `ANO_SHADOW_BUDGET=2` (content-dirty shadow re-renders capped, frusta 8). Wall-clock throughput and GPU-pass time at each resolution, borderless windowed, SHADOWMAP. Compared against 2026-07-11-crashreport.md (same branch at 3e6259e) below — the regression flagged there does not reproduce.

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (Zen 3), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8655, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: Anoptic at commit 4abd042 (branch feature-crashreport)
- Build: Release -O3, CMake + Ninja, clang 22.1.8 (MSYS2 clang64, target x86_64-w64-windows-gnu)
- Renderer: Vulkan, mesh shaders on, MSAA 4x, present mode VK_PRESENT_MODE_IMMEDIATE_KHR (no vsync, uncapped), render loop unthrottled
- Scene: default HEAD demo scene, lighting mode SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: tools/perf/bench_fps_win64.py, default sweep, 30 s per point, warmup dropped, per-point medians, foreground-verified

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), the Windows 11 compositor. Frames are DWM-composited. No exclusive fullscreen.

## Mode

Borderless windowed (WS_POPUP) at the screen origin, client area sized to each resolution in physical pixels (per-monitor DPI-aware v2). At 3840x2160 the window covers the whole native desktop (borderless fullscreen); the smaller rows are borderless sub-windows.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 640x360 | 41.4 | 1081.2 | 577.0 | 563.1 | 2.205 | 0.514 | 1946 | 0.56 | 8.0 | CPU/present |
| 960x540 | 97.6 | 1038.4 | 569.2 | 546.7 | 2.299 | 0.538 | 1857 | 0.56 | 8.0 | CPU/present |
| 1280x720 | 164.7 | 967.8 | 544.7 | 533.9 | 2.426 | 0.615 | 1626 | 0.60 | 8.0 | CPU/present |
| 1920x1080 | 360.1 | 840.9 | 504.7 | 496.3 | 3.580 | 0.754 | 1326 | 0.63 | 8.0 | CPU/present |
| 2560x1440 | 642.3 | 709.9 | 466.4 | 448.6 | 2.991 | 0.927 | 1079 | 0.66 | 8.0 | CPU/present |
| 3840x2160 | 1406.3 | 598.1 | 562.7 | 550.4 | 3.269 | 1.649 | 606 | 0.99 | 8.0 | GPU |

For textbuffer viewing convenience:
```
┌───────────┬──────────┬──────────┬────────┬──────────┬────────┬────────┬─────────┬──────────┬────────┬─────────────┐
│    res    │ swap MiB │ wall fps │ 1% low │ 0.1% low │ max ms │ GPU ms │ GPU cap │ wall/cap │ frusta │    bound    │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 640x360   │ 41.4     │ 1081.2   │ 577.0  │ 563.1    │ 2.205  │ 0.514  │ 1946    │ 0.56     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 960x540   │ 97.6     │ 1038.4   │ 569.2  │ 546.7    │ 2.299  │ 0.538  │ 1857    │ 0.56     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 1280x720  │ 164.7    │ 967.8    │ 544.7  │ 533.9    │ 2.426  │ 0.615  │ 1626    │ 0.60     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 1920x1080 │ 360.1    │ 840.9    │ 504.7  │ 496.3    │ 3.580  │ 0.754  │ 1326    │ 0.63     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 2560x1440 │ 642.3    │ 709.9    │ 466.4  │ 448.6    │ 2.991  │ 0.927  │ 1079    │ 0.66     │ 8.0    │ CPU/present │
├───────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼────────┼─────────────┤
│ 3840x2160 │ 1406.3   │ 598.1    │ 562.7  │ 550.4    │ 3.269  │ 1.649  │ 606     │ 0.99     │ 8.0    │ GPU         │
└───────────┴──────────┴──────────┴────────┴──────────┴────────┴────────┴─────────┴──────────┴────────┴─────────────┘
```

All six rows foreground-verified. wall fps is the median per-window throughput from the [frame] line (ANO_PERF_WINDOW_FRAMES = 128 presented frames per window). 1% low and 0.1% low are 1000/p99 and 1000/p999 from the [frametime] line, each percentile the median across windows; max ms is the run's worst single frame. GPU ms is the median GPU-pass total (upload + compute + shadow + lighting + composite); GPU cap = 1000 / GPU ms; wall/cap at or above 0.9 is GPU-bound, below is CPU/present-bound; swap MiB is the swapchain allocator's resident VRAM.

Resolution check: swap VRAM tracks pixel count linearly, ~170-188 MiB per megapixel across the sweep. The 4K row's swap is 3.9x the 1080p row against a 4.0x pixel ratio — every row rendered at its true labeled resolution. Swap, not the window label, is the authority. The swap column is byte-identical to the 2026-07-11 runs, as expected for an unchanged renderer.

## vs 2026-07-11-crashreport.md (3e6259e): the flagged regression does not reproduce

Same machine, same branch, three commits apart (3e6259e -> 4abd042; the only engine-relevant delta is the c9e99f2 Perfbench refactor — f9f22fa is docs and 4abd042 is a test-only change). The 2026-07-11 run read ~20-30% below the feature-profiling baseline and flagged it for investigation before merge. Today's numbers recover fully and land on top of that baseline (2026-07-11-bench2.md, feature-profiling 781158e):

| res | wall fps (crashreport 07-11 -> 07-14) | GPU ms (07-11 -> 07-14) | profiling baseline fps |
| --- | --- | --- | --- |
| 640x360 | 883.6 -> 1081.2 (+22%) | 0.589 -> 0.514 | 1074.5 |
| 960x540 | 849.3 -> 1038.4 (+22%) | 0.632 -> 0.538 | 1022.3 |
| 1280x720 | 787.6 -> 967.8 (+23%) | 0.695 -> 0.615 | 959.0 |
| 1920x1080 | 738.9 -> 840.9 (+14%) | 0.804 -> 0.754 | 838.7 |
| 2560x1440 | 575.5 -> 709.9 (+23%) | 1.071 -> 0.927 | 693.3 |
| 3840x2160 | 420.2 -> 598.1 (+42%) | 2.311 -> 1.649 | 593.5 |

For textbuffer viewing convenience:
```
┌───────────┬───────────────────────────────────────┬─────────────────────────┬────────────────────────┐
│    res    │ wall fps (crashreport 07-11 -> 07-14) │ GPU ms (07-11 -> 07-14) │ profiling baseline fps │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 640x360   │ 883.6 -> 1081.2 (+22%)                │ 0.589 -> 0.514          │ 1074.5                 │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 960x540   │ 849.3 -> 1038.4 (+22%)                │ 0.632 -> 0.538          │ 1022.3                 │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 1280x720  │ 787.6 -> 967.8 (+23%)                 │ 0.695 -> 0.615          │ 959.0                  │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 1920x1080 │ 738.9 -> 840.9 (+14%)                 │ 0.804 -> 0.754          │ 838.7                  │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 2560x1440 │ 575.5 -> 709.9 (+23%)                 │ 1.071 -> 0.927          │ 693.3                  │
├───────────┼───────────────────────────────────────┼─────────────────────────┼────────────────────────┤
│ 3840x2160 │ 420.2 -> 598.1 (+42%)                 │ 2.311 -> 1.649          │ 593.5                  │
└───────────┴───────────────────────────────────────┴─────────────────────────┴────────────────────────┘
```

Every row now matches or edges past the feature-profiling numbers, GPU-pass time is back at or below the profiling-era cost, and frametime consistency improved outright (4K 1% low 216.3 -> 562.7 fps, 0.1% low 196.4 -> 550.4). Whether the 07-11 deficit was cured by the Perfbench refactor or was machine state during that run cannot be separated retroactively, but the pre-merge concern in 2026-07-11-crashreport.md is closed: the branch at 4abd042 shows no regression against its profiling merge-base. Conditions differ across the three runs (bench2 15 s/point, 07-11 crashreport 45 s/point, this run the current 30 s default); rows line up by render extent regardless.
