cmake_minimum_required(VERSION 3.0)
project(anopticengine)

# Include all headers
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