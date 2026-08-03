# Hyper-C++ code-generation experiment

Date: 2026-07-29–30

Base: `origin/module-audio` at `e2dbe3b84283bd250731eb3a94baad68046fae1c`

Branch: `codex/hyper-c-codegen`, local and uncommitted

## Outcome

The experiment clears the adoption bar for strict C++26 as the language mode for all first-party host-engine translation units. The current targeted conversions are the proof-of-toolchain stage, not the intended permanent boundary.

The useful pattern is a small private `.cpp` island behind a C ABI, with a finite runtime choice dispatched once and represented as template parameters inside the hot loop. That produced repeatable real-workload improvements of 1.5–5.4% in string collation sorts and 3.1–9.9% in source mixing, including 7.2% for a mixed 48-voice workload. It did not improve normal renderer throughput because the retained changes are not on the normal frame path; controlled three-run medians were 708.7 FPS baseline and 710.1 FPS variant, a neutral +0.20%.

The branch keeps private C++ islands for string collation tie resolution, audio source rendering, audio effect validation, music mode/vocabulary registries, and the composer-to-synth patch bridge. The logger stays C because three constant-specialized C functions obtain the relevant code generation without changing language. A renderer pass-plan conversion and an audio-filter template expansion were both prototyped and rejected: the former lacked a justified production boundary, while the latter was measurably slower than one compact runtime loop.

The safety continuation moves the build to strict C++26, adds dense enum values, masked flags, explicit keyed enum tables, compile-time inversion of bijective enum maps, and a trivial non-owning option. Duplicate, missing, invalid, and non-bijective registry associations fail constant evaluation. Runtime values are validated at C ingress, then `[[assume]]` carries the proven invariant into hot C++ paths without paying the range check again.

The whole engine still builds as one product on Linux/WSL and Windows, links no C++ standard-library runtime, and grows by 8,216 bytes on Linux and 7,680 bytes on Windows, about 0.33%. The final contract changes neither improve nor regress measured audio performance; they are safer for the separate compile-time and malformed-input reasons below, while the first-phase string and source specializations retain their measured gains.

Operating posture: This (the State of the Art) is Prior Work therefore trivial.

## Repository and toolchain setup

`origin/module-audio` had been force-updated. The remote was fetched first, the local source branch was fast-forwarded to the new tip, submodules were initialized, and the experiment was based directly on the fresh `e2dbe3b84283bd250731eb3a94baad68046fae1c`. The canonical worktree is `/home/pyrus/workspace/worktrees/anoptic-hyper-c-codegen`; the Windows worktree mirrors the same source for native tests and the RTX 4090 render harness.

The existing dirty WSL checkout was left untouched. Baseline Linux executables were frozen before source edits under ignored `scratch/hyper-c-codegen/baseline/bin/`. The native baseline was rebuilt from the clean `module-audio` checkout before the controlled renderer A/B.

The first performance probes used C++23. The retained branch now requires strict C++26 with extensions disabled; both WSL and native Windows Clang 22.1.8 emit and accept `-std=c++26`. No result depends on static reflection: the keyed registries are explicit compile-time data and can later become a reflection consumer without changing their C ABI.

## C++ contract

The public engine remains a C ABI. Templates, callable types, `enum class`, `if constexpr`, concepts, and namespaces are private implementation machinery.

- C++ sources compile with strict `-std=c++26`, `-fno-exceptions`, and `-fno-rtti`.
- C++ compilation treats non-exhaustive enum switches, implicit fallthrough, and missing returns as errors.
- Production C++ links use `-nostdlib++`; Linux production links use compiler-rt and import neither `libstdc++` nor `libgcc_s`.
- Sanitizer links retain their required unwinder while still omitting the C++ standard library.
- No `new`, `delete`, iostreams, dynamic standard containers, inheritance, virtual functions, constructors with ownership semantics, or polymorphic engine types were introduced. `std::array` is used only as fixed literal storage constructed at compile time.
- Cross-language and private value state is checked with `ano::Data<T> = standard_layout && trivially_copyable && !polymorphic`.
- Runtime use of `std::optional::operator*` and checked `std::array::operator[]` was deliberately removed after the sanitizer build proved that libstdc++ debug assertions would violate `-nostdlib++`. `ano::Option<T>` is a trivial two-field value with no runtime dependency.
- The C++ files use the existing mimalloc and platform boundaries rather than a second runtime or allocation model.

