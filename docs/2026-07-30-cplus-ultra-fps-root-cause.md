# C+Ultra FPS root cause

Date: 2026-07-30

Branch: `codex/hyper-c-codegen`

Reference: `module-audio` at `e2dbe3b84283bd250731eb3a94baad68046fae1c`

## Verdict

The reported `-0.13%` to `-1.17%` FPS result is not a C++26 regression. It was a branch-attribution error caused by non-stationary Windows compositor scheduling in the smaller borderless windows. The original four-sweep order put both C+Ultra sweeps in the middle of a throughput trough and the second reference sweep after recovery. Native 4K almost entirely bypasses DWM composition and was neutral from the start.

Balanced single-resolution reruns reverse the sign: C+Ultra is `+0.21%` at 2560x1440 and `+0.09%` at 960x540. Both confidence intervals contain zero and exclude the original large negative at that resolution. The correct performance statement is no detected whole-frame FPS difference.

No alignment, instruction-cache, or C++ code-generation fix is indicated. Executable text remains 0.42–0.47% smaller on C+Ultra, and the apparent loss does not follow the executable.

## Initial observation

The original process order was reference A, C+Ultra A, C+Ultra B, reference B. The two revisions therefore occupied different portions of a curved session-state trajectory even though the order was balanced against a linear drift.

| resolution | reference A | C+Ultra A | C+Ultra B | reference B | original reported delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| 960x540 | 996.1 | 991.2 | 986.9 | 1001.4 | -0.97% |
| 2560x1440 | 672.8 | 671.9 | 663.3 | 678.2 | -1.17% |

At 2560x1440 the first adjacent pair was only `-0.13%`; the headline was created by the later `663.3` trough followed by a `678.2` reference recovery. At 960x540 both middle C+Ultra samples were below both reference endpoints. This pattern is not a stable executable effect.

## Controlled branch comparisons

Every row below used the official Windows driver, a fresh process, 30 seconds, `ANO_SHADOW_BUDGET=2`, realized the requested extent, emitted complete profiles, and was `FRONT`. The sequences were constructed so the branch indicator is orthogonal to linear run position.

| configuration | sequence | reference mean | C+Ultra mean | delta | paired 95% interval | FPS versus GPU ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 2560x1440, normal scene | R V V R / V R R V / R V V R | 682.4 | 683.8 | +0.21% | -0.58% to +0.99% | -0.904 |
| 960x540, normal scene | R V V R / V R R V | 1007.9 | 1008.8 | +0.09% | -0.43% to +0.61% | -0.977 |
| 2560x1440, shadow cache frozen after initialization | R V V R / V R R V | 770.1 | 770.4 | +0.04% | -0.48% to +0.55% | -0.743 |
| 960x540, authored motion speed zero with motion types retained | R V V R / V R R V | 1009.2 | 1008.2 | -0.11% | -0.65% to +0.44% | not used for the verdict |

The original `-1.17%` at 2560x1440 lies outside the new paired interval. The original `-0.97%` at 960x540 lies outside its new paired interval. The branch effect changes sign while the machine-state/GPU-time effect persists.

## Pass-level evidence

In the 960x540 normal-scene crossover, upload stayed `0.001 ms`, compute stayed `0.047–0.048 ms`, lighting stayed `0.203–0.204 ms`, and composite stayed `0.015 ms`. The shadow interval alone moved from `0.281` to `0.294 ms` while wall FPS moved from `1018.1` to `998.8`. Across the eight alternating runs, FPS and total GPU time correlate at `-0.977`.

One preserved 45-second normal-scene trace contains 224 profile windows. Within that single process, FPS and the shadow timestamp interval correlate at `-0.912`. A six-harmonic scan finds the strongest 8–16 second component at 12.31 seconds, near the demo point light's 12.57-second orbit, but authored motion is only a secondary component.

The demo has one shadow-casting point light with `ANO_MOTION_ORBIT` at `0.5 rad/s`. Its six cubemap faces are matrix-dirty every frame, and the benchmark budget admits two additional oldest content-dirty frusta, matching the observed `8.0` frusta. Freezing the shadow cache reduces variance, but zeroing every authored motion speed while retaining the same non-static invalidation still leaves a `-0.884` per-window FPS/shadow correlation. Rendering the same complete 26-frustum set every frame also exhibits intermittent slow runs. Therefore changing geometry and frustum selection contribute noise but do not explain the whole drift.

Disabling async Hi-Z also pins async light-cull and text work into the graphics frame. This materially changes the mean at 960x540 but does not remove the run spread or timestamp movement. Async queue overlap is not the root of the original branch delta.

## Clock and throttle evidence

