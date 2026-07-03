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

# Single-config generators default to an EMPTY build type, which is neither Debug nor
# Release: no DEBUG_BUILD/RELEASE_BUILD define, no optimization flags, validation layers
# compiled out while the layer-asserting vk tests stay enabled. Pick Release explicitly
# so a bare `cmake -S . -B build` is a real configuration.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No CMAKE_BUILD_TYPE given; defaulting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
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

# Mimalloc submodule directory and definitions.
# EXCLUDE_FROM_ALL also suppresses mimalloc's unconditional install() rules, which would
# otherwise ship vendor headers/libs alongside (previously: instead of) the engine.
set(MI_BUILD_OBJECT OFF)
set(MI_BUILD_SHARED OFF)
set(MI_BUILD_STATIC ON)
set(MI_OVERRIDE OFF)
set(MI_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/mimalloc EXCLUDE_FROM_ALL)

# pthreads everywhere: native on Linux, winpthreads on MinGW
find_package(Threads REQUIRED)

# Add headers into targets so IDEs parse them
file(GLOB HEADER_SOURCES CONFIGURE_DEPENDS
        "./include/*.h"
)

# Doing the same for single-header external dependencies
file(GLOB EXTERNAL_SOURCES CONFIGURE_DEPENDS
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
    # GLFW as a vendored subproject: never generate its install/docs targets, or
    # `cmake --install` ships GLFW headers/libs alongside the engine.
    set(GLFW_INSTALL OFF)
    set(GLFW_BUILD_DOCS OFF)
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw EXCLUDE_FROM_ALL)

    # anoptic_render library
    add_library(anoptic_render STATIC "")
    target_link_libraries(anoptic_render PUBLIC anoptic_core glfw Vulkan::Vulkan)

    # Renderer module subdirectories
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

    # Shaders: compile at build time so .spv don't silently desync from source; if glslc
    # is absent, fall back to the committed .spv (which may be stale). Outputs land in
    # the BUILD tree (<build>/resources/shaders, i.e. next to the engine binary, where
    # loadFile resolves them exe-relative) -- never in the source tree: source-tree
    # outputs get deleted by `clean`, race across sibling build dirs, and fail outright
    # on read-only checkouts (nix). Refresh the committed fallbacks explicitly with
    # resources/shaders/compile.sh when shader source changes.
    set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/resources/shaders)
    set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/resources/shaders)
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(WARNING "glslc not found: staging committed resources/shaders/*.spv (may be stale).")
    endif()
    set(SPV_OUTPUTS "")
    # Shared GLSL includes (#include): not compiled standalone, but every shader rebuilds when one
    # changes so the shadow CDF math can't silently desync from a stale .spv.
    set(SHADER_INCLUDES ${SHADER_SRC_DIR}/shadow_cdf.glsl ${SHADER_SRC_DIR}/shadow_sample.glsl)

    # Compile <source> into ${SHADER_BIN_DIR}/<output>.spv with any extra glslc defines
    # in ARGN; without glslc, stage the committed <output>.spv instead.
    function(ano_add_shader output source)
        if(Vulkan_GLSLC_EXECUTABLE)
            add_custom_command(
                    OUTPUT  ${SHADER_BIN_DIR}/${output}.spv
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${ARGN} ${SHADER_SRC_DIR}/${source} -o ${SHADER_BIN_DIR}/${output}.spv
                    DEPENDS ${SHADER_SRC_DIR}/${source} ${SHADER_INCLUDES}
                    COMMENT "glslc ${source} ${ARGN} -> ${output}.spv"
                    VERBATIM)
        else()
            add_custom_command(
                    OUTPUT  ${SHADER_BIN_DIR}/${output}.spv
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_SRC_DIR}/${output}.spv ${SHADER_BIN_DIR}/${output}.spv
                    DEPENDS ${SHADER_SRC_DIR}/${output}.spv
                    COMMENT "staging committed ${output}.spv (no glslc)"
                    VERBATIM)
        endif()
        set(SPV_OUTPUTS ${SPV_OUTPUTS} ${SHADER_BIN_DIR}/${output}.spv PARENT_SCOPE)
    endfunction()

    foreach(shader IN ITEMS flat.mesh flat.vert flat.frag flat.task transmission.frag additive.frag cull.comp tpsort.comp update.comp scatter.comp lightcull.comp lightsetup.comp shadowsetup.comp hiz.comp shadow_depth.frag shadowblur.vert shadowblur.frag tonemap.vert tonemap.frag)
        ano_add_shader(${shader} ${shader})
    endforeach()
    # Depth-only geometry variants: second compiles of flat.mesh / flat.vert with ANO_DEPTH_ONLY
    # (gl_Position only, no user attributes) for the depth pre-pass and the shadow atlas render.
    # Same source as the full modules + invariant gl_Position = bit-identical clip depth, which
    # the opaque pass's EQUAL test requires.
    ano_add_shader(flat_depth.mesh flat.mesh -DANO_DEPTH_ONLY)
    ano_add_shader(flat_depth.vert flat.vert -DANO_DEPTH_ONLY)
    # Task-launched mesh variants (review priority 10): ANO_TASK_CULL compiles of flat.mesh (color
    # + depth-only) that read the meshlet index from flat.task's payload instead of gl_WorkGroupID.
    # Loaded when RendererState.taskCull; the plain compiles above stay the direct-dispatch fallback.
    ano_add_shader(flat_task.mesh flat.mesh -DANO_TASK_CULL)
    ano_add_shader(flat_depth_task.mesh flat.mesh -DANO_TASK_CULL -DANO_DEPTH_ONLY)
    # Hi-Z avenue 1: a second compile of hiz.comp with RESOLVED_DEPTH (reduce reads a single-sample
    # sampler2D of the MAX-resolved depth). Loaded for the reduce pipeline only when the device
    # supports VK_RESOLVE_MODE_MAX_BIT; the default hiz.comp.spv stays the per-sample MSAA fallback.
    ano_add_shader(hiz_resolve.comp hiz.comp -DRESOLVED_DEPTH)

    add_custom_target(anoptic_shaders DEPENDS ${SPV_OUTPUTS})
    # Everything that links anoptic_render loads these at runtime (engine AND the vk
    # tests), so the library itself carries the dependency edge -- a lone
    # `--build --target anotest_vk_lifecycle` can no longer run against stale shaders.
    add_dependencies(anoptic_render anoptic_shaders)
