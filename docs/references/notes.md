# Anoptic Engine -- Internal Architecture Notes

## Vision

A simulation engine for a space colony game in the lineage of Dwarf Fortress and Stellaris. Thousand-star systems, each with worlds, stations, millions of asteroids, fleets, populations with individual histories. Simulation is the content; the renderer is a viewport.

No off-the-shelf engine fits this workload. Unity and Unreal optimize for rendering complexity with modest entity counts. This engine optimizes for entity count and simulation depth; rendering is secondary. Target hardware: modern desktop (Ryzen 9 class CPU, RTX 4090 class GPU).

Ludicrous-scale simulation at interactive framerates. The engine is the foundation; the game builds on top.

## Language: C23

C23, Clang 17+ only. C23 was in beta when development began (2022-2023); compiler coverage is now mature.

C over C++: control, simplicity, ABI stability. Where C lacks modern conveniences (ownership, scoped cleanup, type-safe generics), use targeted Clang extensions and C23 features. Know what the machine does; smallest abstraction that makes that control ergonomic.

## Core Architectural Principles

### 1. Region-Based Memory Management (Scoped Arenas)

Most allocations in a game frame are scope-shaped: born together, used together, die together. Per-object malloc/free is the wrong granularity.

Allocate from a **scoped heap** (mimalloc local heap); destroy the heap when the scope exits. Setup/teardown is O(regions). A gigabyte of frame-scratch dies in microseconds via `mi_heap_destroy` (pages returned, no content iteration).

Literature: Tofte & Talpin region inference (ML), Cyclone explicit regions, Muratori/Fleury arena tradition.

Mechanism in C is `__attribute__((cleanup))`:

```c
#define LOCALHEAPATTR __attribute__((__cleanup__(ano_heap_release)))

// Usage: heap is automatically destroyed when it leaves scope.
mi_heap_t *frameHeap LOCALHEAPATTR = mi_heap_new();
```

Same mechanism as systemd (`_cleanup_`), GLib (`g_autoptr`), and the Linux kernel. Clang-only policy: fully supported.

**Memory hierarchy (planned):**
- **Process arena**: the OS is the outermost garbage collector. `exit()` is a region free.
- **Level/session arena**: allocated on load, destroyed on unload. Holds the star map, entity pools, persistent simulation state.
- **Frame arena**: reset at the top of every frame. Holds transient computation: command buffers, intermediate query results, scratch space.
- **Scratch arenas**: short-lived, task-scoped. The glTF parser allocates into a scratch arena, copies survivors out, drops the arena.
- **Pool allocators**: for the genuinely dynamic 10% whose lifetimes aren't scope-shaped: entities that spawn and die unpredictably. Free-list or slab allocation within a region.

**Hugepages and mimalloc:** mimalloc is the global allocator (`mimalloc-override.h`). Uses `mi_reserve_huge_os_pages_at()` for 1 GiB hugepages (x86-64 PDPE1GB): one TLB entry instead of 262,144 at 4 KiB. Validated: fill+destroy of a gigabyte of structured data is near-instantaneous with pinned hugepages.

**Related models:**
- Rust `Drop`: per-object RAII. Arenas (`bumpalo`, `typed-arena`) exist because per-object ownership has costs. Engine approach is coarser-grained for bulk simulation data.
- Zig allocator-passing + `defer arena.deinit()`: morally identical to `LOCALHEAPATTR`. Zig requires explicit defer; C cleanup attribute is automatic.
- OCaml/Haskell generational GC nursery: a bump allocator. Engine model: lexical region boundaries, no GC.

No garbage collector. OS reclaims on context switches; everything else is deterministic.

### 2. Lock-Free Concurrency

Influenced by Michael L. Scott (Michael & Scott queues, hazard pointers) and his lecture series. Goal: **progress guarantees** (obstruction-free / lock-free / wait-free). Never block a thread on another thread's scheduling.

Uses:
- ECS tick loop: distribute work across cores without mutex serialization.
- Event bus: lock-free MPSC/MPMC queues.
- Logger: smallest deployment: one MPSC queue, one consumer, bounded buffer.

