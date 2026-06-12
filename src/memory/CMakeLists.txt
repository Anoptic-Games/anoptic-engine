# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/memalign_win64.c
    )
elseif (UNIX)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/memalign_linux.c
    )
endif()

# Universally compiled source files
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/memory.c
)