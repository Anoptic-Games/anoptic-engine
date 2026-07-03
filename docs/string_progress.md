# Strings — Design Progress and Synthesis

The composited record of everything in-tree on the string question. The conventions other languages converged on, the type theory underneath, the notes worth keeping from the literature, the competing designs on the table for Anoptic. The build sequence (`docs/notes.md`) specifies `ano_strings`. This is the design context behind it.

Composited from: `.claude/fable_stringtheory.md` (the string-theory design notes), `docs/data.md` ("Safety Through Geometry"), `docs/notes.md`'s owned-string design, `docs/TODO.md`, and `docs/references/game-engine-architecture.md` §6.4. A TODO section closes the file.

---

## 0. Status snapshot

- The strings work is the live branch (`feature-string-redux`). The string module sits in `src/strings/` with `include/anoptic_strings.h` as its contract (currently a stub on `main`). `src/strings/` is described as "owned-string-type work and scoped-heap experiments" (the `mem_chariot` tests in `ano_strings.c`).
- The 2024 spec survives on the `feature-strings` branch as `include/ano_strings.h`. It is reviewed below and flagged "load-bearing". Recover it during the strings work (it predates the `anoptic_core` split and is ~45 commits behind). House rule: archive-tag, never plain-delete.
- The byte half unblocks the engine; the meaning half (codepoints, graphemes, normalization) stays deferred until a glyph shaper forces it.

---

## 1. Why strings are the load-bearing problem

The string question blocked the whole engine, correctly so. Three framings, all the same fact:

- A string is the purest instance of *dynamically-sized bytes with unknown lifetime*, the universal container problem. Rust states it in its type system: `String` is `Vec<u8>` plus an invariant. Solve owned-growable-sliceable bytes and you have solved the dynamic array, the substrate under hash maps, queues, ECS columns, serialization buffers, log lines, asset paths, network packets. In most languages the string and the vector literally share an allocator policy, a growth policy, an ownership story. Get the ownership story wrong in the string and it metastasizes into every API that touches text, which is every API.
- The string question and the allocator question are the same question at two granularities. Memory itself is one big byte-string (the von Neumann machine is "an arbitrary amount of bytes under a grand series of transformations"). String ownership was undefinable before byte ownership (arenas, `mi_heap_t`) existed. That dependency is the field's actual dependency graph. The infrastructure exists now, so the strings work is no longer blocked.
- Strings are two hard problems stacked: **bytes** (allocation, lifetime) and **meaning** (codepoints, graphemes, normalization). The engine needs only the bytes half to unblock.

The historical record carries a body count, all of it evidence that string decisions are where allocation, encoding, ownership, and ergonomics collide first. NUL termination was a one-byte economy in 1972 that became "the most expensive one-byte mistake" (Kamp), half a century of buffer overflows. Java, Windows, and JavaScript pay a permanent UTF-16 tax for encoding too early. The largest schism in the most popular language's history, Python 2→3, was a fight about what a string is.

---

## 2. Conventions from other languages

### The fat-pointer convergence

The 2024 `anostr_t { char* buffer; size_t len; }` was drawn before the tour that would have shown everyone else drew the same thing:

| Language | View type | Shape |
|----------|-----------|-------|
| Rust     | `&str`        | (ptr, len) |
| Zig      | `[]const u8`  | (ptr, len) |
| Go       | `string`      | (ptr, len) |
| Anoptic  | `anostr_t`    | (ptr, len) |

The payoff of the fat pointer: O(1) length, no NUL-termination dependency, substring without copy, bounds-checkable iteration. The companion `anostr_utfhandle_t {int32 index; uint8 bytesize}` is an iterator cursor carrying `(index, bytesize)` so a codepoint is never re-decoded, exactly the shape of Rust's `char_indices()`.

### Rust, and the load-bearing insight

`String` is `Vec<u8>` plus an invariant. Bytes are validated as UTF-8 once, at construction. Slicing panics off char boundaries. Everything downstream trusts without re-checking. The value is in *where validation happens* ("nothing crazy in the assembly"): once at the boundary. This is the same conclusion as the notes' "UTF-8 is byte-transparent in storage, defer the rest": byte-transparent storage, validation as a separable layer on top.

### Five families of "who owns the callee's effects"

Every paradigm on the tour is one answer to a single question. Who owns the effects of a callee, allocation being the easiest effect to point at.