**Logger as lock-free proving ground:**

Minimal MPSC (many-producer, single-consumer) queue:
- **Hot path (per-frame):** format on caller's stack, reserve via `atomic_fetch_add` on tail, memcpy into reserved region. No syscalls, no locks, no IO.
- **Cold path (deferred IO):** flusher thread wakes on interval, drains buffer in one batched file write, resets index.
- **Immediate mode:** fatal messages bypass the queue and hit stderr synchronously (flusher will not run during crash).

Current implementation uses a mutex ("make it correct first"). `_Atomic` on `tail_index` is the breadcrumb for lock-free: producers `fetch_add` to reserve, write payload, set per-slot commit marker with release ordering. Consumer walks forward until an uncommitted slot. Same pattern as Quill and NanoLog.

**Gap problem:** reservations can commit out of order (A reserves 0-100, B reserves 100-180, B finishes first). Per-slot commit headers with release semantics solve it. Further reading: Dmitry Vyukov's MPSC intrusive queue and bounded MPMC queue.

### 3. Data-Oriented Design (with FP sympathies)

ECS: entities are IDs, components are contiguous arrays of plain data, systems iterate component arrays in tight loops. Correct for bulk simulation: a million asteroid positions is a sequential scan the prefetcher and SIMD can accelerate.

Arenas are the FP memory model with the GC removed: immutable-ish data into a scoped region, process, copy results out, drop region. Referential transparency within a scope, no surprise mutation, predictable lifetimes via structural discipline.

### 4. Scoped Resolution (Simulation LOD)

A thousand star systems cannot all run at full fidelity every tick. Scale detail with player attention:

- **Active system**: full-fidelity. Every asteroid, ship, orbital mechanic.
- **Nearby systems**: coarse. Aggregate population, economy, fleet movements. No individual asteroids.
- **Distant systems**: statistical. Trends and events, evaluated infrequently.
- **Unobserved systems**: catch-up on access. Player jumps to a system asleep 10,000 ticks: fast-forward state deterministically.

Each resolution level is a different arena (layouts + tick rates). Promote coarse→full: arena alloc + state expansion. Demote: state compression + arena free.

Scoped resolution algorithms are still in the architect's head. Priority once base ECS and arenas are operational.

### 5. Rendering Philosophy

**No PBR.** PBR per-material cost (roughness/metalness maps, probes, BRDF LUTs, IBL) optimizes for ~50 photoreal objects. This engine optimizes for a million objects looking good. Flat/stylized/non-PBR keeps the material pipeline thin and per-fragment cost low.

**Vulkan directly.** Direct control over memory allocation, synchronization, and compute dispatch. Renderer is GPU-driven and meshlet-based (below); substantial advance over the early tutorial-derived rasterizer.

**GPU-driven meshlet rendering, with a compatibility fallback.** Frame built on GPU: compute animates entity transforms, compute cull frustum-tests every entity into an indirect draw list, geometry drawn from a shared vertex + meshlet mega-buffer. Meshes decomposed into meshlets (`ano_meshoptimizer`) at upload.

`VK_EXT_mesh_shader` is unavailable on much still-current hardware (pre-2019 discrete, older iGPU, software rasterizers). Two interchangeable geometry paths, selected at device creation:

- **Mesh path** (preferred): cull emits `VkDrawMeshTasksIndirectCommandEXT`s; mesh shader (`flat.mesh`) expands meshlets on GPU.
- **Fallback path**: without the extension, cull emits `VkDrawIndexedIndirectCommand`s; vertex shader (`flat.vert`) does classic indexed indirect draws. A meshlet is an indexed primitive cluster; hardware index/vertex fetch does the expansion. Each mesh stores a plain u32 index region alongside meshlet metadata.

Paths differ only in geometry stage and indirect command format. Shared: resource handling, geometry pool, compute cull/animation, materials, punctual lighting, fragment shaders. Active path keyed off `DeviceCapabilities.meshShader`; `ANO_FORCE_NO_MESH_SHADER=1` forces fallback. Fallback keeps per-entity frustum cull, drops per-meshlet cone cull. Design/phasing: `PLANS_COMPATIBILITY.md`.