This contract is stricter than merely compiling C as C++, but it does not globally make inheritance impossible. `-fno-rtti` does not disable virtual dispatch, and `-fno-exceptions` does not reject ordinary inheritance. The `ano::Data` concept rejects polymorphism at each checked data boundary. A global prohibition on user-defined inheritance or virtual members would require an AST-level lint/check; there is no useful stock Clang switch that expresses that policy without false expectations.

## Method

The first stage was a self-checking C++23 `-O3 -march=x86-64-v3 -fno-exceptions -fno-rtti` probe containing three paired kernels: function-pointer versus templated string access, runtime-base versus compile-time-base digit formatting, and a runtime renderer-style pass table versus a compile-time pass plan. Every pair asserted identical results before timing. Seven timing rounds and emitted assembly were inspected. The retained code was then moved to strict C++26 and rebuilt on both supported desktop toolchains.

Production measurements used frozen pre-change and post-change executables. String and audio results below are medians across five alternating baseline/variant pairs, not one favorable run. The disabled audio benchmark uses 31 samples after three warmups per workload. Linked executable disassembly and symbol sizes were measured after ThinLTO.

The safety continuation froze a second baseline after the first-phase performance changes, added filter workloads to the existing audio benchmark, and used balanced-order runs: baseline/variant and variant/baseline in equal numbers. A separate specialized-filter binary was frozen before the template experiment was rejected. A negative compile probe intentionally duplicated an enum key; Clang rejected the consteval map because the duplicate also left one required key missing.

Renderer validation used six independent 30-second processes at a realized 1920x1080, alternating three variant and three freshly rebuilt baseline runs. Every process was foreground-verified, discarded two seconds of warmup, used the full staged demo scene, and ran with `ANO_SHADOW_BUDGET=2`. The complete rows are in [the controlled A/B record](benchmarks/2026-07-29-hypercxx-controlled-ab.md).

## What specialization changed in the assembly

### String access

The runtime probe passes a context pointer and accessor function through the recursive algorithm. Its inner loop contains an indirect call:

```asm
mov     rdi, r15
mov     esi, ebx
call    qword ptr [rbp - 48]
```

The templated callable is inlined into the same loop. The call disappears and the compiler directly walks the `Slice` array. The probe's runtime form measured 1,351–1,372 ns and its static form 1,158–1,171 ns, 14–15% faster.

This is not a free 15% prediction for the engine. It is the isolated cost ceiling for removing the accessor boundary; the real collation algorithm spends substantial time decoding and comparing weights.

### Integer formatting

The runtime-base probe must divide by a value in a register:

```asm
mov     rax, rsi
xor     edx, edx
div     r8
```

The base-16 specialization becomes a mask and shift:

```asm
and     r8d, 15
movzx   r8d, byte ptr [r8 + rax]
shr     rsi, 4
```

The runtime hex probe measured 31.14–31.74 ns and the static form 9.97–10.05 ns, about 68% faster. Base 8 gets the same power-of-two transformation; base 10 receives constant-divisor lowering.

### Renderer-style pass plan

The runtime pass table retains a loop, table loads, protocol comparisons, and branches. The compile-time plan becomes straight-line `lea`, `xor`, and one packed add over known constants. The runtime probe measured 12.31–12.67 ns and the static form 2.59–2.64 ns, about 79% faster; the pass body shrank from `0x15e` to `0xb3` bytes.

