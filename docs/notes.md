# Anoptic Engine -- Internal Architecture Notes

## Vision

A simulation engine for a space colony game in the lineage of Dwarf Fortress and Stellaris. Thousand-star systems, each with worlds, stations, millions of asteroids, fleets, populations with individual histories. The kind of game where the simulation is the content and the renderer is just a viewport into it.

The engine exists because no off-the-shelf engine is designed for this workload. Unity and Unreal optimize for rendering complexity with modest entity counts. This engine optimizes for entity count and simulation depth, with rendering as a secondary concern. The target hardware is a modern desktop (Ryzen 9 class CPU, RTX 4090 class GPU) -- a machine with more compute than the top supercomputer of the year 2000, almost entirely wasted by conventional game architectures.

The ambition is ludicrous-scale simulation at interactive framerates. The engine is the foundation; the game is built on top.

## Language: C23

The engine is written in C23, compiled exclusively with Clang 17+. The C23 standard was in beta when development began (2022-2023); compiler coverage is now mature.

C was chosen deliberately over C++ for control, simplicity, and ABI stability. Where C lacks modern conveniences (ownership, scoped cleanup, type-safe generics), the engine uses targeted compiler extensions and C23 features rather than switching languages. The philosophy is: know exactly what the machine does, control it explicitly, and use the smallest possible abstraction to make that control ergonomic.

## Core Architectural Principles

### 1. Region-Based Memory Management (Scoped Arenas)

The central insight, arrived at independently and later confirmed by the academic literature (Tofte & Talpin's region inference for ML, Cyclone's explicit regions, and the game-dev arena tradition of Casey Muratori and Ryan Fleury):

**Most allocations in a game frame are scope-shaped.** They are born together, used together, and should die together. Per-object malloc/free is the wrong granularity -- it fragments memory, pollutes the cache, and forces the programmer to track individual lifetimes.

Instead: allocate from a **scoped heap** (a mimalloc local heap), and destroy the entire heap when the scope exits. Setup and teardown cost is O(regions), not O(objects). A gigabyte of frame-scratch data dies in microseconds because `mi_heap_destroy` hands pages back without iterating contents.

The mechanism in C is `__attribute__((cleanup))`:

```c
#define LOCALHEAPATTR __attribute__((__cleanup__(ano_heap_release)))

// Usage: heap is automatically destroyed when it leaves scope.
mi_heap_t *frameHeap LOCALHEAPATTR = mi_heap_new();
```

This is not a hack -- it's the same mechanism used by systemd (`_cleanup_`), GLib (`g_autoptr`), and the Linux kernel. Given the Clang-only policy, it's fully supported and well-defined.

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

Influenced directly by Michael L. Scott's work (Michael & Scott queues, hazard pointers) and his lecture series. The goal is not merely "fast locks" but **progress guarantees**: obstruction-free, lock-free, or wait-free primitives that never block a thread on another thread's scheduling.

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

But the architect has a soft spot for functional programming and "things being correct and sorting themselves out." The resolution is that **arenas are secretly the FP memory model with the GC removed**. Immutable-ish data allocated into a scoped region, processed, results copied out, region dropped. That's a functional transformation with deterministic cleanup. The engine doesn't need to be purely functional; it needs the *properties* of functional code (referential transparency within a scope, no surprise mutation, predictable resource lifetimes) achieved through structural discipline rather than language enforcement.

### 4. Scoped Resolution (Simulation LOD)

A thousand star systems cannot all be simulated at full fidelity every tick. The engine must scale simulation detail with player attention:

- **Active system**: full-fidelity simulation. Every asteroid, every ship, every orbital mechanic.
- **Nearby systems**: coarse simulation. Aggregate population, economy, fleet movements. No individual asteroids.
- **Distant systems**: statistical simulation. Trends and events, evaluated infrequently.
- **Unobserved systems**: catch-up simulation on access. When the player jumps to a system that's been asleep for 10,000 ticks, fast-forward its state deterministically.

This is not novel in concept (Stellaris, Dwarf Fortress, and others do variants), but the engine's memory architecture is designed to support it natively: each resolution level is a different arena with different data layouts and tick rates. Promoting a system from coarse to full-fidelity is an arena allocation + state expansion; demoting it is a state compression + arena free.

Details of the scoped resolution algorithms are currently in the architect's head, not in code. Materializing them is a priority once the base ECS and arena system are operational.

### 5. Rendering Philosophy

**No PBR.** The engine does not target photorealism. PBR's per-material cost (roughness maps, metalness maps, environment probes, BRDF LUTs, image-based lighting) optimizes for making 50 objects look photorealistic. This engine optimizes for making a million objects look good. Non-PBR rendering (flat shading, stylized lighting, older visual aesthetics) keeps the material pipeline thin and the per-fragment cost low, freeing GPU budget for entity count.

**Vulkan directly.** The renderer uses the Vulkan API without an abstraction layer. This is deliberate: the engine needs direct control over memory allocation, synchronization, and compute dispatch. The current renderer is a basic rasterization pipeline — functional but rough, largely tutorial-derived, and in need of cleanup rather than replacement.

**Future direction: selective raymarching (SDF).** The long-term rendering vision is a hybrid approach: rasterization for UI, HUD, and conventional geometry; raymarching via signed distance fields for the space environment. SDFs are procedural (asteroids defined by math, not meshes), composable (smooth union/subtraction for constructive geometry), and provide natural LOD (fewer march steps at distance). This maps directly onto the scoped resolution hierarchy. However, rasterization is the immediate path — raymarching is a later-stage addition once the simulation infrastructure is operational.

## Current State (June 2026)

### What Exists (in code)

