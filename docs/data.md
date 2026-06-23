# Safety Through Geometry

### Region-Based Memory, Wait-Free Concurrency, and Type Theory as the Foundations of Anoptic

> **Thesis.** Systems programming has long been told it must choose between two
> safety regimes: a tracing garbage collector (safe, ergonomic, non-deterministic)
> or a borrow checker (safe, deterministic, hostile to mutable aliasing). Anoptic
> rejects the dichotomy. A million-entity simulation needs *unrestricted* mutation
> of shared state at speed — and it can have it, with correctness, by deriving
> safety from **architectural geometry** rather than from restrictive language
> semantics. The geometry has three axes: memory bound to scope, concurrency bound
> to hardware, and structure bound to types. This document synthesizes the research
> behind each axis and maps it onto the engine as it actually exists today.

This is the manifesto: *all allocations through arenas or thread-local heaps; no mutexes outside the Vulkan backend; C23, no heavyweight deps.* Those aren't arbitrary house rules — each is the direct operational consequence of a well-developed line of computer-science research. The good news, and the reason to be optimistic, is that the hard theory was done decades ago and the C23 toolchain finally makes it ergonomic.

---

## Pillar I — Memory: Regions Bound to Scope

### The lineage

**Tofte & Talpin (1994)** formalized *region-based memory management*: instead of tracking individual objects, group allocations into regions whose lifetimes are inferred statically and reclaimed wholesale in O(1). Their `letregion ρ in e` construct binds a region's lifetime to a lexical scope — allocated on entry, destroyed on exit — and a **type-and-effect system** proves no value outlives its region. The key trick is *effect masking*: an access effect `access(ρ)` that occurs entirely inside `letregion ρ` is erased from the expression's outward effect. If the whole program type-checks to an empty residual effect, every memory access provably happened inside a live region. No GC, no runtime checks.

The theory grew two important branches:

- **Calculus of Capabilities** (Walker, Crary, Morrisett, 1999) decoupled allocation from deallocation — explicit `newrgn`/`freergn` instead of strict LIFO scoping — by threading a static *capability* set through the type system. This is what lets regions serve event loops, state machines, and continuation-passing code where lifetimes aren't tree-shaped.
- **Cyclone** (Grossman, Morrisett, Jim, Hicks, Cheney, Wang) carried the ideas into a real C dialect. Pointers carry their region (`int *ρ`); the `regions_of(τ)` operator plus region subtyping make dangling-pointer dereference a *compile-time* error with zero runtime checks. Cyclone is the existence proof that C idioms and region safety can coexist.

### What Anoptic already does

The engine implements the Tofte–Talpin *frame region* directly. From `include/anoptic_memory.h`:

```c
#define LOCALHEAPATTR  __attribute__((__cleanup__(ano_heap_release)))
// usage:
mi_heap_t *frameHeap LOCALHEAPATTR = mi_heap_new();
```

When `frameHeap` leaves scope the compiler emits an inline call to `ano_heap_release()`, destroying every allocation made against that heap in a single O(1) reclamation. That *is* `letregion`, expressed in C. mimalloc's per-heap arenas give us the contiguous backing store; the `cleanup` attribute gives us the lexical boundary. Effect masking we get socially rather than formally — by convention and review — but the operational shape is identical.

**A precision note worth keeping:** `__attribute__((cleanup))` is a GCC/Clang extension, *not* part of ISO C23. C23 standardizes the `[[...]]` attribute syntax and `typeof`, but `cleanup` remains a (universally supported) GNU extension. We depend on it deliberately and should say so plainly rather than imply the standard blesses it.

### The arena hierarchy

The natural structure is a small tower of region lifetimes, each reclaimed by a different mechanism:

| Arena    | Lifetime                  | Reclaimed by                        | Holds |
|----------|---------------------------|-------------------------------------|-------|
| Process  | whole run                 | OS unmap at `exit()`                | global singletons, immutable tables |
| Session  | a major phase / level     | explicit teardown on transition     | persistent world state, large assets |
| Frame    | one simulation tick       | `cleanup` at the loop tail          | transient math, physics scratch, transforms |
| Scratch  | one parse / one block     | `cleanup` at block exit             | glTF/JSON intermediate buffers, dropped instantly |

### One genuine footgun

If you wrap a `cleanup` variable inside a GCC statement-expression macro (`({ ... })`) and *return the pointer*, the destructor fires when the inner statement-expression scope closes — yielding a deterministic use-after-free. The rule: a `cleanup` binding must live in the **caller's** scope, never escape a statement-expression. This bites people building "fat pointer" string-view macros; worth a comment wherever we do it.