**Future: selective raymarching (SDF).** Hybrid: raster for UI/HUD/conventional geometry; SDF raymarch for space environment. SDFs are procedural, composable (smooth CSG), and provide natural LOD (fewer march steps at distance). Maps onto scoped resolution. Immediate path is rasterization; raymarching comes after simulation infrastructure is operational.

## Current State (June 2026)

### What Exists (in code)

**Vulkan renderer (GPU-driven, meshlet-based):**

> The bullets below were written for the original tutorial-derived rasterizer. The
> renderer has since moved to a GPU-driven, meshlet-based pipeline (compute animation +
> compute culling + indirect draws over a shared mega-buffer) with a dual mesh-shader /
> vertex-shader geometry path. See Rendering Philosophy above and
> `PLANS_COMPATIBILITY.md`. Retained as a record of foundational pieces,
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

**ECS ↔ render bridge: the two parallel worlds (June 2026):**

First real slice of the simulation/render split in code. Authoritative simulation and non-authoritative renderer as **two parallel worlds on separate threads**, joined by two bounded lock-free SPSC rings. First production lock-free deployment outside the logger, and genuinely lock-free today (SPSC: acquire/release on head/tail, no CAS; producer `tail` and consumer `head` on separate cache lines). Design of record: `docs/artifacts/ECS.md` (logic), `docs/artifacts/VK_BACKEND_INTEROP.md` (render).

- **ECS module** (`anoptic_ecs.h`, `src/ecs/`) *(since removed from the tree pending a proper rebuild; see `src/src.md`; the design stands)*: entities are generational `(index, generation)` handles; components live in chunked sparse-set stores with swap-and-pop removal. Structural mutation (create/destroy/add/remove) is deferred and flushed at a tick boundary, so iteration is stable. The store allocates from a caller-provided mimalloc heap. The ECS knows nothing about Vulkan or GPU slots.

- **The bridge** (private `src/render_bridge/render_bridge.h`; public command protocol in `include/anoptic_render.h`): one ring carries `RenderCommand`s (logic → render), the other `RenderEvent`s (render → logic). Logic master is sole command producer (emits after parallel update settles; total order); render master is sole event producer. Protocol: `CREATE / UPDATE / DESTROY / BULK_CREATE`. `UPDATE` carries a field-bit mask so one message can fold several discrete changes ("≤1 message per entity per tick").

- **Render-side slot authority** (`src/vulkan_backend/render_slots.h`): renderer is sole authority over GPU memory and physical slot space. Logic names renderables by stable logical `render_id`; renderer privately maps `render_id → GPU slot`. Slots are **stable and may contain holes**: cull already compacts visible work, so a dead slot costs one skipped compute invocation and zero draw cost. Deleted the early-draft defragmentation/remap machinery. Slot reuse is **frame-gated**: `DESTROY` quarantines until all frames in flight retire, then `REVENT_SLOT_RETIRED` lets the ECS recycle the id.

- **Sparse/continuous split**: only *discrete* transitions cross the bridge (spawn, despawn, teleport, mesh/material swap, light change). *Continuous*, GPU-parameterized motion (orbit/spin via update compute) is sent once as parameters and never restreamed. Teleport writes the `initialTransform` buffer (base pose).

- **Dynamic chunked GPU capacity**: per-entity (slot-indexed) GPU buffers start at an initial capacity and grow on demand in chunk-aligned, geometrically-doubling steps (drops former hard `maxEntities = 10000`). Growth recreates buffers larger and re-points descriptor sets; shader and descriptor *layouts* never change. GPU allocator is a bump arena (no per-allocation free): growth is reallocate-and-copy; old region reclaimed only on teardown. Geometric growth bounds waste to ~final size. Material and light palettes scale on their own axis (distinct-element-keyed).

