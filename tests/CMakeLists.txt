# Test executables link anoptic_core. Exit 0 = pass.

# Testing for ``anoptic_time.h``
add_executable(anotest_time anotest_time.c)
target_link_libraries(anotest_time PRIVATE anoptic_core)
add_test(NAME anoptic_time COMMAND anotest_time)
set_tests_properties(anoptic_time PROPERTIES TIMEOUT 30 LABELS "unit")

# Testing for ``anoptic_logging.h``
add_executable(anotest_logging anotest_logging.c)
target_link_libraries(anotest_logging PRIVATE anoptic_core)
add_test(NAME anoptic_logging COMMAND anotest_logging)
set_tests_properties(anoptic_logging PROPERTIES TIMEOUT 10 LABELS "unit;concurrency")

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

# Testing for the render_bridge transport (SPSC rings; concurrency)
add_executable(anotest_render_bridge anotest_render_bridge.c)
target_link_libraries(anotest_render_bridge PRIVATE anoptic_core)
add_test(NAME anoptic_render_bridge COMMAND anotest_render_bridge)
set_tests_properties(anoptic_render_bridge PROPERTIES TIMEOUT 60 LABELS "unit;concurrency")

# Testing for Vulkan Lifecycle
if(TARGET anoptic_render)
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
    set_tests_properties(anotest_vk_compliance_layers PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_memory anotest_vk_memory.c)
    target_link_libraries(anotest_vk_memory PRIVATE anoptic_render)
    add_test(NAME anotest_vk_memory COMMAND anotest_vk_memory)
    set_tests_properties(anotest_vk_memory PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")

    add_executable(anotest_vk_sync anotest_vk_sync.c)
    target_link_libraries(anotest_vk_sync PRIVATE anoptic_render)
    add_test(NAME anotest_vk_sync COMMAND anotest_vk_sync)
    set_tests_properties(anotest_vk_sync PROPERTIES TIMEOUT 30 LABELS "unit;vulkan")
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
