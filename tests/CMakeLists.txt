# Test executables link anoptic_core. Exit 0 = pass.

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_log.h``
# Scratch dirs anchor to the test exe dir at runtime (ano_fs_chdir_gamepath / scratch_anchor_to_exe).
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

# Testing for ``anoptic_memory.h`` (mimalloc heaps, aligned/scoped alloc, huge pages)
add_executable(anotest_memory anotest_memory.c)
target_link_libraries(anotest_memory PRIVATE anoptic_core)
add_test(NAME anoptic_memory COMMAND anotest_memory)
set_tests_properties(anoptic_memory PROPERTIES TIMEOUT 30 LABELS "unit;mem")

# Easter egg: the original allocator experiment, preserved verbatim (see the file banner). 
# Built so it cannot rot, but DISABLED in ctest
# -- run ./anotest_chariots by hand. -w
add_executable(anotest_chariots anotest_chariots.c)
target_link_libraries(anotest_chariots PRIVATE anoptic_core)
target_compile_options(anotest_chariots PRIVATE -w)
add_test(NAME anoptic_chariots COMMAND anotest_chariots)
set_tests_properties(anoptic_chariots PROPERTIES DISABLED TRUE LABELS "optional;easteregg")

# Logger bench: lock-free ring (anoptic_core) vs mutex baseline (log_old.c / mtxlog_*, this target only).
# DISABLED in ctest. Run ./anotest_logbench by hand.
add_executable(anotest_logbench anotest_logbench.c ${CMAKE_SOURCE_DIR}/src/log/log_old.c)
target_include_directories(anotest_logbench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anotest_logbench PRIVATE anoptic_core)
add_test(NAME anoptic_logbench COMMAND anotest_logbench)
set_tests_properties(anoptic_logbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger tail bench: enqueue latency percentiles (p50/p90/p99/p99.9). DISABLED in ctest.
# Run ./anotest_logtail by hand.
add_executable(anotest_logtail anotest_logtail.c)
target_link_libraries(anotest_logtail PRIVATE anoptic_core)
add_test(NAME anoptic_logtail COMMAND anotest_logtail)
set_tests_properties(anoptic_logtail PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger fuzzer: multi-producer ano_log_* with randomized input. Oracle: output lines == enqueued.
# Fixed seed by default. argv[1] scales the soak.
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

# Testing for ``anoptic_threads.h`` (barrier cohort integrity across over-subscribed reuse;
# glibc's barrier is the oracle on Linux). A 30 s watchdog turns a lost cohort into a failure.
add_executable(anotest_threads anotest_threads.c)
target_link_libraries(anotest_threads PRIVATE anoptic_core)
add_test(NAME anoptic_threads COMMAND anotest_threads)
set_tests_properties(anoptic_threads PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

# AnoRenderSubmitResult on the six owned-payload submit APIs; real ano_render_bridge.c, no anoptic_core.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_rendersubmitguard.c)
    add_executable(anotest_rendersubmitguard anotest_rendersubmitguard.c ${CMAKE_SOURCE_DIR}/src/render_bridge/ano_render_bridge.c)
    target_include_directories(anotest_rendersubmitguard PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/mimalloc/include)
    if(UNIX)
        target_compile_definitions(anotest_rendersubmitguard PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    endif()
    add_test(NAME anoptic_rendersubmitguard COMMAND anotest_rendersubmitguard)
    set_tests_properties(anoptic_rendersubmitguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")
endif()

# Testing for the audio stack Phase 0 (churned-heap offline determinism; WAV; live null-backend round-trip)
add_executable(anotest_audio anotest_audio.c)
target_link_libraries(anotest_audio PRIVATE anoptic_core)
add_test(NAME anoptic_audio COMMAND anotest_audio)
set_tests_properties(anoptic_audio PROPERTIES TIMEOUT 60 LABELS "unit;mem")

# Log flood: wide "%*d" deferred records survive a drain with nothing lost and full-width fields.
add_executable(anotest_logflood anotest_logflood.c)
target_link_libraries(anotest_logflood PRIVATE anoptic_core)
add_test(NAME anoptic_logflood COMMAND anotest_logflood)
set_tests_properties(anoptic_logflood PROPERTIES TIMEOUT 120 LABELS "unit;concurrency;mem")

# Live audio smoke on AUTO backend cascade. DISABLED in ctest.
# Run ./anotest_audiotone by hand.
add_executable(anotest_audiotone anotest_audiotone.c)
target_link_libraries(anotest_audiotone PRIVATE anoptic_core)
add_test(NAME anoptic_audiotone COMMAND anotest_audiotone)
set_tests_properties(anoptic_audiotone PROPERTIES DISABLED TRUE TIMEOUT 30 LABELS "optional;audible")

# Live Phase 2 scene: one-shots + ambient bed + orbiting listener + swept bus filter.
# DISABLED in ctest. Run ./anotest_audioscene by hand (argv[1] seconds).
add_executable(anotest_audioscene anotest_audioscene.c)
target_link_libraries(anotest_audioscene PRIVATE anoptic_core)
add_test(NAME anoptic_audioscene COMMAND anotest_audioscene 2)
set_tests_properties(anoptic_audioscene PROPERTIES DISABLED TRUE TIMEOUT 60 LABELS "optional;audible")

# Phase 3 DSP library + console: primitive unit oracles, effect property
# tests, and the churned-heap console golden. All offline, CI-safe.
add_executable(anotest_audiodsp anotest_audiodsp.c)
target_link_libraries(anotest_audiodsp PRIVATE anoptic_core)
add_test(NAME anoptic_audiodsp COMMAND anotest_audiodsp)
set_tests_properties(anoptic_audiodsp PROPERTIES TIMEOUT 60 LABELS "unit;mem")

# Phase 4 synth offline gate: BeatClock oracle, tie-chain bit-compare, patch showcase,
# journey fixture (churned-heap byte-identity + listenable WAV).
add_executable(anotest_synth anotest_synth.c)
target_link_libraries(anotest_synth PRIVATE anoptic_core)
target_compile_definitions(anotest_synth PRIVATE
        ANO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME anoptic_synth COMMAND anotest_synth)
set_tests_properties(anoptic_synth PROPERTIES TIMEOUT 300 LABELS "unit;mem")

# Live Phase 4 scene: journey demo via synth + music console + device backend.
# CI-safe on null (8 s). By hand: the audible three-minute piece.
add_executable(anotest_musichost anotest_musichost.c)
target_link_libraries(anotest_musichost PRIVATE anoptic_core)
target_include_directories(anotest_musichost PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_musichost COMMAND anotest_musichost)

add_executable(anotest_synthlive anotest_synthlive.c)
target_link_libraries(anotest_synthlive PRIVATE anoptic_core)
target_include_directories(anotest_synthlive PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_synthlive COMMAND anotest_synthlive)

# Phase 7: composer in the audio callback (ano_synth_attach_music).
# Sample-identical to batch over shared bars. Live-only: endless piece, bar meaning on downbeat.
add_executable(anotest_musicdrive anotest_musicdrive.c)
target_link_libraries(anotest_musicdrive PRIVATE anoptic_core)
target_include_directories(anotest_musicdrive PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME anoptic_musicdrive COMMAND anotest_musicdrive)
set_tests_properties(anoptic_musicdrive PROPERTIES TIMEOUT 120 LABELS "unit")

# Phase 7 audible: endless piece in the audio callback, live steer, save/seek by reconstruction.
# DISABLED in ctest. Run ./anotest_musicscene 180 by hand.
add_executable(anotest_musicscene anotest_musicscene.c)
target_link_libraries(anotest_musicscene PRIVATE anoptic_core)
add_test(NAME anoptic_musicscene COMMAND anotest_musicscene 30)
set_tests_properties(anoptic_musicscene PROPERTIES DISABLED TRUE TIMEOUT 120 LABELS "optional;audible")

# Bridge hot-op tail bench: publish/acquire latency + per-block mixer cost. DISABLED.
# Run by hand (argv[1] seconds).
add_executable(anotest_bridgebench anotest_bridgebench.c)
target_link_libraries(anotest_bridgebench PRIVATE anoptic_core)
add_test(NAME anoptic_bridgebench COMMAND anotest_bridgebench)
set_tests_properties(anoptic_bridgebench PROPERTIES DISABLED TRUE LABELS "optional;bench;audible")

# Offline source-shape/mixer throughput. DISABLED; run from an -O3 build.
add_executable(anotest_audiomixbench anotest_audiomixbench.c)
target_link_libraries(anotest_audiomixbench PRIVATE anoptic_core)
add_test(NAME anoptic_audiomixbench COMMAND anotest_audiomixbench)
set_tests_properties(anoptic_audiomixbench PROPERTIES DISABLED TRUE LABELS "optional;bench;audio")

add_executable(anotest_synthscene anotest_synthscene.c)
target_link_libraries(anotest_synthscene PRIVATE anoptic_core)
target_compile_definitions(anotest_synthscene PRIVATE
        ANO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME anoptic_synthscene COMMAND anotest_synthscene 8)
set_tests_properties(anoptic_synthscene PROPERTIES DISABLED TRUE TIMEOUT 60 LABELS "optional;audible")

# Phase 6 step-1 gate: determinism kernel bit-compatibility with CPython 3.12
# (BLAKE2b-8 tags, MT19937 seeding + draw semantics, banker's rounding).
add_executable(anotest_music anotest_music.c)
target_link_libraries(anotest_music PRIVATE anoptic_core)
add_test(NAME anoptic_music COMMAND anotest_music)
set_tests_properties(anoptic_music PROPERTIES TIMEOUT 120 LABELS "unit;concurrency")

# Testing for Vulkan Lifecycle
if(TARGET anoptic_render)
    # Stage shaders/assets into this binary dir (vk tests resolve exe-relative, exes live here).
    add_custom_target(anoptic_test_resources ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_BIN_DIR} ${CMAKE_CURRENT_BINARY_DIR}/resources/shaders
            COMMENT "Staging shaders (and assets, if present) into tests/"
            VERBATIM)
    add_dependencies(anoptic_test_resources anoptic_shaders)
    if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
        add_custom_command(TARGET anoptic_test_resources POST_BUILD
                COMMAND ${CMAKE_COMMAND} -DANO_ASSETS_SRC=${CMAKE_SOURCE_DIR}/assets
                        -DANO_ASSETS_DST=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_SOURCE_DIR}/cmake/stage_assets.cmake
                VERBATIM)
    endif()

    # Render-side slot authority: pure logic, links anoptic_render but needs no GPU device.
    add_executable(anotest_render_slots anotest_render_slots.c)
    target_link_libraries(anotest_render_slots PRIVATE anoptic_render)
    add_test(NAME anoptic_render_slots COMMAND anotest_render_slots)
    set_tests_properties(anoptic_render_slots PROPERTIES TIMEOUT 10 LABELS "unit;render")

    # Global descriptor layout: reflected schema, fallback equivalence, and invalid-schema rejection.
    add_executable(anotest_vk_reflection anotest_vk_reflection.c)
    target_link_libraries(anotest_vk_reflection PRIVATE anoptic_render)
    add_test(NAME anoptic_vk_reflection COMMAND anotest_vk_reflection)
    set_tests_properties(anoptic_vk_reflection PROPERTIES TIMEOUT 10 LABELS "unit;render;reflection")

    add_executable(anotest_vk_lifecycle anotest_vk_lifecycle.c)
    target_link_libraries(anotest_vk_lifecycle PRIVATE anoptic_render)
    add_test(NAME anotest_vk_lifecycle COMMAND anotest_vk_lifecycle)
    # SKIP_RETURN_CODE 77: the device tests skip (not fail) where no Vulkan device can
    # run the renderer, e.g. lavapipe-only sandboxes (no multisampled integer color).
    set_tests_properties(anotest_vk_lifecycle PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        SKIP_RETURN_CODE 77)

    # Static shadow-caster region of the backend.h static-row contract: pure logic, no GPU device.
    add_executable(anotest_vk_shadow anotest_vk_shadow.c)
    target_link_libraries(anotest_vk_shadow PRIVATE anoptic_render)
    add_test(NAME anotest_vk_shadow COMMAND anotest_vk_shadow)
    set_tests_properties(anotest_vk_shadow PROPERTIES TIMEOUT 30 LABELS "unit;render")

    add_executable(anotest_vk_components anotest_vk_components.c)
    target_link_libraries(anotest_vk_components PRIVATE anoptic_render)
    add_test(NAME anotest_vk_components COMMAND anotest_vk_components)
    set_tests_properties(anotest_vk_components PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_compliance_layers anotest_vk_compliance_layers.c)
    target_link_libraries(anotest_vk_compliance_layers PRIVATE anoptic_render)
    add_test(NAME anotest_vk_compliance_layers COMMAND anotest_vk_compliance_layers)
    # Asserts validation layers catch an intentional error. Disabled in Release (layers Debug-only).
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
    # Asserts validation layers catch an intentional error. Disabled in Release (layers Debug-only).
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>" SKIP_RETURN_CODE 77)

    # Texture-custody guards: real TUs vs link-seam vk/helper stubs; EXISTS until sources land.

    # createTextureImage / createTextureImageFromPixels failure unwind + MUTABLE_FORMAT format list.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_texunwindguard.c)
        add_executable(anotest_texunwindguard anotest_texunwindguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/texture/texture.c)
        target_include_directories(anotest_texunwindguard PRIVATE ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(anotest_texunwindguard PRIVATE anoptic_core m)
        add_test(NAME anoptic_texunwindguard COMMAND anotest_texunwindguard)
        set_tests_properties(anoptic_texunwindguard PROPERTIES TIMEOUT 30 LABELS "unit;render")
    endif()

    # ano_vk_register_texture growth refusal; mi_realloc/mi_free renamed to test seams.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_texregisterguard.c)
        add_executable(anotest_texregisterguard anotest_texregisterguard.c ${CMAKE_SOURCE_DIR}/src/vulkan_backend/components.c)
        target_include_directories(anotest_texregisterguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
        target_compile_definitions(anotest_texregisterguard PRIVATE mi_realloc=anotest_seam_realloc mi_free=anotest_seam_free)
        target_link_libraries(anotest_texregisterguard PRIVATE anoptic_core m)
        add_test(NAME anoptic_texregisterguard COMMAND anotest_texregisterguard)
        set_tests_properties(anoptic_texregisterguard PROPERTIES TIMEOUT 30 LABELS "unit;render;mem")
    endif()

    # parseGltf texture custody: refuse construct, refuse adopt, haltLoad on device refusal.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_gltftexleakguard.c)
        add_executable(anotest_gltftexleakguard anotest_gltftexleakguard.c ${CMAKE_SOURCE_DIR}/src/render/gltf/ano_GltfParser.c)
        target_include_directories(anotest_gltftexleakguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(anotest_gltftexleakguard PRIVATE anoptic_core m)
        add_test(NAME anoptic_gltftexleakguard COMMAND anotest_gltftexleakguard)
        set_tests_properties(anoptic_gltftexleakguard PROPERTIES TIMEOUT 30 LABELS "unit;render")
    endif()

    # glTF colour/data domain split: dual views, distinct bindless slots, no cross-domain alias.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_gltftexdomainguard.c)
        add_executable(anotest_gltftexdomainguard anotest_gltftexdomainguard.c ${CMAKE_SOURCE_DIR}/src/render/gltf/ano_GltfParser.c)
        target_include_directories(anotest_gltftexdomainguard PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/glfw/include ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(anotest_gltftexdomainguard PRIVATE anoptic_core m)
        add_test(NAME anoptic_gltftexdomainguard COMMAND anotest_gltftexdomainguard)
        set_tests_properties(anoptic_gltftexdomainguard PROPERTIES TIMEOUT 30 LABELS "unit;render")
    endif()

    # Dual-view SRGB/UNORM sample readback via compute probe.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/anotest_vk_texdomain.c)
        add_executable(anotest_vk_texdomain anotest_vk_texdomain.c)
        target_link_libraries(anotest_vk_texdomain PRIVATE anoptic_render)
        add_test(NAME anotest_vk_texdomain COMMAND anotest_vk_texdomain)
        set_tests_properties(anotest_vk_texdomain PROPERTIES TIMEOUT 30 LABELS "unit;vulkan" SKIP_RETURN_CODE 77)
        add_dependencies(anotest_vk_texdomain anoptic_test_resources)
    endif()

    # A lone `--build --target anotest_vk_*` must still stage this directory's
    # shaders/assets before the test can run.
    foreach(vktest IN ITEMS anotest_render_slots anotest_vk_lifecycle anotest_vk_components
            anotest_vk_shadow anotest_vk_compliance_layers anotest_vk_memory anotest_vk_sync)
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

# Scale ctest TIMEOUTs under sanitizers. Tests without an explicit TIMEOUT keep the default.
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
