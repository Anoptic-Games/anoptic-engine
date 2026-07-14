## Anoptic Game Engine

The Anoptic Game Engine is designed to create games that can handle large numbers of events and entities occurring simultaneously. It is tailored for 4X, RTS, and other kinds of complex simulation.

### Features

- **ECS**: En entity component system that hopefully doesn't suck.
- **ano Scripting**: A neat scripting language designed for batched invocations over an ECS runtime.
- **Event System**: Message-passing between different systems via a generalized event bus.
- **Native Parallelism**: Robust multithreading through the use of lockfree interconnects.
- **Vulkan Renderer**: From-scratch Vulkan mesh renderer built for modern hardware. Has an automatic vertex-shader fallback for legacy devices.
- **Custom Allocators**: Uses mimalloc for a fast global allocator implementation, as well as several special-purpose local allocators.
- **Platform Compatibility**: Built and tested for full feature parity on `x86-64 Linux`,`x86-64 Windows`, and `Apple Silicon macOS`.
- **Networking**: Built-in networking support for both p2p and authoritative server.


### Getting this Repo

Make sure to use `--recursive` to fetch all submodules!

```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

Set `git config submodule.recurse true`
or
Run `git submodule update --init --recursive` after every pull to keep dependencies in sync.

The expected versions of each submodule are included in `flake.nix`, so the Nix dev shells warn on entry when the repo's submodules are out of sync.


### Installation

The following instructions apply to any system that can use Nix. That includes every major Linux distribution, MacOS Darwin, and Windows WSL.

We like Nix. Get it from: https://nix.dev/install-nix.html

Turn on flakes:
```bash
# /etc/nix/nix.conf (or ~/.config/nix/nix.conf)
experimental-features = nix-command flakes
```

#### Impure Nix Install (Default)

When you're in the repo's root directory, try running:
```bash
nix run
```

This should automagically take care of everything. (Thank you GLSLtesseract!)

Keep reading if that didn't work or if you want to learn about every other build option.

- `nix run .#nvidia` (or `.#nvidia-debug`): for non-NixOS distros where the Nix loader cannot dlopen host ICDs.

**In general:**
`nix run [-- N]` builds in-tree and runs the result: it checks submodule pointers against the flake, fetches anything missing, stages assets, and runs `./build.sh N` in the dev shell. Profiles `1|2` then launch the renderer, `3` launches the headless console engine (WSL included); test profiles run their CTest suite instead. On foreign distros (no `/run/opengl-driver`) the launch auto-bridges to the host GPU: mesa ICDs from the store, plus `nixglhost` when an NVIDIA kernel module is present. Bare `nix run` is `build.sh 1` — Release into `build/Release/`, then the window.


#### Pure Nix Install (Compliant Systems)

If you're a purist, run:
```bash
nix build
```

This produces the full Vulkan renderer (Release + ThinLTO) at `./result/bin/anopticengine`. Nix fetches the toolchain, pinned submodules, shader compiler, and public asset pack.

