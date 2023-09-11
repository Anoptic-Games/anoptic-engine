# Conditionally compile platform-specific source files
if (WIN32)
    #TODO: Look into making the platform-layer stuff a static library  
    #if(MSVC)
    #    target_compile_options(win_platform PRIVATE /W4 /WX)
    #else()
    #    target_compile_options(win_platform PRIVATE -Wall -Wextra -Wpedantic)
    #endif()

    add_executable(anopticengine
    ./windows/win_memory.c
    ./windows/win_time.c
    )
endif()

if (UNIX)
    add_executable(anopticengine
    ./linux/linux_memory.c
    ./linux/linux_time.c
    )
endif()