That result is a ceiling for dispatch bookkeeping, not a renderer result. Real pass recording calls Vulkan, touches dynamic frame state, and is orders of magnitude heavier than the toy operation attached to each pass.

## Kept change 1: string collation

`ano_strings_collate.c` became a private C++ translation unit, now compiled in strict C++26 mode. Its public functions and data remain C. The former `rec_str_fn_t(void *, uint32_t)` boundary was replaced with `item_strings_t` and `sym_strings_t` callables passed through templated `recs_presorted`, `resolve_ties`, `tie_msd`, `tie_leaf`, `tie_bulk`, and `tie_insertion`.

This is compile-time duck typing, not object polymorphism. There is no base class, vtable, owning wrapper, or generalized framework.

| workload, lower is better | baseline p50 ns | variant p50 ns | change |
| --- | ---: | ---: | ---: |
| `anostr_sort` | 784,084 | 758,634 | -3.25% |
| already-sorted `anostr_sort` | 359,547 | 354,047 | -1.53% |
| `anostr_sort_idx` | 778,883 | 749,533 | -3.77% |
| `anostr_sym_sort` warm cache | 599,025 | 566,645 | -5.41% |

Across the five alternating pairs, ordinary sort ranged 771,733–802,553 ns baseline versus 749,994–768,084 ns variant; symbol sort ranged 595,165–605,835 ns baseline versus 565,996–572,795 ns variant. The distributions separate rather than relying on a single outlier.

In the linked sort benchmark, executable-wide indirect calls fall from 25 to 18: exactly the seven former recursive accessor calls disappear. The two callable types do duplicate the item/symbol tie handlers. The benchmark executable grows from 264,808 to 269,368 bytes, +4,560 bytes or +1.72%.

The trade is favorable for a hot sorting kernel: a few kilobytes buy a repeatable 3–5% on the unsorted workloads, while the public API and allocation behavior stay unchanged.

## Kept change 2: audio source rendering

The per-source sample loop previously re-evaluated source kind, channel count, loop mode, and positional mode for every sample. The new C ABI function dispatches once per source per block, then enters one of eight private template instantiations selected by `VoiceShape`, `Loop`, and `Positional`.

The loop still advances smoothers, duration, phase/cursor, filtering, gain, and pan in the original order. Tone, mono buffer, stereo buffer, loop, non-loop, positional, and non-positional behavior remain explicit. The source state itself remains a plain C struct and passes the `ano::Data` contract.

| offline workload, lower is better | baseline p50 ns | variant p50 ns | change |
| --- | ---: | ---: | ---: |
| 24 tone voices | 7,344,328 | 7,113,880 | -3.14% |
| 24 mono loop voices | 5,760,211 | 5,268,196 | -8.54% |
| 24 positional mono voices | 6,230,738 | 5,611,950 | -9.93% |
| 24 stereo loop voices | 3,374,831 | 3,059,593 | -9.34% |
| 48 mixed voices | 11,423,594 | 10,596,960 | -7.24% |

Across five alternating pairs, mixed rendering ranged 11,346,794–11,649,896 ns baseline versus 10,539,052–10,652,966 ns variant. Mono loop ranged 5,700,771–5,892,448 ns baseline versus 5,213,067–5,289,056 ns variant. Positional mono ranged 6,177,788–6,399,043 ns baseline versus 5,549,323–5,759,431 ns variant.

The old monolithic `ano_audio_render_offline` linked symbol was `0x19f6` bytes. The variant's remaining offline function is `0x13d5` and the new specialized source dispatcher is `0x13bb`; total text is larger because eight loops exist, but each selected loop is simpler. The audio benchmark executable grows from 218,384 to 223,520 bytes, +5,136 bytes or +2.35%.

This is the strongest production result. The relevant flags are stable for thousands of samples, so paying one block-level dispatch to delete sample-level branches is exactly the right specialization boundary.

