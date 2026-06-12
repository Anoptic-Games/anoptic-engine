cmake_minimum_required(VERSION 3.25)
project(anopticengine C)

# Debugging the Build System
message(STATUS "Toolchain file specified: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")

# Anoptic Engine Only Supports 64-bit Compilation
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(STATUS "64-bit compiler detected.")
else()
  message(FATAL_ERROR "64-bit compiler REQUIRED.")
endif()

# Enable C23 standard.
# CMAKE_C_EXTENSIONS is ON explicitly: the engine compiles as gnu23.
# POSIX usage (clock_gettime, clock_nanosleep, CLOCK_MONOTONIC) is declared
# explicitly via _POSIX_C_SOURCE below rather than inherited accidentally.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ANOPTIC_TESTS "Build the Anoptic test suite" OFF)

# Mimalloc submodule directory and definitions
set(MI_BUILD_OBJECT OFF)
set(MI_BUILD_SHARED OFF)
set(MI_BUILD_STATIC ON)
set(MI_OVERRIDE OFF)
set(MI_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/mimalloc)

# pthreads on Linux, winpthreads on MinGW
find_package(Threads REQUIRED)

# Add headers into targets so IDEs parse them
file(GLOB HEADER_SOURCES
        "./include/*.h"
)

# Doing the same for single-header external dependencies
file(GLOB EXTERNAL_SOURCES
        "./external/jsmn/*.h"
        "./external/stb/*.h"
)

# ---------------------------------------------------------------------------
# anoptic_core: every module that builds and tests without a GPU.
# Module subdirectories add their sources to this target.
# ---------------------------------------------------------------------------
add_library(anoptic_core STATIC ${HEADER_SOURCES} ${EXTERNAL_SOURCES})

target_include_directories(anoptic_core PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/jsmn
)

target_link_libraries(anoptic_core PUBLIC mimalloc-static Threads::Threads)

# clock_gettime / clock_nanosleep / CLOCK_MONOTONIC require a feature-test
# macro under strict ISO C; declare the POSIX dependency explicitly.
if(UNIX)
    target_compile_definitions(anoptic_core PUBLIC _POSIX_C_SOURCE=200809L)
endif()

target_compile_definitions(anoptic_core PUBLIC
        "$<$<CONFIG:Debug>:DEBUG_BUILD>"
        "$<$<CONFIG:Release>:RELEASE_BUILD>"
)

target_compile_options(anoptic_core PRIVATE
        "$<$<CONFIG:Debug>:-O0;-g>"
        "$<$<CONFIG:Release>:-O3>"
)

# Core module subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/src/memory)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/threads)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/time)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/strings)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/logging)

# ---------------------------------------------------------------------------
# anopticengine: the engine executable. Requires the Vulkan SDK and GLFW.
# Skipped (with a warning) when Vulkan is absent so that anoptic_core and
# the test suite remain buildable on headless/CI machines.
# ---------------------------------------------------------------------------
find_package(Vulkan)
if(Vulkan_FOUND)
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

    add_executable(anopticengine ./src/engine/main.c)

    # Define the project root path
    target_compile_definitions(anopticengine PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    # Renderer module subdirectories
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

    target_link_libraries(anopticengine PRIVATE anoptic_core glfw Vulkan::Vulkan)

    target_compile_options(anopticengine PRIVATE
            "$<$<CONFIG:Debug>:-O0;-g>"
            "$<$<CONFIG:Release>:-O3>"
    )
else()
    message(WARNING "Vulkan SDK not found: building anoptic_core and tests only.")
endif()

# CTest integration
if(ANOPTIC_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
