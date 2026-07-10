{
  # Targets:
  #   nix build                  headless engine -> ./result/bin
  #   nix build .#renderer       Vulkan renderer + shaders (Linux GPU host)
  #   nix build .#headless-musl  static musl headless engine (one server binary, no glibc floor)
  #   nix develop                dev shell (Linux clang, macOS toolchain on a Mac)
  #   nix develop .#windows      MinGW-w64 cross shell -> Windows .exe from WSL/Linux
  #
  # Flakes need `experimental-features = nix-command flakes` in nix.conf.
  description = "Anoptic Engine — C23 game engine (Linux native + Windows MinGW-w64 cross + macOS Apple Silicon)";

  inputs = {
    # Same rev as the pylon system flake.
    nixpkgs.url = "github:NixOS/nixpkgs/b5aa0fbd538984f6e3d201be0005b4463d8b09f8";

    # Pinned submodule sources, revs match .gitmodules.
    mimalloc-src = {
      url = "github:microsoft/mimalloc/02a2f5df9d7d46d30263b83832eebeeab62dc5fe";
      flake = false;
    };
    freetype-src = {
      url = "github:freetype/freetype/b1f47850878d232eea372ab167e760ccac4c4e32";
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
    { self, nixpkgs, mimalloc-src, freetype-src, glfw-src, cgltf-src }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      # Windows cross package set (ucrt64 for C11 timespec_get).
      crossPkgs = pkgs.pkgsCross.ucrt64;

      # macOS Apple Silicon package set.
      darwinSystem = "aarch64-darwin";
      darwinPkgs = nixpkgs.legacyPackages.${darwinSystem};

      # clang stdenv with LLVM bintools for wrapped lld.
      clangLldStdenv = pkgs.overrideCC pkgs.clangStdenv (
        pkgs.llvmPackages.clang.override { bintools = pkgs.llvmPackages.bintools; }
      );

      # Build-machine tools shared by both targets.
      commonNativeTools = with pkgs; [
        cmake
        ninja
        pkg-config
        shaderc # glslc
        git
      ];

      # Fortify errors under -O0, disabled in dev shells.
      fortifyOff = [
        "fortify"
        "fortify3"
      ];

      # Copy a pinned submodule source into the unpacked tree.
      injectSubmodule = name: src: ''
        rm -rf "$sourceRoot/external/${name}"
        cp -r ${src} "$sourceRoot/external/${name}"
        chmod -R u+w "$sourceRoot/external/${name}"
      '';

      # Warns on shell entry when the repo's recorded submodule gitlinks disagree with the
      # flake pins (a stale-submodule `git add -A` regressed the 2026-06-24 dep bump twice).
      submodulePinCheck = ''
        if git rev-parse --git-dir >/dev/null 2>&1; then
          for pair in external/glfw=${glfw-src.rev} external/mimalloc=${mimalloc-src.rev} external/freetype=${freetype-src.rev} external/cgltf=${cgltf-src.rev}; do
            p="''${pair%%=*}" want="''${pair#*=}"
            rec="$(git ls-tree HEAD "$p" 2>/dev/null | awk '{ print $3 }')"
            if [ -n "$rec" ] && [ "$rec" != "$want" ]; then
              echo "[anoptic] WARNING: $p gitlink $rec != flake pin $want — stale submodule commit. Run 'git submodule update --init' and commit the corrected pointer." >&2
            fi
          done
        fi
      '';

      # Shared derivation shape for the engine packages.
      mkEngine =
        {
          pname,
          description,
          headless,
          buildPkgs ? pkgs,
          stdenv ? clangLldStdenv,
          extraNative ? [ ],
          extraBuild ? [ ],
          extraUnpack ? "",
        }:
        stdenv.mkDerivation {
          inherit pname;
          version = "0.0.1";
          src = self;

          nativeBuildInputs = (with buildPkgs; [ cmake ninja pkg-config ]) ++ extraNative;
          buildInputs = extraBuild;

          postUnpack =
            injectSubmodule "mimalloc" mimalloc-src
            + injectSubmodule "freetype" freetype-src
            + extraUnpack;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ] ++ buildPkgs.lib.optional headless "-DANOPTIC_HEADLESS=ON";

          meta = {
            inherit description;
            mainProgram = "anopticengine";
          };
        };
    in
    {
      devShells.${system} = {
        # Linux shell: headless build, non-GPU tests, ASan/TSan (build.sh 6/4/5).
        default = (pkgs.mkShell.override { stdenv = clangLldStdenv; }) {
          name = "anoptic-linux";
          hardeningDisable = fortifyOff;
          nativeBuildInputs = commonNativeTools;
          shellHook = ''
            echo "[anoptic] Linux target — $(clang --version | head -1)"
          '' + submodulePinCheck;
        };

        # Windows cross shell, configure with: cmake $cmakeFlags -G Ninja -S . -B build/Windows
        windows = crossPkgs.mkShell {
          name = "anoptic-windows";
          hardeningDisable = fortifyOff;
          nativeBuildInputs = commonNativeTools;
          # Windows-target link dependencies.
          buildInputs = [
            crossPkgs.vulkan-headers
            crossPkgs.vulkan-loader
            crossPkgs.windows.pthreads # <pthread.h>
          ];
          shellHook = ''
            echo "[anoptic] Windows target — $($CC --version | head -1)"
            echo "[anoptic] configure with: cmake \$cmakeFlags -G Ninja -S . -B build/Windows"
          '' + submodulePinCheck;
        };
      };

      # macOS shell: clang, cmake, ninja, glslc, MoltenVK.
      devShells.${darwinSystem} = {
        default = darwinPkgs.mkShell {
          name = "anoptic-macos";
          hardeningDisable = fortifyOff;
          nativeBuildInputs = with darwinPkgs; [
            cmake
            ninja
            pkg-config
            shaderc # glslc
            git
          ];
          buildInputs = with darwinPkgs; [
            vulkan-headers
            vulkan-loader
            moltenvk
          ];
          VK_ICD_FILENAMES = "${darwinPkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
          shellHook = ''
            echo "[anoptic] macOS target — $(clang --version | head -1)"
          '' + submodulePinCheck;
        };
      };

      # Buildable packages: `nix build` -> runnable engine in ./result/bin.
      # The renderer package ships no assets (gitignored), stage them beside the exe.
      packages.${system} = {
        default = mkEngine {
          pname = "anopticengine-headless";
          description = "Anoptic Engine — headless console build";
          headless = true;
        };

        # Fully static against musl: the blackbox needs no execinfo and mimalloc replaced the
        # allocator, so nothing here misses glibc. Renderer stays glibc: the GPU ICDs are.
        headless-musl = mkEngine {
          pname = "anopticengine-headless-musl";
          description = "Anoptic Engine — headless static musl build";
          headless = true;
          buildPkgs = pkgs.pkgsStatic;
          stdenv = pkgs.pkgsStatic.stdenv;
        };

        renderer = mkEngine {
          pname = "anopticengine";
          description = "Anoptic Engine — Vulkan renderer build";
          headless = false;
          # glslc for shaders, wayland-scanner for vendored glfw.
          extraNative = with pkgs; [
            shaderc
            wayland-scanner
          ];
          # glfw's X11 + Wayland backends need these, libGL for glfw3.h's <GL/gl.h>.
          # libffi: wayland-client.pc Requires it, so glfw's pkg_check_modules(Wayland REQUIRED) fails without it on every Linux host.
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
            libffi
            libxkbcommon
          ];
          extraUnpack = injectSubmodule "glfw" glfw-src + injectSubmodule "cgltf" cgltf-src;
        };
      };

      # macOS headless package. For the renderer use the dev shell + build.sh 1.
      packages.${darwinSystem} = {
        default = mkEngine {
          pname = "anopticengine-headless";
          description = "Anoptic Engine — headless console build";
          headless = true;
          buildPkgs = darwinPkgs;
          stdenv = darwinPkgs.stdenv;
        };
      };

      formatter.${system} = pkgs.nixfmt;
      formatter.${darwinSystem} = darwinPkgs.nixfmt;
    };
}
