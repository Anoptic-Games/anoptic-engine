# C++26 full reference comparison

Status: superseded by `docs/2026-07-30-cplus-ultra-whole-engine-report.md`. This file records the earlier C++-island pilot and its pre-migration measurements.

Date: 2026-07-30

Reference: `origin/module-audio` at `e2dbe3b84283bd250731eb3a94baad68046fae1c`

Variant: local uncommitted branch `codex/hyper-c-codegen` at the same commit plus the experimental diff

## Verdict

The C++26 variant is decisively faster in the two places deliberately specialized: the audio source mixer improves 3.98–10.12%, with 7.31% on the mixed 48-voice workload, and the transformed collation sorts improve 1.76–3.09%. It does not make the renderer faster: the stable 960x540–3840x2160 rows are 0.13–0.27% lower in average FPS, GPU time differs by 0.0000–0.0015 ms, and the 640x360 endpoint is visibly machine-state-sensitive. The rest of the CPU sweep is mixed, timer-quantized, or concurrent enough that it should be treated as regression coverage rather than evidence for a general C++ speed effect. Nothing measured presents a performance reason to retain C language mode for first-party engine translation units.

The safety result is separate from timing. In a 13-case malformed-public-input corpus, the reference completes four cases without a sanitizer finding and the variant completes eleven. Seven previously undefined inputs now receive defined fallbacks, no previously passing case regresses, and two concrete holes remain: `AnoScale.mode = 255` crosses into C++ as an invalid enum representation before the checked wrapper can validate it, and `AnoScale.tonic = 12` still indexes past a 12-entry table. That is a real, testable improvement from 4/13 to 11/13 on this corpus, not a percentage measure of “safety.”

The compile-time contract also does what it claims where it is applied: malformed keyed enum maps, non-bijective inverses, polymorphic or nontrivial asserted data, non-exhaustive enum switches, implicit fallthrough, missing returns, `throw`, RTTI use, and derivation from a marked `final` wrapper all fail compilation. It does not globally ban inheritance or virtual dispatch; explicit controls prove that both still compile when no concept or `final` declaration is applied.

## Headline comparison

| surface | observed result | interpretation |
| --- | --- | --- |
| Audio source/effect workloads | 3.98–10.12% faster; median row improvement 9.06% | Repeatable production win from once-per-block template dispatch and specialized inner loops |
| Collation sort workloads | 1.76–3.09% faster on the four transformed paths; qsort controls 0.48–0.49% faster | Repeatable production win from removing recursive accessor calls |
| General string operations | Mixed: stable cull rows are 12.50% faster and 12.14% slower; most other rows are within about 3%, with two noisier exceptions | Whole-program layout/code generation changed even where source did not; there is no uniform string-library win |
| Fixed-size compare and SID microbenchmarks | Mostly identical integer-nanosecond p50s | Timer resolution dominates the small deltas |
| Logger producer-tail tests | Exact ties through four producers; 2.95–5.58% slower at 8/16 producers | Contention-sensitive and quantized; changed formatting is on the drain side, so no producer speedup is claimed |
| Audio/render bridge | Mixer block +0.77%; tiny atomic rows move in 5–45 ns steps | Meaningful mixer row is neutral; atomic rows are below useful resolution |
| Long logger throughput benchmark | Individual rows swing from -23% to +89% and the built-in mutex controls move with them | Inconclusive by the benchmark's own “trend, not digits” warning |
| Full renderer FPS, 960x540–4K | Average FPS -0.13% to -0.27%; GPU time +0.0000 to +0.0015 ms | Neutral |
| Full renderer FPS, 640x360 | Two-pair median -2.06%, but adjacent pairs are +0.06% and -4.15% | Unstable CPU/present endpoint, not a branch result |
| Malformed interface corpus | Reference 4/13 clean; variant 11/13 clean | Seven defined fallbacks added, two residual failures |

There is no defensible single “overall speed” number across nanosecond string operations, million-voice audio blocks, contended loggers, and GPU frames. The branch wins where it deliberately exposes stable invariants to the optimizer and is neutral at whole-frame scope.

## Method