---

## Pillar II — Concurrency: Wait-Freedom Bound to Hardware

This is the pillar behind *"no mutexes outside the Vulkan backend."* A mutex serializes cores and lets one preempted thread stall everyone. Lock-free and wait-free structures guarantee system-wide (or per-thread) progress using atomic hardware primitives — **Compare-And-Swap** and **Fetch-And-Add** — instead of blocking.

### The reclamation problem

**Michael & Scott** showed (the canonical MS-queue) that rich data structures can be made non-blocking with CAS/FAA. But removing locks creates a second problem: if thread A unlinks and frees a node while thread B holds a stale pointer into it, B faults. GC languages dodge this implicitly (and pay non-deterministic latency). Lock-free C must solve it explicitly with **Safe Memory Reclamation (SMR)**: *a node is reclaimed only once no thread can still reach it.*

**Interval-Based Reclamation** (Wen, Izraelevitz, Cai, Beadle & Scott, PPoPP
2018) is the modern answer. Each operation runs inside an interval (`start_op` / `end_op`); retired nodes are stamped with the current epoch and parked on a thread-local list; a node is freed only once its stamp precedes the oldest interval any thread could still be inside. IBR keeps the bounded overhead of hazard pointers without their per-read memory-barrier tax.

A worthwhile optimization is to source the interval clock from a **hardware timestamp counter** rather than a shared software epoch, removing the cache-line contention of a global counter. The research calls this "TSC-IBR"; treat that as *a technique* (hardware-clock interval source) rather than a fixed canonical algorithm name. **The counter is platform-specific, so it goes through `anoptic_time`, never inline:** x86-64 has `rdtsc` (wrapped by `clock_gettime` on Linux, `QueryPerformanceCounter` on Windows); arm64/Apple Silicon has `CNTVCT_EL0`, most portably reached via `mach_absolute_time()`. One `ano_*` interface, three lowerings.

| SMR scheme        | Mechanism                              | Strength                          | Cost |
|-------------------|----------------------------------------|-----------------------------------|------|
| Hazard pointers   | publish addresses being read           | bounded memory, wait-free         | heavy per-read fences |
| RCU               | readers free-run, writers wait grace   | ~free reads, read-heavy scaling   | unbounded delay if a reader stalls |
| Epoch-based (EBR) | shared global epoch counter            | low barrier overhead, simple      | one stalled thread blocks all reclamation |
| Interval (IBR)    | per-op intervals, hardware/epoch clock | high throughput, bounded          | needs a reliable monotonic clock source |

### False sharing, and turning MESI into a feature

Cores keep caches coherent with the **MESI** protocol at **cache-line granularity**. Two threads writing *different* variables that happen to share one line force the line to ping-pong between cores — *false sharing* — and it silently erases the benefit of going lock-free. The fix is to pad hot, independently-written fields to their own line with `alignas`, so ownership transfers cleanly at line boundaries via release/acquire stores. Done right, MESI stops being a tax and becomes the synchronization mechanism: a single release-store publishes a fully-written, cache-aligned slot.

> **A constant the platform layer must own.** The research assumes 64-byte cache
> lines throughout. That holds on x86-64 (Linux and Windows) — but **Apple Silicon
> uses 128-byte lines** (`sysctl hw.cachelinesize` → `128` on this M1). A hardcoded
> `alignas(64)` would pad to *half* a line on macOS and still false-share. So the
> line width is not a literal: define one engine-wide `ANO_CACHELINE` (64 on
> x86-64, 128 on `__aarch64__`/Apple), resolve it in the abstraction layer, and
> align every hot lock-free slot to it. C has no standard
> `hardware_destructive_interference_size`, so this abstraction is on us.

### What Anoptic already does

The logger is the first lock-free citizen. `src/logging/logging_core.c` already keeps an `_Atomic int tail_index` over a shared buffer with an `enqueue_log_string` producer path — the seed of a many-producer / single- consumer log bus. The maturation path is textbook: reserve a slot with one atomic `fetch_add`, write into an `ANO_CACHELINE`-aligned slot, then publish with an `atomic_store(…, memory_order_release)` on a commit header so the consumer (`memory_order_acquire`) flushes only fully-written, contiguous runs — no syscall, no lock, no gap-problem ambiguity.

