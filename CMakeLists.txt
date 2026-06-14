cmake_minimum_required(VERSION 3.25)
project(anopticengine)

# Debugging the Build System
message(STATUS "Toolchain file specified: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

# Anoptic Engine Only Supports 64-bit Compilation
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(STATUS "64-bit compiler detected.")
else()
  message(FATAL_ERROR "64-bit compiler REQUIRED.")
endif()

# Enable C23 standard with GNU extensions (gnu23).
# _POSIX_C_SOURCE declared below for clock_gettime etc.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ANOPTIC_TESTS "Build the Anoptic test suite" OFF)
set(ANOPTIC_SANITIZE "" CACHE STRING "Sanitizer for test builds: asan, tsan, or empty")

# Sanitizer flags are global so mimalloc is instrumented too
if(ANOPTIC_SANITIZE STREQUAL "asan")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(ANOPTIC_SANITIZE STREQUAL "tsan")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(NOT ANOPTIC_SANITIZE STREQUAL "")
    message(FATAL_ERROR "ANOPTIC_SANITIZE must be 'asan', 'tsan', or empty (got '${ANOPTIC_SANITIZE}')")
endif()

# Mimalloc submodule directory and definitions
set(MI_BUILD_OBJECT OFF)
set(MI_BUILD_SHARED OFF)
set(MI_BUILD_STATIC ON)
set(MI_OVERRIDE OFF)
set(MI_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/mimalloc)

# pthreads everywhere: native on Linux, winpthreads on MinGW
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

# anoptic_core: every module that builds and tests without a GPU
add_library(anoptic_core STATIC ${HEADER_SOURCES} ${EXTERNAL_SOURCES})

target_include_directories(anoptic_core PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/jsmn
        ${CMAKE_SOURCE_DIR}/external/cgltf
)

target_link_libraries(anoptic_core PUBLIC mimalloc-static Threads::Threads)

# Explicit POSIX feature-test macro for clock_gettime etc.
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

# anopticengine executable (requires Vulkan + GLFW, skipped if absent)
find_package(Vulkan)
if(Vulkan_FOUND)
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

    # anoptic_render library
    add_library(anoptic_render STATIC "")
    target_link_libraries(anoptic_render PUBLIC anoptic_core glfw Vulkan::Vulkan)
    target_compile_definitions(anoptic_render PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    # Renderer module subdirectories
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

    add_executable(anopticengine ./src/engine/main.c)

    # Define the project root path
    target_compile_definitions(anopticengine PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    target_link_libraries(anopticengine PRIVATE anoptic_render)

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
