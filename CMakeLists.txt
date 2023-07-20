cmake_minimum_required(VERSION 3.0)
project(anopticengine)

# Enable C11 standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find the GLFW and Vulkan packages
find_package(glfw3 REQUIRED)
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