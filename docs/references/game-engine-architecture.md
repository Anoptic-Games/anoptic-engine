# *Game Engine Architecture* (3rd ed.) 〜 distilled for Anoptic

A cross-reference of Jason Gregory, *Game Engine Architecture*, 3rd edition (CRC Press,
2018) against the Anoptic Engine. It pulls out what matters to *this* engine at its current early stage and, for each topic, says where the book **validates**, **challenges**, or **adds nuance** to our existing decisions (see [docs/notes.md](../notes.md)).

**How to read this.** Organized by book chapter, in book order. Page citations like `(p. 289)` are the *book's* printed page numbers. Each section has three parts:

- **Book** 〜 the distilled, page-cited content.
- **Anoptic** 〜 how it maps to our principles / build steps / files.
- **Verdict** 〜 ✅ validates a decision · ⚠️ challenges or warns · ➕ adds nuance or a technique we don't have yet. C++→C23 translations are called out where they matter.

**The book's worldview vs ours.** Gregory writes from the AAA-console world circa 2018: C++/OOP, third-party middleware (Havok, PhysX, Granny), optical-disc and limited-RAM consoles, photoreal rendering, and *modest* entity counts (a few hundred dynamic actors). Anoptic inverts almost all of these: **C23/Clang-only**, no frameworks, **data-oriented**, **lock-free**, desktop SSD + huge RAM, deliberately **non-photoreal**, and built for **millions** of entities. So the book is most valuable as (a) a catalogue of battle-tested low-level techniques (allocators, handles, event objects, job systems) that are language-agnostic, and (b) a foil 〜 the places where its OOP/console assumptions *don't* hold are exactly where Anoptic's design earns its keep. Translate accordingly throughout: where Gregory says `std::atomic`, read C11 `_Atomic`; where he says RAII destructor, read `__attribute__((cleanup))`; where he says "avoid because C++ is awkward here," note that C often makes it free.

**Edition note 〜 the 4th edition (CRC Press, 2026).** A fourth edition now exists, split into two physical volumes because the book outgrew one binding. **Volume I 〜 Foundations and Core Engine Systems** is Ch 1–10 and covers *everything here except* rendering and the gameplay/event material. **Volume II 〜 Graphics, Motion, and Sound** holds the rendering engine, **two brand-new chapters on real-time photoreal lighting**, animation (now with **motion matching**), physics, audio, **and the game object model + event system + scripting**. So **Ch 11 (Rendering)** and **Ch 15–16 (Gameplay Foundation & Event Pipelining 〜 the event bus)** here map to Volume II, which is a separate book; their section numbers will differ there. What this means for us:

- *Numbering is stable in Vol I* for every system we care about 〜 Ch 3 (fundamentals / memory layout), Ch 4 (concurrency), Ch 5 (math), Ch 6 (engine support), Ch 7 (resources / filesystem), Ch 8 (game loop), Ch 10 (logging) keep their numbers. Only §1.6 *Runtime Engine Architecture* moved to **§1.5** (the new §1.6 is *Tools and the Asset Pipeline*). Page numbers shifted; the `(p. …)` citations below remain **3rd-ed** pages.
- *The concurrency chapter 〜 our centerpiece 〜 was reorganized.* §4.9 is now carved into finer subsections (4.9.2.1–.12 atomics, 4.9.3 barriers, 4.9.4 memory ordering, 4.9.5 atomic variables + C++ memory_order, 4.9.7 spin locks, 4.9.8 transactions, 4.9.9 lock-free linked list) and got a fresh review pass, but the substance is unchanged. Verified directly: **4.9.9 still shows only `push_front()`** (the Herb Sutter CppCon 2014 example) 〜 no lock-free pop/dequeue 〜 so our "book shows push, not pop" note still holds.
- *One genuinely new, relevant addition is folded in below:* the **undefined-behavior / strict-aliasing / type-punning** discussion (§3.3) 〜 see the new block in Ch 3. The 4th ed also expands the C++-standards survey through **C++23** (§3.1.2), but that is C++-specific and orthogonal to our C23 codebase.

---

## Contents

