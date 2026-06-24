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
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Emit compile_commands.json so language servers (clangd) get the real per-file
# include paths, -std=c23, and defines instead of falling back to bare defaults.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANOPTIC_TESTS "Build the Anoptic test suite" OFF)
option(ANOPTIC_HEADLESS "Build without the Vulkan renderer" OFF)
set(ANOPTIC_SANITIZE "" CACHE STRING "Sanitizer for test builds: asan, tsan, or empty")

# Sanitizer flags
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
        "$<$<BOOL:${ANOPTIC_HEADLESS}>:HEADLESS_BUILD>"
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
add_subdirectory(${CMAKE_SOURCE_DIR}/src/mesh)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/render_bridge)

# Renderer (default on).
if(NOT ANOPTIC_HEADLESS)
    find_package(Vulkan)
endif()

if(Vulkan_FOUND)
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

    # anoptic_render library
    add_library(anoptic_render STATIC "")
    target_link_libraries(anoptic_render PUBLIC anoptic_core glfw Vulkan::Vulkan)
    target_compile_definitions(anoptic_render PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    # Renderer module subdirectories
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

    # Shaders: compile at build time so .spv don't silently desync from source.
    # If glslc is absent, fallback to existing .spv (which may be stale).
    if(Vulkan_GLSLC_EXECUTABLE)
        set(SHADER_DIR ${CMAKE_SOURCE_DIR}/resources/shaders)
        set(SPV_OUTPUTS "")
        foreach(shader IN ITEMS flat.mesh flat.vert flat.frag transmission.frag cull.comp update.comp scatter.comp lightcull.comp shadowsetup.comp shadow_depth.frag tonemap.vert tonemap.frag voxelize.frag rc_trace.comp rc_merge.comp rc_integrate.comp)
            add_custom_command(
                    OUTPUT  ${SHADER_DIR}/${shader}.spv
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${SHADER_DIR}/${shader} -o ${SHADER_DIR}/${shader}.spv
                    DEPENDS ${SHADER_DIR}/${shader}
                    COMMENT "glslc ${shader} -> ${shader}.spv"
                    VERBATIM)
            list(APPEND SPV_OUTPUTS ${SHADER_DIR}/${shader}.spv)
        endforeach()
        add_custom_target(anoptic_shaders DEPENDS ${SPV_OUTPUTS})
    else()
        message(WARNING "glslc not found: using committed resources/shaders/*.spv (may be stale).")
    endif()
elseif(NOT ANOPTIC_HEADLESS)
    message(WARNING "Vulkan SDK not found: building anoptic_core + tests only (use -DANOPTIC_HEADLESS=ON for a console engine).")
endif()

# anopticengine: headless links anoptic_core (main.c's HEADLESS_BUILD path);
# a renderer build links anoptic_render. Neither => no runnable engine, so it
# isn't built. Tests don't touch main.c and are unaffected either way.
if(ANOPTIC_HEADLESS OR Vulkan_FOUND)
    add_executable(anopticengine ./src/engine/main.c)
    target_compile_definitions(anopticengine PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(anopticengine PRIVATE
            "$<$<CONFIG:Debug>:-O0;-g>"
            "$<$<CONFIG:Release>:-O3>")
    if(Vulkan_FOUND)
        target_link_libraries(anopticengine PRIVATE anoptic_render)
        if(TARGET anoptic_shaders)
            add_dependencies(anopticengine anoptic_shaders)
        endif()
    else()
        target_link_libraries(anopticengine PRIVATE anoptic_core)
    endif()
    # Platform filesystem helpers (executable-dir resolution) compiled into the engine.
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/filesystem)
endif()

# CTest integration
if(ANOPTIC_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
