{
  # Anoptic Engine dev environments + buildable packages. Targets, by command:
  #
  #   nix build                  # headless engine binary, one shot -> ./result/bin
  #   nix build .#renderer       # full Vulkan renderer binary + shaders (runs on a
  #                              # real-GPU Linux host; WSL has no Linux Vulkan driver)
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
  #
  # All of the above are flake commands. They need `experimental-features =
  # nix-command flakes` in nix.conf, or `--extra-experimental-features` per run.
  # A flake cannot self-enable this (nixConfig is read only once flakes work),
  # so the setup note lives in the README ("Building under WSL / Nix").
  description = "Anoptic Engine — C23 game engine (Linux native + Windows MinGW-w64 cross)";

  inputs = {
    # Pinned to the same rev as the pylon system flake so the (large) mingw cross
    # toolchain is shared from the store instead of rebuilt. Advance deliberately.
    nixpkgs.url = "github:NixOS/nixpkgs/b5aa0fbd538984f6e3d201be0005b4463d8b09f8";

    # `src = self` gives the superproject git tree WITHOUT submodule contents, so the
    # packages inject the ones they compile from pinned inputs instead. Each rev == the
    # gitlink recorded in .gitmodules; bump both together. The headless build needs only
    # mimalloc; the renderer adds glfw + cgltf. (external/freetype is a submodule too,
    # but nothing in the build or source references it -- deliberately not an input.)
    mimalloc-src = {
      url = "github:microsoft/mimalloc/02a2f5df9d7d46d30263b83832eebeeab62dc5fe";
      flake = false;
    };
    glfw-src = {
      url = "github:glfw/glfw/7b6aead9fb88b3623e3b3725ebb42670cbe4c579";
      flake = false;
    };
    cgltf-src = {
      url = "github:jkuhlmann/cgltf/85cd62382dfea638278962690cf515023f33ed00";
      flake = false;
    };
  };

  outputs =
    { self, nixpkgs, mimalloc-src, glfw-src, cgltf-src }:
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
      # Dev shells only -- the packages build Release (-O3), where fortify is fine.
      fortifyOff = [
        "fortify"
        "fortify3"
      ];

      # Drop a pinned submodule source into the unpacked tree (submodule contents are
      # absent from `self`; the store copy is read-only, hence the chmod).
      injectSubmodule = name: src: ''
        rm -rf "$sourceRoot/external/${name}"
        cp -r ${src} "$sourceRoot/external/${name}"
        chmod -R u+w "$sourceRoot/external/${name}"
      '';

      # One derivation shape for both engine packages; the engine's own install()
      # rules (root CMakeLists) place bin/anopticengine and, for the renderer,
      # bin/resources/shaders -- the default installPhase just runs them.
      mkEngine =
        {
          pname,
          description,
          headless,
          extraNative ? [ ],
          extraBuild ? [ ],
          extraUnpack ? "",
        }:
        pkgs.clangStdenv.mkDerivation {
          inherit pname;
          version = "0.0.1";
          src = self;

          nativeBuildInputs = (with pkgs; [ cmake ninja pkg-config ]) ++ extraNative;
          buildInputs = extraBuild;

          postUnpack = injectSubmodule "mimalloc" mimalloc-src + extraUnpack;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ] ++ pkgs.lib.optional headless "-DANOPTIC_HEADLESS=ON";

          meta = {
            inherit description;
            mainProgram = "anopticengine";
          };
        };
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
          # Runtime DLLs the produced exe imports (libmcfgthread-2, libwinpthread-1).
          # The Windows host cannot resolve them from the nix store and a missing DLL
          # fails SILENTLY over WSL interop, so the root CMakeLists stages every DLL
          # in these directories next to the built binaries.
          ANOPTIC_MINGW_DLL_DIRS = "${crossPkgs.windows.mcfgthreads}/bin;${crossPkgs.windows.pthreads}/bin";
          shellHook = ''
            echo "[anoptic] Windows target — $($CC --version | head -1)"
            echo "[anoptic] configure with: cmake \$cmakeFlags -G Ninja -S . -B build/Windows"
          '';
        };
      };

      # ---- Buildable packages -------------------------------------------------
      # `nix build` -> a runnable engine in one shot, no dev shell, no manual cmake.
      # Shaders resolve relative to the executable (loadFile / ano_fs_gamepath) and the
      # engine's install() rules ship them next to it, so the renderer package is
      # self-contained: bin/anopticengine + bin/resources/shaders. Test assets
      # (assets/*.gltf) are gitignored -- absent from `self` -- so the packaged
      # renderer aborts at the model load unless assets are provided beside the exe;
      # the resource manager (docs/resourcesmg.md) owns the real fix for that.
      packages.${system} = {
        default = mkEngine {
          pname = "anopticengine-headless";
          description = "Anoptic Engine — headless console build";
          headless = true;
        };

        renderer = mkEngine {
          pname = "anopticengine";
          description = "Anoptic Engine — Vulkan renderer build";
          headless = false;
          # glslc compiles shaders at build time; wayland-scanner is a hard
          # find_program requirement of the vendored glfw's Wayland backend.
          extraNative = with pkgs; [
            shaderc
            wayland-scanner
          ];
          # glfw 3.4 builds BOTH Linux backends by default: X11 headers via
          # find_package(X11) + per-header checks, Wayland via pkg-config
          # (wayland-client/cursor/egl) + libxkbcommon. The protocol XMLs are
          # vendored in glfw itself, so wayland-protocols is not needed.
          # libGL: GLFW_INCLUDE_VULKAN does NOT suppress glfw3.h's <GL/gl.h>
          # include (only GLFW_INCLUDE_NONE would), so the GL headers must exist
          # even though the engine is pure Vulkan.
          extraBuild = with pkgs; [
            vulkan-headers
            vulkan-loader
            libGL
            libx11
            libxrandr
            libxinerama
            libxcursor
            libxi
            wayland
            libxkbcommon
          ];
          extraUnpack = injectSubmodule "glfw" glfw-src + injectSubmodule "cgltf" cgltf-src;
        };
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
