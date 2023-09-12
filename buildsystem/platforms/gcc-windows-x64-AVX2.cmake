# windows-x64-AVX2.cmake
# Toolchain file for 64-bit Windows operating platforms with support for AVX2 vector extensions.

message(STATUS "!! Using windows-x64-AVX2.cmake toolchain.")


# WIP

# Specify Target System
set(CMAKE_SYSTEM_NAME Windows)

# Specify the compiler
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# Set gcc compiler to 64-bit mode and enables AVX2 extensions.
set(CMAKE_C_FLAGS "-m64 -march=x86-64 -mavx2")
set(CMAKE_CXX_FLAGS "-m64 -march=x86-64 -mavx2")