elseif(NOT ANOPTIC_HEADLESS)
    message(WARNING "Vulkan SDK not found: building anoptic_core + tests only (use -DANOPTIC_HEADLESS=ON for a console engine).")
endif()

# anopticengine: headless links anoptic_core (main.c's HEADLESS_BUILD path);
# a renderer build links anoptic_render. Neither => no runnable engine, so it
# isn't built. Tests don't touch main.c and are unaffected either way.
if(ANOPTIC_HEADLESS OR Vulkan_FOUND)
    add_executable(anopticengine ./src/engine/main.c)
    target_compile_options(anopticengine PRIVATE
            "$<$<CONFIG:Debug>:-O0;-g>"
            "$<$<CONFIG:Release>:-O3>")
    if(Vulkan_FOUND)
        target_link_libraries(anopticengine PRIVATE anoptic_render)

        # Stage test assets (glTF/textures; gitignored, dev-machine-only) next to the
        # binary so the CWD-relative model loads work from any build flow -- previously
        # only build.sh/build.bat copied these, so a direct cmake build had none.
        if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
            add_custom_command(TARGET anopticengine POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:anopticengine>
                    COMMENT "Staging assets/ next to anopticengine"
                    VERBATIM)
        endif()
    else()
        target_link_libraries(anopticengine PRIVATE anoptic_core)
    endif()

    # Install story: the exe with resources/ next to it. loadFile resolves shaders
    # relative to the executable, so bin/ + bin/resources/shaders is self-contained.
    # (Test assets are gitignored and deliberately NOT installed.)
    install(TARGETS anopticengine RUNTIME DESTINATION bin)
    if(Vulkan_FOUND)
        install(DIRECTORY ${SHADER_BIN_DIR} DESTINATION bin/resources)
    endif()
endif()

# MinGW cross builds: stage the toolchain's runtime DLLs (mcfgthread, winpthreads)
# next to the binaries -- the Windows host cannot resolve them from the nix store, and
# a missing DLL fails SILENTLY over WSL interop. The Nix .#windows shell exports the
# search dirs (semicolon-separated = a CMake list); the MSYS2 path never sets the
# variable and finds its DLLs on PATH (clang64\bin) instead.
if(WIN32 AND CMAKE_CROSSCOMPILING AND DEFINED ENV{ANOPTIC_MINGW_DLL_DIRS})
    set(ANO_DLL_DIRS "$ENV{ANOPTIC_MINGW_DLL_DIRS}")
    foreach(dlldir IN LISTS ANO_DLL_DIRS)
        file(GLOB ANO_RUNTIME_DLLS ${dlldir}/*.dll)
        file(COPY ${ANO_RUNTIME_DLLS} DESTINATION ${CMAKE_BINARY_DIR})
        if(ANOPTIC_TESTS)
            file(COPY ${ANO_RUNTIME_DLLS} DESTINATION ${CMAKE_BINARY_DIR}/tests)
        endif()
    endforeach()
endif()

# CTest integration
if(ANOPTIC_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
