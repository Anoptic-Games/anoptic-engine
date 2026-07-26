# Safety Through Geometry

### Region-Based Memory, Wait-Free Concurrency, and Type Theory as the Foundations of Anoptic

> **Thesis.** Systems programming gets told it must pick one safety regime: a tracing GC (safe, ergonomic, non-deterministic) or a borrow checker (safe, deterministic, hostile to mutable aliasing). Anoptic rejects the dichotomy. A million-entity simulation needs *unrestricted* mutation of shared state at speed. It can have that, with correctness, by deriving safety from **architectural geometry**. Three axes: memory bound to scope, concurrency bound to hardware, structure bound to types. This document maps the research behind each onto the engine as it exists today.

This is the manifesto: *all allocations through arenas or thread-local heaps; no mutexes outside the Vulkan backend; C23, no heavyweight deps.* Decades of theory; C23 makes it ergonomic.

---

## Pillar I: Memory: Regions Bound to Scope

### The lineage

**Tofte & Talpin (1994)** formalized *region-based memory management*: group allocations into regions whose lifetimes are inferred statically and reclaimed wholesale in O(1). Their `letregion ρ in e` construct binds a region's lifetime to a lexical scope, allocated on entry, destroyed on exit, and a **type-and-effect system** proves no value outlives its region. *Effect masking*: an access effect `access(ρ)` wholly inside `letregion ρ` is erased from the outward effect. Empty residual effect ⇒ every access was inside a live region. No GC, no runtime checks.

Two branches:

- **Calculus of Capabilities** (Walker, Crary, Morrisett, 1999) decoupled allocation from deallocation via explicit `newrgn`/`freergn`, threading a static *capability* set through the type system. Regions for event loops, state machines, and CPS where lifetimes aren't tree-shaped.
- **Cyclone** (Grossman, Morrisett, Jim, Hicks, Cheney, Wang) carried the ideas into a C dialect. Pointers carry their region (`int *ρ`). The `regions_of(τ)` operator plus region subtyping make dangling-pointer dereference a *compile-time* error. Existence proof: C idioms and region safety coexist.

### What Anoptic already does

The engine implements the Tofte-Talpin *frame region* directly. From `include/anoptic_memory.h`:

```c
#define LOCALHEAPATTR  __attribute__((__cleanup__(ano_heap_release)))
// usage:
mi_heap_t *frameHeap LOCALHEAPATTR = mi_heap_new();
```

When `frameHeap` leaves scope the compiler emits an inline call to `ano_heap_release()`, destroying every allocation against that heap in one O(1) reclamation. That *is* `letregion` in C. mimalloc per-heap arenas: contiguous backing. `cleanup` attribute: lexical boundary. Effect masking: by convention and review, not formally. Operational shape identical.

**A precision note worth keeping:** `__attribute__((cleanup))` is a GCC/Clang extension. C23 standardizes the `[[...]]` attribute syntax and `typeof`, but `cleanup` remains a (universally supported) GNU extension. We depend on it deliberately and should say so plainly.

### The arena hierarchy

The natural structure is a small tower of region lifetimes, each reclaimed by a different mechanism:

| Arena    | Lifetime                  | Reclaimed by                        | Holds |
|----------|---------------------------|-------------------------------------|-------|
| Process  | whole run                 | OS unmap at `exit()`                | global singletons, immutable tables |
| Session  | a major phase / level     | explicit teardown on transition     | persistent world state, large assets |
| Frame    | one simulation tick       | `cleanup` at the loop tail          | transient math, physics scratch, transforms |
| Scratch  | one parse / one block     | `cleanup` at block exit             | glTF/JSON intermediate buffers, dropped instantly |

### One genuine footgun

If you wrap a `cleanup` variable inside a GCC statement-expression macro (`({ ... })`) and *return the pointer*, the destructor fires when the inner statement-expression scope closes, yielding a deterministic use-after-free. The rule: a `cleanup` binding must live in the **caller's** scope, never escape a statement-expression. This bites people building "fat pointer" string-view macros. Worth a comment wherever we do it.

