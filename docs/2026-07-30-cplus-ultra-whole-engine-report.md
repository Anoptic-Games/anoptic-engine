# C+Ultra whole-engine C++26 report

Date: 2026-07-30

Branch: `codex/hyper-c-codegen`

Reference: fetched `origin/module-audio` at `e2dbe3b84283bd250731eb3a94baad68046fae1c`

## Decision

Use the C++26 compiler for every first-party host-engine and test translation unit.

Keep the `.c` and `.h` names, the SoA data model, procedural APIs, plain structs, direct control flow, and public C ABI. Keep third-party C dependencies in C language mode. Do not convert the engine into an object hierarchy, do not introduce exception control flow or RTTI, and do not take a C++ standard-library runtime dependency. This is C+Ultra: C-shaped systems code with C++26 compile-time machinery and stricter type checking.

This branch implements that decision. It is not a set of C++ islands: the final WSL compilation database contains 146 first-party `.c` compile commands, all 146 use `clang++ -std=gnu++26`, all carry `-fno-exceptions -fno-rtti`, and zero escape the policy. The same graph contains 84 third-party `.c` compile commands, all 84 remain C.

The performance call is also yes. Controlled reruns find no whole-frame FPS regression: C+Ultra is +0.21% at 2560x1440 and +0.09% at 960x540, with paired intervals containing zero. The initial negative table was root-caused to DWM-composited small-window scheduling combined with run order, not code layout. The loaded text segment is 0.42–0.47% smaller, targeted audio work is 3.80–9.53% faster, the main sort paths are 1.50–2.94% faster, and the public/runtime safety contract is materially stronger. There is no evidence that “C++ made the instruction cache larger”; it did not.

## What the branch does

- `ano_enable_cplus_ultra(target)` marks first-party `.c` sources as C++ and requires GNU C++26, while external C targets remain untouched.
- Every first-party C++ translation unit is built without exceptions and RTTI.
- Clang makes format mismatches, non-exhaustive enum switches, implicit fallthrough, missing returns, C-only designator forms, reordered designators, dangling references, C++ VLAs, and writable string literals fatal.
- Public module headers retain C23 compatibility and wrap their callable surface in `extern "C"` for C++ consumers and implementations.
- Linux links with `-nostdlib++`; Linux and Windows binary audits find no `libstdc++`, `libc++`, `libsupc++`, or equivalent C++ runtime import.
- `ano::Data<T>` accepts only standard-layout, trivially-copyable, non-polymorphic data where the contract is applied.
- `ano::EnumValue`, `ano::EnumTable`, `ano::EnumFlags`, and consteval registry builders prove dense enum ranges, unique keys, complete tables, and valid inversions at compile time.
- `ano::Option<T>` is a final, non-owning checked value wrapper used by the enum machinery.
- Typed allocation helpers bind element type, count, `sizeof(T)`, and overflow checking at one call site without introducing ownership or destructors.
- First-party atomic use goes through the engine’s compiler-builtin wrapper rather than `std::atomic` or a C++ runtime.
- Audio source kinds and FX modes, string collation, music modes and vocabulary, and synth patch registries use compile-time specialization or validated enum tables.
- Renderer, string, logger, filesystem, thread, text, audio, synth, music, UI, and test `.c` files now compile under the same C++26 contract.
- C++-illegal VLAs were replaced with checked typed allocations; implicit `void *` conversions and implicit integer-to-enum conversions were made explicit at validated boundaries.
- Music snapshot state is zero-initialized in place before field assignment, eliminating nondeterministic padding discovered by cross-thread snapshot comparison.
- Vulkan structure initialization is declaration-ordered and typed; invalid sentinel and allocation conversions are explicit.
- The raw `AnoScale` boundary canonicalizes tonic bytes before indexing, validates mode bytes before constructing an enum, and safely wraps negative degrees without negative table indexes.

No OOP redesign was performed. The new helper classes are small value contracts and are `final`; the engine remains procedural and data-oriented.

## Build and validation

