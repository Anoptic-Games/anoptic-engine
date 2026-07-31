# 2026-07-29 hyper-C++ controlled A/B

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T (x86-64), AM4, 3.4 GHz base
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25)
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b
- OS: Windows 11 Pro 25H2, build 26200.8875, x64
- Display: 3840x2160 at 119 Hz, 150% scale

## Runtime environment

- Engine: baseline rebuilt from e2dbe3b84283bd250731eb3a94baad68046fae1c on `module-audio`; variant built from the same base plus the uncommitted `codex/hyper-c-codegen` experiment
- Build: Release -O3 + ThinLTO, CMake + Ninja, Clang 22.1.8 (MSYS2 MinGW, target x86_64-w64-mingw32)
- Renderer: Vulkan, mesh shaders and task culling on, fp16 CDF reconstruction, MSAA 4x, asynchronous Hi-Z/light-cull/text lanes on
- Scene: Staged repository demo assets including Sponza, viking room, and candles; SHADOWMAP, 8 of 42 shadow frusta per frame, HUD menu open
- Harness: tools/perf/bench_fps_win64.py, `--res 1920x1080 --exe build\Release\anopticengine.exe`, 30 s per process, warmup dropped, per-process medians, foreground-verified, fresh process for every row

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Windowed 1920x1080 client area, per-monitor-v2 DPI-aware, foreground-verified.

## Results

The six rows below are the complete execution order. `avg FPS` and `p50` are per-window throughput statistics emitted by the harness; every row is one independent 30-second process.

| order | binary | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | variant V1 | 1920x1080 | 360.1 | 710.4 | 714.3 | 381.2 | 364.8 | 3.298 | 0.850 | 1176 | 0.61 | 8.0 | CPU/present |
| 2 | baseline B1 | 1920x1080 | 360.1 | 706.8 | 711.6 | 383.9 | 371.9 | 3.265 | 0.842 | 1188 | 0.60 | 8.0 | CPU/present |
| 3 | variant V2 | 1920x1080 | 360.1 | 696.1 | 705.4 | 376.6 | 368.5 | 3.327 | 0.842 | 1188 | 0.59 | 8.0 | CPU/present |
| 4 | baseline B2 | 1920x1080 | 360.1 | 710.2 | 714.4 | 384.0 | 371.0 | 2.927 | 0.845 | 1183 | 0.60 | 8.0 | CPU/present |
| 5 | variant V3 | 1920x1080 | 360.1 | 710.1 | 715.4 | 383.9 | 370.0 | 3.376 | 0.850 | 1176 | 0.61 | 8.0 | CPU/present |
| 6 | baseline B3 | 1920x1080 | 360.1 | 708.7 | 714.6 | 379.9 | 369.7 | 3.732 | 0.846 | 1182 | 0.60 | 8.0 | CPU/present |

## Three-run medians

| binary | avg FPS | p50 | 1% low | 0.1% low | GPU ms |
| --- | --- | --- | --- | --- | --- |
| baseline | 708.7 | 714.4 | 383.9 | 371.0 | 0.845 |
| variant | 710.1 | 714.3 | 381.2 | 368.5 | 0.850 |
| variant delta | +0.20% | -0.01% | -0.70% | -0.67% | +0.59% |

The renderer result is neutral. The variant's throughput median differs by +0.20%, its p50 by -0.01%, and its GPU-pass median by 0.005 ms; all are smaller than observed process-to-process variance. The experiment does not change renderer code or a normal per-frame path, so this controlled A/B supersedes the earlier unpaired 765.5 FPS source-clean record for causal comparison while retaining that record as machine-history context.

All rows were foreground-verified. The worst single frame observed across the three baseline runs was 3.732 ms and across the three variant runs was 3.376 ms; those isolated maxima are not treated as a throughput result.

Resolution check: 360.1 MiB / 2.0736 MP = 173.66 MiB/MP in every process. A single-resolution series cannot supply a cross-resolution swap ratio; the realized 1920x1080 extent and swap accounting agree in all six runs.
