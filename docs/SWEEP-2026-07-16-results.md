# Windows Sweep Results 〜 2026-07-16

## Meta
- Branch: module-audio
- Commit: ba25f214aad6a0b7893d9adbb43dc355ceac9b08 (ba25f21)
- Date: 2026-07-16
- OS: Microsoft Windows 11 Pro 25H2, build 26200.8875, x64
- Machine: AMD Ryzen 9 5950X 16C/32T, NVIDIA GeForce RTX 4090, 64 GB DDR4-3600, Gigabyte X570S AORUS PRO AX
- Log staging: scratch/sweep-2026-07-16/
- Commands: sequential cmd /c "build.bat N" for N=1..8 with stdout+stderr redirected; then ctest --test-dir build/<LABEL> --output-on-failure -V for HeadlessDebug/Tests/O3Tests; hand-run build/O3Tests/tests/anotest_*.exe benches; python tools/perf/bench_fps_win64.py

## Build matrix (build.bat 1–8)

| N | Label | Exit | Duration (s) | Result |
| --- | --- | --- | --- | --- |
| 1 | Release | 0 | 11.9 | success |
| 2 | Debug | 0 | 8.4 | success |
| 3 | Headless | 0 | 5.4 | success |
| 4 | HeadlessDebug (+ctest) | 0 | 120.0 | success |
| 5 | Tests Debug (+ctest) | 0 | 141.1 | success |
| 6 | ASan | 1 | 0.4 | expected-fail (Windows unsupported) |
| 7 | TSan | 1 | 0.4 | expected-fail (Windows unsupported) |
| 8 | O3Tests (+ctest) | 0 | 97.3 | success |

### build.bat 6 / 7 message (verbatim)
```
The sanitizer profiles are Linux/macOS-only: MinGW clang on Windows supports
neither TSan nor a working ASan against ucrt. Use build.sh 6/7 under WSL.
EXIT=1
DURATION_SEC=0.4
```

### Key tails
- build 1: Linked `anopticengine.exe` (Release); EXIT=0
- build 2: Linked `anopticengine.exe` (Debug); EXIT=0
- build 3: Linked headless `anopticengine.exe`; EXIT=0
- build 4: CTest during build: 100% of runnable tests; 8 Disabled benches; EXIT=0
- build 5: CTest during build including vulkan suite; 8 Disabled benches; EXIT=0
- build 8: CTest during build; 8 Disabled benches + vk_compliance_layers + vk_sync Disabled; EXIT=0

## CTest 〜 HeadlessDebug

### Inventory (ctest -N)

```
Internal ctest changing into directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug
Test project C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug
  Test  #1: anoptic_time
  Test  #2: anoptic_logging
  Test  #3: anoptic_filesystem
  Test  #4: anoptic_strings
  Test  #5: anoptic_strings_utf
  Test  #6: anoptic_strings_sort
  Test  #7: anoptic_strings_fuzz
  Test  #8: anoptic_sortbench (Disabled)
  Test  #9: anoptic_stropsbench (Disabled)
  Test #10: anoptic_logstrbench (Disabled)
  Test #11: anoptic_text
  Test #12: anoptic_ui
  Test #13: anoptic_strbench (Disabled)
  Test #14: anoptic_sidbench (Disabled)
  Test #15: anoptic_meshoptimizer
  Test #16: anoptic_memory
  Test #17: anoptic_chariots (Disabled)
  Test #18: anoptic_logbench (Disabled)
  Test #19: anoptic_logtail (Disabled)
  Test #20: anoptic_logfuzz
  Test #21: anoptic_blackbox
  Test #22: anoptic_render_bridge
  Test #23: anoptic_audio
  Test #24: anoptic_audiotone
  Test #25: anoptic_audioscene
  Test #26: anoptic_audiodsp
  Test #27: anoptic_synth
  Test #28: anoptic_musichost
  Test #29: anoptic_synthlive
  Test #30: anoptic_musicdrive
  Test #31: anoptic_musicscene
  Test #32: anoptic_synthscene
  Test #33: anoptic_music

Total Tests: 33
```

### Full verbose run

Pass/fail progress lines and summaries from ctest -V (full log scratch/sweep-2026-07-16/ctest-HeadlessDebug.log, ~27KB):