| surface | result |
| --- | --- |
| Native Windows Release + O3 + ThinLTO | 29/29 enabled tests passed |
| Native Vulkan | lifecycle, memory, texture-domain, shadow, and component tests passed on the live RTX 4090 |
| WSL/Nix Release + O3 + ThinLTO | 29/29 enabled tests passed; lifecycle, memory, and texture-domain tests skipped because WSL exposed no suitable renderer device |
| WSL/Nix Debug before the final linkage-only header pass | 31/31 enabled tests passed |
| WSL ASan/UBSan CPU/non-window surface | 25 first-party tests passed |
| WSL ASan/UBSan no-device Vulkan surface | five processes report third-party Fontconfig/Pango/libdecor shutdown leaks after the renderer cleanly reports no suitable GPU; no first-party sanitizer trace |
| Runtime hostile-interface matrix | reference 4/13 clean; C+Ultra 13/13 clean |
| Compile-contract matrix | 31/31 expected accept/reject outcomes |
| CPU benchmark processes | 64/64 successful |
| Official FPS points | 82/82 `FRONT`, requested extent realized, GPU profile complete: 24 initial sweep points plus 58 root-cause points |
| Public headers | all 18 compile together as C23 and C++26 with the build’s normal POSIX and allocator include contract |

The sanitizer caveat is environmental, not hidden: WSL’s Vulkan skip path creates a Wayland/libdecor window before device selection fails, and LeakSanitizer reports allocations retained by Fontconfig/Pango/libdecor at process shutdown. The same native Vulkan tests pass with a real device. Leak detection was not silently disabled for the reported full sanitizer run.

## Binary and ABI

### Loaded sections

| platform | metric | reference | C+Ultra | delta |
| --- | --- | ---: | ---: | ---: |
| Linux | text | 2,172,091 B | 2,163,056 B | -9,035 B (-0.42%) |
| Linux | data | 66,808 B | 66,888 B | +80 B (+0.12%) |
| Linux | bss | 321,217 B | 322,161 B | +944 B (+0.29%) |
| Linux | text+data+bss | 2,560,116 B | 2,552,105 B | -8,011 B (-0.31%) |
| Linux | file | 2,438,128 B | 2,446,328 B | +8,200 B (+0.34%) |
| Windows | text | 1,570,358 B | 1,563,014 B | -7,344 B (-0.47%) |
| Windows | data | 521,114 B | 520,425 B | -689 B (-0.13%) |
| Windows | loaded total reported by `llvm-size` | 2,091,472 B | 2,083,439 B | -8,033 B (-0.38%) |
| Windows | file | 2,308,096 B | 2,319,872 B | +11,776 B (+0.51%) |

The on-disk files are slightly larger because symbol and name-table representation changed, but executable text is smaller on both platforms. File size is not instruction-cache footprint.

Linux `readelf -d` lists libc, libm, libdl, pthread, rt, atomic, Vulkan, and the loader; no C++ runtime is needed. Windows imports the Universal CRT, Win32 system DLLs, and Vulkan; no C++ runtime DLL is imported.

All callable public headers now carry C language linkage under C++. Representative final Linux symbols are the raw names `ano_busywait`, `ano_fs_sync`, `ano_text_init`, `ano_thread_join`, and `ano_ui_clip`. Private functions declared only in `src/` may be mangled, which is intentional and not an ABI promise.

## CPU benchmark method

Both WSL worktrees resolve to `e2dbe3b84283bd250731eb3a94baad68046fae1c`. The reference changes only register the identical benchmark harness where the base lacked it; no reference engine implementation changed.

The suite invokes every disabled `bench` CTest executable directly from matched Clang 22.1.8 O3 + ThinLTO builds. Short and medium targets run four balanced rounds per revision; bridge and the long logger target run two. Even rounds run reference then C+Ultra and odd rounds run C+Ultra then reference. Each table value is the median of the executable’s reported p50 across process runs.

For `ns p50`, a negative delta favors C+Ultra. For `M/s`, a positive delta favors C+Ultra.

### Sort

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| `anostr_sort` | 777,853 ns | 760,343 ns | -2.25% |
| `anostr_sort` presorted | 353,652 ns | 353,367 ns | -0.08% |
| `anostr_sort_idx` | 767,388 ns | 755,873 ns | -1.50% |
| `anostr_sym_sort` warm | 592,290 ns | 574,860 ns | -2.94% |
| qsort bytes floor | 672,099 ns | 663,324 ns | -1.31% |
| qsort + collation baseline | 8,803,840 ns | 8,745,284 ns | -0.67% |

