# Test executables link anoptic_core. Exit 0 = pass.

# Per-sanitizer TIMEOUT scaling. A sanitized binary is 2-20x slower, and a suite that goes
# red on the clock under TSan teaches everyone to ignore the clock. Every TIMEOUT below is
# expressed through these, so a slow lane is a slow lane and not a failure.
set(ANO_T_SCALE 1)
if(ANOPTIC_SANITIZE STREQUAL "asan")
    set(ANO_T_SCALE 4)
elseif(ANOPTIC_SANITIZE STREQUAL "tsan")
    set(ANO_T_SCALE 8)
endif()
math(EXPR ANO_T10  "10  * ${ANO_T_SCALE}")
math(EXPR ANO_T30  "30  * ${ANO_T_SCALE}")
math(EXPR ANO_T60  "60  * ${ANO_T_SCALE}")
math(EXPR ANO_T120 "120 * ${ANO_T_SCALE}")
math(EXPR ANO_T240 "240 * ${ANO_T_SCALE}")

# Every resource-manager test carries LABELS "resource", so `ctest -L resource` is the
# module's gate and the verification matrix has one name to quote.

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Testing for ``anoptic_log.h``
# Scratch dirs are anchored at runtime to the test exe's own directory (ano_fs_chdir_gamepath in
# main / scratch_anchor_to_exe), so output never lands in the caller's CWD and a cross-built exe
# writes to a valid path on its own OS -- no compile-time build path is baked in.
add_executable(anotest_logging anotest_logging.c)
target_link_libraries(anotest_logging PRIVATE anoptic_core)
add_test(NAME anoptic_logging COMMAND anotest_logging)
set_tests_properties(anoptic_logging PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;concurrency")

# Testing for ``anoptic_filesystem.h`` (ano_fspath values, game/user paths, append-file API).
# Note: the userpath check creates the real per-user game directory and one probe file in it
# (removed on exit) -- that is the function's actual contract, so it is tested for real.
add_executable(anotest_filesystem anotest_filesystem.c)
target_link_libraries(anotest_filesystem PRIVATE anoptic_core)
add_test(NAME anoptic_filesystem COMMAND anotest_filesystem)
set_tests_properties(anoptic_filesystem PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Testing for ``anoptic_strings.h`` (the 16-byte value type, builder, keep, slicing)
add_executable(anotest_strings anotest_strings.c)
target_link_libraries(anotest_strings PRIVATE anoptic_core)
add_test(NAME anoptic_strings COMMAND anotest_strings)
set_tests_properties(anoptic_strings PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;mem")

# Testing for ``anoptic_strings_utf.h`` (UTF-8 iteration/encode, Unicode case + classification)
add_executable(anotest_strings_utf anotest_strings_utf.c)
target_link_libraries(anotest_strings_utf PRIVATE anoptic_core)
add_test(NAME anoptic_strings_utf COMMAND anotest_strings_utf)
set_tests_properties(anoptic_strings_utf PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Collation sort pipeline (prefix keys, radix, sym_sort cache) and transforms (replace_all,
# cull, rune_sort). Oracle: qsort over anostr_collate, plus naive rebuilds for transforms.
add_executable(anotest_strings_sort anotest_strings_sort.c)
target_link_libraries(anotest_strings_sort PRIVATE anoptic_core)
add_test(NAME anoptic_strings_sort COMMAND anotest_strings_sort)
set_tests_properties(anoptic_strings_sort PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit")

# Property fuzzer + smoketest for the whole anostr_t surface: cross-kind agreement, every sort,
# split/join, slice/splice, find/replace, builder, intern/dedupe/keep, hash<->SID twin, UTF
# round-trips. Runs in ctest (fixed seed, fast); argv[1] scales the soak. Oracles: qsort(collate),
# naive byte rebuilds, FNV twins.
add_executable(anotest_strings_fuzz anotest_strings_fuzz.c)
target_link_libraries(anotest_strings_fuzz PRIVATE anoptic_core)
add_test(NAME anoptic_strings_fuzz COMMAND anotest_strings_fuzz)
set_tests_properties(anoptic_strings_fuzz PROPERTIES TIMEOUT ${ANO_T120} LABELS "fuzz;mem")

# Collation sort benchmark: the 6000-item inventory. qsort+collate baseline vs anostr_sort
# / sort_idx / sym_sort (cold + warm) vs byte floor, plus replace_all/cull/rune_sort.
# DISABLED in ctest, run ./anotest_sortbench from a -O3 build (build.bat 7).
add_executable(anotest_sortbench anotest_sortbench.c)
target_link_libraries(anotest_sortbench PRIVATE anoptic_core)
add_test(NAME anoptic_sortbench COMMAND anotest_sortbench)
set_tests_properties(anoptic_sortbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# String-ops benchmark: find / replace_all / cull / rune_sort across hit/miss/dense/sparse/
# grow/shrink/no-match shapes, results checked. DISABLED in ctest, run from a -O3 build.
add_executable(anotest_stropsbench anotest_stropsbench.c)
target_link_libraries(anotest_stropsbench PRIVATE anoptic_core)
add_test(NAME anoptic_stropsbench COMMAND anotest_stropsbench)
set_tests_properties(anoptic_stropsbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger stress: anostr_t values through %.*s under multi-producer fire. Latency percentiles
# plus two oracles (no loss, byte-exact UTF-8). DISABLED in ctest, run from -O3.
add_executable(anotest_logstrbench anotest_logstrbench.c)
target_link_libraries(anotest_logstrbench PRIVATE anoptic_core)
add_test(NAME anoptic_logstrbench COMMAND anotest_logstrbench)
set_tests_properties(anoptic_logstrbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Testing for ``anoptic_text.h`` (FreeType lifecycle, the glyph-curve bake, and the
# multi-face Runic range bake). Fonts are staged next to the binary. The test chdirs
# to its exe dir and loads them by relative path.
add_executable(anotest_text anotest_text.c)
target_link_libraries(anotest_text PRIVATE anoptic_core m)
add_custom_command(TARGET anotest_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts ${CMAKE_CURRENT_BINARY_DIR}/resources/fonts
        COMMENT "Staging fonts next to anotest_text"
        VERBATIM)
add_test(NAME anoptic_text COMMAND anotest_text)
set_tests_properties(anoptic_text PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Testing for ``anoptic_ui.h`` (prim ABI, builder, reference evaluator). Oracles:
# 64x64-supersampled coverage + separable-Gaussian-blurred masks, both in double.
add_executable(anotest_ui anotest_ui.c)
target_link_libraries(anotest_ui PRIVATE anoptic_core m)
add_test(NAME anoptic_ui COMMAND anotest_ui)
set_tests_properties(anoptic_ui PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit")

# String compare microbenchmark: anostr_compare (German-string prefix path) vs naive memcmp.
# DISABLED in ctest, run ./anotest_strbench by hand from a -O3 build.
add_executable(anotest_strbench anotest_strbench.c)
target_link_libraries(anotest_strbench PRIVATE anoptic_core)
add_test(NAME anoptic_strbench COMMAND anotest_strbench)
set_tests_properties(anoptic_strbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Compile-time string-id benchmark: ANOSTR_SID switch dispatch vs strcmp / anostr_eq /
# runtime hash / intern_find, plus the bulk-keying cost SID deletes at startup.
# DISABLED in ctest, run ./anotest_sidbench by hand from a -O3 build.
add_executable(anotest_sidbench anotest_sidbench.c)
target_link_libraries(anotest_sidbench PRIVATE anoptic_core)
add_test(NAME anoptic_sidbench COMMAND anotest_sidbench)
set_tests_properties(anoptic_sidbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Testing for mesh optimizer
add_executable(anotest_meshoptimizer anotest_meshoptimizer.c)
target_link_libraries(anotest_meshoptimizer PRIVATE anoptic_core m)
add_test(NAME anoptic_meshoptimizer COMMAND anotest_meshoptimizer)
set_tests_properties(anoptic_meshoptimizer PROPERTIES TIMEOUT ${ANO_T10} LABELS "unit;mesh")

# Testing for ``anoptic_memory.h`` (mimalloc heaps, aligned/scoped alloc, huge pages)
add_executable(anotest_memory anotest_memory.c)
target_link_libraries(anotest_memory PRIVATE anoptic_core)
add_test(NAME anoptic_memory COMMAND anotest_memory)
set_tests_properties(anoptic_memory PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;mem")

# Testing for the ``anoptic_resources.h`` core: namespace/mounts/shadow order, hostile-path
# grammar fuzz, slurp read contract (byte-identical, guard NUL, alignment, multi-chunk),
# durable writes + quarantine, save-frame commits with an independent FNV/layout oracle.
# Writes real probe files under the user root (that is the write path's actual contract)
# and cleans them on exit. Owned loads use the explicit engine/save domain grammar.
add_executable(anotest_resources anotest_resources.c)
target_link_libraries(anotest_resources PRIVATE anoptic_core)
add_test(NAME anoptic_resources COMMAND anotest_resources)
set_tests_properties(anoptic_resources PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Testing for ``anoptic_res_graphics.h``: glTF ingest against a handcrafted-in-test
# scene (every vertex/node/material asserted), URI decode + directory grafting, base64
# data: buffers, single-copy scenes, and a PNG decode round-trip with stb_image_write
# as the independent encoder oracle. Real-asset smoke when the source assets/ exists.
add_executable(anotest_resgfx anotest_resgfx.c)
target_link_libraries(anotest_resgfx PRIVATE anoptic_core)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_resgfx PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_resgfx COMMAND anotest_resgfx)
set_tests_properties(anoptic_resgfx PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Explicit lifetime domains: engine survival, world retirement, reader-pinned reclamation,
# same-slot reload, transfer after quiescence, and zero-residue manager shutdown.
add_executable(anotest_resgroups anotest_resgroups.c)
target_include_directories(anotest_resgroups PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resgroups PRIVATE anoptic_core)
add_test(NAME anoptic_resgroups COMMAND anotest_resgroups)
set_tests_properties(anoptic_resgroups PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Stage A ownership foundation: lock-free immutable publication raced by registered readers,
# stale generations, pinned epoch retirement, non-moving row growth, accounting and wrap refusal.
add_executable(anotest_resownership anotest_resownership.c)
target_include_directories(anotest_resownership PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resownership PRIVATE anoptic_core)
add_test(NAME anoptic_resownership COMMAND anotest_resownership)
set_tests_properties(anoptic_resownership PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;concurrency;resource")

# Lock-free collections: layout asserts (cursor separation real, not aspirational),
# unit edges (full/empty/wrap/laps, u32 cursor wrap), then the conservation stress
# battery (8P x 8C MPMC, 8P x 1C MPSC drain, 1P x 8C SPMC strictly-increasing, SPSC
# mirror, ticket bitmap) -- the TSan gate's food, hence the concurrency label.
add_executable(anotest_collections anotest_collections.c)
target_link_libraries(anotest_collections PRIVATE anoptic_core)
add_test(NAME anoptic_collections COMMAND anotest_collections)
set_tests_properties(anoptic_collections PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;concurrency")

# Write-protocol crash harness: compiles resources_core.c PRIVATELY with
# ANO_RES_FAULT_INJECT (link-time override of the anoptic_core copy, the log_old.c
# trick) and longjmps out after every protocol step; the target file must be
# old-complete or new-complete at each crash instant, never torn.
add_executable(anotest_resfault anotest_resfault.c
        ${CMAKE_SOURCE_DIR}/src/resources/resources_core.c)
target_compile_definitions(anotest_resfault PRIVATE ANO_RES_FAULT_INJECT)
target_include_directories(anotest_resfault PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resfault PRIVATE anoptic_core)
add_test(NAME anoptic_resfault COMMAND anotest_resfault)
set_tests_properties(anoptic_resfault PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;resource")

# Persistence clients and save correctness: strict config/keybinding schemas, migration and
# quarantine, complete bounded-memory generation/temp fallback, world-save migration, and
# exact-slot concurrency. Oracle: accepted commits equal durable generations; distinct slots
# enter concurrently while one slot is totally ordered.
add_executable(anotest_persistence anotest_persistence.c)
target_include_directories(anotest_persistence PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_compile_definitions(anotest_persistence PRIVATE ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(anotest_persistence PRIVATE anoptic_core)
add_test(NAME anoptic_persistence COMMAND anotest_persistence)
set_tests_properties(anoptic_persistence PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;concurrency;resource")

# Real subprocess termination after every fixed-name durability boundary. The private
# resources_core.c copy exposes ANO_RES_FAULT_INJECT; parent oracle is old-complete/new-complete.
add_executable(anotest_resdurability anotest_resdurability.c
        ${CMAKE_SOURCE_DIR}/src/resources/resources_core.c)
target_compile_definitions(anotest_resdurability PRIVATE
        ANO_RES_FAULT_INJECT ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(anotest_resdurability PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resdurability PRIVATE anoptic_core)
add_test(NAME anoptic_resdurability COMMAND anotest_resdurability)
set_tests_properties(anoptic_resdurability PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;resource")

# ---------------------------------------------------------------------------------------------
# The resource-manager targets the FREEZE registers, so no later workstream ever has to touch
# a CMakeLists.txt. Each is owned by exactly one workstream and its name, LABELS and TIMEOUT
# are fixed here and now. anotest_rescodec has a real oracle already (LZ4 is vendored); the
# rest print PENDING and assert nothing -- a stub that printed OK would be a green lie.

# Codec: RAW/LZ4 round-trips, the reserved-codec refusal, hostile-bytes totality. (W4)
add_executable(anotest_rescodec anotest_rescodec.c)
target_include_directories(anotest_rescodec PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_rescodec PRIVATE anoptic_core)
add_test(NAME anoptic_rescodec COMMAND anotest_rescodec)
set_tests_properties(anoptic_rescodec PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;resource")

# Placement: the routing oracle -- every model x every plan -> the expected site. (W1)
add_executable(anotest_resplace anotest_resplace.c)
target_include_directories(anotest_resplace PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resplace PRIVATE anoptic_core)
add_test(NAME anoptic_resplace COMMAND anotest_resplace)
set_tests_properties(anoptic_resplace PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Ranges: partial reads that never lie. (W4)
add_executable(anotest_resrange anotest_resrange.c)
target_include_directories(anotest_resrange PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resrange PRIVATE anoptic_core)
add_test(NAME anoptic_resrange COMMAND anotest_resrange)
set_tests_properties(anoptic_resrange PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;resource")

# Packs: mount, bsearch lookup, ranged read, the deterministic builder, TOC corruption. (W4)
add_executable(anotest_respack anotest_respack.c)
target_include_directories(anotest_respack PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_respack PRIVATE anoptic_core)
add_test(NAME anoptic_respack COMMAND anotest_respack)
set_tests_properties(anoptic_respack PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;resource")

# Block framing: the hostile-input battery. The highest-severity item in the ledger. (W5)
add_executable(anotest_resbake anotest_resbake.c)
target_include_directories(anotest_resbake PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resbake PRIVATE anoptic_core)
add_test(NAME anoptic_resbake COMMAND anotest_resbake)
set_tests_properties(anoptic_resbake PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;fuzz;resource")

# Hot reload: old-complete or new-complete, plus the derived cascade. (W5)
add_executable(anotest_reshotreload anotest_reshotreload.c)
target_include_directories(anotest_reshotreload PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_reshotreload PRIVATE anoptic_core)
add_test(NAME anoptic_reshotreload COMMAND anotest_reshotreload)
set_tests_properties(anoptic_reshotreload PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;concurrency;resource")

# Audio: planar PCM ingest, and the null sink's FNV hash as the mixer's oracle. (W8)
add_executable(anotest_resaudio anotest_resaudio.c)
target_include_directories(anotest_resaudio PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resaudio PRIVATE anoptic_core m)
add_test(NAME anoptic_resaudio COMMAND anotest_resaudio)
set_tests_properties(anoptic_resaudio PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Script: a bytecode block, and a VM that always terminates. (W9)
add_executable(anotest_resscript anotest_resscript.c)
target_include_directories(anotest_resscript PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resscript PRIVATE anoptic_core m)
add_test(NAME anoptic_resscript COMMAND anotest_resscript)
set_tests_properties(anoptic_resscript PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Levels: a domain that opens, runs, and returns to baseline over 20 cycles. (W10)
add_executable(anotest_reslevel anotest_reslevel.c)
target_include_directories(anotest_reslevel PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_reslevel PRIVATE anoptic_core m)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_reslevel PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_reslevel COMMAND anotest_reslevel)
set_tests_properties(anoptic_reslevel PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;resource")

# The remote-FS floor: 9P and SMB tell the truth about nothing. (W12)
add_executable(anotest_resremote anotest_resremote.c)
target_include_directories(anotest_resremote PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resremote PRIVATE anoptic_core)
add_test(NAME anoptic_resremote COMMAND anotest_resremote)
set_tests_properties(anoptic_resremote PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;resource")

# ---------------------------------------------------------------------------------------------

# Testing for ``anoptic_memory_pools.h`` (monotonic/multipool/pool over parents): hostile
# input totality, shadow-oracle churn fuzz, alignment contracts, reset/recycle behavior,
# false-on-empty at the pool cap, Multipool<Monotonic> composition, wink-out.
add_executable(anotest_mempools anotest_mempools.c)
target_link_libraries(anotest_mempools PRIVATE anoptic_core)
add_test(NAME anoptic_mempools COMMAND anotest_mempools)
set_tests_properties(anoptic_mempools PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem")

# Memory-pools benchmark: the Lakos grid (churn / batch-and-wink / composition) vs the raw
# mi_heap baseline, percentiles + wall ops/sec. DISABLED in ctest, run ./anotest_mempoolbench
# by hand from a -O3 build (build.sh 8).
add_executable(anotest_mempoolbench anotest_mempoolbench.c)
target_link_libraries(anotest_mempoolbench PRIVATE anoptic_core)
add_test(NAME anoptic_mempoolbench COMMAND anotest_mempoolbench)
set_tests_properties(anoptic_mempoolbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Thread bridges: waiter park/wake soak (adversarial publish-vs-park window; joining
# at all is the liveness oracle), policy semantics (TRYFAIL truthful, WAIT degrades
# against a dead consumer), and the anores_t handle channel's exactly-once oracle.
add_executable(anotest_bridges anotest_bridges.c)
target_link_libraries(anotest_bridges PRIVATE anoptic_core)
add_test(NAME anoptic_bridges COMMAND anotest_bridges)
set_tests_properties(anoptic_bridges PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;concurrency")

# Ring benchmark: every lock-free flavor vs a mutex+array-queue baseline at the same
# layouts, plus the FAA ticket vs a mutex counter. DISABLED in ctest; run by hand from
# a -O3 build (build.sh 8 / build.bat 8).
add_executable(anotest_ringbench anotest_ringbench.c)
target_link_libraries(anotest_ringbench PRIVATE anoptic_core)
add_test(NAME anoptic_ringbench COMMAND anotest_ringbench)
set_tests_properties(anoptic_ringbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Historical churn / real-asset domain-cycle / direct hand-off workloads retained behind
# the explicit lifetime grammar. This is not the allocator hierarchy contest. DISABLED in
# ctest; run ./anotest_resbench from a -O3 build.
add_executable(anotest_resbench anotest_resbench.c)
target_include_directories(anotest_resbench PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resbench PRIVATE anoptic_core)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_resbench PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_resbench COMMAND anotest_resbench)
set_tests_properties(anoptic_resbench PROPERTIES DISABLED TRUE LABELS "optional;bench;resource")

# Easter egg: the original allocator experiment, preserved verbatim (see the file banner).
# Built so it cannot rot, but DISABLED in ctest
# -- run ./anotest_chariots by hand. -w
add_executable(anotest_chariots anotest_chariots.c)
target_link_libraries(anotest_chariots PRIVATE anoptic_core)
target_compile_options(anotest_chariots PRIVATE -w)
add_test(NAME anoptic_chariots COMMAND anotest_chariots)
set_tests_properties(anoptic_chariots PROPERTIES DISABLED TRUE LABELS "optional;easteregg")

# Optional logger benchmark: the lock-free ring (in anoptic_core) vs the preserved mutex baseline.
# log_old.c is compiled ONLY into this target -- it never enters anoptic_core or the standard
# build, so the two loggers coexist in one executable (the baseline is namespaced mtxlog_*). The
# src include dir resolves "log/log_old.h". Built so it cannot rot, DISABLED in ctest --
# run ./anotest_logbench by hand.
add_executable(anotest_logbench anotest_logbench.c ${CMAKE_SOURCE_DIR}/src/log/log_old.c)
target_include_directories(anotest_logbench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anotest_logbench PRIVATE anoptic_core)
add_test(NAME anoptic_logbench COMMAND anotest_logbench)
set_tests_properties(anoptic_logbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger tail benchmark: per-call enqueue latency percentiles (p50/p90/p99/p99.9) from a calibrated
# rdtsc tick source -- the percentile complement to anotest_logbench's means. Built so it cannot
# rot, DISABLED in ctest -- run ./anotest_logtail by hand.
add_executable(anotest_logtail anotest_logtail.c)
target_link_libraries(anotest_logtail PRIVATE anoptic_core)
add_test(NAME anoptic_logtail COMMAND anotest_logtail)
set_tests_properties(anoptic_logtail PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger fuzzer: many producers drive ano_log_* with randomized well-typed input (variable-length
# content as "%s", a fixed (literal-fmt, typed-arg) table for the deferred formatter, immediates, and
# output-dir swaps) while a flusher drains on short random intervals. Oracle: the logger drops nothing,
# so the summed output line count must equal the total enqueued. Deterministic and fast by default
# (fixed seed, ~few seconds); pass an iteration count as argv[1] for a longer soak.
add_executable(anotest_logfuzz anotest_logfuzz.c)
target_link_libraries(anotest_logfuzz PRIVATE anoptic_core)
add_test(NAME anoptic_logfuzz COMMAND anotest_logfuzz)
set_tests_properties(anoptic_logfuzz PROPERTIES TIMEOUT ${ANO_T120} LABELS "fuzz;concurrency")

# Testing for ``anoptic_log_crash.h`` (crash hooks, crash records, the logger hail mary, deadman).
# The binary re-execs itself once per scenario and judges the child's death, its <stamp>_CRASH.log
# record, and sentinel survival in a scratch session log. The deadman scenario waits through the
# 5 s watchdog window.
add_executable(anotest_blackbox anotest_blackbox.c)
target_link_libraries(anotest_blackbox PRIVATE anoptic_core)
add_test(NAME anoptic_blackbox COMMAND anotest_blackbox)
set_tests_properties(anoptic_blackbox PROPERTIES TIMEOUT ${ANO_T240} LABELS "unit;concurrency")
# Skipped under ASan/TSan: sanitizers install their own fatal-signal handlers.
if(NOT ANOPTIC_SANITIZE STREQUAL "")
    set_tests_properties(anoptic_blackbox PROPERTIES DISABLED TRUE)
endif()

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;concurrency")

# Testing for Vulkan Lifecycle
if(TARGET anoptic_render)
    # The vk tests resolve shaders (loadFile) and assets exe-relative, and their
    # executables live HERE, not next to anopticengine -- so stage both into this
    # directory too. Plain custom target: re-copies on every build, but that is six
    # small .spv plus the dev assets, and it can never go stale.
    add_custom_target(anoptic_test_resources ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_BIN_DIR} ${CMAKE_CURRENT_BINARY_DIR}/resources/shaders
            COMMENT "Staging shaders (and assets, if present) into tests/"
            VERBATIM)
    add_dependencies(anoptic_test_resources anoptic_shaders)
    if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
        add_custom_command(TARGET anoptic_test_resources POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/resources/models
                VERBATIM)
    endif()

    # Render-side slot authority: pure logic, links anoptic_render but needs no GPU device.
    add_executable(anotest_render_slots anotest_render_slots.c)
    target_link_libraries(anotest_render_slots PRIVATE anoptic_render)
    add_test(NAME anoptic_render_slots COMMAND anotest_render_slots)
    set_tests_properties(anoptic_render_slots PROPERTIES TIMEOUT ${ANO_T10} LABELS "unit;render")

    add_executable(anotest_vk_lifecycle anotest_vk_lifecycle.c)
    target_link_libraries(anotest_vk_lifecycle PRIVATE anoptic_render)
    add_test(NAME anotest_vk_lifecycle COMMAND anotest_vk_lifecycle)
    set_tests_properties(anotest_vk_lifecycle PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan")

    add_executable(anotest_vk_components anotest_vk_components.c)
    target_link_libraries(anotest_vk_components PRIVATE anoptic_render)
    add_test(NAME anotest_vk_components COMMAND anotest_vk_components)
    set_tests_properties(anotest_vk_components PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan")

    add_executable(anotest_vk_compliance_layers anotest_vk_compliance_layers.c)
    target_link_libraries(anotest_vk_compliance_layers PRIVATE anoptic_render)
    add_test(NAME anotest_vk_compliance_layers COMMAND anotest_vk_compliance_layers)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_compliance_layers PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>")

    add_executable(anotest_vk_memory anotest_vk_memory.c)
    target_link_libraries(anotest_vk_memory PRIVATE anoptic_render)
    add_test(NAME anotest_vk_memory COMMAND anotest_vk_memory)
    set_tests_properties(anotest_vk_memory PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan")

    add_executable(anotest_vk_sync anotest_vk_sync.c)
    target_link_libraries(anotest_vk_sync PRIVATE anoptic_render)
    add_test(NAME anotest_vk_sync COMMAND anotest_vk_sync)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>")

    # A lone `--build --target anotest_vk_*` must still stage this directory's
    # shaders/assets before the test can run.
    foreach(vktest IN ITEMS anotest_render_slots anotest_vk_lifecycle anotest_vk_components
            anotest_vk_compliance_layers anotest_vk_memory anotest_vk_sync)
        add_dependencies(${vktest} anoptic_test_resources)
    endforeach()
else()
    message(WARNING
        "\n"
        "################################################################\n"
        "##  !!!  VULKAN TESTS SKIPPED  !!!                            ##\n"
        "##  No anoptic_render target (headless or no Vulkan SDK).     ##\n"
        "##  These tests are NOT compiled here and can rot unnoticed.  ##\n"
        "##  Run a non-headless build with the Vulkan SDK to cover.    ##\n"
        "################################################################\n")
endif()
