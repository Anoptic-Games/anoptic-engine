# 2026-08-02 hyper-c-reflection render-batched 1080p

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T.
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61.
- RAM: 64 GB DDR4-3600.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 physical pixels, 150% scale.

## Runtime environment

- Engine: Anoptic working tree based on commit `94dcf926ea2f6c931695fb4340ec058291c657a1` on `hyper-c-reflection`.
- Candidate state: the same reflected pass and attachment contracts as the immediately preceding run, with seven compatible graphics lanes compiled into three dynamic-rendering scopes and two inter-scope barrier batches.
- Build: Release with LTO, CMake + Ninja, GCC 16.1 MinGW, C++26 reflection enabled.
- Renderer: Vulkan, default Release configuration, HUD menu open.
- Scene: staged repository demo assets including Sponza, viking room, and candles; eight of 42 shadow frusta per frame.
- Harness: official `tools/perf/bench_fps_win64.py --res 1920x1080`, 30 seconds, warmup dropped, foreground verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Results

| target | front | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | FRONT | 1920x1080 | 360.1 | 800.1 | 804.0 | 483.3 | 468.2 | 2.578 | 0.708 | 1412 | 0.57 | 8.0 | CPU/present |

Conditions: one representative-resolution quick run using the same driver, machine, engine environment and byte-identical staged assets as the immediately preceding pass-specialized run. No sweep was run.

## Direct comparison

Against `2026-08-02-hyper-c-reflection-pass-specialized-1080.md`, p50 throughput improved 8.9%, average throughput improved 8.6%, measured GPU time fell 11.1%, and the MinGW executable shrank by 6,665 bytes. This is a single quick A/B rather than a multi-pair confidence interval; the effect is nevertheless far outside the 0.35% spread of the two earlier same-machine 1080p confirmation runs.
