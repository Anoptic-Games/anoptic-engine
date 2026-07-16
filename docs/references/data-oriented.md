# Data-Oriented Design, Arenas, and Lock-Free Structures — Distilled for Anoptic

A single distillation of the study corpus in `docs/study/`: seven recorded talks, their slides
and code, and five academic papers. Each talk gets its own segment. Techniques are pulled to
the parts that matter and tied back to the Anoptic Engine (see `docs/notes.md`). The closing
section is decision guidance.

The corpus maps cleanly onto the engine's three structural pillars: data-oriented design
(the ECS and the render path), region memory (the arena hierarchy and mimalloc heaps), and
lock-free concurrency (the SPSC bridge, the logger, the planned event bus and striped
structures). The document follows those three pillars.

## Source corpus

| File | Speaker / authors | Talk or paper | Venue, year |
|---|---|---|---|
| `data-oriented-design.txt` + `.pptx` | Mike Acton | Data-Oriented Design and C++ | CppCon 2014 |
| `cpu-caches.txt` + handouts | Scott Meyers | CPU Caches and Why You Care | code::dive 2014 |
| `Culling-BF3.txt` + slides | Daniel Collin (DICE) | Culling the Battlefield (Frostbite 2 / BF3) | GDC 2011 |
| `DoD-guide.txt` | Andrew Kelley | Practical Data-Oriented Design | Handmade Seattle 2021 |
| `Enter-the-arena.txt` | Ryan Fleury | Enter The Arena: Simplifying Memory Management | workshop, ~2023 |
| `Aerna-allocators*.txt` | John Lakos | Local ("Arena") Memory Allocators, parts 1–2 | CppCon 2017 |
| `Nonblocking-Structures-*.txt` | Michael L. Scott | Nonblocking Data Structures, parts 1–2 | lecture, St. Petersburg ~2019 |
| `citations/1996_PODC_queues.pdf` | Michael & Scott | Simple, Fast, and Practical … Concurrent Queue Algorithms | PODC 1996 |
| `citations/1998_JPDC_nonblocking.pdf` | Michael & Scott | Nonblocking Algorithms and Preemption-Safe Locking | JPDC 1998 |
| `citations/2004_DISC_dual_DS.pdf` | Scherer & Scott | Nonblocking … with Condition Synchronization (duals) | DISC 2004 |
| `citations/2105.09508v2.pdf` | Cai, Wen, … Scott | Fast Nonblocking Persistence (nbMontage) | DISC 2021 |
| `citations/2301.00996v2.pdf` | Cai, Wen, Scott | Transactional Composition of Nonblocking Structures (NBTC) | 2023 |

---

# Part I — Data-Oriented Design

The unifying premise across all four talks in this part: a program is a transform of input
data to output data on a finite, knowable machine, and **the cache line is the unit of cost**.
The same 64 bytes move whether you use them or not; the engineering problem is to fill them
with data you will actually consume.

## Segment 1 — Mike Acton, Data-Oriented Design and C++ (CppCon 2014)

The keynote that named the discipline. Acton's frame: Insomniac ships on a hard date into a
33 ms (or 16 ms) frame budget; performance buys the space for new problems.

First principles, verbatim in spirit:
- The purpose of all programs is to transform data from one form to another. If you don't
  understand the data, you don't understand the problem.
- Different data is a different problem and needs a different solution. You cannot future-proof.
- If you don't understand the hardware you can't reason about cost; everything is a data problem.

The three lies Acton attacks, the **three big lies**: that software is a platform (no —
hardware is); that code should model the world (world-modeling hides the data and produces
monolithic, unrelated structures — a `Chair`, a physics chair, a breakable chair share nothing
in their transforms); and that code matters more than data (the programmer is responsible for
the transform, not the code). Resolution: hardware is the platform, design around the data,
solve the transform first.

