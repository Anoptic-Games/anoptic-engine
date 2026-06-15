# Always compile the common source file
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/threads.c)

# macOS libpthread lacks spinlocks & barriers — supply them
if (APPLE)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/threads_macos.c)
endif()