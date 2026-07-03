# CODE_REPORT.md — Anoptic `src/` against the data-oriented canon

A critical audit of the engine's source against the principles distilled in
`docs/data-oriented.md` (Acton, Meyers, Collin, Kelley on data-oriented design;
Fleury, Lakos on arenas; Scott on lock-free structures). Scope: all of `src/`
(~11.4k lines) and `include/`. Method: the architecturally central modules (ECS,
render bridge, render slots, memory, logger, render hot path) were read in full;
the whole tree was swept for allocation discipline, concurrency primitives, data
layout, and debt. Findings are file:line-anchored. Verdicts: fundamental
mistakes, confused assumptions, and prioritized directions of improvement.

This is a design-and-completeness review, not a line-by-line Vulkan correctness
pass.

## Verdict

The sophisticated, novel parts of the engine are textbook-correct against the
canon; the mundane, load-bearing foundations the veterans care about most are
unbuilt or contradicted. The lock-free SPSC bridge, the structure-of-arrays ECS,
and the epoch-reclaimed render-slot authority are exactly what Scott, Acton, and
Kelley would write. Meanwhile the arena hierarchy that the whole memory
philosophy rests on does not exist, the asset-load path is the malloc/free forest
Fleury holds up as the anti-example, and the logger — designated as the
foundation everything else builds on — is the least finished module in the tree
and the only place that breaks the engine's own no-mutex rule.

The engine built the spire before the foundation. Nothing here is unrecoverable;
the gaps are concentrated, well-defined, and mostly about finishing and
discipline rather than redesign.

## What holds up (credit before criticism)

These are correct and should not be touched except to extend:

- The ECS is real structure-of-arrays: one dense, gapless `(data, owners)` column
  per component, chunked sparse map keyed by entity index, swap-and-pop removal,
  generational `(index, generation)` handles, all from a caller-supplied scoped
  `mi_heap_t` (`src/ecs/ano_ecs.c:33-43`, `:158-175`, `:267-283`, `:321-332`;
  `include/anoptic_ecs.h:40-44`). This is Acton, Kelley, and Collin applied
  faithfully, and it is the cleanest module in the codebase.
- The SPSC ring is a correct, genuinely lock-free, false-sharing-free ring:
  `_Alignas(64)` on `tail`, `head`, and `mask` so producer and consumer cursors
  sit on separate cache lines, relaxed load of the own cursor, acquire of the
  peer, release on publish, no CAS (`include/anoptic_render_bridge.h:196-240`).
  This is Scott's acquire/release discipline and Meyers' false-sharing avoidance,
  exactly. Single-producer-by-design makes the command order total for free.
- The render-slot authority is epoch-based reclamation with the frame counter as
  the clock: a `DESTROY` quarantines the slot with `safeFrame = currentFrame +
  framesInFlight` and only recycles it once every in-flight frame retires
  (`src/vulkan_backend/render_slots.c:104-143`), single-thread-owned so it needs
  no atomics (`render_slots.h:21-22`). This is the right answer from the canon and
  it correctly avoids hazard-pointer per-dereference fences.
- The render-apply path is O(pending changes), never O(entities): commands are
  ingested once, propagated across frames via a `pendingFrameMask`, and applied by
  slot (`src/vulkan_backend/vulkanMaster.c:681-748`). The per-entity GPU data is
  SoA across parallel SSBOs (transform, initialTransform, angular-velocity,
  mesh/material), not one fat struct (`vulkanMaster.c:508-543`, `:630-643`).

The designs are not the problem. The problem is everything below.

## Fundamental mistakes

### M1 — The arena layer the entire memory philosophy assumes does not exist

`docs/notes.md` and `CLAUDE.md` promise a hierarchy of arenas
(process > level > frame > scratch > pool) reached through `ano_*()` calls, and
"all allocations should go through arenas or thread-local heaps." The actual
memory module implements one function: `ano_heap_release`
(`src/memory/memory.c:8-10`). There is no `ano_alloc`, `ano_free`, `ano_heap_*`,
no frame arena, no scratch arena, no pool allocator anywhere in the tree. The
public memory surface is three things: `LOCALHEAPATTR`, `ano_salloc` (an `alloca`
macro), and `ano_heap_release` (`include/anoptic_memory.h:19-27`). The hugepage
reservation — the headline, experimentally-validated memory feature — is not in
the memory module at all; it lives raw in `main()` inside `#ifdef DEBUG_BUILD`
(`src/engine/main.c:93,96`), so release builds reserve no hugepages and the
TLB-elimination win is absent from the shipping path.