### String operations

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| cull no-op clean document | 452,486 ns | 452,681 ns | +0.04% |
| cull whitespace + punctuation, 1 MiB | 1,401,502 ns | 1,404,022 ns | +0.18% |
| find 44-byte needle | 1,170,090 ns | 1,161,160 ns | -0.76% |
| find common first byte | 3,259,281 ns | 3,260,401 ns | +0.03% |
| find hit at far end | 54,489.5 ns | 40,999.5 ns | -24.76% |
| find miss, full scan | 33,914.5 ns | 33,899.5 ns | -0.04% |
| find rare first byte | 49,499.5 ns | 52,829.5 ns | +6.73% |
| replace UTF-8 needle | 868,412 ns | 870,732 ns | +0.27% |
| replace delete all spaces | 3,148,517 ns | 3,129,652 ns | -0.60% |
| replace grow | 585,530 ns | 577,894 ns | -1.30% |
| replace no match | 14,415 ns | 14,875 ns | +3.19% |
| replace same-size dense | 1,016,632 ns | 1,021,921 ns | +0.52% |
| replace same-size sparse | 681,524 ns | 679,084 ns | -0.36% |
| replace shrink | 669,364 ns | 658,859 ns | -1.57% |
| rune sort, 4 KiB mixed page | 149,173 ns | 149,264 ns | +0.06% |

The large far-end and rare-byte deltas are opposed branches of a very short scan and should not be added together or generalized. Bulk transform rows are within roughly 1.6%, with no general C++ penalty.

### Log strings

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| `anostr` at 1 producer | 49 ns | 49 ns | 0.00% |
| `anostr` at 4 producers | 69 ns | 74 ns | +7.25% |
| `anostr` at 8 producers | 229 ns | 259 ns | +13.10% |

These p50s are quantized to the timer and move with contention state.

### Fixed-size strings

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| inline8 `anostr_compare` | 2 ns | 2 ns | 0.00% |
| inline8 `memcmp` | 2 ns | 2 ns | 0.00% |
| long32 `anostr_compare` | 2 ns | 2 ns | 0.00% |
| long32 `memcmp` | 6 ns | 6 ns | 0.00% |
| shared16 `anostr_compare` | 6 ns | 6 ns | 0.00% |
| shared16 `memcmp` | 5 ns | 5 ns | 0.00% |

### SID

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| `anostr_eq` chain | 10 ns | 10 ns | 0.00% |
| bulk intern find + symbol | 113.0 ns | 113.5 ns | +0.44% |
| dispatch intern find + symbol | 13 ns | 13 ns | 0.00% |
| hash + equality confirm | 95.0 ns | 92.5 ns | -2.63% |
| hash64 + SID switch | 14 ns | 13 ns | -7.14% |
| SID map, integer only | 9.0 ns | 9.5 ns | +5.56% |
| SID switch, baked | 9 ns | 9 ns | 0.00% |
| sorted SID binary search | 64 ns | 54 ns | -15.62% |
| `strcmp` chain | 28 ns | 28 ns | 0.00% |

Most SID rows are one timer tick apart or identical; they demonstrate no systematic regression.

### Logger tail

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| enqueue at 1 producer | 49 ns | 59 ns | +20.41% |
| enqueue at 2 producers | 69 ns | 79 ns | +14.49% |
| enqueue at 4 producers | 79 ns | 79 ns | 0.00% |
| enqueue at 8 producers | 334 ns | 269 ns | -19.46% |
| enqueue at 16 producers | 504 ns | 504 ns | 0.00% |
| timer overhead | 9 ns | 9 ns | 0.00% |

The one- and two-producer differences are one 10 ns timer quantum, while the eight-producer result moves in the opposite direction. This is regression coverage, not a stable language-mode effect.

