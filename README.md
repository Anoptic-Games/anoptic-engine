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
- **Platform Compatibility**: Built and tested for full feature parity on both Linux and Windows.
- **Networking**: Built-in networking support for p2p or authoritative server.

### Installation

All builds of Anoptic Engine require `clang 17+`, `CMake`, `glslc`, and the `Vulkan SDK`.
The engine is C23 and is compiled exclusively with Clang; other toolchains are not supported.

Acquire a copy of the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/), version 1.3.2 or later.

> macOS note: `build.sh` uses Homebrew LLVM (`brew install llvm`), since Apple Clang rejects C23.

For editor integration the engine uses `clangd` (shipped with the LLVM toolchain above).
See [Editor Setup](#editor-setup).

### Building

Both `build.sh` (Linux/macOS) and `build.bat` (Windows) take a single numeric argument
selecting the build profile. Output goes to `build/<label>/`, and assets from `assets/`
are copied there automatically.

| Arg | Label        | Description                                              | Output dir         |
|-----|--------------|----------------------------------------------------------|--------------------|
| `1` | Release      | Optimized build                                          | `build/Release/`   |
| `2` | Debug        | Debug build (validation layers on)                       | `build/Debug/`     |
| `3` | Tests        | Debug build + run the CTest suite                        | `build/Tests/`     |
| `4` | Tests-ASan   | Tests with AddressSanitizer + UBSan                      | `build/Tests-ASan/`|
| `5` | Tests-TSan   | Tests with ThreadSanitizer                               | `build/Tests-TSan/`|
| `6` | Headless     | Core + tests with the Vulkan renderer disabled           | `build/Headless/`  |

```bash
./build.sh 2        # Debug build
./build.sh 3        # build + run all tests
```

These map to the following CMake options, which can also be passed directly:
`-DANOPTIC_TESTS=ON` (build the test suite), `-DANOPTIC_HEADLESS=ON` (omit the renderer,
skips the Vulkan probe — useful for CI without a GPU), and
`-DANOPTIC_SANITIZE=asan|tsan` (instrument test builds).

**Shaders** are compiled to SPIR-V automatically at build time via `glslc` (the
`anoptic_shaders` target), so `.spv` files never silently desync from their source. If
`glslc` is not found, the committed `resources/shaders/*.spv` are used as-is (and may be
stale). `resources/shaders/compile.sh` remains available for compiling them by hand.

#### Building on Windows

Make sure you have `CMake` installed and in your path: https://cmake.org/install/.

Have a copy of the `Mingw-w64` toolkit in your path.
We recommend [MSYS2](https://www.msys2.org/) with the [mingw-w64-x64_64-clang](https://packages.msys2.org/package/mingw-w64-x86_64-clang) package. 

Additional guidance:
- [Microsoft Documentation](https://learn.microsoft.com/en-us/vcpkg/users/platforms/mingw)
- [CLion Configuration](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html#clang-mingw)

Once Mingw-w64 is installed with `clang` working on your system, run `build.bat <arg>`
from the repository root using the same argument table above.

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

`./build.sh 3` builds and runs the full suite via CTest. The suite covers the platform
layer (`anoptic_time`, `anoptic_logging`), the mesh pipeline (`anoptic_meshoptimizer`),
and the Vulkan backend (`anotest_vk_lifecycle`, `anotest_vk_components`,
`anotest_vk_compliance_layers`, `anotest_vk_memory`, `anotest_vk_sync`). The Vulkan tests
create a real device, so they need a Vulkan-capable driver (a software rasterizer such as
lavapipe is sufficient). To run a subset directly:

```bash
ctest --test-dir build/Tests --output-on-failure -R anotest_vk
```

Use profiles `4`/`5` to run the same suite under AddressSanitizer or ThreadSanitizer, and
`6` for a headless build that skips the renderer entirely.

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

The startup log prints which path is active, e.g. `Enabling 3 device extensions (mesh
shader: yes)`. See `PLANS_COMPATIBILITY.md` for the full design.

### Architecture

The engine runs the simulation and the renderer as **two parallel worlds on
separate threads**, connected by two bounded lock-free SPSC rings:

- **Logic / ECS master** (the `main` thread): the authoritative world. Holds entities
  (generational handles) and components (chunked sparse-set stores), and is the *sole
  producer* of render commands.
- **Render master** (its own thread): a non-authoritative view that owns all Vulkan
  state, all GLFW windowing, and the physical GPU slot space. It is the *sole consumer*
  of commands and the *sole producer* of events (e.g. slot-retirement notifications).

The logic world names each renderable by a stable logical `render_id`; the renderer
privately maps `render_id -> GPU slot`, so it can place and grow GPU data without the
logic world ever seeing a slot. Only **discrete** transitions cross the bridge (spawn,
despawn, teleport, mesh/material swap, light change). **Continuous** GPU-parameterized
motion (orbit/spin) is sent once as parameters and animated entirely on the GPU, so it
costs zero per-frame bridge traffic. Per-entity GPU buffers grow on demand in
chunk-aligned steps, dropping any fixed entity ceiling.

The full design lives in `docs/artifacts/ECS.md` (logic side) and
`docs/artifacts/VK_BACKEND_INTEROP.md` (render side); `docs/notes.md` has the broader
architecture and build sequence.

### More

Check out the `.md` files in each subdirectory for more information on a given module.
