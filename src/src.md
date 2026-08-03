# Source Directory

`src/` is the engine implementation root. `main.c`, the entry point, lives here.
(This is subject to change as the engine expands and the entry point for a final game build is moved to the games themselves, with the engine core becoming a dynamic library).

Other subdirectories are modules.

## Directory Structure

Each module follows the platform-abstraction convention (see `docs/docs.md`):
public interface in `include/anoptic_<mod>.h` (`ano_*()` + platform-agnostic types only);
`src/<mod>/` holds a common `<mod>.c` plus, where needed, per-platform files
(`<mod>_linux.c` / `<mod>_win64.c` / `<mod>_macos.c`) selected by the module's
`CMakeLists.txt`. Callers include the public header and call `ano_*()`.

The current layout:

```
src/
├── meta/           # Header-only C++26 reflection and compile-time value contracts
├── engine/         # Entry point (main.c): runs the render world on the main thread, spawns the logic master
├── render_bridge/  # Logic <-> render boundary: lock-free SPSC command/event rings
├── vulkan_backend/ # GPU-driven Vulkan renderer (render master thread); owns GPU slots + all GLFW
├── render/         # Asset-facing render support: glTF loader (gltf/), text/font stack (text/)
├── mesh/           # clean-room mesh ops: meshlet decomposition, vertex-cache opt, LOD simplify
├── audio/          # Lock-free mixer thread + DSP library + per-platform device backends
├── synth/          # The synthesizer: music IR -> voices -> the audio module's bus graph
├── music/          # The composer: decides each bar (see music/ANOPTIC_MUSICGEN.md)
├── memory/         # Aligned allocation + mimalloc integration (per-platform)
├── threads/        # Thread / mutex / condvar / atomics abstraction over pthreads + Win32
├── time/           # High-resolution monotonic timing and OS-scheduled sleeps
├── strings/        # Owned string type experiments and scoped-heap tests
├── log/            # Async queue-based logger + crash blackbox: fatal-signal/SEH hooks, session CRASH-log record, hail-mary log flush
└── filesystem/     # Path and file I/O abstraction (per-platform)
```

## Purpose of Each Subdirectory

- `meta/` (public `include/anoptic_meta.h`): Header-only C++26 structural reflection, enum-domain proofs, strong plain-data concepts, and compile-time value contracts shared across modules. It owns no runtime behavior.

- `audio/` (public `include/anoptic_audio.h`): Mixer thread owns every audio structure (sources, buses, insert chains, sends). Structural change arrives as commands at block boundaries over lock-free rings; telemetry and listener pose ride published double buffers. Device backends are per-platform and hand-rolled, cascading to a null device when none opens. A `generator` seam lets another module render into the bus mixes; the synth is the one that does.

- `synth/` (public `include/anoptic_synth.h`): Voices, patches, beat clock, deadline-sorted schedule. Renders music IR into the audio buses through that seam. Hosts a live music engine: attach one and the generator composes ahead of the playhead every block, with steering, per-bar musical meaning, and seek riding the audio bridge.

- `music/` (public `include/anoptic_music.h`): Composer: harmony, form, motifs, dramaturgy, one generator per layer, bit-exact against a Python oracle. Knows nothing of audio, threads, or devices; emits notes. See `music/ANOPTIC_MUSICGEN.md`.

- `engine/`: Process entry. `main.c` runs the render world (Vulkan + GLFW) on the main thread (GLFW pins window/event handling there; mandatory on macOS). Calls `initVulkan`, spawns the logic/ECS master (`anoLogicThreadMain`) as sole producer of render commands over the bridge, then drives `glfwPollEvents` + `drawFrame` until the window closes.

- `render_bridge/` (private `render_bridge.h`; public command protocol in
  `include/anoptic_render.h`): One-way-each-direction boundary between logic and render. Two bounded lock-free SPSC rings carry `RenderCommand`s (logic -> render) and `RenderEvent`s (render -> logic). Also defines the logic-side `DisplayState` projection and the command/event protocol. (The logic-side ECS that will produce these was removed pending a proper rebuild; see `docs/notes.md`.)

- `vulkan_backend/` (renderer contract in `include/anoptic_render.h`): GPU-driven, meshlet-based renderer on the render master thread. Sole authority over GPU memory and the physical slot space (private `render_slots.h`: logical `render_id` -> GPU slot, stable slots with holes, frame-gated reuse). Drains the bridge; grows slot-indexed GPU buffers on demand. Owns all GLFW (window + event pump).

- `render/`: Asset-facing render support for the backend: glTF loader (`gltf/`), FreeType/SDF text stack (`text/`).

- `mesh/` (`ano_meshoptimizer.h`): Clean-room reimplementation of the meshoptimizer algorithms (no library linked): vertex-cache optimization, meshlet + bounds decomposition for the GPU geometry pool, and quadric-error edge-collapse simplification (`ano_simplify`) for LOD chain production.

- `memory/` (`anoptic_memory.h`, C++ extension `anoptic_memory_typed.h`): Aligned allocation primitives, overflow-safe typed allocation, hardware interference constants (`ANO_CACHE_LINE` / `ANO_THREAD_LINE`), and mimalloc integration for arenas and thread-local heaps.

- `threads/` (`anoptic_threads.h`): Platform-agnostic threads, mutexes, condition variables, spinlocks, barriers, and TLS over pthreads / Win32. Spawn shim arms each new thread's crash stack via `ano_log_crash_thread_arm` (see `log/`).

- `time/` (`anoptic_time.h`): Emulator-grade monotonic timestamps and precise sleep/busy-wait.

- `strings/` (`anoptic_strings.h`): Owned-string-type work and scoped-heap experiments.

- `log/` (`anoptic_log.h`, `anoptic_log_crash.h`): Asynchronous, queue-based logger (hot-path enqueue, cold-path flush)

- `anoptic_log_crash.h` Crash handling. The blackbox hooks fatal signals
  (POSIX) and unhandled SEH exceptions + SIGABRT (Windows), writes an async-signal-safe
  record (signal, fault address, backtrace) to the session's `logs/<stamp>_CRASH.log` (path
  pre-resolved at init, stamp shared with the logger), then gives the logger one last flush
  before re-raising. A deadman guarantees the process exits instead of hanging. Per-thread
  crash stacks (sigaltstack / SetThreadStackGuarantee) arm via `ano_log_crash_thread_arm`,
  called automatically by `ano_thread_create`, so a blown stack reports on any engine thread.

- `filesystem/` (`anoptic_filesystem.h`): Path handling and file I/O, per platform.

Modules that are still aspirational (audio, physics, input, scripting) will appear here
as they are built; see `docs/notes.md` for the architecture and build sequence.


## Usage

`src/` is internal implementation. Games call the public API in `include/`. Put component code in its matching subdirectory.