1. [Ch 1.6 〜 Runtime Engine Architecture](#ch-16)
2. [Ch 3.3–3.5 〜 Hardware, Memory Layout & Caches](#ch-3)
3. [Ch 4 〜 Parallelism & Concurrent Programming](#ch-4)  ← core
4. [Ch 5 〜 3D Math for Games](#ch-5)
5. [Ch 6 〜 Engine Support Systems](#ch-6)  ← core
6. [Ch 7 〜 Resources & the File System](#ch-7)  ← core (asset/filesystem)
7. [Ch 8 〜 The Game Loop & Real-Time Simulation](#ch-8)  ← core
8. [Ch 10 〜 Logging & Memory Tooling](#ch-10)
9. [Ch 11.1–11.2 〜 The Rendering Engine](#ch-11)
10. [Ch 15–16 〜 Gameplay Foundation & Event Pipelining](#ch-16-foundation)  ← core (events)
11. [Deferred topics](#deferred)
12. [Action items](#actions)

---

<a name="ch-16"></a>
## Ch 1.6 〜 Runtime Engine Architecture (pp. 38–58)

**Book.** A game engine is built in **layers**; upper layers depend on lower, never the reverse, and **circular dependencies are to be avoided** because they kill testability and reuse (p. 38). The canonical layer stack (Fig 1.16): target hardware → drivers → OS → 3rd-party SDKs → **platform independence layer** → **core systems** (assertions, custom memory allocators, math lib, hand-coded containers "to minimize or eliminate dynamic memory allocation," pp. 43–45) → **resource manager** → **rendering engine** → collision/physics → animation → HID → audio → **gameplay foundation layer** (game object model, event/messaging system, scripting, world streaming) → game-specific subsystems. Two reasons to wrap platform APIs (p. 43): consistent behaviour across platforms, and insulation from future dependency swaps. Vulkan is described (p. 42) as a low-level library giving "fine-grained control over memory and other resources shared between the CPU and GPU."

**Anoptic.** This is the map our roadmap already follows: the `ano_*()` platform abstraction layer ([include/anoptic_memory.h](../../include/anoptic_memory.h), `anoptic_time.h`, `anoptic_threads.h`, `anoptic_filesystem.h`) *is* Gregory's platform independence layer; the core-systems layer is the logger, strings, lock-free collections, and data structures; the gameplay foundation layer is the event bus and game loop, plus the future ECS/scoped-resolution work.

**Verdict.**
- ✅ The strict no-cycles layering and "wrap the platform" doctrine match our design exactly.
- ✅ "Hand-code containers to minimize dynamic allocation" (p. 45) is the arena thesis stated in the book's own voice.
- ➕ Gregory lists every subsystem an engine *eventually* grows; useful as a completeness checklist, but treat the OOP "game object model pervading the entire engine" (p. 56) as the thing we are deliberately replacing with ECS 〜 see [Ch 16](#ch-16-foundation).

---

<a name="ch-3"></a>
## Ch 3.3–3.5 〜 Hardware, Memory Layout & Caches (pp. 131–201)

This chapter is the hardware rationale beneath Anoptic's two most distinctive bets: DOD and the cache-line-striped lock-free design. Worth internalizing in full.

**Book 〜 data layout (§3.3, pp. 159–164).**
- Every type has a **natural alignment** = its width; misalignment forces the memory controller to do **two** reads plus mask/shift/OR, and some CPUs fault outright (Fig 3.14, pp. 159–160). SIMD 4-float vectors are 128-bit → **16-byte aligned**.
- Compilers insert **padding** between mixed-width members; reorder members large→small to reclaim it, and a struct is padded up to a multiple of its largest member's alignment so that *arrays* stay aligned (pp. 160–161). Gregory advises explicit `_pad[]` members to make waste visible.
- A C++ class with virtuals carries a **vtable pointer** (4 B / 8 B) per instance (pp. 161– 164). In an array of objects this bloats the stride and every virtual call is a pointer chase.

**Book 〜 the memory gap & caches (§3.4–3.5, pp. 188–198).**
- A register op is **1–10 cycles**; a **main-RAM access is ~500 cycles** (pp. 189–190). Three coping strategies: faster memory nearer the core; *hide* latency with other work; **lay data out to minimize main-memory accesses** (strategy 3 is the DOD rationale verbatim).
- Caches move data in **cache lines** (64–128 B) to exploit **spatial** and **temporal locality** (pp. 191–192). Cache hit ≈ tens of cycles, miss ≈ hundreds.
- **Write-back** caches mark a line **dirty** and flush to RAM only on eviction/flush (p. 196). Multi-core coherency is maintained by **MESI** (Modified/Exclusive/Shared/ Invalid), **MOESI** (+Owned), or **MESIF** (+Forward) (pp. 196–197). Deep MESI walk-through is deferred to §4.9.
- **Best way to avoid D-cache misses (p. 197):** organize data in **contiguous blocks, as small as possible, accessed sequentially.** Keep hot loops small for the I-cache; don't call (non-inlined) functions inside innermost loops.
- **TLB** (pp. 187–188): a small on-die cache of virtual→physical page mappings; page tables are large, so a TLB miss is expensive. Typical page = 4–8 KiB.

**Anoptic.**
- DOD core principle: components are contiguous POD arrays scanned sequentially. §3.5.4.9 is the textbook justification, almost word for word.
- **1 GiB hugepages** (`mi_reserve_huge_os_pages_at`): §3.5.2 explains *why* 〜 fewer, larger pages → fewer TLB entries → no TLB misses on large working sets. The notes.md claim ("a gigabyte at 4 KiB = 262,144 TLB entries → one with 1 GiB pages") is exactly the mechanism Gregory describes.
- **Cache-line striping** (the novel lock-free idea): §3.5.4 + §3.5.4.8 confirm the 64-byte line is the unit of coherency traffic and that write-back/dirty/exclusive-ownership is the substrate. The book frames coherency strictly as a **cost to minimize**; Anoptic inverts it into a **mechanism** 〜 transfer ownership at line granularity *because* MESI already enforces exclusivity per line.

**Book 〜 undefined behavior & type punning (§3.3, *new in 4th ed.*).** Reinterpreting a bit pattern by casting a pointer to a different type and dereferencing it (**type punning**) breaks under **strict aliasing** 〜 on by default at `-O2` 〜 and is **undefined behavior (UB)**: the optimizer is then free to reorder, fold, or delete the access. Gregory's worked example is endian-swapping a `float` as a `u32`. His key warning: **even a `union` is UB for punning *in C++***; the standard-safe path is **`memcpy` into a fresh object of the other type** (compilers elide the copy), or **C++20 `std::bit_cast`**.

**Anoptic (C23 nuance 〜 one of the sharpest C++→C divergences).** Gregory's "even unions aren't safe" is a **C++-only rule**. In C, **union type-punning has been well-defined since C99** (§6.5.2.3). So C23 gives us *three* legal puns where C++ has one: (1) `memcpy` (portable, optimizer-elided 〜 the universal idiom), (2) a **`union`** (legal in C, illegal in C++), and (3) access through **`char*` / `unsigned char*`**, which is *always* allowed to alias anything. Clang also offers `__builtin_bit_cast`. Where this bites us: the **glTF byte-buffer reads** in [ano_GltfParser.c](../../src/render/gltf/ano_GltfParser.c), **any arena code that reinterprets raw bytes as typed objects** 〜 which is *exactly* what the planned **load-in-place** resource format does (action item 7) 〜 and endian/format conversion. A bare pointer-cast pun is a latent miscompile that only surfaces at high `-O`.

**Verdict.**
- ✅ DOD, contiguous SoA component arrays, and hugepages are all directly grounded here.
- ➕ **New and directly relevant:** decide the aliasing policy *before* load-in-place ships 〜 prefer `memcpy`/`union` punning (free in C), or compile the asset/serialization TUs with `-fno-strict-aliasing` (the Linux-kernel choice). Never pun by bare pointer cast.
- ✅ Avoiding virtual dispatch in hot data (no vtable in POD components) is supported by the vtable-pointer/pointer-chase discussion.
- ➕ The cache-line-stripe idea is *consistent* with the book's hardware model 〜 Gregory never proposes aligning an algorithm to the coherency unit. This is genuinely our contribution; §3.5.4.8 + §4.9.4.2 are the references to cite when we write it up. Watch **false sharing** (two stripes' hot fields sharing a line): the direct-mapped "ping-pong eviction" pathology (p. 194) is the single-core cousin of the multi-core line-bouncing we must avoid by 64-byte-aligning each stripe.

---

<a name="ch-4"></a>
## Ch 4 〜 Parallelism & Concurrent Programming (pp. 203–358) 〜 CORE

The new-in-3rd-edition chapter and the most important one for us. The logger, lock-free collections, job system, and main loop all live here.

**Book 〜 definitions (§4.1–4.5).** Concurrency = "composition of independently executing computations" (Rob Pike, p. 257); a system is concurrent only if it has **multiple readers/writers of shared data** (p. 204) 〜 the central problem is eliminating **data races**. **Task** parallelism = different ops in parallel; **data** parallelism = one op over many elements (p. 207). A mutex lock/unlock is a kernel call ≈ **>1000 clock cycles** (p. 267) 〜 the standing motivation for going lock-free. **Atomic** = a critical op whose invocation/response can't be interleaved by another critical op on the *same* object (pp. 262–265).

**Book 〜 sync primitives (§4.6, pp. 267–281).** Mutex (only the locker may unlock); **Windows critical section / Linux futex** = cheap mutex that spins briefly before sleeping; **condition variable** = a *queue of sleeping threads* (must re-check the predicate in a `while` loop because of **spurious wakeups**); **semaphore** = atomic counter, and a *binary semaphore ≠ mutex* because it can be signalled by a different thread than took it. All are kernel-backed and **expensive**.

**Book 〜 why locks hurt (§4.7–4.8, pp. 281–289).** Deadlock needs the four **Coffman conditions**; the practical defenses are **global lock-ordering** (always take A before B) and reducing the number of locks. Also livelock, starvation, and **priority inversion**. Two rules of thumb that matter to us: (1) a **doubly-linked list is not a concurrent data structure** 〜 prefer a **singly-linked append-only** list when order doesn't matter, or a **stable deterministic ordering** when it does (pp. 286–287); (2) **don't try to make everything thread-safe** 〜 "striving in the direction of lock-freedom is a far better strategy than over-using locks," and minimize contention by giving each thread a **private repository** that's collated later (pp. 287–289).

**Book 〜 lock-free (§4.9, pp. 289–330) 〜 the centerpiece.**
- "Lock-free" ≠ "no mutexes"; it means **never blocking/sleeping on a resource** (p. 289). Progress hierarchy, weakest→strongest: **blocking → obstruction-free → lock-free** (some thread always progresses; individual threads *can* still starve) **→ wait-free** (lock-free
  + starvation-free). Umbrella term: **non-blocking** (pp. 289–290).
- Three causes of race bugs: op interleaving, **compiler/CPU instruction reordering**, and **hardware memory ordering** (pp. 291–292). The "preserves single-threaded behaviour" rule is insufficient for concurrency.
- **`volatile` does NOT help in C/C++** (pp. 302–303): it only forces re-reads from memory; it does *not* stop CPU reordering or fix cache coherency. (Java/C# `volatile` is different.) A **compiler barrier** (`asm volatile("" ::: "memory")`) stops compiler reordering but not CPU reordering 〜 and **LTO can defeat the function-call-as-barrier assumption** (p. 304).
- **Atomic RMW**: TAS, exchange, and **CAS** (compare-and-swap: write iff `*p == expected`, loop until success). CAS suffers the **ABA problem** (p. 297): value goes A→B→A and CAS can't tell 〜 defeated by sequence/generation counters or **LL/SC** (load-linked/ store-conditional, immune to ABA and more pipeline-friendly, pp. 297–299).
- **Memory ordering (pp. 304–314):** the deep **MESI** walk-through; coherency-protocol *optimizations* can make two writes appear to other cores in the **opposite** order. Fixed by **fences** and by **acquire/release semantics**: a **write-release** (used by **producers**) lets no prior read/write move after it; a **read-acquire** (used by **consumers**) lets no later read/write move before it. x86 is **strongly ordered** (fences rarely needed); DEC Alpha / PowerPC / ARM are weak. ARM rolls acquire/release into `ldar`/`stlr`.
- **C++11 atomics → C11/C23 mapping (pp. 314–317):** `std::atomic<T>` → `_Atomic T`; default ordering is **seq_cst (full fence)**; `std::memory_order_*` → C11 `memory_order_relaxed / acquire / release / acq_rel / seq_cst`. 32/64-bit types are lock-free; larger types fall back to a mutex (check `atomic_is_lock_free`). **80/20 rule:** only drop below seq_cst when profiling proves the win, and **always check the disassembly**.
- **The lock-free transaction pattern (pp. 327–330):** do the work in **thread-private** memory, then a **single atomic CAS/LL-SC to publish**; on failure, retry 〜 and every failure means *another* thread succeeded, which is exactly why the system always makes progress. Worked example: lock-free singly-linked `push_front` via `head.compare_exchange_weak(node->next, node)` in a loop. **The book shows only push** 〜 and pop is where ABA bites.
- **Lock-not-needed assertions (pp. 325–327):** in a frame loop, data touched single-threaded early-frame and again single-threaded late-frame needs **no lock** 〜 assert it with a cheap `BEGIN/END_ASSERT_LOCK_NOT_NECESSARY` macro (Naughty Dog shipped this; it caught real overlaps).

**Book 〜 SIMD & GPGPU (§4.10–4.11, pp. 331–357).** SSE = 4 floats/128-bit reg (16-byte aligned), AVX = 8/256-bit, AVX-512 = 16/512-bit; `#include <x86intrin.h>` on Clang. Best practice: **write the scalar single-lane algorithm first, then widen to N-at-a-time** 〜 the same source scales SSE→AVX→AVX-512. **Avoid horizontal adds** (`hadd` dot products are slower than scalar); transpose and multiply component-wise instead (~3.5× speedup). SIMD has no per-lane `if` 〜 compute both branches and **select via a comparison mask**. GPGPU = a data-parallel coprocessor for *independent* per-element work; **SIMT** = SIMD + cooperative wavefront/warp scheduling that hides memory stalls.

**Anoptic.**
- **Logger:** the hot path (`atomic_fetch_add` to reserve a slot, write payload, release-store a per-slot commit header; consumer walks with acquire-loads) is *precisely* the §4.9 transaction + acquire/release pattern. The "gap problem" in notes.md (out-of-order commit) is the §4.8 ordering problem, and the per-slot commit header is the standard fix.
- **Lock-free collections:** §4.9.6 is the M&S/`push_front` baseline; the bounded-MPMC Vyukov ring is the §4.8 "stable deterministic ordering" + sequence-counter (ABA) design. Cache-line stripes amortize the §4.9.4 coherency cost over N items per line.
- **Job system:** "private repository, collate later" (p. 288) = per-thread frame arenas with no shared writes. The "no mutexes outside Vulkan" rule = Gregory's spin-locks-over-OS-mutexes guidance taken to its logical end.

**Verdict.**
- ✅ Acquire/release commit-header MPSC, the transaction/retry pattern, sequence counters for ABA, private-then-collate contention avoidance, and lock-not-needed assertions are all directly endorsed. Adopt the `BEGIN/END_ASSERT_LOCK_NOT_NECESSARY` macro 〜 it fits our "robust tooling" ethos and our single-writer-phase assumptions.
- ⚠️ **Tension to own consciously:** Gregory repeatedly cautions that lock-free is "a lot of work and tough to get right," an open research area, and recommends **restricting lock-free to the most performance-critical subsystems** while using spin locks elsewhere (pp. 289, 555). Anoptic's blanket "no mutexes anywhere but Vulkan" is *more* aggressive than the book advises. That's a defensible bet for a million-entity simulator, but it means **every** lock-free structure must clear the ABA + memory-ordering + linearizability bar the book warns about. Build the classic M&S/Vyukov baselines first (notes.md's lock-free-collections baseline, Phase A) and benchmark before the cache-line-stripe experiment (Phase B).
- ⚠️ **Don't rely on `volatile`** anywhere for synchronization 〜 the `_Atomic` on the logger's `tail_index` is correct; plain `volatile` would be a bug. Beware LTO defeating function-call barriers.
- ➕ The book stops at lock-free `push`; our dequeue and the stripe-publication ordering are beyond it 〜 cite Vyukov (MPSC/MPMC) and McKenney for those.
- ➕ SIMD "scalar-first, then widen" is the right discipline for the eventual component-system kernels; 16-byte alignment for SSE dovetails with the §3.3 alignment rules and our arena alignment.

---

<a name="ch-5"></a>
## Ch 5 〜 3D Math for Games (pp. 359–414)

Medium priority 〜 informs whether/how to grow the ad-hoc math in [vertex.c](../../src/vulkan_backend/vertex/vertex.c) into a real library.

**Book.**
- **Handedness is a visualization choice only** 〜 the math is identical; convert RH↔LH by flipping one axis (pp. 361–362). Graphics convention is typically LH, y-up, +z into screen.
- **Use squared magnitude to avoid `sqrt`** whenever comparing lengths (pp. 366–367). "Normal" (perpendicular) ≠ "normalized" (unit length).
- **THE CONVENTION GOTCHA (§5.3.2, pp. 377–378):** Gregory uses **row vectors** (`v' = vM`, transforms read **left-to-right**). The **column-vector** convention (`v' = Mv`, right-to-left) requires **transposing every matrix in the book**. OpenGL/GLSL 〜 and most C math libs 〜 are **column-major / column-vector**.
- Homogeneous coords: **points w=1, directions w=0** (pp. 379–381). Pure-rotation matrices are **orthonormal** so their inverse is their transpose (cheap). Transform **normals** with the **inverse-transpose** of the 3×3 (p. 392).
- **Storage (§5.3.12):** row-contiguous matches row-vector math (most common); **column-strided is sometimes required for fast SIMD** matrix×vector. Detect a library's convention by building a translation matrix with t=(4,3,2) and seeing where the values land.
- **Quaternions (§5.4):** 4 floats for 3 DOF, no gimbal lock, concatenate/apply directly, LERP/SLERP cheaply; for unit quats **q⁻¹ = q\*** (conjugate, no division). Gregory orders components **[x y z w]** 〜 academic papers often use **[w x y z]**; check before porting. SLERP vs LERP: **profile** (Naughty Dog found good SLERP nearly as cheap as LERP on PS3).
- **Useful objects (§5.6):** sphere and plane each pack into a **4-float / 128-bit SIMD** package; a **frustum = 6 planes**; the trick is to project into homogeneous **clip space** where the frustum becomes an AABB, simplifying in/out tests.
- **RNG (§5.7):** prefer Mersenne Twister/SFMT, PCG, or KISS99 over `rand()`.

**Anoptic.** We already hand-roll `Vector2/3/4`, `mat4`, `lookAt`, `perspective`, `multiplyMat4`, and `extractFrustumPlanes`. The frustum extraction (§5.6.6) and the planned compute-cull both want the **clip-space-AABB** formulation. A million-entity sim will do a *lot* of distance comparisons (scoped-resolution LOD) 〜 squared-magnitude everywhere.

**Verdict.**
- ⚠️ **Pin down one matrix convention and document it at the top of `vertex.h`.** We target **Vulkan/GLSL = column-major**, which is the *transpose* of every formula in the book 〜 the single most likely source of silent transform bugs if someone copies a matrix straight from Gregory. (Vulkan also flips clip-space Y and uses z∈[0,1] vs GL's [-1,1] 〜 note both.)
- ✅ Squared-magnitude, inverse-transpose for normals, 4-float SIMD packing of plane/sphere, and the 6-plane frustum all match what we need.
- ➕ If/when entities carry rotation, prefer **quaternions** (4 floats, SIMD-packable, no gimbal lock) over storing 3×3/4×4 per entity 〜 relevant to keeping component arrays small.
- ➕ A real math lib is worth building only when a second consumer appears (physics, transforms for ECS); until then, the renderer-local math is fine. When built, choose **column-strided storage** for the SIMD matrix paths (§5.3.12).

---

<a name="ch-6"></a>
## Ch 6 〜 Engine Support Systems (pp. 417–479) 〜 CORE

The chapter that most directly validates the arena thesis and specs the strings and containers work.

**Book 〜 start-up/shut-down (§6.1, pp. 417–426).** C++ **static-initialization order is undefined** and destructors run unordered after `main()` 〜 bad for interdependent subsystems. Gregory's recommended pattern: give each singleton manager explicit `startUp()`/`shutDown()`, make **constructors/destructors do nothing**, and call start-up in dependency order straight from `main()` (Memory → FileSystem → … ; shut down in reverse). "Brute force, but simple, explicit, and easy to debug" (pp. 420–422). Naughty Dog's `BigInit()` works this way and **avoids dynamic allocation wherever possible**.

**Book 〜 memory management (§6.2, pp. 426–441) 〜 the core.** Two performance levers: dynamic allocation is **very slow** (general-purpose, and `malloc`/`new` often **context-switch into the kernel**), and **access patterns dominate** (contiguous beats scattered). Rule of thumb: **never allocate from the heap in a tight loop** (p. 427). Custom allocators win by serving from a **preallocated block** (no kernel transition) and exploiting usage assumptions:
- **Stack allocator** (pp. 427–429): bump a top pointer up to allocate; free **in reverse order** via `getMarker()`/`freeToMarker()`. A **double-ended** variant puts two stacks in one block (Midway's *Hydro Thunder* put level-load data on the bottom and per-frame temporaries on top and **never fragmented**, pp. 429–430).
- **Pool allocator** (pp. 430–431): for fixed-size blocks; O(1) alloc/free via a free list whose "next" pointer is **stored inside each free block** (or an index if elements are smaller than a pointer).
- **Single-frame & double-buffered allocators** (pp. 434–437): a stack `clear()`ed every frame ("blindingly fast," never freed 〜 *never cache a pointer across a frame boundary*); the double-buffered version ping-pongs two stacks so frame *i*'s results survive into *i+1* 〜 ideal for **async/multicore job results**.
- **Aligned allocation** (pp. 431–434): over-allocate, round the address up to the alignment, store the shift in the byte just below the returned pointer so `free` can recover the original.
- **Fragmentation** (pp. 437–441): mixed-size alloc/free leaves holes; allocation can fail even with enough *total* free bytes. **Stack and pool allocators are immune.** For general heaps, **defragment by relocation** 〜 which requires **handles** (index into a pointer table) or smart pointers, because raw pointers break when blocks move. Amortize: relocate N blocks/frame.

**Book 〜 containers (§6.3, pp. 441–456).** Prefer **contiguous arrays** (cache-friendly, no per-node overhead) over linked lists (≤16 B of next/prev overhead per element on 64-bit, plus non-contiguous nodes). Grow dynamic arrays by doubling, but **if you can establish a high-water mark, just preallocate it**. STL drawbacks: generic = slower/bigger, **does a lot of hidden dynamic allocation**, and its **allocator model can't express stack-based allocators** 〜 Naughty Dog **prohibits STL containers in runtime game code**. For dictionaries, a **closed (open-addressing) hash table** has **fixed memory, no dynamic allocation** 〜 good for consoles; use a **prime table size + quadratic probing** (or Robin Hood hashing). Good hashes: xxHash, MurmurHash3, CityHash.

**Book 〜 strings (§6.4, pp. 456–470).** Strings are expensive: `strcmp` is O(n), `strcpy` copies + maybe allocates; Gregory profiled a game where **`strcmp`/`strcpy` were the top two most expensive functions** (p. 457). `std::string` has hidden copy-ctor/allocation costs 〜 **always pass by reference**; know whether a string class **owns** its buffer or references memory it doesn't own, and whether it's copy-on-write. The big idea: **hashed string ids** ("string id" / Unreal `FName`) 〜 hash a string to an int, compare as fast ints, keep the originals in a global table for debug. **Interning** = hash + add to the table; intern **once and cache** the result. Naughty Dog uses **compile-time hashing** via user-defined literals (`"foo"_sid`) so ids can be `switch` labels, and moved from 32-bit to **64-bit hashes** to eliminate collisions. **Unicode/UTF-8** (pp. 462–466): UTF-8 is **ASCII-backward-compatible**, byte-granular, multibyte chars flagged by the high bit; Naughty Dog uses **8-bit `char` + UTF-8 everywhere**. Define **your own character/string type** (`wchar_t`, whose size is platform-dependent).

**Book 〜 configuration (§6.5, pp. 470–479).** Persist options to **text config files** (INI/JSON preferred). Quake **cvars** = named float/string globals with an `ARCHIVE` flag. Naughty Dog binds in-game menu items **directly to the address** of a global and persists via a Scheme-like data-definition language that **auto-generates C struct headers** and is looked up by `SID(...)`.

**Anoptic.**
- The arena hierarchy (process → level → frame → scratch → pool) **is** §6.2's allocator catalogue: level/session = stack allocator with markers; frame = single-frame allocator; scratch = stack allocator; pool = §6.2.1.2 pool allocator; double-buffered = job-result buffers. `LOCALHEAPATTR` (mimalloc local heap + `__attribute__((cleanup))`) is the C realization of "destroy the whole region at scope exit." Hugepages back the process arena.
- The owned string `{char* ptr, uint32_t len, uint32_t capacity}` is exactly Gregory's "owns-its-memory, carries its length" design; copy-on-slice ≈ the COW/string_ref question; UTF-8-transparent storage matches his recommendation precisely.
- Containers: stb_ds as a prototyping stopgap aligns with "dynamic arrays for development, fixed-size once budgets are known."

**Verdict.**
- ✅✅ §6.2 is the strongest external validation of the entire memory architecture. The *Hydro Thunder* "one double-ended stack, never fragmented" anecdote is our level-vs-frame arena split, shipped in a real game. Stack/pool fragmentation-immunity is the reason we can refuse a general heap.
- ✅ Start-up/shut-down: in **C this is free** 〜 there are no constructors to misorder. Our modules already use explicit `ano_*_init()`/`ano_*_cleanup()` (e.g. [anoptic_log.h](../../include/anoptic_log.h)). Keep `main()` (the main loop) calling them in explicit dependency order; this is Gregory's recommended pattern with the C++ caveat deleted.
- ✅ The owned-string + length + ownership design and UTF-8 transparency are confirmed best practice; the `strcmp`/`strcpy` profiling result is the empirical case for it.
- ➕ **Adopt hashed string ids early.** This isn't yet in notes.md but it's the natural id type for ECS entity-type names, event types ([Ch 16](#ch-16-foundation)), resource GUIDs ([Ch 7](#ch-7)), and config keys. C23 gives us `constexpr`/compile-time-evaluable hashing, so we can replicate Naughty Dog's `"name"_sid` → `switch`-able constant without C++ UDLs. Use a **64-bit** hash from the start. This one primitive unifies four subsystems 〜 high payoff.
- ➕ **Handles for relocatable data.** If level/pool arenas ever defragment, §6.2.2.2 says only handles (id + generation index into a table) survive relocation. This is also the ECS entity-handle pattern (see [Ch 16](#ch-16-foundation)). Decide now that cross-arena references are handles.
- ⚠️ Confirm our container strategy avoids STL-style hidden allocation; closed/open-addressing hash tables (fixed memory, prime size + quadratic probing) are the console-grade choice if we outgrow stb_ds.

---

<a name="ch-7"></a>
## Ch 7 〜 Resources & the File System (pp. 481–523) 〜 CORE (asset/filesystem)

Elevated per the architect's priorities. This is the biggest *gap* between the book's mature practice and our current state: we parse glTF JSON at runtime with loose `malloc`/`free`; the book argues for offline-baked, load-in-place binary resources behind a real resource manager.

**Book 〜 file system (§7.1, pp. 482–492).** Wrap the native FS API for two reasons: cross-platform consistency, and because the OS API lacks engine needs 〜 chiefly **streaming** (p. 482). Path handling is more than string ops (isolate dir/name/extension, canonicalize, abs↔rel) 〜 implement a **stripped-down path API** over the native one (p. 486). **Avoid runtime path searching** 〜 asset paths are knowable a priori (p. 485). C stdlib gives **buffered** (`fopen`…) and **unbuffered** (`open`…) I/O; both are **synchronous** (the caller blocks). **Asynchronous I/O / streaming (§7.1.3, pp. 489–492)** is the key capability: `asyncReadFile()` returns immediately and a **callback** fires on completion, or the caller **waits later** (`asyncWait`). Requests carry **priorities and deadlines** so streaming the next audio buffer can preempt loading a texture. Implementation: a **separate I/O thread** pulls requests off a **queue**, does blocking reads, and signals completion via a **semaphore** 〜 "nearly any synchronous op becomes async by moving it to a separate thread."

**Book 〜 the resource manager (§7.2, pp. 493–523).** Every resource manager has **two halves**: an **offline tool chain** that conditions assets into engine-ready form, and a **runtime manager** that loads/unloads them. Core runtime responsibilities (pp. 503–504): ensure **one copy in memory** per unique resource; manage lifetimes; handle **composite resources** (a model = mesh + materials + textures + skeleton + animations); maintain **referential integrity**; manage memory placement; run **post-load initialization**.
- **Asset conditioning pipeline (ACP, pp. 501–503):** exporters (DCC plug-in) → resource compilers (massage data: compress textures, build strips) → resource linkers (combine into one package). Assets have **interdependencies** that dictate build order and rebuild rules.
- **Resource GUIDs (pp. 507–508):** every resource needs a globally-unique id; the most common is the **file path** (string or 32-bit hash).
- **Resource registry (pp. 508–509):** a dictionary GUID→pointer enforces single-copy. Auto-loading on demand causes gameplay **hitches**; prefer **load behind a screen** or **async streaming**.
- **Resource lifetime (pp. 509–511):** global / level-lifetime / shorter-than-level / live-streamed. Because resources are **shared across levels**, manage unload by **reference counting** (increment for the new level's resources, decrement the old, unload at zero).
- **Memory layout (pp. 511–516):** stack-based (no fragmentation, if each level fits in RAM), double-ended stack (Hydro Thunder), or **pool of equal-sized chunks** for streaming (typical chunk a few KiB 〜 ND used 512 KiB on PS3, 1 MiB on PS4). **Sectioned files** split a resource into main-RAM / video-RAM / **temporary-load-time** / debug-only sections.
- **Cross-references & load-in-place (pp. 516–521):** in memory a cross-ref is a pointer, but pointers are meaningless on disk. Two schemes: **(1) store GUIDs and convert to pointers via a global lookup table after load**; **(2) pointer fix-up tables** 〜 serialize objects contiguously, convert each pointer to a **file offset**, store a table of pointer locations, and on load do `pointer = baseAddress + offset`. **C++ needs placement-new to run constructors on a loaded image; restricting resources to PODS avoids this entirely.**
- **Post-load init (pp. 521–523):** some setup can only happen at runtime (e.g. uploading vertex/index data to video RAM). In **C**, configure this with a **lookup table mapping resource type → {init fn ptr, teardown fn ptr}**.

**Anoptic.**
- `ano_fs_gamepath`/`ano_fs_userpath` (path structs, Linux + Windows backends) are §7.1.1's stripped-down path API. We have **no async I/O yet** 〜 §7.1.3's thread + queue + semaphore + callback model is the blueprint, and it's a natural early consumer of the lock-free queue and `anoptic_threads`.
- Our glTF path ([src/render/gltf/ano_GltfParser.c](../../src/render/gltf/ano_GltfParser.c), jsmn-based, staging through `scratch_process.c`) is the *runtime text-parse* approach the book argues against. The scratch-arena staging is good and maps onto the **temporary load-time section** idea (§7.2.2); the loose `malloc`/`free` (a known debt in notes.md) is exactly what §7.2 says to push offline.

**Verdict.**
- ✅ The "wrap the FS, the OS doesn't give you streaming" rationale and the stripped-down path API match `ano_fs` exactly.
- ✅ Because Anoptic is **C23**, the **load-in-place PODS path is free** 〜 no placement-new, no constructor-ordering problem. Pointer-fix-up tables + offsets are a clean fit with arena allocation (load the whole image into one arena block, fix up, done). This is a genuine C-over-C++ advantage the book half-acknowledges.
- ⚠️ **The console seek-time argument is largely moot on our SSD/desktop target** (the book itself notes SSDs don't suffer seek penalties, p. 505) 〜 but the *other* reasons to bake packed binary resources still hold: **eliminating per-file open cost, sequential bulk reads, and zero runtime parsing/formatting.** Don't cargo-cult the optical-disc layout; do adopt offline conditioning + load-in-place.
- ➕ **The resource manager is a real architectural hole.** Before the glTF/asset path grows, introduce: (1) a **resource registry** (GUID→pointer) for single-copy enforcement, where GUID = the hashed string id from [Ch 6](#ch-6); (2) **reference-counted lifetimes** tied to the level/session arena; (3) an **offline bake step** that turns glTF into a load-in-place binary so the runtime does no jsmn parsing; (4) **handles** for inter-resource references. This is the single largest "the book has it, we don't" item, and it's foundational (asset/filesystem was flagged top-priority).
- ➕ **Async streaming** (§7.1.3) is what makes "scoped resolution" catch-up viable without hitches 〜 promoting a distant star system to full fidelity is a background load. Build the thread+queue+callback async-I/O layer on top of the lock-free queue.

---

<a name="ch-8"></a>
## Ch 8 〜 The Game Loop & Real-Time Simulation (pp. 525–558) 〜 CORE

Specs the timing module and the main loop, and frames the multi-core direction.

**Book 〜 loop & timelines (§8.1–8.4).** Subsystems need servicing at **different rates** 〜 animation 30/60 Hz, physics often 120 Hz, AI maybe 1–2 Hz and not synced to render at all (pp. 526–527). Loop styles: **Windows message pump** (game freezes while the window is dragged/resized), **callback frameworks** (the framework owns the loop), and **event-based updating** (post an event into the future, the handler re-posts one period ahead). A **timeline** is a clock variable; **game time can diverge from real time** 〜 pause by halting the game clock while render + a debug fly-cam keep running on a separate clock; single-step by advancing the game clock one frame interval (pp. 532–534).

**Book 〜 measuring time (§8.5, pp. 534–544) 〜 central.**
- Move by `Δx = v·Δt` (explicit Euler); perceived speed depends on Δt (pp. 535–538).
- **Don't reuse last frame's Δt as next frame's estimate** 〜 a spike breaks it. **The spiral of death:** after one long frame you step physics twice to "catch up," which takes ~2× as long, making the next frame worse (pp. 536–537). Mitigations: **running average** of Δt; **frame-rate governing** (sleep until the target interval elapses) 〜 but design all systems for arbitrary Δt regardless. Consistent frame time helps numerical integrators, avoids tearing, and makes **record-and-playback deterministic** (events + timestamps + identical RNG seed).
- **V-sync** = waiting for the vertical blank before buffer swap; itself a form of governing.
- **High-resolution timer (§8.5.3):** all modern CPUs have a cycle-counting register; `QueryPerformanceCounter`/`QueryPerformanceFrequency` on Win32, `rdtsc`, PowerPC `mftb`. 64-bit so it wraps in ~195 years (a 32-bit cycle clock wraps in ~1.4 s). **Per-core timers can drift** 〜 comparing absolute readings across cores can yield negative deltas (§8.5.3.1).
- **Clock variables (§8.5.4):** prefer a **64-bit integer cycle clock**; if using 32-bit or float, **subtract the two 64-bit timestamps first, then convert**, to avoid negative/garbage deltas. **Float clocks lose precision over time** (23-bit mantissa) 〜 use them only for short deltas.
- **Breakpoints (§8.5.5):** the real-time clock keeps running at a breakpoint; on resume Δt could be minutes. **Clamp Δt** to the target (e.g. 1/30 s) when measured frame time exceeds a ceiling (~1 s).

**Book 〜 multiprocessor loops (§8.6, pp. 544–558).** One-thread-per-subsystem **doesn't work** (thread count ≠ core count, uneven load, hard dependencies). Better: **scatter/gather** (split a data-parallel task into ~one batch per core, write into **separate preallocated per-thread output buffers**, then `pthread_join`). But **spawning threads is expensive** 〜 use a **pre-spawned thread pool**, which generalizes to a **job system** (§8.6.4): subdivide each frame into many fine-grained **independent jobs** submitted to a queue and distributed across core-pinned worker threads 〜 "like a lightweight OS kernel that schedules jobs, not threads." A job = entry-point fn ptr + a single `uintptr_t` param (+ priority + a `Counter*`). Simple pool jobs **must run to completion** (they share the worker's call stack); to let a job *sleep* mid-execution (e.g. waiting on a ray-cast), implement jobs as **coroutines or fibers** (Naughty Dog's is fiber-based). Join via a **counter** ("a semaphore in reverse": kick increments, finish decrements, wait until zero). **Use spin locks inside jobs** 〜 an OS mutex sleeps the whole worker thread and can deadlock the pool.

**Anoptic.**
- `ano_timestamp_raw` (ns, `CLOCK_MONOTONIC`/`QueryPerformanceCounter`), `ano_busywait` (spinloop), `ano_sleep` are §8.5.3's high-res timer and §8.5.2.4's governor. The overflow-safe QPC→ns conversion in notes.md is the §8.5.4 "subtract 64-bit values before scaling" rule.
- **Windows hi-res timing**: the `Sleep()` 15.6 ms jitter problem is precisely why §8.5 insists on a high-res timer and Δt discipline; the `timeBeginPeriod` + waitable-timer + spin-tail plan is a frame-rate governor with sub-ms precision.
- **Main loop** + **job system**: §8.6 is the blueprint 〜 per-thread frame arenas = the "separate preallocated output buffers," and the `uintptr_t`-param + counter job API is a clean, C-friendly, lock-free-aligned interface.
- **Scoped resolution** = §8.2's "different subsystems at different rates," applied to star systems: active = full tick, distant = coarse/infrequent. Deterministic catch-up is the §8.5.2.4 record-and-playback determinism requirement (fixed step + fixed seed) generalized.

**Verdict.**
- ✅ The high-res timer, governor, Δt-clamp, and 64-bit-subtract-before-convert rules directly validate the timing module. The cross-core drift warning (§8.5.3.1) is worth a note in `anoptic_time` for the multi-core era (the job system).
- ✅ The job-system design (thread pool, `uintptr_t` param, counter-based join, spin-locks) is exactly the C-friendly, no-mutex approach Anoptic wants for the job system.
- ⚠️ **Build the deterministic fixed tick deliberately.** Gregory states the *constraints* (spiral of death, integrators like constant rate, Δt clamp) but the 3rd edition **does not give an explicit "fixed-timestep-with-accumulator + interpolation" code pattern**. That canonical pattern 〜 fixed sim dt, accumulate real Δt, run N fixed sub-steps, clamp the accumulator, interpolate render state between sim states 〜 is the standard synthesis we should implement in the main loop; cite Gaffer-on-Games "Fix Your Timestep" alongside Gregory's constraints.
- ➕ **Fibers/coroutines for jobs** (§8.6.4) is how you make blocking-style code (`castRayAndWait`) work without stalling a core. Likely overkill for v0.1, but the right long-term target for a job system that the simulation and async streaming both lean on.

---

<a name="ch-10"></a>
## Ch 10 〜 Logging & Memory Tooling (§10.1 pp. 589–594, §10.9 pp. 615–618)

**Book 〜 logging (§10.1).** `printf` debugging is still valuable, *especially* for real-time, timing-dependent bugs that breakpoints can't catch (p. 590). Provide a `va_list`-based core (`VDebugPrintF`) so other print functions can be layered on it 〜 "you can't pass `...` between functions, but you can pass a `va_list`" (pp. 590–591). Add **verbosity levels** (a global threshold; only print if the message's level ≥ it) and **channels** (per-subsystem categories; with ≤32/64 channels, use a **bitmask** so a filter is a single integer) (pp. 591–593). **Mirror all output to a log file** regardless of verbosity/channel filters, so post-mortem diagnosis works; **flush** after each call **only if** you log little or you've proven it necessary, since flushing is expensive (pp. 593–594). **Crash reports** (§10.1.5): install a top-level exception handler that dumps level, player position, **stack trace**, and **memory allocator state** (free/fragmentation).

**Book 〜 memory stats (§10.9).** Wrapping `malloc`/`free` is **not** enough to track usage: you can't see inside third-party libs, video-RAM, or 〜 crucially 〜 **custom allocators** (ND has six: global heap, game-object heap, level-load heap, single-frame stack, video-RAM allocator, and a **debug-only heap** for data not shipped). To get useful data you must track allocations **inside each allocator's block**. Report **high-water marks** per subsystem and provide on-screen/graphical displays and obvious failure cues (missing model → red text in world; missing texture → ugly pink).

**Anoptic.**
- The logger: 5 levels (DEBUG/INFO/WARN/ERROR/FATAL) = §10.1.2 verbosity; immediate mode for FATAL = the "flush so the crash log isn't missing the last buffer" advice; the not-yet-wired file output (`ano_log_output_dir`, flusher thread) is §10.1.4 file mirroring.
- The lock-free MPSC hot path is *our* addition on top of the book's single-threaded model (the book never makes the logger lock-free).

**Verdict.**
- ✅ Levels, file mirroring, and FATAL-flushes-immediately are all confirmed. The `va_list` core/wrapper split is good C practice 〜 make `ano_log_*` build on one `va_list` function.
- ➕ **Add channels as a 64-bit bitmask** (per-subsystem: render/memory/sim/io/…). One integer filters output, costs almost nothing, and pairs with hashed string ids for channel names. Not in notes.md; cheap, high-utility, and the logger is being built right now 〜 the best moment to add it.
- ➕ **Memory instrumentation must be arena-aware.** §10.9's central lesson 〜 you must track *inside* each custom allocator 〜 applies directly: report per-arena high-water marks (process/level/frame/scratch/pool). mimalloc's stats hooks plus our own per-arena counters give this. Also fold "allocator state on crash" into the FATAL path.

---

<a name="ch-11"></a>
## Ch 11.1–11.2 〜 The Rendering Engine (pp. 622–697)

Medium priority, for the renderer rewrite. We use Vulkan directly and **deliberately skip PBR**, so the relevant material is the pipeline architecture and the **high-entity-count** techniques.

**Book.**
- **Indexed triangle lists** (vertex buffer + 16-bit index buffer) avoid duplicate transform/lighting; offline **vertex-cache optimizers** reorder triangles for post-transform cache reuse (pp. 628–630).
- **Mesh instancing** (pp. 631–632): many instances share one mesh + a per-instance **model-to-world matrix** 〜 a large memory/bandwidth win for repeated objects. **The technique pairing that matters most for us:** mesh-instance model + indexed/indirect draws + material-sorted batching.
- **Transform chain** (pp. 656–663): model→world (world matrix) → view (world-to-view) → **clip** (projection) → screen. Often concatenate model+view. Clip-space z is **[0,1] in DirectX/Vulkan**, [-1,1] in GL.
- **Materials & submeshes** (p. 646): a material = textures + shaders + render-state; a model splits into **submeshes**, one material each. A mesh-material pair is a "render packet."
- **Buffering** (pp. 663–664): **double buffering** avoids tearing; **triple buffering** lets the engine start the next frame instead of idling.
- **Depth/z-buffer** solves occlusion order-independently; 1/z precision concentrates near the camera → **z-fighting** far away (w-buffering fixes it). Back-face culling by winding.
- **The pipeline** (pp. 667–677): tools → ACP (offline) → **application stage (CPU:** visibility
  + geometry submission + render-state control**)** → geometry processing (GPU: vertex shader, optional geometry shader, clip) → rasterization (GPU: fragments, **early-z**, pixel shader, ROP/blend). Compute-shader culling slots in as GPGPU (p. 673).
- **Culling 〜 "the cheapest triangles are the ones you never draw"** (pp. 687–690): **frustum cull** each object's **bounding sphere** against the 6 planes (plug center into each plane equation, compare distance to radius); **occlusion culling**; **PVS**; **portals** (indoors) and **occlusion volumes/antiportals** (dense outdoors).
- **Scene graph / spatial subdivision** (pp. 693–696): quadtree/octree/BSP/kd-tree/sphere hierarchy to discard large off-screen regions in O(log n).
- **Render queue & state-change minimization** (pp. 691–693): render state is **global** 〜 changing it flushes the GPU pipeline, so **sort geometry by material** to minimize state changes. Per-draw-call CPU cost can dominate, so high-perf engines build command lists manually or via **a low-level API like Vulkan (named explicitly, p. 692)**. **z-prepass** reconciles material-sorting with early-z (fill depth front-to-back cheaply, then draw material-sorted).

**Anoptic.** We already have a geometry pool (vertex/index mega-buffers), bindless textures, planned compute-cull, and indirect draws 〜 i.e. we've independently arrived at the instancing + batching + culling stack the book identifies as the high-entity-count core.

**Verdict.**
- ✅ Vulkan-direct + manual command lists is the book's own recommendation for minimizing per-draw CPU cost (p. 692). Our geometry pool, bindless textures, indirect draws, and compute-cull are the correct million-entity techniques.
- ✅ Frustum-cull-by-bounding-sphere-vs-6-planes is the exact kernel for the planned compute-cull; it dovetails with [Ch 5](#ch-5)'s clip-space-AABB frustum trick.
- ➕ **Mesh LOD chains** (pp. 625–627) are the rendering-side analogue of **scoped resolution**: spend transform/shading budget on the near/large instances, swap to coarser meshes (or fewer march steps, for the future SDF path) with distance. Build a render-side LOD selector that reads the same distance metric as the simulation LOD.
- ➕ **Render-queue sort key** (material/shader/depth) and an optional **z-prepass** belong in the renderer rewrite; sorting to minimize state changes is the cheapest large win.
- ⚠️ Skipping PBR is consistent with the book treating advanced lighting as a separate concern (§11.3, **not distilled here**) 〜 but keep the *architecture* (materials, submeshes, render packets, the programmable vertex/fragment stages) even though our materials stay thin. Note the Vulkan clip-space conventions (z∈[0,1], flipped Y) against the book's GL-leaning examples.

---

<a name="ch-16-foundation"></a>
## Ch 15–16 〜 Gameplay Foundation & Event Pipelining (pp. 1015–1158) 〜 CORE (events)

The ECS rationale and the event-bus blueprint. §16.8 (events) is elevated per the architect's "event pipelining" priority and is the most detailed section below.

**Book 〜 game worlds & objects (Ch 15, pp. 1015–1025).** A world = **static** (precomputed, never changes 〜 an optimization category) + **dynamic** elements. Dynamic elements are **game objects** (a.k.a. entities/actors/agents) = **attributes** (state) + **behaviors**. A **type** ≠ an **instance**. **Tool-side** object model (what designers see) need not match the **runtime** object model (how it's implemented) 〜 a tool-side type can be "just a unique id with state in tables." Data-driven engines give designers power but cost heavily in tooling 〜 beware over-engineering (KISS).

**Book 〜 runtime object models (§16.2, pp. 1043–1062) 〜 the ECS chapter.** Two styles:
- **Object-centric:** each GO is a class instance; the monolithic inheritance hierarchy (Unreal's `Actor`) has well-known problems (pp. 1046–1051): deep hierarchies are unmaintainable; a tree can only classify on **one axis** (amphibious vehicle breaks land/water); multiple-inheritance "deadly diamond"; the **"bubble-up effect"** pushes every feature toward the root. The fix is **composition over inheritance** (pp. 1051–1055): `GameObject` becomes a **hub** owning **components** (MeshInstance, RigidBody, Transform…), each a single service often mapping 1:1 to an engine subsystem.
- **Pure component model (pp. 1056–1057):** strip all behavior from `GameObject` until it's just a **unique id**, then eliminate the hub 〜 **components share the id and are looked up by it.** This is the entity-as-pure-id ECS model exactly. Open problem the book flags: **inter-component communication** (look up siblings by id 〜 slow 〜 or pre-wire them).
- **Property-centric (pp. 1043, 1057–1061):** each GO = a unique id; properties live in **many tables, one per property type, keyed by id** 〜 "more akin to a relational database than an object model." Gregory's explicit pros: **memory-efficient** (store only the attributes in use), easy to **data-drive**, and **more cache-friendly because same-type data is contiguous = "struct of arrays" (SoA)** vs "array of structs" (AoS) 〜 he gives the AoS-vs-SoA code and cites a PS3 cache miss ≈ thousands of instructions (pp. 1060–1061). Cons: hard to enforce relationships between properties and harder to debug.

**Book 〜 references & queries (§16.5, pp. 1079–1086).** Raw **pointers** are fast but suffer stale/dangling/relocation problems. **Handles** (recommended): an **integer index into a global handle table** of pointers; on delete, null the slot (all handles become null); store a **unique id in the handle** and validate on dereference to catch slot reuse; handles **survive memory relocation** (update one table entry). World **queries** are served by **specialized accelerators per query type** 〜 hash table by id, pre-sorted lists, collision casts for line-of-sight, and **spatial hash / grid / quadtree / octree / kd-tree** for radius/region queries.

**Book 〜 updating objects (§16.6, pp. 1086–1101).** The naive "iterate all objects, call virtual `Update(dt)`, each updates its own subsystems inline" is the **anti-pattern** (pp. 1088–1090). Instead use **batched updates**: the game loop drives each subsystem **once, in a big batch**, so the subsystem keeps its per-object data **contiguous** (maximal cache coherency); objects only *manipulate* subsystem state 〜 **this is ECS systems iterating tight contiguous loops.** Handle inter-object/subsystem dependencies with **phased updates** (objects get multiple per-frame hooks; ND updates GOs three times) and **bucketed updates** (group objects by dependency-tree tier, update bucket by bucket). The **consistency rule** (pp. 1098–1100): object states are consistent **before and after** the update loop but **inconsistent during** it 〜 querying a peer mid-loop risks **one-frame-off lag**. **State caching** (pp. 1100–1101): cache each object's previous state, so any object can safely read any other's previous state 〜 "explicitly tied to **pure functional programming** (immutable data, produce a new datum)."

**Book 〜 concurrency on object updates (§16.7, pp. 1101–1114).** GO models are "notoriously difficult to parallelize." Kick subsystem work as **many jobs** with **scatter/gather**; interfaces must be thread-safe (spin locks / lock-free / lock-not-needed assertions). Think **asynchronous**: replace blocking calls with **non-blocking request + later wait/result**, and tolerate a deliberate **one-frame lag** where possible 〜 *"the secret of optimized concurrent design is delay"* (Mike Acton). **Degree of Parallelism (DOP)** = leaves in the dependency graph; dependencies create **sync points** where cores idle. The shipped solution (TLOU Remastered, Uncharted 4): **object snapshots** 〜 observe that most inter-object interactions are **read-only**, so each object publishes a **read-only snapshot** of its state at bucket start (no cross-queries → run concurrently, lockless); everyone reads snapshots lock-free. Writes are harder: **minimize inter-object mutation**, and **defer cross-object mutations to a lock-protected request queue** processed after the bucket update.

**Book 〜 events & message-passing (§16.8, pp. 1114–1134) 〜 TOP PRIORITY.**
- The naive approach 〜 a virtual `OnExplosion()` per object 〜 is **statically-typed late binding** and is inflexible (forces a base class declaring a handler for every event, every object "knows" every event). What's wanted is **dynamically-typed late binding**: **encapsulate the call in an object** (the GoF **Command pattern**).
- **An event = type + arguments**, stored as data: `struct Event { EventType type; U32 numArgs; EventArg args[MAX]; }` (pp. 1116–1117). Three payoffs of making it an object: a **single handler** (`OnEvent(Event&)` switches on type); **persistence** (it can be **queued for later, copied, broadcast**); and **blind forwarding** (forward without understanding it).
- **Event types** (pp. 1117–1118): a global **enum** is simple/fast but centralizes all event knowledge and is **order-dependent** (inserting an enumerator shifts stored indices) 〜 "works for demos, doesn't scale." **Strings** are flexible but slow/collision-prone. **Hashed string ids** are the practical choice (fast int compares, recoverable strings) 〜 ND uses a central **event-type database** with conflict detection and per-event metadata.
- **Arguments** (pp. 1118–1120): a tagged-union **`Variant`** (`{Type type; union{i32;f32;bool; stringId;};}`), in a **fixed-size array** (no allocation 〜 good for memory-constrained targets) or dynamic. Prefer **key-value pairs** over positional args to avoid sender/receiver **order dependency** (optional/omittable args).
- **Distribution:** **chains of responsibility** (pp. 1121–1123) 〜 forward an event along an object **relationship graph**; a handler returns whether it **consumed** the event (stop) or passes it on; also used to **multicast** to all objects in a radius via the query system. **Registering interest** (pp. 1123–1124): most objects care about few event types 〜 keep **one list of interested objects per event type**, or a **per-object interest bitmask**, or (best) restrict the originating **query** to only-interested objects. This is **publish- subscribe**.
- **To queue or not (pp. 1124–1129):** queuing buys (1) **control over when** events are handled (only at safe points in the loop), (2) **posting events into the future** (sender sets a delivery time → sort the queue by delivery time, dispatch all with time ≤ now; enables alarm-clock/periodic tasks that re-post themselves), and (3) **prioritization** (sort by delivery time, then decreasing priority; use the *fewest* priority levels that resolve real ambiguities). Costs: more **complexity**; queued events must be **deep-copied** (args must outlive the sender's stack frame); **dynamic allocation** 〜 use a **pool allocator** (fits fixed-size Variant events); harder **debugging** (the handler isn't on the sender's call stack 〜 keep a "paper trail"); and **multiple dispatch points** may be needed to avoid one-frame delays. **Immediate** sending instead risks **very deep call stacks** and requires every handler to be **re-entrant**.
- **Data-driven event systems (pp. 1131–1134):** from designer-configurable responses, through **scripting**, to **graphical flowcharts (Unreal Blueprints)** / **data-flow port systems** (think in streams of typed data through wired input/output ports).

**Anoptic.**
- **ECS** = §16.2's pure-component / property-centric model. Gregory's SoA endorsement and the AoS-vs-SoA cache argument (pp. 1060–1061) are the textbook backing for entity-as-id + contiguous component arrays. The "explicitly tied to pure functional programming" line (state caching, §16.6) is almost a direct quote of notes.md's "arenas are the FP memory model with the GC removed."
- **Entity handles** = §16.5 handles (id + generation, validated on deref) 〜 also the relocation-safe reference from [Ch 6](#ch-6).
- **Event bus** = §16.8 in its entirety: Command-pattern event objects, hashed- string-id event types, Variant key-value args, interest registration (pub-sub), and a future-stamped, prioritized, pool-allocated queue.
- **Scoped resolution / deterministic catch-up** = §16.6 bucketed updates at different tick rates + §16.8 future-dated events + §16.7 snapshots, generalized across star systems.
- **Parallel million-entity tick** = §16.7: snapshots (lock-free read path) + deferred mutation request queue (≈ a command buffer / the event bus) + the job system from [Ch 8](#ch-8).

**Verdict.**
- ✅✅ **ECS is fully vindicated by the book itself.** §16.2 walks from monolithic inheritance → composition → pure-component / property-centric SoA and lands on exactly Anoptic's model, with the cache argument as justification. When someone asks "why not just use an object hierarchy like Unreal," §16.2.4's problem list (single-axis taxonomy, deadly diamond, bubble-up) is the answer.
- ✅ **The event-bus design is almost entirely pre-specified by §16.8.** Adopt directly: event = type + args **object** (Command pattern); **hashed-string-id** event types (reuse the [Ch 6](#ch-6) `_sid` primitive 〜 solves the enum order-dependency problem); **Variant key-value** args; **interest registration** (pub-sub) to avoid blind broadcast; **future-stamped + prioritized** queue; **pool-allocated** fixed-size events (fits our arena rule). The "deep-copy queued events" and "pool allocator" cautions map straight onto allocating events from a frame/scratch arena.
- ➕ **This is where Anoptic goes beyond the book.** Gregory's event queue is **single-threaded** (sort, dispatch, all on the loop thread). notes.md's event-bus work proposes **two buses** 〜 a classic lock-free monotonic queue for ordered events (input) and a **lock-free cache-line-striped** bus for high-throughput bulk events (physics/sim). The book gives the *semantics* (queuing, future-stamping, priorities, pub-sub); we supply the *lock-free concurrent substrate* it lacks. The §16.7 **snapshot** + **deferred mutation request queue** pattern is the bridge: the request queue *is* the bulk event bus.
- ⚠️ **Mind the one-frame-off-lag rule** (§16.6): consistency holds only before/after the update loop. Our snapshot/double-buffer approach must make the read-vs-write phase explicit, and the lock-not-needed assertions from [Ch 4](#ch-4) are how we check it cheaply.
- ➕ **Adopt the spatial-index query accelerators** (§16.5) for proximity queries in the colony sim (which entities are near this star/station) 〜 a grid/quadtree/octree over the entity set, the same structure that feeds GPU culling in [Ch 11](#ch-11).
- ⚠️ KISS warning (§15.3): don't build a fully data-driven Blueprints-style event editor up front; start with code/enum-of-`_sid` handlers and grow toward data-driven only when designers actually need it.

---

<a name="deferred"></a>
## Deferred topics (noted)

Out of scope for the current/near-future roadmap; pointers for when they become relevant:

- **Ch 2 〜 Tools of the Trade** (pp. 69–104): version control, compilers/linkers, **profiling tools** (VTune, Valgrind), memory-leak detection. Relevant when we set up profiling/CI.
- **Ch 9 〜 Human Interface Devices** (pp. 559–588): input device abstraction, dead zones, debouncing, chords/sequences, control remapping. **Touches the input work** 〜 read §9.5 ("Game Engine HID Systems") when wiring GLFW input into the event bus.
- **Ch 11.3–11.4 〜 Advanced Lighting, Global Illumination & Visual Effects** (pp. 697–719): deliberately skipped 〜 Anoptic is **non-PBR**. Revisit selectively only for the future SDF raymarching path.
- **Ch 12 〜 Animation Systems** (pp. 721–815): skeletons, clips, blending, skinning, state machines. Low relevance to a space-colony sim with no organic characters.
- **Ch 13 〜 Collision & Rigid Body Dynamics** (pp. 817–910): if physics is ever added, §13.3 (collision detection) and §13.4 (rigid-body dynamics) are the references; the event/job infrastructure is the prerequisite.
- **Ch 14 〜 Audio** (pp. 911–1010): future; §14.5 (audio engine architecture) when relevant.
- **Ch 16.9–16.10 〜 Scripting & High-Level Flow** (pp. 1134–1158): a scripting layer and an objectives FSM/task system 〜 far-future game layer.

---

<a name="actions"></a>
## Action items (book → roadmap)

Concrete things the book suggests adding or changing, keyed to where they fit. Roughly ordered by payoff at the current stage.

1. **Hashed string ids (`_sid`), 64-bit, compile-time.** New primitive, not yet in notes.md. One type serves ECS type names, **event types** (fixes the enum order-dependency problem), **resource GUIDs**, **config keys**, and **logger channel names**. C23 constexpr hashing replicates Naughty Dog's `"name"_sid` → `switch`-able constant. *Refs: §6.4.3, §16.8.2, §7.2.3, §10.1.3.* **Highest payoff 〜 unblocks four subsystems.**
2. **Logger channels as a 64-bit bitmask + arena-aware memory stats.** Add per-subsystem channels to the logger (one int = a filter) and per-arena high-water-mark tracking; dump allocator state on FATAL. *Refs: §10.1.3, §10.9.* **Cheap, and the logger is in flight now.**
3. **`BEGIN/END_ASSERT_LOCK_NOT_NECESSARY` macro.** A debug-build invariant for single-writer- phase assumptions across the lock-free code and the parallel tick. *Ref: §4.9.7.*
4. **Build classic lock-free baselines (M&S, Vyukov MPMC) and benchmark before the cache-line- stripe experiment.** The book's repeated "lock-free is hard, restrict it" warning means each structure must clear the ABA + ordering + linearizability bar first. *Refs: §4.9, notes.md's lock-free-collections baseline (Phase A→B).*
5. **Pin the matrix convention in `vertex.h`.** Document column-major / Vulkan clip space (z∈[0,1], flipped Y) explicitly 〜 the book's row-vector formulas are the transpose. Prevents silent transform bugs. *Ref: §5.3.2, §5.3.12.*
6. **Event bus = §16.8 directly:** Command-pattern event objects, `_sid` event types, Variant key-value args, interest-registration pub-sub, future-stamped + prioritized queue, **pool/arena-allocated** events. Layer the **lock-free** substrate (and the cache-line-stripe bulk bus) on top 〜 that's our extension beyond the single-threaded book design. *Refs: §16.8, notes.md's event-bus work.*
7. **Resource manager (the biggest gap):** introduce a **registry** (`_sid` GUID → pointer, single-copy), **reference-counted lifetimes** on the level/session arena, an **offline bake** that converts glTF → **load-in-place** binary (kills runtime jsmn parsing 〜 a current debt), and **handles** for inter-resource references. PODS + arenas make load-in-place free in C. *Refs: §7.2; addresses the glTF loose-malloc/free debt in notes.md.*
8. **Async I/O layer (thread + lock-free queue + completion callback/semaphore).** Built on the lock-free queue; the prerequisite for hitch-free **scoped-resolution catch-up** (background- load a promoted star system). *Ref: §7.1.3.*
9. **Entity handles = id + generation, validated on deref.** The relocation-safe, ECS-standard reference type; decide now that all cross-arena references are handles. *Refs: §16.5.2, §6.2.2.2.*
10. **Main loop: fixed-timestep accumulator + render interpolation + Δt clamp.** The book gives the constraints (spiral of death, integrators want constant rate, breakpoint clamp) but not the explicit pattern 〜 implement the standard synthesis. *Refs: §8.5; pair with Gaffer "Fix Your Timestep."*
11. **Renderer: render-queue sort key (material/shader/depth) + optional z-prepass + mesh LOD chains** tied to the same distance metric as simulation LOD. *Refs: §11.2.*
12. **Parallel tick (later): object snapshots (lock-free read path) + deferred mutation request queue + bucketed updates** on the job system. *Refs: §16.6–16.7, §8.6.*
13. **Settle the type-punning / aliasing policy before load-in-place (item 7).** Standardize on `memcpy` or `union` punning (both legal and free in C23 〜 unlike C++), or build the asset / serialization TUs with `-fno-strict-aliasing`; ban bare pointer-cast puns. Audit the glTF byte reads. *Ref: §3.3 (new in 4th ed.); prerequisite for the load-in-place format.*

---

*Sources: Jason Gregory, Game Engine Architecture, **3rd ed.**, CRC Press / A K Peters, 2018 (ISBN 978-1-138-03545-4) 〜 the spine of these notes; page citations are its printed pages. Cross-checked against the **4th ed., Vol I: Foundations and Core Engine Systems**, CRC Press, 2026 (ISBN 978-1-032-44306-5) 〜 see the Edition note up top. Distilled 2026-06, updated for the 4th ed. 2026-06.*
