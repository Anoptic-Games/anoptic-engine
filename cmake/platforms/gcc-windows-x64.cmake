# windows-x64.cmake
# Toolchain file for 64-bit Windows platforms.

message(STATUS "!! Using windows-x64.cmake toolchain.")

# Specify Target System
set(CMAKE_SYSTEM_NAME Windows)

# Specify the compiler
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# Set gcc compiler to 64-bit mode.
set(CMAKE_C_FLAGS "-m64 -march=x86-64")
set(CMAKE_CXX_FLAGS "-m64 -march=x86-64")