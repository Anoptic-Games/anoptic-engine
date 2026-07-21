# clang-linux-x64.cmake
# Toolchain file for 64-bit Linux platforms.

# Specify the compilers to use for C and C++
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_FLAGS "-target x86_64-linux-gnu")
set(CMAKE_CXX_FLAGS "-target x86_64-linux-gnu")
    
# No CMAKE_SYSTEM_NAME: native build 〜 let CMake detect the host (avoids cross-compile mode).
