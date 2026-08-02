#!/bin/bash
set -e

# Move to the script's directory
cd "$(dirname "$0")"

# Build profile
case $1 in
  1)
    build_type="Release"
    build_dir="Release"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags=""
    run_tests=0
    ;;
  2)
    build_type="Debug"
    build_dir="Debug"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags=""
    run_tests=0
    ;;
  3)
    # Headless (no GPU, WSL). TESTS=OFF clears stale cache
    build_type="Release"
    build_dir="Headless"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_HEADLESS=ON -DANOPTIC_TESTS=OFF"
    run_tests=0
    ;;
  4)
    # Headless debug engine: core + CTest, no renderer
    build_type="Debug"
    build_dir="HeadlessDebug"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_HEADLESS=ON"
    run_tests=1
    ;;
  5)
    # Debug build with CTest enabled
    build_type="Debug"
    build_dir="Tests"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON"
    run_tests=1
    ;;
  6)
    build_type="Debug"
    build_dir="Tests-ASan"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=asan"
    run_tests=1
    ;;
  7)
    build_type="Debug"
    build_dir="Tests-TSan"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=tsan"
    run_tests=1
    ;;
  8)
    # Release (-O3) tests, use for benchmarks
    build_type="Release"
    build_dir="O3Tests"
    toolchain_file="gcc-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON"
    run_tests=1
    ;;
  *)
    echo "Usage: $0 <build_type>"
    echo "  where <build_type> is one of:"
    echo "    1 = Release (-O3 full engine build)"
    echo "    2 = Debug"
    echo "    3 = Headless engine (Release console mode, no GPU)"
    echo "    4 = Headless debug engine (core + CTest, no renderer)"
    echo "    5 = Tests (Debug -O0, build + run CTest)"
    echo "    6 = Tests + AddressSanitizer/UBSan"
    echo "    7 = Tests + ThreadSanitizer"
    echo "    8 = O3 tests (Release, build + run CTest)"
    exit 1
    ;;
esac

# Paths
script_dir=$(dirname "$0")
toolchain_path="$script_dir/cmake/platforms/${toolchain_file}"

# P2996 reflection is canonical here; Darwin has no supported compiler yet.
if [ "$(uname -s)" = "Darwin" ]; then
    echo "Error: hyper-c-reflection requires GCC 16.1+; no supported Darwin compiler is available." >&2
    exit 1
else
    platform_args="-DCMAKE_TOOLCHAIN_FILE=${toolchain_path}"
fi

# Generator: Ninja when available
generator_args=""
if command -v ninja >/dev/null 2>&1; then
    generator_args="-G Ninja"
    # Wipe cache on generator or source root mismatch
    cache="./build/${build_dir}/CMakeCache.txt"
    src="$(pwd -P)"
    if [ -f "$cache" ] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$' "$cache"; then
        rm -rf "./build/${build_dir}"
    fi
    if [ -f "$cache" ] && ! grep -qxF "CMAKE_HOME_DIRECTORY:INTERNAL=$src" "$cache"; then
        rm -rf "./build/${build_dir}"
    fi
    compiler_state="$(find "./build/${build_dir}/CMakeFiles" -name CMakeCXXCompiler.cmake -print -quit 2>/dev/null || true)"
    if [ -f "$compiler_state" ] && ! grep -q 'set(CMAKE_CXX_COMPILER_ID "GNU")' "$compiler_state"; then
        echo "[anoptic] cached compiler is not GCC; rebuilding ${build_dir} configuration."
        rm -rf "./build/${build_dir}"
    fi
else
    echo "[anoptic] ninja not found; falling back to Makefiles (slower)." \
         "Install it: brew install ninja / apt install ninja-build / pacman -S ninja"
fi

# Configure
mkdir -p ./build/${build_dir}
cmake ${generator_args} -DCMAKE_BUILD_TYPE=${build_type} ${extra_flags} ${platform_args} -S . -B ./build/${build_dir}

# Scrub all object files.
cmake --build ./build/${build_dir} --target ano_scrub

# Build
cmake --build ./build/${build_dir} --parallel

# Engine profile: require binary (no Vulkan skips it)
if [ ${run_tests} -eq 0 ] && [ ! -e "./build/${build_dir}/anopticengine" ]; then
    echo "Error: no anopticengine binary was produced." >&2
    echo "CMake found no Vulkan SDK, so the renderer (and the engine target) was skipped" >&2
    echo "-- see the CMake warning above. Options:" >&2
    echo "  ./build.sh 3|4            headless engine / non-GPU tests (no Vulkan needed)" >&2
    echo "  nix build                 full renderer package (any supported host)" >&2
    echo "  nix build .#release-wsl   Windows renderer exe from WSL/Linux" >&2
    exit 1
fi

# Tests
if [ ${run_tests} -eq 1 ]; then
    ctest --test-dir ./build/${build_dir} --output-on-failure
fi
