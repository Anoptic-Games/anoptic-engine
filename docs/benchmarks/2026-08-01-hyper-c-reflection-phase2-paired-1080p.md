# 2026-08-01 hyper-c-reflection phase-two paired check at 1920x1080

## System

- AMD Ryzen 9 5950X, NVIDIA GeForce RTX 4090 24 GB driver 572.61, 64 GB DDR4-3600, Windows 11 Pro 25H2 build 26200.8875, 3840x2160 119 Hz display at 150% scale.

## Runtime environment

- A: `hyper-c-reflection` phase-one checkpoint `1aba7c027bacc9be005837b43b82f38647dd4ed6`; executable SHA-256 `46BAA49EC10F6F3EB6BE6FDE6F219CFAA9D51A1E2637066C5043A1351B0E57A3`, 2,472,470 bytes.
- B: `hyper-c-reflection` phase-two commit `4e0364b`; executable SHA-256 `36082AAE21243AD8E4F1EF8AF4CF8F1CF779B02B28891FD05AD69AF8E513E42B`, 2,472,726 bytes (+256 bytes, +0.010%).
- Build: C++26 audited-fallback path; Release -O3 + LTO, CMake + Ninja, MinGW GCC 15.2.0, x86_64-w64-mingw32, `-nostdlib++`. Both executables were built in the same Nix Windows environment and launched from the same runtime asset directory.
- Renderer: Vulkan, mesh/task shaders enabled, MSAA 4x, immediate present, uncapped foreground render loop.
- Scene: Sponza, viking room, candles, SHADOWMAP, 8.0 of 42 shadow frusta per frame, HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py --res 1920x1080 --pairs 2 --exe <A> --compare-exe <B>`; two adjacent pairs in balanced AB/BA order, 30 s per fresh process, 2 s warmup dropped, foreground verified, aggregate DWM GPU exposure sampled at 1 Hz. This is the remembered minimum quick A/B, not a sweep.

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited. DWM GPU exposure drifted materially between the first A launch and the other three launches.

## Mode

Borderless windowed physical-pixel client area; per-monitor-v2 DPI aware. Every 1920x1080 target realized exactly 1920x1080 with 360.1 MiB swap allocation.

## Raw pairs

```text
 pair ord rev      target front      render  swapMiB  avgFPS     p50   1%low 0.1%low   maxms   GPUms  GPUcap  w/cap frusta  bound    DWM%
    1  AB   A   1920x1080 FRONT   1920x1080    360.1   888.1   901.4   679.3   592.8   2.666   0.708    1412   0.64    8.0  CPU/present    3.54
    1  AB   B   1920x1080 FRONT   1920x1080    360.1   832.8   837.7   504.7   492.4   2.481   0.742    1348   0.62    8.0  CPU/present   14.15
    2  BA   B   1920x1080 FRONT   1920x1080    360.1   832.3   836.4   505.1   492.7   2.530   0.747    1339   0.62    8.0  CPU/present   14.48
    2  BA   A   1920x1080 FRONT   1920x1080    360.1   821.5   831.5   500.3   485.3   3.051   0.736    1359   0.61    8.0  CPU/present   13.44
```

## Paired comparison

| res | A fps | B fps | B - A | paired 95% CI | A DWM % | B DWM % | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | 854.81 | 832.57 | -2.60% | -51.67% to +46.47% | 8.49 | 14.31 | neutral |

All four processes were foreground verified and produced the same realized extent, 360.1 MiB swap allocation, 8.0-frusta count, CPU/present-bound classification, and post-warmup GPU profile. Swap allocation was 173.7 MiB per megapixel for both revisions.

The interval contains zero by a wide margin, so the measured -2.60% point estimate is not evidence of a phase-two regression. Pair one exposed A to 3.54% DWM GPU work and B to 14.15%; pair two exposed both revisions to roughly 14%, producing the opposite signed pair delta. The result is recorded as neutral and compositor-drift-limited. No additional pairs or resolutions were run automatically.

The phase-two code materializes descriptor-set layouts during renderer initialization and adds no frame-loop work. That supports expecting runtime neutrality, but it is source-level evidence, not a substitute for the inconclusive measurement above.
