# Platform-agnostic common TU (session stamp, log directory).
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.c
)

# Conditionally compile platform-specific source files.
# APPLE before UNIX: both are true on macOS, and macOS needs the Mach-O lookup.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_win64.c
    )
elseif (APPLE)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_macos.c
    )
elseif (UNIX)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_linux.c
    )
endif()