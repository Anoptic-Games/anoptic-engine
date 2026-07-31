# 2026-07-30 C+Ultra reference sweep B

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (x86-64), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8875, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: Anoptic at commit e2dbe3b84283bd250731eb3a94baad68046fae1c (branch `module-audio`)
- Build: Release -O3 + ThinLTO test configuration, CMake + Ninja, Clang 22.1.8 (MSYS2 MinGW, target x86_64-w64-mingw32)
- Renderer: Vulkan, mesh shaders and task culling on, fp16 CDF reconstruction, MSAA 4x, asynchronous Hi-Z/light-cull/text lanes on
- Scene: Staged repository demo assets including Sponza, viking room, and candles; SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: `tools/perf/bench_fps_win64.py --exe C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\anopticengine.exe`, default sweep, 30 s per point, warmup dropped, per-point medians, foreground-verified

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Windowed client area sized in physical pixels by the DPI-aware harness; the 3840x2160 row fills the native desktop.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 41.4 | 1039.3 | 566.3 | 544.7 | 2.399 | 0.509 | 1965 | 0.53 | 8.0 | CPU/present |
| 960x540 | 97.6 | 1001.4 | 548.2 | 535.6 | 3.477 | 0.559 | 1789 | 0.56 | 8.0 | CPU/present |
| 1280x720 | 164.7 | 949.5 | 553.4 | 536.2 | 2.692 | 0.609 | 1643 | 0.58 | 8.0 | CPU/present |
| 1920x1080 | 360.1 | 832.8 | 511.6 | 501.3 | 3.070 | 0.741 | 1350 | 0.62 | 8.0 | CPU/present |
| 2560x1440 | 642.3 | 678.2 | 444.8 | 435.2 | 2.888 | 0.951 | 1052 | 0.65 | 8.0 | CPU/present |
| 3840x2160 | 1406.3 | 593.2 | 560.2 | 549.3 | 3.325 | 1.651 | 606 | 0.99 | 8.0 | GPU |

All rows were foreground-verified. This was the fourth run in the balanced order: reference A, variant A, variant B, reference B.

Resolution check: swap allocation ranges from 179.7 MiB/MP at 640x360 to 169.5 MiB/MP at 3840x2160. The 4K/1080p swap ratio is 3.905 against a 4.000 pixel ratio; the realized extents and allocation scaling agree.
