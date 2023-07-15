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

# Execute cmake 
# --build: build the project in ./build/${build_type}, configure before building, set build type, using '.' as the source code root directory.
cmake --build ./build/${build_type} --configure -- -DCMAKE_BUILD_TYPE=${build_type} -S .
#             ^Target directory     ^create subdirectory     ^build type parameter     ^Source directory 