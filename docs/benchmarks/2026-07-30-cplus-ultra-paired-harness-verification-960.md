# 2026-07-30 C+Ultra paired-harness verification at 960x540

## System

- AMD Ryzen 9 5950X, NVIDIA GeForce RTX 4090 24 GB driver 572.61, 64 GB DDR4-3600, Windows 11 Pro 25H2 build 26200.8875, 3840x2160 119 Hz display at 150% scale.

## Runtime environment

- A: Anoptic `module-audio` reference at commit `e2dbe3b84283bd250731eb3a94baad68046fae1c`; executable SHA-256 `E105B3FD8BA3A8F9322C1F89D15B3C3153D7B36617DCE6C229AF480BDE6453D3`.
- B: Anoptic `codex/hyper-c-codegen` C+Ultra worktree based on commit `e2dbe3b84283bd250731eb3a94baad68046fae1c` with the report's uncommitted branch changes; executable SHA-256 `443A0BC9FCEFB9F9F8DF0FF2CDEEA960D882BCF603D7A7C9537DB73817CAF0D8`.
- Build: A C23 reference and B C++26 branch; Release -O3 + ThinLTO, CMake + Ninja, Clang 22.1.8.
- Renderer: Vulkan, mesh/task shaders enabled, MSAA 4x, immediate present, uncapped foreground render loop.
- Scene: Sponza, viking room, candles, SHADOWMAP, 8.0 of 42 shadow frusta per frame, HUD menu open.
- Harness: updated `tools/perf/bench_fps_win64.py --exe <A> --compare-exe <B> --res 960x540 --pairs 6 --dur 30`; six adjacent pairs, ABBA/BAAB balanced launch order, 30 s per fresh process, 2 s warmup dropped, foreground verified, aggregate DWM GPU exposure sampled at 1 Hz.

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited. The 960x540 surface exposed DWM GPU work throughout the run.

## Mode

Borderless windowed physical-pixel client area; per-monitor-v2 DPI aware. Every 960x540 target realized exactly 960x540 with 97.6 MiB swap allocation.

## Raw pairs

```text
 pair ord rev      target front      render  swapMiB  avgFPS     p50   1%low 0.1%low   maxms   GPUms  GPUcap  w/cap frusta  bound    DWM%
    1  AB   A     960x540 FRONT     960x540     97.6   999.0   999.9   545.3   533.6   2.675   0.560    1784   0.56    8.0  CPU/present   13.75
    1  AB   B     960x540 FRONT     960x540     97.6  1006.1  1006.9   547.0   535.3   2.760   0.555    1802   0.56    8.0  CPU/present   13.77
    2  BA   B     960x540 FRONT     960x540     97.6  1013.8  1016.3   547.3   536.8   2.302   0.552    1812   0.56    8.0  CPU/present   13.69
    2  BA   A     960x540 FRONT     960x540     97.6  1007.1  1010.8   545.9   535.6   2.355   0.554    1805   0.56    8.0  CPU/present   13.66
    3  BA   B     960x540 FRONT     960x540     97.6  1001.3  1003.0   545.3   533.3   3.153   0.557    1795   0.56    8.0  CPU/present   13.81
    3  BA   A     960x540 FRONT     960x540     97.6  1003.4  1006.3   548.8   535.6   2.344   0.557    1795   0.56    8.0  CPU/present   13.93
    4  AB   A     960x540 FRONT     960x540     97.6  1006.9  1012.2   545.4   536.2   3.159   0.555    1802   0.56    8.0  CPU/present   13.89
    4  AB   B     960x540 FRONT     960x540     97.6  1002.3  1003.2   544.7   533.6   2.733   0.558    1792   0.56    8.0  CPU/present   13.80
    5  AB   A     960x540 FRONT     960x540     97.6  1006.7  1007.8   545.7   535.5   2.206   0.556    1797   0.56    8.0  CPU/present   13.74
    5  AB   B     960x540 FRONT     960x540     97.6  1011.1  1013.9   547.9   535.6   2.459   0.554    1805   0.56    8.0  CPU/present   13.76
    6  BA   B     960x540 FRONT     960x540     97.6   999.5  1003.7   547.0   534.8   2.558   0.559    1789   0.56    8.0  CPU/present   13.57
    6  BA   A     960x540 FRONT     960x540     97.6   999.5  1000.6   547.6   535.6   3.033   0.561    1783   0.56    8.0  CPU/present   13.74
```

## Paired comparison

| res | A fps | B fps | B - A | paired 95% CI | A DWM % | B DWM % | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 960x540 | 1003.77 | 1005.70 | +0.19% | -0.32% to +0.70% | 13.78 | 13.74 | neutral |

All twelve processes were foreground verified and produced the same realized extent, swap allocation, shadow-frusta count, bound classification, and post-warmup GPU profile. The paired interval contains zero, so this run rejects attribution of either a C+Ultra regression or gain at 960x540. DWM exposure differed by only -0.04 percentage points for B; no compositor correction was applied.