Against Fleury and Lakos this is the central gap: the engine has a fast global
allocator and two ad-hoc scoped heaps (the ECS world heap and the render heap),
but not the lifetime-grouped arena discipline that is the actual point.

### M2 — Diffusion blindness, the mistake you cannot see yet

Lakos's most important measured result is that on a long-running system a global
allocator lets memory diffuse — subsystem allocations intermix until locality
collapses and access slows 10–16×, and the cure is per-subsystem local
allocators. This engine's stated purpose is million-entity, thousand-system,
multi-hour simulation with continuous spawn/death — the exact diffusion
worst-case. Yet the only partitioning that exists is two heaps total. There is no
per-system arena, no per-resolution-level arena; the scoped-resolution LOD
hierarchy (the engine's signature feature, described in notes as "each resolution
level is a different arena") is entirely unimplemented. This costs nothing today
because no long churning session is ever run, which is precisely why it is
dangerous: it is invisible until the engine does the one thing it was built to do.
This is the single most consequential long-horizon issue in the codebase and it
is the performance reason the arena work is foundational, not cosmetic.

### M3 — The designated foundation (the logger) is the weakest module and breaks the engine's own rule

`notes.md` Step 1 makes the logger the first thing built — "the first module that
exercises arenas + atomics + threads together, and provides instrumentation for
everything after." In reality it is the least-finished module and it inverts that
rationale:

- It is mutex-based (`src/logging/logging_core.c:22-23`), and that mutex is the
  only host mutex in the entire engine — sitting in `src/logging/`, outside
  `src/vulkan_backend/`, in direct violation of the "no mutexes outside the
  Vulkan backend" rule. The lock-free parts of the engine are the ECS and bridge
  (later steps); the foundation is the lock.
- The `_Atomic int tail_index` (`:18`) is vestigial — every access is a plain
  `int` read/write under the mutex (`:64-65`, `:116`, `:125`). None of the
  intended fetch_add-reserve + per-slot commit-header MPSC (the gap protocol
  documented in notes and in `docs/data-oriented.md`) exists.
- File output is entirely dead: every `write_to_log_file` call is commented
  (`:59`, `:128`, `:177`) and `output_file_path` is never assigned (`:24`). The
  engine has no working log persistence, undercutting the "instrumentation for
  everything after" premise.
- A real bug: `ano_log_immediate` calls `write_all_buffered()` on every immediate
  message (`:180`, itself tagged `// TODO: Remove this`), which under the buffer
  lock copies the whole buffer into a `char fileMsg[LOG_BUFFER_MAX]` stack array
  (`:106`) and then resets `tail_index` to 0 (`:125`) — so any fatal/immediate
  log silently discards all buffered async logs, and a large `LOG_BUFFER_MAX`
  makes that stack array a latent overflow.

Plus `log_strings[]` is a `static` array defined in a public header
(`include/anoptic_logging.h:22`), so every translation unit that includes it gets
a private copy.

### M4 — The asset path is the malloc/free forest Fleury warns against

Fleury's canonical case study is replacing a malloc/free forest in a parser with
a bump arena (~100%+ speedup, because the time was in the allocator). The engine's
live glTF loader is precisely that forest: `parseGltf` is a 629-line function
(`src/render/gltf/ano_GltfParser.c:16-646`) doing ~20 raw `calloc`/`free`/`realloc`
operations with zero arena use — per-primitive `calloc`+`free` of vertices and
indices (`:76`, `:93`, `:108-109`), per-node `calloc` of child indices (`:619`),
and a recursive raw `realloc` of the global `entities[]` array one batch at a time
inside `instantiate_node` (`:661`). The top-level `calloc` is not even NULL-checked
before use (`:38-39`). The same pattern recurs in `geometry_pool_upload` — four
raw `malloc`s of meshlet scratch allocated and freed on every mesh upload across
seven exit sites (`src/vulkan_backend/geometry.c:116-119`) — and in the mesh
optimizer's per-call scratch (`src/mesh/ano_meshoptimizer.c:111,131`). Every one
of these is a textbook scratch-arena site left as loose libc.

## Confused assumptions

### C1 — "Routing raw malloc through mimalloc satisfies the memory philosophy"

The codebase mixes `mi_malloc`, raw `malloc`/`calloc`/`realloc`, `strdup`, and
`ano_aligned_malloc`-freed-with-`free` throughout, and it only works because
`mimalloc-override.h` redirects libc allocation globally
(`include/anoptic_memory.h:12`). The assumption is that this redirection makes raw
allocation acceptable. It does not: the canon's point is that the allocator's
identity is secondary to lifetime grouping and locality (Fleury, Lakos), so a fast
global allocator is still a global allocator. The override also masks latent bugs:
`ano_aligned_malloc` freed with plain `free` (`src/vulkan_backend/instance/pipeline.c:48`),
`mi_malloc` and raw `malloc` used in the same function
(`src/filesystem/filesystem_win64.c:22` vs `:41`), and raw `strdup`
(`instanceInit.c:266-274`) — all of which break the moment the override is removed
or a custom aligned allocator diverges from `free`'s contract. Net: the "everything
through `ano_*()`" abstraction is asserted but mostly absent, and the parts that
honor the spirit do so via `mi_heap_*` directly, not the promised API.

### C2 — Hot and cold data interleaved in the new render projection

`DisplayState` (`include/anoptic_render_bridge.h:162-171`, ~104 bytes) is the ECS
component the graphics-extract pass will scan every tick — but it packs the
cold 64-byte `mat4 transform` and the anim `Vector4` inline with the hot `dirty`
bitfield and the ids. The extract scans `dirty` every tick to find the rare
entities that changed, touching 4 bytes but dragging 104-byte elements through
cache: this is Acton's dictionary key/value split and bool-in-a-fat-struct
anti-pattern, reproduced in the newest code. It should be two parallel columns —
a hot `(dirty, render_id)` column scanned every tick and a cold
`(transform, anim, indices)` column read only on transition. Relatedly,
`RenderCommand` is ~160 bytes (carries a `mat4`) and the ring copies it with a
hand-rolled byte loop rather than `memcpy` (`anoptic_render_bridge.h:221-222`),
leaving the compiler's vectorization on the table on a hot path. The mistake is
latent only because no producer drives the extract yet (see C4).

### C3 — Silent degradation instead of growth or loud failure

The entity buffers correctly grow (`ensureEntityCapacity`,
`src/vulkan_backend/vulkanMaster.c:609-679`, replacing the old hard
`maxEntities = 10000`). But several remaining ceilings degrade silently rather
than grow or shout: `PALETTE_CAPACITY = 10000` caps both materials and lights and
overflow falls back to index 0 (`vulkanMaster.c:36`; enforced
`ano_GltfParser.c:309`), so an over-cap material renders as material 0 instead of
erroring — a correctness footgun. `maxMeshes = 1024` is a hard cap duplicated as
two hand-synced literals (`vulkanMaster.c:1116`, `instanceInit.c:1498`); the
geometry pool (64 MiB / 16 MiB, `geometry.c:29-30`) and bindless textures (4096,
`pipeline.c:253`) are hard ceilings. The canon's rule (Acton, and the "no silent
truncation" guidance) is to grow, or fail loudly — never to quietly substitute
wrong data.

### C4 — Mistaking built for proven: the data-flow is scaffolded but undriven

The hard, novel plumbing (bridge, slot authority, SoA command consumer) is built
and correct — and dormant. The render thread still runs the O(N) legacy path
(`updateTransformBuffer`/`updateCullingBuffers` every frame, `vulkanMaster.c:852-853`)
which is "still authoritative for the existing scene" (`:496-501`), while
`render_apply_commands` drains an empty ring. The logic side that would drive it —
the two-stage parallel tick and the `DisplayState` graphics-extract emitting
≤1 message/entity/tick — does not exist; `main()`'s "logic loop" is a
`ano_sleep`-based stand-in producing nothing (`src/engine/main.c:173-192`). So the
million-entity O(pending) data-flow has never actually run end to end. Having
finished the difficult lock-free pieces is being treated as having proven the
architecture; it has not been exercised.

## Directions of improvement (prioritized)

P1 — Build the arena layer the philosophy already assumes (addresses M1, M2, C1).
Implement a real `ano_arena` with reserve-then-commit growth (Fleury), and the
hierarchy as actual objects: a frame arena reset each tick, thread-local scratch
arenas (with the aliasing rule — `get_scratch` returns an arena distinct from any
passed in, ≥2 per thread), per-subsystem and per-resolution-level arenas (Lakos),
and free-list pools for the dynamic minority. Make hugepage reservation a
release-path `ano_*` call, not a debug-only side effect of `main`. Either ship the
`ano_*` allocation API the rule promises or amend the rule to say "scoped
`mi_heap_*` is the arena API" — but stop asserting an abstraction that is not there.

P2 — Convert the asset path to scratch arenas (addresses M4). Route the glTF
parser, `geometry_pool_upload`, and the mesh optimizer through a per-load scratch
arena (push, parse, copy survivors out, drop the arena). Split `parseGltf` into
named stages while doing it. This is the highest-ROI, lowest-risk change and is
exactly Fleury's worked example.

P3 — Fix the foundation (addresses M3). Two viable paths: (a) pragmatically, ship
a correct mutex logger now — wire `output_file_path`, uncomment the file writes,
delete the `write_all_buffered()` call from the immediate path, move
`log_strings[]` to a `.c` — and defer lock-free; or (b) build the intended MPSC
(fetch_add reserve + per-slot release-commit header + consumer halts at the first
uncommitted slot, the gap protocol from `docs/data-oriented.md`) and relocate or
formally exempt the mutex. Either way the foundation must work before more is
stacked on it.

P4 — Finish the cutover and the layout (addresses C2, C4). Make slots/SoA buffers
authoritative and delete the legacy `entities[]` O(N) path
(`vulkanMaster.c:496-501`); split `DisplayState` into hot/cold columns; `memcpy`
the ring payload; and write the real two-stage tick + graphics-extract so the
bridge is actually driven and the million-entity path runs.

P5 — Close the loud-failure and hygiene gaps (addresses C3 and confirmed debt).
Make caps grow or assert/log instead of substituting index 0; de-duplicate
`maxMeshes`; make `gpu_alloc`'s block array grow geometrically rather than +1 per
call (`src/vulkan_backend/gpu_alloc.c:55`); NULL-check the glTF top allocation;
fix the shader-buffer/`VkShaderModule` leaks on pipeline-init error paths
(`flat.c`, `transmission.c`, `pipeline.c`) and the staging-buffer handle leaks in
`texture.c`; finish `cleanupVulkan` (`instanceInit.c:1913-1914`, "I basically gave
up").

P6 — Verify per Scott. Before the lock-free event bus and the cache-line-striped
structures (notes Step 5/8) become load-bearing, model-check or TLA+ the ring and
the future striped structure, run assertion-heavy thread pile-on, and benchmark
the event bus against LCRQ and the M&S queue (the bars the canon sets). The SPSC
ring's init visibility currently relies — correctly — on the thread-creation
happens-before plus the seq_cst READY gate (`vulkanMaster.c:798`,
`main.c:153-161`); document that, because it is subtle and unstated.

## Scorecard

| Area | Canon principle | Verdict | Anchor |
|---|---|---|---|
| ECS storage | SoA, handles, swap-and-pop (Acton/Kelley/Collin) | Strong | `ano_ecs.c:33-43,158-175,267-283` |
| SPSC bridge ring | acquire/release, cache-line split (Scott/Meyers) | Strong | `anoptic_render_bridge.h:196-240` |
| Render-slot reclamation | epoch reclamation (Scott) | Strong | `render_slots.c:104-143` |
| Render apply path | O(pending), SoA GPU buffers (Acton/Collin) | Strong (dormant) | `vulkanMaster.c:681-748` |
| Arena hierarchy | group by lifetime (Fleury/Lakos) | Broken — unbuilt | `memory.c:8-10` |
| Long-session locality | per-subsystem arenas vs diffusion (Lakos) | Broken — unaddressed | two heaps total |
| Hugepages | release-path TLB win | Gap — debug-only | `main.c:88-96` |
| Asset-load allocation | scratch arena (Fleury) | Broken — malloc forest | `ano_GltfParser.c`, `geometry.c:116-119` |
| Logger | lock-free MPSC, working instrumentation (Scott) | Broken | `logging_core.c:18-23,180` |
| No-mutex rule | lock-free outside Vulkan | Violated (logger) | `logging_core.c:22-23` |
| DisplayState layout | hot/cold split (Acton) | Gap | `anoptic_render_bridge.h:162-171` |
| Cap handling | grow or fail loud (Acton) | Gap — silent fallback | `vulkanMaster.c:36` |
| Data-flow proven | end-to-end exercise | Gap — undriven | `main.c:173-192` |
| Maintainability | reason about cost of change (Acton) | Gap — god functions, 31 TODOs | `parseGltf` 629 lines |

## Closing

The engine's author clearly knows the canon — the lock-free bridge, the SoA ECS,
and the epoch-reclaimed slots could be lifted straight from these talks. The work
left is not more cleverness; it is the unglamorous discipline the same veterans
insist on: arenas everywhere and grouped by lifetime, a working instrumentation
layer, asset loading that does not thrash the allocator, and actually running the
million-entity path the architecture was shaped for. Finish the foundation, then
the spire is justified.