Both worktrees resolve to the exact fetched `origin/module-audio` tip above. The reference worktree is detached and source-clean except for test harness registration. The variant is the uncommitted experimental branch. The reference did not contain the new audio benchmark, so the exact variant `anotest_audiomixbench.c` and a disabled CTest registration were overlaid into the reference worktree; no reference engine source changed.

The CPU suite used matched WSL/Nix Clang 22.1.8 optimized builds. Every disabled CTest benchmark executable was invoked directly so its full metric table could be retained. Short and medium targets ran four balanced pairs per revision; the two long targets ran two balanced pairs. Even rounds ran reference then variant, odd rounds variant then reference. All 64 processes exited zero. Tables report the median of each executable's own p50 across process runs.

| CTest target | executable | runs per revision |
| --- | --- | ---: |
| `anotest_sortbench` | `anotest_sortbench` | 4 |
| `anotest_stropsbench` | `anotest_stropsbench` | 4 |
| `anotest_logstrbench` | `anotest_logstrbench` | 4 |
| `anotest_strbench` | `anotest_strbench` | 4 |
| `anotest_sidbench` | `anotest_sidbench` | 4 |
| `anotest_logtail` | `anotest_logtail` | 4 |
| `anotest_audiomixbench` | `anotest_audiomixbench` | 4 |
| `anotest_bridgebench` | `anotest_bridgebench` | 2 |
| `anotest_logbench` | `anotest_logbench` | 2 |

The renderer comparison used the required native Windows driver `tools/perf/bench_fps_win64.py`, not a custom runner. Four complete sweeps ran in the order reference A, variant A, variant B, reference B. Each sweep measured six display-derived resolutions for 30 seconds per point with `ANO_SHADOW_BUDGET=2`; all 24 rows were foreground-verified, realized the requested render size, and produced complete GPU profiles. FPS tables report the median of two sweeps per revision.

The interface corpus was compiled into two additional isolated WSL worktrees using identical probe source, confirmed by SHA-256 `c6a8bd22754c7506d18cc5615dad53b9eb633fe04601026a5148bd8ee16420c3`. Both used Clang 22.1.8 Debug, headless mode, ASan, and UBSan. Each malformed input ran in a fresh process with immediate sanitizer halt. Compile-fail probes used the exact CMake-generated C/C++ command lines from `compile_commands.json`.

Both reference and variant complete engine builds passed all 29 enabled tests on WSL and native Windows before measurement.

## Full CPU benchmark sweep

Negative variant delta is faster for nanosecond rows. Positive variant delta is faster for `M/s` logger rows.

### Sort

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| `anostr_sort` | 761,448 | 739,493 | ns p50 | -2.88% | 4 |
| `anostr_sort` presorted | 349,802 | 343,657 | ns p50 | -1.76% | 4 |
| `anostr_sort_idx` | 760,794 | 738,859 | ns p50 | -2.88% | 4 |
| `anostr_sym_sort` warm | 578,525 | 560,635 | ns p50 | -3.09% | 4 |
| qsort bytes floor | 653,169 | 650,029 | ns p50 | -0.48% | 4 |
| qsort plus collate baseline | 8,534,154 | 8,492,224 | ns p50 | -0.49% | 4 |

The four transformed paths all move in the expected direction and retain the earlier isolated result. The qsort controls are effectively flat.

