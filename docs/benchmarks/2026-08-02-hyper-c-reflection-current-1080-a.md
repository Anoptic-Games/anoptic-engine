# 2026-08-02 hyper-c-reflection current 1080p A

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T.
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61.
- RAM: 64 GB DDR4-3600.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 physical pixels, 150% scale.

## Runtime environment

- Engine: Anoptic at commit `5d343815d7780d6b906b59f85fdf731a6115a567` on `hyper-c-reflection`.
- Build: Release with LTO, CMake + Ninja, GCC 16.1 MinGW, C++26 reflection enabled.
- Renderer: Vulkan, default Release configuration, HUD menu open.
- Scene: staged repository demo assets including Sponza, viking room, and candles; eight of 42 shadow frusta per frame.
- Harness: official `tools/perf/bench_fps_win64.py --res 1920x1080`, 30 seconds, warmup dropped, foreground verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Results

| target | front | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | FRONT | 1920x1080 | 360.1 | 738.1 | 737.8 | 464.6 | 448.5 | 4.404 | 0.782 | 1279 | 0.58 | 8.0 | CPU/present |

Conditions: current-only first measurement; the current executable and its own staged shaders/assets were copied from the canonical WSL Release build to a local Windows build directory before launch.