The cost model he teaches by hand (Jason Gregory's numbers): register ~free, L1 ~3 cycles,
L2 ~20, main RAM ~200+. The compiler reasons about ~1–10% of the problem space (the
arithmetic); the other 90% is memory layout, which it cannot touch.

The canonical refactor is the array-of-structs-with-waste turned into **structure of arrays**.
A monolithic `GameObject` updated one field at a time wastes ~56–60 of 64 bytes per line
(≈90% waste, "10% of the line used"). Pull out only the read/written fields and batch:

```c
struct FooUpdateIn  { float m_Velocity[2]; float m_Foo; };
struct FooUpdateOut { float m_Foo; };

void UpdateFoos(const FooUpdateIn* in, size_t count, FooUpdateOut* out, float f) {
    for (size_t i = 0; i < count; ++i) {
        float mag = sqrtf(in[i].m_Velocity[0]*in[i].m_Velocity[0] +
                          in[i].m_Velocity[1]*in[i].m_Velocity[1]);
        out[i].m_Foo = in[i].m_Foo + mag * f;
    }
}
```

At count = 32 the packed input fills exactly 6 cache lines, output 2 lines, ~5.33 iterations
per input line, ~213 cycles of math per line against a ~200-cycle line read — work and
bandwidth balanced, plus the x86 streaming-prefetch bonus. Roughly 10× faster, and the
code is now reason-about-able for cost-of-change.

Other rules that recur:
- Where there is one, there are many. Look on the time axis. Solve the common case first,
  not the generic case. Keep context; don't throw away data you need.
- A `bool` in a struct is an anti-pattern: ~1 bit of information stored in a byte, but worse,
  it pushes hot fields onto a second cache line. Acton's entropy demo: a per-frame spawn bool
  zipped over 10,000 frames is 915 bytes ≈ 0.732 bits/frame, while naively carrying it costs
  ~2 L2 misses/frame ≈ 1.28 MB moved — ~99.93% noise. Store the decision out of band, make it
  512 times at once, fold it into another transform, or project it into a future command
  buffer.
- Compilers re-read members and re-call functions inside loops even at O2 (shown for MSVC and
  clang). Hoist loop-invariant reads and branches yourself, even the "obvious" ones.
- The OGRE `Node` review: an over-generalized class (~7 bools ⇒ ~128 states, virtual updates,
  unmanaged i-cache, generated names) is fixed by splitting the one switch into three bulk
  loops, triaging by `p(call) × count`, splitting root-vs-parented cases, and finally a packed
  parent-then-children byte-stream. OGRE 1.9 → 2.0 saw ~5×.

For Anoptic. This is the charter for the ECS and the render path. Components as contiguous
plain-data arrays iterated in tight loops is exactly Acton's SoA. Two refinements the engine
should adopt explicitly: split each component into hot fields (transform) and cold fields
(metadata) the way Acton split dictionary keys from values, so the cull and animation passes
touch only what they consume; and treat the bridge's discrete-vs-continuous split as the
"project it into the future" move — continuous motion sent once as GPU parameters is the
command-buffer-into-the-future pattern.

## Segment 2 — Scott Meyers, CPU Caches and Why You Care (code::dive 2014)

The hardware grounding under Acton. Two demos open it. Row-major vs column-major matrix
traversal does identical work but row-major is much faster and scales linearly (two
independent compilers agree, so it is hardware not compiler). And a parallel odd-counter that
writes per-thread slots in a shared array scales *negatively* until each thread accumulates
into a local and writes the shared slot once — then it scales perfectly. Both are cache
effects.

The numbers for an i7-9xx (representative, not special):

| Level | Size | Latency | Note |
|---|---|---|---|
| L1 D/I | 32 KB each, per core | 4 cycles | shared by 2 HW threads |
| L2 | 256 KB per core | 11 cycles | data+instructions |
| L3 | 8 MB | 39 cycles | shared by all cores |
| main RAM | — | 107 cycles | 27× L1 |

Consequences he draws:
- Small is fast. The hardware never learned the time-space trade-off; compact, localized
  code and data that fit in cache are fastest. Effective memory ≈ cache size; main memory is
  so slow you may "as well assume it's not even there."
- Memory moves in cache lines (64 B = 16 int32 = 8 int64). Touch one byte, pay for 64.
  This is why row-major wins and why the `isLive` bool in a fat struct is pathological
  (Bruce Dawson's Xbox tool flagged lines with 2–5% utilization on sight).
- Hardware prefetch follows forward and backward strides, so linear array traversal is the
  most cache-friendly access there is — a linear scan can beat a pointer-chasing tree;
  binary search of a sorted array can beat a heap-based hash table at modest n. Big-O wins
  only once n is large.
- **False sharing**: independent values on one line, written by ≥1 core while others access
  the line concurrently and frequently. Cache coherency invalidates the whole line on any
  write; you get the right answer slowly. Susceptible: globals/statics laid out adjacently,
  heap blocks that share a line, even stack/thread-local data if a pointer escapes. There is
  no automatic fix; develop the intuition (suspicious lack of scalability).
- Cache associativity: a line's address restricts it to a small set of slots (e.g. 8-way).
  Power-of-two strides (notably 512) collide and shrink the effective cache — padding a matrix
  can make it traverse faster.

Instruction-cache guidance: fit the working set; sort heterogeneous sequences by type so
virtual dispatch doesn't thrash the i-cache; make fast paths branch-free with up-front
screening of slow cases; inline to cut branches but not so much that duplication shrinks
effective cache. PGO/WPO automate some of this.

For Anoptic. Direct validation of the SPSC ring keeping the producer's `tail` and consumer's
`head` on separate cache lines — that is textbook false-sharing avoidance, and the same
discipline must hold for the planned cache-line-striped structures and any per-thread logger
counters. The linear-array verdict underwrites the whole ECS scan model. The associativity
warning is a caution for the geometrically-doubling GPU buffers and any power-of-two ring
sizing.

## Segment 3 — Daniel Collin (DICE), Culling the Battlefield (GDC 2011)

DOD applied under a shipping console budget — the production proof of Parts 1–2. The old
hierarchical sphere-tree culling (separate static/dynamic trees, one job per frustum, bitmask
merges) didn't scale, didn't fit streamed sub-levels, and was branch-heavy. The hardware
(PS3/360/PC) punishes non-local data, branches, register-type switches (load-hit-store), and
trees; it rewards local data, SIMD, and parallelism.

The decision: throw out the tree, put everything in linear arrays, and brute-force. The first
naive parallel brute force was already 3× faster than the old system at 1/5 the code, with
predictable data, few branches, easy prefetch, and trivial further optimization. Worlds hold
~15k objects; that count drove the design.

Data layout is a grid of cells (separate grids for render-static, render-dynamic, physics-…),
each cell a structure of arrays over fixed blocks:

```c
struct EntityGridCell { Block* blocks[256]; u8 counts[256]; u32 totalCount; };
// each Block holds parallel arrays:  positions(x,y,z,r), entityInfo(handle), transformData
struct TransformData { half rotation[4]; half minAabb[3]; half pad; half maxAabb[3]; half scale[3]; };
```

Allocation is one pre-allocated pool of ~4k blocks claimed with `AtomicAdd` on a pointer —
SPU-safe lock-free bump. Removal is the swap trick: data needn't be sorted, so swap the
entry with the last and decrement the count. Objects straddling cells go into one wrapping
super-cell, checked alongside.

The hot kernel: jobs grab a block via `interlockedIncrement`, test every frustum, OR the
result into a per-element mask, then filter:

```c
while ((blockIter = interlockedIncrement(&currentBlockIndex) - 1) < blockCount) {
    u32 masks[Block::MaxCount] = {}, frustumMask = 1;
    Block* block = cell->blocks[blockIter];
    for (frustum in frustums, frustumMask <<= 1)
        for (i = 0; i < cell->counts[blockIter]; ++i)
            masks[i] |= frustumMask & intersect(frustum, block->position[i]);
    /* if masks[i]==0 the object is culled in all views; skip it */
}
```

The scalar `intersect` (six plane distance tests, each a load-hit-store and a float branch) is
the "typical C++ rubbish" Acton rails against. The fix is AoS→SoA so one register holds
all X's, another all Y's: a dot product becomes multiply-add ×3 — three instructions for four
dots. Frustum planes are pre-swizzled into `x0x1x2x3 / y… / z…` with planes 4–5 packed as
`x4 x5 x4 x5`. The loop does two spheres at once, 12 dot products in 9 instructions, branch-free
mask merge. On PS3 the SPU Pipelining Assembler (software-pipelined, ~24 cycles/loop = 12
cycles/sphere, both pipes ~100%) bought a further ~35–45%.

Results, 15k spheres, no occlusion: 360 1.55→0.52 ms (1→4 jobs), PC i7 1.0→0.32, PS3
0.85→0.23 (SPUs scale best, being independent). Beyond frustum: project AABB to screen and
cull on screen area, not distance (snipers zoom, so FOV changes), plus software occlusion
— a 256×114 float z-buffer, PS1-style software raster of artist occluders + terrain, tiled 16
triangles/job into per-job z-buffers then merged, used because it removes CPU not just GPU time
and supports destruction. Conclusions: it's all about the data; simple data means simple code;
know your target hardware.

For Anoptic. This is the CPU ancestor of the engine's GPU-driven cull. The compute cull pass
that frustum-tests every entity and writes an indirect draw list is exactly Collin's brute
force, moved to the GPU. The grid-of-SoA-blocks, `AtomicAdd` allocation, and swap-trick removal
are a near-perfect description of the chunked sparse-set stores with swap-and-pop. Two things
to lift: cull on projected screen area, which is the natural threshold for the scoped-resolution
LOD hierarchy; and accept the cone-culling drop in the vertex-shader fallback as exactly the
kind of fidelity-for-simplicity trade Collin makes.

## Segment 4 — Andrew Kelley, Practical Data-Oriented Design (Handmade Seattle 2021)

DOD as a concrete, repeatable procedure rather than a philosophy. Kelley's mental model:
CPU fast, memory slow; every access goes through a 64-byte cache line; the whole game is
avoiding cache misses. The strategy is one sentence: find the struct you have the most of in
memory and make it smaller.

He first teaches struct size and alignment by example (size grows to a multiple of the largest
member's alignment; a trailing `bool` can cost 8 bytes of padding). Then five size-reduction
techniques:

- Indexes instead of pointers. On 64-bit, a pointer is 8 bytes; an index into an array can
  be 4 (or fewer), halving the struct and reducing alignment. Caveat: lose pointer type-safety
  — use distinct integer types if the language has them. ("Handles are the better pointers.")
- Booleans out of band. Don't store an `alive` flag; keep two arrays, alive and dead.
  Information theory says the bit still exists (which array), but the footprint drops and the
  hot loop iterates only the live set — no branch, no flag load, fewer misses.
- Eliminate padding with structure of arrays. Array-of-structs pays per-element padding;
  one array per field pays none. 10,000 monsters: 160 KB → 91 KB from this one change.
- Sparse data in hash maps. If 90% of monsters hold no item, drop the field from the struct
  and store the exceptions in a map keyed by index. 366 KB → 198 KB.
- Encodings instead of polymorphism. Replace a uniform tagged union (every variant pays for
  the largest) with multiple tag encodings that each carry only the fields that variant needs,
  with rare extras in an out-of-band side table. The monster example: 32 B (tagged union) →
  24 B (OOP base+extension) → 17 B (encodings).

The case study is the Zig compiler. Tokens went 64 B → 5 B (compute line/column lazily, cap
source files at 4 GB, don't store end positions or pre-parse literals). AST nodes 120 B → 15.6 B
average via encodings, for 22% faster wall-clock parsing. ZIR instructions 54 B → 20.3 B,
for a further 39% faster. Because the output became four flat arrays (tag, data, extra,
string table), it serializes to the on-disk cache in one `writev`/`readv`, and the
embarrassingly-parallel front end hit 8.9M lines/s on a thread pool.

For Anoptic. The engine's generational `(index, generation)` handles are precisely
indexes-instead-of-pointers with a safety tag — Kelley's technique, already adopted. Three more
to apply directly: the chunked sparse-set already realizes booleans-out-of-band (dead slots are
holes, the iteration skips them); the encoding strategy is the right tool for component variants
and is exactly the spirit of the bridge's field-bit-mask `UPDATE` (one message folds several
discrete changes); and "store the type you have the most of, make it smaller" is the sizing
rule for the per-entity GPU-slot buffers, where every saved byte multiplies by a million
entities.

---

# Part II — Memory: Arenas and Allocators

Two talks, two altitudes, one thesis: per-object `malloc`/`free` is the wrong granularity;
group allocations by lifetime and reclaim them in bulk. Fleury builds the idea from scratch in
C; Lakos quantifies it and fits it to a standard library. This is the academic and practical
backing for the engine's entire region-memory model.

## Segment 5 — Ryan Fleury, Enter The Arena (workshop, ~2023)

A from-first-principles derivation of the **arena**. Starting from Acton's A→B transform and
virtual memory (48-bit address space ≈ 256 TB, mapped lazily to physical pages), Fleury shows
why per-allocation lifetimes are the problem: the "extreme rule" of one `free` per `alloc`
produces a graph of interdependent lifetimes you must maintain forever, with leaks, use-after-
free, double-free, and fragmentation as the failure modes. `malloc` is ~5,600 lines of glibc
trying to serve every possible alloc/free order, and it contends across threads.

The stack already solves a restricted version: a local is an alloc, the closing brace frees
everything, lifetime is a property of scope, and the implementation is a bump pointer set up
once per thread. The arena generalizes the stack to N named stacks. An arena corresponds to one
tree of lifetimes; you push onto it and free it (or clear it) as a whole. The transform picture
gains one parameter: B from A, *here* — pass the arena that says where the result lives, so any
function (simple alloc, list, tree, graph) can allocate into the caller's chosen lifetime.

Mechanics:
- Lifetime flavors fall out naturally: frame arena, per-request arena, permanent arena.
- When an arena fills: panic, chain a new block (linked list, discontiguous), or reserve a
  huge virtual range and commit pages on demand (e.g. reserve 64 GB, commit as you push).
  The reserve-commit variant gives dynamic arrays that never relocate — no invalidated pointers.
- The whole thing is ~200 lines: `arena_alloc`/`arena_release` (reserve/free address space),
  `push`/`pop`/`clear`, with push zeroing by default.
- Thread-local scratch arenas expose the call-stack lifetime through the arena interface,
  so stack-shaped code reuses arena-shaped code. The hazard is arena aliasing: if a callee
  uses the same scratch the caller passed as output, the callee's pop truncates the caller's
  data. Fix: `get_scratch` returns an arena that does not match any arena the caller passed
  (the two conflict arguments), and keep ≥2 scratches per thread.
- A free list layered on an arena gives a pool allocator: check the free list on alloc,
  push onto it on free, grow the arena only when empty. More elaborate allocators (a quadtree
  atlas) build on the same base; he has never needed a full general-purpose allocator, because
  you always know more about your problem than `malloc` does.

Payoffs: allocation is a pointer bump, release is per-lifetime not per-object, the slim layer
makes logging/visualization/leak-detection trivial (e.g. mark popped pages no-access to catch
use-after-pop), and arenas work in any language or even assembly. The cited speedup is Andreas
Fredriksson's JSON parser — replacing a malloc/free forest with a bump allocator was "at least
100%" faster, because the time was in the allocator. Caveats from the Q&A: arenas are an
organizational tool, not an excuse to stop thinking (overlapping lifetimes and return-pointer-
to-popped bugs still happen; ASan still helps); for threading, give each thread its own arena,
or scope an arena to a shared structure and guard it with a lock at that layer.

For Anoptic. This is the engine's memory model, stated as theory. `LOCALHEAPATTR` + `mi_heap_*`
is Fleury's scoped arena with automatic teardown (his "missile null garbage collector" is the
engine's "OS is the outermost GC; `exit()` is a region free"). The architect's note that arenas
are "the FP memory model with the GC removed" is Fleury's A→B+lifetime exactly. Concrete lifts:
the reserve-commit dynamic array would let the geometrically-doubling GPU-side buffers grow
without the reallocate-and-copy they currently pay (commit more pages behind a fixed virtual
reservation instead); the thread-local-scratch discipline and the aliasing rule belong in the
arena API before the renderer rewrite leans on scratch arenas; and the free-list-on-arena is
the exact shape of the planned pool allocators for the dynamic 10% of entity lifetimes.

## Segment 6 — John Lakos, Local ("Arena") Memory Allocators (CppCon 2017, parts 1–2)

The rigorous, measured counterpart. Lakos (Bloomberg) argues allocators are not magic or syntax
but essential, with three independent payoffs — performance, placement (put objects in a
specific kind of memory, e.g. memory-mapped/shared), and testing/debugging/profiling — and the
talk measures only the first.

Foundations. An allocator is a stateful mechanism bound to a region of memory, reached
through a pointer; it is not a value and cannot be copied. Taxonomy: global vs local, general-
vs special-purpose. `malloc`/`new` are global general-purpose specs; tcmalloc/jemalloc are
global general-purpose implementations. The local allocator's superpower is release: hand
the whole region back at once, ignoring the objects — and the more aggressive winking out,
where you simply never destroy the objects and let the region vanish (legal C++). The governing
fact: locality is king — a boxed working set means fewer cache lines and pages in play,
hence more hits, at every level of the hierarchy.

The strategy set: the global allocator; the monotonic allocator (a buffered sequential
allocator, ideally over a stack buffer — bump-allocate, deallocation is a no-op); the
multipool (per-size pools with geometric chunk growth, backed by the global; requests over
the max pool size, e.g. 2048 B, pass straight through); and multipool-over-monotonic. Each can
be wired by type parameter (bakes the allocator into the type, C++98/11 style) or by abstract
base pointer (the C++17 polymorphic-memory-resource style, which keeps the type stable and whose
virtual-dispatch cost is, when measured, negligible to small — and can often be bound at compile
time). Times two for destroy-vs-wink gives 14 strategies.

The analysis frame: five problem dimensions, mnemonic DIV LUC — Density (alloc ops / total
ops), Variation (in allocated sizes), Locality (small region accessed repeatedly), Utilization
(max-in-use / total-allocated), Contention (concurrent allocations) — later joined by
fragmentability (how many movable parts a structure has; a `vector<int>` ≈ 0, a
`vector<string>` or nested map high). Fragmentability was contributed by Graham Blaney and
accepted into C++17 alongside monotonic and multipool.

Headline results across four benchmarks:
- Build-up / use / tear-down (high utilization): the global allocator is always inferior; the
  monotonic allocator is best, especially on a stack buffer (like `alloca`). For a
  `vector<vector<int>>`, ~5× over global.
- Long-running, time-multiplexed subsystems (the locality benchmark, "almost everything we do"):
  the speed of alloc/free becomes irrelevant; what matters is that memory stays partitioned.
  Run a global allocator and memory **diffuses** — not fragments — as nodes from different
  subsystems intermix like salad dressing, and access slows by ~10× after one shuffle, ~15–16×
  at saturation. Per-subsystem local allocators cut that degradation to ~2.5× and the bigger the
  system the bigger the win (16× on a 2^25 problem). Use copy, not splice, to keep data local.
- Utilization (pump): never use a monotonic allocator alone when utilization is low and bytes
  large (it only grows); multipool beats it, but multipool degrades below global once chunks
  exceed the pool threshold and pass through.
- Contention: local allocators give a steady 3–4× over the global allocator's best thread-per-
  subsystem effort, and eliminate the false sharing diffusion causes.

Closing thesis: object-level control over allocation is intrinsic to C++; for any large,
long-running system with disproportionate access, local allocators deliver order-of-magnitude
wins, and the virtual-interface overhead is usually nil.

For Anoptic. The engine's hierarchy is Lakos's taxonomy: frame and scratch arenas are monotonic
allocators; per-level and per-subsystem heaps are local allocators; `mi_heap_destroy` is
release/winking out (his "10 seconds to zero" reclamation anecdote is the engine's bulk-free
claim, validated). The warning that matters for a million-entity simulation running for hours is
diffusion: even with mimalloc as the global allocator, entities that spawn and die across a
long session will diffuse and lose locality unless each subsystem and resolution level owns its
arena or pool. DIV LUC + fragmentability is a ready decision tool — the ECS component stores are
high-utilization, low-fragmentability single arrays (arena is ideal), while nested/dynamic
structures and the glTF parser are where pooling and scratch arenas earn their keep. The
scoped-resolution levels are Lakos's per-subsystem allocators: promote = allocate a richer
arena, demote = release it.

---

# Part III — Lock-Free Concurrency

Scott's two-part lecture is the spine; the five papers are the formal backbone (the M&S queue,
the dual structures, and the two modern Rochester papers on reclamation and composition). This
is the theory under the engine's no-mutex-outside-Vulkan rule, the SPSC bridge, the logger, and
the planned event bus and striped structures.

## Segment 7 — Michael L. Scott, Nonblocking Data Structures (lecture, parts 1–2)

Why not just lock? Locks fail on thread death, stall everyone on preemption of the holder, and
cause priority inversion when a signal/interrupt handler needs the held lock. Nonblocking
structures define away these failures: every realizable concrete state maps to a valid abstract
state in which any thread can make progress. The typical operation has three phases — private
preparation, one hardware-atomic **linearizing instruction**, and cleanup that any thread can
perform.

Hardware and memory model:
- CAS vs LL/SC. CAS succeeds if the bits match — even if they match "for the wrong
  reason" (the ABA hazard). LL/SC tags a cache line and fails if anyone touched it, so it has no
  ABA but can fail spuriously. C/C++ expose both: `compare_exchange_strong` (CAS semantics) and
  `_weak` (LL/SC semantics; use it inside a retry loop).
- `swap` and `fetch_add` always succeed, so N threads finish in O(n); emulating them with a CAS
  loop is O(n²) under contention. This is why a `fetch_add` counter is wait-free and a CAS
  counter only lock-free — and it foreshadows the fastest queues.
- Relaxed memory models are real. The sufficient discipline: label every variable that drives a
  concurrent decision `atomic` and leave the default `seq_cst`. Relax individual accesses only
  after extensive profiling and proof — it is "very, very hard to get right."

Core structures and the recurring techniques:
- Treiber stack: push/pop with a single CAS on the top pointer; lock-free; the linearization
  point is the successful CAS (or, for pop on empty, the read that returns null).
- The **ABA problem**, shown on the stack: pop reads top→A and A→C, sleeps; another thread pops
  A, frees it, pushes B, and a recycled node reuses A's address; the sleeper's CAS wrongly
  succeeds and loses B. Fixes: LL/SC (no ABA), or **counted pointers** — pair every pointer with
  a serial number incremented on each update, needing a double-wide CAS.
- **Linearizability** (the safety criterion): every operation appears to take effect
  instantaneously at one point between call and return; an implementation is linearizable if
  every history is. Its decisive property is that proofs compose — two independently correct
  structures stay correct when used together. Liveness has three strengths: **wait-free**
  (bounded steps regardless of others), **lock-free** (someone always progresses), and
  **obstruction-free** (progress if run in isolation; needs an out-of-band back-off to avoid
  livelock). Most shipped algorithms are lock-free.
- The **Michael & Scott queue**: linked list with a dummy node, separate head/tail. Enqueue is
  two CASes — link the node onto the last node's `next` (the linearization point), then swing
  tail (cleanup). Any thread that sees a lagging tail performs the swing: this is helping,
  ubiquitous in nonblocking code. Dequeue reads head, the dummy's `next`, and the value, then
  CASes head forward — an atomic snapshot that works because the value cannot change without
  head changing. Read the value before the CAS, since the CAS lets another thread free the node.
- Memory reclamation, the problem locks don't have: you may free a node another thread is about
  to dereference. Solutions in the talk: counted pointers plus a type-preserving allocator
  (a freed queue node always stays a queue node, so a stale `next` is still a valid pointer to a
  queue node — a Treiber stack of free nodes serves and preserves locality); **hazard pointers**
  (publish the pointers you hold; a deleter frees only nodes no hazard pointer references, parking
  the rest on a limbo list — general and portable, but each update needs a release fence, ~30
  cycles); and **epoch-based reclamation** (a slow global clock; free a node only once every
  active thread has advanced past the epoch of its deletion — one fence per operation instead of
  per dereference, at the cost of unbounded limbo under indefinite preemption).
- Harris/Michael sorted list: insert and delete must contend on the *same* word, so delete first
  tags (marks a bit in) the victim's `next` pointer — the tag is the linearization point and
  logical deletion — then unlinks as cleanup. Pointer tagging in the low bits is a recurring
  trick (C/C++ only; needs indirection in Java).

The survey half (part 2):
- Hash tables: chained Harris/Michael lists per bucket give a fixed-size table; the elegant
  Shalev-Shavit split-ordered table is resizable in O(1) expected time for all operations —
  one sorted list keyed by the bit-reversed hash, buckets are lazily-initialized hints, doubling
  the table just adds one hash bit and interleaves new hints.
- The Herlihy-Luchango-Moyer obstruction-free deque — the motivating example for obstruction
  freedom — uses counted array elements and a two-CAS operation, fixing livelock with scheduler
  back-off rather than algorithm changes.
- Nonblocking trees (Ellen et al. external BST; Natarajan-Mittal's edge-flagging for more
  concurrency) and skip lists (Fraser/Lea/Herlihy; bottom list authoritative, upper lists hints,
  wait-free lookup).
- LCRQ (Morrison-Afek, 2013): ring buffers in an M&S queue, slots claimed by `fetch_add`
  rather than CAS, ~10× faster than the M&S queue — the fastest known concurrent queue, at the
  cost of "tantrum" semantics (abandon a ring when consumers run ahead) and heavy engineering.
- Wait-free from lock-free via fast-path/slow-path (Petrank et al.): run the lock-free algorithm;
  if you suspect starvation, publish your operation so others help you. Universal constructions
  and software transactional memory exist but are slower than hand-tuned structures; STM's value
  is atomic composability of multiple structures.
- Combining and flat combining: a pusher and popper can satisfy each other in a side
  arena without touching the main structure; flat combining lets one thread own the structure
  under high contention to exploit cache locality, trading concurrency for locality at a tuned
  sweet spot.
- Cost note: read-modify-write instructions are no longer slow — they run in cache at near load/
  store cost. What is expensive is cross-chip cache misses (~200 cycles) and the fences inside
  atomics (~20–30 cycles). For verification, Scott's honest advice: assertions plus many threads
  piling on finds most bugs; model checkers find them fast; TLA+ for the highest-confidence library
  code.

For Anoptic. This segment is the engine's lock-free curriculum. The shipped SPSC bridge ring is
the simplest correct instance: acquire/release on head/tail, no CAS, and because the indices are
monotonic it is ABA-free by construction — the right baseline. The single-producer design
(logic master is the sole command producer, emitting after the parallel update settles) is a
deliberate trick to make the linearization order total for free. The logger's intended MPSC
(`fetch_add` to reserve a slot, write payload, set a per-slot commit marker with release, consumer
walks to the first uncommitted slot) is precisely the prep/linearize/cleanup rhythm with a commit
header solving the out-of-order-publication gap — Quill/NanoLog do the same. The render-slot
quarantine (a `DESTROY` holds the slot until all frames in flight retire, then `REVENT_SLOT_RETIRED`
recycles the id) is epoch-based reclamation with the frame counter as the epoch clock — the
correct answer to the safe-reclamation problem, and it avoids hazard pointers' per-dereference
fence. The planned cache-line-striped structures should be read against LCRQ (which already
exploits `fetch_add` to beat the M&S queue ~10×) and flat combining (which already trades
concurrency for locality): the stripe idea is in good company, but Scott's cautions apply — pin
the linearization point, handle the gap exactly as the logger does, and model-check or prove
before benchmarking against LCRQ and M&S.

## The papers — formal backbone

The talk is grounded in five papers, summarized here for the algorithms the engine will copy or
cite.

Michael & Scott, PODC 1996 / JPDC 1998 — the M&S queue. The two-CAS lagging-tail enqueue with
helping, the atomic-snapshot dequeue, counted pointers for ABA, and a companion two-lock queue
(separate head/tail locks for one concurrent enqueuer and dequeuer). The 1998 journal version
adds the empirical case: across queues, stacks, and counters on a multiprogrammed multiprocessor,
data-structure-specific lock-free algorithms beat both locks and general constructions (the M&S
queue >40% over a preemption-safe single lock at 11 processors; Treiber's stack wins everywhere),
but a *general* method can lose for complex structures (the nonblocking heap loses to a lock). Two
portability rules fall out: on LL/SC targets (ARM, Power) the counted-pointer machinery is
unnecessary; on x86 CAS it is required (CMPXCHG16B or a packed index+counter). This is the
canonical MPMC FIFO behind `ConcurrentLinkedQueue` and is the right Step-5A baseline.

Scherer & Scott, DISC 2004 — **dual data structures**. The fix for consumers that must wait on an
empty structure without busy-retrying (which wastes bandwidth and breaks FIFO fairness among
waiters). A partial operation splits into a request and a follow-up, each with its own
linearization point; when the structure can't satisfy you, you enqueue a reservation and spin
only on memory you own, so blocked threads don't interfere. The dualqueue (M&S queue + reservations)
gives FIFO fairness among waiters, terminates each waiter with one remote CAS, and measured ~2×
the bare M&S queue under heavy contention; specializations yield queue locks with no release-path
spin, semaphores, and tunable limited-contention locks. This is the algorithm for an engine job
queue or event bus where workers sleep until work arrives.

Cai et al., DISC 2021 — nbMontage (persistence). Mostly about non-volatile memory and so largely
out of scope, but two transferable mechanics: a single-producer/multi-consumer ring whose concurrent
consumers may overlap ranges safely because the consumed operation (a cache-line write-back) is
idempotent — a clean pattern for any background flush/drain queue; and an epoch + `<thread-id,
serial-number>` operation-ID reclamation scheme that mirrors the engine's frame-gated slot reuse.

Cai, Wen & Scott, 2023 — NBTC / Medley / txMontage (composition). The key design rule, not the
durability layer: a structure is composable into multi-structure transactions if every operation
has an **immediately-identifiable linearization point** (statically one load or CAS, dynamically
recognizable as the linearizing step with no extra shared reads). Keeping each Anoptic lock-free
structure to this rule preserves the option to later compose atomic cross-structure operations
(e.g. moving an entity between two systems) with ~2.2× overhead and no global lock — Medley's
invisible-reader, per-thread-descriptor design also reinforces the cache-frugal patterns above.

---

# Cross-cutting synthesis — the best techniques

The non-redundant set worth lifting, in rough order of payoff for a million-entity engine.

1. Layout for the cache line, not the object. Default to structure of arrays; split hot from
   cold fields; never put a hot value behind a `bool` in a fat struct (Acton, Meyers, Kelley,
   Collin all converge here). The cache line is the unit of cost and of correctness (false
   sharing).
2. Linear arrays over pointer structures wherever the access is a scan. The prefetcher rewards
   forward/backward strides; an array can beat a tree or a hash table at the sizes a hot loop
   sees (Meyers, Collin).
3. Brute force on linear data often beats a clever hierarchy — fewer branches, predictable
   prefetch, trivial parallelism, less code (Collin's 3×-at-1/5-the-code).
4. Indexes/handles instead of pointers; swap-and-pop removal; encodings instead of polymorphism;
   sparse data out of band (Kelley, Collin).
5. Group allocations by lifetime: the arena. Allocation is a bump, release is per-lifetime,
   and reserve-then-commit gives non-relocating dynamic arrays (Fleury).
6. Prefer local allocators to the global one for anything long-running; the enemy is diffusion,
   not fragmentation, and per-subsystem partitioning is the cure (Lakos). Monotonic for build-up/
   tear-down, pool/multipool for the dynamic minority.
7. Free in bulk: release / winking out / `mi_heap_destroy` turn O(objects) teardown into
   O(regions) (Lakos, Fleury).
8. For concurrency, design the three-phase operation: private prep, one linearizing instruction,
   helpable cleanup. Keep that point immediately identifiable so structures stay composable
   (Scott, NBTC).
9. Pick the primitive to the hardware: `fetch_add`/`swap` are O(n) and wait-free where a CAS loop
   is O(n²) and only lock-free; this is the lever behind LCRQ's ~10× (Scott).
10. Decide reclamation deliberately: monotonic-index structures are ABA-free for free; use
    epoch-based reclamation (a frame clock) for recycled slots/nodes; reach for hazard
    pointers only when portability forces it; counted pointers on x86 when a structure recycles
    pointers (Scott, papers).
11. Avoid false sharing by construction: put each producer/consumer index, each per-thread counter,
    each stripe on its own cache line (Meyers, Scott).
12. Memory-order discipline: label atomic, leave `seq_cst`, relax only with profiling and proof
    (Scott).
13. Measure the right thing: cache-line utilization and miss rate per hot loop, wall-clock A/B,
    and the long-running diffusion ratio — not micro alloc speed (Acton, Kelley, Lakos).

---

# Comparative analysis with Anoptic

Where the engine already aligns with the corpus, and where the corpus would refine it. The
verdict column is the actionable part.

| Anoptic mechanism (`docs/notes.md`) | Corpus source | Verdict |
|---|---|---|
| Scoped arenas: `LOCALHEAPATTR` + `mi_heap_destroy` | Fleury arenas; Lakos release/wink | Canonical. Teardown is O(regions); this is the engine's correct core. |
| Hierarchy process>level>frame>scratch>pool | Fleury frame/permanent/scratch; Lakos per-subsystem | Aligned. Add explicit per-system and per-resolution-level arenas to fight diffusion over long sessions. |
| mimalloc as global override | Lakos: global always inferior on long-running locality | Keep as fallback. The 3–16× lives in the local heaps; ensure hot subsystems own theirs. |
| 1 GiB hugepages (PDPE1GB) | Meyers TLB; Fleury reserve/commit | Aligned. Pair with reserve-commit growth to keep the TLB win and avoid realloc-copy. |
| ECS `(index, generation)` handles | Kelley handles-are-better-pointers | Aligned. Halves handle size with a safety tag; exactly the recommended pattern. |
| Chunked sparse-set, swap-and-pop | Collin swap trick; Kelley booleans-out-of-band | Aligned. Dead slots as holes already realize the alive/dead split. |
| Components as contiguous arrays | Acton/Meyers/Collin/Kelley SoA | Aligned. Refine with hot/cold field split and Kelley encodings for variant components. |
| Deferred structural mutation at tick boundary | Acton future-command-buffer; Scott prep/linearize/cleanup | Aligned. Single-producer flush gives a total order for free. |
| SPSC bridge ring, head/tail on separate lines | Meyers false sharing; Scott | Correct and ABA-free by construction (monotonic indices, no CAS). The right baseline. |
| Bridge `UPDATE` field-bit mask, ≤1 msg/entity/tick | Kelley encodings; Acton combine transforms | Aligned. One message folds several discrete changes — the encoding strategy applied to traffic. |
| Sparse/continuous split (only discrete crosses bridge) | Acton where-there-is-one-there-are-many | Aligned. Continuous motion as GPU params = zero per-frame bridge traffic. |
| Render-slot frame-gated reuse + `REVENT_SLOT_RETIRED` | Scott/Fraser epoch reclamation; nbMontage epoch+op-ID | This *is* epoch-based reclamation with the frame counter as clock. Correct; avoids hazard-pointer fences. |
| `render_id` → GPU slot indirection | Kelley handles; Scott indirection-for-reclamation | Aligned. Stable logical id over a privately-remapped physical slot. |
| Dynamic chunked GPU buffers (geometric doubling, bump arena) | Fleury reserve/commit; Lakos multipool doubling | Refine: reserve a large virtual range and commit pages to grow in place, dropping reallocate-and-copy. |
| Logger MPSC: `fetch_add` + commit header; the gap problem | Scott helping/3-phase; Vyukov; NBTC linearization point | The commit-header per slot is the right fix; consumer halts at first uncommitted slot. Keep it immediately identifiable. |
| Cache-line-striped lock-free (Step 5B, planned) | Meyers/Lakos locality; Scott cross-chip cost; LCRQ; flat combining | Promising and well-precedented. Benchmark against LCRQ (the ~10× bar) and M&S; model-check/TLA+ the linearizability and gap handling first. |
| Two-bus event design (monotonic + striped), Step 8 | Scott M&S queue, dual structures | Sound. Use M&S/Vyukov for ordered input; consider the dualqueue where workers must block for work. |
| Scoped resolution: arenas per LOD level | Lakos per-subsystem allocators + DIV LUC; Acton different-data-different-problem | Aligned. Promote = allocate richer arena; demote = release/wink. |
| No mutexes outside Vulkan | Scott (preemption, priority inversion, thread death) | Aligned with the formal motivation. |
| GPU-driven cull, no PBR | Collin culling philosophy | Aligned. Add screen-area (not distance) culling for the LOD threshold. |
| Timing `_Atomic` lazy init | Scott "label atomic, seq_cst default" | Aligned. |

The reclamation decision, made concrete for the Ryzen/x86-64 target. CAS carries the ABA
problem, so any future structure that recycles pointers (a Step-5A M&S queue baseline) needs
counted pointers via CMPXCHG16B or a packed index+counter — *unless* it is built on monotonic
indices (the SPSC ring, the logger, a stripe claimed by `fetch_add`), in which case it is ABA-free
for free. For slot/node recycling, the engine already has the better answer: frame-gated quarantine
is epoch reclamation, and it should be the default over hazard pointers (whose per-dereference fence
costs ~30 cycles) everywhere the frame clock is available.

The single most important long-horizon warning the corpus adds: diffusion. A million entities
spawning and dying across a multi-hour session will, under a global allocator, intermix until
locality collapses and access slows by an order of magnitude (Lakos measured 10–16×). The engine's
arena hierarchy is the cure, but only if the steady-state simulation actually allocates per-subsystem
and per-resolution-level rather than falling back to the global mimalloc heap. This is the
performance reason the arena work is foundational, not cosmetic.

---

# Guidance

A decision guide for the engine, distilled to the choices that recur.

Layout. Default every component and every bulk buffer to structure of arrays. Use array-of-structs
only when fields are always accessed together. Split hot fields (transform, position) from cold
metadata so the cull and animation passes touch only what they consume. Never gate a hot loop on a
`bool` inside a fat struct; use a separate live set or an out-of-band table. For component variants,
prefer encodings over a uniform tagged union. Size the type you have the most of first — at a
million entities every byte is a megabyte.

Allocation. Match the allocator to DIV LUC and fragmentability. Per-frame and per-task scratch:
monotonic arena, reset or winked each frame. Per-subsystem and per-resolution-level steady state:
a dedicated arena or pool, never the global heap — this is the diffusion defense. The dynamic
minority of entity lifetimes (unpredictable spawn/despawn): free-list-on-arena or multipool. Reserve
large virtual ranges and commit on demand for buffers that grow (GPU per-slot arrays). Reserve the
global mimalloc heap for the non-performance path. Free in bulk; never iterate to destroy what a
region drop can reclaim.

Concurrency. Choose the primitive to the shape: SPSC ring for one-to-one hand-offs (already shipped,
keep head/tail on separate lines); bounded MPMC (Vyukov) for the job queue; M&S queue as the
unbounded MPMC baseline; the dualqueue when consumers must block for work without busy-retrying;
`fetch_add`-based designs (LCRQ-style) when raw throughput dominates. Treat the cache-line-striped
structure as an experimental bet measured against LCRQ and M&S, not a foregone conclusion — its
nearest relatives (LCRQ's `fetch_add` slots, flat combining's locality trade) already exist and set
the bar. Design every operation as prep / one linearizing instruction / helpable cleanup, and keep
that point immediately identifiable so structures compose later.

Reclamation. Prefer monotonic-index structures (ABA-free, no counted pointers). Use the frame clock
as an epoch for slot and node reuse — the render bridge already does this; generalize it. Reach for
hazard pointers only when portability across non-frame-clocked contexts demands it. On x86, any
pointer-recycling structure needs counted pointers (double-wide CAS); on a future ARM target, LL/SC
removes that need.

Memory order. Label atomic, leave `seq_cst`. Relax a specific access only after profiling proves it
is the bottleneck and a proof or model-check shows the relaxation is safe. False sharing is a layout
bug: give each index, counter, and stripe its own cache line.

Verification and measurement. Assertions plus many threads piling on finds most lock-free bugs fast;
model-check Step 5B and TLA+ the stripe's linearizability before treating it as a result. Measure
cache-line utilization and miss rate per hot loop (Acton/Xbox style), wall-clock A/B on real
workloads (Kelley), and the long-running diffusion ratio for the steady-state simulation (Lakos) —
not micro allocation speed, which the corpus repeatedly shows is the wrong metric.

The non-negotiables, compressed. Hardware is the platform. Understand the data and you understand the
problem. Where there is one, there are many. Small is fast; locality is king; the cache line is the
unit of cost. Bundle lifetimes; reclaim in bulk. One linearizing instruction; label atomic. Measure
the line, not the call.
