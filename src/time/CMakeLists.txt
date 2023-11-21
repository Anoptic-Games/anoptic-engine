# Always compile the common source file
target_sources(anopticengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/time.c)

# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_win64.c
    )
elseif (UNIX)
    target_sources(anopticengine PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/time_linux.c
    )
endif()
