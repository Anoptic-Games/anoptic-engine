# 2026-07-30 C+Ultra variant sweep A

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (x86-64), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8875, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: Anoptic branch `codex/hyper-c-codegen`, uncommitted C+Ultra source over commit e2dbe3b84283bd250731eb3a94baad68046fae1c
- Build: Every first-party engine and test translation unit in C++26 language mode; Release -O3 + ThinLTO test configuration, CMake + Ninja, Clang 22.1.8 (MSYS2 MinGW, target x86_64-w64-mingw32), exceptions and RTTI disabled
- Renderer: Vulkan, mesh shaders and task culling on, fp16 CDF reconstruction, MSAA 4x, asynchronous Hi-Z/light-cull/text lanes on
- Scene: Staged repository demo assets including Sponza, viking room, and candles; SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: `tools/perf/bench_fps_win64.py --exe C:\Users\Pyrus\Code\anoptic-engine\worktrees\hyper-c-codegen\build\O3Tests\anopticengine.exe`, default sweep, 30 s per point, warmup dropped, per-point medians, foreground-verified

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Windowed client area sized in physical pixels by the DPI-aware harness; the 3840x2160 row fills the native desktop.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 41.4 | 1039.6 | 573.4 | 548.4 | 3.072 | 0.506 | 1976 | 0.53 | 8.0 | CPU/present |
| 960x540 | 97.6 | 991.2 | 543.2 | 532.2 | 2.823 | 0.567 | 1764 | 0.56 | 8.0 | CPU/present |
| 1280x720 | 164.7 | 943.9 | 562.0 | 537.3 | 2.367 | 0.597 | 1675 | 0.57 | 8.0 | CPU/present |
| 1920x1080 | 360.1 | 826.5 | 521.6 | 496.0 | 3.222 | 0.729 | 1372 | 0.61 | 8.0 | CPU/present |
| 2560x1440 | 642.3 | 671.9 | 440.2 | 430.9 | 2.985 | 0.956 | 1046 | 0.65 | 8.0 | CPU/present |
| 3840x2160 | 1406.3 | 591.6 | 558.7 | 549.6 | 3.290 | 1.656 | 604 | 0.98 | 8.0 | GPU |

All rows were foreground-verified. This was the second run in the balanced order: reference A, variant A, variant B, reference B.

Resolution check: swap allocation ranges from 179.7 MiB/MP at 640x360 to 169.5 MiB/MP at 3840x2160. The 4K/1080p swap ratio is 3.905 against a 4.000 pixel ratio; the realized extents and allocation scaling agree.
