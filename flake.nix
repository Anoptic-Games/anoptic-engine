{
  # Anoptic Engine dev environments. Two build targets, selected by shell:
  #
  #   nix develop                # Linux-native clang: headless build, tests, ASan/TSan
  #   nix develop .#windows      # MinGW-w64 clang cross: Windows .exe from WSL/Linux
  #
  # The Windows shell provisions the ENTIRE Windows toolchain from Nix -- compiler,
  # Vulkan import lib/headers, glslc -- no MSYS2, no Windows-side Vulkan SDK needed
  # to BUILD. The produced .exe runs on the Windows host (real GPU driver) via WSL
  # interop, so `ctest` and `./anopticengine.exe` work directly from the shell.
  #
  # Target selection is the flake-idiomatic "argument": one devShell per target.
  # There is no autodetect -- a Linux host is a valid host for BOTH targets, so
  # the choice can't be inferred; you name the one you want.
  description = "Anoptic Engine — C23 game engine (Linux native + Windows MinGW-w64 cross)";

  inputs = {
    # Pinned to the same rev as the pylon system flake so the (large) mingw cross
    # toolchain is shared from the store instead of rebuilt. Advance deliberately.
    nixpkgs.url = "github:NixOS/nixpkgs/b5aa0fbd538984f6e3d201be0005b4463d8b09f8";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      # Cross package set: same nixpkgs, re-evaluated with hostPlatform = Windows.
      # Tools under .buildPackages run on Linux; top-level attrs are Windows-target.
      # ucrt64, not mingwW64: the engine needs C11 timespec_get, which the UCRT
      # has and the legacy msvcrt (mingwW64's default) lacks -- and UCRT is what
      # the MSYS2 clang64 path links too, so both Windows builds share a CRT.
      crossPkgs = pkgs.pkgsCross.ucrt64;

      # Build-machine tools shared by both targets (these always run on Linux).
      commonNativeTools = with pkgs; [
        cmake # carries ctest
        ninja
        gnumake # build.sh uses the default "Unix Makefiles" generator
        pkg-config
        shaderc # glslc: compiles shaders to SPIR-V at build time
        git
      ];

      # Nixpkgs' cc-wrapper injects -D_FORTIFY_SOURCE, which errors under -O0
      # Debug builds: "_FORTIFY_SOURCE requires compiling with optimization".
      fortifyOff = [
        "fortify"
        "fortify3"
      ];
    in
    {
      devShells.${system} = {
        # ---- Linux target (default) -------------------------------------------
        # Headless build, the non-GPU test suite, and the ASan/TSan runs -- the
        # sanitizers are Linux-only (MinGW supports neither, and TSan is how the
        # lock-free logger/threads get validated). WSL has no Linux Vulkan driver,
        # so the renderer/vk_* tests are not runnable here; use .#windows.
        #
        #   nix develop --command ./build.sh 6    # headless build + non-GPU tests
        #   nix develop --command ./build.sh 5    # ThreadSanitizer
        default = pkgs.mkShell {
          name = "anoptic-linux";
          hardeningDisable = fortifyOff;
          nativeBuildInputs =
            commonNativeTools
            ++ (with pkgs; [
              clang # native C23 compiler (README requires clang 17+)
              lld
            ]);
          shellHook = ''
            echo "[anoptic] Linux target — $(clang --version | head -1)"
          '';
        };

        # ---- Windows target (MinGW-w64 cross) ---------------------------------
        # x86_64-w64-mingw32-gcc, wholly Nix-provisioned. GCC, not clang: the
        # engine is pure C23 (CXX is only enabled because project() defaults to
        # C+CXX), but CMake's CXX sanity check must still link, and nixpkgs'
        # mingw *clang* wrapper ships no C++ stdlib (fails at <iostream> and
        # -lgcc_s). The gcc cross stdenv carries full libstdc++/libgcc, and
        # gcc-for-Windows is already in the project matrix (gcc-windows-x64.cmake).
        # Clang stays the compiler for the Linux target and the MSYS2 path.
        #
        # The engine's cmake/platforms/*-mingw.cmake toolchain files are for the
        # MSYS2 path and expect a bare `clang` on PATH -- do NOT pass them here.
        # Nixpkgs' cmake hook exports $cmakeFlags with the equivalent cross setup
        # (CMAKE_SYSTEM_NAME=Windows, compilers, CMAKE_FIND_ROOT_PATH), so:
        #
        #   nix develop .#windows
        #   cmake $cmakeFlags -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build/Windows
        #   cmake --build build/Windows
        #   build/Windows/anopticengine.exe   # runs on the Windows host via interop
        windows = crossPkgs.mkShell {
          name = "anoptic-windows";
          hardeningDisable = fortifyOff;
          # Runs on Linux: the shared tool set (glslc included -- shaders compile
          # on the build machine regardless of target).
          nativeBuildInputs = commonNativeTools;
          # Windows-target link dependencies. find_package(Vulkan) resolves the
          # import lib + headers from these via CMAKE_FIND_ROOT_PATH; the runtime
          # vulkan-1.dll comes from the host GPU driver, not from Nix.
          buildInputs = [
            crossPkgs.vulkan-headers
            crossPkgs.vulkan-loader
            # "pthreads everywhere" (root CMakeLists): MSYS2's toolchain ships
            # winpthreads implicitly; nixpkgs' mingw gcc (mcfgthread threading
            # model) does not, so provide it explicitly for <pthread.h>.
            crossPkgs.windows.pthreads
          ];
          shellHook = ''
            echo "[anoptic] Windows target — $($CC --version | head -1)"
            echo "[anoptic] configure with: cmake \$cmakeFlags -G Ninja -S . -B build/Windows"
          '';
        };
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
