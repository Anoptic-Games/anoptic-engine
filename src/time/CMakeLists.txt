# Always compile the common source file
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/time.c)

# Conditionally compile platform-specific source files.
# APPLE before UNIX: both are true on macOS, and we want the Darwin file there.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_win64.c
    )
    # MSVC ships native <stdatomic.h> only from VS2022 17.5 (MSVC_VERSION 1935) under /std:clatest;
    # older MSVC needs the experimental flag. MinGW/Clang are unaffected.
    if (MSVC AND MSVC_VERSION LESS 1935)
        target_compile_options(anoptic_core PRIVATE /experimental:c11atomics)
    endif()
elseif (APPLE)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_macos.c
    )
elseif (UNIX)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_linux.c
    )
endif()