### String operations

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| cull: no-op clean document | 446,326 | 390,546 | ns p50 | -12.50% | 4 |
| cull: whitespace plus punctuation, 1 MiB | 1,371,723 | 1,538,217 | ns p50 | +12.14% | 4 |
| find: 44-byte needle | 1,140,815 | 1,135,915 | ns p50 | -0.43% | 4 |
| find: common first byte | 3,198,936 | 3,202,117 | ns p50 | +0.10% | 4 |
| find: hit at far end | 51,219.5 | 51,164.5 | ns p50 | -0.11% | 4 |
| find: miss full scan | 33,814.5 | 34,270.0 | ns p50 | +1.35% | 4 |
| find: rare first byte | 46,920.0 | 51,109.5 | ns p50 | +8.93% | 4 |
| replace: UTF-8 e-acute | 842,962 | 874,932 | ns p50 | +3.79% | 4 |
| replace: delete all spaces | 3,093,388 | 3,012,514 | ns p50 | -2.61% | 4 |
| replace: grow dog to direwolf | 570,265 | 563,690 | ns p50 | -1.15% | 4 |
| replace: no match | 12,915.0 | 12,775.0 | ns p50 | -1.08% | 4 |
| replace: same-size dense | 999,346 | 993,081 | ns p50 | -0.63% | 4 |
| replace: same-size sparse | 660,794 | 656,604 | ns p50 | -0.63% | 4 |
| replace: shrink the to a | 648,999 | 642,969 | ns p50 | -0.93% | 4 |
| rune sort: 4 KiB mixed page | 146,438 | 146,594 | ns p50 | +0.11% | 4 |

The two cull distributions are stable and non-overlapping but move in opposite directions, despite unchanged cull source. This is a real executable-layout/code-generation effect, not evidence that the C++ contract uniformly helps or hurts general string operations. Rare-first-byte and UTF-8 rows vary materially within each four-run set and receive no regression claim.

### Log strings

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| log `anostr`, 1 producer | 49 | 49 | ns p50 | +0.00% | 4 |
| log `anostr`, 4 producers | 79 | 74 | ns p50 | -6.33% | 4 |
| log `anostr`, 8 producers | 239 | 249 | ns p50 | +4.18% | 4 |

These are integer-nanosecond, contention-sensitive measurements with opposite signs. They establish no systematic branch effect.

### Fixed-size strings

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| inline8 `anostr_compare` | 2 | 2 | ns p50 | +0.00% | 4 |
| inline8 `memcmp` | 2 | 2 | ns p50 | +0.00% | 4 |
| long32 `anostr_compare` | 2 | 2 | ns p50 | +0.00% | 4 |
| long32 `memcmp` | 6 | 6 | ns p50 | +0.00% | 4 |
| shared16 `anostr_compare` | 6 | 6 | ns p50 | +0.00% | 4 |
| shared16 `memcmp` | 4 | 5 | ns p50 | +25.00% | 4 |

The apparent 25% control change is one timer tick. The result is neutral.

### SID

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| `anostr_eq` chain | 10 | 9.5 | ns p50 | -5.00% | 4 |
| bulk `intern_find` plus symbol | 57 | 56 | ns p50 | -1.75% | 4 |
| dispatch `intern_find` plus symbol | 13 | 13 | ns p50 | +0.00% | 4 |
| hash plus equality confirm | 47 | 47 | ns p50 | +0.00% | 4 |
| hash64 plus SID switch | 13 | 13 | ns p50 | +0.00% | 4 |
| SID map integer-only | 7 | 7 | ns p50 | +0.00% | 4 |
| SID switch baked | 9 | 9 | ns p50 | +0.00% | 4 |
| sorted-SID binary search | 46 | 45.5 | ns p50 | -1.09% | 4 |
| `strcmp` chain | 27 | 27 | ns p50 | +0.00% | 4 |

The reducer keeps dispatch and bulk `intern_find` rows separate. At this resolution the result is neutral.

### Logger tail

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| enqueue, 1 producer | 49 | 49 | ns p50 | +0.00% | 4 |
| enqueue, 2 producers | 69 | 69 | ns p50 | +0.00% | 4 |
| enqueue, 4 producers | 79 | 79 | ns p50 | +0.00% | 4 |
| enqueue, 8 producers | 269 | 284 | ns p50 | +5.58% | 4 |
| enqueue, 16 producers | 509 | 524 | ns p50 | +2.95% | 4 |
| timer overhead | 9 | 9 | ns p50 | +0.00% | 4 |

The logger change specializes drain-side integer formatting, while this target measures producer enqueue and contention. The exact low-producer ties and small high-producer shifts support “no formatter speedup,” not a reliable regression.