- **The thread split**: `main.c` runs the render world on the main thread. GLFW pins window/event handling there (mandatory on macOS). Render thread owns all Vulkan *and* all GLFW (init, frame loop including `glfwPollEvents`, swapchain recreation, teardown). Spawns logic/ECS master (`anoLogicThreadMain`) via `ano_thread_create` as sole command producer. Render side drains the command ring each frame and applies each transition across all frames in flight. Coordination: three atomics; shutdown orders producer quiesce before bridge destroy. *Not yet materialized:* real two-stage tick and `DisplayState` graphics-extract (stand-in producer still in `main.c`).

**Memory system (foundational):**
- mimalloc as global allocator with override
- `LOCALHEAPATTR` macro for scoped heap teardown
- `ano_salloc` for stack allocation
- Hugepage reservation tested and validated
- Scoped heap experiments in `ano_strings.c` (the "mem_chariot" tests)

**Logger (done; Step 1 shipped 2026-06-24; see `docs/logger.md` and TODO.md):**
- Lock-free MPSC ring (variable-length records, CAS reserve, lap-counter reclaim) with an owned drain thread; `ano_log_flush` is a synchronous inline pass
- 5 log levels (DEBUG, INFO, WARN, ERROR, FATAL); immediate mode for fatal/debug-now messages
- Eager (~48 ns) and deferred (~22 ns) formatting strategies; file output + `ano_log_output_dir` wired
- Validated via TLA+/TLC, TSan, the `anotest_logfuzz` no-loss fuzzer, and the `anotest_logbench` benchmark
- The Step 1 audit below describes the old mutex version; it stays as the record that drove the rewrite

**High-resolution timing module (`anoptic_time.h`):**
- Best-in-class precision timestamps sourced from the highest-resolution monotonic clocks available on each platform: `CLOCK_MONOTONIC` on Linux, invariant TSC (rdtsc) with a QPC fallback on Windows, mach timebase on macOS
- Windows timebase (Step 1 follow-up, 2026-07-03): x86-64 uses rdtsc when the CPU reports an invariant TSC (CPUID 0x80000007 EDX[8]), calibrated against QPC (median of three ~4 ms Sleep-bracketed samples) so `ano_ticks_to_ns` converts correctly. This replaces a bare 10 MHz QPC (100 ns grain, too coarse to order log records stamped in the same window) with a sub-nanosecond counter. The timebase is resolved once and frozen so `ano_timestamp_ticks` and `ano_ticks_to_ns` never disagree; QPC remains the fallback on non-invariant-TSC or non-x86 Windows builds.
- Windows implementation uses overflow-safe counter-to-nanosecond conversion: splits the counter into seconds and sub-seconds before scaling, avoiding uint64_t overflow on long-running machines. Same technique used by Yuzu/Ryujinx emulator timing code.
- `cached_performance_frequency` is `_Atomic` for thread-safe lazy initialization
- `ano_busywait`: tight spinloop on the monotonic clock for sub-microsecond waits where OS sleep granularity is too coarse, with `MAX_BUSYWAIT_NS` safety cap
- `ano_sleep` (Linux): `clock_nanosleep` with `CLOCK_MONOTONIC` and `EINTR` retry loop
- `ano_sleep` (Windows): per-thread high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Win10 1803+) for the coarse wait plus an `ano_busywait` spin tail (Step 3, done; verified green on a real Windows host 2026-07-02).
- Separate NTP timestamp stub for future network time synchronization
- Full API: `ano_timestamp_raw` (ns), `ano_timestamp_us`, `ano_timestamp_ms`, `ano_timestamp_unix` (UTC), `ano_busywait` (spinlock), `ano_sleep` (OS-scheduled)

**Platform abstraction:**
- Separate implementations for Linux, Windows, and macOS (memory, time, filesystem)
- Cross-compilation support via CMake toolchain files (Clang targeting MinGW-w64)

**Build system:**
- CMake with platform-specific toolchain files
- Release, Debug, and Test build configurations
- Build scripts for Linux/macOS (`build.sh`) and Windows (`build.bat`)

### What Exists (in the architect's head, not yet materialized)

