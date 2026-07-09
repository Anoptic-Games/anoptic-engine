# Source Directory

The `src/` directory is the root directory of the game engine's source code.

``main.c``, the entry point of the engine, is here. 
(This is subject to change as the engine expands and the entry point for a final game build is moved to the games themselves, with the engine core becoming a dynamic library).

Other subdirectories within ``src/`` are for the various submodules

## Directory Structure

Each module follows the platform-abstraction convention (see `docs/docs.md`):
a public interface lives in `include/anoptic_<mod>.h` (exposing only `ano_*()`
declarations and platform-agnostic types), while `src/<mod>/` holds the
implementation — a common `<mod>.c` plus, where needed, per-platform files
(`<mod>_linux.c` / `<mod>_win64.c` / `<mod>_macos.c`) selected by the module's
`CMakeLists.txt`. Callers only ever include the public header and call `ano_*()`.

The current layout:

```
src/
├── engine/         # Entry point (main.c): runs the render world on the main thread, spawns the logic master
├── render_bridge/  # Logic <-> render boundary: lock-free SPSC command/event rings
├── vulkan_backend/ # GPU-driven Vulkan renderer (render master thread); owns GPU slots + all GLFW
├── render/         # Asset-facing render support: glTF loader (gltf/), text/font stack (text/)
├── mesh/           # clean-room mesh ops: meshlet decomposition, vertex-cache opt, LOD simplify
├── memory/         # Aligned allocation + mimalloc integration (per-platform)
├── threads/        # Thread / mutex / condvar / atomics abstraction over pthreads + Win32
├── time/           # High-resolution monotonic timing and OS-scheduled sleeps
├── strings/        # Owned string type experiments and scoped-heap tests
├── logging/        # Async queue-based logger
├── blackbox/       # Crash handler: fatal-signal/SEH hooks, CRASH.log record, hail-mary log flush
└── filesystem/     # Path and file I/O abstraction (per-platform)
```

## Purpose of Each Subdirectory

- `engine/`: The process entry point. `main.c` runs the render world (all Vulkan +
  GLFW) directly on the main thread — GLFW pins window/event handling to the process
  main thread, mandatory on macOS. It calls `initVulkan`, spawns the logic/ECS master
  (`anoLogicThreadMain`) as the sole producer of render commands over the bridge, then
  drives the frame loop (`glfwPollEvents` + `drawFrame`) until the window closes.

- `render_bridge/` (private `render_bridge.h`; public command protocol in
  `include/anoptic_render.h`): The one-way-each-direction boundary between the logic and
  render worlds. Two bounded lock-free SPSC rings carry `RenderCommand`s (logic -> render)
  and `RenderEvent`s (render -> logic). Also defines the logic-side `DisplayState`
  projection and the command/event protocol. (The logic-side ECS that will produce these
  was removed pending a proper rebuild; see `docs/notes.md`.)

- `vulkan_backend/` (renderer contract in `include/anoptic_render.h`): The GPU-driven, meshlet-based renderer, run
  entirely on the render master thread. It is the sole authority over GPU memory and
  the physical slot space (private `render_slots.h`: logical `render_id` -> GPU slot,
  stable slots with holes, frame-gated reuse), drains the bridge, and grows the
  slot-indexed GPU buffers on demand. Owns all GLFW (window + event pump).

- `render/`: Higher-level render support that feeds the backend — the glTF model
  loader (`gltf/`) and the FreeType/SDF text stack (`text/`).

- `mesh/` (`ano_meshoptimizer.h`): Clean-room reimplementation of the meshoptimizer
  algorithms (no library linked): vertex-cache optimization, meshlet + bounds decomposition
  for the GPU geometry pool, and quadric-error edge-collapse simplification (`ano_simplify`)
  for LOD chain production.

- `memory/` (`anoptic_memory.h`): Aligned allocation primitives, the hardware
  interference constants (`ANO_CACHE_LINE` / `ANO_THREAD_LINE`), and the mimalloc
  integration that backs the engine's arenas and thread-local heaps.

- `threads/` (`anoptic_threads.h`): Platform-agnostic threads, mutexes, condition
  variables, spinlocks, barriers, and TLS over pthreads / Win32. The spawn shim arms
  each new thread's crash stack via `ano_blackbox_thread_arm` (see `blackbox/`).

- `time/` (`anoptic_time.h`): Emulator-grade monotonic timestamps and precise
  sleep/busy-wait.

- `strings/` (`anoptic_strings.h`): Owned-string-type work and scoped-heap experiments.

- `logging/` (`anoptic_logging.h`): Asynchronous, queue-based logger (hot-path enqueue,
  cold-path flush).

- `blackbox/` (`anoptic_blackbox.h`): The crash blackbox. Hooks fatal signals (POSIX) and
  unhandled SEH exceptions + SIGABRT (Windows), writes an async-signal-safe record (signal,
  fault address, backtrace) to CRASH.log, then gives the logger one last flush before
  re-raising. A deadman guarantees the process exits instead of hanging. Per-thread crash
  stacks (sigaltstack / SetThreadStackGuarantee) arm via `ano_blackbox_thread_arm`, called
  automatically by `ano_thread_create`, so a blown stack reports on any engine thread.

- `filesystem/` (`anoptic_filesystem.h`): Path handling and file I/O, per platform.

Modules that are still aspirational (audio, physics, input, scripting) will appear here
as they are built; see `docs/notes.md` for the architecture and build sequence.


## Usage

The `src/` directory contains the implementation of the game engine. Each subdirectory corresponds to a major component of the engine, and the organization of these directories and files should help to keep the codebase clean and understandable. Code related to a specific component of the engine should be placed in the corresponding subdirectory.

While the source code in `src/` comprises the implementation details of the engine, the game should primarily interface with the engine through the public API in the `include/` directory. The code in `src/` should be thought of as "internal" to the engine, while the headers in `include/` expose a public, stable API for games to use.
