#!/bin/bash
set -e

# Move to the script's directory
cd "$(dirname "$0")"

# Parse the command line argument
case $1 in
  1)
    build_type="Release"
    build_dir="Release"
    toolchain_file="clang-linux-x64.cmake"
    extra_flags=""
    run_tests=0
    ;;
  2)
    build_type="Debug"
    build_dir="Debug"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags=""
    run_tests=0
    ;;
  3)
    # Debug build with CTest enabled
    build_type="Debug"
    build_dir="Tests"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON"
    run_tests=1
    ;;
  4)
    build_type="Debug"
    build_dir="Tests-ASan"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=asan"
    run_tests=1
    ;;
  5)
    build_type="Debug"
    build_dir="Tests-TSan"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=tsan"
    run_tests=1
    ;;
  6)
    # Headless: core + CTest, renderer explicitly disabled (no Vulkan probe)
    build_type="Debug"
    build_dir="Headless"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_HEADLESS=ON"
    run_tests=1
    ;;
  7)
    # Release (-O3) build with CTest enabled: optimized test + benchmark runs. Option 1 is the same
    # -O3 full engine build without tests. Use this, NOT 3, to benchmark the logger -- 3 is a Debug
    # (-O0) build and reports ~2x pessimistic numbers.
    build_type="Release"
    build_dir="RelTests"
    toolchain_file="clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON"
    run_tests=1
    ;;
  *)
    echo "Usage: $0 <build_type>"
    echo "  where <build_type> is one of:"
    echo "    1 = Release (-O3 full engine build)"
    echo "    2 = Debug"
    echo "    3 = Tests (Debug -O0, build + run CTest)"
    echo "    4 = Tests + AddressSanitizer/UBSan"
    echo "    5 = Tests + ThreadSanitizer"
    echo "    6 = Headless tests (core + CTest, no renderer)"
    echo "    7 = Release tests (-O3, build + run CTest)"
    exit 1
    ;;
esac

# Path to the directory containing this script
script_dir=$(dirname "$0")

# Path to the toolchain file
toolchain_path="$script_dir/cmake/platforms/${toolchain_file}"

# macOS: Homebrew clang, no toolchain file (Apple clang rejects C23). Else: toolchain file.
if [ "$(uname -s)" = "Darwin" ]; then
    # || true: keep set -e from aborting here when llvm is absent; the check below reports it.
    llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
    if [ -z "$llvm_prefix" ] || [ ! -x "$llvm_prefix/bin/clang" ]; then
        echo "Error: Homebrew LLVM clang not found. Install it with 'brew install llvm'."
        exit 1
    fi
    platform_args="-DCMAKE_C_COMPILER=$llvm_prefix/bin/clang -DCMAKE_CXX_COMPILER=$llvm_prefix/bin/clang++"
else
    platform_args="-DCMAKE_TOOLCHAIN_FILE=${toolchain_path}"
fi

# Create build directory if not exist
mkdir -p ./build/${build_dir}

# Configure the build
cmake -DCMAKE_BUILD_TYPE=${build_type} ${extra_flags} ${platform_args} -S . -B ./build/${build_dir}

# Build the project
cmake --build ./build/${build_dir}

# Conditionally copy assets if they exist
if [ -d "./assets" ]; then
    echo "Copying assets to build directory..."
    cp -r ./assets/* ./build/${build_dir}/
    
    # If building tests, ensure assets are in the tests working directory as well
    if [ ${run_tests} -eq 1 ]; then
        mkdir -p ./build/${build_dir}/tests
        cp -r ./assets/* ./build/${build_dir}/tests/
    fi
fi

# Run the test suite
if [ ${run_tests} -eq 1 ]; then
    ctest --test-dir ./build/${build_dir} --output-on-failure
fi
