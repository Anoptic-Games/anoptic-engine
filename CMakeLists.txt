cmake_minimum_required(VERSION 3.5)
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

# Enable C23 standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add GLFW submodule directory
add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

# Find the Vulkan SDK system-wide package
find_package(Vulkan REQUIRED)

# Add headers into add_executable() so IDEs parse them
file(GLOB HEADER_SOURCES
        "./include/*.h"
)

# Doing the same for single-header external dependencies
file(GLOB EXTERNAL_SOURCES
        "./external/jsmn/*.h"
        "./external/stb/*.h"
)

# Add an executable target
add_executable(anopticengine ./src/engine/main.c ${HEADER_SOURCES} ${EXTERNAL_SOURCES})

# Define the project root path
target_compile_definitions(anopticengine PRIVATE -DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# Include all header directories
target_include_directories(anopticengine PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/jsmn
)

# Module Subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/src/memory)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/threads)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/time)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/render)

# Link the GLFW and Vulkan libraries to the target
target_link_libraries(anopticengine glfw)
target_link_libraries(anopticengine Vulkan::Vulkan)

# CTest integration
enable_testing()
add_subdirectory(${CMAKE_SOURCE_DIR}/tests)

# Add compilation flags and preprocessor definitions for Debug and Release builds
target_compile_definitions(anopticengine PRIVATE
        "$<$<CONFIG:Debug>:DEBUG_BUILD>"
        "$<$<CONFIG:Release>:RELEASE_BUILD>"
)

target_compile_options(anopticengine PRIVATE
        "$<$<CONFIG:Debug>:-O0;-g>"
        "$<$<CONFIG:Release>:-O3>"
)