---

## Pillar II: Concurrency: Wait-Freedom Bound to Hardware

Pillar behind *"no mutexes outside the Vulkan backend."* A mutex serializes cores; one preempted thread stalls everyone. Lock-free / wait-free structures: system-wide (or per-thread) progress via atomic hardware primitives, **Compare-And-Swap** and **Fetch-And-Add**, not blocking.

### The reclamation problem

**Michael & Scott** (canonical MS-queue): rich non-blocking structures with CAS/FAA. Second problem: thread A unlinks and frees a node while thread B holds a stale pointer → B faults. GC languages dodge this (non-deterministic latency). Lock-free C needs **Safe Memory Reclamation (SMR)**: *reclaim a node only once no thread can still reach it.*

**Interval-Based Reclamation** (Wen, Izraelevitz, Cai, Beadle & Scott, PPoPP 2018): each op runs in an interval (`start_op` / `end_op`). Retired nodes stamped with current epoch, parked on a thread-local list. Free only when stamp precedes the oldest live interval. IBR keeps hazard-pointer-bounded overhead.

Optimization: source the interval clock from a **hardware timestamp counter** (no global-counter cache-line contention). Research name: "TSC-IBR". Treat as *a technique* (hardware-clock interval source). **The counter is platform-specific, so it goes through `anoptic_time`:** x86-64 has `rdtsc` (wrapped by `clock_gettime` on Linux, `QueryPerformanceCounter` on Windows). arm64/Apple Silicon has `CNTVCT_EL0`, most portably via `mach_absolute_time()`. One `ano_*` interface, three lowerings.

| SMR scheme        | Mechanism                              | Strength                          | Cost |
|-------------------|----------------------------------------|-----------------------------------|------|
| Hazard pointers   | publish addresses being read           | bounded memory, wait-free         | heavy per-read fences |
| RCU               | readers free-run, writers wait grace   | ~free reads, read-heavy scaling   | unbounded delay if a reader stalls |
| Epoch-based (EBR) | shared global epoch counter            | low barrier overhead, simple      | one stalled thread blocks all reclamation |
| Interval (IBR)    | per-op intervals, hardware/epoch clock | high throughput, bounded          | needs a reliable monotonic clock source |

### False sharing, and turning MESI into a feature

Cores keep caches coherent with the **MESI** protocol at **cache-line granularity**. Two threads writing *different* variables that share one line force ping-pong. *False sharing* erases the lock-free benefit. Fix: pad hot, independently-written fields to their own line with `alignas`, so ownership transfers at line boundaries via release/acquire stores. Done right, MESI is the sync: one release-store publishes a fully-written, cache-aligned slot.

> **A constant the platform layer must own.** The research assumes 64-byte cache lines throughout. That holds on x86-64 (Linux and Windows), but **Apple Silicon uses 128-byte lines** (`sysctl hw.cachelinesize` → `128` on this M1). A hardcoded `alignas(64)` would pad to *half* a line on macOS and still false-share. So define one engine-wide `ANO_CACHELINE` (64 on x86-64, 128 on `__aarch64__`/Apple), resolve it in the abstraction layer, and align every hot lock-free slot to it. C has no standard `hardware_destructive_interference_size`, so this abstraction is on us.

### What Anoptic already does

The logger is the first lock-free citizen. `src/log/log_core.c` keeps an `_Atomic int tail_index` over a shared buffer with an `enqueue_log_string` producer path: seed of a many-producer / single-consumer log bus. Maturation: reserve a slot with atomic `fetch_add`, write into an `ANO_CACHELINE`-aligned slot, publish with `atomic_store(…, memory_order_release)` on a commit header; consumer (`memory_order_acquire`) flushes only fully-written contiguous runs. No syscall, no lock, no gap-problem ambiguity.

