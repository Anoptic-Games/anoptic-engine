# Conditionally compile platform-specific source files.
# APPLE before UNIX: both true on macOS.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/memalign_win64.c
    )
elseif (APPLE)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/memalign_macos.c
    )
elseif (UNIX)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/memalign_linux.c
    )
endif()

# Common sources
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/memory.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pools.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stripe.c
<<<<<<< HEAD
)
=======
)
>>>>>>> block-b1-base