### Audio mixer

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| 24 mono loop voices | 5,635,714 | 5,116,414 | ns p50 | -9.21% | 4 |
| 24 positional voices | 6,062,076 | 5,477,221 | ns p50 | -9.65% | 4 |
| 24 stereo loop voices | 3,321,836 | 2,985,748 | ns p50 | -10.12% | 4 |
| 24 tone voices | 7,193,531 | 6,907,358 | ns p50 | -3.98% | 4 |
| 48 mixed voices | 11,181,664 | 10,364,312 | ns p50 | -7.31% | 4 |
| filter bandpass | 5,817,118 | 5,293,268 | ns p50 | -9.01% | 4 |
| filter highpass | 5,823,558 | 5,302,097 | ns p50 | -8.95% | 4 |
| filter lowpass | 5,826,928 | 5,296,658 | ns p50 | -9.10% | 4 |

Filter rows include source mixing, so their improvement versus the original reference is the retained source-specialization gain. The later isolated filter-contract comparison remains neutral; this table does not reclassify validation itself as a speedup.

### Bridge

| metric | reference | C++26 | unit | variant delta | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: |
| acquire telemetry | 85 | 40 | ns p50 | -52.94% | 2 |
| mixer block CPU | 190,139 | 191,603 | ns p50 | +0.77% | 2 |
| publish listener | 20 | 20 | ns p50 | +0.00% | 2 |
| render-bridge acquire snapshot | 50 | 50 | ns p50 | +0.00% | 2 |
| render-bridge publish view | 35 | 40 | ns p50 | +14.29% | 2 |

The 190 µs mixer block is the only row large enough to interpret and is neutral at +0.77%. The atomic rows move in one or nine 5 ns timer ticks and are not credible 14–53% effects.

### Long logger benchmark

| metric | reference ring | C++26 ring | unit | variant delta | ref ring/control | C++26 ring/control | runs/revision |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| enqueue latency, 1 thread | 67.10 | 65.05 | ns | -3.06% | 3.32x | 3.55x | 2 |
| mixed, 1 producer | 0.41 | 0.32 | M/s | -21.95% | 7.78x | 6.01x | 2 |
| mixed, 2 producers | 0.91 | 0.86 | M/s | -5.49% | 15.30x | 14.56x | 2 |
| mixed, 4 producers | 1.74 | 1.40 | M/s | -19.25% | 24.96x | 18.59x | 2 |
| mixed, 8 producers | 1.64 | 2.41 | M/s | +47.40% | 26.93x | 38.36x | 2 |
| mixed, 16 producers | 1.57 | 2.59 | M/s | +64.44% | 27.20x | 42.28x | 2 |
| throughput, 1 producer | 16.09 | 13.91 | M/s | -13.58% | 28.43x | 22.94x | 2 |
| throughput, 2 producers | 14.25 | 11.65 | M/s | -18.25% | 19.19x | 16.47x | 2 |
| throughput, 4 producers | 16.27 | 12.51 | M/s | -23.09% | 23.62x | 16.06x | 2 |
| throughput, 8 producers | 12.35 | 16.62 | M/s | +34.57% | 12.86x | 13.84x | 2 |
| throughput, 16 producers | 10.83 | 12.14 | M/s | +12.05% | 6.93x | 7.28x | 2 |
| variable length, 1 producer | 0.79 | 0.82 | M/s | +4.43% | 7.69x | 8.19x | 2 |
| variable length, 2 producers | 1.74 | 1.67 | M/s | -4.31% | 15.51x | 13.78x | 2 |
| variable length, 4 producers | 3.29 | 3.16 | M/s | -3.95% | 25.82x | 21.47x | 2 |
| variable length, 8 producers | 2.96 | 5.58 | M/s | +88.68% | 19.41x | 36.30x | 2 |
| variable length, 16 producers | 4.21 | 3.93 | M/s | -6.64% | 31.12x | 30.07x | 2 |

Individual process values swing sharply inside a revision, for example variant throughput at two producers is 18.61 and 4.69 M/s. The mutex control and ring/control ratio also move. This target explicitly warns that results vary run to run; no branch throughput conclusion is taken from it.

## Full FPS sweep

Positive FPS delta favors the variant. Negative millisecond delta favors it.