```
      Start  1: anoptic_time
1: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_time.exe
1: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
1: Test timeout computed to be: 30
1: Testing current date.
1: Current Date and Time: Thu Jul 16 04:52:32 2026
1: 
1: Testing timestamps across various resolutions
1: 
1: Timebase: invariant TSC @ 3399974030 Hz (QPC 10000000 Hz)
1: 
1: nanoseconds: 34297546445920
1: microseconds: 34297546446
1: milliseconds: 34297546
1: 
1: Testing timebase granularity (smallest resolvable step)
1:   [PASS] min step = 1 ticks = 0 ns (need < 100 ns)
1: 
1: ano_busywait resolution sweep:
1:   [PASS] busywait      1000ns  best=     1040ns  ceil<=101000ns
1:   [PASS] busywait     10000ns  best=    10040ns  ceil<=110000ns
1:   [PASS] busywait    100000ns  best=   100040ns  ceil<=200000ns
1:   [PASS] busywait   1000000ns  best=  1000038ns  ceil<=1100000ns
1:   [PASS] busywait  10000000ns  best= 10000056ns  ceil<=11000000ns
1: 
1: ano_sleep resolution sweep:
1:   [PASS] sleep       50us  best=   50060ns  floor>=29500ns  ceil<=2050000ns
1:   [PASS] sleep      100us  best=  100060ns  floor>=79000ns  ceil<=2100000ns
1:   [PASS] sleep      250us  best=  250062ns  floor>=227500ns  ceil<=2250000ns
1:   [PASS] sleep      500us  best=  500054ns  floor>=475000ns  ceil<=2500000ns
1:   [PASS] sleep     1000us  best= 1000077ns  floor>=970000ns  ceil<=3000000ns
1:   [PASS] sleep     2000us  best= 2000195ns  floor>=1960000ns  ceil<=4000000ns
1:   [PASS] sleep     5000us  best= 5000178ns  floor>=4930000ns  ceil<=7500000ns
1:   [PASS] sleep    10000us  best=10000206ns  floor>=9880000ns  ceil<=15000000ns
1:   [PASS] sleep    50000us  best=50000203ns  floor>=49480000ns  ceil<=75000000ns
1:   [PASS] sleep   100000us  best=100000263ns  floor>=98980000ns  ceil<=150000000ns
1: 
1: anoptic_time.h: All Tests passed!
 1/33 Test  #1: anoptic_time .....................   Passed    1.47 sec
      Start  2: anoptic_logging
2: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_logging.exe
2: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
2: Test timeout computed to be: 60
2: WARN  anotest_logging.c:724:  pre-init immediate (expected on stderr)
2: 
2: Timebase: invariant TSC @ 3399835501 Hz (QPC 10000000 Hz)
2: 
2:   [PASS] lifecycle_preinit
2:   [PASS] roundtrip
2:   [PASS] formatting
2:   [PASS] deferred_formatting
2:   [PASS] accumulation_order
2:   [PASS] level_gate
2:   [PASS] full_ring
2:   [PASS] immediate_order
2: 04:52:34 INFO  route term only 1
2: 04:52:34 INFO  route both 2
2: 04:52:34 INFO  rerouted info line
2: 04:52:34 FATAL a FATAL routed through the default BOTH|NOW route
2: WARN  anotest_logging.c:724:  post-cleanup immediate (expected on stderr)
2:   [PASS] routing
2:   [PASS] truncation
2:   [PASS] empty_message
2:   [PASS] concurrent
2:   [PASS] contention_1_flush_vs_write
2:   [PASS] contention_2_aba_bait
2:   [PASS] contention_3_config_thrash
2:   [PASS] edge_cap_boundary
2:   [PASS] edge_tiny_records
2:   [PASS] edge_ring_seam
2:   [PASS] edge_alternating_immediate
2:   [PASS] edge_output_dir_switch
2:   [PASS] edge_level_churn
2:   [PASS] contention_heavy_mixed
2:   [PASS] contention_soak
2:   [PASS] premature_join_all
2:   [PASS] premature_join_half
2:   [PASS] abuse_inputs
2:   [PASS] abuse_config
2:   [PASS] abuse_output_dir
2:   [PASS] visible_output
2:   [PASS] lifecycle_postcleanup
2:   Showcase log written and verified: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/./anolog_visible/2026-07-16_413701_ano.log
2: anoptic_logging: all cases passed.
 2/33 Test  #2: anoptic_logging ..................   Passed    0.41 sec
      Start  3: anoptic_filesystem
3: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_filesystem.exe
3: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
3: Test timeout computed to be: 30
3: userpath: "C:\Users\Pyrus\AppData\Roaming\anoptic" (length 38)
3: gamepath: "C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests" (length 60)
3: anotest_filesystem: all checks passed
 3/33 Test  #3: anoptic_filesystem ...............   Passed    0.06 sec
      Start  4: anoptic_strings
4: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_strings.exe
4: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
4: Test timeout computed to be: 30
4: anotest_strings: all checks passed
 4/33 Test  #4: anoptic_strings ..................   Passed    0.06 sec
      Start  5: anoptic_strings_utf
5: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_strings_utf.exe
5: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
5: Test timeout computed to be: 30
5: anotest_strings_utf: all checks passed
 5/33 Test  #5: anoptic_strings_utf ..............   Passed    0.14 sec
      Start  6: anoptic_strings_sort
6: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_strings_sort.exe
6: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
6: Test timeout computed to be: 60
6: anotest_strings_sort: all checks passed
 6/33 Test  #6: anoptic_strings_sort .............   Passed    0.09 sec
      Start  7: anoptic_strings_fuzz
7: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_strings_fuzz.exe
7: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
7: Test timeout computed to be: 120
7: anotest_strings_fuzz: all checks passed
 7/33 Test  #7: anoptic_strings_fuzz .............   Passed    0.10 sec
      Start  8: anoptic_sortbench
 8/33 Test  #8: anoptic_sortbench ................***Not Run (Disabled)   0.00 sec
      Start  9: anoptic_stropsbench
 9/33 Test  #9: anoptic_stropsbench ..............***Not Run (Disabled)   0.00 sec
      Start 10: anoptic_logstrbench
10/33 Test #10: anoptic_logstrbench ..............***Not Run (Disabled)   0.00 sec
      Start 11: anoptic_text
11: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_text.exe
11: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
11: Test timeout computed to be: 30
11: before init: FreeType version reads 0.0.0
11: after init: FreeType 2.13.3
11: after shutdown: FreeType version reads 0.0.0
11: cubic quarter-circle -> 4 quads at 1e-3 em tolerance
11: bake: 1654 monotone curves across ASCII (audit oracle 1654), 3490 stream points
11: ref '#': 32x46 rms= 2.28 max= 37 unclamped-peak=2.000
11: ref '$': 34x58 rms= 1.79 max= 35 unclamped-peak=2.000
11: ref '+': 32x32 rms= 2.71 max= 53 unclamped-peak=2.000
11: ref 'f': 20x46 rms= 1.25 max= 20 unclamped-peak=2.000
11: ref 't': 20x42 rms= 1.23 max= 23 unclamped-peak=2.000
11: ref 'H': 35x46 rms= 1.75 max= 51 unclamped-peak=1.876
11: ref '8': 35x49 rms= 1.49 max= 14 unclamped-peak=1.748
11: ref '@': 54x54 rms= 1.39 max= 17 unclamped-peak=1.504
11: ref '%': 47x47 rms= 1.19 max= 18 unclamped-peak=1.000
11: ref 'A': 41x46 rms= 0.48 max=  6 unclamped-peak=1.000
11: ref 'g': 31x46 rms= 1.35 max= 13 unclamped-peak=1.000
11: ref 'O': 43x49 rms= 1.05 max= 11 unclamped-peak=1.000
11: ref 'i':  7x46 rms= 1.03 max=  5 unclamped-peak=1.000
11: ref worst: rms=2.71 max=53 over 13 probes
11: ghost sweep @ 64px: 0 ghost pixels (worst 0 on ' ')
11: ghost sweep @200px: 0 ghost pixels (worst 0 on ' ')
11: runic bake: 89 of 89 runes carry ink
11: anotest_text: all checks passed
11/33 Test #11: anoptic_text .....................   Passed    1.13 sec
      Start 12: anoptic_ui
12: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_ui.exe
12: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
12: Test timeout computed to be: 60
12: anotest_ui: prim ABI, builder, reference evaluator (soak x1)
12:   sdf sign sweep: 15213 probes, 12 boundary-skipped
12:   cov rect r=0 frac  edgeMax 0.00000 cornerMax 0.12000 rms 0.00432 (1739 windows)
12:   cov rrect r=8      edgeMax 0.00000 cornerMax 0.03745 rms 0.00284 (3149 windows)
12:   cov rrect mixed    edgeMax 0.00000 cornerMax 0.12500 rms 0.00355 (3149 windows)
12:   cov ring w=3 mixed edgeMax 0.00000 cornerMax 0.12500 rms 0.00473 (3149 windows)
12:   clip rect interior rows worst 0.0000008
12:   gradient linear  interior worst 0.0000001
12:   gradient radial  interior worst 0.0000001
12:   gradient conic   interior worst 0.0000001
12:   gradient modulate  worst 0.0000001
12:   path rect interior 0.000000  edge 0.000000  (9 words, 4 seg)
12:   path hole ring 1.0000  hole 0.0000
12:   tiles demo     69x37  8028 entries (1565 solid)  worst |tiled-brute| 0.000393391
12:   tiles random   33x24  536 entries (105 solid)  worst |tiled-brute| 0.000000000
12:   tiles random   28x17  241 entries (22 solid)  worst |tiled-brute| 0.000000000
12:   shadow rect s=2         max 0.01324 (3.38/255) rms 0.00350 (4800 px)
12:   shadow rect s=8         max 0.01041 (2.66/255) rms 0.00310 (13824 px)
12:   shadow rrect r=8 s=4    max 0.01822 (4.65/255) rms 0.00392 (7296 px)
12:   demo scene: 15 prims, 2 clips, 1 paints, 7 curve words, bitwise-stable
12: anotest_ui: all passed
12/33 Test #12: anoptic_ui .......................   Passed    1.31 sec
      Start 13: anoptic_strbench
13/33 Test #13: anoptic_strbench .................***Not Run (Disabled)   0.00 sec
      Start 14: anoptic_sidbench
14/33 Test #14: anoptic_sidbench .................***Not Run (Disabled)   0.00 sec
      Start 15: anoptic_meshoptimizer
15: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_meshoptimizer.exe
15: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
15: Test timeout computed to be: 10
15: Running test_meshlet_bounds_calculation...
15: Running test_degenerate_triangles...
15: Running test_meshlet_limits...
15: Running test_bounds_checks...
15: Running test_vertex_cache_optimization...
15: Running test_simplify_scale...
15: Running test_simplify_passthrough...
15: Running test_simplify_degenerate_input...
15: Running test_simplify_grid...
15: Running test_simplify_error_budget...
15: Running test_simplify_guard_bridge...
15: Running test_simplify_guard_byte_identity...
15: Running test_simplify_flat_not_gutted...
15: Running test_simplify_concave_trench...
15: Running test_simplify_pillar_silhouette...
15: Running test_simplify_tetra_link...
15: All tests passed successfully!
15/33 Test #15: anoptic_meshoptimizer ............   Passed    0.04 sec
      Start 16: anoptic_memory
16: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_memory.exe
16: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
16: Test timeout computed to be: 30
16: huge-page reservation probe: status=0 (non-zero is acceptable)
16: anotest_memory: all checks passed
16/33 Test #16: anoptic_memory ...................   Passed    0.10 sec
      Start 17: anoptic_chariots
17/33 Test #17: anoptic_chariots .................***Not Run (Disabled)   0.00 sec
      Start 18: anoptic_logbench
18/33 Test #18: anoptic_logbench .................***Not Run (Disabled)   0.00 sec
      Start 19: anoptic_logtail
19/33 Test #19: anoptic_logtail ..................***Not Run (Disabled)   0.00 sec
      Start 20: anoptic_logfuzz
20: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_logfuzz.exe
20: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
20: Test timeout computed to be: 120
20: 
20: Timebase: invariant TSC @ 3399933200 Hz (QPC 10000000 Hz)
20: 
20: logfuzz: producers=6 iters=4000 enqueued=23482 lines=23482
20: logfuzz: PASS: every enqueued record survived (no loss, no duplication)
20/33 Test #20: anoptic_logfuzz ..................   Passed    0.93 sec
      Start 21: anoptic_blackbox
21: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_blackbox.exe
21: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
21: Test timeout computed to be: 240
21: 
21: Timebase: invariant TSC @ 3399724645 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_587658_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_195395_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_100416_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_425259_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399913889 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_677367_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_537929_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_439263_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399778199 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_760631_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399792884 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_400878_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399916239 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_846800_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_755574_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_865906_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_852200_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_111438_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_215766_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_920066_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_656890_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_259806_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_596768_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_633498_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_244021_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_572528_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399940454 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_572528_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_372670_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399861693 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696211_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399935929 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696211_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_660651_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399776929 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_660651_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_165503_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399864572 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 2 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_660651_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_232035_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399836979 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 3 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_660651_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696174_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399862466 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 4 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696174_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_499137_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399924169 Hz (QPC 10000000 Hz)
21: 
21: 04:52:46 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696174_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_198342_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399909102 Hz (QPC 10000000 Hz)
21: 
21: 04:52:47 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2026-07-16_696174_CRASH.log.
21: 
21: Timebase: invariant TSC @ 3399939650 Hz (QPC 10000000 Hz)
21: 
21: 04:52:47 WARN  blackbox: 6 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests/logs/2006-01-01_000001_CRASH.log.
21: 
21: Timebase: invariant TSC @ 3399836310 Hz (QPC 10000000 Hz)
21: 
21: anotest_blackbox: all checks passed
21/33 Test #21: anoptic_blackbox .................   Passed    8.81 sec
      Start 22: anoptic_render_bridge
22: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_render_bridge.exe
22: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
22: Test timeout computed to be: 60
22: anotest_render_bridge: all checks passed
22/33 Test #22: anoptic_render_bridge ............   Passed    0.08 sec
      Start 23: anoptic_audio
23: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_audio.exe
23: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
23: Test timeout computed to be: 60
23: 
23: Timebase: invariant TSC @ 3399929948 Hz (QPC 10000000 Hz)
23: 
23: info: live telemetry ΓÇö blocks 28, cpu 8670 ns/block, underruns 0, clipped 0
23: anotest_audio: all passed (soak x1)
23/33 Test #23: anoptic_audio ....................   Passed    0.43 sec
      Start 24: anoptic_audiotone
24: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_audiotone.exe
24: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
24: Test timeout computed to be: 30
24: 
24: Timebase: invariant TSC @ 3399858074 Hz (QPC 10000000 Hz)
24: 
24: info: 48000 Hz, block 512 ΓÇö blocks 216, cpu 20490 ns/block, peak 0.000, underruns 0
24: anotest_audiotone: all passed
24/33 Test #24: anoptic_audiotone ................   Passed    2.34 sec
      Start 25: anoptic_audioscene
25: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_audioscene.exe "2"
25: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
25: Test timeout computed to be: 60
25: 
25: Timebase: invariant TSC @ 3399946182 Hz (QPC 10000000 Hz)
25: 
25: info: scene ΓÇö 5 clicks, blocks 219, cpu 73921 ns/block, underruns 0, clipped 0
25: anotest_audioscene: all passed (2 s scene)
25/33 Test #25: anoptic_audioscene ...............   Passed    2.37 sec
      Start 26: anoptic_audiodsp
26: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_audiodsp.exe
26: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
26: Test timeout computed to be: 60
26: anotest_audiodsp: all passed (soak x1)
26/33 Test #26: anoptic_audiodsp .................   Passed    0.10 sec
      Start 27: anoptic_synth
27: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_synth.exe
27: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
27: Test timeout computed to be: 300
27: info: journey ΓÇö 173.9 s, peak 0.850, C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests/journey_s42_synth.wav
27: anotest_synth: all passed
27/33 Test #27: anoptic_synth ....................   Passed   22.53 sec
      Start 28: anoptic_musichost
28: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_musichost.exe
28: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
28: Test timeout computed to be: 10000000
28: anotest_musichost: all passed
28/33 Test #28: anoptic_musichost ................   Passed    0.04 sec
      Start 29: anoptic_synthlive
29: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_synthlive.exe
29: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
29: Test timeout computed to be: 10000000
29: anotest_synthlive: all passed
29/33 Test #29: anoptic_synthlive ................   Passed    9.39 sec
      Start 30: anoptic_musicdrive
30: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_musicdrive.exe
30: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
30: Test timeout computed to be: 120
30: 
30: Timebase: invariant TSC @ 3399804671 Hz (QPC 10000000 Hz)
30: 
30:   bar composition: worst 251 us of the 10666 us block (2.4%)
30: anotest_musicdrive: all passed
30/33 Test #30: anoptic_musicdrive ...............   Passed   28.17 sec
      Start 31: anoptic_musicscene
31: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_musicscene.exe "30"
31: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
31: Test timeout computed to be: 120
31: 
31: Timebase: invariant TSC @ 3399963740 Hz (QPC 10000000 Hz)
31: 
31: game: calm                 (bar 0)
31: game: something is coming  (bar 1)
31: info: bar 2    Γöé peak 0.378 Γöé bar composed in 145 us of 10666 Γöé underruns 0
31: game: the fight            (bar 4)
31: game: SAVE at bar 7 (5 cues)
31: info: bar 4    Γöé peak 0.521 Γöé bar composed in 127 us of 10666 Γöé underruns 0
31:       ┬╖ bar 6    the key arrives: G mixolydian
31: game: the quiet after      (bar 9)
31: game: LOAD ΓÇö rebuilding bar 7 off the audio thread
31:       ┬╖ 62912-byte state rebuilt in 2 ms (never on the audio thread)
31:       ┬╖ seek consumed ΓÇö the save's bar 7 is next
31: info: bar 8    Γöé peak 0.763 Γöé bar composed in 188 us of 10666 Γöé underruns 0
31: note: 15 bars ΓÇö too short to reach a cadence once the steering has displaced it; run it longer to gate that
31: info: 30 s Γöé 2832 blocks Γöé bars 15 Γöé cadences 0 Γöé motifs 0 Γöé keys 1 Γöé cues 8
31: info: worst bar composed in 243 us of the 10666 us block (2.3%) Γöé late 0 Γöé dropped 0 Γöé underruns 0 Γöé clipped 0
31: anotest_musicscene: all passed (30 s)
31/33 Test #31: anoptic_musicscene ...............   Passed   30.28 sec
      Start 32: anoptic_synthscene
32: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_synthscene.exe "8"
32: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
32: Test timeout computed to be: 60
32: 
32: Timebase: invariant TSC @ 3399963661 Hz (QPC 10000000 Hz)
32: 
32: info: ~bar 1 Γöé peak 0.113 Γöé underruns 0
32: info: scene ΓÇö blocks 769, cpu 289103 ns/block, underruns 0, clipped 0, dropped 0
32: anotest_synthscene: all passed (8 s)
32/33 Test #32: anoptic_synthscene ...............   Passed    8.24 sec
      Start 33: anoptic_music
33: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\HeadlessDebug\tests\anotest_music.exe
33: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/HeadlessDebug/tests
33: Test timeout computed to be: 30
33: anotest_music: all passed
33/33 Test #33: anoptic_music ....................   Passed    0.97 sec
100% tests passed, 0 tests failed out of 25
Total Test time (real) = 119.62 sec
The following tests did not run:
	  8 - anoptic_sortbench (Disabled)
	  9 - anoptic_stropsbench (Disabled)
	 10 - anoptic_logstrbench (Disabled)
	 13 - anoptic_strbench (Disabled)
	 14 - anoptic_sidbench (Disabled)
	 17 - anoptic_chariots (Disabled)
	 18 - anoptic_logbench (Disabled)
	 19 - anoptic_logtail (Disabled)
EXIT=0
DURATION_SEC=119.7
```

