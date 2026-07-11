## Anoptic Game Engine

The Anoptic Game Engine is designed to create games that can handle large numbers of events and entities occurring simultaneously. It is tailored for 4X, RTS, or any other kind of complex simulation.


### Runtime Features

- **ECS**: Generational entity handles over chunked sparse-set component stores, built to manage large numbers of entities with dynamic properties and behaviors.
- **Parallel logic/render worlds**: The simulation (logic/ECS master thread) and the renderer (its own thread) run as two parallel worlds joined by lock-free single-producer/single-consumer rings. The logic side streams only *discrete* state transitions; continuous, GPU-parameterized motion is sent once and never restreamed. See [Architecture](#architecture).
- **Events**: Enable interactions between different systems via a generalized event bus.
- **Vulkan Renderer**: GPU-driven Vulkan backend, run on a dedicated render thread that owns all GPU resources and the renderable slot space. Meshlet rendering via `VK_EXT_mesh_shader` on modern hardware, with an automatic vertex-shader fallback for devices that lack the extension (see [Rendering Compatibility](#rendering-compatibility)). Per-entity GPU buffers grow dynamically, so entity count is not capped by a fixed ceiling.
- **Custom Allocators**: Uses mimalloc for a fast global allocator implementation, as well as several special-purpose local allocators.
- **Platform Compatibility**: Built and tested for full feature parity on Linux, macOS, and Windows.
- **Networking**: Built-in networking support for p2p or authoritative server.


### Getting this Repo

Make sure to use `--recursive` to fetch all submodules!

```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

Set `git config submodule.recurse true`
or
Run `git submodule update --init --recursive` after every pull to keep dependencies in sync.

The pinned revisions are mirrored in `flake.nix`, and the Nix dev shells warn on entry when the repo's recorded pointers disagree with the flake pins.


### Installation

We use Nix. Get it from: https://nix.dev/install-nix.html

Turn on flakes:
```bash
# /etc/nix/nix.conf (or ~/.config/nix/nix.conf)
experimental-features = nix-command flakes
```

Then run:
```bash
nix build
```

This produces the full Vulkan renderer (Release + ThinLTO) at `./result/bin/anopticengine`. Nix fetches the toolchain, pinned submodules, shader compiler, and public asset pack. Targets are named build type first:

- `nix build .#debug` — Debug renderer, validation layers on.
- `nix build .#release-headless` (alias `.#headless`) — the server build: no renderer, GPU, or display.
- `nix build .#release-wsl` — cross-built Windows renderer `.exe` (see [Building for Windows with WSL](#building-for-windows-with-wsl)).
- `nix build .#<type>[-headless]-<platform>-<arch>[-wayland|-x11]` — any permutation, e.g. `.#release-linux-x64-x11`.
- `nix build .#tests-headless` — CTest suite in the sandbox (`.#tests-asan`, `.#tests-tsan`, `.#tests-full` on Linux). `nix flake check` runs all host suites.

`nix run [-- N]` builds in-tree and runs the result: it checks submodule pointers against the flake pins (halts on drift), fetches anything missing, stages assets, and runs `./build.sh N` in the dev shell. Profiles `1|2` then launch the renderer, `3` launches the headless console engine (WSL included); test profiles run their CTest suite instead. Bare `nix run` is `build.sh 1` — Release into `build/Release/`, then the window.

`nix develop` opens the dev shell; `nix develop .#windows` is the MinGW-w64 cross shell.

Private assets instead of the public pack:
```bash
nix build --override-input anoptic-assets git+ssh://git@github.com/Anoptic-Games/assets
```
`nix run` tries the private repo and falls back to the public pack.

Native Windows has no Nix; that is the `build.bat` path below.

**Without Nix** (native Windows is this path — see [Building on Windows](#building-on-windows)): `clang 17+`, `CMake 3.29+`, `Ninja`, `glslc`, and the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)(1.3.2+). Headless builds (`-DANOPTIC_HEADLESS=ON`) need only clang + CMake + Ninja.

The engine is C23. Clang/LLVM (linking through `lld`) is the default toolchain on every OS; the Nix environments pin the latest release (currently 22). GCC is the supported fallback (MinGW gcc in the Nix cross build, and the `gcc-*` platform files, which link through `mold` where available).

> macOS without Nix: `build.sh` uses Homebrew LLVM (`brew install llvm`) — Apple Clang rejects C23.

For editor integration the engine uses `clangd` (shipped with the LLVM toolchain above).
See [Editor Setup](#editor-setup).



### Building

Each platform has its own build script: `build.sh` (Linux/macOS) and `build.bat` (Windows). Run it with no arguments and it prints the available build profiles. The rundown:

- **Release** — the optimized (`-O3` + ThinLTO) engine build.
- **Debug** — debug build, Vulkan validation layers on.
- **Tests** — Debug build + the full CTest suite.
- **Headless** — Release engine without the renderer; the console/server entry point (profile 3).
- **Sanitizer tests** — the same suite under AddressSanitizer/UBSan or ThreadSanitizer.
- **Headless** — core + tests with the renderer disabled entirely.
- **Release tests** — the CTest suite at `-O3` for benchmarks.

These map to the following CMake options, which can also be passed directly:
`-DANOPTIC_TESTS=ON` (build the test suite),
`-DANOPTIC_HEADLESS=ON` (omit the renderer, skips the Vulkan probe),
`-DANOPTIC_SANITIZE=asan|tsan` (instrument test builds).

Anoptic never builds incrementally: the scripts run the `ano_scrub` target before every build, deleting every object file so all C recompiles from scratch each time.

Output goes to `build/<label>/`; shaders and assets from `assets/` are staged next to the binaries by CMake itself (so direct `cmake` invocations get them too, not just the scripts).

`assets/` is gitignored.

**Shaders** are compiled to SPIR-V automatically at build time via `glslc` (the `anoptic_shaders` target) into `build/<label>/resources/shaders/`.
`resources/shaders/compile.sh` regenerates those committed fallbacks by hand.

`cmake --install` produces a self-contained tree: `bin/anopticengine` and `bin/resources/shaders/`.


#### Building on Windows

Make sure you have `CMake` installed and in your path: https://cmake.org/install/.

Have a copy of the `Mingw-w64` toolkit in your path.
We recommend [MSYS2](https://www.msys2.org/)'s **CLANG64** environment with the
[mingw-w64-clang-x86_64-clang](https://packages.msys2.org/package/mingw-w64-clang-x86_64-clang)
and [mingw-w64-clang-x86_64-ninja](https://packages.msys2.org/package/mingw-w64-clang-x86_64-ninja)
packages (`clang64\bin` — this is what `build.bat` looks for). The engine needs the UCRT
C runtime for C11 `timespec_get`; the legacy MINGW64/msvcrt environment will not link.

Additional guidance:
- [Microsoft Documentation](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [CLion Configuration](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html#clang-mingw)

Once Mingw-w64 is installed with `clang` working on your system, run `build.bat` from the repository root; its usage mirrors `build.sh`.

#### Building for Windows with WSL

WSL has no Linux Vulkan driver, so the renderer runs there only as a **Windows** exe.
WSL's in-guest Vulkan devices (Mesa `dozen` and `llvmpipe`) are not supported render
targets. The **headless** build needs no GPU or display at all: `nix build .#headless` /
`build.sh 3` artifacts run fine in WSL, containers, and GPU-less servers, and
`nix run -- 3` builds and launches it in-guest (`build.sh 4` is the same core, Debug, plus
its test suite). That is the intended "server build" for hosting.

Two ways to build a Windows renderer exe from WSL:

1. **Nix** (`nix build .#release-wsl`) — the MinGW-w64 (gcc, ucrt64) toolchain, Vulkan import lib, and glslc come from Nix; no MSYS2 or Windows Vulkan SDK. The static exe and shaders land in `./result/bin/`. `nix develop .#windows` opens the cross shell for interactive builds; it exports the cross setup as `$cmakeFlags`, so do **not** pass the `cmake/platforms/*-mingw.cmake` toolchain files (those are the MSYS2 path):

   ```bash
   nix develop .#windows
   cmake $cmakeFlags -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build/Windows
   cmake --build build/Windows
   build/Windows/anopticengine.exe      # runs on the Windows host via interop
   ```

2. **MSYS2 clang + Windows Vulkan SDK** (no Nix) — the `build.bat` path:

   ```bash
   cmd.exe /c build.bat 1                                  # -> build\Release\anopticengine.exe
   ( cd build/Release && ./anopticengine.exe )
   ```

   Windows binaries link `-static`, so the exe is self-contained — no toolchain
   runtime DLLs to stage or put on `PATH` (only `vulkan-1.dll`, which the driver/SDK owns).

#### Building on Linux

```bash
nix develop --command ./build.sh 1     # or: nix run
```

The default shell provides clang/lld (currently 22), cmake, ninja, glslc + glslangValidator, lldb, llvm-ar, Vulkan headers, loader, and validation layers, and the X11 + Wayland client libraries. Renderer builds compile both window backends and select at runtime; the single-backend `-wayland`/`-x11` packages are explicit targets. For GPU-less test runs, point `VK_ICD_FILENAMES` at `$ANO_LAVAPIPE_ICD` (exported by the shell).

Without Nix: install `clang 17+`, `CMake`, `Ninja`, `glslc`, and the distro's Vulkan SDK, then run `./build.sh` directly.

#### Building on macOS

Apple Silicon. Vulkan runs through MoltenVK; the Nix packages bake in the ICD path, and the dev shell exports `VK_ICD_FILENAMES`.

```bash
nix build                              # store artifact -> ./result/bin/anopticengine
nix develop --command ./build.sh 1     # in-tree -> build/Release/   (or: nix run)
```

Without Nix: Homebrew LLVM (see the note above) and the Vulkan SDK. `build.sh` finds `brew --prefix llvm` automatically.


### Editor / LSP Setup

The engine uses `clangd` as its language server. 

Install `clangd`.

> Fresh-clone note: there is no `build/` until you build (it is gitignored), 
> So you need to have built the engine once before `clangd` resolves anything.

On VSCode, install the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).

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

### Tests

The Tests profile of the build script builds and runs the full suite via CTest. The suite covers the platform layer (`anoptic_time`, `anoptic_logging`, `anoptic_memory`, the `anotest_logfuzz` logger fuzzer, plus the `anoptic_blackbox` crash-handler suite, which re-execs itself and dies once per scenario), the mesh pipeline (`anoptic_meshoptimizer`), the logic/render transport (`anoptic_render_bridge`, `anoptic_render_slots`), and the Vulkan backend (`anotest_vk_lifecycle`, `anotest_vk_components`, `anotest_vk_compliance_layers`, `anotest_vk_memory`, `anotest_vk_sync`). The Vulkan tests create a real device, so they need a Vulkan-capable driver (a software rasterizer such as lavapipe is sufficient). To run a subset directly:

```bash
ctest --test-dir build/Tests --output-on-failure -R anotest_vk
```

The sanitizer profiles run the same suite under AddressSanitizer or ThreadSanitizer, and the Headless profile skips the renderer entirely. The sanitizer profiles are Linux/macOS-only: MinGW clang on Windows supports neither TSan nor a working ASan against ucrt. 
The logger benchmark (`anotest_logbench`) and the allocator easter egg (`anotest_chariots`) are built but disabled in CTest; run them by hand, from a Release-tests build — Debug numbers are ~2x pessimistic.

### Profiling

The switch to a multi-queue async approach is likely the cause of an Nsight deadlock error.
To capture frame traces, try ANO_FORCE_NO_ASYNC_HIZ=1 ANO_FORCE_NO_ASYNC_LC=1 ANO_FORCE_NO_ASYNC_TEXT=1.

### Rendering Compatibility

The renderer is GPU-driven: a compute pass animates transforms, a compute culling pass
frustum-tests every entity and builds an indirect draw list, and the geometry is drawn
from a shared vertex/meshlet mega-buffer.

- **Mesh path (default):** on devices exposing `VK_EXT_mesh_shader`, the cull pass emits
  `VkDrawMeshTasksIndirectCommandEXT`s and a mesh shader expands meshlets on the GPU.
- **Fallback path (automatic):** on devices without the extension — many pre-2019 GPUs,
  older integrated graphics, software rasterizers — the cull pass instead emits
  `VkDrawIndexedIndirectCommand`s and a vertex shader (`flat.vert`) renders the same
  geometry via classic indexed indirect draws. Resource handling, culling, materials,
  lighting, and the fragment shaders are shared verbatim between both paths.

Path selection is automatic at device-creation time. For testing, set the environment
variable `ANO_FORCE_NO_MESH_SHADER=1` to force the fallback path even on mesh-capable
hardware:

```bash
ANO_FORCE_NO_MESH_SHADER=1 ./build/Debug/anopticengine
```

The startup log prints which path is active, e.g. `Enabling 3 device extensions (mesh shader: yes)`. See the Rendering Philosophy section of `docs/notes.md` for the full design.

#### Device selection

The engine ranks suitable Vulkan devices by **mesh-shader capability first**, then
largest DEVICE_LOCAL (video) memory, discrete over integrated. The winner is logged as
`Selected device: <name>`.

Capability-first matters on machines with *layered* Vulkan drivers, e.g. Mesa's `dozen`
(Vulkan-on-D3D12, installed on Windows by the "OpenCL, OpenGL, and Vulkan Compatibility
Pack"). These enumerate alongside the native driver, can claim more memory than the
real GPU, and typically lack `VK_EXT_mesh_shader`.

Set `ANO_DEVICE` to a caseless substring of a device name (as printed at startup) to
override the choice:

```bash
ANO_DEVICE=nvidia ./build/Release/anopticengine        # pin the NVIDIA adapter
ANO_DEVICE=direct3d12 ./build/Release/anopticengine    # deliberately test a layered one
```

If nothing matches, the engine warns and selects automatically.

### More

Check out the `.md` files in each subdirectory for more information on a given module.
