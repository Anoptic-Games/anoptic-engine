# Resource manager: platform-free core + one OS TU per platform.
# APPLE before UNIX: both are true on macOS, and the POSIX TU covers both anyway.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/resources_win64.c
    )
else ()
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/resources_posix.c
    )
endif ()

# Universally compiled source files
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_registry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/res_graphics.c
)
