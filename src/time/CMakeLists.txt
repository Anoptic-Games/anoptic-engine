# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ./time_win64.c
    )
endif()

if (UNIX)
    target_sources(anopticengine PRIVATE
            ./time_linux.c
    )
endif()