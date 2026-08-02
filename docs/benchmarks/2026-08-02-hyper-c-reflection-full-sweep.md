# 2026-08-02 hyper-c-reflection full sweep

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T.
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61.
- RAM: 64 GB DDR4-3600.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 physical pixels, 150% scale.

## Runtime environment

- Engine: final reflected optimization working tree based on commit `94dcf926ea2f6c931695fb4340ec058291c657a1` on `hyper-c-reflection`.
- Build: Windows Release with LTO, CMake + Ninja, GCC 16.1 MinGW, C++26 reflection enabled.
- Renderer: Vulkan, default Release configuration, 4x MSAA, mesh shaders enabled, HUD menu open.
- Scene: staged repository demo assets; eight of 42 shadow frusta per frame.
- Harness: the exact `tools/perf/bench_fps_win64.py` from module-audio commit `e2dbe3b`, full display-derived sweep, 30 seconds per point, warmup dropped, foreground verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Results

| target | front | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | FRONT | 640x360 | 41.4 | 1008.1 | 1007.3 | 553.1 | 533.9 | 2.510 | 0.493 | 2026 | 0.50 | 8.0 | CPU/present |
| 960x540 | FRONT | 960x540 | 97.6 | 957.9 | 959.5 | 538.8 | 520.8 | 8.471 | 0.534 | 1873 | 0.51 | 8.0 | CPU/present |
| 1280x720 | FRONT | 1280x720 | 164.7 | 904.2 | 906.6 | 522.5 | 498.3 | 3.300 | 0.583 | 1715 | 0.53 | 8.0 | CPU/present |
| 1920x1080 | FRONT | 1920x1080 | 360.1 | 787.5 | 790.1 | 477.1 | 462.2 | 2.901 | 0.720 | 1390 | 0.57 | 8.0 | CPU/present |
| 2560x1440 | FRONT | 2560x1440 | 642.3 | 653.4 | 655.8 | 427.5 | 410.7 | 3.150 | 0.935 | 1070 | 0.61 | 8.0 | CPU/present |
| 3840x2160 | FRONT | 3840x2160 | 1406.3 | 612.6 | 615.0 | 560.5 | 540.5 | 3.149 | 1.602 | 624 | 0.99 | 8.0 | GPU |

All rows were foreground verified. The isolated 8.471 ms maximum at 960x540 did not propagate into the percentile lows.

Resolution check: swap residency is identical to the module-audio baseline at every point. The 4K-to-1080p swap ratio is 3.905 against a 4.000 pixel ratio.

## Comparison against native C module-audio

| Resolution | C p50 | C+Ultra p50 | p50 delta | C 1% low | C+Ultra 1% low | 1% delta | C GPU ms | C+Ultra GPU ms | GPU-time delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 640x360 | 929.0 | 1007.3 | +8.4% | 531.3 | 553.1 | +4.1% | 0.541 | 0.493 | −8.9% |
| 960x540 | 884.0 | 959.5 | +8.5% | 512.6 | 538.8 | +5.1% | 0.586 | 0.534 | −8.9% |
| 1280x720 | 834.1 | 906.6 | +8.7% | 496.0 | 522.5 | +5.3% | 0.638 | 0.583 | −8.6% |
| 1920x1080 | 729.2 | 790.1 | +8.4% | 457.9 | 477.1 | +4.2% | 0.780 | 0.720 | −7.7% |
| 2560x1440 | 614.8 | 655.8 | +6.7% | 414.8 | 427.5 | +3.1% | 1.000 | 0.935 | −6.5% |
| 3840x2160 | 580.5 | 615.0 | +5.9% | 524.1 | 560.5 | +6.9% | 1.698 | 1.602 | −5.7% |

Across the six-point ladder, the geometric-mean p50 improvement is 7.8% and the geometric-mean GPU-time reduction is 7.7%. Average FPS, p50, 1% low and 0.1% low improve at every resolution. The advantage narrows as fixed pixel shading dominates, but remains 5.9% at fully GPU-bound 4K.

This is a sequential full-sweep comparison rather than the driver's interleaved multi-pair confidence mode. Both runs used the same machine state, driver script, duration, environment, foreground gate and display-derived ladder.
