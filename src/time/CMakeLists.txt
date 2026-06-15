# Always compile the common source file
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/time.c)

# Conditionally compile platform-specific source files.
# APPLE before UNIX: both are true on macOS, and we want the Darwin file there.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_win64.c
    )
elseif (APPLE)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_macos.c
    )
elseif (UNIX)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_linux.c
    )
endif()
