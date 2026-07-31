# 2026-07-29 hyper-C++ full-scene baseline

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (x86-64), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8875, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: Anoptic at commit e2dbe3b84283bd250731eb3a94baad68046fae1c (branch codex/hyper-c-codegen, source-clean baseline)
- Build: Release -O3 + ThinLTO, CMake + Ninja, Clang 22.1.8 (MSYS2 MinGW, target x86_64-w64-mingw32)
- Renderer: Vulkan, mesh shaders and task culling on, fp16 CDF reconstruction, MSAA 4x, asynchronous Hi-Z/light-cull/text lanes on
- Scene: Staged repository demo assets including Sponza, viking room, and candles; SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: tools/perf/bench_fps_win64.py, `--res 1920x1080 --exe build\Release\anopticengine.exe`, 30 s, warmup dropped, per-point medians, foreground-verified

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Windowed 1920x1080 client area, per-monitor-v2 DPI-aware, foreground-verified.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1920x1080 | 360.1 | 765.5 | 449.2 | 419.6 | 3.336 | 0.772 | 1296 | 0.60 | 8.0 | CPU/present |

All rows foreground-verified. This is the full-scene baseline for the code-generation experiment.

Resolution check: 360.1 MiB / 2.0736 MP = 173.66 MiB/MP. A single-resolution run cannot supply a cross-resolution swap ratio; the realized 1920x1080 extent and swap accounting agree with the existing harness contract.