- Complete arena hierarchy (process > level > frame > scratch > pool)
- Lock-free MPSC queue design for the event bus (the logger's MPSC ring and the SPSC bridge ring have both shipped; see above)
- ~~ECS architecture and component storage layout~~ now in code (generational handles + chunked sparse-set stores); the two-stage parallel tick and graphics-extract are still to be built
- Event bus for inter-system communication
- Scoped resolution algorithms for multi-scale simulation
- The simulation game itself (star systems, worlds, populations, economies, fleets)
- Novel approaches to deterministic catch-up simulation
- String type with ownership semantics (ptr + len + capacity, copy-on-slice, scoped cleanup)

### What Needs to Be Built (bottom-up, in dependency order)

**Step 1 -- High-performance logger:** Standalone module. Lock-free MPSC enqueue using fetch_add + commit-header pattern, inlined directly. Flusher thread via `anoptic_threads`. Wire up `ano_log_output_dir`, implement `ano_log_interval`, test file output. First module that exercises arenas + atomics + threads together; provides instrumentation for everything after.

Current state (mutex version, audited June 2026). The concurrency half is correct; the output half is absent.
- The mutex-guarded enqueue (`enqueue_log_string`) is race-free and the bounds check at logging_core.c:56 has no overflow: the accepted case writes its terminating NUL at worst index `LOG_BUFFER_MAX-1`. Verified under TSan.
- `tail_index` is `_Atomic` but only ever touched under `log_buffer_mtx`. Redundant today; kept as the breadcrumb for the lock-free version.
- Output is entirely stubbed. All three `write_to_log_file` calls are commented out (logging_core.c:59, 128, 177); `output_file_path` is never assigned; `ano_log_output_dir` is declared in the public header but never defined (first caller = link error). So enqueued DEBUG/INFO/WARN/ERROR never reach any sink: `write_all_buffered` formats the batch, discards it, and resets the index. Only immediate mode (FATAL, `_now`) prints, to stdout for <=WARN and stderr for >WARN.
- `ano_log_immediate` calls `write_all_buffered()` unconditionally ("TODO: Remove this", logging_core.c:180), so an immediate message also wipes the pending enqueue buffer, and the immediate line prints before any buffered lines it implicitly drops.
- No timestamp exists anywhere. The "preserve order via timestamps" goal is unbuilt; ordering today is an accident of the mutex (FIFO). The prefix is only `LEVEL file:line:`.
- Buffer-full drops the message (returns -1 + stderr note). It does not write immediate as the message string claims.
- Latent: `ano_log_init` calls `ano_log_fatal` if the buffer mutex fails to init (logging_core.c:187), and the immediate path then locks that just-failed mutex. UB on the error path.

Rewrite recommendations.
- Stamp every record with a monotonic timestamp (`ano_timestamp_raw`/`_us`) in its slot/commit header. Once enqueue is lock-free the FIFO-by-mutex property is gone, so the timestamp is the only thing that can reconstruct cross-thread order at flush time.
- MPSC hot path: reserve with `fetch_add` on the tail, write the payload, publish with a per-slot commit marker stored release; the flusher walks forward and stops at the first uncommitted slot (Quill/NanoLog). Records are variable-length, so either a fixed-size POD slot ring `{ts, level, file, line, msg[]}` or a byte ring of length-prefixed records with a commit sequence. Decide before writing the consumer.
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

**Step 2 -- Dependency update:** Bump GLFW, stb, jsmn, mimalloc submodules to latest stable. Quick audit for API changes. Fold mimalloc finalization into this: integration already done; version bump + validate `mi_heap_new` / `mi_heap_destroy` / `mi_heap_zalloc_aligned`. Confirm hugepage support. Validate scoped heap teardown (`LOCALHEAPATTR`). Ensure global override (`mimalloc-override.h`) is clean. Low risk, low effort.

**Step 3 -- Windows high-resolution timing:** Linux (`clock_nanosleep` + `CLOCK_MONOTONIC`) delivers sub-microsecond precision. Windows falls back to `Sleep()` (ms granularity; 15.6ms default). Bring `ano_sleep` on Windows to parity: `timeBeginPeriod(1)`, `WaitableTimer` or `Sleep(1)` for coarse wait, then `ano_busywait` for the sub-ms remainder. Emulator-grade pattern (Yuzu/Ryujinx). Also consider `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Win10 1803+) for native sub-ms OS sleeps without the spin tail. Tick needs deterministic length; 15.6ms sleep jitter makes that impossible.

**Step 4 -- ano_strings:** Owned string type: `{char* ptr, uint32_t len, uint32_t capacity}` with `LOCALHEAPATTR`-style scoped cleanup. Allocations through a heap parameter so strings can live in any arena. Copy-on-slice. ~150 lines. UTF-8 support deferred: UTF-8 is byte-transparent in storage. Validation/iteration added later as a layer, when the text renderer demands it.

**Step 5 -- Lock-free collections:**

*Phase A: Classic implementations.* Michael & Scott queue, bounded MPMC ring buffer (Vyukov-style). Correct, tested, benchmarked. Baselines; usable immediately.

*Phase B: Cache-line-striped lock-free structures (experimental/novel).*

Align concurrency to the hardware coherency unit. x86 MESI/MESIF enforces exclusive ownership at cache-line granularity (64 bytes). Make the cache line the unit of ownership transfer instead of per-item atomics (classic M&S).

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

Producer claims a stripe (fetch_add on head), fills with plain stores, publishes via release-store on a commit flag. Consumer walks stripes in order via acquire-loads on commit flags. No per-item CAS. Cross-core traffic is intentional ownership transfer only. Thread-local heaps (mimalloc) keep the allocator from causing false sharing.

Batched throughput (amortize sync over N items per stripe) with lock-free progress (stalled producer leaves an uncommitted stripe; doesn't block others). On a 16-core Ryzen, cache-line bouncing is the dominant cost; align the algorithm to the coherency unit.

Open problems:
- Gap handling at stripe granularity (out-of-order publication)
- Variable-size data (fixed-size event structs under 64 bytes is the likely constraint)
- Formal linearizability argument (TLA+ or hand proof)
- Benchmarking against classic M&S and LCRQ on many-core hardware

If this works and benchmarks well, it's worth a paper (DISC or PPoPP). Classic implementations come first as the baseline.

Target structures: ring buffers, queues, heaps. These serve the event bus, job system, and inter-system communication.

**Step 6 -- Additional data structures (as needed):** Build structures in tandem with the features that operate on them. stb_ds is acceptable as a stopgap for prototyping (e.g., hash maps during renderer work) without long-term commitment.

**Step 7 -- Renderer rewrite:** Full rewrite of the Vulkan renderer. Current implementation is tutorial-derived with poor system design. Rewrite as a proper subsystem: scratch arenas, real logger, event bus. Scope for v1: one render pass, one pipeline, geometry on screen, driven by the event bus. No PBR. Rasterization only. stb_image retained for texture loading.

**Step 8 -- Event bus + input:** Global, thread-agnostic event bus. Possibly two buses: one monotonic per-item (classic lock-free, ordered events like input), one with lock-free cache-line stripes (high-throughput bulk events like physics/sim updates). GLFW callbacks enqueue input; game loop dequeues. Clean producer/consumer boundary. Also serves future physics integration.

**Step 9 -- Main game loop + first visual output:** Integration milestone. Input moves camera, event bus carries input, simulation updates transforms, renderer draws, all from frame arenas, all logged. A sphere on screen through the full pipeline. v0.1: proof every layer works together. Everything after builds the game on trusted infrastructure.

### Branch Archaeology (surveyed June 2026)

Work is fractured across 16 remote branches. Survey results:

**Dead branches (fully merged into main, 0 commits ahead; safe to delete):** `ctest-config`, `feature-filepath`, `feature-logging`, `feature-memory`, `feature_threading`, `fix-clang-usage`, `fix-vertex-deps`, `git-status-fix`, `logging`, `platform-cleanup`, `time-time2`

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