| target | ref avg FPS | C++26 avg FPS | delta | pair A | pair B | ref p50 | C++26 p50 | ref 1% low | C++26 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 640x360 | 1085.05 | 1062.70 | -2.06% | +0.06% | -4.15% | 1083.90 | 1067.05 | 582.75 | 586.25 |
| 960x540 | 1043.15 | 1040.35 | -0.27% | -0.61% | +0.07% | 1045.15 | 1044.65 | 579.85 | 583.85 |
| 1280x720 | 967.20 | 965.40 | -0.19% | -0.08% | -0.29% | 963.45 | 961.85 | 546.70 | 547.85 |
| 1920x1080 | 838.10 | 836.20 | -0.23% | -0.25% | -0.20% | 840.00 | 839.65 | 505.70 | 505.55 |
| 2560x1440 | 700.60 | 699.45 | -0.16% | -0.09% | -0.24% | 703.75 | 702.40 | 469.10 | 467.60 |
| 3840x2160 | 594.60 | 593.85 | -0.13% | -0.12% | -0.13% | 598.75 | 597.90 | 563.05 | 561.65 |

| target | ref 0.1% low | C++26 0.1% low | ref max ms | C++26 max ms | ref GPU ms | C++26 GPU ms | GPU delta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 571.05 | 564.70 | 2.680 | 2.189 | 0.5190 | 0.5125 | -1.25% | CPU/present |
| 960x540 | 555.55 | 556.90 | 2.297 | 2.151 | 0.5345 | 0.5350 | +0.09% | CPU/present |
| 1280x720 | 537.80 | 537.55 | 3.261 | 2.875 | 0.6165 | 0.6175 | +0.16% | CPU/present |
| 1920x1080 | 497.55 | 497.95 | 2.925 | 2.643 | 0.7545 | 0.7550 | +0.07% | CPU/present |
| 2560x1440 | 449.90 | 447.10 | 2.783 | 3.325 | 0.9310 | 0.9325 | +0.16% | CPU/present |
| 3840x2160 | 554.30 | 554.20 | 3.250 | 3.317 | 1.6460 | 1.6460 | +0.00% | GPU |

The 4K GPU-bound row is exact within reported GPU precision and -0.13% in wall FPS. The 960x540–4K wall deltas are small, same-signed, and far below a practical change. The 640x360 result is rejected as causal because its two adjacent pair results disagree by 4.21 percentage points while GPU work is slightly lower on the variant.

The official per-run records are `docs/benchmarks/2026-07-30-hypercxx-reference-sweep-a.md`, `docs/benchmarks/2026-07-30-hypercxx-variant-sweep-a.md`, `docs/benchmarks/2026-07-30-hypercxx-variant-sweep-b.md`, and `docs/benchmarks/2026-07-30-hypercxx-reference-sweep-b.md`.

## Runtime interface rejection matrix

“Clean” means the process completed under ASan/UBSan and the probe's expected fallback assertion passed. “Sanitizer” means the process was terminated on a concrete undefined access/conversion. These tests characterize only the named interfaces and inputs.

| malformed public input | reference | C++26 variant | comparison |
| --- | --- | --- | --- |
| `ano_mode_intervals(ANO_MODE_NONE)` | UBSan: index -1 before a 7-row table | Clean: Ionian fallback | Improved |
| `ano_mode_intervals(ANO_MODE_COUNT)` | ASan: global buffer overflow | Clean: Ionian fallback | Improved |
| `AnoScale.mode = 255` in `ano_scale_name` | UBSan: index 255 before a 7-row table | UBSan: invalid `AnoMode` value 255 loaded before wrapper validation | Residual in both |
| `AnoScale.mode = ANO_MODE_COUNT` in `ano_scale_name` | UBSan: index 7 before a 7-row table | Clean: Ionian fallback | Improved |
| `AnoScale.tonic = 12` in `ano_scale_name` | UBSan: index 12 before a 12-row table | UBSan: index 12 before a 12-row table | Residual in both |
| filter mode `NaN` through `AnoAudioOfflineDesc` | UBSan: float outside range of `uint32_t` | Clean: filter mode becomes `OFF` before cast | Improved |
| filter mode `-1.0f` | UBSan: float outside range of `uint32_t` | Clean: filter mode becomes `OFF` before cast | Improved |
| filter mode `+infinity` | UBSan: float outside range of `uint32_t` | Clean: filter mode becomes `OFF` before cast | Improved |
| filter mode `1.0e30f` | UBSan: float outside range of `uint32_t` | Clean: filter mode becomes `OFF` before cast | Improved |
| filter mode `ANO_AUDIO_FILTER_COUNT` | Clean | Clean | Existing behavior retained |
| effect kind `UINT32_MAX` | Clean: normalized to no effect | Clean: normalized to no effect | Existing behavior retained |
| source kind `UINT32_MAX` | Clean: rejected to silence | Clean: rejected to silence | Existing behavior retained |
| music/synth name and patch IDs `UINT32_MAX` | Clean: empty/default fallbacks | Clean: empty/default fallbacks | Existing behavior retained |