### Audio mixer

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| 24 mono loop voices | 5,696,979 ns | 5,164,033 ns | -9.35% |
| 24 positional voices | 6,136,720 ns | 5,612,509 ns | -8.54% |
| 24 stereo loop voices | 3,371,480 ns | 3,050,252 ns | -9.53% |
| 24 tone voices | 7,287,280 ns | 7,010,097 ns | -3.80% |
| 48 mixed voices | 11,325,368 ns | 10,548,470 ns | -6.86% |
| filter bandpass | 5,876,727 ns | 5,367,716 ns | -8.66% |
| filter highpass | 5,863,038 ns | 5,362,227 ns | -8.54% |
| filter lowpass | 5,861,478 ns | 5,348,637 ns | -8.75% |

This is the clearest performance win. Compile-time source-kind and filter-mode specialization removes repeated runtime selection from the inner work while keeping the public command/data API unchanged.

### Bridge

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| acquire telemetry | 125 ns | 125 ns | 0.00% |
| mixer `blockCpu` | 195,528 ns | 196,053 ns | +0.27% |
| publish listener | 20 ns | 20 ns | 0.00% |
| render bridge acquire snapshot | 70 ns | 80 ns | +14.29% |
| render bridge publish view | 65 ns | 65 ns | 0.00% |

The snapshot row is one 10 ns timer quantum; the material bridge workload is +0.27%.

### Long logger control

| metric | reference | C+Ultra | delta |
| --- | ---: | ---: | ---: |
| enqueue latency, 1 thread | 63.55 ns | 65.20 ns | +2.60% |
| mixed at 1 producer | 0.39 M/s | 0.37 M/s | -5.13% |
| mixed at 2 producers | 0.84 M/s | 0.74 M/s | -13.02% |
| mixed at 4 producers | 1.63 M/s | 1.40 M/s | -14.42% |
| mixed at 8 producers | 1.84 M/s | 2.20 M/s | +19.24% |
| mixed at 16 producers | 1.72 M/s | 2.30 M/s | +33.33% |
| throughput at 1 producer | 18.29 M/s | 17.95 M/s | -1.86% |
| throughput at 2 producers | 16.40 M/s | 18.42 M/s | +12.32% |
| throughput at 4 producers | 16.11 M/s | 18.09 M/s | +12.29% |
| throughput at 8 producers | 15.31 M/s | 15.12 M/s | -1.21% |
| throughput at 16 producers | 13.93 M/s | 10.58 M/s | -24.05% |
| variable length at 1 producer | 0.82 M/s | 0.82 M/s | -0.61% |
| variable length at 2 producers | 1.75 M/s | 1.73 M/s | -1.15% |
| variable length at 4 producers | 3.17 M/s | 3.40 M/s | +7.10% |
| variable length at 8 producers | 2.84 M/s | 5.07 M/s | +78.38% |
| variable length at 16 producers | 3.53 M/s | 4.74 M/s | +34.28% |

The benchmark’s own mutex/control ratios move substantially between runs, and neighboring producer counts reverse direction. These results are inconclusive for the language decision and should not be averaged into a headline number.

## FPS benchmark method

The native Windows comparison uses the required `tools/perf/bench_fps_win64.py` driver, not a custom runner. Four complete sweeps ran in the order reference A, C+Ultra A, C+Ultra B, reference B. Every sweep measured the six display-derived extents for 30 seconds per point with `ANO_SHADOW_BUDGET=2`; all 24 rows were `FRONT`, realized the requested render size, and emitted a complete GPU profile. The table reports the median of two sweeps per revision. This table is retained as the initial observation, not the final language-effect estimate.

Positive FPS delta favors C+Ultra. Negative millisecond delta favors C+Ultra.

| target | ref avg FPS | C+Ultra avg FPS | delta | pair A | pair B | ref p50 | C+Ultra p50 | ref 1% low | C+Ultra 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 640x360 | 1038.80 | 1037.40 | -0.13% | +0.13% | -0.39% | 1041.50 | 1038.00 | 567.10 | 569.35 |
| 960x540 | 998.75 | 989.05 | -0.97% | -0.49% | -1.45% | 1001.45 | 989.30 | 547.15 | 543.20 |
| 1280x720 | 946.40 | 942.00 | -0.46% | +0.06% | -0.99% | 954.40 | 948.35 | 558.85 | 559.65 |
| 1920x1080 | 830.10 | 826.80 | -0.40% | -0.11% | -0.68% | 837.80 | 835.50 | 515.95 | 521.20 |
| 2560x1440 | 675.50 | 667.60 | -1.17% | -0.13% | -2.20% | 677.10 | 671.10 | 442.00 | 437.65 |
| 3840x2160 | 592.10 | 591.35 | -0.13% | +0.10% | -0.35% | 595.60 | 594.40 | 558.50 | 556.80 |

