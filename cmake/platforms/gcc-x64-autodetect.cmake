# gcc-x64-autodetect.cmake
# Auto-detected toolchain for the current platform.
# GCC FALLBACK path -- clang/LLVM is the project default (clang-linux-x64.cmake).

message(STATUS "!! Using auto-detected toolchain for your current platform.")


# WIP

# Specify the compiler
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# Set gcc compiler to 64-bit and determine processor type of the compiling machine.
set(CMAKE_C_FLAGS "-m64 -march=x86-64 -mtune=native")
set(CMAKE_CXX_FLAGS "-m64 -march=x86-64 -mtune=native")