## CTest 〜 Tests

### Inventory (ctest -N)

```
Internal ctest changing into directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests
Test project C:/Users/Pyrus/Code/anoptic-engine/build/Tests
  Test  #1: anoptic_time
  Test  #2: anoptic_logging
  Test  #3: anoptic_filesystem
  Test  #4: anoptic_strings
  Test  #5: anoptic_strings_utf
  Test  #6: anoptic_strings_sort
  Test  #7: anoptic_strings_fuzz
  Test  #8: anoptic_sortbench (Disabled)
  Test  #9: anoptic_stropsbench (Disabled)
  Test #10: anoptic_logstrbench (Disabled)
  Test #11: anoptic_text
  Test #12: anoptic_ui
  Test #13: anoptic_strbench (Disabled)
  Test #14: anoptic_sidbench (Disabled)
  Test #15: anoptic_meshoptimizer
  Test #16: anoptic_memory
  Test #17: anoptic_chariots (Disabled)
  Test #18: anoptic_logbench (Disabled)
  Test #19: anoptic_logtail (Disabled)
  Test #20: anoptic_logfuzz
  Test #21: anoptic_blackbox
  Test #22: anoptic_render_bridge
  Test #23: anoptic_audio
  Test #24: anoptic_audiotone
  Test #25: anoptic_audioscene
  Test #26: anoptic_audiodsp
  Test #27: anoptic_synth
  Test #28: anoptic_musichost
  Test #29: anoptic_synthlive
  Test #30: anoptic_musicdrive
  Test #31: anoptic_musicscene
  Test #32: anoptic_synthscene
  Test #33: anoptic_music
  Test #34: anoptic_render_slots
  Test #35: anotest_vk_lifecycle
  Test #36: anotest_vk_components
  Test #37: anotest_vk_compliance_layers
  Test #38: anotest_vk_memory
  Test #39: anotest_vk_sync

Total Tests: 39
```

### Full verbose run

Pass/fail progress lines and summaries from ctest -V (full log scratch/sweep-2026-07-16/ctest-Tests.log, ~30KB):

