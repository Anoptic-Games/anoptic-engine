# Logger + crash blackbox (anoptic_log_crash.h).
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/log_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/log_crash.c)

# Crash platform. APPLE before UNIX (both true on macOS).
if (WIN32)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/log_crash_win64.c)
elseif (APPLE)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/log_crash_macos.c)
elseif (UNIX)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/log_crash_linux.c)
endif()

# log_core.c: -Wformat fatal.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/log_core.c
    TARGET_DIRECTORY anoptic_core
    PROPERTIES COMPILE_OPTIONS "-Wformat;-Wformat-nonliteral;-Werror")
