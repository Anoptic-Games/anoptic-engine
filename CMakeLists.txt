cmake_minimum_required(VERSION 3.0)
project(panopticon)

# Enable C11 standard
set(CMAKE_C_STANDARD 11)

# Find the GLFW and Vulkan packages
find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)

# Add an executable target
add_executable(panopticon ./src/engine/main.c)

# Link the GLFW and Vulkan libraries to the target
target_link_libraries(panopticon glfw)
target_link_libraries(panopticon Vulkan::Vulkan)