Here's a list of the valid build targets:
- `nix build .#release`: The same as `nix build`
- `nix build .#debug`: Debug with renderer validation layers enabled.
- `nix build .#headless`: server release build. No renderer, GPU, or display.
- `nix build .#release-headless`: same as `.#headless`.
- `nix build .#release-wsl`: cross-built Windowss `.exe` with renderer (see [Building for Windows with WSL](#building-for-windows-with-wsl)).
- `nix build .#anygpu` — Linux renderer with mesa's Vulkan ICDs bundled as a driver default (`VK_ADD_DRIVER_FILES`, opt out by setting it empty): AMD/Intel/NVK hardware works on any distro with no host driver packages.
- `nix build .#tests-headless` — CTest suite in the sandbox (`.#tests-asan`, `.#testsI wan-tsan`, `.#tests-full` on Linux; `tests-full` runs the renderer suite under Xvfb, and the Vulkan device tests skip where no device can run the renderer). The suites are packages — build them explicitly; `nix flake check` only evaluates.

**In general:**
`nix build .#<type>[-headless]-<platform>-<arch>[-wayland|-x11|-anygpu]`: any permutation, e.g. `.#release-linux-x64-x11`.

`nix develop` opens the dev shell; `nix develop .#windows` is the MinGW-w64 cross shell.

Private assets instead of the public pack:
```bash
nix build --override-input anoptic-assets git+ssh://git@github.com/Anoptic-Games/assets
```
`nix run` tries the private repo and falls back to the public pack.

Native Windows has no Nix; that is the `build.bat` path below.


### Building without Nix

Each platform has its own build script: `build.sh` (Linux/macOS) and `build.bat` (Windows).

- **Release**: the optimized (`-O3` + ThinLTO) engine build.
- **Debug**: debug build, Vulkan validation layers on.
- **Tests**:  Debug build + the full CTest suite.
- **Headless**: Release engine without the renderer; the console/server entry point (profile 3).
- **Sanitizer tests**: the same suite under AddressSanitizer/UBSan or ThreadSanitizer.
- **Headless**: core + tests with the renderer disabled entirely.
- **Release tests**:  the CTest suite at `-O3` for benchmarks.

Anoptic never builds incrementally, because uhhhh we just don't okay? The build scripts run the `ano_scrub` target before every build, deleting every object file so we always recompile from scratch.

Output goes to `build/<label>/`; shaders and assets from `assets/` are staged next to the binaries by CMake itself.

**Shaders** are compiled to SPIR-V automatically at build time via `glslc` (the `anoptic_shaders` target) into `build/<label>/resources/shaders/`.
`resources/shaders/compile.sh` regenerates those committed fallbacks by hand.

`cmake --install` produces a self-contained tree: `bin/anopticengine` and `bin/resources/shaders/`.


#### Building on Windows

Make sure you have `CMake` installed and in your path: https://cmake.org/install/.

Have a copy of the `Mingw-w64` toolkit in your path.

We recommend [MSYS2](https://www.msys2.org/)'s **CLANG64** environment with [mingw-w64-clang-x86_64-clang](https://packages.msys2.org/package/mingw-w64-clang-x86_64-clang)
and [mingw-w64-clang-x86_64-ninja](https://packages.msys2.org/package/mingw-w64-clang-x86_64-ninja).
The engine needs the UCRT runtime because we just do okay?

Additional guidance:
- [Microsoft Documentation](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [CLion Configuration](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html#clang-mingw)

Once Mingw-w64 is installed with `clang` working on your system, run `build.bat` from the repository root. Its usage mirrors `build.sh`.

#### Building for Windows with WSL

WSL has no Linux Vulkan driver, so the renderer runs there only as a **Windows** exe.

WSL's Vulkan devices (Mesa `dozen` and `llvmpipe`) are not supported render targets. The **headless** build needs no GPU or display at all: `nix build .#headless` / `build.sh 3` run fine in WSL, containers, and GPU-less servers.

**MSYS2 clang + Windows Vulkan SDK** (no Nix): the `build.bat` path:

```bash
cmd.exe /c build.bat 1
( cd build/Release && ./anopticengine.exe )
```

If your config is cursed and that doesn't work, just use Nix okay?

#### Building on Linux

```bash
nix develop --command ./build.sh  1
```

The Nix shell provides clang/lld 22, cmake, ninja, glslc + glslangValidator, lldb, llvm-ar, Vulkan headers, loader, and validation layers, and the X11 + Wayland client libraries. Renderer builds compile both window backends and select at runtime; the single-backend `-wayland`/`-x11` packages are explicit targets. For GPU-less test runs, point `VK_ICD_FILENAMES` at `$ANO_LAVAPIPE_ICD` (exported by the shell). The foreign-distro GPU plumbing — why host ICDs fail to load under the Nix loader, the `nixglhost` bridge, lavapipe's limits — is documented in `docs/nix/NIX_LINUX.md`.

Without Nix: install `clang 20+`, `CMake`, `Ninja`, `glslc` yourself I guess. You'll also need to install your distro's Vulkan SDK. Then you can run `build.sh` and pray.

If your config is cursed and that doesn't work, just use Nix okay?

#### Building on macOS

[Apple Silicon.](https://youtu.be/2zkLh_QMFdQ)

Vulkan runs through MoltenVK; the Nix packages bake in the ICD path, and the dev shell exports `VK_ICD_FILENAMES`.

```bash
nix build                              # store artifact -> ./result/bin/anopticengine
nix develop --command ./build.sh 1     # in-tree -> build/Release/   (or: nix run)
```

Without Nix: yeahh you know what you're doing so you can figure it out.

If your config is cursed and that doesn't work, just use Nix okay?


### Editor / LSP Setup

You may want to integrate `clangd` as a language server. 

Install `clangd`.

On VSCode, use the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).

`.vscode/settings.json`:
```json
{
  "clangd.path": "/opt/homebrew/opt/llvm/bin/clangd",
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build/Debug",
    "--background-index",
    "--clang-tidy"
  ],
  "C_Cpp.intelliSenseEngine": "disabled"
}
```

> Note: You need the debug build compiled before `clangd` resolves anything.


### Tests

The build script's test option runs a full CTest suite.

You can see the tests in test/ and look at test/CMakeLists.txt.

They are 100% vibe-coded by frontier models.

The logger benchmark (`anotest_logbench`) and the allocator easter egg (`anotest_chariots`) are built but disabled in CTest. Run them by hand from a Release-tests build (debug numbers are of no use in benchmarking).

### Profiling

To capture frame traces, try ANO_FORCE_NO_ASYNC_HIZ=1 ANO_FORCE_NO_ASYNC_LC=1 ANO_FORCE_NO_ASYNC_TEXT=1.

### Rendering Compatibility

- **Mesh path (default):** on devices exposing `VK_EXT_mesh_shader`. A mesh shader expands meshlets on the GPU.
- **Vertex path (fallback):** on devices without the mesh shader extension. A vertex shader (`flat.vert`) renders the same geometry via classic indexed indirect draws.

Path selection is automatic at device-creation time. For testing, set the environment variable `ANO_FORCE_NO_MESH_SHADER=1` to force the fallback path even on mesh-capable hardware:

```bash
ANO_FORCE_NO_MESH_SHADER=1 ./build/Debug/anopticengine
```

The startup log prints which rendering path is active, e.g. `Enabling 3 device extensions (mesh shader: yes)`.

#### Device selection

The engine ranks suitable Vulkan devices by the following metrics, in order:
1. mesh-shader capability
2. largest DEVICE_LOCAL memory
3. suitable CPU integrated graphics
4. virtual devices (software rasterizers, VM adapters) as a last-resort fallback.

The chosen rendering device is logged as `Selected device: <name>`.

Set `ANO_DEVICE` to a substring of a device name to override, for example:
```bash
ANO_DEVICE=nvidia ./build/Release/anopticengine   # force use NVIDIA adapter
ANO_DEVICE=intel  ./build/Release/anopticengine   # force Intel iGPU
```

If nothing matches ANO_DEVICE, the engine warns and selects a device automatically.

### More

Check out the `.md` files in each subdirectory for more information on a given module.