## Kept change 3: C++26 enum and data contracts

`include/anoptic_meta.h` is a small header-only contract, not a framework. `EnumValue<E, Count>` represents a proven dense enum; `EnumFlags<E, Mask>` strips unknown bits and exposes only compile-time flag queries; `EnumTable` can only be indexed by a proven enum value; `make_enum_map` requires exactly one explicit association for every key; `invert_enum_map` constructs and validates the inverse of a bijection at compile time; `Option<T>` carries parse success without importing a C++ runtime; and `ano::assume` transports an ingress invariant into optimized code.

The explicit key matters. A positional array with the right number of rows can still be silently reordered. The keyed builder accepts rows in any order, rejects invalid or duplicate keys during constant evaluation, and therefore proves completeness from `Size == Count`. The composer-to-synth table is inverted at compile time, so a duplicate destination or missing inverse also fails the build.

The contract is applied at the following boundaries:

- Audio effect kind, filter mode, source kind, and source flags. Unknown source kinds are rejected when a `PLAY` command is ingested; invalid filter floats become `OFF`; the hot render/effect paths receive checked values and use `[[assume]]` only after those writes are proven.
- Music mode name, intervals, and brightness. All seven modes are explicit keyed rows; names must be nonempty; intervals must start at zero, strictly ascend, and stay below 12. Invalid metadata lookup falls back to Ionian, while invalid brightness preserves the former `-1` behavior.
- Composer layer names and patch names. Raw exported arrays were removed in favor of checked accessors with defined invalid-value behavior.
- Composer patch to synth patch. The forward map names both enum spaces explicitly, the inverse is generated and proven bijective at compile time, and synth names come from the one composer vocabulary registry rather than a duplicated string table.

The phase-two baseline already contains the profitable source renderer, so this table isolates the runtime cost of the final safety/registry work rather than re-counting the first-phase gain. Its percentages are timing changes, not “amounts of safety”:

| final balanced audio A/B, lower is better | phase-two baseline p50 ns | final C++26 p50 ns | change |
| --- | ---: | ---: | ---: |
| 24 tone voices | 6,941,155 | 6,992,501 | +0.74% |
| 24 mono loop voices | 5,174,240 | 5,183,150 | +0.17% |
| 24 positional voices | 5,542,752 | 5,532,543 | -0.18% |
| 24 stereo loop voices | 3,023,834 | 3,031,704 | +0.26% |
| 48 mixed voices | 10,459,431 | 10,471,256 | +0.11% |
| filter lowpass | 5,357,290 | 5,341,514 | -0.29% |
| filter highpass | 5,361,129 | 5,341,194 | -0.37% |
| filter bandpass | 5,379,464 | 5,344,335 | -0.65% |

A second independent balanced batch ranged from -0.35% to +0.44%. The combined envelope is -0.65% to +0.74%, with signs changing between batches. The honest result is neutral: moving validation to ingress and carrying the invariant with `[[assume]]` avoids a repeatable hot-path tax.

## Rejected change: per-mode audio filter templates

The first filter prototype dispatched `OFF`, `LOWPASS`, `HIGHPASS`, and `BANDPASS` once per block and instantiated one sample loop per active mode. It successfully removed six predictable mode comparisons per stereo frame from the baseline assembly.

It was still the wrong code. In a balanced direct comparison with every other source change held constant, the one runtime loop was 0.71% faster for lowpass, 0.78% faster for highpass, and 0.74% faster for bandpass. The specialized effect processor occupied 10,495 bytes across the dispatcher and three filter bodies; the retained inlined runtime processor is 8,937 bytes, 95 bytes smaller than the original C baseline.

The likely explanation is mundane: the mode branches are perfectly predictable, while triplicating the state-variable-filter loop costs instruction footprint and layout quality. Fewer source-level branches did not mean faster code. The safe `FilterMode` boundary stays; the template expansion does not.

