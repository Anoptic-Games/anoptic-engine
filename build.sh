#!/bin/bash
set -e

# Move to the script's directory
cd "$(dirname "$0")"

# Parse the command line argument
case $1 in
  1)
    build_type="Release"
    ;;
  2)
    build_type="Debug"
    ;;
  3)
    build_type="Tests"
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

# Create build directory if not exist
mkdir -p ./build/${build_type}

# Configure the build
cmake -DCMAKE_BUILD_TYPE=${build_type} -S . -B ./build/${build_type}

# Build the project
cmake --build ./build/${build_type}