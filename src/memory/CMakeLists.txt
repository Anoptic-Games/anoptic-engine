# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ./memalign_win64.c
    )
endif()

if (UNIX)
    target_sources(anopticengine PRIVATE
            ./memalign_linux.c
    )
endif()