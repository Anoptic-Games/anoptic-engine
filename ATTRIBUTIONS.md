## Third-Party Attributions

This project makes use of the following third-party libraries and SDKs. Layout and integration notes: `external/external.md`.

### GLFW - v3.4
- **Description**: Multi-platform library for creating windows, Vulkan/OpenGL contexts, and managing input.
- **Source**: [GLFW GitHub Repository](https://github.com/glfw/glfw)
- **Path**: `external/glfw` (git submodule)
- **License**: [Zlib License](https://github.com/glfw/glfw/blob/master/LICENSE.md)

### FreeType - VER-2-13-3 line
- **Description**: Font parsing and outline access for the text stack (`include/anoptic_text.h` / `src/text/`).
- **Source**: [FreeType](https://freetype.org/) / [GitHub](https://github.com/freetype/freetype)
- **Path**: `external/freetype` (git submodule)
- **License**: [FreeType License (FTL)](https://github.com/freetype/freetype/blob/master/docs/FTL.TXT) (also offered under GPLv2; we use FTL)

### mimalloc - v2.3.2
- **Description**: Compact general-purpose allocator (heaps, aligned alloc, global override).
- **Source**: [mimalloc GitHub Repository](https://github.com/microsoft/mimalloc)
- **Docs**: [mimalloc Documentation](https://microsoft.github.io/mimalloc/)
- **Path**: `external/mimalloc` (git submodule)
- **License**: [MIT License](https://github.com/microsoft/mimalloc/blob/master/LICENSE)

### cgltf - historical v1.15 provenance
- **Description**: The previous glTF loader and differential oracle. The first-party `anogltf` implementation retains cgltf-derived tokenizer and compatibility behavior.
- **Source**: [cgltf GitHub Repository](https://github.com/jkuhlmann/cgltf)
- **Path**: Former submodule removed; derived portions and the complete notice are retained in `include/anogltf.h`.
- **License**: [MIT License](https://github.com/jkuhlmann/cgltf/blob/master/LICENSE), reproduced in `include/anogltf.h`.

### stb (stb_image.h v2.30)
- **Description**: Single-file public-domain / MIT libraries for C/C++. We vendor `stb_image` for textures.
- **Source**: [stb GitHub Repository](https://github.com/nothings/stb)
- **Path**: `external/stb` (vendored)
- **License**: [MIT License | Public Domain](https://github.com/nothings/stb/blob/master/LICENSE)

### jsmn
- **Description**: Minimalistic JSON parser in C.
- **Source**: [jsmn GitHub Repository](https://github.com/zserge/jsmn)
- **Path**: `external/jsmn` (vendored header)
- **License**: MIT (see license block in `external/jsmn/jsmn.h`)

### Vulkan SDK
- **Description**: Runtime libraries, headers, and validation layers for building Vulkan applications (system install, not under `external/`).
- **SDK**: [LunarG Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)
- **Khronos Docs**: [Vulkan-Docs](https://github.com/KhronosGroup/Vulkan-Docs)
- **Licenses**: [Khronos Spec Copyright / licenses](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/LICENSES)

### Platform audio (not vendored)
Device backends use OS APIs (PipeWire/ALSA, WASAPI/DirectSound, CoreAudio). Not third-party source trees under `external/`; see `docs/AUDIO_PLAN.md` and `docs/references/` for ABI notes.