1. **Ownership transfer** — C++ RAII + move, Rust move + `Drop`. The callee constructs, the value moves, the cleanup obligation moves with it. C++ guaranteed copy elision means return-by-value compiles to the caller passing a hidden out-pointer (`sret`) and the callee constructing into the caller's frame. RVO is the calling convention. Rust tracks the same obligation statically. The borrow checker proves no one kept a pointer.
2. **Region polymorphism** — Tofte–Talpin, MLKit, Cyclone, `bumpalo`. Functions are parameterized over the region they allocate into. The caller picks it. `letregion` frees wholesale at scope exit. The academically exact answer to "bind a heap item to the caller's scope." Anoptic's `mi_heap_t*` parameter plan is this, verbatim, minus the compiler proof.
3. **Scoped effect tokens** — Haskell's ST monad: `runST :: (forall s. ST s a) -> a`. Real mutation inside, provably pure outside. The rank-2 `forall s` makes the scope name unutterable beyond the block, so nothing mutable escapes. The type-theoretic ideal of "nothing within pollutes what is without."
4. **Convention and context** — Zig (every allocating function takes an `Allocator`; `defer deinit()` is manual RAII; the stdlib refuses to allocate any other way, so the convention is load-bearing); Odin/Jai (implicit `context.allocator` / `temp_allocator` = dynamically scoped regions the caller rebinds for a scope). C analog: a thread-local arena stack.
5. **Runtime liveness** — the GC world. Nothing binds to scope, everything binds to reachability. Two facts worth keeping: a generational nursery *is* a bump arena that gets reset (functional languages have been running on arenas all along), and Go's escape analysis is the compiler answering "does this callee allocation escape" statistically. Arenas are the deterministic version.

Plus a sixth, sneaky answer, **array-language dissolution**. APL/Q dissolve ownership by having few owners. Pipeline-of-whole-column transforms give intermediates trivially obvious lifetimes. Refcounted column vectors clean up eagerly. Lifetime management is easy when a program has ten big values instead of a million small ones, the deep reason data-oriented design and arenas fit together.

### Storage-layout traditions worth stealing

- **Redis `sds`** — metadata stored behind the pointer, so an `sds` is a plain `char*` that is also length-carrying. Invisible header.
- **Lua** — interns every string, equality is pointer compare. Why Lua strings "feel like things you just have."
- **kdb+ symbols** — a string is a `u32` index into a global table, equality is integer compare. The identity-heavy subset (asset paths, entity names, keys).
- **Umbra / CedarDB "German strings"** — 16-byte immutable value with an inline prefix, built for columnar scans (see §5.3).

---

## 3. Type-theoretical facts

The region lineage is the formal backbone of the whole memory architecture (`data.md`, Pillar I), and strings inherit it directly.

