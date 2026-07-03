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
add_subdirectory(${CMAKE_SOURCE_DIR}/src/filesystem)
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
        # Shared GLSL includes (#include): not compiled standalone, but every shader rebuilds when one
        # changes so the shadow CDF math can't silently desync from a stale .spv.
        set(SHADER_INCLUDES ${SHADER_DIR}/shadow_cdf.glsl ${SHADER_DIR}/shadow_sample.glsl)
        foreach(shader IN ITEMS flat.mesh flat.vert flat.frag flat.task transmission.frag additive.frag cull.comp tpsort.comp update.comp scatter.comp lightcull.comp lightsetup.comp shadowsetup.comp hiz.comp shadow_depth.frag shadowblur.vert shadowblur.frag tonemap.vert tonemap.frag)
            add_custom_command(
                    OUTPUT  ${SHADER_DIR}/${shader}.spv
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${SHADER_DIR}/${shader} -o ${SHADER_DIR}/${shader}.spv
                    DEPENDS ${SHADER_DIR}/${shader} ${SHADER_INCLUDES}
                    COMMENT "glslc ${shader} -> ${shader}.spv"
                    VERBATIM)
            list(APPEND SPV_OUTPUTS ${SHADER_DIR}/${shader}.spv)
        endforeach()
        # Depth-only geometry variants: second compiles of flat.mesh / flat.vert with ANO_DEPTH_ONLY
        # (gl_Position only, no user attributes) for the depth pre-pass and the shadow atlas render.
        # Same source as the full modules + invariant gl_Position = bit-identical clip depth, which
        # the opaque pass's EQUAL test requires.
        foreach(dshader IN ITEMS flat.mesh flat.vert)
            string(REPLACE "flat." "flat_depth." dspv ${dshader})
            add_custom_command(
                    OUTPUT  ${SHADER_DIR}/${dspv}.spv
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 -DANO_DEPTH_ONLY ${SHADER_DIR}/${dshader} -o ${SHADER_DIR}/${dspv}.spv
                    DEPENDS ${SHADER_DIR}/${dshader} ${SHADER_INCLUDES}
                    COMMENT "glslc ${dshader} (ANO_DEPTH_ONLY) -> ${dspv}.spv"
                    VERBATIM)
            list(APPEND SPV_OUTPUTS ${SHADER_DIR}/${dspv}.spv)
        endforeach()
        # Task-launched mesh variants (review priority 10): ANO_TASK_CULL compiles of flat.mesh (color
        # + depth-only) that read the meshlet index from flat.task's payload instead of gl_WorkGroupID.
        # Loaded when RendererState.taskCull; the plain compiles above stay the direct-dispatch fallback.
        foreach(tshader IN ITEMS flat.mesh flat_depth.mesh)
            string(REPLACE ".mesh" "_task.mesh" tspv ${tshader})
            set(tdefs -DANO_TASK_CULL)
            if(tshader STREQUAL "flat_depth.mesh")
                set(tdefs -DANO_TASK_CULL -DANO_DEPTH_ONLY)
            endif()
            add_custom_command(
                    OUTPUT  ${SHADER_DIR}/${tspv}.spv
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${tdefs} ${SHADER_DIR}/flat.mesh -o ${SHADER_DIR}/${tspv}.spv
                    DEPENDS ${SHADER_DIR}/flat.mesh ${SHADER_INCLUDES}
                    COMMENT "glslc flat.mesh (${tdefs}) -> ${tspv}.spv"
                    VERBATIM)
            list(APPEND SPV_OUTPUTS ${SHADER_DIR}/${tspv}.spv)
        endforeach()
        # Hi-Z avenue 1: a second compile of hiz.comp with RESOLVED_DEPTH (reduce reads a single-sample
        # sampler2D of the MAX-resolved depth). Loaded for the reduce pipeline only when the device
        # supports VK_RESOLVE_MODE_MAX_BIT; the default hiz.comp.spv stays the per-sample MSAA fallback.
        add_custom_command(
                OUTPUT  ${SHADER_DIR}/hiz_resolve.comp.spv
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 -DRESOLVED_DEPTH ${SHADER_DIR}/hiz.comp -o ${SHADER_DIR}/hiz_resolve.comp.spv
                DEPENDS ${SHADER_DIR}/hiz.comp ${SHADER_INCLUDES}
                COMMENT "glslc hiz.comp (RESOLVED_DEPTH) -> hiz_resolve.comp.spv"
                VERBATIM)
        list(APPEND SPV_OUTPUTS ${SHADER_DIR}/hiz_resolve.comp.spv)
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
endif()

# CTest integration
if(ANOPTIC_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