```
      Start  1: anoptic_time
1: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_time.exe
1: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
1: Test timeout computed to be: 30
1: Testing current date.
1: Current Date and Time: Thu Jul 16 04:54:32 2026
1: 
1: Testing timestamps across various resolutions
1: 
1: Timebase: invariant TSC @ 3399819671 Hz (QPC 10000000 Hz)
1: 
1: nanoseconds: 34418889031161
1: microseconds: 34418889031
1: milliseconds: 34418889
1: 
1: Testing timebase granularity (smallest resolvable step)
1:   [PASS] min step = 1 ticks = 0 ns (need < 100 ns)
1: 
1: ano_busywait resolution sweep:
1:   [PASS] busywait      1000ns  best=     1030ns  ceil<=101000ns
1:   [PASS] busywait     10000ns  best=    10030ns  ceil<=110000ns
1:   [PASS] busywait    100000ns  best=   100035ns  ceil<=200000ns
1:   [PASS] busywait   1000000ns  best=  1000053ns  ceil<=1100000ns
1:   [PASS] busywait  10000000ns  best= 10000041ns  ceil<=11000000ns
1: 
1: ano_sleep resolution sweep:
1:   [PASS] sleep       50us  best=   50062ns  floor>=29500ns  ceil<=2050000ns
1:   [PASS] sleep      100us  best=  100065ns  floor>=79000ns  ceil<=2100000ns
1:   [PASS] sleep      250us  best=  250063ns  floor>=227500ns  ceil<=2250000ns
1:   [PASS] sleep      500us  best=  500066ns  floor>=475000ns  ceil<=2500000ns
1:   [PASS] sleep     1000us  best= 1000063ns  floor>=970000ns  ceil<=3000000ns
1:   [PASS] sleep     2000us  best= 2000156ns  floor>=1960000ns  ceil<=4000000ns
1:   [PASS] sleep     5000us  best= 5000165ns  floor>=4930000ns  ceil<=7500000ns
1:   [PASS] sleep    10000us  best=10000200ns  floor>=9880000ns  ceil<=15000000ns
1:   [PASS] sleep    50000us  best=50000262ns  floor>=49480000ns  ceil<=75000000ns
1:   [PASS] sleep   100000us  best=100000254ns  floor>=98980000ns  ceil<=150000000ns
1: 
1: anoptic_time.h: All Tests passed!
 1/39 Test  #1: anoptic_time .....................   Passed    1.47 sec
      Start  2: anoptic_logging
2: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_logging.exe
2: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
2: Test timeout computed to be: 60
2: WARN  anotest_logging.c:724:  pre-init immediate (expected on stderr)
2: 
2: Timebase: invariant TSC @ 3399759358 Hz (QPC 10000000 Hz)
2: 
2:   [PASS] lifecycle_preinit
2:   [PASS] roundtrip
2:   [PASS] formatting
2:   [PASS] deferred_formatting
2:   [PASS] accumulation_order
2:   [PASS] level_gate
2:   [PASS] full_ring
2:   [PASS] immediate_order
2: 04:54:33 INFO  route term only 1
2: 04:54:33 INFO  route both 2
2: 04:54:33 INFO  rerouted info line
2: 04:54:33 FATAL a FATAL routed through the default BOTH|NOW route
2: WARN  anotest_logging.c:724:  post-cleanup immediate (expected on stderr)
2:   [PASS] routing
2:   [PASS] truncation
2:   [PASS] empty_message
2:   [PASS] concurrent
2:   [PASS] contention_1_flush_vs_write
2:   [PASS] contention_2_aba_bait
2:   [PASS] contention_3_config_thrash
2:   [PASS] edge_cap_boundary
2:   [PASS] edge_tiny_records
2:   [PASS] edge_ring_seam
2:   [PASS] edge_alternating_immediate
2:   [PASS] edge_output_dir_switch
2:   [PASS] edge_level_churn
2:   [PASS] contention_heavy_mixed
2:   [PASS] contention_soak
2:   [PASS] premature_join_all
2:   [PASS] premature_join_half
2:   [PASS] abuse_inputs
2:   [PASS] abuse_config
2:   [PASS] abuse_output_dir
2:   [PASS] visible_output
2:   [PASS] lifecycle_postcleanup
2:   Showcase log written and verified: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/./anolog_visible/2026-07-16_806520_ano.log
2: anoptic_logging: all cases passed.
 2/39 Test  #2: anoptic_logging ..................   Passed    0.43 sec
      Start  3: anoptic_filesystem
3: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_filesystem.exe
3: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
3: Test timeout computed to be: 30
3: userpath: "C:\Users\Pyrus\AppData\Roaming\anoptic" (length 38)
3: gamepath: "C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests" (length 52)
3: anotest_filesystem: all checks passed
 3/39 Test  #3: anoptic_filesystem ...............   Passed    0.07 sec
      Start  4: anoptic_strings
4: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_strings.exe
4: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
4: Test timeout computed to be: 30
4: anotest_strings: all checks passed
 4/39 Test  #4: anoptic_strings ..................   Passed    0.06 sec
      Start  5: anoptic_strings_utf
5: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_strings_utf.exe
5: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
5: Test timeout computed to be: 30
5: anotest_strings_utf: all checks passed
 5/39 Test  #5: anoptic_strings_utf ..............   Passed    0.13 sec
      Start  6: anoptic_strings_sort
6: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_strings_sort.exe
6: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
6: Test timeout computed to be: 60
6: anotest_strings_sort: all checks passed
 6/39 Test  #6: anoptic_strings_sort .............   Passed    0.08 sec
      Start  7: anoptic_strings_fuzz
7: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_strings_fuzz.exe
7: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
7: Test timeout computed to be: 120
7: anotest_strings_fuzz: all checks passed
 7/39 Test  #7: anoptic_strings_fuzz .............   Passed    0.09 sec
      Start  8: anoptic_sortbench
 8/39 Test  #8: anoptic_sortbench ................***Not Run (Disabled)   0.00 sec
      Start  9: anoptic_stropsbench
 9/39 Test  #9: anoptic_stropsbench ..............***Not Run (Disabled)   0.00 sec
      Start 10: anoptic_logstrbench
10/39 Test #10: anoptic_logstrbench ..............***Not Run (Disabled)   0.00 sec
      Start 11: anoptic_text
11: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_text.exe
11: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
11: Test timeout computed to be: 30
11: before init: FreeType version reads 0.0.0
11: after init: FreeType 2.13.3
11: after shutdown: FreeType version reads 0.0.0
11: cubic quarter-circle -> 4 quads at 1e-3 em tolerance
11: bake: 1654 monotone curves across ASCII (audit oracle 1654), 3490 stream points
11: ref '#': 32x46 rms= 2.28 max= 37 unclamped-peak=2.000
11: ref '$': 34x58 rms= 1.79 max= 35 unclamped-peak=2.000
11: ref '+': 32x32 rms= 2.71 max= 53 unclamped-peak=2.000
11: ref 'f': 20x46 rms= 1.25 max= 20 unclamped-peak=2.000
11: ref 't': 20x42 rms= 1.23 max= 23 unclamped-peak=2.000
11: ref 'H': 35x46 rms= 1.75 max= 51 unclamped-peak=1.876
11: ref '8': 35x49 rms= 1.49 max= 14 unclamped-peak=1.748
11: ref '@': 54x54 rms= 1.39 max= 17 unclamped-peak=1.504
11: ref '%': 47x47 rms= 1.19 max= 18 unclamped-peak=1.000
11: ref 'A': 41x46 rms= 0.48 max=  6 unclamped-peak=1.000
11: ref 'g': 31x46 rms= 1.35 max= 13 unclamped-peak=1.000
11: ref 'O': 43x49 rms= 1.05 max= 11 unclamped-peak=1.000
11: ref 'i':  7x46 rms= 1.03 max=  5 unclamped-peak=1.000
11: ref worst: rms=2.71 max=53 over 13 probes
11: ghost sweep @ 64px: 0 ghost pixels (worst 0 on ' ')
11: ghost sweep @200px: 0 ghost pixels (worst 0 on ' ')
11: runic bake: 89 of 89 runes carry ink
11: anotest_text: all checks passed
11/39 Test #11: anoptic_text .....................   Passed    1.14 sec
      Start 12: anoptic_ui
12: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_ui.exe
12: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
12: Test timeout computed to be: 60
12: anotest_ui: prim ABI, builder, reference evaluator (soak x1)
12:   sdf sign sweep: 15213 probes, 12 boundary-skipped
12:   cov rect r=0 frac  edgeMax 0.00000 cornerMax 0.12000 rms 0.00432 (1739 windows)
12:   cov rrect r=8      edgeMax 0.00000 cornerMax 0.03745 rms 0.00284 (3149 windows)
12:   cov rrect mixed    edgeMax 0.00000 cornerMax 0.12500 rms 0.00355 (3149 windows)
12:   cov ring w=3 mixed edgeMax 0.00000 cornerMax 0.12500 rms 0.00473 (3149 windows)
12:   clip rect interior rows worst 0.0000008
12:   gradient linear  interior worst 0.0000001
12:   gradient radial  interior worst 0.0000001
12:   gradient conic   interior worst 0.0000001
12:   gradient modulate  worst 0.0000001
12:   path rect interior 0.000000  edge 0.000000  (9 words, 4 seg)
12:   path hole ring 1.0000  hole 0.0000
12:   tiles demo     69x37  8028 entries (1565 solid)  worst |tiled-brute| 0.000393391
12:   tiles random   33x24  536 entries (105 solid)  worst |tiled-brute| 0.000000000
12:   tiles random   28x17  241 entries (22 solid)  worst |tiled-brute| 0.000000000
12:   shadow rect s=2         max 0.01324 (3.38/255) rms 0.00350 (4800 px)
12:   shadow rect s=8         max 0.01041 (2.66/255) rms 0.00310 (13824 px)
12:   shadow rrect r=8 s=4    max 0.01822 (4.65/255) rms 0.00392 (7296 px)
12:   demo scene: 15 prims, 2 clips, 1 paints, 7 curve words, bitwise-stable
12: anotest_ui: all passed
12/39 Test #12: anoptic_ui .......................   Passed    1.31 sec
      Start 13: anoptic_strbench
13/39 Test #13: anoptic_strbench .................***Not Run (Disabled)   0.00 sec
      Start 14: anoptic_sidbench
14/39 Test #14: anoptic_sidbench .................***Not Run (Disabled)   0.00 sec
      Start 15: anoptic_meshoptimizer
15: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_meshoptimizer.exe
15: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
15: Test timeout computed to be: 10
15: Running test_meshlet_bounds_calculation...
15: Running test_degenerate_triangles...
15: Running test_meshlet_limits...
15: Running test_bounds_checks...
15: Running test_vertex_cache_optimization...
15: Running test_simplify_scale...
15: Running test_simplify_passthrough...
15: Running test_simplify_degenerate_input...
15: Running test_simplify_grid...
15: Running test_simplify_error_budget...
15: Running test_simplify_guard_bridge...
15: Running test_simplify_guard_byte_identity...
15: Running test_simplify_flat_not_gutted...
15: Running test_simplify_concave_trench...
15: Running test_simplify_pillar_silhouette...
15: Running test_simplify_tetra_link...
15: All tests passed successfully!
15/39 Test #15: anoptic_meshoptimizer ............   Passed    0.04 sec
      Start 16: anoptic_memory
16: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_memory.exe
16: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
16: Test timeout computed to be: 30
16: huge-page reservation probe: status=0 (non-zero is acceptable)
16: anotest_memory: all checks passed
16/39 Test #16: anoptic_memory ...................   Passed    0.33 sec
      Start 17: anoptic_chariots
17/39 Test #17: anoptic_chariots .................***Not Run (Disabled)   0.00 sec
      Start 18: anoptic_logbench
18/39 Test #18: anoptic_logbench .................***Not Run (Disabled)   0.00 sec
      Start 19: anoptic_logtail
19/39 Test #19: anoptic_logtail ..................***Not Run (Disabled)   0.00 sec
      Start 20: anoptic_logfuzz
20: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_logfuzz.exe
20: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
20: Test timeout computed to be: 120
20: 
20: Timebase: invariant TSC @ 3399924265 Hz (QPC 10000000 Hz)
20: 
20: logfuzz: producers=6 iters=4000 enqueued=23482 lines=23482
20: logfuzz: PASS: every enqueued record survived (no loss, no duplication)
20/39 Test #20: anoptic_logfuzz ..................   Passed    0.98 sec
      Start 21: anoptic_blackbox
21: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_blackbox.exe
21: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
21: Test timeout computed to be: 240
21: 
21: Timebase: invariant TSC @ 3399868568 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_879023_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_926576_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_453611_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_244370_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399890426 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_371429_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_573415_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_215616_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399707646 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_058968_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399840342 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_771986_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399646039 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_825456_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_701565_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_024512_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_807424_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_512370_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_219720_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_131028_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_350359_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_852575_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_270943_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_197326_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_652397_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_661943_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399811118 Hz (QPC 10000000 Hz)
21: 
21: 04:54:45 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_661943_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_775469_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399828652 Hz (QPC 10000000 Hz)
21: 
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_291076_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399903642 Hz (QPC 10000000 Hz)
21: 
21: 04:54:45 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_291076_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_208491_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399922354 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_208491_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_069970_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399821543 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 2 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_208491_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_015599_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399803861 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 3 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_208491_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_787154_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399813774 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 4 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_787154_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_267364_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399823450 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_787154_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_001255_CRASH.log
21: 
21: Timebase: invariant TSC @ 3399940846 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2026-07-16_787154_CRASH.log.
21: 
21: Timebase: invariant TSC @ 3399861351 Hz (QPC 10000000 Hz)
21: 
21: 04:54:46 WARN  blackbox: 6 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests/logs/2006-01-01_000001_CRASH.log.
21: 
21: Timebase: invariant TSC @ 3399834224 Hz (QPC 10000000 Hz)
21: 
21: anotest_blackbox: all checks passed
21/39 Test #21: anoptic_blackbox .................   Passed    8.13 sec
      Start 22: anoptic_render_bridge
22: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_render_bridge.exe
22: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
22: Test timeout computed to be: 60
22: anotest_render_bridge: all checks passed
22/39 Test #22: anoptic_render_bridge ............   Passed    0.08 sec
      Start 23: anoptic_audio
23: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_audio.exe
23: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
23: Test timeout computed to be: 60
23: 
23: Timebase: invariant TSC @ 3399883960 Hz (QPC 10000000 Hz)
23: 
23: info: live telemetry ΓÇö blocks 28, cpu 9120 ns/block, underruns 0, clipped 0
23: anotest_audio: all passed (soak x1)
23/39 Test #23: anoptic_audio ....................   Passed    0.43 sec
      Start 24: anoptic_audiotone
24: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_audiotone.exe
24: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
24: Test timeout computed to be: 30
24: 
24: Timebase: invariant TSC @ 3399941722 Hz (QPC 10000000 Hz)
24: 
24: info: 48000 Hz, block 512 ΓÇö blocks 216, cpu 19620 ns/block, peak 0.000, underruns 0
24: anotest_audiotone: all passed
24/39 Test #24: anoptic_audiotone ................   Passed    2.35 sec
      Start 25: anoptic_audioscene
25: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_audioscene.exe "2"
25: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
25: Test timeout computed to be: 60
25: 
25: Timebase: invariant TSC @ 3399941456 Hz (QPC 10000000 Hz)
25: 
25: info: scene ΓÇö 5 clicks, blocks 219, cpu 70631 ns/block, underruns 0, clipped 0
25: anotest_audioscene: all passed (2 s scene)
25/39 Test #25: anoptic_audioscene ...............   Passed    2.37 sec
      Start 26: anoptic_audiodsp
26: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_audiodsp.exe
26: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
26: Test timeout computed to be: 60
26: anotest_audiodsp: all passed (soak x1)
26/39 Test #26: anoptic_audiodsp .................   Passed    0.10 sec
      Start 27: anoptic_synth
27: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_synth.exe
27: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
27: Test timeout computed to be: 300
27: info: journey ΓÇö 173.9 s, peak 0.850, C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests/journey_s42_synth.wav
27: anotest_synth: all passed
27/39 Test #27: anoptic_synth ....................   Passed   22.85 sec
      Start 28: anoptic_musichost
28: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_musichost.exe
28: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
28: Test timeout computed to be: 10000000
28: anotest_musichost: all passed
28/39 Test #28: anoptic_musichost ................   Passed    0.04 sec
      Start 29: anoptic_synthlive
29: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_synthlive.exe
29: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
29: Test timeout computed to be: 10000000
29: anotest_synthlive: all passed
29/39 Test #29: anoptic_synthlive ................   Passed    9.41 sec
      Start 30: anoptic_musicdrive
30: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_musicdrive.exe
30: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
30: Test timeout computed to be: 120
30: 
30: Timebase: invariant TSC @ 3399767074 Hz (QPC 10000000 Hz)
30: 
30:   bar composition: worst 256 us of the 10666 us block (2.4%)
30: anotest_musicdrive: all passed
30/39 Test #30: anoptic_musicdrive ...............   Passed   25.04 sec
      Start 31: anoptic_musicscene
31: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_musicscene.exe "30"
31: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
31: Test timeout computed to be: 120
31: 
31: Timebase: invariant TSC @ 3399940539 Hz (QPC 10000000 Hz)
31: 
31: game: calm                 (bar 0)
31: game: something is coming  (bar 1)
31: info: bar 2    Γöé peak 0.378 Γöé bar composed in 147 us of 10666 Γöé underruns 0
31: game: the fight            (bar 4)
31: game: SAVE at bar 7 (5 cues)
31: info: bar 4    Γöé peak 0.521 Γöé bar composed in 125 us of 10666 Γöé underruns 0
31:       ┬╖ bar 6    the key arrives: G mixolydian
31: game: the quiet after      (bar 9)
31: game: LOAD ΓÇö rebuilding bar 7 off the audio thread
31:       ┬╖ 62912-byte state rebuilt in 2 ms (never on the audio thread)
31:       ┬╖ seek consumed ΓÇö the save's bar 7 is next
31: info: bar 8    Γöé peak 0.763 Γöé bar composed in 195 us of 10666 Γöé underruns 0
31: note: 15 bars ΓÇö too short to reach a cadence once the steering has displaced it; run it longer to gate that
31: info: 30 s Γöé 2832 blocks Γöé bars 15 Γöé cadences 0 Γöé motifs 0 Γöé keys 1 Γöé cues 8
31: info: worst bar composed in 301 us of the 10666 us block (2.8%) Γöé late 0 Γöé dropped 0 Γöé underruns 0 Γöé clipped 0
31: anotest_musicscene: all passed (30 s)
31/39 Test #31: anoptic_musicscene ...............   Passed   30.27 sec
      Start 32: anoptic_synthscene
32: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_synthscene.exe "8"
32: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
32: Test timeout computed to be: 60
32: 
32: Timebase: invariant TSC @ 3399869609 Hz (QPC 10000000 Hz)
32: 
32: info: ~bar 1 Γöé peak 0.113 Γöé underruns 0
32: info: scene ΓÇö blocks 769, cpu 288641 ns/block, underruns 0, clipped 0, dropped 0
32: anotest_synthscene: all passed (8 s)
32/39 Test #32: anoptic_synthscene ...............   Passed    8.24 sec
      Start 33: anoptic_music
33: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_music.exe
33: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
33: Test timeout computed to be: 30
33: anotest_music: all passed
33/39 Test #33: anoptic_music ....................   Passed    0.98 sec
      Start 34: anoptic_render_slots
34: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_render_slots.exe
34: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
34: Test timeout computed to be: 10
34: anotest_render_slots: all checks passed
34/39 Test #34: anoptic_render_slots .............   Passed    0.01 sec
      Start 35: anotest_vk_lifecycle
35: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_vk_lifecycle.exe
35: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
35: Test timeout computed to be: 30
35: Starting Vulkan lifecycle test...
35: initVulkan() succeeded.
35: unInitVulkan() completed.
35/39 Test #35: anotest_vk_lifecycle .............   Passed    6.47 sec
      Start 36: anotest_vk_components
36: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_vk_components.exe
36: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
36: Test timeout computed to be: 30
36: Starting Vulkan components test...
36: Vulkan components test passed!
36/39 Test #36: anotest_vk_components ............   Passed    0.02 sec
      Start 37: anotest_vk_compliance_layers
37: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_vk_compliance_layers.exe
37: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
37: Test timeout computed to be: 30
37: Starting Vulkan Compliance Layer test...
37: Triggering intentional validation error (invalid buffer creation)...
37: Success: Intercepted 2 validation errors!
37/39 Test #37: anotest_vk_compliance_layers .....   Passed    6.36 sec
      Start 38: anotest_vk_memory
38: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_vk_memory.exe
38: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
38: Test timeout computed to be: 30
38: Starting Vulkan Memory Allocation test...
38: Memory Allocation test passed successfully!
38/39 Test #38: anotest_vk_memory ................   Passed    6.38 sec
      Start 39: anotest_vk_sync
39: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\Tests\tests\anotest_vk_sync.exe
39: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/Tests/tests
39: Test timeout computed to be: 30
39: Starting Vulkan Synchronization Primitives test...
39: Testing synchronous command buffer submission...
39: Testing asynchronous submission with Fences and Semaphores...
39: Triggering intentional validation error (invalid fence creation)...
39: Success: Sync test completed and caught intentional validation violations!
39/39 Test #39: anotest_vk_sync ..................   Passed    6.50 sec
100% tests passed, 0 tests failed out of 31
Total Test time (real) = 142.20 sec
The following tests did not run:
	  8 - anoptic_sortbench (Disabled)
	  9 - anoptic_stropsbench (Disabled)
	 10 - anoptic_logstrbench (Disabled)
	 13 - anoptic_strbench (Disabled)
	 14 - anoptic_sidbench (Disabled)
	 17 - anoptic_chariots (Disabled)
	 18 - anoptic_logbench (Disabled)
	 19 - anoptic_logtail (Disabled)
EXIT=0
DURATION_SEC=142.3
```