- **Tofte & Talpin (1994)** — region-based memory management. Group allocations into regions whose lifetimes are inferred statically and reclaimed wholesale in O(1). `letregion ρ in e` binds a region's lifetime to a lexical scope. A **type-and-effect system** proves no value outlives its region. The key trick is **effect masking**, an `access(ρ)` effect occurring entirely inside `letregion ρ` is erased from the expression's outward effect. Type-check to an empty residual effect and every access provably happened inside a live region. No GC, no runtime checks.
- **Calculus of Capabilities** (Walker, Crary, Morrisett, 1999) — decouples allocation from deallocation (explicit `newrgn`/`freergn` instead of strict LIFO) by threading a static capability set through the type system. Lets regions serve event loops and state machines whose lifetimes are not tree-shaped.
- **Cyclone** (Grossman, Morrisett, Jim, Hicks, Cheney, Wang) — carried regions into a real C dialect. Pointers carry their region (`int *ρ`). `regions_of(τ)` plus region subtyping make dangling-pointer dereference a *compile-time* error with zero runtime checks. The existence proof that C idioms and region safety can coexist, and the proof that enforcement is a language change. The moment you can reject escape, you are no longer writing C. Rust's lifetimes (`'a`) are descendants of these region variables.
- **Substructural types** (Pierce, *TaPL*) — the chapter that became Rust lifetimes. Most of the rest still awaits industry.

### The Wall — what C structurally cannot do

Two language facts, both load-bearing:

1. A callee cannot attach anything to the caller's scope. `__attribute__((cleanup))` binds only at a **declaration**, written by the declarer. There is no mechanism by which a callee can reach up a stack frame and register "free this on exit." C++ does it because a returned temporary has a destructor, an agent the callee leaves behind in the caller's frame. C has no agents. This is the wall the 2024 macros hit.
2. C's type system relates no pointer to a lifetime, so escape cannot be rejected. An `anostr_t` leaking its frame compiles clean every time.

The statically-enforced version is therefore literally impossible in C. The crucial calibration: **Zig has no soundness here either**. A Zig slice outliving its arena is exactly as undetected at compile time as in C. Zig ships on good defaults, `defer`, and debug-mode runtime checks. So the C-vs-Zig delta is *ergonomics only*, the C-vs-Rust delta is *the static proof*. Only one language on the tour actually has the theorem, and its price was already judged not worth paying.

---

## 4. Interesting notes from the theory and the canon

### Convergent evolution

Four lineages reached "allocations are scope-shaped, the region is the unit, the caller owns the callee's effects" independently: the region-calculus theorists from ML semantics, the game-engine practitioners from shipping (Muratori), the GC people backwards via the nursery, and the architect on walks. Same organ, four times. The constraint is not in anyone's formalism, it is in the territory. Memory hierarchies and lifetimes really do have that shape.

### What ships, what never shipped

- Production region **trees** ship everywhere: `talloc` (Samba), APR pools (Apache), Postgres `MemoryContext`s. Region hierarchies in production for decades.
- Region **inference** (automatic Tofte–Talpin) has exactly one serious implementation, MLKit, and it needed a backup tracing GC for the cases inference kept a region alive pathologically long. The lexical `cleanup`-attribute version is the practical compromise the industry standardized on without naming it.
- SIMD pays in strings at **validation and scanning**. `memcpy` is already the hand-tuned dispatched-at-load AVX routine, so a SIMD `strncpy` re-derives it. Lemire-style table-lookup UTF-8 validation does multiple GB/s and is what simdjson rides on. Point the assembly itch at `utfstrcheck`.

### The canon audit (taste calibration)

- **Sedgewick** — flat-memory cost model is dead. On a machine where a cache miss costs ~200 instructions the constants invert the asymptotics. Open-addressing Swiss tables, in-memory B-trees, pdqsort-class hybrids, and radix sort (back from the dead for fixed-width keys, exactly entity IDs) beat the textbook verdicts. Keep the ideas, trust none of the data-structure verdicts.
- **Yourdon & Constantine** — decompose-by-control-flow is what data-oriented design exists to repudiate. The coupling/cohesion vocabulary survives (data coupling vs common coupling is "nothing pollutes what is without" in a 1975 haircut). Information hiding is Parnas, the one 1972 idea that aged perfectly.
- **Bentley, *Programming Pearls*** — the **method** (estimation, invariant-driven derivation) is the durable layer, and it doubles as the human–AI interface. Header invariants are specs and prompts. The cautionary tale: his loop-invariant-derived binary search shipped `(lo + hi) / 2` overflow in the JDK for years (Bloch, "nearly all binary searches are broken"). The proof was right, the machine integers were not in the proof. The deepest single lesson for this engine: **invariants must be stated in the machine's arithmetic.**
- **Knuth** — math immortal, MIX/MMIX model a period piece (it will not tell you the memory mountain dominates everything).

### Game Engine Architecture §6.4 (Gregory) — the practitioner's column

The empirical case and a parallel identity type:

- Strings are expensive. `strcmp` is O(n), `strcpy` copies and maybe allocates. Gregory profiled a game where `strcmp`/`strcpy` were the **top two most expensive functions**.
- Always pass by reference. Know whether a string class **owns** its buffer or references memory it doesn't, and whether it is copy-on-write.
- **Hashed string ids** ("string id" / Unreal `FName`): hash a string to an int, compare as fast ints, keep originals in a global table for debug. Interning = hash + add to table, done once and cached. Naughty Dog uses compile-time hashing (`"foo"_sid`) so ids are `switch` labels, and moved from 32-bit to **64-bit** hashes to kill collisions. C23 `constexpr` gives us this without C++ UDLs.
- UTF-8 everywhere on 8-bit `char` (ASCII-backward-compatible, byte-granular, high bit flags multibyte). Define your own string type. This confirms the owned-string + length + UTF-8-transparency design precisely.

---

## 5. Competing designs for Anoptic

Four candidate string designs are live, plus a parallel identity type. They are not all mutually exclusive. The leading direction composes several.

### 5.1 Design A — the 2024 `ano_strings.h` spec (on `feature-strings`)

`anostr_t {char* buffer; size_t len}`, `anostr_utfhandle_t {int32 index; uint8 bytesize}`, cleanup fns, byte/utf length, UTF-8 iteration/validation, UTF-16↔UTF-8 via `wchar_t`, byte/utf slices, and "managed slice" macros combining statement expressions + `__attribute__((cleanup))` + `alloca`/`mi_malloc`. Implementations are stubs.

What held up:
- The fat-pointer slice type (independently converged with `&str` / `[]const u8` / Go `string`).
- The `utfhandle` iterator cursor (`char_indices` shape).
- Byte-transparent UTF-8 storage with validation as a separate layer.

What was broken:
- `ANOSTR_HEAP_BYTESLICE` is a deterministic use-after-free. The cleanup-attributed `_heap_mem` is declared *inside* the statement expression, so cleanup fires at its closing `})`. The buffer is freed before the surrounding expression hands back an `anostr_t` pointing at it.
- `ANOSTR_STACK_BYTESLICE` has the sibling lifetime hole (GCC frees `alloca` space at the statement-expression's end) and a third mundane bug: it never `memcpy`s the source, so the slice is uninitialized stack garbage (the UTFSLICE variants remembered the copy).
- `wchar_t` is 32-bit on Linux, 16-bit on Windows. The one function whose entire reason to exist is Windows paths means something different per platform. Use `char16_t`.
- `int` lengths cap at 2 GiB and sign-mix against `size_t`. Use `size_t`/`ptrdiff_t`.

The deep gap: one type plays both **owner** (some `anostr_t`s own their buffer, implied by `anostr_cleanup`) and **view** (others don't, implied by an unmanaged-slice return), with no way to tell them apart at a call site, the classic C string-library failure mode. The fix the macros could never reach in 2024 was the arena infrastructure that did not exist yet. The correct cleanup shape was already in the header: attach the attribute at the *caller's* declaration.

```c
anostr_t s CLEANUPATTR(anostr_cleanup) = anostr_heap_slice(...);  // works
```

### 5.2 Design B — notes.md's owned string (the current spec)

```c
{ char* ptr; uint32_t len; uint32_t capacity; }
```

`LOCALHEAPATTR`-style scoped cleanup. Allocations through a **heap parameter** so strings live in any arena. Copy-on-slice. ~150 lines. UTF-8 deferred (byte-transparent in storage, validation/iteration layered on later when the text renderer demands it). This is 2026-you having already fixed 2024-you's owner/view ambiguity: lifetimes become a property of the *region*. It is exactly Gregory's "owns-its-memory, carries its length" design, and the region-parameter family (§2.2) hand-encoded in C: pass `mi_heap_t*` first, return plain values pointing into it, lifetimes O(1) per scope, "move semantics" reduced to copying 16 bytes of `{ptr, len}`.

### 5.3 Design C — German-string value + ambient region + `keep` (leading candidate)

The "a string is something you just have" synthesis. A 16-byte immutable value, Umbra layout:

```
        0        4        8                16
        ├────────┼────────┴────────────────┤
