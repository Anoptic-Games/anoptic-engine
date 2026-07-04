## Anoptic Game Engine

The Anoptic Game Engine is designed to create games that can handle large numbers of events and entities occurring simultaneously. It is tailored for 4X, RTS, or any other kind of complex simulation.

### Getting this Repo

Make sure to use `--recursive` to fetch all submodules!

```bash
git clone --recursive https://github.com/Anoptic-Games/anoptic-engine.git
```

### Runtime Features

- **ECS**: Generational entity handles over chunked sparse-set component stores, built to manage large numbers of entities with dynamic properties and behaviors.
- **Parallel logic/render worlds**: The simulation (logic/ECS master thread) and the renderer (its own thread) run as two parallel worlds joined by lock-free single-producer/single-consumer rings. The logic side streams only *discrete* state transitions; continuous, GPU-parameterized motion is sent once and never restreamed. See [Architecture](#architecture).
- **Events**: Enable interactions between different systems via a generalized event bus.
- **Vulkan Renderer**: GPU-driven Vulkan backend, run on a dedicated render thread that owns all GPU resources and the renderable slot space. Meshlet rendering via `VK_EXT_mesh_shader` on modern hardware, with an automatic vertex-shader fallback for devices that lack the extension (see [Rendering Compatibility](#rendering-compatibility)). Per-entity GPU buffers grow dynamically, so entity count is not capped by a fixed ceiling.
- **Custom Allocators**: Uses mimalloc for a fast global allocator implementation, as well as several special-purpose local allocators.
- **Platform Compatibility**: Built and tested for full feature parity on Linux, macOS, and Windows.
- **Networking**: Built-in networking support for p2p or authoritative server.

### Installation

Renderer builds of Anoptic Engine require `clang 17+`, `CMake`, `glslc`, and the `Vulkan SDK`.
Headless builds (`-DANOPTIC_HEADLESS=ON`, and the Nix packages below) need only clang + CMake.
The engine is C23 and is developed with Clang; the Nix Windows cross shell uses MinGW gcc.

Acquire a copy of the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/), version 1.3.2 or later.

> macOS note: `build.sh` uses Homebrew LLVM (`brew install llvm`), since Apple Clang rejects C23.

For editor integration the engine uses `clangd` (shipped with the LLVM toolchain above).
See [Editor Setup](#editor-setup).

### Building

Each platform has its own build script: `build.sh` (Linux/macOS) and `build.bat` (Windows). Run it with no arguments and it prints the available build profiles. The rundown:

- **Release** — the optimized (`-O3`) engine build.
- **Debug** — debug build, Vulkan validation layers on.
- **Tests** — Debug build + the full CTest suite.
- **Sanitizer tests** — the same suite under AddressSanitizer/UBSan or ThreadSanitizer.
- **Headless** — core + tests with the renderer disabled entirely.
- **Release tests** — the CTest suite at `-O3`; the one to use for benchmarks.

Output goes to `build/<label>/`; shaders and assets from `assets/` are staged next to the
binaries by CMake itself (so direct `cmake` invocations get them too, not just the scripts).
`assets/` is gitignored. The demo scene and the Vulkan tests load `viking_room.gltf` (the vulkan-tutorial viking room) and `GlassHurricaneCandleHolder.gltf` (Khronos [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)) plus their textures from it — populate it before running the engine or the full test suite. The demo scene also loads Sponza as its environment if present at `assets/sponza/2.0/Sponza/glTF/Sponza.gltf` (Khronos [glTF-Sample-Models](https://github.com/KhronosGroup/glTF-Sample-Models)); it is optional — the engine logs a warning and continues without it (the viking room + candles still spawn as props) if it's missing.

These map to the following CMake options, which can also be passed directly:
`-DANOPTIC_TESTS=ON` (build the test suite), `-DANOPTIC_HEADLESS=ON` (omit the renderer,
skips the Vulkan probe — useful for CI without a GPU), and
`-DANOPTIC_SANITIZE=asan|tsan` (instrument test builds).

**Shaders** are compiled to SPIR-V automatically at build time via `glslc` (the
`anoptic_shaders` target) into `build/<label>/resources/shaders/` — next to the binary,
where the engine resolves them relative to its own executable. If `glslc` is not found,
the committed `resources/shaders/*.spv` are staged there as-is (and may be stale).
`resources/shaders/compile.sh` regenerates those committed fallbacks by hand.

`cmake --install` produces a self-contained tree: `bin/anopticengine` plus
`bin/resources/shaders/`. The engine runs from any working directory.

#### Building on Windows

Make sure you have `CMake` installed and in your path: https://cmake.org/install/.

Have a copy of the `Mingw-w64` toolkit in your path.
We recommend [MSYS2](https://www.msys2.org/)'s **CLANG64** environment with the
[mingw-w64-clang-x86_64-clang](https://packages.msys2.org/package/mingw-w64-clang-x86_64-clang)
package (`clang64\bin` — this is what `build.bat` looks for). The engine needs the UCRT
C runtime for C11 `timespec_get`; the legacy MINGW64/msvcrt environment will not link.

Additional guidance:
- [Microsoft Documentation](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [CLion Configuration](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html#clang-mingw)

Once Mingw-w64 is installed with `clang` working on your system, run `build.bat` from the repository root; its usage mirrors `build.sh`.

#### Building under WSL / Nix

The flake is the primary Nix entry point (`shell.nix` remains as a thin legacy shell):

```bash
nix develop --command ./build.sh 6    # Linux clang shell: headless build + non-GPU tests
nix develop --command ./build.sh 5    # ThreadSanitizer (Linux only)
nix build                             # one-shot headless package -> ./result/bin
nix build .#renderer                  # one-shot Vulkan renderer package (binary + shaders)
```

WSL has no Linux Vulkan driver, so the renderer runs there only as a **Windows** exe.
Two ways to build one:

1. **Nix cross shell** (`nix develop .#windows`) — the whole MinGW-w64 (gcc, ucrt64)
   toolchain, Vulkan import lib, and glslc come from Nix; no MSYS2 or Windows Vulkan SDK
   needed. Do **not** pass the `cmake/platforms/*-mingw.cmake` toolchain files here (they
   are for the MSYS2 path); the shell exports the cross setup as `$cmakeFlags`:

   ```bash
   nix develop .#windows
   cmake $cmakeFlags -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build/Windows
   cmake --build build/Windows
   build/Windows/anopticengine.exe      # runs on the Windows host via interop
   ```

2. **MSYS2 clang + Windows Vulkan SDK** (no Nix) — the `build.bat` path:

   ```bash
   cmd.exe /c build.bat 1                                  # -> build\Release\anopticengine.exe
   ( cd build/Release && PATH="/mnt/c/msys64/clang64/bin:$PATH" ./anopticengine.exe )
   ```

   The MSYS2 build needs `clang64\bin` on `PATH` to find its runtime DLLs.

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

The Tests profile of the build script builds and runs the full suite via CTest. The suite covers the platform layer (`anoptic_time`, `anoptic_logging`, `anoptic_memory`, plus the `anotest_logfuzz` logger fuzzer), the mesh pipeline (`anoptic_meshoptimizer`), the logic/render transport (`anoptic_render_bridge`, `anoptic_render_slots`), and the Vulkan backend (`anotest_vk_lifecycle`, `anotest_vk_components`, `anotest_vk_compliance_layers`, `anotest_vk_memory`, `anotest_vk_sync`). The Vulkan tests create a real device, so they need a Vulkan-capable driver (a software rasterizer such as lavapipe is sufficient). To run a subset directly:

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

### More

Check out the `.md` files in each subdirectory for more information on a given module.
