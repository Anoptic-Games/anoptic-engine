#!/bin/bash
set -e

# Move to the script's directory
cd "$(dirname "$0")"

# Parse the command line argument
case $1 in
  1)
    build_type="Release"
    toolchain_file="clang-linux-x64.cmake"
    ;;
  2)
    build_type="Debug"
    toolchain_file="debug_clang-linux-x64.cmake"
    ;;
  3)
    build_type="Tests"
    toolchain_file="debug_clang-linux-x64.cmake"
    ;;
  *)
    echo "Usage: $0 <build_type>"
    echo "  where <build_type> is one of:"
    echo "    1 = Release"
    echo "    2 = Debug"
    echo "    3 = Test"
    exit 1
    ;;
esac

# Path to the directory containing this script
script_dir=$(dirname "$0")

# Path to the toolchain file
toolchain_path="$script_dir/cmake/platforms/${toolchain_file}"

# Create build directory if not exist
mkdir -p ./build/${build_type}

# Configure the build
cmake -DCMAKE_BUILD_TYPE=${build_type} -DCMAKE_TOOLCHAIN_FILE=${toolchain_path} -S . -B ./build/${build_type}

# Build the project
cmake --build ./build/${build_type}
