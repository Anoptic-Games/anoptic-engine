# Always compile the common source file
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/time.c)

# Platform source. APPLE before UNIX (both true on macOS).
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_win64.c
    )
    # MSVC < 17.5: /experimental:c11atomics for <stdatomic.h>
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
