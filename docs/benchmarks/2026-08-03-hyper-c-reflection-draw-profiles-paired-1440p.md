# 2026-08-03 reflected draw-profile paired check at 2560x1440

## System

- AMD Ryzen 9 5950X, NVIDIA GeForce RTX 4090 24 GB driver 572.61, 64 GB DDR4-3600, Windows 11 Pro 25H2 build 26200.8875, 3840x2160 119 Hz display at 150% scale.

## Runtime environment

- A: hyper-c-reflection commit 00736a6; executable SHA-256 6436CCCC8F813D06E329BFC448FC8387EC13AA6CD58362C1065FD953CBE12314, 2,700,117 bytes.
- B: hyper-c-reflection working tree with reflected draw profiles; executable SHA-256 A272DE3D417B38B7D532F7E13E786CB0F76232BA6197BBADB1CE12CDF4A079BA, 2,697,730 bytes (-2,387 bytes). Executable text is 3,608 bytes smaller.
- Build: C++26 reflection; Release -O3 + LTO, CMake + Ninja, MinGW GCC 16.1.0, x86_64-w64-mingw32, -nostdlib++. Both executables used the same canonical Nix Windows environment and identical runtime assets.
- Renderer: Vulkan, mesh/task shaders enabled, indirect-count path supported, MSAA 4x, immediate present, uncapped foreground render loop. Async Hi-Z, async light-cull, and task meshlet culling were active.
- Scene: Sponza, viking room, candles, SHADOWMAP, 8.0 of 42 shadow frusta per frame, HUD menu open.
- Harness: tools/perf/bench_fps_win64.py --res 2560x1440 --pairs 2 --exe <A> --compare-exe <B>; two adjacent pairs in balanced AB/BA order, 30 s per fresh process, 2 s warmup dropped, foreground verified, aggregate DWM GPU exposure sampled at 1 Hz. This is the remembered minimum quick A/B, not a sweep.

ENV_VARS: ANO_SHADOW_BUDGET=2

## Window manager

Desktop Window Manager (DWM), composited. Aggregate DWM GPU exposure remained within 0.48 percentage points across all four processes.

## Mode

Borderless windowed physical-pixel client area; per-monitor-v2 DPI aware. Every 2560x1440 target realized exactly 2560x1440 with 642.3 MiB swap allocation.

## Raw pairs

~~~text
 pair ord rev      target front      render  swapMiB  avgFPS     p50   1%low 0.1%low   maxms   GPUms  GPUcap  w/cap frusta  bound    DWM%
    1  AB   A   2560x1440 FRONT   2560x1440    642.3   520.7   522.4   331.9   320.7   4.512   1.082     924   0.57    8.0  CPU/present   16.91
    1  AB   B   2560x1440 FRONT   2560x1440    642.3   521.2   523.9   334.0   323.6   3.681   1.084     923   0.57    8.0  CPU/present   16.99
    2  BA   B   2560x1440 FRONT   2560x1440    642.3   525.8   528.1   335.9   327.0   3.518   1.086     921   0.57    8.0  CPU/present   17.01
    2  BA   A   2560x1440 FRONT   2560x1440    642.3   520.5   524.9   335.6   324.1   3.983   1.080     926   0.57    8.0  CPU/present   16.53
~~~

## Paired comparison

| res | A fps | B fps | B - A | paired 95% CI | A DWM % | B DWM % | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 2560x1440 | 520.57 | 523.52 | +0.57% | -5.30% to +6.43% | 16.72 | 17.00 | neutral |

All four processes were foreground verified and produced the same realized extent, 642.3 MiB swap allocation, 8.0-frusta count, CPU/present-bound classification, and post-warmup GPU profile. Swap allocation was 174.2 MiB per megapixel for both revisions.

The interval contains zero, so the +0.57% point estimate is not evidence of a throughput improvement. The experiment is retained because runtime behavior is neutral while the isolated non-LTO recorder text falls from 16,957 to 12,693 bytes (-25.1%), ano_record_views falls from 15,524 to 9,995 bytes, and the final Windows executable text falls by 3,608 bytes.
