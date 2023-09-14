cmake_minimum_required(VERSION 3.5)
project(anopticengine)

# Debugging the Build System
message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
message(STATUS "System version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")

# Anoptic Engine Only Supports 64-bit Compilation
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(STATUS "64-bit compiler detected.")
else()
  message(FATAL_ERROR "64-bit compiler REQUIRED.")
endif()

# Include all header directories
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Define the project root path
add_definitions(-DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# Enable C23 standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add GLFW submodule directory
add_subdirectory(${CMAKE_SOURCE_DIR}/external/glfw)

# Find the Vulkan SDK system-wide package
find_package(Vulkan REQUIRED)

# Add an executable target
add_executable(anopticengine ./src/engine/main.c)

# Module Subdirectories
add_subdirectory(${CMAKE_SOURCE_DIR}/src/platform)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/vulkan_backend)

# Link the GLFW and Vulkan libraries to the target
target_link_libraries(anopticengine glfw)
target_link_libraries(anopticengine Vulkan::Vulkan)

# Add compilation flags and preprocessor definitions for Debug and Release builds
target_compile_definitions(anopticengine PRIVATE 
    "$<$<CONFIG:Debug>:DEBUG_BUILD>"
    "$<$<CONFIG:Release>:RELEASE_BUILD>"
)

target_compile_options(anopticengine PRIVATE 
    "$<$<CONFIG:Debug>:-O0;-g>"
    "$<$<CONFIG:Release>:-O3>"
)