## CTest 〜 O3Tests

### Inventory (ctest -N)

```
Internal ctest changing into directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests
Test project C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests
  Test  #1: anoptic_time
  Test  #2: anoptic_logging
  Test  #3: anoptic_filesystem
  Test  #4: anoptic_strings
  Test  #5: anoptic_strings_utf
  Test  #6: anoptic_strings_sort
  Test  #7: anoptic_strings_fuzz
  Test  #8: anoptic_sortbench (Disabled)
  Test  #9: anoptic_stropsbench (Disabled)
  Test #10: anoptic_logstrbench (Disabled)
  Test #11: anoptic_text
  Test #12: anoptic_ui
  Test #13: anoptic_strbench (Disabled)
  Test #14: anoptic_sidbench (Disabled)
  Test #15: anoptic_meshoptimizer
  Test #16: anoptic_memory
  Test #17: anoptic_chariots (Disabled)
  Test #18: anoptic_logbench (Disabled)
  Test #19: anoptic_logtail (Disabled)
  Test #20: anoptic_logfuzz
  Test #21: anoptic_blackbox
  Test #22: anoptic_render_bridge
  Test #23: anoptic_audio
  Test #24: anoptic_audiotone
  Test #25: anoptic_audioscene
  Test #26: anoptic_audiodsp
  Test #27: anoptic_synth
  Test #28: anoptic_musichost
  Test #29: anoptic_synthlive
  Test #30: anoptic_musicdrive
  Test #31: anoptic_musicscene
  Test #32: anoptic_synthscene
  Test #33: anoptic_music
  Test #34: anoptic_render_slots
  Test #35: anotest_vk_lifecycle
  Test #36: anotest_vk_components
  Test #37: anotest_vk_compliance_layers (Disabled)
  Test #38: anotest_vk_memory
  Test #39: anotest_vk_sync (Disabled)

Total Tests: 39
```

### Full verbose run

Pass/fail progress lines and summaries from ctest -V (full log scratch/sweep-2026-07-16/ctest-O3Tests.log, ~27KB):

