# Test executables link anoptic_core. Exit 0 = pass.

# TIMEOUT scale: asan x4, tsan x8. Sanitized binaries are slower; a red clock under TSan teaches everyone to ignore the clock.
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

# Resource tests: LABELS "resource" (`ctest -L resource`).

# Unit

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Testing for ``anoptic_log.h``
# Scratch dirs: anchored to test exe dir at runtime (no baked build path).
add_executable(anotest_logging anotest_logging.c)
target_link_libraries(anotest_logging PRIVATE anoptic_core)
add_test(NAME anoptic_logging COMMAND anotest_logging)
set_tests_properties(anoptic_logging PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;concurrency")

# Testing for ``anoptic_filesystem.h`` (paths, append-file). Userpath writes real probe under user root.
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

# Collation sort + transforms. Oracle: qsort(anostr_collate), naive rebuilds.
add_executable(anotest_strings_sort anotest_strings_sort.c)
target_link_libraries(anotest_strings_sort PRIVATE anoptic_core)
add_test(NAME anoptic_strings_sort COMMAND anotest_strings_sort)
set_tests_properties(anoptic_strings_sort PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit")

# anostr_t property fuzzer. argv[1] scales soak. Oracles: qsort(collate), naive rebuilds, FNV.
add_executable(anotest_strings_fuzz anotest_strings_fuzz.c)
target_link_libraries(anotest_strings_fuzz PRIVATE anoptic_core)
add_test(NAME anoptic_strings_fuzz COMMAND anotest_strings_fuzz)
set_tests_properties(anoptic_strings_fuzz PROPERTIES TIMEOUT ${ANO_T120} LABELS "fuzz;mem")

# Collation sort bench. DISABLED in ctest, run ./anotest_sortbench from -O3 (build.bat 7).
add_executable(anotest_sortbench anotest_sortbench.c)
target_link_libraries(anotest_sortbench PRIVATE anoptic_core)
add_test(NAME anoptic_sortbench COMMAND anotest_sortbench)
set_tests_properties(anoptic_sortbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# String-ops bench. DISABLED in ctest, run from -O3.
add_executable(anotest_stropsbench anotest_stropsbench.c)
target_link_libraries(anotest_stropsbench PRIVATE anoptic_core)
add_test(NAME anoptic_stropsbench COMMAND anotest_stropsbench)
set_tests_properties(anoptic_stropsbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger string stress bench. DISABLED in ctest, run from -O3.
add_executable(anotest_logstrbench anotest_logstrbench.c)
target_link_libraries(anotest_logstrbench PRIVATE anoptic_core)
add_test(NAME anoptic_logstrbench COMMAND anotest_logstrbench)
set_tests_properties(anoptic_logstrbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Testing for ``anoptic_text.h`` (FreeType, glyph bake). Fonts staged next to binary.
add_executable(anotest_text anotest_text.c)
target_link_libraries(anotest_text PRIVATE anoptic_core m)
add_custom_command(TARGET anotest_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts ${CMAKE_CURRENT_BINARY_DIR}/resources/fonts
        COMMENT "Staging fonts next to anotest_text"
        VERBATIM)
add_test(NAME anoptic_text COMMAND anotest_text)
set_tests_properties(anoptic_text PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit")

# Testing for ``anoptic_ui.h`` (prim ABI, builder, ref eval). Oracles: supersampled coverage + blur masks.
add_executable(anotest_ui anotest_ui.c)
target_link_libraries(anotest_ui PRIVATE anoptic_core m)
add_test(NAME anoptic_ui COMMAND anotest_ui)
set_tests_properties(anoptic_ui PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit")

# String compare bench. DISABLED in ctest, run ./anotest_strbench from -O3.
add_executable(anotest_strbench anotest_strbench.c)
target_link_libraries(anotest_strbench PRIVATE anoptic_core)
add_test(NAME anoptic_strbench COMMAND anotest_strbench)
set_tests_properties(anoptic_strbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# ANOSTR_SID dispatch bench. DISABLED in ctest, run ./anotest_sidbench from -O3.
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

# Testing for ``anoptic_resources.h`` (mounts, slurp, durable writes, save-frame). Real user-root probes.
add_executable(anotest_resources anotest_resources.c)
target_link_libraries(anotest_resources PRIVATE anoptic_core)
add_test(NAME anoptic_resources COMMAND anotest_resources)
set_tests_properties(anoptic_resources PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Testing for ``anoptic_res_graphics.h`` (glTF ingest, URI/base64, PNG round-trip). Real-asset smoke if assets/.
add_executable(anotest_resgfx anotest_resgfx.c)
target_link_libraries(anotest_resgfx PRIVATE anoptic_core)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_resgfx PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_resgfx COMMAND anotest_resgfx)
set_tests_properties(anoptic_resgfx PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Lifetime domains: engine/world/pin/reload/transfer/shutdown.
add_executable(anotest_resgroups anotest_resgroups.c)
target_include_directories(anotest_resgroups PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resgroups PRIVATE anoptic_core)
add_test(NAME anoptic_resgroups COMMAND anotest_resgroups)
set_tests_properties(anoptic_resgroups PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem;resource")

# Ownership: lock-free publish, pin/retire, row growth, wrap refusal.
add_executable(anotest_resownership anotest_resownership.c)
target_include_directories(anotest_resownership PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resownership PRIVATE anoptic_core)
add_test(NAME anoptic_resownership COMMAND anotest_resownership)
set_tests_properties(anoptic_resownership PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;concurrency;resource")

# Lock-free collections: layout, edges, conservation stress (8P×8C MPMC, 8P×1C MPSC, 1P×8C SPMC, SPSC). TSan gate food → concurrency label.
add_executable(anotest_collections anotest_collections.c)
target_link_libraries(anotest_collections PRIVATE anoptic_core)
add_test(NAME anoptic_collections COMMAND anotest_collections)
set_tests_properties(anoptic_collections PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;concurrency")

# Fault inject: private resources_core.c + ANO_RES_FAULT_INJECT (link-time override, log_old.c trick). Oracle: old-complete or new-complete, never torn.
add_executable(anotest_resfault anotest_resfault.c
        ${CMAKE_SOURCE_DIR}/src/resources/resources_core.c)
target_compile_definitions(anotest_resfault PRIVATE ANO_RES_FAULT_INJECT)
target_include_directories(anotest_resfault PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resfault PRIVATE anoptic_core)
add_test(NAME anoptic_resfault COMMAND anotest_resfault)
set_tests_properties(anoptic_resfault PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;resource")

# Persistence: schemas, migration/quarantine, slot concurrency. Oracle: commits = durable gens. Distinct slots concurrent; one slot totally ordered.
add_executable(anotest_persistence anotest_persistence.c)
target_include_directories(anotest_persistence PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_compile_definitions(anotest_persistence PRIVATE ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(anotest_persistence PRIVATE anoptic_core)
add_test(NAME anoptic_persistence COMMAND anotest_persistence)
set_tests_properties(anoptic_persistence PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;concurrency;resource")

# Durability: kill at each boundary. Private resources_core.c + ANO_RES_FAULT_INJECT. Oracle: old/new-complete.
add_executable(anotest_resdurability anotest_resdurability.c
        ${CMAKE_SOURCE_DIR}/src/resources/resources_core.c)
target_compile_definitions(anotest_resdurability PRIVATE
        ANO_RES_FAULT_INJECT ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(anotest_resdurability PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resdurability PRIVATE anoptic_core)
add_test(NAME anoptic_resdurability COMMAND anotest_resdurability)
set_tests_properties(anoptic_resdurability PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;resource")

# FREEZE
# Resource-manager test names/LABELS/TIMEOUT fixed here. Later workstreams do not retouch this CMakeLists. Stubs print PENDING (never fake OK).

# Codec: RAW/LZ4 round-trips, the reserved-codec refusal, hostile-bytes totality. (W4)
add_executable(anotest_rescodec anotest_rescodec.c)
target_include_directories(anotest_rescodec PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_rescodec PRIVATE anoptic_core)
add_test(NAME anoptic_rescodec COMMAND anotest_rescodec)
set_tests_properties(anoptic_rescodec PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;resource")

# Placement: routing oracle, every model x plan -> site. (W1)
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

# Block framing: hostile-input battery. (W5)
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

# Levels: open/run/baseline over 20 cycles. (W10)
add_executable(anotest_reslevel anotest_reslevel.c)
target_include_directories(anotest_reslevel PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_reslevel PRIVATE anoptic_core m)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_reslevel PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_reslevel COMMAND anotest_reslevel)
set_tests_properties(anoptic_reslevel PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;mem;resource")

# Remote-FS floor: 9P/SMB. (W12)
add_executable(anotest_resremote anotest_resremote.c)
target_include_directories(anotest_resremote PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resremote PRIVATE anoptic_core)
add_test(NAME anoptic_resremote COMMAND anotest_resremote)
set_tests_properties(anoptic_resremote PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;resource")

# Memory / threads / benches

# Testing for ``anoptic_memory_pools.h`` (monotonic/multipool/pool). Hostile input, churn fuzz, composition.
add_executable(anotest_mempools anotest_mempools.c)
target_link_libraries(anotest_mempools PRIVATE anoptic_core)
add_test(NAME anoptic_mempools COMMAND anotest_mempools)
set_tests_properties(anoptic_mempools PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;mem")

# Mempool bench. DISABLED in ctest, run ./anotest_mempoolbench from -O3 (build.sh 8).
add_executable(anotest_mempoolbench anotest_mempoolbench.c)
target_link_libraries(anotest_mempoolbench PRIVATE anoptic_core)
add_test(NAME anoptic_mempoolbench COMMAND anotest_mempoolbench)
set_tests_properties(anoptic_mempoolbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Thread bridges: park/wake soak, TRYFAIL/WAIT policy, anores_t exactly-once.
add_executable(anotest_bridges anotest_bridges.c)
target_link_libraries(anotest_bridges PRIVATE anoptic_core)
add_test(NAME anoptic_bridges COMMAND anotest_bridges)
set_tests_properties(anoptic_bridges PROPERTIES TIMEOUT ${ANO_T120} LABELS "unit;concurrency")

# Ring bench. DISABLED in ctest, run from -O3 (build.sh 8 / build.bat 8).
add_executable(anotest_ringbench anotest_ringbench.c)
target_link_libraries(anotest_ringbench PRIVATE anoptic_core)
add_test(NAME anoptic_ringbench COMMAND anotest_ringbench)
set_tests_properties(anoptic_ringbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Resource domain-cycle bench. DISABLED in ctest, run ./anotest_resbench from -O3.
add_executable(anotest_resbench anotest_resbench.c)
target_include_directories(anotest_resbench PRIVATE ${CMAKE_SOURCE_DIR}/src/resources)
target_link_libraries(anotest_resbench PRIVATE anoptic_core)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    target_compile_definitions(anotest_resbench PRIVATE ANO_TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets")
endif()
add_test(NAME anoptic_resbench COMMAND anotest_resbench)
set_tests_properties(anoptic_resbench PROPERTIES DISABLED TRUE LABELS "optional;bench;resource")

# Easter egg (see file banner). DISABLED in ctest, run ./anotest_chariots by hand. -w
add_executable(anotest_chariots anotest_chariots.c)
target_link_libraries(anotest_chariots PRIVATE anoptic_core)
target_compile_options(anotest_chariots PRIVATE -w)
add_test(NAME anoptic_chariots COMMAND anotest_chariots)
set_tests_properties(anoptic_chariots PROPERTIES DISABLED TRUE LABELS "optional;easteregg")

# Logger bench: lock-free vs mutex baseline (log_old.c private to this target). DISABLED in ctest, run ./anotest_logbench by hand.
add_executable(anotest_logbench anotest_logbench.c ${CMAKE_SOURCE_DIR}/src/log/log_old.c)
target_include_directories(anotest_logbench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anotest_logbench PRIVATE anoptic_core)
add_test(NAME anoptic_logbench COMMAND anotest_logbench)
set_tests_properties(anoptic_logbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger tail latency percentiles. DISABLED in ctest, run ./anotest_logtail by hand.
add_executable(anotest_logtail anotest_logtail.c)
target_link_libraries(anotest_logtail PRIVATE anoptic_core)
add_test(NAME anoptic_logtail COMMAND anotest_logtail)
set_tests_properties(anoptic_logtail PROPERTIES DISABLED TRUE LABELS "optional;bench")

# Logger fuzzer: multi-producer + flusher. Oracle: line count = enqueued. argv[1] scales soak.
add_executable(anotest_logfuzz anotest_logfuzz.c)
target_link_libraries(anotest_logfuzz PRIVATE anoptic_core)
add_test(NAME anoptic_logfuzz COMMAND anotest_logfuzz)
set_tests_properties(anoptic_logfuzz PROPERTIES TIMEOUT ${ANO_T120} LABELS "fuzz;concurrency")

# Testing for ``anoptic_log_crash.h`` (hooks, hail mary, deadman). Re-execs per scenario. Deadman waits 5 s.
add_executable(anotest_blackbox anotest_blackbox.c)
target_link_libraries(anotest_blackbox PRIVATE anoptic_core)
add_test(NAME anoptic_blackbox COMMAND anotest_blackbox)
set_tests_properties(anoptic_blackbox PROPERTIES TIMEOUT ${ANO_T240} LABELS "unit;concurrency")
# Skipped under ASan/TSan (sanitizer signal handlers).
if(NOT ANOPTIC_SANITIZE STREQUAL "")
    set_tests_properties(anoptic_blackbox PROPERTIES DISABLED TRUE)
endif()

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT ${ANO_T60} LABELS "unit;concurrency")

# Vulkan
if(TARGET anoptic_render)
    # Stage shaders/assets into tests/ (vk tests resolve exe-relative).
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

    # Render slot authority (no GPU).
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
    # Validation-layer intentional error. Debug-only.
    set_tests_properties(anotest_vk_compliance_layers PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>")

    add_executable(anotest_vk_memory anotest_vk_memory.c)
    target_link_libraries(anotest_vk_memory PRIVATE anoptic_render)
    add_test(NAME anotest_vk_memory COMMAND anotest_vk_memory)
    set_tests_properties(anotest_vk_memory PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan")

    add_executable(anotest_vk_sync anotest_vk_sync.c)
    target_link_libraries(anotest_vk_sync PRIVATE anoptic_render)
    add_test(NAME anotest_vk_sync COMMAND anotest_vk_sync)
    # Validation-layer intentional error. Debug-only.
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT ${ANO_T30} LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>")

    # Lone anotest_vk_* target still needs staged shaders/assets.
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