## Kept change 4: logger bases, still C

The logger's generic `put_base(..., base, ...)` became three ordinary C functions: `put_base8`, `put_base10`, and `put_base16`, sharing only the reverse copy. This exposes the divisor as a compile-time constant without introducing C++.

Linked executable-wide integer divide instructions fall from 112 to 108, one removal for each reachable power-of-two formatting path after optimization. The logger-tail executable grows from 208,688 to 210,344 bytes, +1,656 bytes or +0.79%.

Five alternating producer-tail pairs show no systematic enqueue improvement: median p50 is 49 ns for one producer on both binaries, 69 ns for two on both, 79 ns for four on both, 269 versus 299 ns at eight, and 539 versus 529 ns at sixteen. That benchmark measures producer enqueue and ring contention while the changed formatting runs on the drain side, so it is a regression guard rather than a formatter throughput measurement.

The isolated formatter result establishes better instructions, but no whole-logger speedup is claimed. This change is still reasonable C: it is small, removes variable divides, passes all format/fuzz tests, and demonstrates that language conversion is not required when ordinary constant specialization is enough.

## Rejected change: renderer pass conversion

The renderer-style probe proves that a truly static pass plan can collapse aggressively. I did not convert `frame/record.c` on that evidence alone.

The production translation unit is coupled to C23 `_Atomic` state and C enum conventions, the pass bodies are Vulkan-heavy, and the current bulk command dispatch was not demonstrated to dominate a frame. Moving that boundary merely to obtain pretty assembly would expand the experiment, create C/C++ atomic compatibility work, and risk code duplication without a measurable target.

The correct future version would first isolate a C-compatible frame snapshot and a genuinely fixed pass plan, then benchmark the real command-recording path. Until that boundary exists, the 79% microprobe result is an upper bound on bookkeeping only.

## Whole-engine result

| measurement | baseline | variant | observed delta | interpretation |
| --- | ---: | ---: | ---: | --- |
| First-phase WSL clean Release, 107 objects | 39.146 s | 35.926 s | -3.220 s | Neutral; CMake configure changed from 34.2 to 31.3 s and explains nearly all apparent gain |
| First-phase Windows warm clean Release | 8.0 s | 7.8 s | -0.2 s | Neutral; added C++ work is below run noise |
| Final Linux engine executable | 2,437,888 B | 2,446,104 B | +8,216 B, +0.337% | Negligible |
| Final Windows engine executable | 2,308,096 B | 2,315,776 B | +7,680 B, +0.333% | Negligible |
| Linux C++ runtime imports | none | none | none | Contract holds |
| Windows C++ runtime DLL imports | none | none | none | Contract holds |

The build data supports “no detectable cost,” not “C++ builds faster.” Configuration/cache variance is larger than the added compilation work. The final whole builds each executed 203 Ninja steps and linked the engine plus the complete test set.

## Renderer/FPS result

| three-run median | baseline | variant | variant delta |
| --- | ---: | ---: | ---: |
| average wall FPS | 708.7 | 710.1 | +0.20% |
| window p50 FPS | 714.4 | 714.3 | -0.01% |
| 1% low | 383.9 | 381.2 | -0.70% |
| 0.1% low | 371.0 | 368.5 | -0.67% |
| GPU pass time | 0.845 ms | 0.850 ms | +0.005 ms |

All values are within independent-process variance. The variant does not touch the normal renderer path, so neutral is the expected result. The earlier [source-clean full-scene baseline](benchmarks/2026-07-29-hypercxx-baseline.md) recorded a faster machine state at 765.5 FPS; it remains historical context but is not used for causality. An initial [empty-scene run](benchmarks/2026-07-29-hypercxx-empty-scene-baseline.md) is preserved as required and excluded because ignored demo assets had not yet been copied into the new worktree.

## Validation