```
      Start  1: anoptic_time
1: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_time.exe
1: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
1: Test timeout computed to be: 30
1: Testing current date.
1: Current Date and Time: Thu Jul 16 04:56:54 2026
1: 
1: Testing timestamps across various resolutions
1: nanoseconds: 34561085452418
1: microseconds: 34561085452
1: milliseconds: 34561085
1: 
1: Testing timebase granularity (smallest resolvable step)
1:   [PASS] min step = 1 ticks = 0 ns (need < 100 ns)
1: 
1: ano_busywait resolution sweep:
1:   [PASS] busywait      1000ns  best=     1030ns  ceil<=101000ns
1:   [PASS] busywait     10000ns  best=    10030ns  ceil<=110000ns
1:   [PASS] busywait    100000ns  best=   100035ns  ceil<=200000ns
1:   [PASS] busywait   1000000ns  best=  1000037ns  ceil<=1100000ns
1:   [PASS] busywait  10000000ns  best= 10000044ns  ceil<=11000000ns
1: 
1: ano_sleep resolution sweep:
1:   [PASS] sleep       50us  best=   50052ns  floor>=29500ns  ceil<=2050000ns
1:   [PASS] sleep      100us  best=  100074ns  floor>=79000ns  ceil<=2100000ns
1:   [PASS] sleep      250us  best=  250052ns  floor>=227500ns  ceil<=2250000ns
1:   [PASS] sleep      500us  best=  500064ns  floor>=475000ns  ceil<=2500000ns
1:   [PASS] sleep     1000us  best= 1000067ns  floor>=970000ns  ceil<=3000000ns
1:   [PASS] sleep     2000us  best= 2000135ns  floor>=1960000ns  ceil<=4000000ns
1:   [PASS] sleep     5000us  best= 5000137ns  floor>=4930000ns  ceil<=7500000ns
1:   [PASS] sleep    10000us  best=10000134ns  floor>=9880000ns  ceil<=15000000ns
1:   [PASS] sleep    50000us  best=50000183ns  floor>=49480000ns  ceil<=75000000ns
1:   [PASS] sleep   100000us  best=100000198ns  floor>=98980000ns  ceil<=150000000ns
1: 
1: anoptic_time.h: All Tests passed!
 1/39 Test  #1: anoptic_time .....................   Passed    1.49 sec
      Start  2: anoptic_logging
2: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_logging.exe
2: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
2: Test timeout computed to be: 60
2: WARN  anotest_logging.c:724:  pre-init immediate (expected on stderr)
2:   [PASS] lifecycle_preinit
2:   [PASS] roundtrip
2:   [PASS] formatting
2:   [PASS] deferred_formatting
2:   [PASS] accumulation_order
2:   [PASS] level_gate
2:   [PASS] full_ring
2:   [PASS] immediate_order
2: 04:56:56 INFO  route term only 1
2: 04:56:56 INFO  route both 2
2: 04:56:56 INFO  rerouted info line
2: 04:56:56 FATAL a FATAL routed through the default BOTH|NOW route
2: WARN  anotest_logging.c:724:  post-cleanup immediate (expected on stderr)
2:   [PASS] routing
2:   [PASS] truncation
2:   [PASS] empty_message
2:   [PASS] concurrent
2:   [PASS] contention_1_flush_vs_write
2:   [PASS] contention_2_aba_bait
2:   [PASS] contention_3_config_thrash
2:   [PASS] edge_cap_boundary
2:   [PASS] edge_tiny_records
2:   [PASS] edge_ring_seam
2:   [PASS] edge_alternating_immediate
2:   [PASS] edge_output_dir_switch
2:   [PASS] edge_level_churn
2:   [PASS] contention_heavy_mixed
2:   [PASS] contention_soak
2:   [PASS] premature_join_all
2:   [PASS] premature_join_half
2:   [PASS] abuse_inputs
2:   [PASS] abuse_config
2:   [PASS] abuse_output_dir
2:   [PASS] visible_output
2:   [PASS] lifecycle_postcleanup
2:   Showcase log written and verified: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/./anolog_visible/2026-07-16_921510_ano.log
2: anoptic_logging: all cases passed.
 2/39 Test  #2: anoptic_logging ..................   Passed    0.41 sec
      Start  3: anoptic_filesystem
3: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_filesystem.exe
3: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
3: Test timeout computed to be: 30
3: userpath: "C:\Users\Pyrus\AppData\Roaming\anoptic" (length 38)
3: gamepath: "C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests" (length 54)
3: anotest_filesystem: all checks passed
 3/39 Test  #3: anoptic_filesystem ...............   Passed    0.06 sec
      Start  4: anoptic_strings
4: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_strings.exe
4: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
4: Test timeout computed to be: 30
4: anotest_strings: all checks passed
 4/39 Test  #4: anoptic_strings ..................   Passed    0.04 sec
      Start  5: anoptic_strings_utf
5: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_strings_utf.exe
5: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
5: Test timeout computed to be: 30
5: anotest_strings_utf: all checks passed
 5/39 Test  #5: anoptic_strings_utf ..............   Passed    0.05 sec
      Start  6: anoptic_strings_sort
6: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_strings_sort.exe
6: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
6: Test timeout computed to be: 60
6: anotest_strings_sort: all checks passed
 6/39 Test  #6: anoptic_strings_sort .............   Passed    0.06 sec
      Start  7: anoptic_strings_fuzz
7: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_strings_fuzz.exe
7: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
7: Test timeout computed to be: 120
7: anotest_strings_fuzz: all checks passed
 7/39 Test  #7: anoptic_strings_fuzz .............   Passed    0.05 sec
      Start  8: anoptic_sortbench
 8/39 Test  #8: anoptic_sortbench ................***Not Run (Disabled)   0.00 sec
      Start  9: anoptic_stropsbench
 9/39 Test  #9: anoptic_stropsbench ..............***Not Run (Disabled)   0.00 sec
      Start 10: anoptic_logstrbench
10/39 Test #10: anoptic_logstrbench ..............***Not Run (Disabled)   0.00 sec
      Start 11: anoptic_text
11: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_text.exe
11: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
11: Test timeout computed to be: 30
11: before init: FreeType version reads 0.0.0
11: after init: FreeType 2.13.3
11: after shutdown: FreeType version reads 0.0.0
11: cubic quarter-circle -> 4 quads at 1e-3 em tolerance
11: bake: 1654 monotone curves across ASCII (audit oracle 1654), 3490 stream points
11: ref '#': 32x46 rms= 2.28 max= 37 unclamped-peak=2.000
11: ref '$': 34x58 rms= 1.79 max= 35 unclamped-peak=2.000
11: ref '+': 32x32 rms= 2.71 max= 53 unclamped-peak=2.000
11: ref 'f': 20x46 rms= 1.25 max= 20 unclamped-peak=2.000
11: ref 't': 20x42 rms= 1.23 max= 23 unclamped-peak=2.000
11: ref 'H': 35x46 rms= 1.75 max= 51 unclamped-peak=1.876
11: ref '8': 35x49 rms= 1.49 max= 14 unclamped-peak=1.748
11: ref '@': 54x54 rms= 1.39 max= 17 unclamped-peak=1.504
11: ref '%': 47x47 rms= 1.19 max= 18 unclamped-peak=1.000
11: ref 'A': 41x46 rms= 0.48 max=  6 unclamped-peak=1.000
11: ref 'g': 31x46 rms= 1.35 max= 13 unclamped-peak=1.000
11: ref 'O': 43x49 rms= 1.05 max= 11 unclamped-peak=1.000
11: ref 'i':  7x46 rms= 1.03 max=  5 unclamped-peak=1.000
11: ref worst: rms=2.71 max=53 over 13 probes
11: ghost sweep @ 64px: 0 ghost pixels (worst 0 on ' ')
11: ghost sweep @200px: 0 ghost pixels (worst 0 on ' ')
11: runic bake: 89 of 89 runes carry ink
11: anotest_text: all checks passed
11/39 Test #11: anoptic_text .....................   Passed    0.31 sec
      Start 12: anoptic_ui
12: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_ui.exe
12: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
12: Test timeout computed to be: 60
12: anotest_ui: prim ABI, builder, reference evaluator (soak x1)
12:   sdf sign sweep: 15213 probes, 12 boundary-skipped
12:   cov rect r=0 frac  edgeMax 0.00000 cornerMax 0.12000 rms 0.00432 (1739 windows)
12:   cov rrect r=8      edgeMax 0.00000 cornerMax 0.03745 rms 0.00284 (3149 windows)
12:   cov rrect mixed    edgeMax 0.00000 cornerMax 0.12500 rms 0.00355 (3149 windows)
12:   cov ring w=3 mixed edgeMax 0.00000 cornerMax 0.12500 rms 0.00473 (3149 windows)
12:   clip rect interior rows worst 0.0000008
12:   gradient linear  interior worst 0.0000001
12:   gradient radial  interior worst 0.0000001
12:   gradient conic   interior worst 0.0000001
12:   gradient modulate  worst 0.0000001
12:   path rect interior 0.000000  edge 0.000000  (9 words, 4 seg)
12:   path hole ring 1.0000  hole 0.0000
12:   tiles demo     69x37  8028 entries (1565 solid)  worst |tiled-brute| 0.000393391
12:   tiles random   33x24  536 entries (105 solid)  worst |tiled-brute| 0.000000000
12:   tiles random   28x17  241 entries (22 solid)  worst |tiled-brute| 0.000000000
12:   shadow rect s=2         max 0.01324 (3.38/255) rms 0.00350 (4800 px)
12:   shadow rect s=8         max 0.01041 (2.66/255) rms 0.00310 (13824 px)
12:   shadow rrect r=8 s=4    max 0.01822 (4.65/255) rms 0.00392 (7296 px)
12:   demo scene: 15 prims, 2 clips, 1 paints, 7 curve words, bitwise-stable
12: anotest_ui: all passed
12/39 Test #12: anoptic_ui .......................   Passed    0.54 sec
      Start 13: anoptic_strbench
13/39 Test #13: anoptic_strbench .................***Not Run (Disabled)   0.00 sec
      Start 14: anoptic_sidbench
14/39 Test #14: anoptic_sidbench .................***Not Run (Disabled)   0.00 sec
      Start 15: anoptic_meshoptimizer
15: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_meshoptimizer.exe
15: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
15: Test timeout computed to be: 10
15: Running test_meshlet_bounds_calculation...
15: Running test_degenerate_triangles...
15: Running test_meshlet_limits...
15: Running test_bounds_checks...
15: Running test_vertex_cache_optimization...
15: Running test_simplify_scale...
15: Running test_simplify_passthrough...
15: Running test_simplify_degenerate_input...
15: Running test_simplify_grid...
15: Running test_simplify_error_budget...
15: Running test_simplify_guard_bridge...
15: Running test_simplify_guard_byte_identity...
15: Running test_simplify_flat_not_gutted...
15: Running test_simplify_concave_trench...
15: Running test_simplify_pillar_silhouette...
15: Running test_simplify_tetra_link...
15: All tests passed successfully!
15/39 Test #15: anoptic_meshoptimizer ............   Passed    0.03 sec
      Start 16: anoptic_memory
16: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_memory.exe
16: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
16: Test timeout computed to be: 30
16: huge-page reservation probe: status=0 (non-zero is acceptable)
16: anotest_memory: all checks passed
16/39 Test #16: anoptic_memory ...................   Passed    0.37 sec
      Start 17: anoptic_chariots
17/39 Test #17: anoptic_chariots .................***Not Run (Disabled)   0.00 sec
      Start 18: anoptic_logbench
18/39 Test #18: anoptic_logbench .................***Not Run (Disabled)   0.00 sec
      Start 19: anoptic_logtail
19/39 Test #19: anoptic_logtail ..................***Not Run (Disabled)   0.00 sec
      Start 20: anoptic_logfuzz
20: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_logfuzz.exe
20: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
20: Test timeout computed to be: 120
20: logfuzz: producers=6 iters=4000 enqueued=23482 lines=23482
20: logfuzz: PASS: every enqueued record survived (no loss, no duplication)
20/39 Test #20: anoptic_logfuzz ..................   Passed    1.06 sec
      Start 21: anoptic_blackbox
21: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_blackbox.exe
21: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
21: Test timeout computed to be: 240
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_139892_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_670897_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_697702_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_145022_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_512936_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_631588_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_402713_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_123887_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_253061_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_094524_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_619553_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_140546_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_817344_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_736956_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_387445_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_314513_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_494440_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_057556_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_669495_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_026799_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_804661_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_524783_CRASH.log
21: 04:57:06 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_524783_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_160980_CRASH.log
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_626625_CRASH.log
21: 04:57:06 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_626625_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_690894_CRASH.log
21: 04:57:06 WARN  blackbox: 1 crash log detected, C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_690894_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_722586_CRASH.log
21: 04:57:06 WARN  blackbox: 2 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_722586_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_089512_CRASH.log
21: 04:57:07 WARN  blackbox: 3 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_722586_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_709320_CRASH.log
21: 04:57:07 WARN  blackbox: 4 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_722586_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_451120_CRASH.log
21: 04:57:07 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_722586_CRASH.log.
21: anoptic: fatal exception, blackbox record written to C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_788721_CRASH.log
21: 04:57:07 WARN  blackbox: 5 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2026-07-16_788721_CRASH.log.
21: 04:57:07 WARN  blackbox: 6 crash logs detected, newest C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests/logs/2006-01-01_000001_CRASH.log.
21: anotest_blackbox: all checks passed
21/39 Test #21: anoptic_blackbox .................   Passed    8.23 sec
      Start 22: anoptic_render_bridge
22: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_render_bridge.exe
22: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
22: Test timeout computed to be: 60
22: anotest_render_bridge: all checks passed
22/39 Test #22: anoptic_render_bridge ............   Passed    0.03 sec
      Start 23: anoptic_audio
23: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_audio.exe
23: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
23: Test timeout computed to be: 60
23: info: live telemetry ΓÇö blocks 28, cpu 3900 ns/block, underruns 0, clipped 0
23: anotest_audio: all passed (soak x1)
23/39 Test #23: anoptic_audio ....................   Passed    0.41 sec
      Start 24: anoptic_audiotone
24: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_audiotone.exe
24: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
24: Test timeout computed to be: 30
24: info: 48000 Hz, block 512 ΓÇö blocks 217, cpu 8270 ns/block, peak 0.000, underruns 0
24: anotest_audiotone: all passed
24/39 Test #24: anoptic_audiotone ................   Passed    2.34 sec
      Start 25: anoptic_audioscene
25: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_audioscene.exe "2"
25: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
25: Test timeout computed to be: 60
25: info: scene ΓÇö 5 clicks, blocks 218, cpu 20630 ns/block, underruns 0, clipped 0
25: anotest_audioscene: all passed (2 s scene)
25/39 Test #25: anoptic_audioscene ...............   Passed    2.36 sec
      Start 26: anoptic_audiodsp
26: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_audiodsp.exe
26: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
26: Test timeout computed to be: 60
26: anotest_audiodsp: all passed (soak x1)
26/39 Test #26: anoptic_audiodsp .................   Passed    0.04 sec
      Start 27: anoptic_synth
27: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_synth.exe
27: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
27: Test timeout computed to be: 300
27: info: journey ΓÇö 173.9 s, peak 0.850, C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests/journey_s42_synth.wav
27: anotest_synth: all passed
27/39 Test #27: anoptic_synth ....................   Passed    6.51 sec
      Start 28: anoptic_musichost
28: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_musichost.exe
28: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
28: Test timeout computed to be: 10000000
28: anotest_musichost: all passed
28/39 Test #28: anoptic_musichost ................   Passed    0.03 sec
      Start 29: anoptic_synthlive
29: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_synthlive.exe
29: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
29: Test timeout computed to be: 10000000
29: anotest_synthlive: all passed
29/39 Test #29: anoptic_synthlive ................   Passed    2.37 sec
      Start 30: anoptic_musicdrive
30: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_musicdrive.exe
30: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
30: Test timeout computed to be: 120
30:   bar composition: worst 121 us of the 10666 us block (1.1%)
30: anotest_musicdrive: all passed
30/39 Test #30: anoptic_musicdrive ...............   Passed    8.00 sec
      Start 31: anoptic_musicscene
31: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_musicscene.exe "30"
31: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
31: Test timeout computed to be: 120
31: game: calm                 (bar 0)
31: game: something is coming  (bar 1)
31: info: bar 2    Γöé peak 0.378 Γöé bar composed in 85 us of 10666 Γöé underruns 0
31: game: the fight            (bar 4)
31: game: SAVE at bar 7 (5 cues)
31: info: bar 4    Γöé peak 0.521 Γöé bar composed in 84 us of 10666 Γöé underruns 0
31:       ┬╖ bar 6    the key arrives: G mixolydian
31: game: the quiet after      (bar 9)
31: game: LOAD ΓÇö rebuilding bar 7 off the audio thread
31:       ┬╖ 62912-byte state rebuilt in 1 ms (never on the audio thread)
31:       ┬╖ seek consumed ΓÇö the save's bar 7 is next
31: info: bar 8    Γöé peak 0.763 Γöé bar composed in 107 us of 10666 Γöé underruns 0
31: note: 15 bars ΓÇö too short to reach a cadence once the steering has displaced it; run it longer to gate that
31: info: 30 s Γöé 2831 blocks Γöé bars 15 Γöé cadences 0 Γöé motifs 0 Γöé keys 1 Γöé cues 8
31: info: worst bar composed in 135 us of the 10666 us block (1.3%) Γöé late 0 Γöé dropped 0 Γöé underruns 0 Γöé clipped 0
31: anotest_musicscene: all passed (30 s)
31/39 Test #31: anoptic_musicscene ...............   Passed   30.27 sec
      Start 32: anoptic_synthscene
32: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_synthscene.exe "8"
32: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
32: Test timeout computed to be: 60
32: info: ~bar 1 Γöé peak 0.113 Γöé underruns 0
32: info: scene ΓÇö blocks 769, cpu 67271 ns/block, underruns 0, clipped 0, dropped 0
32: anotest_synthscene: all passed (8 s)
32/39 Test #32: anoptic_synthscene ...............   Passed    8.24 sec
      Start 33: anoptic_music
33: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_music.exe
33: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
33: Test timeout computed to be: 30
33: anotest_music: all passed
33/39 Test #33: anoptic_music ....................   Passed    0.49 sec
      Start 34: anoptic_render_slots
34: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_render_slots.exe
34: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
34: Test timeout computed to be: 10
34: anotest_render_slots: all checks passed
34/39 Test #34: anoptic_render_slots .............   Passed    0.01 sec
      Start 35: anotest_vk_lifecycle
35: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_vk_lifecycle.exe
35: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
35: Test timeout computed to be: 30
35: Starting Vulkan lifecycle test...
35: initVulkan() succeeded.
35: unInitVulkan() completed.
35/39 Test #35: anotest_vk_lifecycle .............   Passed    2.53 sec
      Start 36: anotest_vk_components
36: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_vk_components.exe
36: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
36: Test timeout computed to be: 30
36: Starting Vulkan components test...
36: Vulkan components test passed!
36/39 Test #36: anotest_vk_components ............   Passed    0.01 sec
      Start 37: anotest_vk_compliance_layers
37/39 Test #37: anotest_vk_compliance_layers .....***Not Run (Disabled)   0.00 sec
      Start 38: anotest_vk_memory
38: Test command: C:\Users\Pyrus\Code\anoptic-engine\build\O3Tests\tests\anotest_vk_memory.exe
38: Working Directory: C:/Users/Pyrus/Code/anoptic-engine/build/O3Tests/tests
38: Test timeout computed to be: 30
38: Starting Vulkan Memory Allocation test...
38: Memory Allocation test passed successfully!
38/39 Test #38: anotest_vk_memory ................   Passed    2.48 sec
      Start 39: anotest_vk_sync
39/39 Test #39: anotest_vk_sync ..................***Not Run (Disabled)   0.00 sec
100% tests passed, 0 tests failed out of 29
Total Test time (real) =  78.89 sec
The following tests did not run:
	  8 - anoptic_sortbench (Disabled)
	  9 - anoptic_stropsbench (Disabled)
	 10 - anoptic_logstrbench (Disabled)
	 13 - anoptic_strbench (Disabled)
	 14 - anoptic_sidbench (Disabled)
	 17 - anoptic_chariots (Disabled)
	 18 - anoptic_logbench (Disabled)
	 19 - anoptic_logtail (Disabled)
	 37 - anotest_vk_compliance_layers (Disabled)
	 39 - anotest_vk_sync (Disabled)
EXIT=0
DURATION_SEC=78.9
```

