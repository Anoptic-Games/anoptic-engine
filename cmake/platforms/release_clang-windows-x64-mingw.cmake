# clang-windows-x64-mingw.cmake

# Compilers
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Target system
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# MinGW prefix
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Toolchain sets compiler and target only, optimization lives in the build config.
set(CMAKE_C_FLAGS "--target=${TOOLCHAIN_PREFIX} -m64 -march=x86-64")
set(CMAKE_CXX_FLAGS "--target=${TOOLCHAIN_PREFIX} -m64 -march=x86-64")

# Find libs/headers in target only
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