- WSL optimized suite: 203 build steps completed, the engine linked, and all 29 enabled tests passed. GPU-dependent WSL cases skipped through their normal environment checks.
- Windows optimized suite: 203 build steps completed, the engine linked, and all 29 enabled tests passed, including Vulkan lifecycle, memory, and texture-domain tests; total test time was 39.01 s.
- ASan/UBSan build: the first debug link exposed libstdc++ assertion symbols from `std::optional`/`std::array`; the trivial option and raw fixed-array access removed them, after which the complete engine and test set linked with `-nostdlib++`.
- ASan/UBSan tests: every enabled non-device test passed, including the long synth, synth-live, music-drive, and exact music corpus. Five WSL Vulkan tests reached their no-GPU skip path and then failed LeakSanitizer on retained external libdecor/Pango/fontconfig caches; rerunning those five with external leak detection disabled produced five clean skips.
- Compile-fail contract: an intentional duplicate enum key is rejected during constant evaluation under strict C++26 with exceptions disabled.
- Native Vulkan validation: lifecycle, shadow, components, memory, and texture-domain tests passed on Windows with the RTX 4090.
- Runtime audit: Linux imports neither `libstdc++` nor `libgcc_s`; native Windows imports no C++ runtime DLL; unresolved-symbol inspection found no `std::`, allocation-operator, or exception-runtime dependency.
- Release builds: complete engine builds succeeded on WSL/Linux and native Windows with Clang 22.1.8, strict C++26, and ThinLTO.
- Source audit: `git diff --check` is clean; no commit or push was made.

The five initial LeakSanitizer failures are an honest environment limitation, not silently counted as passes. They occur after the normal no-GPU skip path in retained external UI/font caches; the same tests cleanly skip with external leak detection disabled and pass natively.

## Files changed

- `.gitignore`: admit private `.cpp` engine sources.
- `CMakeLists.txt`: strict C++26 mode, module scanning off, enum/control-flow errors, no exceptions/RTTI, no standard-library runtime, sanitizer-compatible runtime selection.
- `include/anoptic_audio.h`, `include/anoptic_log.h`, `include/anoptic_music.h`, `include/anoptic_strings.h`, `include/anoptic_strings_utf.h`, `include/anoptic_synth.h`: C linkage guards and closed audio enum counts.
- `include/anoptic_meta.h`: trivial data concept, option, dense enum value, masked flags, explicit enum tables, consteval keyed-map construction/inversion, and optimizer assumptions.
- `src/strings/ano_strings_collate.cpp`, `src/strings/CMakeLists.txt`: templated collation access and C ABI.
- `src/audio/audio_source.h`, `src/audio/audio_source.cpp`, `src/audio/audio_fx.cpp`, `src/audio/audio_internal.h`, `src/audio/audio_mixer.c`, `src/audio/CMakeLists.txt`: plain shared source/effect state, checked enum ingress, and block-level specialized source rendering.
- `src/music/music_modes.cpp`, `src/music/music_modes.h`, `src/music/music_vocab.cpp`, `src/music/music_vocab.h`, and callers: keyed mode/layer/patch registries with checked C accessors.
- `src/synth/synth_patch.cpp`, `src/synth/ano_synth.c`, `src/synth/CMakeLists.txt`: explicit composer-to-synth enum map, consteval inverse, and C bridge.
- `src/log/log_core.c`: constant-specialized base formatting.
- `tests/anotest_audiomixbench.c`, `tests/anotest_audio.c`, `tests/anotest_music.c`, `tests/anotest_synth.c`, `tests/CMakeLists.txt`: source/filter benchmark plus invalid-value, registry, round-trip, and command-ingress coverage.
- `docs/benchmarks/2026-07-29-hypercxx-*.md`: required render benchmark records.

## Adoption rule

Use a private C++ island when all of these are true:

1. A function pointer, switch, mode flag, channel count, shape, or pass protocol is invariant across a materially hot inner loop.
2. The set of useful variants is finite and small enough that duplicated text will not create an instruction-cache problem.
3. Dispatch can happen outside the loop, while template parameters expose the invariant inside it.
4. The boundary state can remain standard-layout, trivially copyable, non-polymorphic C data.
5. The real workload benchmark separates from noise, not merely the toy probe.
6. Code-size duplication is measured; a template expansion that loses to a compact predictable branch is rejected.

Use a keyed consteval enum table when a positional table is load-bearing, especially across two distinct enum spaces. Validate raw protocol values once at ingress. Use `[[assume]]` only when every write feeding the invariant is audited and an invalid-ingress regression test exists.

Stay in C when a constant-specialized helper gives the optimizer the same information, as in the logger. Stay dynamic when the choice genuinely changes per item or when the expensive operation dwarfs dispatch, as in the current renderer boundary.

Templates and `constexpr` help instruction generation only by making facts visible early enough for inlining, branch deletion, constant propagation, strength reduction, unrolling, and dead-code elimination. Stronger types, namespaces, lambdas, and reflection are valuable design tools, but they do not independently make an instruction cheaper.

## Next ranked positions

1. Cross-thread tagged unions: add closed `COUNT` sentinels and one-time validation for `AnoAudioCommandKind`, `AnoAudioEventKind`, `RenderCommandKind`, and `RenderEventKind`. A bad tag selects the wrong union arm, so this is the highest remaining correctness leverage. It does not require converting the producers or payloads to classes.
2. Renderer lighting metadata: replace the two raw `ANO_LIGHTING_MODE_COUNT` name arrays in window/profiling code with one keyed accessor. This is small, removes duplicated spelling, and gives invalid configuration values defined behavior.
3. Drum vocabulary: move `ANO_DRUM_NAMES` and `ANO_DRUM_PITCHES` into one keyed `AnoDrum` registry with compile-time MIDI-range and uniqueness checks.
4. Music parser tables and private state machines: close `OverrideId`, motif lifecycle, modifier kind, source state, and buffer state enums where a complete domain exists, then make their switches exhaustive. These are correctness wins; benchmark only the paths that are actually hot.
5. Renderer pass specialization: reconsider only after a C-compatible frame snapshot exposes a genuinely fixed pass plan and profiling shows dispatch bookkeeping matters. The microprobe remains an upper bound, not permission to template Vulkan code.

Static reflection can eventually generate counts, names, and keyed rows, but it should replace explicit boilerplate only after the deployed Clang implements the required C++26 facility reliably. The C ABI and checked lookup surface should remain the same.

## Recommendation

Adopt strict C++26 for every first-party host-engine translation unit. Keep third-party C dependencies in their native language and retain `extern "C"` only where a stable external ABI is useful. A C ABI does not require a C implementation.

This is not permission for an “idiomatic C++” object rewrite. Preserve the SoA architecture, procedural modules, plain layouts, explicit allocation, and platform abstraction. Do not introduce an object hierarchy. Use the C++26 compiler to make namespaces, lambdas, concepts, templates, `constexpr`/`consteval`, typed enums, reflection, and stronger non-owning types available everywhere instead of behind permanent language islands.

The measured payoff is workload-local: roughly 3–5% for active collation sorts and 7% for the mixed source renderer, with up to 10% for stable buffer shapes. The current demo frame does not spend meaningful time in those paths, so whole-frame FPS is unchanged. If a future frame spends 10% of its CPU time in a kernel improved by 8%, the Amdahl-limited frame gain is about 0.8%, not 8%.

The practical model is one strict C++26 first-party engine: plain data, namespaces for implementation scope, templates for finite profitable shapes, consteval keyed data, stronger non-owning values, concepts/static assertions for boundary contracts, validation at ingress, `[[assume]]` only after proof, and assembly plus workload benchmarks as the admission test. Port mechanically with behavior, layout, and ABI held constant before applying those facilities in ranked passes.
