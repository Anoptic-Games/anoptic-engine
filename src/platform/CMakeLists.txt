# Conditionally compile platform-specific source files
if (WIN32)
    target_sources(anopticengine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/win_memalign.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/win_threads.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/win_time.c
    )
endif()

if (UNIX)
    target_sources(anopticengine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/linux_memalign.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/linux_threads.c
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/linux_time.c
    )
endif()