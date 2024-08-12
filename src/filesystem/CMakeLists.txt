# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_win64.c
    )
elseif (UNIX)
    target_sources(anopticengine PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_linux.c
    )
endif()