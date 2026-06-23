# Anoptic Engine -- Internal Architecture Notes

## Vision

A simulation engine for a space colony game in the lineage of Dwarf Fortress and Stellaris. Thousand-star systems, each with worlds, stations, millions of asteroids, fleets, populations with individual histories. The kind of game where the simulation is the content and the renderer is just a viewport into it.

The engine exists because no off-the-shelf engine is designed for this workload. Unity and Unreal optimize for rendering complexity with modest entity counts. This engine optimizes for entity count and simulation depth, with rendering as a secondary concern. The target hardware is a modern desktop (Ryzen 9 class CPU, RTX 4090 class GPU) -- a machine with more compute than the top supercomputer of the year 2000, almost entirely wasted by conventional game architectures.

The ambition is ludicrous-scale simulation at interactive framerates. The engine is the foundation; the game is built on top.

## Language: C23

The engine is written in C23, compiled exclusively with Clang 17+. The C23 standard was in beta when development began (2022-2023); compiler coverage is now mature.

C was chosen deliberately over C++ for control, simplicity, and ABI stability. Where C lacks modern conveniences (ownership, scoped cleanup, type-safe generics), the engine uses targeted compiler extensions and C23 features. The philosophy is: know exactly what the machine does, control it explicitly, and use the smallest possible abstraction to make that control ergonomic.

## Core Architectural Principles

### 1. Region-Based Memory Management (Scoped Arenas)

The central insight, arrived at independently and later confirmed by the academic literature (Tofte & Talpin's region inference for ML, Cyclone's explicit regions, and the game-dev arena tradition of Casey Muratori and Ryan Fleury):

**Most allocations in a game frame are scope-shaped.** They are born together, used together, and should die together. Per-object malloc/free is the wrong granularity -- it fragments memory, pollutes the cache, and forces the programmer to track individual lifetimes.

Instead: allocate from a **scoped heap** (a mimalloc local heap), and destroy the entire heap when the scope exits. Setup and teardown cost is O(regions). A gigabyte of frame-scratch data dies in microseconds because `mi_heap_destroy` hands pages back without iterating contents.

The mechanism in C is `__attribute__((cleanup))`:

```c
#define LOCALHEAPATTR __attribute__((__cleanup__(ano_heap_release)))

// Usage: heap is automatically destroyed when it leaves scope.
mi_heap_t *frameHeap LOCALHEAPATTR = mi_heap_new();
```

It's the same mechanism used by systemd (`_cleanup_`), GLib (`g_autoptr`), and the Linux kernel. Given the Clang-only policy, it's fully supported and well-defined.

**The memory hierarchy for the engine (planned):**
- **Process arena**: the OS is the outermost garbage collector. `exit()` is a region free.
- **Level/session arena**: allocated on load, destroyed on unload. Holds the star map, entity pools, persistent simulation state.
- **Frame arena**: reset at the top of every frame. Holds transient computation -- command buffers, intermediate query results, scratch space.
- **Scratch arenas**: short-lived, task-scoped. The glTF parser allocates into a scratch arena, copies survivors out, drops the arena.
- **Pool allocators**: for the genuinely dynamic 10% of allocations whose lifetimes aren't scope-shaped -- entities that spawn and die unpredictably. Free-list or slab allocation within a region.

**Hugepages and mimalloc integration**: The engine uses mimalloc as its global allocator (via `mimalloc-override.h`) and exploits its deepest features -- `mi_reserve_huge_os_pages_at()` for 1 GiB hugepages (x86-64 PDPE1GB), eliminating TLB misses for large working sets. A gigabyte allocation that would require 262,144 TLB entries at 4 KiB page granularity requires exactly one. This has been experimentally validated: filling and destroying a gigabyte of structured data is near-instantaneous with pinned hugepages.