Atomics *interface* hides the lowering: Apple Silicon M1 reports `FEAT_LSE`, so `_Atomic` CAS and add are **single instructions** (`CAS`, `LDADD`, `SWP`). x86-64: `lock`-prefixed ops. One C11 `<stdatomic.h>` write; each compiler lowers correctly. Lock-free is cheap on all three.

---

## Pillar III: Structure: Types as Zero-Cost Layout

To orchestrate raw memory and atomics without a C++ type lattice or a borrow checker, we borrow type-theory ideas (Pierce, *TaPL*) as *design inspiration realized in layout*.

- **Intersection / union types & the Forsythe merge.** An intersection type `A ∩ B`: value usable as both `A` and `B`. Merge operator: Reynolds' **Forsythe** (1988). Engine analogue: an entity is an index into contiguous Struct-of-Arrays. A system over both `Physics` and `Render` resembles an intersection ("has both types") via sequential SIMD scans, zero vtable indirection. **Evocative analogy**: ECS composition is closer to a product over component arrays than to Pierce intersection types. Payoff holds either way: SoA maximizes L1 locality and kills vtable chasing for million-entity loops.

- **Union type-punning is legal C.** Reading a different union member than was written is well-defined in C since C99 (§6.5.2.3, footnote) and remains so in C23. Honest tagged-variant and view types at the bare metal.

- **`_Generic` dispatch.** C23 `_Generic`: compile-time, type-directed selection. Polymorphic interface macros route to the specialized routine. Typed, zero-overhead front-ends over atomic/queue primitives.

---

## Pillar IV: C23 as the Ergonomic Substrate

Toolchain meets theory on every target: gcc or clang on Linux, clang on macOS (Homebrew LLVM clang 22; Apple clang 15 is too old for C23) and on Windows, all speaking the same C23.

- **`[[unsequenced]]` and `[[reproducible]]`** (new in C23). Former: effectless, stateless, idempotent (≈ GNU `const`). Latter: effectless-but-may-read (≈ GNU `pure`). Math core (transforms, orbital integration, hashing): hoist, CSE, reorder. Standardized purity annotations.

- **`typeof` and `auto`** (C23). *Local* type deduction at declaration. Keeps `size_t` discipline across void and arena boundaries so a 32-bit `int` index can't silently truncate past `INT_MAX` (~2.1 billion) in a billion-entity world. Fair to call *inspired by* Pierce & Turner's "Local Type Inference" (1998). C23 `auto` copies the initializer's type; no bidirectional propagation. Useful framing.

---

## The Hardware Is Not Abstract: So the Platform Layer Is

