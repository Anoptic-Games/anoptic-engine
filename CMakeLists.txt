cmake_minimum_required(VERSION 3.29)
project(anopticengine LANGUAGES C CXX)

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

# Default to Release when no build type is given.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No CMAKE_BUILD_TYPE given; defaulting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Enable C23 standard with GNU extensions (gnu23).
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Emit compile_commands.json for clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build policy: whole builds, ThinLTO Release, modern linkers, static linking.

# Keep diagnostics colored under Ninja.
set(CMAKE_COLOR_DIAGNOSTICS ON)

# Linker selection: clang -> lld, gcc on ELF -> mold, MinGW gcc -> GNU ld, Apple -> ld64.
if(NOT CMAKE_LINKER_TYPE AND NOT APPLE)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        find_program(ANO_LLD_EXECUTABLE NAMES ld.lld lld)
        if(ANO_LLD_EXECUTABLE)
            set(CMAKE_LINKER_TYPE LLD)
        endif()
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT WIN32)
        find_program(ANO_MOLD_EXECUTABLE mold)
        if(ANO_MOLD_EXECUTABLE)
            set(CMAKE_LINKER_TYPE MOLD)
        endif()
    endif()
endif()

# ANOSTR_SID's unrolled FNV expansion nests brackets past clang's default 256 cap
# (gcc/MSVC have no such limit); any TU using compile-time string ids needs this.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fbracket-depth=1024)
endif()

# Release LTO: ThinLTO on clang, fat -flto on gcc.
include(CheckIPOSupported)
check_ipo_supported(RESULT ANO_IPO_SUPPORTED OUTPUT ANO_IPO_DETAIL LANGUAGES C CXX)
if(ANO_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "Release link-time optimization: ON")
else()
    message(WARNING "Release link-time optimization unsupported here: ${ANO_IPO_DETAIL}")
endif()

# Static linking on MinGW, only vulkan-1.dll stays dynamic.
if(WIN32 AND NOT MSVC)
    add_link_options(-static)
endif()

# ano_scrub: delete every object file.
add_custom_target(ano_scrub
    COMMAND ${CMAKE_COMMAND} -DANO_SCRUB_DIR=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/scrub_objects.cmake
    COMMENT "Scrubbing object files (whole rebuild ahead)"
    VERBATIM)

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
set(MI_BUILD_OBJECT OFF)
set(MI_BUILD_SHARED OFF)
set(MI_BUILD_STATIC ON)
set(MI_OVERRIDE OFF)
set(MI_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/mimalloc EXCLUDE_FROM_ALL)

# FreeType submodule with optional codecs and HarfBuzz disabled.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/freetype EXCLUDE_FROM_ALL)

# pthreads everywhere: native on Linux, winpthreads on MinGW
find_package(Threads REQUIRED)

# Headers into targets for IDE parsing.
file(GLOB HEADER_SOURCES CONFIGURE_DEPENDS
        "./include/*.h"
)

# Same for single-header external dependencies.
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
# PRIVATE: FreeType types stay out of public headers.
target_link_libraries(anoptic_core PRIVATE freetype)

# Explicit POSIX feature-test macro for clock_gettime etc.
if(UNIX)
    target_compile_definitions(anoptic_core PUBLIC _POSIX_C_SOURCE=200809L)
endif()

target_compile_definitions(anoptic_core PUBLIC
        "$<$<CONFIG:Debug>:DEBUG_BUILD>"
        "$<$<CONFIG:Release>:RELEASE_BUILD>"
        "$<$<BOOL:${ANOPTIC_HEADLESS}>:HEADLESS_BUILD>"
)

# Core module subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/src/memory)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/threads)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/time)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/strings)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/filesystem)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/logging)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/mesh)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/text)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/ui)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/render_bridge)

# Renderer (default on).
if(NOT ANOPTIC_HEADLESS)
    find_package(Vulkan)
endif()

