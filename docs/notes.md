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

**Layer 0 -- Memory (extend what exists):**
- Frame arena API: `ano_arena_frame()`, reset at top of main loop
- Scratch arena API for parsers and transient work
- Pool/slab allocator for entities with dynamic lifetimes

**Layer 1 -- Concurrency primitives:**
- Lock-free MPSC bounded queue (the fetch_add + commit-header design)
- Thread pool on top of `anoptic_threads`

**Layer 2 -- Logger (finish it):**
- Wire up `ano_log_output_dir` to set the file path
- Implement `ano_log_interval` as a flusher thread
- Replace mutex with lock-free enqueue
- Uncomment and test file output

**Layer 3 -- ECS:**
- Entity ID system (generational indices)
- Component storage (sparse sets or archetypes)
- System registration and iteration
- Arena-backed component allocation

**Layer 4 -- Event bus:**
- MPSC/MPMC queue for inter-system events
- Type-safe event dispatch

**Layer 5 -- Simulation:**
- Star system data model
- Multi-resolution simulation ticking
- Scoped resolution promotion/demotion
- Deterministic catch-up

**Layer 6 -- The game:**
- Everything above, serving an actual playable thing

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
