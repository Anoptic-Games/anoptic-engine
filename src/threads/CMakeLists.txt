# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ./threads_win64.c
    )
endif()

if (UNIX)
    target_sources(anopticengine PRIVATE
            ./threads_linux.c
    )
endif()