if(Vulkan_FOUND)
    # Vendored GLFW without its install/docs targets.
    set(GLFW_INSTALL OFF)
    set(GLFW_BUILD_DOCS OFF)
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw EXCLUDE_FROM_ALL)

    # anoptic_render library
    add_library(anoptic_render STATIC "")
    target_link_libraries(anoptic_render PUBLIC anoptic_core glfw Vulkan::Vulkan)

    # Renderer module subdirectories
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
    add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

    # Shaders compile to SPIR-V in the build tree, committed .spv are the no-compiler fallback.
    set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/resources/shaders)
    set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/resources/shaders)

    # Shader compiler by build type: Debug embeds SPIR-V debug info, Release strips it.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
        set(SHADER_COMPILER ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
        set(SHADER_COMPILE_ARGS -V --target-env vulkan1.2 -gVS)
        set(SHADER_COMPILE_TAG "glslangValidator -gVS")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" AND Vulkan_GLSLC_EXECUTABLE)
        set(SHADER_COMPILER ${Vulkan_GLSLC_EXECUTABLE})
        set(SHADER_COMPILE_ARGS --target-env=vulkan1.2 -g)
        set(SHADER_COMPILE_TAG "glslc -g")
    elseif(Vulkan_GLSLC_EXECUTABLE)
        set(SHADER_COMPILER ${Vulkan_GLSLC_EXECUTABLE})
        set(SHADER_COMPILE_ARGS --target-env=vulkan1.2)
        set(SHADER_COMPILE_TAG "glslc")
    else()
        set(SHADER_COMPILER "")
        message(WARNING "no shader compiler found: staging committed resources/shaders/*.spv (may be stale).")
    endif()
    if(SHADER_COMPILER)
        message(STATUS "Shader compiler: ${SHADER_COMPILE_TAG}")
    endif()

    set(SPV_OUTPUTS "")
    # Shared GLSL includes, every shader rebuilds when one changes.
    set(SHADER_INCLUDES ${SHADER_SRC_DIR}/shadow_cdf.glsl ${SHADER_SRC_DIR}/shadow_sample.glsl)

    # Compile <source> to <output>.spv, or stage the committed .spv without a compiler.
    function(ano_add_shader output source)
        if(SHADER_COMPILER)
            add_custom_command(
                    OUTPUT  ${SHADER_BIN_DIR}/${output}.spv
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
                    COMMAND ${SHADER_COMPILER} ${SHADER_COMPILE_ARGS} ${ARGN} -o ${SHADER_BIN_DIR}/${output}.spv ${SHADER_SRC_DIR}/${source}
                    DEPENDS ${SHADER_SRC_DIR}/${source} ${SHADER_INCLUDES}
                    COMMENT "${SHADER_COMPILE_TAG} ${source} ${ARGN} -> ${output}.spv"
                    VERBATIM)
        else()
            add_custom_command(
                    OUTPUT  ${SHADER_BIN_DIR}/${output}.spv
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_SRC_DIR}/${output}.spv ${SHADER_BIN_DIR}/${output}.spv
                    DEPENDS ${SHADER_SRC_DIR}/${output}.spv
                    COMMENT "staging committed ${output}.spv (no compiler)"
                    VERBATIM)
        endif()
        set(SPV_OUTPUTS ${SPV_OUTPUTS} ${SHADER_BIN_DIR}/${output}.spv PARENT_SCOPE)
    endfunction()

    foreach(shader IN ITEMS flat.mesh flat.vert flat.frag flat.task transmission.frag additive.frag cull.comp tpsort.comp update.comp scatter.comp lightcull.comp lightsetup.comp shadowsetup.comp hiz.comp shadow_depth.frag shadowblur.vert shadowblur.frag tonemap.vert tonemap.frag textraster.comp overlay.frag textworld.vert textworld.frag)
        ano_add_shader(${shader} ${shader})
    endforeach()
    # Depth-only variants for the depth pre-pass and shadow atlas.
    ano_add_shader(flat_depth.mesh flat.mesh -DANO_DEPTH_ONLY)
    ano_add_shader(flat_depth.vert flat.vert -DANO_DEPTH_ONLY)
    # Task-launched mesh variants, used when RendererState.taskCull.
    ano_add_shader(flat_task.mesh flat.mesh -DANO_TASK_CULL)
    ano_add_shader(flat_depth_task.mesh flat.mesh -DANO_TASK_CULL -DANO_DEPTH_ONLY)
    # Hi-Z variant reading MAX-resolved depth, used when the device supports it.
    ano_add_shader(hiz_resolve.comp hiz.comp -DRESOLVED_DEPTH)
    # fp16 CDF-reconstruct variants of the lighting frags, used when the device has shaderFloat16.
    ano_add_shader(flat_fp16.frag flat.frag -DANO_CDF_FP16=1)
    ano_add_shader(transmission_fp16.frag transmission.frag -DANO_CDF_FP16=1)
    # Alpha-tested cutout lane (glTF alphaMode MASK).
    ano_add_shader(flat_masked.frag flat.frag -DANO_ALPHA_MASK=1)
    ano_add_shader(flat_masked_fp16.frag flat.frag -DANO_ALPHA_MASK=1 -DANO_CDF_FP16=1)
    ano_add_shader(flat_depth_masked.mesh flat.mesh -DANO_DEPTH_MASKED)
    ano_add_shader(flat_depth_masked_task.mesh flat.mesh -DANO_TASK_CULL -DANO_DEPTH_MASKED)
    ano_add_shader(flat_depth_masked.vert flat.vert -DANO_DEPTH_MASKED)
    ano_add_shader(shadow_depth_masked.frag shadow_depth.frag -DANO_ALPHA_MASK=1)

    add_custom_target(anoptic_shaders DEPENDS ${SPV_OUTPUTS})
    # Everything linking anoptic_render loads shaders at runtime.
    add_dependencies(anoptic_render anoptic_shaders)
elseif(NOT ANOPTIC_HEADLESS)
    message(WARNING "Vulkan SDK not found: building anoptic_core + tests only (use -DANOPTIC_HEADLESS=ON for a console engine).")
endif()

# anopticengine: links anoptic_render, or anoptic_core when headless.
if(ANOPTIC_HEADLESS OR Vulkan_FOUND)
    add_executable(anopticengine ./src/engine/main.c)
    if(Vulkan_FOUND)
        target_link_libraries(anopticengine PRIVATE anoptic_render)

        # Stage test assets next to the binary.
        if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
            add_custom_command(TARGET anopticengine POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:anopticengine>
                    COMMENT "Staging assets/ next to anopticengine"
                    VERBATIM)
        endif()

        # Stage fonts next to the binary.
        add_custom_command(TARGET anopticengine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts $<TARGET_FILE_DIR:anopticengine>/resources/fonts
                COMMENT "Staging fonts next to anopticengine"
                VERBATIM)
    else()
        target_link_libraries(anopticengine PRIVATE anoptic_core)
    endif()

    # Install: bin/anopticengine plus bin/resources/shaders.
    install(TARGETS anopticengine RUNTIME DESTINATION bin)
    if(Vulkan_FOUND)
        install(DIRECTORY ${SHADER_BIN_DIR} DESTINATION bin/resources)
    endif()
endif()

# CTest integration
if(ANOPTIC_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