Research performance section is implicitly x86-64/Linux: `rdtsc`, `PDPE1GB` 1 GiB hugepages, 64-byte lines, 4 KiB pages. Anoptic ships **three platforms**: Linux/x86-64 (SSA's box), Windows/x86-64, macOS/arm64 (Apple Silicon). Those numbers are *one column of a matrix*. Split: `include/` (platform-agnostic `ano_*` interface) vs `src/*_linux.c` / `*_win64.c` / `*_macos.c` (per-platform impl). *Principles* transfer. *Constants and instructions* differ; platform layer resolves each.

| Concern           | x86-64 (Linux / Windows)                              | arm64 (macOS, Apple Silicon)                    | Abstracted through |
|-------------------|------------------------------------------------------|-------------------------------------------------|--------------------|
| Cache line        | 64 bytes                                             | **128 bytes** (`hw.cachelinesize`)              | `ANO_CACHELINE` constant |
| Base page         | 4 KiB                                               | **16 KiB** (`hw.pagesize`)                      | arena sizing in `anoptic_memory` |
| Large pages       | Linux THP / `MAP_HUGETLB`; Windows `MEM_LARGE_PAGES` | kernel-managed; no portable 1 GiB reserve       | `anoptic_memory` backend |
| Timestamp counter | `rdtsc` (via `clock_gettime` / `QueryPerformanceCounter`) | `CNTVCT_EL0` / `mach_absolute_time()`     | `anoptic_time` |
| Atomic lowering   | `lock`-prefixed ops, `cmpxchg`                       | **LSE** (`CAS` / `LDADD` / `SWP`, single-instr) | `<stdatomic.h>` `_Atomic` |
| Thread primitives | full POSIX (Linux) / Win32 (Windows)                 | POSIX **minus** spinlock & barrier              | `anoptic_threads` (+ Darwin compat shim) |

Platform layer turns fragmentation into leverage. Same lock-free, arena-based core on all three; each target brings a gift. Apple Silicon: 16 KiB pages quadruple TLB reach; LSE makes CAS/FAA single instructions. x86-64: tighter 64-byte padding. Linux: richest hugepage and NUMA control for SSA. Every constant through `anoptic_*` headers. Last table row is active work: macOS libpthread has no `pthread_spinlock_t` or `pthread_barrier_t`; Darwin compat shim supplies them while `include/anoptic_threads.h` stays byte-identical. Non-GPU core green on macOS without touching Linux or Windows paths. That is the headless port.

---

## Synthesis: One Architecture, Three Axes

Three orthogonal axes; Anoptic sits at their intersection:

1. **Memory is bound to scope.** Arenas + `cleanup` = Tofte-Talpin region safety in plain C. Alloc O(1), reclaim O(1), lifetime from source shape. No GC, no tracing, no per-object `free`.
2. **Concurrency is bound to hardware.** CAS/FAA + interval-based reclamation + cache-line-aligned ownership = wait-free progress. MESI at the cache line (`ANO_CACHELINE`, 64 B on x86-64, 128 B on Apple Silicon) is the publish/subscribe mechanism. No mutexes outside Vulkan, by construction.
3. **Structure is bound to types.** SoA layout, legal C union punning, `_Generic` dispatch, C23 purity attributes: polymorphism and safety as *layout and compile-time* facts.

Memory lifetime visible in code geometry; concurrency ordering visible in hardware geometry; structure meaning visible in type geometry. Decades of theory (Tofte & Talpin, Walker-Crary-Morrisett, Cyclone, Michael & Scott, IBR, Reynolds, Pierce) converge; C23 plus modern clang/gcc across Linux, Windows, and macOS make it legal and fast on every machine the team runs. Anoptic takes that convergence literally.

---

## Provenance & precision

So this document can be trusted in-tree, here is exactly where it tightens or hedges the source research:

- `__attribute__((cleanup))` is a **GNU/Clang extension** (C23 *does* standardize `typeof` and `[[...]]` attributes; `cleanup` is not among them).
- **Cache lines and page sizes are platform constants**: 64-byte lines / 4 KiB pages on x86-64 (Linux, Windows); 128-byte lines / 16 KiB pages on Apple Silicon (both measured on this M1).
- **`rdtsc` and `PDPE1GB` 1 GiB hugepages are x86-64-specific.** arm64 uses `CNTVCT_EL0` / `mach_absolute_time()`. Large-page reservation differs per OS (Linux THP/`MAP_HUGETLB`, Windows `MEM_LARGE_PAGES`, macOS kernel-managed), all reached through the platform layer.
- **"TSC-IBR"** is described here as a technique (hardware-clock interval source).
- **ECS-as-intersection-types** and **C23 `auto` as Pierce-Turner local type inference** are framed as *analogies/inspiration*. The engineering payoff (SoA locality, `size_t` discipline) holds either way.
- `[[unsequenced]]`/`[[reproducible]]`, `typeof`, `_Generic`, and C99/C23 union type-punning are reported as stated. Those are accurate.

macOS/arm64 figures: `sysctl` on this Apple M1, macOS 14.5 (23F79). The x86-64 figures (64-byte line, 4 KiB page) are the standard platform values for Linux and Windows.