The aggregate is reference 4 clean and 9 sanitizer terminations; variant 11 clean and 2 sanitizer terminations. The seven improvements are all boundary checks before indexing or conversion. The four unchanged-clean cases must not be credited to C++ because the reference already handled them.

## Compile-time rejection matrix

| probe compiled with generated project policy | result | guarantee or limitation |
| --- | --- | --- |
| Duplicate enum key, therefore one key also missing | Rejected during constant evaluation | Explicit keyed maps are complete and unique |
| Enum key equal to the `COUNT` sentinel | Rejected during constant evaluation | Registry keys are in the closed dense domain |
| Forward map with duplicate destination, then `invert_enum_map` | Rejected during constant evaluation | Generated inverse is bijective |
| `static_assert(ano::Data<Polymorphic>)` | Rejected | An asserted data boundary cannot be polymorphic |
| `static_assert(ano::Data<Nontrivial>)` | Rejected | An asserted data boundary must be trivially copyable |
| Enum switch omitting a named value even with `default` | Rejected by `-Werror=switch-enum` | C++ enum switches must be explicit and exhaustive |
| Unannotated switch fallthrough | Rejected by `-Werror=implicit-fallthrough` | Accidental fallthrough is a build failure |
| Non-void path without return | Rejected by `-Werror=return-type` | Missing return is a build failure |
| `throw 1` | Rejected by `-fno-exceptions` | C++ islands cannot introduce exception control flow |
| `typeid` on a polymorphic value | Rejected by `-fno-rtti` | RTTI use is disabled |
| Derive from `ano::Option<int>` | Rejected because the wrapper is `final` | Marked contract wrappers cannot be subclassed |
| Ordinary virtual method with no `ano::Data` assertion | Accepted | `-fno-rtti` does not disable virtual dispatch |
| Ordinary class inheritance with no contract | Accepted | There is no global compiler ban on inheritance |
| Same-size C positional table with the two semantic names swapped | Accepted on reference and variant | Cardinality alone cannot prove key/value association |

Every probe produced its expected result. The acceptance controls matter: the build policy is enforceable, but it is not broader than stated.

## Security guarantee comparison

| property | reference branch | C++26 variant |
| --- | --- | --- |
| Positional enum tables | Size can be asserted; semantic row swaps compile | Selected load-bearing registries use explicit keyed `consteval` maps that reject duplicates, omissions, and invalid keys |
| Cross-enum mapping | Manually maintained direction(s) can drift | Selected mapping has one forward table and a compile-time-proven inverse |
| Raw enum ingress | Ad hoc checks; music lookups index directly | Selected audio/music boundaries parse into `EnumValue` before indexing, except the demonstrated raw-255 crossing |
| Float-to-enum conversion | Selected filter path casts before validating and has UB for NaN/out-of-range | Range and NaN are rejected before conversion and become `OFF` |
| Data representation | C structs are plain by language convention | Selected cross-language/private values additionally assert standard layout, trivial copying, and non-polymorphism |
| Switch/control-flow policy | Existing C warnings and tests | C++ target makes non-exhaustive enum switches, implicit fallthrough, and missing returns errors |
| Exceptions and RTTI | Not applicable to C-only reference | Explicitly disabled in C++ islands and compile-fail tested |
| Inheritance and virtual dispatch | Not expressible in C | Available in the language; absent from the current implementation, locally blocked by `final`/`Data`, but not globally prohibited |
| Public caller types | C enums, integers, pointers, and plain structs | Deliberately unchanged C ABI; stronger types are implementation-side and do not make arbitrary caller integers unrepresentable |
| General memory safety | Raw pointer, extent, lifetime, union-tag, arithmetic, and concurrency obligations remain | The same obligations remain outside the specifically converted contracts |

