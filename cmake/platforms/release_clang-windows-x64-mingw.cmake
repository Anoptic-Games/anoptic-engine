# clang-windows-x64-mingw.cmake

# Specify the compilers to use for C and C++
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Set the system name, processor, and system version
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross compiler locations
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Add any other flags you need
set(CMAKE_C_FLAGS "--target=${TOOLCHAIN_PREFIX} -O3 -m64 -march=x86-64")
set(CMAKE_CXX_FLAGS "--target=${TOOLCHAIN_PREFIX} -O3 -m64 -march=x86-64")

# Search for libraries and headers in the target directories only
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
