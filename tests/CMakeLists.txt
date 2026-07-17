# Test executables link anoptic_core. Exit 0 = pass.

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_log.h``
# Scratch dirs are anchored at runtime to the test exe's own directory (ano_fs_chdir_gamepath in
# main / scratch_anchor_to_exe), so output never lands in the caller's CWD and a cross-built exe
# writes to a valid path on its own OS -- no compile-time build path is baked in.
add_executable(anotest_logging anotest_logging.c)
target_link_libraries(anotest_logging PRIVATE anoptic_core)
add_test(NAME anoptic_logging COMMAND anotest_logging)
set_tests_properties(anoptic_logging PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

# Testing for ``anoptic_filesystem.h`` (ano_fspath values, game/user paths, append-file API).
# Note: the userpath check creates the real per-user game directory and one probe file in it
# (removed on exit) -- that is the function's actual contract, so it is tested for real.
add_executable(anotest_filesystem anotest_filesystem.c)
target_link_libraries(anotest_filesystem PRIVATE anoptic_core)
add_test(NAME anoptic_filesystem COMMAND anotest_filesystem)
set_tests_properties(anoptic_filesystem PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_strings.h`` (the 16-byte value type, builder, keep, slicing)
add_executable(anotest_strings anotest_strings.c)
target_link_libraries(anotest_strings PRIVATE anoptic_core)
add_test(NAME anoptic_strings COMMAND anotest_strings)
set_tests_properties(anoptic_strings PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Testing for ``anoptic_strings_utf.h`` (UTF-8 iteration/encode, Unicode case + classification)
add_executable(anotest_strings_utf anotest_strings_utf.c)
target_link_libraries(anotest_strings_utf PRIVATE anoptic_core)
add_test(NAME anoptic_strings_utf COMMAND anotest_strings_utf)
set_tests_properties(anoptic_strings_utf PROPERTIES TIMEOUT 30 LABELS "unit")

# Collation sort pipeline (prefix keys, radix, sym_sort cache) and transforms (replace_all,
# cull, rune_sort). Oracle: qsort over anostr_collate, plus naive rebuilds for transforms.
add_executable(anotest_strings_sort anotest_strings_sort.c)
target_link_libraries(anotest_strings_sort PRIVATE anoptic_core)
add_test(NAME anoptic_strings_sort COMMAND anotest_strings_sort)
set_tests_properties(anoptic_strings_sort PROPERTIES TIMEOUT 60 LABELS "unit")

# Property fuzzer + smoketest for the whole anostr_t surface: cross-kind agreement, every sort,
# split/join, slice/splice, find/replace, builder, intern/dedupe/keep, hash<->SID twin, UTF
# round-trips. Runs in ctest (fixed seed, fast); argv[1] scales the soak. Oracles: qsort(collate),
# naive byte rebuilds, FNV twins.
add_executable(anotest_strings_fuzz anotest_strings_fuzz.c)
target_link_libraries(anotest_strings_fuzz PRIVATE anoptic_core)
add_test(NAME anoptic_strings_fuzz COMMAND anotest_strings_fuzz)
set_tests_properties(anoptic_strings_fuzz PROPERTIES TIMEOUT 120 LABELS "fuzz;mem")

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
set_tests_properties(anoptic_text PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_ui.h`` (prim ABI, builder, reference evaluator). Oracles:
# 64x64-supersampled coverage + separable-Gaussian-blurred masks, both in double.
add_executable(anotest_ui anotest_ui.c)
target_link_libraries(anotest_ui PRIVATE anoptic_core m)
add_test(NAME anoptic_ui COMMAND anotest_ui)
set_tests_properties(anoptic_ui PROPERTIES TIMEOUT 60 LABELS "unit")

# Path-baker contour guard: a MOVE-only path (> 512 contours) is legal input and must come
# back ANO_UI_REF_NONE, not spray qn past the 513-entry cstart stack array (docs/BUGS.md,
# UI / Implementation). Fails (access violation or wedge in the baker) until the cn bound lands.
add_executable(anotest_uipathguard anotest_uipathguard.c)
target_link_libraries(anotest_uipathguard PRIVATE anoptic_core m)
add_test(NAME anoptic_uipathguard COMMAND anotest_uipathguard)
set_tests_properties(anoptic_uipathguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Tile-build cap guard: tilesX*tilesY and nTiles+1 wrap uint32, so a 2^32-tile request slips
# the offsetsCap guard and reports ok for buffers that hold none of it (docs/BUGS.md, UI /
# Implementation). Fails until the sizing math stops wrapping.
add_executable(anotest_uitileguard anotest_uitileguard.c)
target_link_libraries(anotest_uitileguard PRIVATE anoptic_core m)
add_test(NAME anoptic_uitileguard COMMAND anotest_uitileguard)
set_tests_properties(anoptic_uitileguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Paint-push stop guard: b->stopCount + stopCount wraps uint32, so a near-UINT32_MAX stop
# count slips the cap guard and the copy loop runs off the stop array (docs/BUGS.md, UI /
# Implementation). Fails (access violation in the copy) until the guard computes without wrap.
add_executable(anotest_uipaintguard anotest_uipaintguard.c)
target_link_libraries(anotest_uipaintguard PRIVATE anoptic_core m)
add_test(NAME anoptic_uipaintguard COMMAND anotest_uipaintguard)
set_tests_properties(anoptic_uipaintguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Ref-paint stop-window guard: ano_ui_ref_paint's reject arm computes stopFirst + stopCount
# in uint32 (ui_raster_ref.c:229), so stopFirst UINT32_MAX wraps the sum under stopCount and
# ui_stop_color reads the stop table ~137 GB out of bounds against the header's fail-CLOSED
# contract (anoptic_ui.h:275) (docs/BUGS.md, UI / Implementation). Fails (access violation
# in ui_stop_color) until the guard computes without wrap.
add_executable(anotest_uirefstopguard anotest_uirefstopguard.c)
target_link_libraries(anotest_uirefstopguard PRIVATE anoptic_core m)
add_test(NAME anoptic_uirefstopguard COMMAND anotest_uirefstopguard)
set_tests_properties(anoptic_uirefstopguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

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
set_tests_properties(anoptic_meshoptimizer PROPERTIES TIMEOUT 10 LABELS "unit;mesh")

# Meshlet build/bound pair guard: max_vertices < 3 / max_triangles < 1 must be rejected by
# ano_build_meshlets exactly as its sizing twin ano_build_meshlets_bound already does, not
# packed into meshlets that overrun bound()-sized buffers and break the max_vertices promise
# (docs/BUGS.md, Mesh / Implementation). Fails until the guard lands.
add_executable(anotest_meshguard anotest_meshguard.c)
target_link_libraries(anotest_meshguard PRIVATE anoptic_core m)
add_test(NAME anoptic_meshguard COMMAND anotest_meshguard)
set_tests_properties(anoptic_meshguard PROPERTIES TIMEOUT 30 LABELS "unit;mesh")

# Simplify collapse-validity guard: ano_simplify delegates to ano_simplify_ex with
# edge_len_factor 0, and the link/tetra collapse exclusion only runs when the growth guards
# are on (ano_meshoptimizer.c:955), so a rim collapse on a 3-tri cone fan emits the same face
# twice 〜 a coincident opposite-wound duplicate the guards-on path rejects (docs/BUGS.md,
# Mesh / Implementation). Fails until the base path enforces collapse validity.
add_executable(anotest_meshsimplifyguard anotest_meshsimplifyguard.c)
target_link_libraries(anotest_meshsimplifyguard PRIVATE anoptic_core m)
add_test(NAME anoptic_meshsimplifyguard COMMAND anotest_meshsimplifyguard)
set_tests_properties(anoptic_meshsimplifyguard PROPERTIES TIMEOUT 30 LABELS "unit;mesh")

# Testing for ``anoptic_memory.h`` (mimalloc heaps, aligned/scoped alloc, huge pages)
add_executable(anotest_memory anotest_memory.c)
target_link_libraries(anotest_memory PRIVATE anoptic_core)
add_test(NAME anoptic_memory COMMAND anotest_memory)
set_tests_properties(anoptic_memory PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Aligned-malloc zero contract: anoptic_memory.h promises NULL when size or alignment is 0,
# but every platform memalign forwards straight to mi_malloc_aligned, which hands back a live
# pointer for size 0 (docs/BUGS.md, Memory / Implementation). Fails until the guard lands.
add_executable(anotest_memguard anotest_memguard.c)
target_link_libraries(anotest_memguard PRIVATE anoptic_core)
add_test(NAME anoptic_memguard COMMAND anotest_memguard)
set_tests_properties(anoptic_memguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

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
set_tests_properties(anoptic_logfuzz PROPERTIES TIMEOUT 120 LABELS "fuzz;concurrency")

# Testing for ``anoptic_log_crash.h`` (crash hooks, crash records, the logger hail mary, deadman).
# The binary re-execs itself once per scenario and judges the child's death, its <stamp>_CRASH.log
# record, and sentinel survival in a scratch session log. The deadman scenario waits through the
# 5 s watchdog window.
add_executable(anotest_blackbox anotest_blackbox.c)
target_link_libraries(anotest_blackbox PRIVATE anoptic_core)
add_test(NAME anoptic_blackbox COMMAND anotest_blackbox)
set_tests_properties(anoptic_blackbox PROPERTIES TIMEOUT 240 LABELS "unit;concurrency")
# Skipped under ASan/TSan: sanitizers install their own fatal-signal handlers.
if(NOT ANOPTIC_SANITIZE STREQUAL "")
    set_tests_properties(anoptic_blackbox PROPERTIES DISABLED TRUE)
endif()

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

# UI-block stop-window guard: ano_render_ui_set bounds every block-local reference except
# a paint's [stopFirst, +stopCount) window (ui_prim_valid, ano_render_bridge.c:204); the
# compose rebases it blind (ui_raster.c:123) and the GPU evaluator's promised fail-closed
# mirror check is absent (uicoverage.glsl:176 vs ui_raster_ref.c:229), so a hand-built
# block walks the stop SSBO out of bounds (docs/BUGS.md, Render / Vulkan backend /
# Interlink-Composition). Device-free 〜 bridge transport + CPU ref evaluator only. Fails
# until the bridge drops the bad window like its sibling references.
add_executable(anotest_uistopguard anotest_uistopguard.c)
target_link_libraries(anotest_uistopguard PRIVATE anoptic_core m)
add_test(NAME anoptic_uistopguard COMMAND anotest_uistopguard)
set_tests_properties(anoptic_uistopguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for the audio stack Phase 0 (churned-heap offline determinism; WAV; live null-backend round-trip)
add_executable(anotest_audio anotest_audio.c)
target_link_libraries(anotest_audio PRIVATE anoptic_core)
add_test(NAME anoptic_audio COMMAND anotest_audio)
set_tests_properties(anoptic_audio PROPERTIES TIMEOUT 60 LABELS "unit;mem")

# Buffer-register argument guard: a frames count whose byte size wraps uint64 must be
# rejected, not adopted (docs/BUGS.md, Audio / Implementation). Fails until the guard lands.
add_executable(anotest_audioguard anotest_audioguard.c)
target_link_libraries(anotest_audioguard PRIVATE anoptic_core)
add_test(NAME anoptic_audioguard COMMAND anotest_audioguard)
set_tests_properties(anoptic_audioguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# WAV-write argument guard: a frames count whose byte size wraps uint64 must be rejected
# with false, not written as a truncated file that claims success (docs/BUGS.md, Audio /
# Implementation). Fails until the guard lands.
add_executable(anotest_wavguard anotest_wavguard.c)
target_link_libraries(anotest_wavguard PRIVATE anoptic_core)
add_test(NAME anoptic_wavguard COMMAND anotest_wavguard)
set_tests_properties(anoptic_wavguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Limiter lookahead-window guard: fx_limiter sizes its winmax window == lookahead while the
# emitted sample has age exactly lookahead (audio_fx.c:100/:391), so a peak expires from the
# analysis window on the push that emits it and its output takes one release step of gain-up 〜
# a 10x impulse at the 1 ms release floor emits ~1.107 over the 0.92 ceiling (docs/BUGS.md,
# Audio / Implementation). Fails until the window covers the emitted sample.
add_executable(anotest_limiterguard anotest_limiterguard.c)
target_link_libraries(anotest_limiterguard PRIVATE anoptic_core)
add_test(NAME anoptic_limiterguard COMMAND anotest_limiterguard)
set_tests_properties(anoptic_limiterguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Shutdown adopted-block discharge guard: ano_audio_shutdown joins the mixer, stops the device,
# and destroys both bridge rings and the module heap without walking mx->buffers or draining
# either ring, while adopted blocks live on the DEFAULT mi heap (plain mi_malloc,
# ano_audio.c:260) 〜 so LIVE owned blocks, queued ACMD_BUFFER_REGISTER blocks, and un-polled
# AEVT_BUFFER_RETIRED blocks all orphan permanently against the lossless-block invariant
# (audio_internal.h:9; ano_audio.c:204, docs/BUGS.md, Audio / Implementation). Compiles the real
# ano_audio.c + audio_mixer.c TUs with the allocator tokens interposed to audit adopted-block
# balance; null backend, no sound hardware. Fails until shutdown discharges every adopted block.
add_executable(anotest_audioshutguard anotest_audioshutguard.c
        ${CMAKE_SOURCE_DIR}/src/audio/ano_audio.c
        ${CMAKE_SOURCE_DIR}/src/audio/audio_mixer.c)
target_compile_definitions(anotest_audioshutguard PRIVATE mi_malloc=anotest_seam_malloc mi_free=anotest_seam_free)
target_link_libraries(anotest_audioshutguard PRIVATE anoptic_core)
add_test(NAME anoptic_audioshutguard COMMAND anotest_audioshutguard)
set_tests_properties(anoptic_audioshutguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Drain-batch sizing guard: deferred records render at their format width, not their stored
# size, so a wide "%*d" burst must survive a drain pass with nothing lost and full-width
# fields (docs/BUGS.md, Log / Implementation). Fails (crashes in the drain pass) until the
# batch bound lands.
add_executable(anotest_logflood anotest_logflood.c)
target_link_libraries(anotest_logflood PRIVATE anoptic_core)
add_test(NAME anoptic_logflood COMMAND anotest_logflood)
set_tests_properties(anoptic_logflood PROPERTIES TIMEOUT 120 LABELS "unit;concurrency;mem")

# sourceFile call-time capture guard: the deferred capture stores the caller's sourceFile as
# a raw pointer in the ring blob (log_core.c:205) and format_deferred derefs it at drain
# (:268/:277), while the header's lifetime contract is one-sided 〜 anoptic_log.h:53 constrains
# printFormat only, and the %s arm deep-copies every other caller-owned string at capture
# (docs/BUGS.md, Log / Interface-level, log_core.c:205). Real public logger end to end:
# controls prove literal and intact-buffer round-trips, then trigger records pass live buffers
# scribbled right after the call returns, behind a FIFO backlog that pins the drainer. Fails
# (drained lines carry the scribble, not the call-time names) until capture copies by value.
add_executable(anotest_logsrcguard anotest_logsrcguard.c)
target_link_libraries(anotest_logsrcguard PRIVATE anoptic_core)
add_test(NAME anoptic_logsrcguard COMMAND anotest_logsrcguard)
set_tests_properties(anoptic_logsrcguard PROPERTIES TIMEOUT 30 LABELS "unit")

# GPOS subtable-offset wrap guard: ano_gpos_extract_kerns resolves subtable offsets with
# unchecked uint32 math (text_gpos.c:304 so += innerOff, sibling :186 class matrix), so a type-9
# Extension whose 32-bit extensionOffset overflows so back INTO the buffer parses a different
# in-bounds structure and the extractor returns 0 (success) against its header contract's
# "Malformed -> nonzero" (text_internal.h:89, docs/BUGS.md, Text / Interface-level). Links
# anoptic_core and drives the extractor on hand-built GPOS byte tables 〜 no FreeType, no device.
# Fails (malformed wrapping table returns 0) until the offset math is range-checked.
add_executable(anotest_gposwrapguard anotest_gposwrapguard.c)
target_link_libraries(anotest_gposwrapguard PRIVATE anoptic_core)
add_test(NAME anoptic_gposwrapguard COMMAND anotest_gposwrapguard)
set_tests_properties(anoptic_gposwrapguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Score-event field-range guard: velocity and pitch above their documented AnoNoteEvent
# ranges (1..127 / 0..127) must be rejected like velocity 0 already is, not staged to render
# at ~2x the amplitude ceiling or alias pitch & 0x7F tie chains (docs/BUGS.md, Synth /
# Implementation). Fails until the upper bounds land.
add_executable(anotest_synthguard anotest_synthguard.c)
target_link_libraries(anotest_synthguard PRIVATE anoptic_core)
add_test(NAME anoptic_synthguard COMMAND anotest_synthguard)
set_tests_properties(anoptic_synthguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Score-tempo schedule-state guard: score_tempo called before score_begin must report the
# documented-order misuse with false like every sibling entry point does, not hand a NULL
# anchor array to clock_add and die on the deref (docs/BUGS.md, Synth / Interface-level,
# ano_synth.c:227). Fails (SEGFAULT at the trigger) until the state guard lands.
add_executable(anotest_synthtempoguard anotest_synthtempoguard.c)
target_link_libraries(anotest_synthtempoguard PRIVATE anoptic_core)
add_test(NAME anoptic_synthtempoguard COMMAND anotest_synthtempoguard)
set_tests_properties(anoptic_synthtempoguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Score-begin anchor-cap wrap guard: anchorCap = tempoCount + 1u wraps to 0 at UINT32_MAX,
# the zero-count calloc passes the NULL check, and score_begin OOB-writes the 24-byte seed
# anchor into a zero-size block while returning true (docs/BUGS.md, Synth / Interface-level,
# ano_synth.c:202). Fails (first promised tempo point rejected, or allocator abort) until
# the sizing rejects or survives the wrap.
add_executable(anotest_synthbeginguard anotest_synthbeginguard.c)
target_link_libraries(anotest_synthbeginguard PRIVATE anoptic_core)
add_test(NAME anoptic_synthbeginguard COMMAND anotest_synthbeginguard)
set_tests_properties(anoptic_synthbeginguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Live audio smoke on the AUTO backend cascade. Passes on any backend (CI
# cascades to null); by hand on a desktop it is the audible Phase 1 check.
add_executable(anotest_audiotone anotest_audiotone.c)
target_link_libraries(anotest_audiotone PRIVATE anoptic_core)
add_test(NAME anoptic_audiotone COMMAND anotest_audiotone)
set_tests_properties(anoptic_audiotone PROPERTIES TIMEOUT 30 LABELS "unit;optional")

# Live Phase 2 scene: positioned one-shots + looping ambient bed + orbiting
# listener + swept bus filter. CI-safe on null; audible by hand (2s in ctest).
add_executable(anotest_audioscene anotest_audioscene.c)
target_link_libraries(anotest_audioscene PRIVATE anoptic_core)
add_test(NAME anoptic_audioscene COMMAND anotest_audioscene 2)
set_tests_properties(anoptic_audioscene PROPERTIES TIMEOUT 60 LABELS "unit;optional")

# Phase 3 DSP library + console: primitive unit oracles, effect property
# tests, and the churned-heap console golden. All offline, CI-safe.
add_executable(anotest_audiodsp anotest_audiodsp.c)
target_link_libraries(anotest_audiodsp PRIVATE anoptic_core)
add_test(NAME anoptic_audiodsp COMMAND anotest_audiodsp)
set_tests_properties(anoptic_audiodsp PROPERTIES TIMEOUT 60 LABELS "unit;mem")

# Phase 4 synth offline gate: BeatClock oracle, tie chains render as single
# voices (bit-compare vs the merged note), patch showcase, and the journey
# fixture — churned-heap byte-identity plus the full listenable WAV artifact.
add_executable(anotest_synth anotest_synth.c)
target_link_libraries(anotest_synth PRIVATE anoptic_core)
target_compile_definitions(anotest_synth PRIVATE
        ANO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME anoptic_synth COMMAND anotest_synth)
set_tests_properties(anoptic_synth PROPERTIES TIMEOUT 300 LABELS "unit;mem")

# Live Phase 4 scene: the journey demo in realtime through synth generator +
# music console + device backend, automation paced from the logic thread.
# CI-safe on null (8 s); by hand it is the audible three-minute piece.
add_executable(anotest_musichost anotest_musichost.c)
target_link_libraries(anotest_musichost PRIVATE anoptic_core)
target_include_directories(anotest_musichost PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_musichost COMMAND anotest_musichost)

add_executable(anotest_synthlive anotest_synthlive.c)
target_link_libraries(anotest_synthlive PRIVATE anoptic_core)
target_include_directories(anotest_synthlive PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_synthlive COMMAND anotest_synthlive)

# Phase 7: the composer hosted in the audio callback (ano_synth_attach_music).
# The gate is that it changes nothing -- sample-identical to the batch path over
# the bars they share -- plus the two properties only the live path has: the
# piece never ends, and a bar's meaning surfaces on its own downbeat.
add_executable(anotest_musicdrive anotest_musicdrive.c)
target_link_libraries(anotest_musicdrive PRIVATE anoptic_core)
target_include_directories(anotest_musicdrive PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_musicdrive COMMAND anotest_musicdrive)
set_tests_properties(anoptic_musicdrive PROPERTIES TIMEOUT 120 LABELS "unit")

# Phase 7's exit criterion, audible by hand: an ENDLESS piece composed inside the
# audio callback, steered live from gameplay, with save/seek by deterministic
# reconstruction. CI-safe on null (30 s); run ./anotest_musicscene 180 on a real
# device for the background-listen test.
add_executable(anotest_musicscene anotest_musicscene.c)
target_link_libraries(anotest_musicscene PRIVATE anoptic_core)
add_test(NAME anoptic_musicscene COMMAND anotest_musicscene 30)
set_tests_properties(anoptic_musicscene PROPERTIES TIMEOUT 120 LABELS "unit;optional")

# Bridge hot-op tail bench: logic-side publish/acquire latency + per-block mixer
# cost under a live mixer. DISABLED; run by hand (argv[1] seconds). A/B vehicle
# for cross-thread layout work.
add_executable(anotest_bridgebench anotest_bridgebench.c)
target_link_libraries(anotest_bridgebench PRIVATE anoptic_core)
add_test(NAME anoptic_bridgebench COMMAND anotest_bridgebench)
set_tests_properties(anoptic_bridgebench PROPERTIES DISABLED TRUE LABELS "optional;bench")

add_executable(anotest_synthscene anotest_synthscene.c)
target_link_libraries(anotest_synthscene PRIVATE anoptic_core)
target_compile_definitions(anotest_synthscene PRIVATE
        ANO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME anoptic_synthscene COMMAND anotest_synthscene 8)
set_tests_properties(anoptic_synthscene PROPERTIES TIMEOUT 60 LABELS "unit;optional")

# Phase 6 step-1 gate: determinism kernel bit-compatibility with CPython 3.12
# (BLAKE2b-8 tags, MT19937 seeding + draw semantics, banker's rounding).
add_executable(anotest_music anotest_music.c)
target_link_libraries(anotest_music PRIVATE anoptic_core)
add_test(NAME anoptic_music COMMAND anotest_music)
set_tests_properties(anoptic_music PROPERTIES TIMEOUT 30 LABELS "unit")

# Arp emission slot guard: ano_meter_slots exceeds ANO_METER_MAX_SLOTS for meters past 8
# quarters (9/4 -> 36) and ano_generate_arp streams one event per slot into the 32-entry
# AnoArpResult array with no bound (docs/BUGS.md, Music / Implementation). Fails (canary
# stomped past the result struct, bar truncated) until the emission respects the cap.
add_executable(anotest_musicarpguard anotest_musicarpguard.c)
target_link_libraries(anotest_musicarpguard PRIVATE anoptic_core)
target_include_directories(anotest_musicarpguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_musicarpguard COMMAND anotest_musicarpguard)
set_tests_properties(anoptic_musicarpguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Cadence-policy value domain guard: ano_music_set_override("cadence_policy", v) validates the
# name but not the value (music_host.c:193), so any v outside {-1..2} rides policy_of verbatim
# into three [3]-sized tables guarded only against NONE (music_melody.c:517 DEGS,
# music_verify.c:752/:753/:762) and back out through AnoMusicMeaning.cadencePolicy
# (docs/BUGS.md, Music / Interface-level). Fails (out-of-enum policy republished on every
# cadence and pre-cadence bar) until the boundary rejects or clamps the value.
add_executable(anotest_musiccadenceguard anotest_musiccadenceguard.c)
target_link_libraries(anotest_musiccadenceguard PRIVATE anoptic_core)
add_test(NAME anoptic_musiccadenceguard COMMAND anotest_musiccadenceguard)
set_tests_properties(anoptic_musiccadenceguard PROPERTIES TIMEOUT 30 LABELS "unit")

# Perc compound-meter slot guard: the grouped-kick pickup writes kick[slots - 2] past the
# 32-entry stack set for meters past 8 quarters (9/4 -> 36, music_perc.c:121), and the u32
# hatDrops mask shifts wrap for slot >= 32 so in-range mask bits eat the out-of-range hats
# (music_perc.c:79/:154) (docs/BUGS.md, Music / Implementation). Fails (mask aliasing observed
# differentially; the OOB kick write runs in the same call) until the 32-wide shapes handle
# wide bars.
add_executable(anotest_percmeterguard anotest_percmeterguard.c)
target_link_libraries(anotest_percmeterguard PRIVATE anoptic_core)
target_include_directories(anotest_percmeterguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_percmeterguard COMMAND anotest_percmeterguard)
set_tests_properties(anoptic_percmeterguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Music mode domain guard: ano_music_set_override("mode", v) casts (int)v unchecked
# (music_host.c:194) and the config seam copies cfg.mode as blind (:58); the conductor pins
# either verbatim refusing only ANO_MODE_NONE, launders negatives via (uint8_t), and every bar
# indexes ano_mode_intervals' [7][7] table with the raw value (music_theory.c:58) while
# republishing it in AnoMusicMeaning.mode against its AnoMode contract (docs/BUGS.md, Music /
# Interface-level). Fails (out-of-enum meaning.mode observed on both seams) until the mode
# domain is validated.
add_executable(anotest_musicmodeguard anotest_musicmodeguard.c)
target_link_libraries(anotest_musicmodeguard PRIVATE anoptic_core)
add_test(NAME anoptic_musicmodeguard COMMAND anotest_musicmodeguard)
set_tests_properties(anoptic_musicmodeguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Cadence-cycle count guard: the config seam copies cadencePolicyCount unvalidated
# (music_host.c:66) while clamping its sibling motifLibraryCount (:101), and policy_of's
# explicit-cycle arm indexes cadencePolicies[phrase % count] (music_conductor.c:147), so any
# count > 8 reads past the int8_t [8] array and republishes an out-of-enum policy through
# AnoMusicMeaning.cadencePolicy into the same one-sided [3]-table guard chain as the
# cadence_policy override (docs/BUGS.md, Music / Interface-level). Fails (out-of-enum meaning
# on phrase 8's flagged bars, or a crash in the policy tables) until the count is clamped or
# rejected at the seam.
add_executable(anotest_musiccadcountguard anotest_musiccadcountguard.c)
target_link_libraries(anotest_musiccadcountguard PRIVATE anoptic_core)
add_test(NAME anoptic_musiccadcountguard COMMAND anotest_musiccadcountguard)
set_tests_properties(anoptic_musiccadcountguard PROPERTIES TIMEOUT 30 LABELS "unit;mem")

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
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}
                VERBATIM)
    endif()

    # Render-side slot authority: pure logic, links anoptic_render but needs no GPU device.
    add_executable(anotest_render_slots anotest_render_slots.c)
    target_link_libraries(anotest_render_slots PRIVATE anoptic_render)
    add_test(NAME anoptic_render_slots COMMAND anotest_render_slots)
    set_tests_properties(anoptic_render_slots PROPERTIES TIMEOUT 10 LABELS "unit;render")

    # Bulk-range mid-batch failure guard: render_slots_alloc_range publishes each element's
    # forward/reverse mapping as it walks the batch (:93/:94) but advances slotHighWater only in
    # the epilogue (:96), so the mid-batch logical_reserve OOM return (render_slots.c:92) reports
    # UNMAPPED while the prefix stays mapped into the un-owned high-water region; apply.c:167
    # discards the return, the next single CREATE re-hands the same physical slot to a different
    # render_id, and a destroy of the stale id retires the live owner's slot into the free-list
    # for a third owner (docs/BUGS.md, Render / Vulkan backend / Implementation). Compiles the
    # real render_slots.c TU with the heap tokens interposed to fail exactly one growth; pure
    # index bookkeeping, no GPU device, no loader. Fails until a failed batch unwinds (or never
    # publishes) its prefix.
    add_executable(anotest_slotrangeguard anotest_slotrangeguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/render_slots.c)
    target_include_directories(anotest_slotrangeguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(anotest_slotrangeguard PRIVATE mi_heap_malloc=anotest_seam_heap_malloc mi_heap_realloc=anotest_seam_heap_realloc mi_free=anotest_seam_free)
    target_link_libraries(anotest_slotrangeguard PRIVATE anoptic_core)
    add_test(NAME anoptic_slotrangeguard COMMAND anotest_slotrangeguard)
    set_tests_properties(anoptic_slotrangeguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Sentinel-id wrap guard: logical_reserve grows the logical map to need = render_id + 1u
    # (render_slots.c:35), so the module's own sentinel 0xFFFFFFFF wraps need to 0, ensure_cap
    # reports success without growing, and render_slots_alloc consumes a slot then stores it at
    # logicalToSlot[0xFFFFFFFF] 〜 a wild write ~16 GiB past the map on the render master thread;
    # ano_render_submit is a bare ring push and apply.c:125/:167 forward the id raw, while the
    # resolve twin guards this exact edge (:102) (docs/BUGS.md, Render / Vulkan backend /
    # Interface-level). Compiles the real render_slots.c TU with the heap tokens interposed onto
    # libc; pure index bookkeeping, no GPU device, no loader. Fails (crash on the wild store, or
    # a live slot handed to the sentinel) until the alloc seam rejects the sentinel.
    add_executable(anotest_slotsentinelguard anotest_slotsentinelguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/render_slots.c)
    target_include_directories(anotest_slotsentinelguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(anotest_slotsentinelguard PRIVATE mi_heap_malloc=anotest_seam_heap_malloc mi_heap_realloc=anotest_seam_heap_realloc mi_free=anotest_seam_free)
    target_link_libraries(anotest_slotsentinelguard PRIVATE anoptic_core)
    add_test(NAME anoptic_slotsentinelguard COMMAND anotest_slotsentinelguard)
    set_tests_properties(anoptic_slotsentinelguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Duplicate-create mapping guard: apply.c:125 forwards cmd.render_id into render_slots_alloc
    # with no mapped-check on either side, and the alloc contract's "render_id unmapped" invariant
    # (render_slots.h:66) is enforced by nobody (alloc stores blind, render_slots.c:79-:80), so a
    # duplicate CREATE of a live id mints a second slot, overwrites the forward map, and strands
    # the old slot live forever with its reverse entry still answering the id 〜 an undestroyable
    # ghost the pick readback misattributes (profiling.c:174); the light sibling guards exactly
    # this (light_registry.c:89) (docs/BUGS.md, Render / Vulkan backend / Interface-level).
    # Compiles the real render_slots.c TU with the heap tokens interposed onto libc; pure index
    # bookkeeping, no GPU device, no loader. Fails (second slot minted, bijection broken, stranded
    # slot pins slotHighWater after full teardown) until the seam rejects or idempotently resolves
    # an already-mapped id.
    add_executable(anotest_slotdupguard anotest_slotdupguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/render_slots.c)
    target_include_directories(anotest_slotdupguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(anotest_slotdupguard PRIVATE mi_heap_malloc=anotest_seam_heap_malloc mi_heap_realloc=anotest_seam_heap_realloc mi_free=anotest_seam_free)
    target_link_libraries(anotest_slotdupguard PRIVATE anoptic_core)
    add_test(NAME anoptic_slotdupguard COMMAND anotest_slotdupguard)
    set_tests_properties(anoptic_slotdupguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Static-shadow light-type domain guard: register_static_shadow indexes shadowTypeUsed[3]
    # with the raw bridge light type (shadow_casters.c:97/:114), and types 7/10 alias the
    # runtime frustum free-list counters right after the array, so a drained pool resurrects
    # and the next runtime caster double-allocates a live frustum block (docs/BUGS.md,
    # Render / Vulkan backend / Interface-level). Pure host logic, no GPU device. Fails until
    # the type is validated at the bridge seam.
    add_executable(anotest_shadowtypeguard anotest_shadowtypeguard.c)
    target_link_libraries(anotest_shadowtypeguard PRIVATE anoptic_render)
    add_test(NAME anoptic_shadowtypeguard COMMAND anotest_shadowtypeguard)
    set_tests_properties(anoptic_shadowtypeguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Boot telemetry handshake guard: music_world_start spins ano_audio_acquire_telemetry
    # 200x and seeds ano_synth_transport_start from the out-param whether or not any acquire
    # succeeded, consuming uninitialized stack telemetry on the timeout arm (main.c:391,
    # docs/BUGS.md, Engine / Interlink-Composition). Compiles the real main.c TU with the
    # acquire and transport-start seam tokens interposed to force the never-publish arm
    # deterministically; real audio world underneath, no GPU device touched. Fails (transport
    # seeded from a failed handshake) until the boot path honors the acquire's false arm.
    add_executable(anotest_boottelemetryguard anotest_boottelemetryguard.c)
    target_link_libraries(anotest_boottelemetryguard PRIVATE anoptic_render m)
    target_include_directories(anotest_boottelemetryguard PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME anoptic_boottelemetryguard COMMAND anotest_boottelemetryguard)
    set_tests_properties(anoptic_boottelemetryguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Texture mip-0 upload copy-extent guard: createTextureImage hands texture.texWidth as
    # BOTH dimensions of the buffer->image copy (texture.c:435) where its sibling
    # createTextureImageFromPixels passes (width, height) (:377), so every non-square file
    # over-reads the staging buffer (landscape) or leaves mip-0 rows undefined (portrait)
    # (docs/BUGS.md, Render / Vulkan backend / Implementation). Compiles the real texture.c
    # TU against link-seam vk/helper stubs that capture the copy region; real stbi decode,
    # no GPU device, no loader. Fails until the copy takes texHeight.
    add_executable(anotest_texuploadguard anotest_texuploadguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/texture/texture.c)
    target_include_directories(anotest_texuploadguard PRIVATE ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_texuploadguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_texuploadguard COMMAND anotest_texuploadguard)
    set_tests_properties(anoptic_texuploadguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Transfer-queue presence guard: createLogicalDevice's transfer-queue fetch is armed by
    # indices->computePresent 〜 a copy-paste of the compute block above it 〜 not transferPresent,
    # and the queue-create loop consumes transferFamily with no presence check (device.c:663 /
    # :537, docs/BUGS.md, Render / Vulkan backend / Implementation). A spec-legal family table
    # that omits TRANSFER_BIT rides findQueueFamilies' UINT32_MAX sentinel into vkCreateDevice
    # and vkGetDeviceQueue. Compiles the real device.c TU against link-seam vk/config stubs that
    # capture every requested family index; no GPU device, no loader. Fails until the sentinel
    # stops reaching the device.
    add_executable(anotest_transferqueueguard anotest_transferqueueguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/device.c)
    target_include_directories(anotest_transferqueueguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_transferqueueguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_transferqueueguard COMMAND anotest_transferqueueguard)
    set_tests_properties(anoptic_transferqueueguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Image-view creation failure guard: createImageView's error arm only logs 〜 a failed
    # vkCreateImageView falls through to `return imageView`, an out-param the failed call's
    # contract leaves with undefined contents (swapchain.c:428, docs/BUGS.md, Render / Vulkan
    # backend / Implementation), so all ten call sites store indeterminate stack bytes as a
    # live handle, createImageViews reports true regardless (:441), and cleanupSwapChain
    # destroys the garbage (:187). Compiles the real swapchain.c TU against link-seam
    # vk/glfw/engine stubs whose failure arm writes a spec-legal poison out-param; no GPU
    # device, no loader. Fails until failure returns VK_NULL_HANDLE.
    add_executable(anotest_imageviewguard anotest_imageviewguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/swapchain.c)
    target_include_directories(anotest_imageviewguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_imageviewguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_imageviewguard COMMAND anotest_imageviewguard)
    set_tests_properties(anoptic_imageviewguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Swapchain support-query leak guard: initSwapChain consumes querySwapChainSupport's two
    # calloc'd arrays (formats swapchain.c:33, presentModes :38) and returns on both arms
    # without freeing either 〜 the by-value details struct is a discarded local 〜 so the boot
    # call (vulkanMaster.c:449) orphans both blocks and every resize recreation
    # (recreateSwapChain, swapchain.c:353) orphans two more, unbounded accrual under resize
    # storms (swapchain.c:110, docs/BUGS.md, Render / Vulkan backend / Implementation).
    # Compiles the real swapchain.c TU against link-seam vk/glfw/engine stubs with the TU's
    # allocator tokens interposed to audit heap balance; no GPU device, no loader. Fails until
    # the support arrays are discharged before return.
    add_executable(anotest_swapleakguard anotest_swapleakguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/swapchain.c)
    target_include_directories(anotest_swapleakguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(anotest_swapleakguard PRIVATE mi_malloc=anotest_seam_malloc mi_calloc=anotest_seam_calloc mi_free=anotest_seam_free)
    target_link_libraries(anotest_swapleakguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_swapleakguard COMMAND anotest_swapleakguard)
    set_tests_properties(anoptic_swapleakguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Staged-upload copy-failure guard: stagingTransfer's copyBuffer arm (commands.c:201) is
    # dead code 〜 copyBuffer returns true unconditionally (:267) because the single-time-command
    # pair discards every VkResult (alloc :222, begin :228, end :235, submit :247, wait :248),
    # so a one-shot GPU copy that never executed reports success to the text-bake uploads
    # (text_raster.c:639/:644), a failed CB allocation's undefined out-param handle rides the
    # recording entry points as VUID breaches, and the arm itself would leak the staging buffer
    # if a fix armed it (:204 skips the :208 destroy) (docs/BUGS.md, Render / Vulkan backend /
    # Implementation). Compiles the real commands.c TU against link-seam vk/engine stubs that
    # capture creates, destroys, copies and submits; no GPU device, no loader. Fails until
    # copyBuffer propagates the failing result and discharges the staging buffer on every path.
    add_executable(anotest_stagingcopyguard anotest_stagingcopyguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/commands.c)
    target_include_directories(anotest_stagingcopyguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_stagingcopyguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_stagingcopyguard COMMAND anotest_stagingcopyguard)
    set_tests_properties(anoptic_stagingcopyguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Texture staging-buffer failure-path leak guard: createTextureImage acquires its staging
    # buffer at texture.c:415 and discharges it only in the success epilogue (:445), so the
    # failure returns at :426 (image creation) and :432 (layout transition) orphan the live
    # VkBuffer; *outStagingBuffer is written only at :445, the glTF caller's calloc'd slot stays
    # VK_NULL_HANDLE while stagingCount++ already consumed it (ano_GltfParser.c:274) and the
    # destroy loop at :296 no-ops, and the sibling createTextureImageFromPixels arms
    # (:368/:374/:382) orphan identically (texture.c:426, docs/BUGS.md, Render / Vulkan backend /
    # Implementation). Compiles the real texture.c TU against link-seam vk/helper stubs keeping a
    # mint/destroy ledger of every staging handle; real stbi decode, no GPU device, no loader.
    # Fails until every failure return destroys or hands out the staging buffer.
    add_executable(anotest_texstagingguard anotest_texstagingguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/texture/texture.c)
    target_include_directories(anotest_texstagingguard PRIVATE ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_texstagingguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_texstagingguard COMMAND anotest_texstagingguard)
    set_tests_properties(anoptic_texstagingguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Texture staging-acquisition failure guard: createTextureImage discards createDataBuffer's
    # status at texture.c:415 and consumes the out-params regardless, so the :417-418 memcpy
    # writes the decoded image through stagingAlloc.mapped 〜 NULL on the callee's arena-exhaustion
    # arm (commands.c:59-64), an uninitialized stack pointer on its vkCreateBuffer arm
    # (commands.c:50-54); the sibling createTextureImageFromPixels repeats the shape at :360-363
    # (docs/BUGS.md, Render / Vulkan backend / Implementation). Compiles the real texture.c TU
    # against link-seam vk/helper stubs whose acquire failure is contract-faithful to the real
    # exhaustion arm; real stbi decode, no GPU device, no loader. Fails (access violation in the
    # memcpy) until the acquire's false arm is honored.
    add_executable(anotest_texacquireguard anotest_texacquireguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/texture/texture.c)
    target_include_directories(anotest_texacquireguard PRIVATE ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_texacquireguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_texacquireguard COMMAND anotest_texacquireguard)
    set_tests_properties(anoptic_texacquireguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Scene-buffer FATAL fall-through guard: createMaterialBuffer's vkCreateBuffer failure arm
    # logs ANO_FATAL and falls through (scene_buffers.c:34-36 〜 ano_log never aborts), so :39
    # consumes the failed call's out-param handle, gpu_alloc sizes from garbage requirements,
    # :46 binds it and the creator returns true; createTransformBuffer repeats the shape at
    # :174-176/:179/:186 and createCullingBuffers drops all six results with no guard
    # (:343/:359/:375/:392/:409/:424), while the same file's siblings return false on the
    # identical check (:212/:249/:288/:295) (docs/BUGS.md, Render / Vulkan backend /
    # Implementation). Compiles the real scene_buffers.c TU against link-seam vk/helper stubs
    # keeping a mint ledger; the contract-faithful failing create leaves the out-param untouched
    # so pre-poisoned handle slots expose every wild consumption deterministically. No GPU
    # device, no loader. Fails until each creator's failure arm actually returns false.
    add_executable(anotest_scenebufferguard anotest_scenebufferguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/scene_buffers.c)
    target_include_directories(anotest_scenebufferguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_scenebufferguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_scenebufferguard COMMAND anotest_scenebufferguard)
    set_tests_properties(anoptic_scenebufferguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Texture mip-chain publish guard: createTextureImage discards generateMipmaps' bool at
    # texture.c:437 while the fallback whole-chain TRANSFER_DST->SHADER_READ transition beneath
    # it is commented out (:439-443), and the generator's only failure arm 〜 driver-reported
    # format properties without SAMPLED_IMAGE_FILTER_LINEAR (:239-243) 〜 returns before
    # recording anything, so the upload returns true with the image parked in
    # TRANSFER_DST_OPTIMAL and mips 1..N-1 never written while the full-chain view is bound
    # bindless as SHADER_READ_ONLY_OPTIMAL (:36) (texture.c:437, docs/BUGS.md, Render / Vulkan
    # backend / Implementation). Compiles the real texture.c TU against link-seam vk/helper
    # stubs shadowing per-mip layout and written state from every recorded barrier, copy and
    # blit; real stbi decode, no GPU device, no loader. Fails until a true return implies a
    # fully written SHADER_READ chain.
    add_executable(anotest_texmipchainguard anotest_texmipchainguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/texture/texture.c)
    target_include_directories(anotest_texmipchainguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_texmipchainguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_texmipchainguard COMMAND anotest_texmipchainguard)
    set_tests_properties(anoptic_texmipchainguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Init depth/Hi-Z FATAL fall-through guard: initVulkan's depth and Hi-Z arms log ANO_FATAL
    # "Quitting init" and fall through (vulkanMaster.c:505/:511 〜 ano_log never aborts) with no
    # unInitVulkan and no return, while every sibling arm in the same function tears down and
    # returns false (:444-:446, :518-:520, ...), so a failed createDepthResources /
    # createHiZResources keeps initializing: layouts and pipelines build against an unset
    # depthFormat, updateHiZDescriptorSets (:664) writes the never-created hizMipViews into live
    # sets, the hizValidOrdinal warmup gate stays 0 (attachments.c:185 is success-only), and
    # initVulkan returns true (:687); createColorResources above (:501) cannot even report
    # failure (docs/BUGS.md, Render / Vulkan backend / Implementation). Compiles the real
    # vulkanMaster.c TU against link-seam stubs for every init helper, recording which steps run
    # after the failed arm; no GPU device, no loader, no glfw. Fails until a failed depth/Hi-Z
    # arm stops the init.
    add_executable(anotest_initdepthguard anotest_initdepthguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/vulkanMaster.c)
    target_include_directories(anotest_initdepthguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_initdepthguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_initdepthguard COMMAND anotest_initdepthguard)
    set_tests_properties(anoptic_initdepthguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Entity-growth descriptor-liveness guard: ensureEntityCapacity's && chain publishes per arm 〜
    # slot_upload_grow_device destroys the old device buffer and swaps in the new one
    # (slot_upload.c:191-:193) 〜 while updateUboDescriptorSets runs only on full success (:276), so a
    # later arm's OOM returns false (:258) with the prefix arms' old VkBuffers destroyed and the live
    # descriptor sets still referencing them (descriptors.c:550/:743/:748); the FATAL at :257 never
    # aborts and apply.c:123/:164 drop the spawn and keep rendering 〜 the next frame binds destroyed
    # buffers (docs/BUGS.md, Render / Vulkan backend / Implementation, slot_upload.c:221). Compiles the
    # real slot_upload.c TU against link-seam vk/helper stubs with a mint/destroy ledger and a
    # descriptor-snapshot updateUboDescriptorSets; no GPU device, no loader. Fails until a failed
    # growth leaves every descriptor-referenced handle live (defer, unwind, or re-point).
    add_executable(anotest_entitygrowguard anotest_entitygrowguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/slot_upload.c)
    target_include_directories(anotest_entitygrowguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_entitygrowguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_entitygrowguard COMMAND anotest_entitygrowguard)
    set_tests_properties(anoptic_entitygrowguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Texture registration ownership-drop guard: ano_vk_register_texture is the only route a
    # loaded texture's image/view take into the teardown registry (cleanup.c:64-:71 walks
    # primitives.textureBuffers and nothing else destroys them), yet it returns void and its
    # realloc-failure arm drops the TextureData record after an ANO_ERROR log
    # (components.c:76-:78), so the glTF caller proceeds to bindless-register and draw a view
    # whose ownership vanished (ano_GltfParser.c:282-:288) 〜 the objects orphan permanently at
    # shutdown with no failure channel; the mesh twin ano_vk_register_mesh shares the shape and
    # has no production caller (docs/BUGS.md, Render / Vulkan backend / Interlink-Composition,
    # components.c:72). Compiles the real components.c TU with the allocator tokens interposed
    # onto libc to refuse exactly one registry growth; caller and teardown modeled
    # contract-faithfully; no GPU device, no loader. Fails until a refused registration cannot
    # silently orphan the adopted handles.
    add_executable(anotest_texregisterguard anotest_texregisterguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/components.c)
    target_include_directories(anotest_texregisterguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(anotest_texregisterguard PRIVATE mi_realloc=anotest_seam_realloc mi_free=anotest_seam_free)
    target_link_libraries(anotest_texregisterguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_texregisterguard COMMAND anotest_texregisterguard)
    set_tests_properties(anoptic_texregisterguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Descriptor-pool sampler-budget guard: createDescriptorPool's combined-image-sampler term
    # (descriptors.c:39) budgets tonemap/view + 4 shadow + Hi-Z/mip + cull bind11/view + 1 text
    # overlay but omits the task-cull Hi-Z sampler the global layout gains at binding 13 when
    # taskCull is on (layouts.c:123-:128/:149, written at descriptors.c:483-:496), so with the
    # shipped constants createDescriptorSets consumes the sampler budget exactly (219/219) and the
    # pool's LAST consumer 〜 the text overlay sets (text_raster.c:846/:852) 〜 is refused with
    # VK_ERROR_OUT_OF_POOL_MEMORY and silently disabled (:862-:864), UI riding with it, on exactly
    # the mesh+task hardware where taskCull defaults on (vulkanMaster.c:390) (docs/BUGS.md,
    # Render / Vulkan backend / Implementation). Compiles the real descriptors.c TU against
    # link-seam vk stubs doing contract-faithful per-type pool accounting; layout specs mirror the
    # real binding tables, the text consumer mirrors text_raster.c batch for batch; no GPU device,
    # no loader. Fails until the pool budgets the binding-13 samplers.
    add_executable(anotest_descpoolguard anotest_descpoolguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/descriptors.c)
    target_include_directories(anotest_descpoolguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_descpoolguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_descpoolguard COMMAND anotest_descpoolguard)
    set_tests_properties(anoptic_descpoolguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Window-create failure guard: initWindow guards glfwInit's failure but never glfwCreateWindow's
    # (window.c:214) 〜 the NULL handle from a failed create (headless / dead display:
    # GLFW_PLATFORM_ERROR; a 0x0 configured resolution: GLFW_INVALID_VALUE) rides ten GLFW entry
    # points (:226-:241) that require a valid window 〜 assert in debug GLFW, straight NULL deref in
    # release 〜 so boot crashes inside GLFW instead of returning NULL to the caller's
    # FATAL-and-teardown arm (vulkanMaster.c:324, instanceInit.h:26 contract) (docs/BUGS.md,
    # Render / Vulkan backend / Implementation). Compiles the real window.c TU against link-seam
    # glfw/config/render-api stubs keeping a NULL-handle ledger; no GPU device, no loader, no real
    # glfw. Fails until a failed create returns NULL with the dead handle untouched.
    add_executable(anotest_windowcreateguard anotest_windowcreateguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/window.c)
    target_include_directories(anotest_windowcreateguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_windowcreateguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_windowcreateguard COMMAND anotest_windowcreateguard)
    set_tests_properties(anoptic_windowcreateguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Async light-cull UBO sharing guard: createUniformBuffers mints each view's camera UBO
    # EXCLUSIVE with no asyncLc arm (commands.c:82), yet updateClusterDescriptorSets binds it into
    # the light-cull set at binding 0 (descriptors.c:308/:330) whose dispatch runs on the dedicated
    # compute queue family when async light-cull is on (hiz.c:93-:111, submit.c:113), and no
    # ownership transfer exists anywhere (every barrier passes VK_QUEUE_FAMILY_IGNORED) 〜 the
    # compute family's reads of view/proj/near/far/clusterDims (lightcull.comp:78-:118) are
    # spec-undefined, while the module's own buffer_share_async_compute rule (slot_upload.c:46)
    # treats every sibling binding in the same set (scene_buffers.c:56/:208/:284) (docs/BUGS.md,
    # Render / Vulkan backend / Implementation, commands.c:82). Compiles the real commands.c,
    # descriptors.c, scene_buffers.c and slot_upload.c TUs against link-seam vk stubs keeping a
    # sharing-mode ledger per created buffer and a write ledger per light-cull set; no GPU device,
    # no loader. Fails until every buffer the async light-cull consumes is gfx+compute CONCURRENT.
    add_executable(anotest_uboshareguard anotest_uboshareguard.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/commands.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/descriptors.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/scene_buffers.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/slot_upload.c)
    target_include_directories(anotest_uboshareguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_uboshareguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_uboshareguard COMMAND anotest_uboshareguard)
    set_tests_properties(anoptic_uboshareguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Shader-module sentinel guard: ano_vk_init_compute discards createShaderModule's documented
    # NULL failure return at all nine consumption sites (compute.c:83/:152/:197/:254/:262/:326/
    # :386/:443/:478) and hands the dead handle to vkCreateComputePipelines as stage.module 〜 an
    # invalid-usage pipeline create (VUID-VkPipelineShaderStageCreateInfo-module, no maintenance5
    # fallback chained) instead of the boot refusal the same function's loadFile arms prove
    # intended, while pipeline.c:104 guards the identical mint one file over (docs/BUGS.md,
    # Render / Vulkan backend / Implementation, compute.c:83). Compiles the real compute.c TU
    # against link-seam vk stubs with a module mint/destroy ledger and a stage.module ledger;
    # loadFile/createShaderModule are contract-faithful stand-ins mirroring pipeline.c:41/:78-93;
    # no GPU device, no loader. Fails until a NULL module aborts init before pipeline creation.
    add_executable(anotest_shadermodguard anotest_shadermodguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/pipelines/compute.c)
    target_include_directories(anotest_shadermodguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_shadermodguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_shadermodguard COMMAND anotest_shadermodguard)
    set_tests_properties(anoptic_shadermodguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Graphics-side shader-module sentinel guard: every graphics pipeline builder discards
    # createShaderModule's documented NULL failure return and bakes the dead handle into
    # VkPipelineShaderStageCreateInfo.module for vkCreateGraphicsPipelines 〜 family head
    # flat_init_with_cull mints three unchecked (flat.c:90-:92, consumed at :106/:112/:269 into
    # the creates at :244/:261/:296 for all three boot lanes), the shape repeating at
    # shadow_pipe.c:52-:53/:162-:163/:214-:215, tonemap.c:55-:56, additive.c:68-:69,
    # transmission.c:75-:76, text_raster.c:344/:379-:380/:489-:490 〜 an invalid-usage pipeline
    # create (VUID-VkPipelineShaderStageCreateInfo-module, no maintenance5 fallback) instead of
    # the boot refusal every loadFile arm one line above proves intended, while pipeline.c:104
    # guards the identical mint one file over (docs/BUGS.md, Render / Vulkan backend /
    # Implementation, flat.c:90). Compiles the real flat.c TU against link-seam vk stubs with a
    # module mint/destroy ledger and a per-stage module ledger; loadFile/createShaderModule/
    # ano_pipeline_task_stage are contract-faithful stand-ins mirroring pipeline.c:41/:78-93/
    # :96-117; no GPU device, no loader. Fails until a NULL module aborts init before pipeline
    # creation.
    add_executable(anotest_gfxshadermodguard anotest_gfxshadermodguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/pipelines/flat.c)
    target_include_directories(anotest_gfxshadermodguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_gfxshadermodguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_gfxshadermodguard COMMAND anotest_gfxshadermodguard)
    set_tests_properties(anoptic_gfxshadermodguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Flat-builder failure-path orphan guard: flat_init_with_cull discharges its three
    # ano_aligned_malloc'd shader-code buffers and minted VkShaderModules only in the success
    # epilogue (flat.c:301-:309), so the failure returns at :79/:88/:101/:244/:261/:296 orphan
    # whatever is live 〜 stack locals no cleanup can reach, the author's own TODO names the
    # missing "garbo removers" (pipeline.c:122), and shadow_pipe.c:160-:161 shares the shape
    # (flat.c:244, docs/BUGS.md, Render / Vulkan backend / Implementation). Compiles the real
    # flat.c TU against link-seam vk stubs with the TU's ano_aligned allocator interposed for a
    # live-set ledger plus a module mint/destroy ledger; armed Nth-call refusals on
    # vkCreateGraphicsPipelines and loadFile; best-effort real teardown before measuring so
    # ownership-adoption fixes pass; no GPU device, no loader. Fails until every failure return
    # discharges the buffers and modules acquired before it.
    add_executable(anotest_flatorphanguard anotest_flatorphanguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance/pipelines/flat.c)
    target_include_directories(anotest_flatorphanguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_flatorphanguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_flatorphanguard COMMAND anotest_flatorphanguard)
    set_tests_properties(anoptic_flatorphanguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")

    # Frame-record begin guard: recordCommandBuffer checks vkBeginCommandBuffer and only logs 〜
    # the function is void, drawFrame has no failure channel (vulkanMaster.c:228) 〜 and the
    # asyncLc split repeats the shape at record.c:202/:205, so a begin refused under memory
    # pressure keeps recording: every vkCmd* lands on a command buffer never put in RECORDING
    # state, the :297 end is a second state breach, and ano_frame_submit consumes the
    # never-recorded buffer as if executable (submit.c:44/:72/:94), while the same module's
    # submit twin returns false on its own failing call (submit.c:47-:51) (docs/BUGS.md, Render /
    # Vulkan backend / Implementation, record.c:29). Compiles the real record.c + passes.c TUs
    # against link-seam vk stubs keeping a per-CB lifecycle state machine and dead-buffer
    # ledgers; armed Nth-call begin refusals; no GPU device, no loader. Fails until a failed
    # begin stops recording on both the plain and split paths.
    add_executable(anotest_recordbeginguard anotest_recordbeginguard.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/frame/record.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/frame/passes.c)
    target_include_directories(anotest_recordbeginguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_recordbeginguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_recordbeginguard COMMAND anotest_recordbeginguard)
    set_tests_properties(anoptic_recordbeginguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Composite inset scissor guard: ano_record_composite places each aux-view inset at
    # y = H - margin - (insetH + margin)*(idx+1) + margin in uint32 before the int32 cast
    # (record_views.c:302, x sibling at :301), so swapchain height <= 22 wraps y negative
    # (H=22 gives -1) and rides into vkCmdSetScissor offset.y 〜 VUID-vkCmdSetScissor-x-00595,
    # UB on real drivers 〜 with H <= 2 also emitting a zero-height inset viewport
    # (VUID-VkViewport-height-01772); reachable since the composite runs unconditionally for
    # ANO_VIEW_COUNT 2, imageExtent adopts the surface currentExtent 〜 the raw Win32 client
    # size 〜 and recreateSwapChain's only floor is the 0x0 wait loop (swapchain.c:340-:345);
    # Win32 minimum tracking size protects x but the client height drags into [1,22]
    # (docs/BUGS.md, Render / Vulkan backend / Implementation, record_views.c:302). Compiles
    # the real record_views.c + passes.c TUs against link-seam vk stubs capturing every
    # vkCmdSetScissor/vkCmdSetViewport argument; no GPU device, no loader. Fails until
    # small-height composites place (or skip) the inset legally.
    add_executable(anotest_insetscissorguard anotest_insetscissorguard.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/frame/record_views.c
        ${CMAKE_SOURCE_DIR}/src/vulkan_backend/frame/passes.c)
    target_include_directories(anotest_insetscissorguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vulkan_backend/instance ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_insetscissorguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_insetscissorguard COMMAND anotest_insetscissorguard)
    set_tests_properties(anoptic_insetscissorguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # Memory-type probe sign-bit shift guard: findMemoryType tests typeFilter & (1 << i) with
    # i walking to memoryTypeCount - 1 (gpu_alloc.c:12, duplicated verbatim at commands.c:173),
    # and the legal domain is VK_MAX_MEMORY_TYPES == 32 with the count adopted raw from
    # vkGetPhysicalDeviceMemoryProperties, so a device exposing all 32 types evaluates 1 << 31 〜
    # a signed int shifted into its sign bit, UB under the build's C23 (6.5.7; this tree's clang
    # in -std=c23 traps it) 〜 on the very probe that should match bit 31; common codegen wraps
    # to INT_MIN and happens to behave, which is why it stayed latent (docs/BUGS.md, Render /
    # Vulkan backend / Implementation). Compiles the real gpu_alloc.c TU with -fsanitize=shift
    # so the abstract-machine UB is a deterministic halt instead of codegen luck; link-seam vk
    # stubs mint fake device memory and ledger every requested type index; no GPU device, no
    # loader. Fails (shift trap at gpu_alloc.c:12) until the mask math is unsigned in both twins.
    add_executable(anotest_memtypeshiftguard anotest_memtypeshiftguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/gpu_alloc.c)
    target_include_directories(anotest_memtypeshiftguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    target_compile_options(anotest_memtypeshiftguard PRIVATE -fsanitize=shift -fno-sanitize-recover=shift)
    target_link_options(anotest_memtypeshiftguard PRIVATE -fsanitize=shift)
    target_link_libraries(anotest_memtypeshiftguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_memtypeshiftguard COMMAND anotest_memtypeshiftguard)
    set_tests_properties(anoptic_memtypeshiftguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    # glTF failed-texture orphan guard: parseGltf hears createTextureImage's false
    # (ano_GltfParser.c:276) but no failure arm discharges the out-params the callee has already
    # written 〜 the post-create failure family (texture.c:429-:433 transition; :446-:450 view,
    # armed the day the tallied swapchain.c:428 / texture.c:437 fixes land) returns false with
    # the VkImage + texture-arena allocation live in loadedImages[t]/loadedAllocs[t], adoption
    # into the teardown registry is success-only (ano_vk_register_texture at :282, the sole
    # route cleanupVulkan walks), and :619-:621 free the host arrays holding the only copies of
    # the handles 〜 one whole device texture orphans per failed load (docs/BUGS.md, Render /
    # Vulkan backend / Interlink-Composition, ano_GltfParser.c:277). Compiles the real
    # ano_GltfParser.c TU (real cgltf parse of a hand-built two-texture .gltf) against
    # link-seam stubs; createTextureImage keeps a mint/adopt/destroy image ledger and models
    # the callee's post-create arm as written. No GPU device, no loader. Fails until a heard
    # false leaves zero un-adopted, un-destroyed images.
    add_executable(anotest_gltftexleakguard anotest_gltftexleakguard.c ${CMAKE_SOURCE_DIR}/src/render/gltf/ano_GltfParser.c)
    target_include_directories(anotest_gltftexleakguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(anotest_gltftexleakguard PRIVATE anoptic_core m)
    add_test(NAME anoptic_gltftexleakguard COMMAND anotest_gltftexleakguard)
    set_tests_properties(anoptic_gltftexleakguard PROPERTIES TIMEOUT 30 LABELS "unit;render")

    add_executable(anotest_vk_lifecycle anotest_vk_lifecycle.c)
    target_link_libraries(anotest_vk_lifecycle PRIVATE anoptic_render)
    add_test(NAME anotest_vk_lifecycle COMMAND anotest_vk_lifecycle)
    # SKIP_RETURN_CODE 77: the device tests skip (not fail) where no Vulkan device can
    # run the renderer, e.g. lavapipe-only sandboxes (no multisampled integer color).
    set_tests_properties(anotest_vk_lifecycle PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        SKIP_RETURN_CODE 77)

    add_executable(anotest_vk_components anotest_vk_components.c)
    target_link_libraries(anotest_vk_components PRIVATE anoptic_render)
    add_test(NAME anotest_vk_components COMMAND anotest_vk_components)
    set_tests_properties(anotest_vk_components PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_compliance_layers anotest_vk_compliance_layers.c)
    target_link_libraries(anotest_vk_compliance_layers PRIVATE anoptic_render)
    add_test(NAME anotest_vk_compliance_layers COMMAND anotest_vk_compliance_layers)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_compliance_layers PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>" SKIP_RETURN_CODE 77)

    add_executable(anotest_vk_memory anotest_vk_memory.c)
    target_link_libraries(anotest_vk_memory PRIVATE anoptic_render)
    add_test(NAME anotest_vk_memory COMMAND anotest_vk_memory)
    set_tests_properties(anotest_vk_memory PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        SKIP_RETURN_CODE 77)

    add_executable(anotest_vk_sync anotest_vk_sync.c)
    target_link_libraries(anotest_vk_sync PRIVATE anoptic_render)
    add_test(NAME anotest_vk_sync COMMAND anotest_vk_sync)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>" SKIP_RETURN_CODE 77)

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

# Sanitizers run 2-15x slower and CI runners have 2 cores; scale every ctest
# TIMEOUT so starvation cannot masquerade as failure (musicdrive hit its 120 s
# budget under tsan on 2026-07-16 with zero race reports). Tests without an
# explicit TIMEOUT keep the ctest default.
if(ANOPTIC_SANITIZE STREQUAL "tsan")
    set(ANO_TEST_TIMEOUT_SCALE 8)
elseif(ANOPTIC_SANITIZE STREQUAL "asan")
    set(ANO_TEST_TIMEOUT_SCALE 4)
else()
    set(ANO_TEST_TIMEOUT_SCALE 1)
endif()
if(ANO_TEST_TIMEOUT_SCALE GREATER 1)
    get_directory_property(ANO_ALL_TESTS TESTS)
    foreach(ANO_T IN LISTS ANO_ALL_TESTS)
        get_test_property(${ANO_T} TIMEOUT ANO_T_SECS)
        if(ANO_T_SECS)
            math(EXPR ANO_T_SECS "${ANO_T_SECS} * ${ANO_TEST_TIMEOUT_SCALE}")
            set_tests_properties(${ANO_T} PROPERTIES TIMEOUT ${ANO_T_SECS})
        endif()
    endforeach()
endif()