And the hardware cooperates on every target, because the atomics *interface* hides the lowering: on Apple Silicon the M1 reports `FEAT_LSE`, so `_Atomic` CAS and add become **single instructions** (`CAS`, `LDADD`, `SWP`); on x86-64 they lower to `lock`-prefixed ops. We write C11 `<stdatomic.h>` once and each platform's compiler emits the right thing — lock-free is cheap on all three, not aspirational.

---

## Pillar III — Structure: Types as Zero-Cost Layout

To orchestrate raw memory and atomics without a C++ type lattice or a borrow checker, we borrow ideas from type theory (Pierce, *TaPL*) — but as *design inspiration realized in layout*, not as formal machinery.

- **Intersection / union types & the Forsythe merge.** An intersection type `A ∩ B` describes a value usable as both `A` and `B`; the *merge operator* that builds such values traces to Reynolds' **Forsythe** (1988). The engine's data-oriented analogue: an entity is just an index into several contiguous Struct-of-Arrays; a system that processes everything with both `Physics` and `Render` components operates on what *resembles* an intersection — the entity effectively "has both types" — but does so with sequential SIMD scans and zero vtable indirection. This is an **evocative analogy**, not a theorem: ECS composition is closer to a product over component arrays than to Pierce's intersection types. The payoff is real regardless: SoA maximizes L1 locality and kills vtable pointer-chasing for million-entity loops.

- **Union type-punning is legal C.** Unlike C++ (where it's UB and needs `bit_cast`), reading a different union member than was written is well-defined in C since C99 (§6.5.2.3, footnote) and remains so in C23. That lets us build honest tagged-variant and view types at the bare metal without ceremony.

- **`_Generic` dispatch.** C23's `_Generic` gives compile-time, type-directed selection — polymorphic interface macros that route to the right specialized routine with no runtime branch and no vtable. The right tool for typed, zero-overhead front-ends over our atomic/queue primitives.

---

## Pillar IV — C23 as the Ergonomic Substrate

The optimism is warranted because the toolchain finally meets the theory — and it meets it on every target: gcc or clang on Linux, clang on macOS (Homebrew LLVM clang 22, since Apple clang 15 is too old for C23) and on Windows, all speaking the same C23.

- **`[[unsequenced]]` and `[[reproducible]]`** (genuinely new in C23). The former marks effectless, stateless, idempotent functions (≈ GNU `const`); the latter effectless-but-may-read functions (≈ GNU `pure`). On our math core — transforms, orbital integration, hashing — they license the compiler to hoist, CSE, and reorder with confidence. Standardized purity annotations, no GNU-ism required.

- **`typeof` and `auto`** (C23). These give *local* type deduction at the point of declaration — handy for keeping `size_t` discipline across queue and arena boundaries so a 32-bit `int` index can't silently truncate past `INT_MAX` (~2.1 billion) in a billion-entity world. It's fair to call this *inspired by* Pierce & Turner's "Local Type Inference" (1998); it is not a full implementation of it — C23 `auto` simply copies the initializer's type, with none of the bidirectional propagation that paper describes. Useful framing, not an equivalence.

---

## The Hardware Is Not Abstract — So the Platform Layer Is

The research's performance section is implicitly x86-64/Linux: `rdtsc`, `PDPE1GB` 1 GiB hugepages, 64-byte lines, 4 KiB pages. But Anoptic ships on **three platforms at once** — Linux/x86-64 (SSA's box), Windows/x86-64, and macOS/arm64 (Apple Silicon) — so those numbers are *one column of a matrix*, not the ground truth. This is exactly why the engine splits `include/` (the platform-agnostic interface — every `ano_*` call) from `src/*_linux.c` / `*_win64.c` / `*_macos.c` (the per-platform implementation). The *principles* below transfer perfectly to all three; the *constants and instructions* differ, so every one of them is a value the platform layer resolves — never a literal hardcoded in the core.

| Concern           | x86-64 (Linux / Windows)                              | arm64 (macOS, Apple Silicon)                    | Abstracted through |
|-------------------|------------------------------------------------------|-------------------------------------------------|--------------------|
| Cache line        | 64 bytes                                             | **128 bytes** (`hw.cachelinesize`)              | `ANO_CACHELINE` constant |
| Base page         | 4 KiB                                               | **16 KiB** (`hw.pagesize`)                      | arena sizing in `anoptic_memory` |
| Large pages       | Linux THP / `MAP_HUGETLB`; Windows `MEM_LARGE_PAGES` | kernel-managed; no portable 1 GiB reserve       | `anoptic_memory` backend |
| Timestamp counter | `rdtsc` (via `clock_gettime` / `QueryPerformanceCounter`) | `CNTVCT_EL0` / `mach_absolute_time()`     | `anoptic_time` |
| Atomic lowering   | `lock`-prefixed ops, `cmpxchg`                       | **LSE** (`CAS` / `LDADD` / `SWP`, single-instr) | `<stdatomic.h>` `_Atomic` |
| Thread primitives | full POSIX (Linux) / Win32 (Windows)                 | POSIX **minus** spinlock & barrier              | `anoptic_threads` (+ Darwin compat shim) |