**Relationship to other languages' models:**
- Rust's `Drop` trait is per-object RAII. Arenas exist as libraries (`bumpalo`, `typed-arena`) precisely because per-object ownership has costs. The engine's approach is coarser-grained and more appropriate for bulk simulation data.
- Zig's allocator-passing convention plus `defer arena.deinit()` is morally identical to `LOCALHEAPATTR`. The difference is that Zig requires explicit defer; C's cleanup attribute is automatic.
- Functional languages (OCaml, Haskell) use generational GC whose nursery IS a bump allocator (an arena). The engine's model is "what if the region boundaries were lexical and the GC didn't exist."

No garbage collector. The OS reclaims during unavoidable context switches; everything else is deterministic.

### 2. Lock-Free Concurrency

Influenced directly by Michael L. Scott's work (Michael & Scott queues, hazard pointers) and his lecture series. The goal is **progress guarantees**: obstruction-free, lock-free, or wait-free primitives that never block a thread on another thread's scheduling.

**Why this matters for the engine:**
- The ECS tick loop must distribute work across all cores without serializing on a mutex.
- The event bus (inter-system communication) is a queue problem. Lock-free MPSC/MPMC queues eliminate contention.
- The logger (see below) is the smallest deployment of these ideas: one MPSC queue, one consumer, bounded buffer.

**The logger as lock-free proving ground:**

The logging system was designed as a minimal MPSC (many-producer, single-consumer) queue:
- **Hot path (per-frame cost):** format the message on the caller's stack, reserve a slot via `atomic_fetch_add` on the tail index, memcpy into the reserved region. No syscalls, no locks, no IO.
- **Cold path (deferred IO):** a flusher thread wakes on a configurable interval, drains the buffer in one batched file write, resets the index.
- **Immediate mode:** fatal messages bypass the queue and hit stderr synchronously, because if you're crashing, the flusher will never run.

The current implementation uses a mutex (the "make it correct first" version). The `_Atomic` on `tail_index` is the breadcrumb of the intended lock-free design: producers `fetch_add` to reserve, write their payload, then set a per-slot commit marker with release ordering. The consumer walks forward until it hits an uncommitted slot. This is essentially how Quill and NanoLog work.

The subtle problem in the lock-free version is the **gap problem**: reservations can commit out of order (producer A reserves 0-100, producer B reserves 100-180, B finishes first). Per-slot commit headers with release semantics solve it. Further reading: Dmitry Vyukov's MPSC intrusive queue and bounded MPMC queue.

### 3. Data-Oriented Design (with FP sympathies)

The engine uses ECS (Entity Component System) architecture: entities are IDs, components are contiguous arrays of plain data, systems iterate over component arrays in tight loops. This is DOD orthodoxy and it's correct for bulk simulation -- iterating a million asteroid positions is a sequential memory scan that the prefetcher and SIMD can accelerate.

But the architect has a soft spot for functional programming and "things being correct and sorting themselves out." The resolution is that **arenas are secretly the FP memory model with the GC removed**. Immutable-ish data allocated into a scoped region, processed, results copied out, region dropped. That's a functional transformation with deterministic cleanup. It needs the *properties* of functional code (referential transparency within a scope, no surprise mutation, predictable resource lifetimes) achieved through structural discipline.

### 4. Scoped Resolution (Simulation LOD)

A thousand star systems cannot all be simulated at full fidelity every tick. The engine must scale simulation detail with player attention:

- **Active system**: full-fidelity simulation. Every asteroid, every ship, every orbital mechanic.
- **Nearby systems**: coarse simulation. Aggregate population, economy, fleet movements. No individual asteroids.
- **Distant systems**: statistical simulation. Trends and events, evaluated infrequently.
- **Unobserved systems**: catch-up simulation on access. When the player jumps to a system that's been asleep for 10,000 ticks, fast-forward its state deterministically.

The engine's memory architecture is designed to support it natively: each resolution level is a different arena with different data layouts and tick rates. Promoting a system from coarse to full-fidelity is an arena allocation + state expansion; demoting it is a state compression + arena free.

Details of the scoped resolution algorithms are currently in the architect's head. Materializing them is a priority once the base ECS and arena system are operational.

### 5. Rendering Philosophy