SHORT   │ len    │  bytes[12] inline       │  len <= 12: the string IS the value.
        ├────────┼────────┬────────────────┤  no pointer, no owner, no lifetime, copy = 16B.
LONG    │ len    │ prefix │ char* ─────────┼─► bytes live in SOME arena
        └────────┴────────┴────────────────┘  prefix = bytes[0..4]: most compares
                                              resolve in-register (columnar-friendly).

compare(a,b): len != len? prefix != prefix? → answered IN REGISTER, no dereference.
```

Composed with engine-owned regions and one ceremony:

```
                      "I need a string"
                             │
                     fits in 12 bytes?
                yes ┌────────┴────────┐ no
                    ▼                 ▼
             INLINE VALUE      frame arena (ambient, zero ceremony)
             no lifetime              │
             just have it      outlives this tick?
                            no ┌──────┴──────┐ yes
                               ▼             ▼
                          dies at      anostr_keep(heap, s)   ← THE only ceremony
                          tick end     the one place lifetimes exist in the API
                          for free            │
                                       used as identity/key?
                                              │ yes
                                              ▼
                                       intern → sym_t (u32), compare = integer ==
```

Load-bearing facts:
- **Immutability** is what makes the prefix never lie, copies safe, and the FP alignment real. Construction-time mutation goes through a builder `{ptr, len, cap, heap}` in scratch, then `freeze()` → `anostr_t`.
- The **ambient frame arena** is legitimate here precisely because a game engine owns the loop. The ambient lifetime is the tick. Destroy and recreate the frame heap each tick so a stale frame pointer hits freed pages (loud), and ASan-poison the region in debug.
- `anostr_keep` is the entire remaining lifetime API surface, the one place a human (or a reviewer, or the model) must think. Its exact feel is the open design question.
- Prior art exists for each piece (sds, Lua interning, Umbra) but **the combination**, German value + engine-owned ambient regions + scoped `keep`, is unclaimed territory.

### 5.4 Design D — handles + generations (checked safety, even in release)

A long string becomes `{u32 idx, u32 gen}` into a string table; every access checks `gen` against the slot's generation, which is bumped on free.

```
anostr_h            strtab.slot[idx]
┌─────┬─────┐       ┌──────┬────────────────┐
│ idx │ gen │ ────► │ gen' │ {ptr, len, cap}│   access valid iff gen == gen'
└─────┴─────┘       └──────┴────────────────┘   free: gen'++  → every stale handle mismatches
```

Converts use-after-free from UB into a checked error at the cost of one indirection. How shipped engines fake memory safety in C. Composes with everything above (debug builds could do both).

### 5.5 The parallel identity type — hashed string ids

Independent of the byte string: a 64-bit compile-time hash (`_sid`) for ECS entity-type names, event types, resource GUIDs, and config keys. One primitive unifying four subsystems (Gregory, §4). Interns once, compares as ints, recovers originals from a debug table. Adopt early. It is the identity subset.

### The C-expressible toolkit and the enforcement ceiling

| mechanism            | lifetime rides on    | cleanup fires at  | violation cost   | in C?        |
|----------------------|----------------------|-------------------|------------------|--------------|
| C++ RAII + move      | the value (dtor)     | owner's `}`       | compile error    | no dtors     |
| Rust owner + Drop    | ownership token      | final owner's `}` | compile error    | no           |
| Tofte–Talpin / MLKit | inferred region      | `letregion` exit  | compile error    | no           |
| Cyclone              | region annotations   | region exit       | compile error    | was C+types  |
| Haskell ST monad     | phantom token `s`    | `runST` boundary  | compile error    | no           |
| GC / escape analysis | reachability         | eventually        | none (runtime)   | unwanted     |
| Zig alloc + defer    | convention           | `defer` line      | debug crash      | yes — equal  |
| Odin context         | implicit param       | scoped defer      | debug crash      | yes — TLS    |
| C cleanup attribute  | caller's DECLARATION | caller's `}`      | silent UB        | native       |
| arena parameter      | the arena            | heap teardown     | ASan/loud crash  | native  ◄──  |
| handles + generation | table slot's gen     | explicit          | CHECKED, always  | native  ◄──  |

Everything above the Zig line needs a type system C lacks. Everything from Zig down is fully expressible in C23 + Clang, and **Zig adds zero soundness** over C here. The honest positioning: C cannot make the wrong program *not compile*, but it can make the wrong program *impossible* for short strings (Design C inline case, no lifetime exists), *crash-on-contact in debug* for frame strings (Design C ambient arena), and a *checked error* for handles (Design D), with ergonomics that match or beat Zig's, because the engine owns the loop. The dream survives. It ships with a debugger.

The architect's philosophy, named: **region-polymorphic imperative programming over columnar (SoA) data, with value semantics at routine boundaries**. Tofte–Talpin's regions chosen manually, the ST monad's boundary held by discipline, Zig's allocator-passing as the calling convention, SoA inside the regions. The Pearls-style header invariant ("returned string is valid for the lifetime of `heap`") does the job Rust's `'a` does, by review.