**Vulkan renderer (basic, functional):**
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

**Platform abstraction:**
- Separate implementations for Linux and Windows (memory, time, filesystem)
- Cross-compilation support via CMake toolchain files (Clang targeting MinGW-w64)

**Build system:**
- CMake with platform-specific toolchain files
- Release, Debug, and Test build configurations
- Build scripts for Linux (`build.sh`) and Windows (`build.bat`)

### What Exists (in the architect's head, not yet materialized)

- Complete arena hierarchy (process > level > frame > scratch > pool)
- Lock-free MPSC queue design for the logger and event bus
- ECS architecture and component storage layout
- Event bus for inter-system communication
- Scoped resolution algorithms for multi-scale simulation
- The simulation game itself (star systems, worlds, populations, economies, fleets)
- Novel approaches to deterministic catch-up simulation
- String type with ownership semantics (ptr + len + capacity, copy-on-slice, scoped cleanup)

### What Needs to Be Built (bottom-up, in dependency order)

**Step 1 -- High-performance logger:**
Standalone module. Lock-free MPSC enqueue using fetch_add + commit-header pattern, inlined directly -- no dependency on a generic lock-free library. Flusher thread via `anoptic_threads`. Wire up `ano_log_output_dir`, implement `ano_log_interval`, test file output. This is the first module that exercises arenas + atomics + threads together, and provides instrumentation for everything after.

**Step 2 -- Dependency update:**
Bump GLFW, stb, jsmn, mimalloc submodules to latest stable. Quick audit for API changes. Fold mimalloc finalization (step 3) into this -- the integration is already done, this is just a version bump and validation that `mi_heap_new` / `mi_heap_destroy` / `mi_heap_zalloc_aligned` still behave. Low risk, low effort.

**Step 3 -- mimalloc finalization:**
Confirm hugepage support still works on current version. Validate scoped heap teardown (`LOCALHEAPATTR`). Ensure global override (`mimalloc-override.h`) is clean. May merge with step 2.

**Step 4 -- ano_strings:**
Owned string type: `{char* ptr, uint32_t len, uint32_t capacity}` with `LOCALHEAPATTR`-style scoped cleanup. Allocations go through a heap parameter so strings can live in any arena. Copy-on-slice. ~150 lines. UTF-8 support deferred -- UTF-8 is byte-transparent in storage, so the string type doesn't need to know about it. UTF-8 validation/iteration added later as a layer on top, only when the text renderer demands it.

**Step 5 -- Lock-free collections:**

*Phase A: Classic implementations.* Michael & Scott queue, bounded MPMC ring buffer (Vyukov-style). Correct, tested, benchmarked. These serve as baselines and are usable immediately.

*Phase B: Cache-line-striped lock-free structures (experimental/novel).*

The core idea: align the concurrency model to the hardware coherency unit. The x86 cache coherency protocol (MESI/MESIF) already enforces exclusive ownership at the cache-line level (64 bytes). Instead of per-item atomic operations (classic M&S), make the cache line the unit of ownership transfer.

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

This gets batched throughput (amortizing sync cost over N items per stripe) with lock-free progress guarantees (a stalled producer leaves an uncommitted stripe, doesn't block anyone). The 90s papers didn't consider this because they reasoned about abstract shared-memory models, not cache controllers. On a 16-core Ryzen where cache-line bouncing is the dominant cost, aligning the algorithm to the coherency unit is the natural move.

Open problems:
- Gap handling at stripe granularity (out-of-order publication)
- Variable-size data (fixed-size event structs under 64 bytes is the likely constraint)
- Formal linearizability argument (TLA+ or hand proof)
- Benchmarking against classic M&S and LCRQ on many-core hardware

If this works and benchmarks well, it's worth a paper (DISC or PPoPP). Classic implementations come first as the baseline.

Target structures: ring buffers, queues, heaps. These serve the event bus, job system, and inter-system communication.

**Step 6 -- Additional data structures (as needed):**
Build structures in tandem with the features that operate on them, not speculatively. stb_ds is acceptable as a stopgap for prototyping (e.g., hash maps during renderer work) without long-term commitment.

**Step 7 -- Renderer rewrite:**
Full rewrite of the Vulkan renderer. The current implementation is tutorial-derived with poor system design. The rewrite builds the renderer as a proper subsystem: allocates from scratch arenas, logs through the real logger, communicates through the event bus. Scope for v1: one render pass, one pipeline, geometry on screen, driven by the event bus. No PBR. Rasterization only for now. stb_image retained for texture loading.

**Step 8 -- Event bus + input:**
Global, thread-agnostic event bus. Possibly two buses: one monotonic per-item (classic lock-free, for ordered events like input), one with lock-free cache-line stripes (for high-throughput bulk events like physics/simulation updates). GLFW callbacks enqueue input events; game loop dequeues. Clean producer/consumer boundary. This infrastructure also serves future physics integration.

**Step 9 -- Main game loop + first visual output:**
The integration milestone. Input moves camera, event bus carries input, simulation updates transforms, renderer draws the frame, all allocated from frame arenas, all logged. A sphere on screen through the full pipeline. This is v0.1: proof that every layer works together. Everything after is building the actual game on trusted infrastructure.

### Known Technical Debt

- glTF parser does loose malloc/free instead of arena allocation
- Vulkan cleanup has copy-paste bugs (partially fixed, June 2026)
- Logger file output entirely non-functional (commented out)
- `autoStringTest()` runs a ~1 GiB allocation on every debug launch (belongs in test suite)
- `recordCommandBuffer` binds only entity[0]'s buffers but loops draw calls over all entities
- `log_strings[]` defined in header; duplicated per translation unit
- Test suite disabled in CMake
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
