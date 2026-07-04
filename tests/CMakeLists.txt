# Test executables link anoptic_core. Exit 0 = pass.

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_logging.h``
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

# Testing for ``anoptic_text.h`` (FreeType lifecycle; glyph-curve bake: stream grammar,
# monotonicity, winding, and the FONT_RENDER.md audit-count oracle). Geist is staged next
# to the binary; the test chdirs to its exe dir (gamepath) and loads it by relative path.
add_executable(anotest_text anotest_text.c)
target_link_libraries(anotest_text PRIVATE anoptic_core m)
add_custom_command(TARGET anotest_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts/Geist ${CMAKE_CURRENT_BINARY_DIR}/resources/fonts/Geist
        COMMENT "Staging Geist next to anotest_text"
        VERBATIM)
add_test(NAME anoptic_text COMMAND anotest_text)
set_tests_properties(anoptic_text PROPERTIES TIMEOUT 30 LABELS "unit")

# String compare microbenchmark: anostr_compare (German-string prefix path) vs naive memcmp.
# Built so it cannot rot, DISABLED in ctest -- run ./anotest_strbench by hand from a -O3 build.
add_executable(anotest_strbench anotest_strbench.c)
target_link_libraries(anotest_strbench PRIVATE anoptic_core)
add_test(NAME anoptic_strbench COMMAND anotest_strbench)
set_tests_properties(anoptic_strbench PROPERTIES DISABLED TRUE LABELS "optional;bench")

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

# Optional logger benchmark: the lock-free ring (in anoptic_core) vs the preserved mutex baseline.
# logging_old.c is compiled ONLY into this target -- it never enters anoptic_core or the standard
# build, so the two loggers coexist in one executable (the baseline is namespaced mtxlog_*). The
# src include dir resolves "logging/logging_old.h". Built so it cannot rot, DISABLED in ctest --
# run ./anotest_logbench by hand.
add_executable(anotest_logbench anotest_logbench.c ${CMAKE_SOURCE_DIR}/src/logging/logging_old.c)
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

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

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

    add_executable(anotest_vk_lifecycle anotest_vk_lifecycle.c)
    target_link_libraries(anotest_vk_lifecycle PRIVATE anoptic_render)
    add_test(NAME anotest_vk_lifecycle COMMAND anotest_vk_lifecycle)
    set_tests_properties(anotest_vk_lifecycle PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_components anotest_vk_components.c)
    target_link_libraries(anotest_vk_components PRIVATE anoptic_render)
    add_test(NAME anotest_vk_components COMMAND anotest_vk_components)
    set_tests_properties(anotest_vk_components PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_compliance_layers anotest_vk_compliance_layers.c)
    target_link_libraries(anotest_vk_compliance_layers PRIVATE anoptic_render)
    add_test(NAME anotest_vk_compliance_layers COMMAND anotest_vk_compliance_layers)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_compliance_layers PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
        DISABLED "$<CONFIG:Release>")

    add_executable(anotest_vk_memory anotest_vk_memory.c)
    target_link_libraries(anotest_vk_memory PRIVATE anoptic_render)
    add_test(NAME anotest_vk_memory COMMAND anotest_vk_memory)
    set_tests_properties(anotest_vk_memory PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_sync anotest_vk_sync.c)
    target_link_libraries(anotest_vk_sync PRIVATE anoptic_render)
    add_test(NAME anotest_vk_sync COMMAND anotest_vk_sync)
    # Asserts the validation layers catch an intentional error; layers are Debug-only, so disable in Release.
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT 30 LABELS "unit;vulkan"
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