**No PBR.** PBR's per-material cost (roughness maps, metalness maps, environment probes, BRDF LUTs, image-based lighting) optimizes for making 50 objects look photorealistic. This engine optimizes for making a million objects look good. Non-PBR rendering (flat shading, stylized lighting, older visual aesthetics) keeps the material pipeline thin and the per-fragment cost low, freeing GPU budget for entity count.

**Vulkan directly.** The renderer uses the Vulkan API. This is deliberate: the engine needs direct control over memory allocation, synchronization, and compute dispatch. The renderer is now GPU-driven and meshlet-based (see below), a substantial advance over the early tutorial-derived rasterizer.

**GPU-driven meshlet rendering, with a compatibility fallback.** The frame is built on the GPU: a compute pass animates entity transforms, a compute culling pass frustum-tests every entity and writes an indirect draw list, and geometry is drawn from a shared vertex + meshlet mega-buffer. Meshes are decomposed into meshlets (via the `ano_meshoptimizer` wrapper) at upload time.

Because `VK_EXT_mesh_shader` is unavailable on a large slice of still-current hardware (pre-2019 discrete GPUs, older integrated graphics, software rasterizers), the renderer carries **two interchangeable geometry paths selected automatically at device-creation time**:

- **Mesh path** (preferred): the cull pass emits `VkDrawMeshTasksIndirectCommandEXT`s and a mesh shader (`flat.mesh`) expands meshlets on the GPU.
- **Fallback path**: on devices without the extension, the cull pass emits `VkDrawIndexedIndirectCommand`s and a vertex shader (`flat.vert`) renders the same geometry via classic indexed indirect draws. A meshlet is just an indexed primitive cluster, so the hardware index/vertex fetch performs the expansion the mesh shader did in software. Each mesh stores a plain u32 index region alongside its meshlet metadata for this purpose.

The two paths differ only in the geometry stage and the indirect command format. Resource handling, the geometry pool, the compute culling/animation passes, materials, punctual lighting, and the fragment shaders are shared verbatim. The active path is keyed off `DeviceCapabilities.meshShader`; `ANO_FORCE_NO_MESH_SHADER=1` forces the fallback for testing. Trade-off: the fallback retains per-entity frustum culling but drops per-meshlet cone culling. Full design and phasing live in `PLANS_COMPATIBILITY.md`.

**Future direction: selective raymarching (SDF).** The long-term rendering vision is a hybrid approach: rasterization for UI, HUD, and conventional geometry; raymarching via signed distance fields for the space environment. SDFs are procedural (asteroids defined by math), composable (smooth union/subtraction for constructive geometry), and provide natural LOD (fewer march steps at distance). This maps directly onto the scoped resolution hierarchy. However, rasterization is the immediate path — raymarching is a later-stage addition once the simulation infrastructure is operational.

## Current State (June 2026)

### What Exists (in code)

**Vulkan renderer (GPU-driven, meshlet-based):**

> The bullets below were written for the original tutorial-derived rasterizer. The
> renderer has since moved to a GPU-driven, meshlet-based pipeline (compute animation +
> compute culling + indirect draws over a shared mega-buffer) with a dual mesh-shader /
> vertex-shader geometry path — see the Rendering Philosophy section above and
> `PLANS_COMPATIBILITY.md`. This list is retained as a record of the foundational pieces,
> most of which still exist.

- Instance creation, physical/logical device selection, swap chain, image views
- Graphics pipeline with vertex/fragment shaders
- Vertex and index buffer management
- Uniform buffer updates (MVP matrix, camera)
- Texture loading (stb_image), sampler, image views
- Depth buffering
- Framebuffer management, command recording, synchronization (fences + semaphores)
- Swap chain recreation on resize
- glTF model loading (viking_room test asset)
- Multi-monitor support, configurable present mode
- Window management via GLFW

**ECS ↔ render bridge — the two parallel worlds (June 2026):**