| target | ref 0.1% low | C+Ultra 0.1% low | ref max ms | C+Ultra max ms | ref GPU ms | C+Ultra GPU ms | GPU delta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 548.65 | 548.45 | 2.258 | 3.200 | 0.5090 | 0.5085 | -0.10% | CPU/present |
| 960x540 | 534.90 | 532.90 | 2.930 | 2.566 | 0.5605 | 0.5695 | +1.61% | CPU/present |
| 1280x720 | 537.65 | 530.55 | 2.649 | 2.425 | 0.6020 | 0.5955 | -1.08% | CPU/present |
| 1920x1080 | 498.65 | 496.50 | 2.761 | 3.011 | 0.7335 | 0.7290 | -0.61% | CPU/present |
| 2560x1440 | 432.95 | 426.85 | 3.592 | 3.031 | 0.9555 | 0.9595 | +0.42% | CPU/present |
| 3840x2160 | 547.30 | 547.45 | 3.883 | 2.896 | 1.6555 | 1.6565 | +0.06% | GPU |

The initial renderer result is not a stable branch effect. At 2560x1440 pair A was only -0.13%; the headline was driven by C+Ultra B falling to 663.3 FPS before reference B recovered to 678.2. At 960x540 both middle C+Ultra sweeps were below both reference endpoints. The ABBA order cancels a linear trend but not a curved trough centered on the two middle runs.

Fifty-eight additional official runs isolated the cause. All were `FRONT`, used fresh processes, realized the requested extent, and retained the normal 30-second duration except one explicitly recorded 45-second trace.

| configuration | sequence | reference mean | C+Ultra mean | delta | paired 95% interval | FPS versus GPU ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 2560x1440 normal scene | R V V R / V R R V / R V V R | 682.4 | 683.8 | +0.21% | -0.58% to +0.99% | -0.904 |
| 960x540 normal scene | R V V R / V R R V | 1007.9 | 1008.8 | +0.09% | -0.43% to +0.61% | -0.977 |
| 2560x1440 shadow cache frozen | R V V R / V R R V | 770.1 | 770.4 | +0.04% | -0.48% to +0.55% | -0.743 |
| 960x540 authored motion speed zero, motion types retained | R V V R / V R R V | 1009.2 | 1008.2 | -0.11% | -0.65% to +0.44% | not used for the verdict |

The original -1.17% and -0.97% estimates lie outside the corresponding controlled intervals. The branch effect changes sign; the session-state/GPU-time effect does not.

At 960x540 upload, compute, lighting, and composite remain effectively fixed while the shadow timestamp interval moves 0.281–0.294 ms and wall FPS moves 1018.1–998.8. A preserved 45-second trace gives a -0.912 within-process correlation between FPS and shadow time. Freezing the cache, zeroing authored motion while preserving invalidation, rendering the same complete 26-frustum set, and disabling async queues show that motion phase, frustum selection, and queue overlap contribute or change the mean but do not create a stable branch delta.

NVIDIA telemetry rejects sustained clock-bin or thermal/power throttling: loaded runs spend nearly all samples at 2700 MHz graphics and 10501 MHz memory, temperatures remain 46–57°C, and every software-power, hardware-thermal, hardware-power-brake, and software-thermal reason is inactive.

The confirmed differentiator is Windows composition. At 960x540, WDDM counters show Anoptic graphics at 49.38%, Anoptic compute at 30.39%, and DWM 3D at 13.44%. Three clean repetitions of the invariant zero-motion, complete 26-frustum workload move from 765.3 FPS at 14.67% DWM through 762.7 FPS at 14.82% DWM to 760.3 FPS at 15.09% DWM. At native 3840x2160, DWM 3D falls to 0.12% while Anoptic graphics rises to 80.53%, consistent with independent flip/direct presentation. `FRONT` proves focus, not exclusive scan-out. Vulkan elapsed timestamps include time between timestamp commands while the context is descheduled, so DWM/WDDM movement appears inside the long shadow interval and in wall FPS. The driver's `wall/cap` uses only Anoptic's timestamps and does not account for compositor contexts.

