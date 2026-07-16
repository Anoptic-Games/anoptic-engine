# External Directory

The `external/` directory stores third-party source libraries and dependencies for the Anoptic Game Engine. Keep attributions in root `ATTRIBUTIONS.md` in sync when adding or bumping anything here.

## Directory Structure

```
external/
├── cgltf/          # glTF 2.0 parsing (submodule)
├── freetype/       # Font outlines for the text stack (submodule)
├── glfw/           # Windowing + input (submodule) — currently v3.4
├── jsmn/           # Minimal JSON parser (vendored header)
├── stb/            # Single-header libs (vendored; stb_image.h v2.30)
├── mimalloc/       # Allocator (submodule) — currently v2.3.2
└── external.md     # This file
```

Not under `external/`: Vulkan SDK (system), platform audio APIs (PipeWire/ALSA, WASAPI/DirectSound, CoreAudio), first-party meshlet/simplify code in `src/mesh/`.

## Purpose of Each Subdirectory

- **`cgltf/`**: [cgltf](https://github.com/jkuhlmann/cgltf) — single-header glTF 2.0 loader/writer. Submodule.
- **`freetype/`**: [FreeType](https://freetype.org/) — font face/outline access for `src/text/`. Submodule (FTL).
- **`glfw/`**: [GLFW](https://www.glfw.org/) — windows, Vulkan surfaces, input. Submodule.
- **`jsmn/`**: [jsmn](https://github.com/zserge/jsmn) — vendored `jsmn.h` (MIT in-header).
- **`stb/`**: [stb](https://github.com/nothings/stb) — vendored single-file headers (we use `stb_image`).
- **`mimalloc/`**: [mimalloc](https://github.com/microsoft/mimalloc) — heaps + global override via `anoptic_memory.h`. Submodule.

## Usage

Prefer public engine headers (`include/anoptic_*.h`) over reaching into `external/` from game/engine code. Modules that must include third-party headers do so from their `src/<module>/` TUs / CMake include dirs (e.g. cgltf from render, FreeType from text).

Typical include style when a module is allowed to see the vendor header:
```c
#include <jsmn.h>
#include <stb_image.h>
```

## Git Submodules

Submodules today: `glfw`, `freetype`, `mimalloc`, `cgltf`. Vendored (not submodules): `jsmn`, `stb`.

Procedure:
- **Adding a Submodule**:
  `git submodule add [repository_url] external/[library_name]`
- **Updating a Submodule**:
  `git submodule update --remote external/[library_name]` (or checkout a tag inside the submodule and commit the pointer)
- **Cloning with Submodules**:
  `git clone --recursive [project_url]`
  or after clone: `git submodule update --init --recursive`

## CMake Integration

Root `CMakeLists.txt` already wires the common set (`add_subdirectory` for mimalloc/freetype/glfw, include path for cgltf, link `freetype` / `glfw` / `mimalloc-static` as appropriate). New libraries:

1. `add_subdirectory(${CMAKE_SOURCE_DIR}/external/[library_name])` (or header-only include dirs)
2. `target_link_libraries(... [library_target])` on the right engine target (`anoptic_core` vs `anoptic_render`)

## Licensing

When integrating or bumping a library, verify the license and update root `ATTRIBUTIONS.md` (version, path, license link).