The first real slice of the simulation/render split now exists in code. The engine runs the authoritative simulation and the non-authoritative renderer as **two parallel worlds on separate threads**, joined by two bounded lock-free SPSC rings. This is the first production deployment of the lock-free concurrency principle (§2) outside the logger — and it is genuinely lock-free today (the SPSC ring is acquire/release on head/tail, no CAS, with the producer's `tail` and consumer's `head` on separate cache lines to avoid false sharing). Design of record: `docs/artifacts/ECS.md` (logic side) and `docs/artifacts/VK_BACKEND_INTEROP.md` (render side).

- **ECS module** (`anoptic_ecs.h`, `src/ecs/`): entities are generational `(index, generation)` handles; components live in chunked sparse-set stores with swap-and-pop removal. Structural mutation (create/destroy/add/remove) is deferred and flushed at a tick boundary, so iteration is stable. The store allocates from a caller-provided mimalloc heap. The ECS knows nothing about Vulkan or GPU slots.

- **The bridge** (`anoptic_render_bridge.h`, `src/render_bridge/`): one ring carries `RenderCommand`s (logic → render), the other `RenderEvent`s (render → logic). The logic master is the sole command producer (it emits after the parallel update stage settles, so ordering is total); the render master is the sole event producer. The command protocol is `CREATE / UPDATE / DESTROY / BULK_CREATE`, with an `UPDATE` carrying a field-bit mask so one message can fold several discrete changes — the literal expression of the "≤1 message per entity per tick" invariant.

- **Render-side slot authority** (`src/vulkan_backend/render_slots.h`): the renderer is the *sole* authority over GPU memory and the physical slot space. The logic world names renderables by a stable logical `render_id`; the renderer privately maps `render_id → GPU slot`. Slots are **stable and may contain holes** — the cull pass already compacts visible work, so a dead slot costs one skipped compute invocation and zero draw cost. This deleted the entire defragmentation/remap machinery the early drafts assumed. Slot reuse is **frame-gated**: a `DESTROY` quarantines the slot until all frames in flight retire, then a `REVENT_SLOT_RETIRED` lets the ECS recycle the id.

- **Sparse/continuous split**: only *discrete* transitions cross the bridge (spawn, despawn, teleport, mesh/material swap, light change). *Continuous*, GPU-parameterized motion (orbit/spin via the update compute pass) is sent once as parameters and never restreamed, so animated entities cost zero per-frame bridge traffic. A teleport writes the `initialTransform` buffer (the base pose).

- **Dynamic chunked GPU capacity**: the per-entity (slot-indexed) GPU buffers start at an initial capacity and grow on demand in chunk-aligned, geometrically-doubling steps — dropping the former hard `maxEntities = 10000` ceiling. Growth recreates the buffers larger and re-points the descriptor sets; the shader and descriptor *layouts* never change. Because the GPU allocator is a bump arena (no per-allocation free), growth is reallocate-and-copy and the old region is reclaimed only on teardown — geometric growth bounds the waste to ~the final size. Material and light palettes scale on their own axis (distinct-element-keyed).

- **The thread split**: `main.c` is the logic/ECS master and the sole command producer; it spawns the render master via `ano_thread_create`. The render thread owns all Vulkan *and* all GLFW (init, the frame loop including `glfwPollEvents`, swapchain recreation, teardown), drains the command ring each frame, and applies each transition across all frames in flight. Coordination is three atomics, with shutdown ordered so the producer quiesces before the bridge is destroyed. *Not yet materialized:* the real two-stage tick and `DisplayState` graphics-extract that will replace the stand-in producer currently living in `main.c`.

**Memory system (foundational):**
- mimalloc as global allocator with override
- `LOCALHEAPATTR` macro for scoped heap teardown
- `ano_salloc` for stack allocation
- Hugepage reservation tested and validated
- Scoped heap experiments in `ano_strings.c` (the "mem_chariot" tests)