## Hand-run benchmarks (DISABLED in ctest)

Working directory: `build/O3Tests/tests`. Also ran extra bench-like exes present but not in the DISABLED list: mempoolbench, resbench, ringbench.

### sortbench

```
inventory: 6000 items, 1500 distinct names, 85356 bytes of text, 40 reps/series

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
qsort+collate (baseline)            40  9426842.3  8898003  9195207  9579362 13484482 13484482   13484482      0
anostr_sort                         40   800413.0   775789   795200   811740   909021   909021     909021      0
anostr_sort (presorted)             40   363403.5   353664   365704   366644   387114   387114     387114      0
anostr_sort_idx                     40   794304.7   779319   793250   800790   824670   824670     824670      0
anostr_sym_sort (warm)              40   607241.3   590707   606287   618437   630348   630348     630348      0
qsort bytes (floor)                 40   832578.8   818990   831660   843570   852450   852450     852450      0

anostr_sym_sort (cold, builds key cache): 653908 ns once
collate_prefix: 30 ns/string (33.0 M strings/s)
collate_key:    322 ns/string (full 3-level key + tiebreak)
replace_all:    1.65 GB/s over 1048616 bytes (1048616-byte result)
cull ws+punct:  0.90 GB/s over 1048616 bytes (797860-byte result)
rune_sort:      225 ns/string
EXIT=0
DURATION_SEC=0.6
```

### stropsbench

```
string ops: find over 4 MiB, replace/cull over 1 MiB, 30 reps/series

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
find: hit at far end                30   215209.3   196222   198852   252733   318124   318124     318124      0
                             scan 21.09 GB/s
find: miss (full scan)              30   208638.5   196852   197812   259103   269173   269173     269173      0
                             scan 21.20 GB/s
find: common first byte             30  3275312.8  3243695  3273595  3292706  3300036  3300036    3300036      0
find: rare first byte               30   197494.0   195762   197122   200112   202992   202992     202992      0
find: 44-byte needle                30  1653394.0  1638192  1650333  1666693  1682973  1682973    1682973      0

replace: same-size sparse           30   930271.2   916272   926162   942953  1018064  1018064    1018064      0
                             1.13 GB/s
replace: same-size dense            30  1388636.3  1362509  1390479  1397619  1404289  1404289    1404289      0
                             0.75 GB/s
replace: grow (dog->direwolf)        30   836513.3   827551   835491   840251   854011   854011     854011      0
replace: shrink (the->a)            30   925279.7   916492   924022   928473   954643   954643     954643      0
replace: delete all spaces          30  3598968.1  3558769  3587060  3628970  3671601  3671601    3671601      0
replace: UTF-8 needle e-acute        30  1007454.2   996483  1006024  1020734  1034534  1034534    1034534      0
replace: no match (identity)        30    49707.7    48990    49810    49850    53210    53210      53210      0
                             21.05 GB/s (count pass only, zero alloc)

cull: ws+punct, 1 MiB               30  1130844.8  1107425  1126055  1151086  1228736  1228736    1228736      0
                             0.93 GB/s
cull: no-op (clean doc)             30   433767.3   430206   432566   435676   463836   463836     463836      0
                             2.42 GB/s (scan only, same backing out)
rune_sort: 4 KiB mixed page         30   100643.0    97431    98501   104311   123331   123331     123331      0
EXIT=0
DURATION_SEC=0.6
```

### logstrbench

```
Logger x strings: %.*s-captured UTF-8 item names, 50000 msgs/producer

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
log anostr @ 1 producer          50000       65.7       30       49       60       90      170     230734      0
log anostr @ 4 producers        200000      304.9       30      100      220      389      750     395244      0
log anostr @ 8 producers        400000      644.8       30      340      800     1500   115587     265146      0

no-loss oracle: 650257 lines == 650257 records
byte-transparency oracle: sentinel UTF-8 intact in the file
EXIT=0
DURATION_SEC=0.6
```

### strbench

```
anotest_strbench: 100000 pairs per series, 64 compares per sample

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
inline8  anostr_compare           1562        2.2        1        2        3        8       12         37      0
inline8  memcmp                   1562        3.0        2        2        4        8      157        341      0
long32   anostr_compare           1562        1.8        1        2        2        7        7          7      0
long32   memcmp                   1562        6.4        3        6        9       14       20         22      0
shared16 anostr_compare           1562        8.9        5        8       12       17       41        194      0
shared16 memcmp                   1562        5.5        3        5        7       11       13        127      0
EXIT=0
DURATION_SEC=0.1
```

### sidbench

```
anotest_sidbench: 200000 events over 16 types, 64 dispatches per sample

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
strcmp chain                      3125       25.7       21       25       30       33       70        314      0
anostr_eq chain                   3125       10.3        8       10       11       11       14        158      0
hash64 + sid switch               3125       16.6       14       17       17       18       19         74      0
intern_find + sym                 3125       17.2       15       17       18       19       31        131      0
sid switch (baked)                3125        9.0        7        9       10       10       11         66      0

bulk keying, 20000 distinct identifiers:
  runtime intern (insert):      1.19 ms total,   59.6 ns/key,   16775287 keys/s
  runtime re-key (find):        0.72 ms total,   35.8 ns/key,   27895715 keys/s
  comptime ANOSTR_SID:          0.00 ms total,    0.0 ns/key (baked at build)

anotest_bulkreads: 50000 records, 2000000 lookups, 64 reads per sample

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
intern_find + sym                31250       75.7       60       73       90      111      136        762      0
sid map (int only)               31250        7.5        4        7        9       15       22        659      0
hash + eq confirm                31250       54.1       45       51       63      107      136        710      0
sorted-sid bsearch               31250       98.5       88       97      103      116      180        765      0

throughput (lookups/s):
  intern_find + sym:       13096767  (hash + probe + anostr_eq, touches bytes)
  sid map (int only):     122503599  (integer-only probe, what a baked ANOSTR_SID buys)
  hash + eq confirm:       18278549  (hash + anostr_eq, touches bytes)
  sorted-sid bsearch:      10084630  (log n integer compares, no hashing)
note: strategy 2 is integer-only reads (the ANOSTR_SID payoff); 1 and 3 hash and touch
      string bytes; 4 pays log n integer compares.
EXIT=0
DURATION_SEC=1.3
```

### chariots

```
Cleanup function received number of value of: 64
1690564506287304995	1310960229923702647

1776177623237123
1690564506020168772	0

EXIT=0
DURATION_SEC=0.1
```

### logbench

```
Anoptic logger benchmark -- ring (lock-free MPSC) vs mutex baseline
  latency:    512 enqueues x 4000 rounds, single thread, fast path
  throughput: 200000 msgs/producer, one concurrent flusher

metric                               ring          mutex ring/mutex
-------------------------------------------------------------------------
enqueue latency (1 thread)         40.6 ns        660.1 ns     16.26x
throughput @ 1 producer        13.67 M/s        1.23 M/s     11.10x
throughput @ 2 producers       14.72 M/s        2.01 M/s      7.31x
throughput @ 4 producers       14.53 M/s        2.54 M/s      5.72x
throughput @ 8 producers       14.01 M/s        2.05 M/s      6.85x
throughput @ 16 producers      11.08 M/s        2.06 M/s      5.37x

variable-length messages -- 100000 msgs/producer, random 8..1024 B, seed 0xA0B1C2
-------------------------------------------------------------------------
varlen @ 1 producer             0.87 M/s        0.42 M/s      2.10x
varlen @ 2 producers            1.74 M/s        0.24 M/s      7.36x
varlen @ 4 producers            3.41 M/s        0.28 M/s     12.31x
varlen @ 8 producers            5.67 M/s        0.33 M/s     17.21x
varlen @ 16 producers           4.15 M/s        0.34 M/s     12.09x

mixed small+large messages -- 100000 msgs/producer, 16B / 2048B 50:50, seed 0x5E3D17
-------------------------------------------------------------------------
mixed @ 1 producer              0.42 M/s        0.11 M/s      3.69x
mixed @ 2 producers             0.88 M/s        0.13 M/s      6.71x
mixed @ 4 producers             1.75 M/s        0.16 M/s     10.83x
mixed @ 8 producers             3.00 M/s        0.19 M/s     15.38x
mixed @ 16 producers            2.15 M/s        0.16 M/s     13.06x

(ring/mutex > 1.0 means the ring won. Latency column inverts the ratio so >1 is
 always "ring better". Numbers vary run to run; take the trend, not the digits.)
EXIT=0
DURATION_SEC=38.2
```

### logtail

```
Anoptic logger tail benchmark -- per-call enqueue latency percentiles
  100000 msgs/producer, logger's own drain thread consuming

series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
timer overhead                    4096        7.7        0       10       10       10       10        230      0
enqueue @ 1 producer            100000       41.4       39       40       49       50       70       7050      0
enqueue @ 2 producers           200000       92.5       39       70       80      100      150     197723      0
enqueue @ 4 producers           400000      180.5       39      100      250      460      890     206413      0
enqueue @ 8 producers           800000      435.8       39      290      720     1370     4560     613650      0
enqueue @ 16 producers         1600000      967.3       39      550     1560     3380   123912     384346      0

(Full-ring waits are part of the tail by design: the producer self-throttles to the
 drain rate rather than dropping. Numbers vary run to run; take the trend.)
EXIT=0
DURATION_SEC=0.5
```

### mempoolbench

