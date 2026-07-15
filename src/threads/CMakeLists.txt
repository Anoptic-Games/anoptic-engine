# Common sources (bridges: platform-free)
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/threads.c
        ${CMAKE_CURRENT_SOURCE_DIR}/threads_bridges.c
)

# Darwin: spinlock/barrier gap-fill
if (APPLE)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/threads_macos.c)
endif()