**Logger (partially built):**
- Async queue-based architecture (enqueue on hot path, flush on cold path)
- 5 log levels (DEBUG, INFO, WARN, ERROR, FATAL)
- Immediate mode for fatal/debug-now messages
- Mutex-based synchronization (placeholder for lock-free version)
- File output path declared but not wired up; flusher thread not implemented
- Buffer drain logic exists but file writes are commented out

**High-resolution timing module (`anoptic_time.h`):**
- Emulator-grade precision timestamps sourced from the highest-resolution monotonic clocks available on each platform: `CLOCK_MONOTONIC` on Linux, `QueryPerformanceCounter` / `QueryPerformanceFrequency` on Windows
- Windows implementation uses overflow-safe QPC-to-nanosecond conversion: splits counter into seconds and sub-seconds before scaling, avoiding uint64_t overflow on long-running machines. Same technique used by Yuzu/Ryujinx emulator timing code.
- `cached_performance_frequency` is `_Atomic` for thread-safe lazy initialization
- `ano_busywait`: tight spinloop on the monotonic clock for sub-microsecond waits where OS sleep granularity is too coarse, with `MAX_BUSYWAIT_NS` safety cap
- `ano_sleep` (Linux): `clock_nanosleep` with `CLOCK_MONOTONIC` and `EINTR` retry loop
- `ano_sleep` (Windows): currently falls back to `Sleep()` (millisecond granularity, 15.6ms default). Upgrade to emulator-grade precision is Step 3 in the build sequence.
- Separate NTP timestamp stub for future network time synchronization
- Full API: `ano_timestamp_raw` (ns), `ano_timestamp_us`, `ano_timestamp_ms`, `ano_timestamp_unix` (UTC), `ano_busywait` (spinlock), `ano_sleep` (OS-scheduled)

**Platform abstraction:**
- Separate implementations for Linux and Windows (memory, time, filesystem)
- Cross-compilation support via CMake toolchain files (Clang targeting MinGW-w64)

**Build system:**
- CMake with platform-specific toolchain files
- Release, Debug, and Test build configurations
- Build scripts for Linux (`build.sh`) and Windows (`build.bat`)

### What Exists (in the architect's head, not yet materialized)

- Complete arena hierarchy (process > level > frame > scratch > pool)
- Lock-free MPSC queue design for the logger and event bus (the SPSC bridge ring is the first lock-free primitive actually shipped — see the ECS↔render bridge above)
- ~~ECS architecture and component storage layout~~ — now in code (generational handles + chunked sparse-set stores); the two-stage parallel tick and graphics-extract are still to be built
- Event bus for inter-system communication
- Scoped resolution algorithms for multi-scale simulation
- The simulation game itself (star systems, worlds, populations, economies, fleets)
- Novel approaches to deterministic catch-up simulation
- String type with ownership semantics (ptr + len + capacity, copy-on-slice, scoped cleanup)

### What Needs to Be Built (bottom-up, in dependency order)

**Step 1 -- High-performance logger:** Standalone module. Lock-free MPSC enqueue using fetch_add + commit-header pattern, inlined directly. Flusher thread via `anoptic_threads`. Wire up `ano_log_output_dir`, implement `ano_log_interval`, test file output. This is the first module that exercises arenas + atomics + threads together, and provides instrumentation for everything after.

Current state (mutex version, audited June 2026). The concurrency half is correct; the output half is absent.
- The mutex-guarded enqueue (`enqueue_log_string`) is race-free and the bounds check at logging_core.c:56 has no overflow: the accepted case writes its terminating NUL at worst index `LOG_BUFFER_MAX-1`. Verified under TSan.
- `tail_index` is `_Atomic` but only ever touched under `log_buffer_mtx` -- redundant today, kept as the breadcrumb for the lock-free version.
- Output is entirely stubbed. All three `write_to_log_file` calls are commented out (logging_core.c:59, 128, 177); `output_file_path` is never assigned; `ano_log_output_dir` is declared in the public header but never defined (first caller = link error). So enqueued DEBUG/INFO/WARN/ERROR never reach any sink -- `write_all_buffered` formats the batch, discards it, and resets the index. Only immediate mode (FATAL, `_now`) prints, to stdout for <=WARN and stderr for >WARN.
- `ano_log_immediate` calls `write_all_buffered()` unconditionally ("TODO: Remove this", logging_core.c:180), so an immediate message also wipes the pending enqueue buffer, and the immediate line prints before any buffered lines it implicitly drops.
- No timestamp exists anywhere. The "preserve order via timestamps" goal is unbuilt; ordering today is an accident of the mutex (FIFO). The prefix is only `LEVEL file:line:`.
- Buffer-full drops the message (returns -1 + stderr note) -- it does not write immediate as the message string claims.
- Latent: `ano_log_init` calls `ano_log_fatal` if the buffer mutex fails to init (logging_core.c:187), and the immediate path then locks that just-failed mutex -- UB on the error path.