NVIDIA telemetry sampled P-state, graphics clock, memory clock, power, temperature, utilization, and every thermal/power clock-event reason. Loaded runs spend nearly all samples at 2700 MHz graphics and 10501 MHz memory, with only the initial ramp at 2595 MHz. Temperatures remain 46–57°C. Software power cap, hardware thermal slowdown, hardware power brake, and software thermal slowdown are inactive.

Reported GPU Boost bins and thermal/power throttling are therefore rejected as the explanation. The faster and slower runs have the same sustained clock residency.

## Confirmed compositor mechanism

The benchmark is borderless windowed under Windows Desktop Window Manager. `FRONT` proves focus, not exclusive scan-out. Vulkan bottom-of-pipe timestamp differences measure device-clock elapsed time between timestamp commands; time lost to WDDM scheduling and another GPU context can therefore appear inside the pass interval where preemption occurs.

Windows GPU-engine counters during the exact 960x540 benchmark show:

| context | average engine utilization |
| --- | ---: |
| Anoptic graphics queue | 49.38% |
| Anoptic compute queue | 30.39% |
| DWM 3D | 13.44% |
| DWM copy | 0.09% |

Three clean repetitions of the invariant zero-motion, complete 26-frustum workload move in the same direction: `765.3 FPS` at `14.67%` DWM, `762.7 FPS` at `14.82%` DWM, and `760.3 FPS` at `15.09%` DWM. Engine GPU occupancy and elapsed GPU time rise as the compositor takes more of the shared adapter.

At native 3840x2160 the same measurement shows:

| context | average engine utilization |
| --- | ---: |
| Anoptic graphics queue | 80.53% |
| Anoptic compute queue | 15.76% |
| Anoptic copy queue | 1.57% |
| DWM 3D | 0.12% |

The native-size surface cuts DWM 3D occupancy from `13.44%` to `0.12%`, consistent with Windows independent flip/direct presentation. This matches the original result: the composited smaller windows showed wandering sub-percent deltas, while native 4K was only `-0.13%` with a `0.001 ms` GPU-time difference.

The driver's `wall/cap` classification uses only Anoptic's timestamped GPU passes. It does not include DWM or other WDDM contexts. A row can therefore read “CPU/present” while compositor scheduling still moves both the measured GPU interval and wall FPS. That omission made the initial result look like a host-code regression.

## Causal call

The negative result came from three nested effects:

1. Smaller benchmark windows are DWM-composited and share the adapter with a substantial compositor context.
2. WDDM scheduling movement appears in both wall throughput and the engine's elapsed timestamp intervals; most of it lands in the long shadow region.
3. The original two C+Ultra sweeps happened during the trough and the final reference sweep during recovery, so the session-state curve was labeled as a branch effect.

Authored motion and rotating shadow work add a smaller periodic component. Code layout, executable size, sustained GPU clocks, thermal/power throttling, fixed frustum selection, and async queue overlap were each tested and do not account for the branch-labeled negative.

## Corrected performance statement

Whole-frame renderer FPS is neutral within the resolution-specific paired intervals. C+Ultra is not measurably slower. The initial `-0.13%` to `-1.17%` table remains a valid record of those four sessions, but it is invalid as an estimate of the language-mode effect.

The canonical Windows driver now implements that correction through `--compare-exe`: six adjacent pairs per resolution by default, ABBA/BAAB balanced launch order, a paired Student-t 95% confidence interval, strict rejection of background/profile/render mismatches, and one-Hz aggregate DWM GPU exposure. A real A/B verification at 960x540 reports C+Ultra `+0.19%`, paired 95% CI `-0.32%` to `+0.70%`, with DWM exposure `13.78%` for reference and `13.74%` for C+Ultra. The verdict is neutral.

Native 4K remains the cleanest current GPU-path comparison. Function alignment and PGO should be evaluated only for independent optimization work, not as a response to this nonexistent regression.

## Artifacts

- 58 one-to-one diagnostic run records: `docs/benchmarks/2026-07-30-cplus-ultra-rootcause-*.md`
- Raw captures, GPU telemetry, preserved engine logs, and analysis: `scratch/hyper-c-codegen/rootcause-*`
- Reproducible reduction: `scratch/hyper-c-codegen/analyze_fps_rootcause.py`
- WDDM and clock-counter reduction: `scratch/hyper-c-codegen/wddm-rootcause-summary.json`
- Original four-sweep reduction: `scratch/hyper-c-codegen/fps-cplus-ultra-comparison-summary.json`
- Implemented paired-harness verification: `docs/benchmarks/2026-07-30-cplus-ultra-paired-harness-verification-960.md`
- Deterministic driver contract tests: `tools/perf/bench_fps_win64_test.py`

All temporary motion edits were reverted. The original reference and C+Ultra executables were restored after the diagnostic binaries were copied. No engine runtime fix was implemented because no C+Ultra FPS regression remained to fix; the faulty attribution path in the benchmark driver and methodology was fixed.
