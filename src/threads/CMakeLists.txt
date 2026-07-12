# Always compile the common source files (bridges is platform-free: C23 atomics +
# the anothread_* layer only)
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/threads.c
        ${CMAKE_CURRENT_SOURCE_DIR}/threads_bridges.c
)

# macOS libpthread lacks spinlocks & barriers — supply them
if (APPLE)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/threads_macos.c)
endif()