Rewrite recommendations.
- Stamp every record with a monotonic timestamp (`ano_timestamp_raw`/`_us`) in its slot/commit header. Once enqueue is lock-free the FIFO-by-mutex property is gone, so the timestamp is the only thing that can reconstruct cross-thread order at flush time.
- MPSC hot path: reserve with `fetch_add` on the tail, write the payload, publish with a per-slot commit marker stored release; the flusher walks forward and stops at the first uncommitted slot (Quill/NanoLog). Records are variable-length, so either a fixed-size POD slot ring `{ts, level, file, line, msg[]}` or a byte ring of length-prefixed records with a commit sequence -- decide before writing the consumer.
- Wire the sink: implement `ano_log_output_dir` (set `output_file_path`, open the file once and hold the `FILE*`), enable the write in the flush path, implement `ano_log_interval` + the flusher thread it implies.
- Separate immediate from flush: immediate must not silently reset the enqueue buffer. If ordering across the two paths matters, flush buffered first then emit immediate, or merge by timestamp.
- Make the full-buffer policy explicit and counted (drop vs block vs immediate-write vs grow), with a dropped-message counter.
- Fix the init error path so it does not log through a mutex/buffer that is not yet live.

Test plan (what the rewrite must make verifiable). The current test only asserts that enqueue returns 0; it cannot see content, order, or flush, because nothing is emitted. Once a sink exists, the test should flush to a temp file and read it back to check:
- Verifiable output: level, `file:line`, and message body survive a round-trip through enqueue -> flush -> file.
- Accumulation: N enqueues accumulate and a single flush emits all N, in order.
- Immediate is immediate: an immediate/FATAL message reaches its stream before any flush, with defined ordering against buffered records.
- Multi-thread: P producers insert concurrently; every message is eventually flushed (count + per-record integrity, no torn or interleaved bytes), clean under TSan; measure hot-path cost per enqueue (target sub-microsecond) and assert a loose ceiling.
- Boundaries: empty message; a max-length message at `LOG_MESSAGE_MAX` with truncation handled; a record landing exactly at `LOG_BUFFER_MAX` (high end); the chosen full-buffer behavior; an empty flush (low end).

**Step 2 -- Dependency update:** Bump GLFW, stb, jsmn, mimalloc submodules to latest stable. Quick audit for API changes. Fold mimalloc finalization into this -- the integration is already done, this is just a version bump and validation that `mi_heap_new` / `mi_heap_destroy` / `mi_heap_zalloc_aligned` still behave. Confirm hugepage support still works on current version. Validate scoped heap teardown (`LOCALHEAPATTR`). Ensure global override (`mimalloc-override.h`) is clean. Low risk, low effort.

