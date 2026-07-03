# shell.nix -- Linux build toolchain for the Anoptic Engine under WSL/Nix.
#
# Debian/WSL ships no C toolchain, so this shell provides clang, cmake and glslc for the
# *native Linux* side of the engine: the headless build, the logger benchmark, the non-GPU
# test suite, and the ASan/TSan sanitizer runs (which are Linux-only -- the MinGW/Windows
# target supports neither, and TSan is how the lock-free logger/threads get validated).
#
# WSL exposes no Linux Vulkan driver, so the renderer and the vk_* tests are NOT runnable
# from a Linux WSL process. Build+run the Windows target for anything graphical -- that path
# uses the native MSYS2 toolchain + Windows Vulkan SDK and needs no Nix (build.bat finds them).
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