---

## 6. TODO

Ordered. Earlier items unblock later ones.

1. **Draft `anoptic_strings.h` signatures** (house rule: signatures are the architect's). Two layers: (a) the 16-byte value type plus `compare`/`slice`/`iterate`; (b) region functions taking `mi_heap_t*` first and returning values. Every function carries a Pearls-style invariant stated in machine arithmetic: "result valid for lifetime of `heap`", explicit overflow bounds, `size_t`/`ptrdiff_t` lengths (never `int`).
2. **Decide ambient-arena mechanics** — thread-local frame-heap API (push/pop/current), destroy-and-recreate-per-tick policy, ASan poisoning in debug. This is engine infrastructure. It also serves glTF/JSON scratch parsing. Builds on the existing `LOCALHEAPATTR` + mimalloc teardown.
3. **Spec the `keep` ceremony precisely** — name, copy semantics, and behavior on inline strings (answer: identity, they are values, nothing to copy). This is the whole remaining lifetime surface. Get it right and the rest is "just having strings."
4. **Builder type** for construction-time mutation: `{ptr, len, cap, heap}` in scratch, then `freeze()` → `anostr_t`. Immutability of the frozen value is load-bearing for the prefix fast path.
5. **Recover, don't merge, `feature-strings`** — lift `ano_strings.h` and the test scaffold onto a fresh branch off current `main`, implement against them, then archive-tag the old branch (never plain-delete). Carry the corrected `CLEANUPATTR`-at-caller pattern.
6. **Decide the short-string cutoff and the long-string layout** — confirm 16-byte value, ≤12-byte inline, 4-byte prefix. Settle endianness of the prefix for `compare`.
7. **Adopt hashed string ids (`_sid`) as a separate primitive** — 64-bit, C23 compile-time hashable, `switch`-able. Lands the ECS/event/resource/config identity type early.
8. **Tests first** (lifetime semantics are headless-testable, the CTest + ASan/TSan baseline is green): round-trip slice/keep/cleanup lifetimes; a use-after-frame that must crash loudly under the destroy-per-tick policy; a German-string `compare` microbenchmark vs naive `memcmp` to validate the in-register prefix path on the target Ryzen.
9. **Defer explicitly** (loud): the UTF meaning layer (lift the `utfhandle` iterator + validation signatures from `feature-strings` when the text renderer forces it), `char16_t` UTF-16↔UTF-8 conversion (when Windows paths arrive), the interning table (until the asset pipeline demands it). SIMD work, if any, targets validation/scanning.

### Open questions

- The exact *feel* of `anostr_keep`, the only place lifetimes remain visible in the API.
- Mutability story: confirm builder-then-freeze is the only mutation path, or whether a capacity-carrying mutable variant earns its place alongside the immutable value.
- Whether Design C (German value) supersedes Design B (`{ptr, len, capacity}` owned string) outright, or B remains the long-string backing behind C's value layout.
- Whether handles (Design D) ship in release or stay a debug-only check.

Three sweeps complete and cross-confirming. Here is the full enumeration, split by what actually compiles today versus what's only designed, with prior-art references last.

---

# Strings in anoptic-engine — every approach

## A. In-tree and compiling today (strings done ad hoc, no unified type)

| # | Approach | Representation | Where | Status |
|---|----------|----------------|-------|--------|
| A1 | **`anoptic_strings.h` stub** | empty header; section comments for UTF-8 / scoped dealloc / slices | `include/anoptic_strings.h`, `src/strings/ano_strings.c` | not implemented — the placeholder the whole effort is meant to fill |
| A2 | **Null-terminated `const char*`** | borrowed pointer + `strlen`/`strcmp`/`strdup`/`strnlen`/`strstr` | everywhere: log APIs, paths, fmt strings, Vk extension name arrays | active, dominant |
| A3 | **Fixed inline char arrays** | `char name[64]` in `ModelNode`/`ModelAsset`; bounded owned | `src/render/gltf/` | active |
| A4 | **Stack scratch buffers** | `char path[256]`, `char spec[48]`, `char ts[16]`, `g_scratch[ANO_LOG_MSG_MAX]` + `snprintf`/`sprintf` | pipeline paths, filesystem, logger | active |
| A5 | **`ano_fspath` path value** | `{uint16_t length; char str[MAXPATH];}` by value -- bytes inline, NUL-terminated for syscalls, no heap, no lifetime (2026-07-03; replaced the mi_malloc'd `{len, char*}` `filepath`). OS-bounded, so the inline case is total, unlike anostr_t's 12-byte tier. Full `anostr_t` migration deferred to the VFS/async-I/O layer, where path *manipulation* (join/split/dedupe/intern) actually starts; until then borrow via `anostr_view(p.str, p.length)`. | `include/anoptic_filesystem.h`; `ano_fs_gamepath/userpath` | active |
| A6 | **`Buffer` sized blob** | `{uint32_t size; char* data;}`, not NUL-terminated, `ano_aligned_malloc` | `src/vulkan_backend/instance/pipeline.h:17` (SPIR-V load) | active |
| A7 | **Logger inline-copy + deferred format** | `%s` args memcpy'd into fixed `char[]` log slot at capture; `uint16_t` len prefix; `vsnprintf` deferred to flusher | `src/logging/logging_core.c` (`capture_deferred`/`format_deferred`) | active, production — the one real "engine string discipline" shipping |
| A8 | **Hand-rolled numeric→text** | `put_u32`/`put2`/`put_base` + `digLo[]`/`digUp[]` tables, no printf in hot path | `logging_core.c:75` | active |
| A9 | **Vendored parser strings** | cgltf (JSON+GLB internal); jsmn `jsmntok_t {start,end}` index slices (no copies) | `external/cgltf`, `external/jsmn/jsmn.h` | cgltf active; jsmn vendored, unused |
| A10 | **FreeType text stubs** | planned `ano_text_add_font(const char* path,...)` | `src/render/text/` | not implemented |

Ground truth: no interning, no SSO, no UTF-8 layer, no unified string type exists in code yet. A7 is the only purpose-built scheme.

## B. The abandoned `feature-strings` branch (Design A, 2024)

| # | Approach | Detail |
|---|----------|--------|
| B1 | **`anostr_t` fat pointer** | `{char* buffer, size_t len}` — length-prefixed, no NUL, no capacity. O(1) len, copy-free substring. Owner/view role is ambiguous (the classic C string failure). |
| B2 | **`anostr_utfhandle_t` cursor** | `{int32 index, uint8 bytesize}` UTF-8 iterator. Fns: `anostr_utflen/utfnexthandle/utfnextchar/utfstrcheck/utfcodecheck/utfslice/byteslice/utfconv_16to8/8to16`. |
| B3 | **Managed-slice macros** | `ANOSTR_{STACK,HEAP}_{BYTE,UTF}SLICE` — stmt-expr + `alloca`/`mi_malloc` + `__attribute__((cleanup))`. **Both broken**: HEAP frees result before caller reads it (cleanup fires at stmt-expr `}`); STACK never memcpy's source. "The Wall": cleanup binds only at the *declaring* scope, a callee can't attach it to the caller's frame. Also `wchar_t` should be `char16_t`; `int` lens should be `size_t`. |

Status: spec structurally sound, implementation blocked in 2024 by missing arena infrastructure; superseded. Plan (string_progress §6) is to lift onto a fresh branch off main and archive-tag the old.

## C. Proposed strings-module designs (the live decision)

| # | Design | Representation | Lifetime / ownership | Status |
|---|--------|----------------|----------------------|--------|
| C1 | **B: Owned + region param** (notes.md Step 4) | `{char* ptr; uint32_t len; uint32_t capacity;}` growable | every alloc takes a `mi_heap_t*` region (Zig-style); copy-on-slice into same arena; cleanup at region granularity | specified, not built — now unblocked by mimalloc + `LOCALHEAPATTR` |
| C2 | **C: German string** (Umbra/CedarDB) | 16-byte immutable value: `≤12B` inline, else `{len:4, prefix:4, ptr:8}`; prefix = cached first 4 bytes for in-register compare | value semantics, copy like `int`; columnar-scan friendly; immutability is load-bearing | **leading candidate** |
| C3 | **Ambient frame arena** (Odin/Jai context) | thread-local default heap pushed per tick; transient strings alloc with zero ceremony, die at frame end; debug = ASan-poison freed pages | frame owns lifetime by construction | proposed; logger already proves the thread-local-heap discipline |
| C4 | **`anostr_keep(heap, s)` promotion** | copy bytes from frame arena up to a persistent region at escape points — the only place a heap parameter appears | explicit escape ceremony | proposed |
| C5 | **D: Handles + generation** | `{u32 idx, u32 gen}` into a string table; gen bumped on free; access checks gen → UAF becomes a checked error not UB | optional safety layer, even in release | proposed, optional/orthogonal |
| C6 | **Hashed string IDs `_sid`** | 64-bit compile-time hash (Naughty Dog style, `"foo"_sid` via C23 `constexpr`); intern once, compare as int, recover names from debug table | separate identity primitive for ECS type names, event types, resource GUIDs, config keys | to adopt independently |
| C7 | **Composed synthesis** | tiers: (1) German value ≤12B → no allocator; (2) longer → frame arena; (3) `anostr_keep` to persist; (4) `_sid` interning for identity/keys | multi-tier | **recommended direction** — called "unclaimed territory": German value + engine-owned ambient regions + scoped promotion combined has no prior implementation |

Shared principle across C: **UTF-8 byte-transparent storage** — strings are always bytes; validation/iteration is a separable layer added only when the text renderer needs it.

## D. Prior-art references cited (not implemented, shaping the above)

Redis `sds` (metadata behind the pointer, public face is plain `char*`) · Lua (intern every string, equality = pointer compare) · kdb+/Q symbols (`u32` into symbol table, integer compare, columnar) · Umbra/CedarDB German strings (source for C2) · Rust `String` = `Vec<u8>` + validate-once UTF-8 invariant · Zig allocator-passing convention (source for C1's region param) · Haskell `ST` monad / rank-2 scope (boundary principle behind C3) · Tofte–Talpin region memory and Cyclone region types (`int *ρ`) — the academic basis for region-polymorphic ownership C cannot enforce, so Anoptic adopts it by discipline.

---

Sources: `docs/notes.md` (Step 4), `docs/string_progress.md`, `.claude/fable_stringtheory.md`, `.claude/logger_old.md`, plus live code in `src/logging/`, `src/strings/`, `include/anoptic_{strings,filesystem,memory}.h`, `src/vulkan_backend/`, `src/render/`.

---

## E. Salvage from the 2024 `feature-strings` branch (commits `8196a49`..`cb0d4df`, Jan–Feb 2024)

The branch shipped a full API sketch over an all-stub implementation — `anostr_utfslice` literally returned `{"hello\0", 5}`, only `anostr_cleanup` had a real body. Nothing is liftable as code. Three things are worth carrying into this spec; the arena/huge-page residue is already documented in the museum copy at `tests/anotest_chariots.c` (FINDINGS block) and distilled in `tests/anotest_memory.c`, so it is not repeated here.

The UTF-8 meaning layer already has a concrete surface. The validation/iteration layer this spec defers to "later" was enumerated in 2024 `include/ano_strings.h` and is worth reviving as-is, with the type bugs fixed: `anostr_bytelen`, `anostr_utflen`, an `anostr_utfnexthandle`/`anostr_utfnextchar` cursor over `anostr_utfhandle_t {i32 index, u8 bytesize}`, `anostr_utfstrcheck`/`anostr_utfcodecheck`, `anostr_utfconv_16to8`/`8to16`, a SIMD `anostrncpy`, and `anostr_utfslice`/`anostr_byteslice`. Carry-forward fixes: the UTF-16 endpoints must be `char16_t`, not `wchar_t` (platform-variable — 32-bit on Linux, 16-bit on Windows), and every `int` length/index must be `size_t`/`ptrdiff_t`. This layer rides on top of the layer-1 view and never owns.

Two of the original TODOs already named the two hard problems. `anostr_t // TODO: figure out what's the point of this type, exactly?` is the owner/view ambiguity — resolved here by splitting the pure borrowing view (layer 1) from the owning builder and German value (layers 2 and 4); the bare `{ptr, len}` survives only as the read face. `ANOSTR_HEAP_BYTESLICE // TODO: this might be quite interesting...` is The Wall — a statement-expression cannot hand a `__cleanup__`-bound buffer back to its caller, since cleanup fires at the stmt-expr brace and frees the result before the caller reads it (deterministic UAF). Resolved by the rule that slicing returns a non-owning view or an arena-backed value, never a cleanup-attached buffer; ownership lives at region granularity, never on the slice.

The arena substrate is validated, with one caveat that lands on layer 5. The playground proves the machinery the frame arena leans on: `__cleanup__` destructors fire at scope exit and `LOCALHEAPATTR` bulk-frees a scoped thread-local `mi_heap` at the brace. Its FINDINGS block carries the constraint for the frame heap: explicit huge pages are a Windows/Linux facility only — Apple Silicon exposes no large-page API at all (`mi_reserve_huge_os_pages` reserves nothing and returns non-zero), so on M1 the frame arena should allocate one big contiguous region and rely on the 16 KiB base granule plus ARMv8 contiguous-bit folding for TLB reach. Gate huge-page reservation to Win/Linux.

Provenance: branch `feature-strings` (`include/ano_strings.h`, `src/strings/ano_strings.c`, `src/strings/ano_string_tests.c`); live museum copy `tests/anotest_chariots.c`.
