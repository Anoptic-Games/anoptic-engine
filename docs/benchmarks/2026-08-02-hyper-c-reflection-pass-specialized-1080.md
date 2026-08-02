# 2026-08-02 hyper-c-reflection pass-specialized 1080p

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T.
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61.
- RAM: 64 GB DDR4-3600.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 physical pixels, 150% scale.

## Runtime environment

- Engine: Anoptic working tree based on commit `94dcf926ea2f6c931695fb4340ec058291c657a1` on `hyper-c-reflection`.
- Candidate state: reflected pass expansion and reflected per-attachment hazard generation, before compatible graphics passes were collapsed into shared rendering scopes.
- Build: Release with LTO, CMake + Ninja, GCC 16.1 MinGW, C++26 reflection enabled.
- Renderer: Vulkan, default Release configuration, HUD menu open.
- Scene: staged repository demo assets including Sponza, viking room, and candles; eight of 42 shadow frusta per frame.
- Harness: official `tools/perf/bench_fps_win64.py --res 1920x1080`, 30 seconds, warmup dropped, foreground verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Results

| target | front | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | FRONT | 1920x1080 | 360.1 | 736.6 | 738.4 | 462.1 | 446.4 | 2.808 | 0.796 | 1256 | 0.59 | 8.0 | CPU/present |

Conditions: one representative-resolution quick run. The executable and its staged assets were copied from the canonical WSL GCC 16.1 Windows Release build to a local Windows runtime directory before launch. No sweep was run.
