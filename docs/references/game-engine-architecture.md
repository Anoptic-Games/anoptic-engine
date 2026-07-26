# *Game Engine Architecture* (3rd ed.) -- distilled for Anoptic

Cross-reference of Jason Gregory, *Game Engine Architecture*, 3rd edition (CRC Press, 2018) against Anoptic. Per topic: where the book **validates**, **challenges**, or **adds nuance** to our decisions (see [docs/notes.md](../notes.md)).

**How to read this.** Book chapter order. `(p. 289)` = book's printed pages. Each section:

- **Book** -- distilled, page-cited content.
- **Anoptic** -- map to our principles / build steps / files.
- **Verdict** -- ✅ validates · ⚠️ challenges or warns · ➕ nuance or technique we lack. C++→C23 translations called out where they matter.

**Worldview.** Gregory: AAA-console 2018, C++/OOP, middleware (Havok, PhysX, Granny), optical-disc / limited RAM, photoreal, modest entity counts. Anoptic: **C23/Clang-only**, no frameworks, **data-oriented**, **lock-free**, desktop SSD + huge RAM, **non-photoreal**, **millions** of entities. Book use: (a) language-agnostic low-level catalogue (allocators, handles, events, jobs); (b) foil where OOP/console assumptions fail. Translate: `std::atomic` → C11 `_Atomic`; RAII destructor → `__attribute__((cleanup))`; "awkward in C++" → often free in C.

**Edition note -- 4th ed. (CRC Press, 2026).** Two volumes. **Vol I -- Foundations and Core Engine Systems**: Ch 1-10 (everything here except rendering and gameplay/events). **Vol II -- Graphics, Motion, and Sound**: rendering, new photoreal lighting chapters, animation (motion matching), physics, audio, game object model + events + scripting. So **Ch 11** and **Ch 15-16** here map to Vol II (section numbers differ there).

- *Vol I numbering stable* for Ch 3-8, 10. Only §1.6 *Runtime Engine Architecture* → **§1.5** (new §1.6 = *Tools and the Asset Pipeline*). Page numbers shifted; `(p. …)` below remain **3rd-ed**.
- *Concurrency chapter reorganized.* §4.9 finer subsections (4.9.2.1-.12 atomics, 4.9.3 barriers, 4.9.4 memory ordering, 4.9.5 atomics + C++ memory_order, 4.9.7 spin locks, 4.9.8 transactions, 4.9.9 lock-free linked list). Substance unchanged. **4.9.9 still shows only `push_front()`** (Herb Sutter CppCon 2014) -- no pop/dequeue. Our "book shows push, not pop" note holds.
- *New relevant addition:* UB / strict-aliasing / type-punning (§3.3) -- see Ch 3 block below. C++-standards survey through **C++23** (§3.1.2) is C++-specific; orthogonal to C23.

---

## Contents

