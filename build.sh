#!/bin/bash
set -e

# Move to the script's directory
cd "$(dirname "$0")"

# Build profile
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
    # Headless engine (no GPU). TESTS=OFF: stale build/Headless cache must not keep ANOPTIC_TESTS=ON.
    build_type="Release"
    build_dir="Headless"
    toolchain_file="clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_HEADLESS=ON -DANOPTIC_TESTS=OFF"
    run_tests=0
    ;;
  4)
    # Headless debug engine: core + CTest, no renderer
    build_type="Debug"
    build_dir="HeadlessDebug"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_HEADLESS=ON"
    run_tests=1
    ;;
  5)
    # Debug build with CTest enabled
    build_type="Debug"
    build_dir="Tests"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON"
    run_tests=1
    ;;
  6)
    build_type="Debug"
    build_dir="Tests-ASan"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=asan"
    run_tests=1
    ;;
  7)
    build_type="Debug"
    build_dir="Tests-TSan"
    toolchain_file="debug_clang-linux-x64.cmake"
    extra_flags="-DANOPTIC_TESTS=ON -DANOPTIC_SANITIZE=tsan"
    run_tests=1
    ;;
  8)
    # Release (-O3) tests, use for benchmarks
    build_type="Release"
    build_dir="O3Tests"
    toolchain_file="clang-linux-x64.cmake"
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

# Platform: macOS uses Nix shell clang when present, else Homebrew LLVM. Other platforms use toolchain files.
if [ "$(uname -s)" = "Darwin" ]; then
    if [ -n "$IN_NIX_SHELL" ] && command -v clang >/dev/null 2>&1; then
        platform_args="-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
    else
        # don't abort under set -e when llvm is absent
        llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
        if [ -z "$llvm_prefix" ] || [ ! -x "$llvm_prefix/bin/clang" ]; then
            echo "Error: no Nix shell and no Homebrew LLVM clang found."
            echo "Either 'nix develop' (flake provides all deps) or 'brew install llvm'."
            exit 1
        fi
        platform_args="-DCMAKE_C_COMPILER=$llvm_prefix/bin/clang -DCMAKE_CXX_COMPILER=$llvm_prefix/bin/clang++"
    fi
else
    platform_args="-DCMAKE_TOOLCHAIN_FILE=${toolchain_path}"
fi

# Generator: Ninja when available
generator_args=""
if command -v ninja >/dev/null 2>&1; then
    generator_args="-G Ninja"
    # Reset a build dir whose cache has a different generator or source root.
    cache="./build/${build_dir}/CMakeCache.txt"
    src="$(pwd -P)"
    if [ -f "$cache" ] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$' "$cache"; then
        rm -rf "./build/${build_dir}"
    fi
    if [ -f "$cache" ] && ! grep -qxF "CMAKE_HOME_DIRECTORY:INTERNAL=$src" "$cache"; then
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

# Engine-only profiles must produce a binary, fail loudly if Vulkan was missing.
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