The corrected renderer result is neutral. Code layout and alignment are not the root cause: executable text is smaller, balanced reruns do not preserve a negative sign, and the apparent penalty disappears when branch labels are separated from compositor state. The full causal record is in `docs/2026-07-30-cplus-ultra-fps-root-cause.md`.

## Runtime hostile-interface matrix

“Clean” means the case returned its documented safe fallback/rejection without ASan or UBSan. It does not mean the caller byte became unrepresentable at the C ABI.

| case | C reference | C+Ultra |
| --- | --- | --- |
| mode `NONE` used as table key | UBSan negative index | clean Ionian fallback |
| mode `COUNT` used as table key | ASan global overflow | clean Ionian fallback |
| `AnoScale.mode = UINT8_MAX` | UBSan index 255 | clean validated fallback |
| `AnoScale.mode = COUNT` | UBSan index 7 | clean validated fallback |
| `AnoScale.tonic = 12` | UBSan index 12 | clean canonicalized tonic |
| filter mode NaN | UBSan float-to-unsigned conversion | clean rejection/fallback |
| filter mode -1 | UBSan float-to-unsigned conversion | clean rejection/fallback |
| filter mode infinity | UBSan float-to-unsigned conversion | clean rejection/fallback |
| filter mode `1.0e30` | UBSan float-to-unsigned conversion | clean rejection/fallback |
| filter mode one past count | clean | clean |
| unknown effect kind | clean | clean |
| unknown source kind | clean | clean silent source |
| out-of-range patch names | clean | clean empty/default result |

The exact result is 4/13 clean on the reference and 13/13 clean on C+Ultra.

## Compile-time contract matrix

| probe | C reference | C+Ultra |
| --- | --- | --- |
| duplicate enum-table key | not applicable | rejected during constant evaluation |
| out-of-range enum-table key | not applicable | rejected during constant evaluation |
| duplicate inverse-map destination | not applicable | rejected during constant evaluation |
| polymorphic type presented as `ano::Data` | not applicable | rejected by concept assertion |
| non-trivial type presented as `ano::Data` | not applicable | rejected by concept assertion |
| non-exhaustive enum switch | warning/normal C policy | rejected by `-Werror=switch-enum` |
| implicit switch fallthrough | warning/normal C policy | rejected by `-Werror=implicit-fallthrough` |
| missing non-void return | warning/normal C policy | rejected by `-Werror=return-type` |
| `throw` | not applicable | rejected because exceptions are disabled |
| `typeid` | not applicable | rejected because RTTI is disabled |
| derivation from a contract type marked `final` | not applicable | rejected |
| variable-length array | accepted | rejected |
| writable pointer to string literal | accepted | rejected |
| out-of-declaration-order designators | accepted | rejected |
| narrowing `300` into `uint8_t` aggregate field | accepted with truncation warning | rejected |
| `%d` with a `long` argument | accepted with warning | rejected |
| return address of a stack local | accepted with warning | rejected |
| implicit `void *` to typed pointer | accepted | rejected |
| implicit integer to enum | accepted | rejected |
| unconstrained polymorphic class | not applicable | accepted |
| unconstrained inheritance | not applicable | accepted |
| positionally initialized table whose rows were semantically reordered | accepted | accepted |

The harness executes 31 side-specific cases and all 31 match the expected outcome.

## What is and is not guaranteed

The compiler contract is strictly safer in the tested dimensions; performance percentages are not “safety scores.” C+Ultra moves invalid enum registries, layout/category violations, narrowing, several implicit conversions, invalid format strings, fallthrough, missing returns, VLA stack sizing, writable literals, exception/RTTI use, and selected dangling-address patterns to compile failures. Validated runtime boundaries eliminate nine sanitizer-visible failures in the hostile-value matrix.

The branch is not memory-safe in the Rust sense. Raw pointers, explicit lifetimes, aliasing, integer arithmetic, unions, and manual allocation remain available. C++26 language mode does not automatically prove them.