**Step 3 -- Windows high-resolution timing:** The Linux timing module (`clock_nanosleep` + `CLOCK_MONOTONIC`) delivers sub-microsecond precision. The Windows side falls back to `Sleep()` which has millisecond granularity at best (15.6ms default timer resolution). Bring `ano_sleep` on Windows up to parity: `timeBeginPeriod(1)` to set the scheduler to 1ms resolution, `WaitableTimer` or `Sleep(1)` for the coarse wait, then spinloop (`ano_busywait`) for the final sub-millisecond remainder. This is the emulator-grade pattern used by Yuzu/Ryujinx for frame-perfect timing. Also consider `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Windows 10 1803+) for native sub-millisecond OS sleeps without the spin tail. The tick needs to be deterministic-length; a 15.6ms sleep jitter on Windows makes that impossible.

**Step 4 -- ano_strings:** Owned string type: `{char* ptr, uint32_t len, uint32_t capacity}` with `LOCALHEAPATTR`-style scoped cleanup. Allocations go through a heap parameter so strings can live in any arena. Copy-on-slice. ~150 lines. UTF-8 support deferred -- UTF-8 is byte-transparent in storage, so the string type doesn't need to know about it. UTF-8 validation/iteration added later as a layer on top, only when the text renderer demands it.

**Step 5 -- Lock-free collections:**

*Phase A: Classic implementations.* Michael & Scott queue, bounded MPMC ring buffer (Vyukov-style). Correct, tested, benchmarked. These serve as baselines and are usable immediately.

*Phase B: Cache-line-striped lock-free structures (experimental/novel).*

The core idea: align the concurrency model to the hardware coherency unit. The x86 cache coherency protocol (MESI/MESIF) already enforces exclusive ownership at the cache-line level (64 bytes). Instead of per-item atomic operations (classic M&S), make the cache line the unit of ownership transfer.

**Step 6 -- Resource Management**

Following the basic instructions laid out in Game Engine Architecture.

Design sketch:
```
[ stripe 0 ]  [ stripe 1 ]  [ stripe 2 ]  [ stripe 3 ]  ...
  64 bytes       64 bytes      64 bytes      64 bytes
  owned:T1      owned:T2      owned:T1       free

  - each stripe is cache-line aligned
  - ownership transferred via atomic on the stripe header
  - within a stripe, the owner reads/writes with zero atomics