```
== churn: 400000 ops, 1024-slot working set ==
series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
(warm)                          400000       15.6        0       10       20       50      640      27611      0
                               wall 11.5 ms, 34.72 Mops/s
(warm)                          400000       23.1        0       10       30       90     1870      18200      0
                               wall 17.5 ms, 22.88 Mops/s
(warm-up pass done)
mi_heap      <=4K               400000       13.6        0       10       20       40      270       5500      0
                               wall 9.6 ms, 41.84 Mops/s
multipool    <=4K               400000       10.0        0       10       10       39       80      76823      0
                               wall 8.0 ms, 50.03 Mops/s
mp<monotonic><=4K               400000        9.7        0       10       10       30       70      33261      0
                               wall 7.9 ms, 50.57 Mops/s
mi_heap      <=64K              400000       22.6        0       10       30      100     1600      19350      0
                               wall 17.7 ms, 22.62 Mops/s
multipool    <=64K              400000       20.2        0       10       50      100      370      33021      0
                               wall 17.9 ms, 22.29 Mops/s
mp<monotonic><=64K              400000       17.7        0       10       50       90      360      47382      0
                               wall 15.3 ms, 26.10 Mops/s

== batch-and-wink: 200000 allocs <= 1 KiB, teardown included ==
mi_heap malloc + per-object free             best of 8:    27.63 ms  (7.24 Mops/s)
mi_heap malloc + heap wink-out               best of 8:    24.49 ms  (8.17 Mops/s)
monotonic + destroy (cold slabs)             best of 8:     8.64 ms  (23.14 Mops/s)
monotonic + reset (warm slabs)               best of 8:     2.47 ms  (80.97 Mops/s)

sink=14733113812879684448
EXIT=0
DURATION_SEC=0.8
```

### resbench

```
anotest_resbench: explicit lifetime domains
series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
churn/g0 get(load)               50067    93313.6    45840    69111   116282   706535  1125084    1518252      0
churn/g0 unload                  49933      516.2       90      330      560     2910    43680     126302      0
churn/g0 get(hit)                20000       93.6       70       90      100      130      210       3520      0
                               wall 4728.0 ms, 0.02 Mops/s
churn/scope get(load)            50065   105070.2    47811    72581   130162   771116  1040702    6879869      0
churn/scope unload               49935      570.6       90      400      600     3800    43590     151033      0
churn/scope get(hit)             20000       94.0       70       90      100      130      170       5800      0
                               wall 5322.3 ms, 0.02 Mops/s
(b) cycle  1: live +33120 B, chunks +8384 B (over baseline; NOT summed)
(b) cycle 25: live +33120 B, chunks +8384 B (over baseline; NOT summed)
(b) cycle 50: live +33120 B, chunks +8384 B (over baseline; NOT summed)
level cycle                         50 17320069.2 16475187 16990608 18488850 19864380 19864380   19864380      0
retire only                         50   798705.6   735545   788327   840958   997241   997241     997241      0
(b) ~19.6 MiB ingested per cycle; after cycle 1: live +33120 B, chunks +8384 B
FAIL: no live leak across cycles (C:/Users/Pyrus/Code/anoptic-engine/tests/anotest_resbench.c:329)
FAIL: retired domains return chunk bytes to baseline (C:/Users/Pyrus/Code/anoptic-engine/tests/anotest_resbench.c:331)
direct release                     200      198.3      170      190      210      330      550        550      0
(c) 200/200 zero-copy hand-offs, 1564 hand-offs/s (get+release+free)
(c) ingest assets_real/sponza/2.0/Sponza/glTF/Sponza.gltf: best 14.02 ms, 1482.6 MB/s conditioned
sink 11173490524532878415
anotest_resbench: 2 check(s) failed
EXIT=1
DURATION_SEC=11.5
```

### ringbench

```
ring grid: 1000000 items, cap 1024, lock-free vs mutex baseline
series (ns)                          n       mean      min      p50      p90      p99    p99.9        max   lost
spsc 1x1 s16                    200000       36.3        0       30       70      110      130      10520 800000
                               wall 70.8 ms, 14.12 Mops/s through
mutexq 1x1 s16                  200000       63.8        0       10       20      760     8150      24230 800000
                               wall 75.5 ms, 13.24 Mops/s through
mpsc 4x1 s16                    200000      352.8        0      269      720     1390     2170      42940  50000
                               wall 79.7 ms, 12.54 Mops/s through
mutexq 4x1 s16                  200000      474.8        0       10       50    11968    47180     467344  50000
                               wall 101.1 ms, 9.89 Mops/s through
mpsc 8x1 s16                    125000     1181.1        0      510     3140     7750    13330      29980      0
                               wall 149.1 ms, 6.71 Mops/s through
mutexq 8x1 s16                  125000     2021.6        0       10     3080    42700    86769     415513      0
                               wall 255.2 ms, 3.92 Mops/s through
spmc 1x4 s16                    200000       76.6        0       70      140      200      250      10479 800000
                               wall 92.4 ms, 10.83 Mops/s through
mutexq 1x4 s16                  200000      313.2        0       10      240     7649    18330     103333 800000
                               wall 302.8 ms, 3.30 Mops/s through
spmc 1x8 s16                    200000      175.8        0      160      350      550      730      24300 800000
                               wall 193.1 ms, 5.18 Mops/s through
mutexq 1x8 s16                  200000      976.5        0       10     2779    15709    27530     130511 800000
                               wall 894.9 ms, 1.12 Mops/s through
mpmc 4x4 s16                    200000      545.5        0      310     1250     2910     4900       9900  50000
                               wall 131.0 ms, 7.64 Mops/s through
mutexq 4x4 s16                  200000      604.5        0       10      690    12819    27370     182801  50000
                               wall 139.3 ms, 7.18 Mops/s through
mpmc 8x8 s16                    125000     1272.4        0      840     2850     6580    10760      85360      0
                               wall 165.6 ms, 6.04 Mops/s through
mutexq 8x8 s16                  125000     1802.6        0       20     4700    27929    53420     743507      0
                               wall 228.6 ms, 4.37 Mops/s through
spsc 1x1 s64                    200000       23.8        0       20       40       90      120      12090 800000
                               wall 43.5 ms, 22.97 Mops/s through
mutexq 1x1 s64                  200000      118.2        0       20       30     1830    19731      74048 800000
                               wall 125.7 ms, 7.96 Mops/s through
mpsc 4x1 s64                    200000      273.0       10      220      520      940     1410     101160  50000
                               wall 65.7 ms, 15.21 Mops/s through
mutexq 4x1 s64                  200000      786.2        0       20       80    17851    82120     799118  50000
                               wall 166.3 ms, 6.01 Mops/s through
mpsc 8x1 s64                    125000      755.5       10      580     1540     2970     4570      35980      0
                               wall 102.4 ms, 9.77 Mops/s through
mutexq 8x1 s64                  125000     3361.1        0       30     9260    56590   116112     928028      0
                               wall 422.2 ms, 2.37 Mops/s through
spmc 1x4 s64                    200000       52.4        0       50      100      140      180      28900 800000
                               wall 67.1 ms, 14.90 Mops/s through
mutexq 1x4 s64                  200000      444.8        0       20      460    10720    21682     132848 800000
                               wall 451.7 ms, 2.21 Mops/s through
spmc 1x8 s64                    200000       95.6        9       90      160      200      350      15110 800000
                               wall 108.5 ms, 9.21 Mops/s through
mutexq 1x8 s64                  200000     1060.5        0       30     2920    15820    27770     277372 800000
                               wall 1071.7 ms, 0.93 Mops/s through
mpmc 4x4 s64                    200000      261.8        0      220      450      910     2250     125341  50000
                               wall 75.6 ms, 13.23 Mops/s through
mutexq 4x4 s64                  200000      719.1        0       29      680    14318    31620     486586  50000
                               wall 170.3 ms, 5.87 Mops/s through
mpmc 8x8 s64                    125000      960.5       10      650     2160     4590     7090      38160      0
                               wall 154.4 ms, 6.48 Mops/s through
mutexq 8x8 s64                  125000     1881.0        0       30     5220    28010    54151    1738696      0
                               wall 238.1 ms, 4.20 Mops/s through
ticket 8x500k: FAA 41.5 ms (96.3 Mops/s)  vs  mutex 77.7 ms (51.5 Mops/s)
sink 169737129754239558
anotest_ringbench: bar held on every >=4-thread point
EXIT=0
DURATION_SEC=6.6
```

## FPS bench (bench_fps_win64.py)

Also written to `docs/benchmarks/2026-07-16-sweep.md`. Duration 181.8 s, EXIT=0.

```
ENV_VARS: ANO_SHADOW_BUDGET=2
display: primary monitor 3840x2160 px (physical; DPI-aware), the largest realizable framebuffer
     target front      render  swapMiB  avgFPS     p50   1%low 0.1%low   maxms   GPUms  GPUcap  w/cap frusta  bound
    640x360 FRONT     640x360     41.4  1014.1  1012.0   509.7   445.6   2.922   0.525    1905   0.53    8.0  CPU/present
    960x540 FRONT     960x540     97.6   950.7   948.1   491.2   435.0   2.758   0.585    1709   0.55    8.0  CPU/present
   1280x720 FRONT    1280x720    164.7   908.9   910.8   486.0   437.6   3.960   0.615    1626   0.56    8.0  CPU/present
  1920x1080 FRONT   1920x1080    360.1   785.7   787.0   444.2   375.2   3.235   0.759    1318   0.60    8.0  CPU/present
  2560x1440 FRONT   2560x1440    642.3   645.6   647.7   382.3   337.7   3.841   0.989    1011   0.64    8.0  CPU/present
  3840x2160 FRONT   3840x2160   1406.3   576.2   578.7   378.2   364.3   3.244   1.698     589   0.98    8.0  GPU
EXIT=0
DURATION_SEC=181.8
```

## Summary table

| Suite | Passed | Failed | Disabled/Skipped | Notes |
| --- | --- | --- | --- | --- |
| build.bat 1–5, 8 | 6 | 0 | 〜 | all linked/ran |
| build.bat 6 ASan | 0 | 1 | expected | Windows unsupported message |
| build.bat 7 TSan | 0 | 1 | expected | Windows unsupported message |
| CTest HeadlessDebug | 25 | 0 | 8 | 33 listed; 119.7 s |
| CTest Tests (Debug) | 31 | 0 | 8 | 39 listed; vulkan included; 142.3 s |
| CTest O3Tests | 29 | 0 | 10 | 39 listed; vk_compliance_layers + vk_sync Disabled; 78.9 s |
| Hand-run DISABLED benches (8) | 8 | 0 | 〜 | sort/strops/logstr/str/sid/chariots/logbench/logtail |
| Extra hand-run benches | 2 | 1 | 〜 | mempoolbench OK, ringbench OK, resbench FAIL (2 checks) |
| FPS bench | 1 | 0 | 〜 | 6/6 FRONT rows, 4K GPU-bound |

### Failures / expected skips
- build.bat 6 and 7: expected fail on Windows (sanitizers Linux/macOS-only).
- anotest_resbench.exe: EXIT=1 〜 `FAIL: no live leak across cycles` and `FAIL: retired domains return chunk bytes to baseline` (tests/anotest_resbench.c:329,331). Not part of default CTest.
- CTest Disabled (all three suites): sortbench, stropsbench, logstrbench, strbench, sidbench, chariots, logbench, logtail.
- O3Tests additionally Disabled: anotest_vk_compliance_layers, anotest_vk_sync (both Passed under build/Tests Debug re-run).

### Verdict
Windows build matrix green except intentional sanitizer skips. All CTest runnable suites 100% pass. Hand-run DISABLED benches green. FPS sweep completed cleanly. Only unexpected failure: hand-run `anotest_resbench` leak assertions.