There is no compiler option that globally disables user-defined inheritance or virtual functions without also breaking ordinary language/library mechanisms. `-fno-rtti` and `-fno-exceptions` do not ban class polymorphism. The probes deliberately prove that unconstrained inheritance and polymorphism still compile. The enforceable choices are to mark owned contract types `final`, require `ano::Data` at important data boundaries, and add an AST/clang-tidy policy test if a source-wide ban is desired.

Plain positional initialization can still compile after a table’s semantic row order changes. The solution is not another warning flag; it is the explicit-key consteval enum-map form used by the new registries.

Public callers still pass C-compatible integers, enums, pointers, and structs. Strong internal types validate those representations at module boundaries without breaking the public ABI. If a value must be impossible to construct even for external callers, the ABI itself must change to opaque handles or checked constructors.

C++26 reflection was not made a build dependency in this pass. The branch establishes the language mode and compile-time registry patterns needed to adopt standardized reflection as soon as the pinned Clang toolchain’s implementation is stable enough for production.

## Ranked next positions

1. Continue replacing raw tag-plus-index pairs at audio, renderer, and music module boundaries with validated raw-to-strong conversions performed once on entry.
2. Convert remaining parallel enum/name/function tables into explicit-key consteval registries so completeness and uniqueness are proven independently of declaration order.
3. Introduce small unit/index wrappers for frame IDs, slot IDs, byte counts, sample counts, texture IDs, and entity ranges inside modules, erasing them only at the C ABI.
4. Add an AST policy test for inheritance, `virtual`, and polymorphic class definitions outside an explicit allowlist if the project wants a global no-OOP rule.
5. Use C++26 reflection for registry generation only after the pinned compiler supports the chosen facility without an unstable compatibility layer.
6. Use the implemented Windows `--compare-exe` path for small-window FPS comparisons: six adjacent pairs per resolution by default, ABBA/BAAB balanced order, paired 95% confidence, and DWM exposure. Treat function alignment and PGO as independent optimization work, not a fix for this nonexistent regression.

## Artifacts

- Final decision report: `docs/2026-07-30-cplus-ultra-whole-engine-report.md`
- FPS root-cause report: `docs/2026-07-30-cplus-ultra-fps-root-cause.md`
- Official FPS run records: `docs/benchmarks/2026-07-30-cplus-ultra-reference-sweep-a.md`, `docs/benchmarks/2026-07-30-cplus-ultra-variant-sweep-a.md`, `docs/benchmarks/2026-07-30-cplus-ultra-variant-sweep-b.md`, and `docs/benchmarks/2026-07-30-cplus-ultra-reference-sweep-b.md`
- Root-cause run records: 58 one-to-one files matching `docs/benchmarks/2026-07-30-cplus-ultra-rootcause-*.md`
- Paired-harness implementation verification: `docs/benchmarks/2026-07-30-cplus-ultra-paired-harness-verification-960.md`
- Raw CPU processes: `scratch/hyper-c-codegen/full-bench-runs.json`
- Reduced CPU tables: `scratch/hyper-c-codegen/full-bench-summary.json` and `scratch/hyper-c-codegen/full-bench-summary.md`
- Raw FPS captures: `scratch/hyper-c-codegen/fps-cplus-ultra-reference-a.txt`, `fps-cplus-ultra-variant-a.txt`, `fps-cplus-ultra-variant-b.txt`, and `fps-cplus-ultra-reference-b.txt`
- Reduced FPS tables: `scratch/hyper-c-codegen/fps-cplus-ultra-comparison-summary.json` and `scratch/hyper-c-codegen/fps-cplus-ultra-comparison-summary.md`
- Root-cause captures, telemetry, preserved engine logs, and reduction: `scratch/hyper-c-codegen/rootcause-*`, `scratch/hyper-c-codegen/analyze_fps_rootcause.py`, and `scratch/hyper-c-codegen/wddm-rootcause-summary.json`
- Runtime rejection results: `scratch/hyper-c-codegen/security-rejections.json`
- Compile-contract results: `scratch/hyper-c-codegen/compile-contract-rejections.json`

No commit or push was made.
