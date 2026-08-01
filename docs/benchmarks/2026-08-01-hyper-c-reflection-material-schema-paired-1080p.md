# 2026-08-01 hyper-c-reflection material-schema paired check at 1920x1080

## System

- AMD Ryzen 9 5950X, NVIDIA GeForce RTX 4090 24 GB driver 572.61, 64 GB DDR4-3600, Windows 11 Pro 25H2 build 26200.8875, 3840x2160 119 Hz display at 150% scale.

## Runtime environment

- A: engine sources at `hyper-c-reflection` checkpoint `b4642edc1ab8ececb49321c739f29e7d852cbe77`, built with B's GCC 16 flake/toolchain wiring so the executable comparison changes the phase-three material implementation rather than the compiler; executable SHA-256 `9F941C7DE39DF388A517E14441727BA121268D9E1012B2E0B348D9FC640B2BBB`, 2,512,620 bytes.
- B: `hyper-c-reflection` material-schema commit `92413ee`; executable SHA-256 `FBFB143B45028CCC6D6AC196D54C30E2CE739F244EE2F45D1FF61D7E71B0E0AF`, 2,513,132 bytes (+512 bytes, +0.020%).
- Build: C++26 P2996 reflection; Release -O3 + LTO, CMake + Ninja, MinGW GCC 16.1.0, x86_64-w64-mingw32, `-nostdlib++`. Both executables were built in the same Nix Windows environment and launched from the same runtime asset directory.
- Renderer: Vulkan, mesh/task shaders enabled, MSAA 4x, immediate present, uncapped foreground render loop.
- Scene: Sponza, viking room, candles, SHADOWMAP, 8.0 of 42 shadow frusta per frame, HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py --res 1920x1080 --pairs 2 --exe <A> --compare-exe <B>`; two adjacent pairs in balanced AB/BA order, 30 s per fresh process, 2 s warmup dropped, foreground verified, aggregate DWM GPU exposure sampled at 1 Hz. This is the remembered minimum quick A/B, not a sweep.

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited. Aggregate DWM GPU exposure was effectively matched: 13.67% for A and 13.79% for B.

## Mode

Borderless windowed physical-pixel client area; per-monitor-v2 DPI aware. Every 1920x1080 target realized exactly 1920x1080 with 360.1 MiB swap allocation.

## Raw pairs

```text
 pair ord rev      target front      render  swapMiB  avgFPS     p50   1%low 0.1%low   maxms   GPUms  GPUcap  w/cap frusta  bound    DWM%
    1  AB   A   1920x1080 FRONT   1920x1080    360.1   803.8   808.1   498.5   477.0   3.043   0.753    1328   0.61    8.0  CPU/present   13.57
    1  AB   B   1920x1080 FRONT   1920x1080    360.1   803.0   808.0   499.5   482.4   2.778   0.752    1330   0.61    8.0  CPU/present   13.81
    2  BA   B   1920x1080 FRONT   1920x1080    360.1   803.8   807.5   498.0   478.4   3.164   0.752    1330   0.61    8.0  CPU/present   13.77
    2  BA   A   1920x1080 FRONT   1920x1080    360.1   802.1   806.3   496.0   480.8   2.468   0.753    1328   0.61    8.0  CPU/present   13.76
```

## Paired comparison

| res | A fps | B fps | B - A | paired 95% CI | A DWM % | B DWM % | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | 802.95 | 803.40 | +0.06% | -1.87% to +1.98% | 13.67 | 13.79 | neutral |

All four processes were foreground verified and produced the same realized extent, 360.1 MiB swap allocation, 8.0-frusta count, CPU/present-bound classification, and effectively identical 0.752-0.753 ms GPU-pass medians. Swap allocation was 173.7 MiB per megapixel for both revisions.

The +0.06% point estimate and interval containing zero provide no evidence of a throughput change. This minimum judge excludes a regression of 2% or larger under the tested CPU/present-bound profile but does not resolve sub-2% effects. No additional pairs or resolutions were run automatically.

The phase-three change moves material defaults, texture-domain selection, feature discovery, and bindless-slot projection into one reflected compile-time schema. It adds no reflection metadata traversal to the frame loop and introduces no C++ runtime or standard-library linkage; the affected generated work remains material-import or initialization work.