The optimistic reading: the platform layer turns this fragmentation into *leverage*. The same lock-free, arena-based core compiles optimally on all three, and each target brings a gift — Apple Silicon's 16 KiB pages quadruple TLB reach for free and its LSE atomics make CAS/FAA single instructions; x86-64's 64-byte line means tighter padding; Linux gives SSA the richest hugepage and NUMA control. The job isn't to crown a winner — it's to keep every constant flowing through the `anoptic_*` headers so no platform is a second-class citizen. That last table row is the concrete work this branch is doing right now: macOS libpthread ships no `pthread_spinlock_t` or `pthread_barrier_t`, so the Darwin implementation supplies them in a compat shim while `include/anoptic_threads.h` stays byte-identical across all three. Get the non-GPU core green on macOS without touching the Linux or Windows paths — that is the headless port.

---

## Synthesis: One Architecture, Three Axes

These are not four separate research interests bolted together — they are three orthogonal axes of a single discipline, and Anoptic already sits at their intersection:

1. **Memory is bound to scope.** Arenas + `cleanup` give Tofte–Talpin region safety in plain C. Allocation is O(1), reclamation is O(1), and lifetime is a property of the source's shape — no GC, no tracing, no per-object `free`.
2. **Concurrency is bound to hardware.** CAS/FAA + interval-based reclamation + cache-line-aligned ownership give wait-free progress. The MESI protocol, respected at the cache line (`ANO_CACHELINE` — 64 B on x86-64, 128 B on Apple Silicon), becomes the publish/subscribe mechanism rather than a hidden tax. No mutexes outside Vulkan, by construction.
3. **Structure is bound to types.** SoA layout, legal C union punning, `_Generic` dispatch, and C23 purity attributes give polymorphism and safety as *layout and compile-time* facts — never as runtime indirection.

The dichotomy was false. You do not need a tracing collector to be safe, nor a borrow checker to be correct under mutation. You need memory whose lifetime is visible in the code's geometry, concurrency whose ordering is visible in the hardware's geometry, and structure whose meaning is visible in the type's geometry. Decades of theory — Tofte & Talpin, Walker–Crary–Morrisett, Cyclone, Michael & Scott, the IBR line, Reynolds, Pierce — converge on it, and C23 plus modern clang/gcc — across Linux, Windows, and macOS — finally make it both legal and fast on every machine the team runs. Anoptic is the engine that takes the convergence literally.

---

## Provenance & precision

So this document can be trusted in-tree, here is exactly where it tightens or hedges the source research:

- `__attribute__((cleanup))` is a **GNU/Clang extension, not ISO C23** (C23 *does* standardize `typeof` and `[[...]]` attributes; `cleanup` is not among them).
- **Cache lines and page sizes are platform constants, not universals**: 64-byte lines / 4 KiB pages on x86-64 (Linux, Windows); 128-byte lines / 16 KiB pages on Apple Silicon (both measured on this M1).
- **`rdtsc` and `PDPE1GB` 1 GiB hugepages are x86-64-specific.** arm64 uses `CNTVCT_EL0` / `mach_absolute_time()`; large-page reservation differs per OS (Linux THP/`MAP_HUGETLB`, Windows `MEM_LARGE_PAGES`, macOS kernel-managed) — all reached through the platform layer.
- **"TSC-IBR"** is described here as a technique (hardware-clock interval source), not asserted as a canonical named algorithm from the IBR literature.
- **ECS-as-intersection-types** and **C23 `auto` as Pierce–Turner local type inference** are framed as *analogies/inspiration*, not formal equivalences — the engineering payoff (SoA locality, `size_t` discipline) holds either way.
- `[[unsequenced]]`/`[[reproducible]]`, `typeof`, `_Generic`, and C99/C23 union type-punning are reported as stated — those are accurate.

macOS/arm64 figures: `sysctl` on this Apple M1, macOS 14.5 (23F79). The x86-64 figures (64-byte line, 4 KiB page) are the standard platform values for Linux and Windows.
