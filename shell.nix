# shell.nix -- LEGACY Linux build shell (unpinned <nixpkgs>). Prefer the flake:
#
#   nix develop                      # same Linux clang shell, pinned nixpkgs
#   nix develop .#windows            # MinGW-w64 cross shell (Windows exe from WSL)
#   nix build / nix build .#renderer # one-shot packages
#
# Kept for nix-shell muscle memory only; it can desync from the flake's pinned rev.
# WSL exposes no Linux Vulkan driver, so the renderer and the vk_* tests are NOT runnable
# from a Linux WSL process -- build the Windows target (flake .#windows shell, or the
# no-Nix MSYS2 path via build.bat) for anything graphical.
#
#   nix-shell --run './build.sh 6'   # headless build + non-GPU test suite
#   nix-shell --run './build.sh 5'   # ThreadSanitizer (Linux-only)

{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "anoptic-engine-dev";

  # Nixpkgs' cc-wrapper injects -D_FORTIFY_SOURCE, which errors under the -O0 Debug builds
  # (profiles 2-6): "_FORTIFY_SOURCE requires compiling with optimization". Turn it off.
  hardeningDisable = [ "fortify" "fortify3" ];

  nativeBuildInputs = with pkgs; [
    clang            # native C23 compiler (README requires clang 17+)
    lld
    cmake
    gnumake          # build.sh uses the default "Unix Makefiles" generator
    ninja
    pkg-config
    shaderc          # glslc: compiles shaders to SPIR-V at build time
    git
  ];

  shellHook = ''
    echo "[anoptic] $(clang --version | head -1)"
    echo "[anoptic] $(cmake --version | head -1)"
  '';
}