1. [Ch 1.6 -- Runtime Engine Architecture](#ch-16)
2. [Ch 3.3-3.5 -- Hardware, Memory Layout & Caches](#ch-3)
3. [Ch 4 -- Parallelism & Concurrent Programming](#ch-4)  ← core
4. [Ch 5 -- 3D Math for Games](#ch-5)
5. [Ch 6 -- Engine Support Systems](#ch-6)  ← core
6. [Ch 7 -- Resources & the File System](#ch-7)  ← core (asset/filesystem)
7. [Ch 8 -- The Game Loop & Real-Time Simulation](#ch-8)  ← core
8. [Ch 10 -- Logging & Memory Tooling](#ch-10)
9. [Ch 11.1-11.2 -- The Rendering Engine](#ch-11)
10. [Ch 15-16 -- Gameplay Foundation & Event Pipelining](#ch-16-foundation)  ← core (events)
11. [Deferred topics](#deferred)
12. [Action items](#actions)

---

<a name="ch-16"></a>
## Ch 1.6 -- Runtime Engine Architecture (pp. 38-58)

**Book.** Engines are **layers**; upper depends on lower, never reverse; **avoid circular dependencies** (kills testability/reuse, p. 38). Canonical stack (Fig 1.16): hardware → drivers → OS → 3rd-party SDKs → **platform independence layer** → **core systems** (assertions, custom allocators, math, hand-coded containers "to minimize or eliminate dynamic memory allocation," pp. 43-45) → **resource manager** → **rendering** → collision/physics → animation → HID → audio → **gameplay foundation** (GO model, events, scripting, world streaming) → game-specific. Wrap platform APIs for: consistent cross-platform behaviour; insulation from dependency swaps (p. 43). Vulkan (p. 42): fine-grained control over CPU/GPU shared memory and resources.

**Anoptic.** Matches our roadmap: `ano_*()` platform layer ([include/anoptic_memory.h](../../include/anoptic_memory.h), `anoptic_time.h`, `anoptic_threads.h`, `anoptic_filesystem.h`) = platform independence; core = logger, strings, lock-free collections, data structures; gameplay foundation = event bus, game loop, future ECS/scoped-resolution.

**Verdict.**
- ✅ Strict no-cycles layering and "wrap the platform" match exactly.
- ✅ "Hand-code containers to minimize dynamic allocation" (p. 45) = arena thesis in the book's voice.
- ➕ Completeness checklist of eventual subsystems. Treat OOP "game object model pervading the entire engine" (p. 56) as what we replace with ECS -- see [Ch 16](#ch-16-foundation).

---

<a name="ch-3"></a>
## Ch 3.3-3.5 -- Hardware, Memory Layout & Caches (pp. 131-201)

Hardware basis for DOD and cache-line-striped lock-free design.

**Book -- data layout (§3.3, pp. 159-164).**
- Every type has **natural alignment** = its width; misalignment → **two** reads + mask/shift/OR; some CPUs fault (Fig 3.14, pp. 159-160). SIMD 4-float vectors: 128-bit → **16-byte aligned**.
- Compilers insert **padding** between mixed-width members; reorder large→small to reclaim; struct padded to multiple of largest member alignment so *arrays* stay aligned (pp. 160-161). Explicit `_pad[]` makes waste visible.
- C++ class with virtuals: **vtable pointer** (4 B / 8 B) per instance (pp. 161-164). Array of objects: bloated stride; every virtual call = pointer chase.

**Book -- memory gap & caches (§3.4-3.5, pp. 188-198).**
- Register op **1-10 cycles**; **main-RAM ~500 cycles** (pp. 189-190). Coping: faster near-core memory; *hide* latency; **lay data out to minimize main-memory accesses** (strategy 3 = DOD).
- Caches move **cache lines** (64-128 B); **spatial** + **temporal locality** (pp. 191-192). Hit ≈ tens of cycles, miss ≈ hundreds.
- **Write-back**: mark line **dirty**, flush on eviction (p. 196). Coherency: **MESI** / **MOESI** / **MESIF** (pp. 196-197). Deep MESI → §4.9.
- **Best D-cache miss avoidance (p. 197):** contiguous blocks, as small as possible, accessed sequentially. Keep hot loops small (I-cache); no non-inlined calls in innermost loops.
- **TLB** (pp. 187-188): on-die virtual→physical page cache; miss expensive. Typical page = 4-8 KiB.

**Anoptic.**
- DOD: contiguous POD component arrays, sequential scan. §3.5.4.9 states this directly.
- **1 GiB hugepages** (`mi_reserve_huge_os_pages_at`): §3.5.2 -- fewer larger pages → fewer TLB entries. notes.md ("1 GiB at 4 KiB = 262,144 TLB entries → one with 1 GiB pages") matches Gregory.
- **Cache-line striping:** §3.5.4 + §3.5.4.8 -- 64-byte line = coherency unit; write-back/dirty/exclusive-ownership is the substrate. Book: coherency as **cost**. Anoptic: **mechanism** -- transfer ownership at line granularity (MESI exclusivity per line).

**Book -- undefined behavior & type punning (§3.3, *new in 4th ed.*).** Pointer-cast reinterpret + deref (**type punning**) breaks under **strict aliasing** (default at `-O2`) → **UB**: optimizer may reorder/fold/delete. Example: endian-swap `float` as `u32`. Warning: **even a `union` is UB for punning in C++**; safe path: **`memcpy` into a fresh object** (elided), or **C++20 `std::bit_cast`**.

**Anoptic (C23 nuance -- sharpest C++→C divergence).** "Unions aren't safe" is **C++-only**. In C, **union type-punning well-defined since C99** (§6.5.2.3). C23: three legal puns vs C++'s one: (1) `memcpy` (portable, elided), (2) **`union`** (legal C, illegal C++), (3) **`char*` / `unsigned char*`** (always aliases). Clang: `__builtin_bit_cast`. Hot spots: **glTF byte-buffer reads** in [ano_GltfParser.c](../../src/render/gltf/ano_GltfParser.c), **arena reinterprets of raw bytes as typed objects** (planned **load-in-place**, action item 7), endian/format conversion. Bare pointer-cast pun = latent miscompile at high `-O`.

**Verdict.**
- ✅ DOD, contiguous SoA arrays, hugepages grounded here.
- ➕ Decide aliasing policy *before* load-in-place: prefer `memcpy`/`union` (free in C), or `-fno-strict-aliasing` on asset/serialization TUs. Never bare pointer-cast pun.
- ✅ No vtable in POD components: supported by vtable-pointer/pointer-chase discussion.
- ➕ Cache-line-stripe is *consistent* with the hardware model; Gregory never aligns algorithms to the coherency unit -- our contribution. Cite §3.5.4.8 + §4.9.4.2. Watch **false sharing**: 64-byte-align each stripe. Direct-mapped "ping-pong eviction" (p. 194) = single-core cousin of multi-core line-bounce.

---

<a name="ch-4"></a>
## Ch 4 -- Parallelism & Concurrent Programming (pp. 203-358) -- CORE

New-in-3rd-ed chapter; most important for us. Logger, lock-free collections, job system, main loop live here.

**Book -- definitions (§4.1-4.5).** Concurrency = "composition of independently executing computations" (Rob Pike, p. 257); concurrent iff **multiple readers/writers of shared data** (p. 204) -- central problem: **data races**. **Task** parallelism = different ops in parallel; **data** parallelism = one op over many elements (p. 207). Mutex lock/unlock ≈ **>1000 clock cycles** (p. 267) -- lock-free motivation. **Atomic** = critical op whose invocation/response can't interleave with another on the *same* object (pp. 262-265).

**Book -- sync primitives (§4.6, pp. 267-281).** Mutex (only locker unlocks); **Windows critical section / Linux futex** = cheap mutex, brief spin then sleep; **condition variable** = queue of sleeping threads (re-check predicate in `while` -- **spurious wakeups**); **semaphore** = atomic counter; *binary semaphore ≠ mutex* (different thread may signal). All kernel-backed, **expensive**.

**Book -- why locks hurt (§4.7-4.8, pp. 281-289).** Deadlock: four **Coffman conditions**; defenses: **global lock-ordering**, fewer locks. Also livelock, starvation, **priority inversion**. Rules: (1) **doubly-linked list is not concurrent** -- prefer **singly-linked append-only** or **stable deterministic ordering** (pp. 286-287); (2) **don't make everything thread-safe** -- "striving toward lock-freedom beats over-using locks"; give each thread a **private repository**, collate later (pp. 287-289).

**Book -- lock-free (§4.9, pp. 289-330) -- centerpiece.**
- "Lock-free" ≠ "no mutexes"; means **never blocking/sleeping on a resource** (p. 289). Progress: **blocking → obstruction-free → lock-free** (some thread always progresses; individuals *can* starve) **→ wait-free** (lock-free + starvation-free). Umbrella: **non-blocking** (pp. 289-290).
- Race bug causes: op interleaving, **compiler/CPU reordering**, **hardware memory ordering** (pp. 291-292). "Preserves single-threaded behaviour" insufficient.
- **`volatile` does NOT help in C/C++** (pp. 302-303): forces re-reads only; no CPU reordering or cache coherency fix. (Java/C# `volatile` differs.) **Compiler barrier** (`asm volatile("" ::: "memory")`) stops compiler reorder, not CPU -- **LTO can defeat function-call-as-barrier** (p. 304).
- **Atomic RMW**: TAS, exchange, **CAS** (write iff `*p == expected`). **ABA** (p. 297): A→B→A fools CAS -- defeat with sequence/generation counters or **LL/SC** (pp. 297-299).
- **Memory ordering (pp. 304-314):** MESI walk-through; coherency opts can reverse write order across cores. Fix: **fences**; **acquire/release**: **write-release** (producers) -- no prior r/w after; **read-acquire** (consumers) -- no later r/w before. x86 **strongly ordered**; Alpha / PowerPC / ARM weak. ARM: `ldar`/`stlr`.
- **C++11 atomics → C11/C23 (pp. 314-317):** `std::atomic<T>` → `_Atomic T`; default **seq_cst**; `std::memory_order_*` → C11 `memory_order_relaxed / acquire / release / acq_rel / seq_cst`. 32/64-bit lock-free; larger → mutex (`atomic_is_lock_free`). **80/20:** drop below seq_cst only when profiled; **check disassembly**.
- **Lock-free transaction (pp. 327-330):** work in **thread-private** memory; **single atomic CAS/LL-SC to publish**; on failure retry (failure = another thread succeeded → progress). Example: `push_front` via `head.compare_exchange_weak(node->next, node)`. **Book shows only push** -- pop is where ABA bites.
- **Lock-not-needed assertions (pp. 325-327):** single-threaded early-frame + single-threaded late-frame → **no lock**; assert with `BEGIN/END_ASSERT_LOCK_NOT_NECESSARY` (Naughty Dog shipped this).

**Book -- SIMD & GPGPU (§4.10-4.11, pp. 331-357).** SSE = 4 floats/128-bit (16-byte aligned), AVX = 8/256, AVX-512 = 16/512; `#include <x86intrin.h>` on Clang. **Scalar single-lane first, then widen** -- same source scales SSE→AVX→AVX-512. **Avoid horizontal adds**; transpose + component-wise multiply (~3.5×). No per-lane `if` -- compute both, **select via comparison mask**. GPGPU: data-parallel coprocessor; **SIMT** = SIMD + wavefront/warp scheduling.

**Anoptic.**
- **Logger:** hot path (`atomic_fetch_add` reserve, write payload, release-store commit header; consumer acquire-loads) = §4.9 transaction + acquire/release. Gap problem (notes.md) = §4.8 ordering; per-slot commit header = standard fix.
- **Lock-free collections:** §4.9.6 = M&S/`push_front` baseline; bounded-MPMC Vyukov = §4.8 deterministic ordering + sequence counters (ABA). Cache-line stripes amortize §4.9.4 coherency cost.
- **Job system:** "private repository, collate later" (p. 288) = per-thread frame arenas. "No mutexes outside Vulkan" = spin-locks-over-OS-mutexes taken to the end.

**Verdict.**
- ✅ Acquire/release commit-header MPSC, transaction/retry, sequence counters for ABA, private-then-collate, lock-not-needed assertions all endorsed. Adopt `BEGIN/END_ASSERT_LOCK_NOT_NECESSARY`.
- ⚠️ **Tension:** Gregory cautions lock-free is hard; restrict to most critical subsystems; spin locks elsewhere (pp. 289, 555). Anoptic's "no mutexes but Vulkan" is *more* aggressive. Every lock-free structure must clear ABA + ordering + linearizability. Build M&S/Vyukov baselines first (notes.md Phase A); benchmark before cache-line-stripe (Phase B).
- ⚠️ **Don't use `volatile` for sync** -- `_Atomic` on logger `tail_index` is correct; plain `volatile` is a bug. Beware LTO defeating function-call barriers.
- ➕ Book stops at lock-free `push`; our dequeue + stripe-publication ordering go beyond -- cite Vyukov and McKenney.
- ➕ SIMD "scalar-first, then widen" for eventual component kernels; 16-byte SSE alignment dovetails §3.3 + arena alignment.

---

<a name="ch-5"></a>
## Ch 5 -- 3D Math for Games (pp. 359-414)

Medium priority -- whether/how to grow ad-hoc math in [vertex.c](../../src/vulkan_backend/vertex/vertex.c) into a real library.

**Book.**
- **Handedness is visualization only** -- math identical; RH↔LH by flipping one axis (pp. 361-362). Graphics: typically LH, y-up, +z into screen.
- **Squared magnitude to avoid `sqrt`** when comparing lengths (pp. 366-367). "Normal" ≠ "normalized".
- **CONVENTION GOTCHA (§5.3.2, pp. 377-378):** Gregory uses **row vectors** (`v' = vM`, left-to-right). **Column-vector** (`v' = Mv`) requires **transposing every matrix in the book**. OpenGL/GLSL and most C math libs: **column-major / column-vector**.
- Homogeneous: **points w=1, directions w=0** (pp. 379-381). Pure-rotation matrices **orthonormal** → inverse = transpose. Transform **normals** with **inverse-transpose** of the 3×3 (p. 392).
- **Storage (§5.3.12):** row-contiguous matches row-vector math; **column-strided sometimes required for fast SIMD** matrix×vector. Detect convention: translation matrix t=(4,3,2), see where values land.
- **Quaternions (§5.4):** 4 floats, 3 DOF, no gimbal lock; LERP/SLERP; unit quats **q⁻¹ = q\***. Gregory **[x y z w]** -- papers often **[w x y z]**; check before porting. SLERP vs LERP: **profile**.
- **Useful objects (§5.6):** sphere and plane = **4-float / 128-bit SIMD**; **frustum = 6 planes**; project to homogeneous **clip space** → frustum = AABB.
- **RNG (§5.7):** Mersenne Twister/SFMT, PCG, or KISS99 over `rand()`.

**Anoptic.** Hand-rolled `Vector2/3/4`, `mat4`, `lookAt`, `perspective`, `multiplyMat4`, `extractFrustumPlanes`. Frustum extraction (§5.6.6) + planned compute-cull want **clip-space-AABB**. Million-entity distance comparisons (scoped-resolution LOD): squared-magnitude everywhere.

**Verdict.**
- ⚠️ **Pin one matrix convention at top of `vertex.h`.** Target **Vulkan/GLSL = column-major** = *transpose* of every Gregory formula -- likeliest silent transform bugs. Also: Vulkan flips clip-space Y; z∈[0,1] vs GL [-1,1].
- ✅ Squared-magnitude, inverse-transpose normals, 4-float SIMD plane/sphere, 6-plane frustum match needs.
- ➕ Entity rotation: prefer **quaternions** (4 floats, SIMD-packable) over 3×3/4×4 per entity.
- ➕ Real math lib when a second consumer appears (physics, ECS transforms); until then renderer-local is fine. When built: **column-strided** for SIMD matrix paths (§5.3.12).

---

<a name="ch-6"></a>
## Ch 6 -- Engine Support Systems (pp. 417-479) -- CORE

Validates arena thesis; specs strings and containers.

**Book -- start-up/shut-down (§6.1, pp. 417-426).** C++ static-init order undefined; destructors unordered after `main()`. Pattern: explicit `startUp()`/`shutDown()`; constructors/destructors do nothing; call from `main()` in dependency order (Memory → FileSystem → …; reverse on shut). Naughty Dog `BigInit()`; **avoid dynamic allocation wherever possible**.

**Book -- memory management (§6.2, pp. 426-441) -- the core.** Levers: dynamic alloc **very slow** (`malloc`/`new` often kernel context-switch); **access patterns dominate**. Rule: **never allocate from the heap in a tight loop** (p. 427). Custom allocators: preallocated block + usage assumptions:
- **Stack allocator** (pp. 427-429): bump top; free reverse via `getMarker()`/`freeToMarker()`. **Double-ended**: two stacks in one block (*Hydro Thunder*: level-load bottom, per-frame top, **never fragmented**, pp. 429-430).
- **Pool allocator** (pp. 430-431): fixed-size; O(1) via free list ("next" inside free block, or index).
- **Single-frame & double-buffered** (pp. 434-437): stack `clear()`ed every frame (*never cache pointer across frame*); double-buffered ping-pong for **async/multicore job results**.
- **Aligned allocation** (pp. 431-434): over-allocate, round up, store shift below returned pointer.
- **Fragmentation** (pp. 437-441): mixed alloc/free → holes; fail despite free bytes. **Stack and pool immune.** General heaps: **defragment by relocation** → needs **handles** (or smart pointers). Amortize: relocate N blocks/frame.

**Book -- containers (§6.3, pp. 441-456).** Prefer **contiguous arrays** over linked lists. Grow by doubling; **if high-water mark known, preallocate**. STL: generic = slower/bigger, **hidden dynamic allocation**, allocator model **can't express stack allocators** -- Naughty Dog **prohibits STL in runtime game code**. Dictionaries: **closed (open-addressing) hash table** = fixed memory, no dynamic alloc; **prime size + quadratic probing** (or Robin Hood). Hashes: xxHash, MurmurHash3, CityHash.

**Book -- strings (§6.4, pp. 456-470).** Expensive: `strcmp` O(n), `strcpy` copies/allocates; Gregory profiled **`strcmp`/`strcpy` as top two** (p. 457). `std::string`: pass by reference; know own-vs-ref and COW. Big idea: **hashed string ids** (`FName`) -- hash to int, compare as ints, originals in global table. **Intern** once, cache. Naughty Dog: compile-time hashing (`"foo"_sid`), **64-bit hashes**. **UTF-8** (pp. 462-466): ASCII-compatible; ND uses **8-bit `char` + UTF-8**. Define own character/string type (`wchar_t` size is platform-dependent).

**Book -- configuration (§6.5, pp. 470-479).** Persist to **text config** (INI/JSON). Quake **cvars**. Naughty Dog: menu items bound to global addresses; Scheme-like DDL auto-generates C struct headers; lookup by `SID(...)`.

**Anoptic.**
- Arena hierarchy (process → level → frame → scratch → pool) **is** §6.2 catalogue: level/session = stack+markers; frame = single-frame; scratch = stack; pool = §6.2.1.2; double-buffered = job-result buffers. `LOCALHEAPATTR` = C "destroy region at scope exit." Hugepages back process arena.
- Owned string `{char* ptr, uint32_t len, uint32_t capacity}` = owns-memory + length; copy-on-slice ≈ COW/string_ref; UTF-8-transparent matches recommendation.
- Containers: stb_ds stopgap = "dynamic arrays for development, fixed-size once budgets known."

**Verdict.**
- ✅✅ §6.2 strongest external validation of memory architecture. *Hydro Thunder* double-ended stack = our level-vs-frame split. Stack/pool fragmentation-immunity = why we refuse a general heap.
- ✅ Start-up/shut-down: in **C this is free** -- no constructors to misorder. Modules use `ano_*_init()`/`ano_*_cleanup()` (e.g. [anoptic_log.h](../../include/anoptic_log.h)). Keep `main()` calling in explicit dependency order.
- ✅ Owned-string + length + UTF-8 transparency confirmed; `strcmp`/`strcpy` profiling = empirical case.
- ➕ **Adopt hashed string ids early.** Natural id for ECS type names, event types ([Ch 16](#ch-16-foundation)), resource GUIDs ([Ch 7](#ch-7)), config keys. C23 `constexpr` hashing ≈ `"name"_sid` without UDLs. **64-bit** from the start. One primitive, four subsystems.
- ➕ **Handles for relocatable data.** If level/pool ever defragment: §6.2.2.2 -- only handles survive. Also ECS entity-handle pattern ([Ch 16](#ch-16-foundation)). Cross-arena refs = handles.
- ⚠️ Confirm containers avoid STL-style hidden allocation; closed/open-addressing hash (fixed memory, prime + quadratic) if we outgrow stb_ds.

---

<a name="ch-7"></a>
## Ch 7 -- Resources & the File System (pp. 481-523) -- CORE (asset/filesystem)

Biggest *gap*: we parse glTF JSON at runtime with loose `malloc`/`free`; book wants offline-baked, load-in-place binary behind a resource manager.

**Book -- file system (§7.1, pp. 482-492).** Wrap native FS: cross-platform consistency; OS lacks engine needs -- chiefly **streaming** (p. 482). Path handling beyond string ops -- **stripped-down path API** (p. 486). **Avoid runtime path searching** -- paths knowable a priori (p. 485). C stdlib: buffered (`fopen`) and unbuffered (`open`); both **synchronous**. **Async I/O / streaming (§7.1.3, pp. 489-492):** `asyncReadFile()` returns immediately; **callback** or **wait later** (`asyncWait`). Requests carry **priorities and deadlines**. Impl: **I/O thread** + **queue** + blocking reads + **semaphore** completion -- "nearly any sync op becomes async on a separate thread."

**Book -- resource manager (§7.2, pp. 493-523).** Two halves: **offline tool chain** + **runtime manager**. Runtime (pp. 503-504): **one copy** per unique resource; lifetimes; **composite resources**; **referential integrity**; memory placement; **post-load init**.
- **ACP (pp. 501-503):** exporters → compilers → linkers. Interdependencies dictate build order.
- **Resource GUIDs (pp. 507-508):** globally unique; commonly **file path** (string or 32-bit hash).
- **Registry (pp. 508-509):** GUID→pointer, single-copy. Auto-load on demand → **hitches**; prefer load behind screen or **async streaming**.
- **Lifetime (pp. 509-511):** global / level / shorter / live-streamed. Shared across levels → **reference counting**.
- **Memory layout (pp. 511-516):** stack (no frag if level fits), double-ended, or **equal-sized chunk pool** for streaming (ND: 512 KiB PS3, 1 MiB PS4). **Sectioned files**: main-RAM / video-RAM / temporary-load-time / debug-only.
- **Cross-refs & load-in-place (pp. 516-521):** (1) store GUIDs, convert via lookup after load; (2) **pointer fix-up tables** -- serialize contiguous, pointers → **file offsets**, on load `pointer = baseAddress + offset`. **C++ needs placement-new; PODS avoid this entirely.**
- **Post-load init (pp. 521-523):** runtime-only setup (e.g. upload to VRAM). In **C**: lookup table type → {init, teardown} fn ptrs.

**Anoptic.**
- `ano_fs_gamepath`/`ano_fs_userpath` = §7.1.1 path API. **No async I/O yet** -- §7.1.3 thread+queue+semaphore+callback is the blueprint; early consumer of lock-free queue + `anoptic_threads`.
- glTF path ([ano_GltfParser.c](../../src/render/gltf/ano_GltfParser.c), cgltf, scratch-heap staging) = runtime text-parse the book argues against. Scratch staging = **temporary load-time section** (§7.2.2); loose `malloc`/`free` (notes.md debt) = what §7.2 pushes offline.

**Verdict.**
- ✅ Wrap FS + stripped-down path API match `ano_fs`.
- ✅ **C23 load-in-place PODS is free** -- no placement-new. Pointer fix-up + arenas: load image into one arena block, fix up, done. C-over-C++ advantage.
- ⚠️ **Console seek-time largely moot on SSD/desktop** (book notes SSDs, p. 505) -- other bake reasons hold: **no per-file open cost, sequential bulk reads, zero runtime parse**. Don't cargo-cult optical-disc layout; do adopt offline conditioning + load-in-place.
- ➕ **Resource manager is an architectural hole.** Before glTF grows: (1) **registry** (GUID→pointer; GUID = hashed string id from [Ch 6](#ch-6)); (2) **refcounted lifetimes** on level/session arena; (3) **offline bake** glTF → load-in-place binary (no runtime jsmn); (4) **handles** for inter-resource refs. Largest "book has it, we don't" item.
- ➕ **Async streaming** (§7.1.3) enables hitch-free scoped-resolution catch-up. Build thread+queue+callback async-I/O on lock-free queue.

---

<a name="ch-8"></a>
## Ch 8 -- The Game Loop & Real-Time Simulation (pp. 525-558) -- CORE

Specs timing module and main loop; frames multi-core direction.

**Book -- loop & timelines (§8.1-8.4).** Subsystems at **different rates** -- animation 30/60 Hz, physics often 120, AI 1-2 Hz (pp. 526-527). Loop styles: **Windows message pump**, **callback frameworks**, **event-based updating**. **Timeline** = clock variable; **game time can diverge from real time** -- pause = halt game clock, render/debug fly-cam on separate clock; single-step = advance one frame interval (pp. 532-534).

**Book -- measuring time (§8.5, pp. 534-544) -- central.**
- Move by `Δx = v·Δt` (explicit Euler); perceived speed depends on Δt (pp. 535-538).
- **Don't reuse last frame's Δt as next estimate** -- spike breaks it. **Spiral of death:** long frame → double physics step → worse next frame (pp. 536-537). Mitigate: **running average** of Δt; **frame-rate governing** (sleep to target interval); design for arbitrary Δt. Consistent frame time helps integrators, tearing, **record-and-playback determinism**.
- **V-sync** = wait for vertical blank; a form of governing.
- **High-res timer (§8.5.3):** cycle-counting register; `QueryPerformanceCounter`/`Frequency` on Win32, `rdtsc`, PowerPC `mftb`. 64-bit wraps ~195 years (32-bit ~1.4 s). **Per-core timers can drift** -- absolute cross-core compare can yield negative deltas (§8.5.3.1).
- **Clock variables (§8.5.4):** prefer **64-bit integer cycle clock**; for 32-bit/float: **subtract 64-bit timestamps first, then convert**. **Float clocks lose precision over time** (23-bit mantissa) -- short deltas only.
- **Breakpoints (§8.5.5):** real-time clock keeps running; on resume Δt can be minutes. **Clamp Δt** to target (e.g. 1/30 s) when frame time exceeds ceiling (~1 s).

**Book -- multiprocessor loops (§8.6, pp. 544-558).** One-thread-per-subsystem **doesn't work**. Better: **scatter/gather** (batch per core, **separate preallocated per-thread output buffers**, then join). Spawning expensive -- **pre-spawned thread pool** → **job system** (§8.6.4): fine-grained **independent jobs** on core-pinned workers. Job = fn ptr + `uintptr_t` param (+ priority + `Counter*`). Pool jobs **must run to completion** (shared call stack); mid-execution sleep → **coroutines or fibers**. Join via **counter** (semaphore in reverse). **Spin locks inside jobs** -- OS mutex sleeps the worker and can deadlock the pool.

**Anoptic.**
- `ano_timestamp_raw`, `ano_busywait`, `ano_sleep` = §8.5.3 high-res + §8.5.2.4 governor. QPC→ns conversion (notes.md) = §8.5.4 subtract-64-bit-before-scale.
- **Windows hi-res:** `Sleep()` 15.6 ms jitter = why §8.5 insists on high-res + Δt discipline; `timeBeginPeriod` + waitable-timer + spin-tail = sub-ms governor.
- **Main loop** + **job system:** §8.6 blueprint -- per-thread frame arenas = separate output buffers; `uintptr_t` + counter API is C-friendly.
- **Scoped resolution** = §8.2 different rates applied to star systems. Deterministic catch-up = §8.5.2.4 record-and-playback (fixed step + seed) generalized.

**Verdict.**
- ✅ High-res timer, governor, Δt-clamp, 64-bit-subtract-before-convert validate timing module. Cross-core drift (§8.5.3.1) worth a note in `anoptic_time`.
- ✅ Job-system design (pool, `uintptr_t` param, counter join, spin-locks) matches Anoptic.
- ⚠️ **Build deterministic fixed tick deliberately.** Book gives constraints (spiral of death, constant-rate integrators, Δt clamp) but **no explicit fixed-timestep-with-accumulator + interpolation pattern**. Implement: fixed sim dt, accumulate real Δt, N fixed sub-steps, clamp accumulator, interpolate render. Cite Gaffer-on-Games "Fix Your Timestep" + Gregory's constraints.
- ➕ **Fibers/coroutines for jobs** (§8.6.4): blocking-style code without stalling a core. Overkill for v0.1; long-term target for sim + async streaming.

---

<a name="ch-10"></a>
## Ch 10 -- Logging & Memory Tooling (§10.1 pp. 589-594, §10.9 pp. 615-618)

**Book -- logging (§10.1).** `printf` debugging valuable for real-time timing bugs breakpoints miss (p. 590). `va_list` core (`VDebugPrintF`) so other prints layer on it (pp. 590-591). **Verbosity levels** (global threshold) and **channels** (per-subsystem; ≤32/64 → **bitmask** filter) (pp. 591-593). **Mirror all output to a log file** regardless of filters; **flush** after each call only if logging little or proven necessary (pp. 593-594). **Crash reports** (§10.1.5): top-level exception handler dumps level, player position, **stack trace**, **allocator state**.

**Book -- memory stats (§10.9).** Wrapping `malloc`/`free` insufficient: can't see third-party, VRAM, or **custom allocators** (ND has six). Track **inside each allocator's block**. Report **high-water marks** per subsystem; on-screen cues (missing model → red text; missing texture → ugly pink).

**Anoptic.**
- Logger: 5 levels = §10.1.2 verbosity; FATAL immediate mode = flush-so-crash-log-isn't-missing advice; `ano_log_output_dir` + flusher thread = §10.1.4 file mirroring (not yet wired).
- Lock-free MPSC hot path is *our* addition (book logger is single-threaded).

**Verdict.**
- ✅ Levels, file mirroring, FATAL-flushes-immediately confirmed. `va_list` core/wrapper = good C practice -- build `ano_log_*` on one `va_list` fn.
- ➕ **Channels as 64-bit bitmask** (render/memory/sim/io/…). One int filters; pairs with hashed string ids. Cheap; logger in flight now.
- ➕ **Memory instrumentation arena-aware.** §10.9: track *inside* each custom allocator -- per-arena high-water marks (process/level/frame/scratch/pool). mimalloc stats + our counters. Fold allocator state into FATAL path.

---

<a name="ch-11"></a>
## Ch 11.1-11.2 -- The Rendering Engine (pp. 622-697)

Medium priority, renderer rewrite. Vulkan direct, **skip PBR**; relevant: pipeline architecture + **high-entity-count** techniques.

**Book.**
- **Indexed triangle lists** (vertex + 16-bit index); offline **vertex-cache optimizers** (pp. 628-630).
- **Mesh instancing** (pp. 631-632): shared mesh + per-instance **model-to-world**. Pairing that matters: mesh-instance + indexed/indirect draws + material-sorted batching.
- **Transform chain** (pp. 656-663): model→world → view → **clip** → screen. Clip z **[0,1] DirectX/Vulkan**, [-1,1] GL.
- **Materials & submeshes** (p. 646): material = textures + shaders + render-state; model → **submeshes** (one material each); mesh-material = "render packet."
- **Buffering** (pp. 663-664): **double** avoids tearing; **triple** starts next frame instead of idle.
- **Depth/z-buffer** solves occlusion; 1/z precision near camera → **z-fighting** far (w-buffering). Back-face cull by winding.
- **Pipeline** (pp. 667-677): tools → ACP → **application stage (CPU:** visibility + geometry submit + render-state**)** → geometry (GPU: VS, optional GS, clip) → raster (fragments, **early-z**, PS, ROP/blend). Compute cull = GPGPU (p. 673).
- **Culling** (pp. 687-690): **frustum cull** bounding sphere vs 6 planes; **occlusion**; **PVS**; **portals** / **antiportals**.
- **Spatial subdivision** (pp. 693-696): quadtree/octree/BSP/kd-tree/sphere hierarchy; discard off-screen in O(log n).
- **Render queue & state minimization** (pp. 691-693): render state **global** -- change flushes GPU; **sort by material**. Per-draw CPU cost can dominate → manual command lists or **Vulkan (named, p. 692)**. **z-prepass** reconciles material-sort with early-z.

**Anoptic.** Geometry pool (vertex/index mega-buffers), bindless textures, planned compute-cull, indirect draws -- independently at the book's high-entity-count stack.

**Verdict.**
- ✅ Vulkan-direct + manual command lists = book's recommendation (p. 692). Geometry pool, bindless, indirect, compute-cull = correct million-entity techniques.
- ✅ Frustum-cull-by-bounding-sphere-vs-6-planes = planned compute-cull kernel; dovetails [Ch 5](#ch-5) clip-space-AABB.
- ➕ **Mesh LOD chains** (pp. 625-627) = render-side scoped resolution. LOD selector reads same distance metric as sim LOD.
- ➕ **Render-queue sort key** (material/shader/depth) + optional **z-prepass** in renderer rewrite.
- ⚠️ Skipping PBR consistent with book treating advanced lighting separately (§11.3, **not distilled**). Keep architecture (materials, submeshes, packets, VS/FS stages) even if materials stay thin. Note Vulkan clip-space (z∈[0,1], flipped Y) vs book's GL-leaning examples.

---

<a name="ch-16-foundation"></a>
## Ch 15-16 -- Gameplay Foundation & Event Pipelining (pp. 1015-1158) -- CORE (events)

ECS rationale + event-bus blueprint. §16.8 elevated (event pipelining); most detailed section below.

**Book -- game worlds & objects (Ch 15, pp. 1015-1025).** World = **static** + **dynamic**. Dynamic = **game objects** = **attributes** + **behaviors**. **Type** ≠ **instance**. Tool-side model need not match runtime. Data-driven = power + tooling cost -- KISS.

**Book -- runtime object models (§16.2, pp. 1043-1062) -- the ECS chapter.** Two styles:
- **Object-centric:** GO = class instance; monolithic inheritance (Unreal `Actor`) problems (pp. 1046-1051): deep hierarchies; tree classifies on **one axis**; MI deadly diamond; **"bubble-up"** to root. Fix: **composition over inheritance** (pp. 1051-1055): hub owning **components**.
- **Pure component (pp. 1056-1057):** strip behavior until GO = **unique id**; eliminate hub -- **components share id, looked up by it.** Entity-as-pure-id ECS. Open problem: **inter-component communication**.
- **Property-centric (pp. 1043, 1057-1061):** GO = unique id; properties in **tables per type, keyed by id** -- relational. Pros: **memory-efficient**, data-driven, **cache-friendly contiguous same-type = SoA** (AoS-vs-SoA code; PS3 cache miss ≈ thousands of instructions, pp. 1060-1061). Cons: hard to enforce property relationships; harder to debug.

**Book -- references & queries (§16.5, pp. 1079-1086).** Raw **pointers** fast but stale/dangling/relocation. **Handles** (recommended): **integer index into global handle table**; on delete null the slot; unique id in handle validates reuse; **survive relocation**. World **queries** via **specialized accelerators** -- hash by id, pre-sorted lists, collision casts, **spatial hash / grid / quadtree / octree / kd-tree**.

**Book -- updating objects (§16.6, pp. 1086-1101).** Naive "iterate all, virtual `Update(dt)`" = **anti-pattern** (pp. 1088-1090). Use **batched updates**: loop drives each subsystem **once, big batch** -- contiguous data, maximal cache; objects only *manipulate* subsystem state = **ECS systems iterating tight loops.** Dependencies: **phased updates** (multiple per-frame hooks) and **bucketed updates** (dependency-tree tiers). **Consistency rule** (pp. 1098-1100): consistent before/after update loop, **inconsistent during** -- mid-loop peer query = **one-frame-off lag**. **State caching** (pp. 1100-1101): cache previous state for safe cross-reads -- "tied to **pure functional programming**."

**Book -- concurrency on object updates (§16.7, pp. 1101-1114).** GO models hard to parallelize. Kick subsystems as **many jobs** with **scatter/gather**; thread-safe interfaces. Think **asynchronous**: non-blocking request + later wait; tolerate **one-frame lag** -- *"the secret of optimized concurrent design is delay"* (Mike Acton). **DOP** = leaves in dependency graph; deps create **sync points**. Shipped solution (TLOU Remastered, Uncharted 4): **object snapshots** -- most interactions **read-only**; publish **read-only snapshot** at bucket start → concurrent lock-free reads. Writes: **minimize inter-object mutation**; **defer cross-object mutations to lock-protected request queue** after bucket.

**Book -- events & message-passing (§16.8, pp. 1114-1134) -- TOP PRIORITY.**
- Naive virtual `OnExplosion()` = **statically-typed late binding**, inflexible. Wanted: **dynamically-typed late binding** -- encapsulate call in an object (**Command pattern**).
- **Event = type + arguments:** `struct Event { EventType type; U32 numArgs; EventArg args[MAX]; }` (pp. 1116-1117). Payoffs: single `OnEvent` handler; **persistence** (queue/copy/broadcast); **blind forwarding**.
- **Event types** (pp. 1117-1118): global **enum** simple/fast but centralized, **order-dependent**. **Strings** flexible but slow. **Hashed string ids** practical (ND: event-type database + conflict detection).
- **Arguments** (pp. 1118-1120): tagged-union **`Variant`** in **fixed-size array** (no alloc) or dynamic. Prefer **key-value** over positional (avoid order dependency).
- **Distribution:** **chains of responsibility** (pp. 1121-1123) along relationship graph; handler returns consumed-or-pass; multicast via query. **Registering interest** (pp. 1123-1124): list per event type, or per-object bitmask, or restrict originating query -- **publish-subscribe**.
- **To queue or not (pp. 1124-1129):** queuing buys (1) **when** handled, (2) **future posting** (delivery time sort), (3) **prioritization**. Costs: complexity; **deep-copy** queued events; **dynamic allocation** → **pool allocator**; harder debugging; may need multiple dispatch points. **Immediate** sending: deep call stacks; handlers must be **re-entrant**.
- **Data-driven event systems (pp. 1131-1134):** designer responses → scripting → Blueprints / data-flow ports.

**Anoptic.**
- **ECS** = §16.2 pure-component / property-centric. SoA endorsement (pp. 1060-1061) = textbook backing. State-caching FP line (§16.6) ≈ notes.md "arenas are the FP memory model with the GC removed."
- **Entity handles** = §16.5 (id + generation) -- also relocation-safe refs from [Ch 6](#ch-6).
- **Event bus** = §16.8: Command-pattern events, hashed-string-id types, Variant key-value args, interest registration, future-stamped prioritized pool-allocated queue.
- **Scoped resolution / deterministic catch-up** = §16.6 bucketed updates at different rates + §16.8 future events + §16.7 snapshots.
- **Parallel million-entity tick** = §16.7 snapshots + deferred mutation queue + [Ch 8](#ch-8) job system.

**Verdict.**
- ✅✅ **ECS fully vindicated.** §16.2 walks inheritance → composition → pure-component/property-centric SoA = Anoptic's model. §16.2.4 problem list answers "why not Unreal hierarchy."
- ✅ **Event-bus design almost entirely pre-specified by §16.8.** Adopt: event = type+args **object**; **hashed-string-id** types (reuse [Ch 6](#ch-6) `_sid`); **Variant key-value** args; **interest registration**; **future-stamped + prioritized** queue; **pool/arena-allocated** fixed-size events. Deep-copy + pool cautions → allocate from frame/scratch arena.
- ➕ **Where Anoptic goes beyond.** Gregory's queue is **single-threaded**. notes.md proposes **two buses** -- lock-free monotonic (ordered/input) + **cache-line-striped** (high-throughput bulk). Book gives *semantics*; we supply *lock-free concurrent substrate*. §16.7 **snapshot** + **deferred mutation queue** = bridge; request queue *is* the bulk event bus.
- ⚠️ **One-frame-off-lag rule** (§16.6): consistency only before/after update. Snapshot/double-buffer must make read-vs-write phase explicit; [Ch 4](#ch-4) lock-not-needed assertions check it.
- ➕ **Spatial-index query accelerators** (§16.5) for colony-sim proximity -- grid/quadtree/octree over entities; same structure feeds GPU cull ([Ch 11](#ch-11)).
- ⚠️ KISS (§15.3): no Blueprints-style editor up front; start code/enum-of-`_sid` handlers; grow data-driven when designers need it.

---

<a name="deferred"></a>
## Deferred topics (noted)

Out of scope for current/near-future roadmap:

- **Ch 2 -- Tools of the Trade** (pp. 69-104): VCS, compilers/linkers, **profiling** (VTune, Valgrind), leak detection. When profiling/CI.
- **Ch 9 -- Human Interface Devices** (pp. 559-588): input abstraction, dead zones, debouncing, chords, remapping. **Touches input work** -- read §9.5 when wiring GLFW into the event bus.
- **Ch 11.3-11.4 -- Advanced Lighting, GI & VFX** (pp. 697-719): skipped -- Anoptic **non-PBR**. Revisit selectively for future SDF raymarching.
- **Ch 12 -- Animation Systems** (pp. 721-815): skeletons, clips, blending, skinning, state machines. Low relevance (no organic characters).
- **Ch 13 -- Collision & Rigid Body Dynamics** (pp. 817-910): if physics added, §13.3 / §13.4; event/job infra is prerequisite.
- **Ch 14 -- Audio** (pp. 911-1010): future; §14.5 when relevant.
- **Ch 16.9-16.10 -- Scripting & High-Level Flow** (pp. 1134-1158): scripting + objectives FSM -- far-future game layer.

---

<a name="actions"></a>
## Action items (book → roadmap)

Concrete book → roadmap deltas, roughly by payoff.

1. **Hashed string ids (`_sid`), 64-bit, compile-time.** Not yet in notes.md. Serves ECS type names, **event types**, **resource GUIDs**, **config keys**, **logger channel names**. C23 constexpr hashing ≈ Naughty Dog `"name"_sid`. *Refs: §6.4.3, §16.8.2, §7.2.3, §10.1.3.* **Highest payoff -- unblocks four subsystems.**
2. **Logger channels as 64-bit bitmask + arena-aware memory stats.** Per-subsystem channels; per-arena high-water marks; dump allocator state on FATAL. *Refs: §10.1.3, §10.9.* **Cheap; logger in flight now.**
3. **`BEGIN/END_ASSERT_LOCK_NOT_NECESSARY` macro.** Debug-build invariant for single-writer-phase assumptions. *Ref: §4.9.7.*
4. **Build classic lock-free baselines (M&S, Vyukov MPMC); benchmark before cache-line-stripe experiment.** Clear ABA + ordering + linearizability first. *Refs: §4.9, notes.md Phase A→B.*
5. **Pin matrix convention in `vertex.h`.** Document column-major / Vulkan clip space (z∈[0,1], flipped Y) -- book's row-vector formulas are the transpose. *Ref: §5.3.2, §5.3.12.*
6. **Event bus = §16.8 directly:** Command-pattern events, `_sid` types, Variant key-value args, interest-registration pub-sub, future-stamped + prioritized queue, **pool/arena-allocated** events. Layer **lock-free** substrate (and cache-line-stripe bulk bus) on top. *Refs: §16.8, notes.md event-bus work.*
7. **Resource manager (biggest gap):** **registry** (`_sid` GUID → pointer), **refcounted lifetimes** on level/session arena, **offline bake** glTF → **load-in-place** binary (kills runtime jsmn), **handles** for inter-resource refs. PODS + arenas make load-in-place free in C. *Refs: §7.2; notes.md glTF malloc/free debt.*
8. **Async I/O layer (thread + lock-free queue + completion callback/semaphore).** Prerequisite for hitch-free **scoped-resolution catch-up**. *Ref: §7.1.3.*
9. **Entity handles = id + generation, validated on deref.** Relocation-safe ECS reference; all cross-arena refs are handles. *Refs: §16.5.2, §6.2.2.2.*
10. **Main loop: fixed-timestep accumulator + render interpolation + Δt clamp.** Book gives constraints, not the explicit pattern -- implement the standard synthesis. *Refs: §8.5; Gaffer "Fix Your Timestep."*
11. **Renderer: render-queue sort key (material/shader/depth) + optional z-prepass + mesh LOD chains** tied to sim LOD distance metric. *Refs: §11.2.*
12. **Parallel tick (later): object snapshots + deferred mutation request queue + bucketed updates** on the job system. *Refs: §16.6-16.7, §8.6.*
13. **Settle type-punning / aliasing policy before load-in-place (item 7).** Standardize on `memcpy` or `union` (legal/free in C23), or `-fno-strict-aliasing` on asset/serialization TUs; ban bare pointer-cast puns. Audit glTF byte reads. *Ref: §3.3 (4th ed.); prerequisite for load-in-place.*

---

*Sources: Jason Gregory, Game Engine Architecture, **3rd ed.**, CRC Press / A K Peters, 2018 (ISBN 978-1-138-03545-4) -- spine of these notes; page citations are its printed pages. Cross-checked against **4th ed., Vol I: Foundations and Core Engine Systems**, CRC Press, 2026 (ISBN 978-1-032-44306-5) -- see Edition note up top. Distilled 2026-06, updated for 4th ed. 2026-06.*