The variant is materially safer in the code it actually converts, but it is not a memory-safe language boundary and it is not a global “no OOP” dialect. `ano::Data` is an opt-in proof, `final` is a per-class prohibition, and `[[assume]]` is not validation: it is safe only after every incoming path has established the invariant. A missed ingress check can make an assumption more dangerous, so every assumption needs a malformed-input regression test.

The two current merge-blocking interface fixes are straightforward in shape: validate `AnoScale.mode` as an integer before ever materializing an invalid C++ enum value, and bounds-check or normalize `AnoScale.tonic` before indexing. The next broader safety targets remain tagged-union commands/events and raw pointer-plus-count interfaces; this experiment does not claim to cover them.

## Artifacts and validation

- Raw balanced CPU process record: `scratch/hyper-c-codegen/full-bench-runs.json`
- Reduced CPU data with every per-process sample: `scratch/hyper-c-codegen/full-bench-summary.json`
- Raw FPS driver captures: `scratch/hyper-c-codegen/fps-reference-a.txt`, `fps-variant-a.txt`, `fps-variant-b.txt`, and `fps-reference-b.txt`
- Reduced FPS data: `scratch/hyper-c-codegen/fps-comparison-summary.json`
- Runtime sanitizer matrix: `scratch/hyper-c-codegen/security-rejections.json`
- Compile-contract matrix: `scratch/hyper-c-codegen/compile-contract-rejections.json`
- Identical interface probe source in isolated worktrees: `/home/pyrus/workspace/worktrees/anoptic-hyper-c-reference/tests/anotest_interface_reject.c` and `/home/pyrus/workspace/worktrees/anoptic-hyper-c-security-variant/tests/anotest_interface_reject.c`

Validation totals: 29/29 enabled WSL tests on each revision, 29/29 enabled native Windows tests on each revision, 64/64 CPU benchmark processes successful, 24/24 FPS points complete and foreground, 26/26 sanitizer probe processes classified, and 15/15 compile probes matching their expected accept/reject result. No commit or push was made.

## Recommendation

Adopt strict C++26 language mode for every first-party host-engine translation unit. The pilot islands are the proof-of-toolchain stage, not the final architecture. Third-party C dependencies may remain C, and first-party exports may retain `extern "C"` where a stable external ABI is useful; a C ABI does not require a C implementation.

This is a compiler and language-mode migration, not an object-oriented redesign. Preserve the SoA architecture, plain layouts, explicit allocation, platform abstraction, procedural module surfaces, and data-oriented algorithms. Use namespaces, lambdas, concepts, templates, `constexpr`/`consteval`, typed enums, reflection as the deployed compiler gains it, and stronger non-owning value types throughout the engine. Keep `-fno-exceptions`, `-fno-rtti`, exhaustive enum switches, fallthrough and return errors, no C++ standard-library runtime dependency, and the measured code-generation admission rule. Add an AST policy check if inheritance and virtual members are to be prohibited globally.

Port mechanically before redesigning: make each first-party translation unit compile as C++26 with behavior, layout, and ABI held constant, keep the full tests and benchmark sweep green, then replace raw tables, tags, and runtime-invariant branches with compile-time contracts in ranked passes. Treat the measured audio and collation gains as real, the renderer as neutral, the general string cull split as a follow-up code-layout investigation, and the logger numbers as inconclusive.

Fix the two residual interface failures before treating the current branch as ready. The final decision is nevertheless yes: Anoptic should use the C++26 compiler for the engine.