```

A producer claims a stripe (fetch_add on head index), fills it with plain stores, publishes via release-store on a commit flag. A consumer walks stripes in order via acquire-loads on commit flags. No per-item CAS. The only cross-core cache traffic is the intentional ownership transfer. Thread-local heaps (mimalloc) guarantee that even the allocator doesn't cause false sharing.

This gets batched throughput (amortizing sync cost over N items per stripe) with lock-free progress guarantees (a stalled producer leaves an uncommitted stripe, doesn't block anyone). The 90s papers didn't consider this because they reasoned about abstract shared-memory models. On a 16-core Ryzen where cache-line bouncing is the dominant cost, aligning the algorithm to the coherency unit is the natural move.

Open problems:
- Gap handling at stripe granularity (out-of-order publication)
- Variable-size data (fixed-size event structs under 64 bytes is the likely constraint)
- Formal linearizability argument (TLA+ or hand proof)
- Benchmarking against classic M&S and LCRQ on many-core hardware

If this works and benchmarks well, it's worth a paper (DISC or PPoPP). Classic implementations come first as the baseline.

Target structures: ring buffers, queues, heaps. These serve the event bus, job system, and inter-system communication.

**Step 6 -- Additional data structures (as needed):** Build structures in tandem with the features that operate on them. stb_ds is acceptable as a stopgap for prototyping (e.g., hash maps during renderer work) without long-term commitment.

**Step 7 -- Renderer rewrite:** Full rewrite of the Vulkan renderer. The current implementation is tutorial-derived with poor system design. The rewrite builds the renderer as a proper subsystem: allocates from scratch arenas, logs through the real logger, communicates through the event bus. Scope for v1: one render pass, one pipeline, geometry on screen, driven by the event bus. No PBR. Rasterization only for now. stb_image retained for texture loading.

**Step 8 -- Event bus + input:** Global, thread-agnostic event bus. Possibly two buses: one monotonic per-item (classic lock-free, for ordered events like input), one with lock-free cache-line stripes (for high-throughput bulk events like physics/simulation updates). GLFW callbacks enqueue input events; game loop dequeues. Clean producer/consumer boundary. This infrastructure also serves future physics integration.

**Step 9 -- Main game loop + first visual output:** The integration milestone. Input moves camera, event bus carries input, simulation updates transforms, renderer draws the frame, all allocated from frame arenas, all logged. A sphere on screen through the full pipeline. This is v0.1: proof that every layer works together. Everything after is building the actual game on trusted infrastructure.

### Branch Archaeology (surveyed June 2026)

Work is fractured across 16 remote branches. Survey results:

**Dead branches (fully merged into main, 0 commits ahead — safe to delete):** `ctest-config`, `feature-filepath`, `feature-logging`, `feature-memory`, `feature_threading`, `fix-clang-usage`, `fix-vertex-deps`, `git-status-fix`, `logging`, `platform-cleanup`, `time-time2`

**`implementation-platformlayer-time` (6 ahead, superseded):** Main's time module is a strict improvement of this branch's version (atomic frequency caching, error returns). Same unsolved Windows Sleep() granularity. Nothing to recover. Notable design decision in commit 264e2a4: "timespans removed (they'll be an ECS singleton)." Also contains an alternative src/platform/{linux,windows}/ directory layout that main abandoned.

**`feature-strings` (5 ahead) -- RECOVER: this is the Step 4 spec.** Contains a fully designed (stub-implemented) string API in include/ano_strings.h:
- `anostr_t {char* buffer, size_t len}` string type
- UTF-8 codepoint handles, validation, iteration
- UTF-16 <-> UTF-8 conversion (needed for Windows paths)
- Byte slices and UTF slices
- Managed slice macros (statement expressions + scoped cleanup attr): ANOSTR_STACK_BYTESLICE, ANOSTR_HEAP_BYTESLICE with CLEANUPATTR The function signatures for Step 4 already exist, written by the architect in 2024. Implementations are stubs returning 0.

**`feature-render-text` (27 ahead) -- PRESERVE as reference for Step 7+.** The unicode rabbit hole, materialized. A complete text rendering stack:
- FreeType integration
- Glyph atlas generation (stb_image_write), upload to VRAM
- SDF font rendering (final commit: "Switched to SDF font rendering") Predates main's renderer restructure; heavy merge conflicts guaranteed. Salvage material for the renderer rewrite's text/UI pass. Contains feature-render-vertex's 7 commits (MSAA, mipmapping, structurally agnostic glTF loading, render asset sharing) in its history.

**`fixes-render-text` (2 unique commits):** VRAM leak mitigation + text debug overflow fix, diverged from feature-render-text after PR #41. Note when salvaging the text stack.

### Known Technical Debt

- glTF parser does loose malloc/free instead of arena allocation
- Vulkan cleanup has copy-paste bugs (partially fixed, June 2026)
- Logger file output entirely non-functional (commented out)
- `autoStringTest()` runs a ~1 GiB allocation on every debug launch (belongs in test suite)
- `recordCommandBuffer` binds only entity[0]'s buffers but loops draw calls over all entities
- `log_strings[]` defined in header; duplicated per translation unit
- 45+ TODO comments scattered through codebase
- Large blocks of commented-out code in vulkanMaster.c and instanceInit.c

## References and Influences

- **Michael L. Scott** -- lock-free queues, hazard pointers, multiprocessor programming theory
- **Maged Michael** -- hazard pointers, lock-free memory reclamation
- **Dmitry Vyukov** -- practical lock-free queue designs (MPSC intrusive, bounded MPMC)
- **Tofte & Talpin** -- region-based memory management, region inference for ML
- **Cyclone** -- explicit regions in a C-like language; ancestor of Rust's lifetime system
- **Casey Muratori** -- handmade, data-oriented game engine philosophy
- **Ryan Fleury** -- arena-based memory architecture writings
- **mimalloc** (Daan Leijen, Microsoft Research) -- the allocator underneath all of this
- **Dwarf Fortress** (Tarn Adams) -- proof that one developer can build deep simulation at scale
- **Quill / NanoLog** -- lock-free logging implementations that validate the MPSC buffer design
