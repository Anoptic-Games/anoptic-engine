# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
            ../memory/win_memalign.c
            ../threads/win_threads.c
            ../time/win_time.c
    )
endif()

if (UNIX)
    target_sources(anopticengine PRIVATE
            ../memory/linux_memalign.c
            ../threads/linux_threads.c
            ../time/linux_time.c
    )
endif()