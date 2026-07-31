# 2026-07-30 C+Ultra FPS root-cause run: rootcause-2560-04-reference

## System

- AMD Ryzen 9 5950X, NVIDIA GeForce RTX 4090 24 GB driver 572.61, 64 GB DDR4-3600, Windows 11 Pro 25H2 build 26200.8875, 3840x2160 119 Hz display at 150% scale.

## Runtime environment

- Engine: Anoptic `module-audio` reference at commit e2dbe3b84283bd250731eb3a94baad68046fae1c.
- Build: C23 reference; Release -O3 + ThinLTO, CMake + Ninja, Clang 22.1.8.
- Renderer: Vulkan, mesh/task shaders enabled, MSAA 4x, immediate present, uncapped foreground render loop.
- Scene: Sponza, viking room, candles, SHADOWMAP, 8.0 of 42 shadow frusta per frame, HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py --res 2560x1440`, 30 s, 2 s warmup dropped, fresh process, foreground verified.

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited except where Windows independently flips the native-size surface.

## Mode

Borderless windowed physical-pixel client area; per-monitor-v2 DPI aware. The 2560x1440 target realized 2560x1440.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 2560x1440 | 642.3 | 681.4 | 437.6 | 418.8 | 3.115 | 0.953 | 1049 | 0.65 | 8.0 | CPU/present |

The row was foreground-verified. Swap allocation is 174.2 MiB/MP at the realized extent, consistent with the established allocation scale.
