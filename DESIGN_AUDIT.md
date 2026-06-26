# Design Audit — ECS, Render Bridge, Vulkan Backend

Scope: a structural audit of `src/ecs`, `src/render_bridge`, `src/vulkan_backend` and their
public interfaces in `include/`, plus the GPU-side data model in `resources/shaders/`. The
question is not "what works today" but "what kinds of game does the *shape* of these data
structures and flow models permit or forbid downstream, and which limits are root-level
versus cleanly extensible later." Read against the vision in `docs/notes.md` (million-entity
space-colony simulation in the Dwarf Fortress / Stellaris lineage).

Method: read every header and implementation in the three modules, all five shaders, the
descriptor-layout wiring in `instance/pipeline.c`, and the orchestration in `vulkanMaster.c`.
Findings cite `file:line` or the struct/identifier so each is checkable.

The foundations are clean and internally coherent — generational handles, sparse-set stores,
the SPSC bridge, render-owned slots with frame-gated reuse, dynamic capacity. The concerns
below are almost all about *expressiveness and scaling shape*, not correctness. They are
written loud because the cheapest time to widen a foundational contract is before code leans
on its current width.

---

## 0. The one-paragraph thesis

Three decisions dominate everything downstream. First, a renderable is permanently described
by exactly five things — base transform, one spin/orbit animation, one mesh index, one
material-palette index, one light index — and that vocabulary is welded into the bridge
protocol, the descriptor layout, and the cull shader simultaneously, so any sixth per-entity
visual attribute (a tint, a selection highlight, a damage state, a decal) is a six-file lockstep
change today. Second, the only continuous motion the GPU understands is constant-angular-velocity
rotation; every other kind of movement — linear drift, Keplerian orbit, physics, pathfinding —
degrades to one teleport message per moving entity per tick, so the celebrated "zero per-frame
bridge traffic" holds only for things that spin in place. Third, the two-world split put the
camera, the input, and all screen-space knowledge exclusively in the render world with a
return channel that carries two event kinds, so picking, hover, selection, and
attention-driven simulation LOD — the load-bearing interactions of this entire genre — have
no path home. None of the three is a bug; all three are contracts that are far cheaper to
widen now than after fifty systems assume their current width.

---

## 1. Hard ceilings quick reference

Numeric limits currently baked in. "Grown" = expands on demand; "hard" = fixed at the cited site.

| Limit | Value | Where | Status | Bites when |
|---|---|---|---|---|
| Component types | 128 | `ANO_ECS_MAX_COMPONENTS`, `anoptic_ecs.h:58` | hard (raisable, per-entity cost) | deep simulation with many distinct data aspects |
| Distinct meshes (cull side) | 1024 | `maxMeshes`, `vulkanMaster.c:1116` | hard | content-rich worlds, many ship/part/tile models |
| Distinct materials | 10000 | `PALETTE_CAPACITY`, `vulkanMaster.c:36`; not in `ensureEntityCapacity` | hard, not grown | per-entity color/tint/state (each distinct value = a material) |
| Total lights (storage) | 10000 | `PALETTE_CAPACITY` | hard, not grown | many emissive sources |
| Usable lights (shading) | ~dozens | `flat.frag:198` loops all lights per fragment | hard architectural | any many-light scene |
| Bindless textures | ~4096 | `BindlessTextureArray.maxTextures`, `structs.h:328` | raisable to device limit | large texture sets; no eviction |
| Commands / tick | 4096 | ring init, `vulkanMaster.c:1611` | raisable; backpressure cliff | mass teleport / mass destroy in one tick |
| Events / tick | 1024 | ring init, `vulkanMaster.c:1611` | raisable | mass slot retirement |
| Frames in flight | 3 | `MAX_FRAMES_IN_FLIGHT`, `structs.h:27` | hard | 3× multiplier on all per-slot GPU memory |
| Pipeline partitions | 7 | `PIPELINE_TYPE_COUNT`, `components.h:23` | hard | indirect+compacted sized ×7, ~5 unused |
| Cameras / views per frame | 1 | single `viewProj` in `CullUBO`, `structs.h:331` | hard architectural | split-screen, minimap-3D, reflections, shadows |
| Lights per renderable | 1 | scalar `light_index`, `anoptic_render_bridge.h:120` | hard | multi-emitter objects |
| GPU motion types | 1 (spin/orbit) | `update.comp:45` | extensible (shader) | anything that translates or accelerates |

No global entity cap remains (the old `maxEntities=10000` ceiling is gone); slot, logical-map,
and ECS-slot arrays all grow. The ceilings above are the ones still standing.

---

## 2. Information — what an entity can *be*

What game-logic attributes and graphics elements the data structures can represent.

### 2.1 The renderable attribute set is closed at five fields (root) - ADDRESSED

A renderable is `DisplayState` on the logic side (`anoptic_render_bridge.h:162`) and `EntityInfo`
`{meshIndex, materialIndex}` (8 bytes) on the GPU (`cull.comp:5`). Everything a renderable can
carry across the bridge is: `transform`, `angular_velocity`, `mesh_index`, `material_index`,
`light_index`. The GPU per-slot buffers mirror exactly these. There is no per-entity scalar,
color, or "user data" channel.

Concretely impossible without widening the contract:

- Per-entity tint / team color / faction color / player-chosen livery. Color lives only in
  `MaterialData.baseColorFactor`, and material is a palette. Eight team colors = eight
  materials (fine); a continuous health-based or heat-based tint, a per-unit selection
  highlight, or per-colonist clothing color = one material per distinct value, against the
  10000-material hard cap (`vulkanMaster.c:36`, not grown by `ensureEntityCapacity`). For an
  RTS/4X this is the single most common per-instance attribute and it has no home.
- Per-entity animation phase. `update.comp` drives rotation off the global clock
  (`update.comp:54`), so every entity sharing an `angular_velocity` is phase-locked. A field of
  asteroids all spin in perfect sync; a thousand fans rotate as one.
- Decals, damage overlays, wear/dirt, build progress, icons/badges, status rings, per-entity
  emissive pulse. None expressible. "Decals" in particular have no primitive: no decal
  projection, no per-entity texture-layer index, no second material slot.
- Per-entity scale beyond what is folded into the `transform` (which works, but is then
  clobbered logic-side rather than parameterized) and any per-instance shader parameter.

Why it is root and not incremental: the five-field shape is asserted in lockstep at six sites —
the bridge struct (`anoptic_render_bridge.h`), the apply path (`applyCommandToFrame`,
`vulkanMaster.c:508`), the slot buffers and their growth (`ensureEntityCapacity:609`), the
descriptor layout (`pipeline.c:78` global, nine fixed bindings), the GLSL struct in three
shaders, and the cull write. Adding a sixth attribute touches all six. Doing it once, well, is
cheap; doing it per feature is the tax.

Recommended root fix: add one general per-slot `uvec4`/`vec4` "instance data" SSBO binding now,
carried by a new `RFIELD_USERDATA` bit, interpreted by the fragment shader as the game sees fit
(packed tint + flags + two scalars). One binding, one field, one growth entry buys an open-ended
per-entity channel and ends the per-feature lockstep edits.

### 2.2 Continuous motion is spin-or-orbit only; everything else is O(moving)/tick (root) - ADDRESSED

`update.comp` (the entire GPU animation vocabulary) does exactly one thing: rotate about a
fixed axis at constant speed, pre-multiplied (orbit) or post-multiplied (spin) with the initial
transform (`update.comp:45-64`). The sparse/continuous split (the architecture's headline
efficiency claim — "animated entities cost zero per-frame bridge traffic") therefore covers
*only* objects that spin in place or circle on a fixed-radius, constant-rate ring.

Falls outside the vocabulary, hence one `RFIELD_TRANSFORM` teleport per entity per tick:

- Linear translation — a ship under way, a projectile, drifting debris, a migrating fleet.
- Keplerian / elliptical / variable-rate orbits — i.e. *actual planetary and asteroid motion*
  in a space game. The one motion a space sim needs most is the one not parameterized.
- Acceleration, easing, spring/damper, interpolated paths, anything physics- or
  pathfinding-driven.
- Non-rigid or scale animation (pulsing, growth, breathing).

The memory note acknowledges this ("CPU-driven non-parameterizable motion degrades to
O(moving) on the command stream"). The audit's amplification: for this genre *most things
translate*, so the steady state is not the advertised zero-traffic case but O(moving)/tick on
the bridge, and each teleport is also written ×`MAX_FRAMES_IN_FLIGHT` into host-visible memory
(`applyCommandToFrame:521`). At a 10k-ship battle all maneuvering, that is 10k commands/tick —
which also collides with the 4096-command ring (§3.3).

Extensible, partially: new closed-form motions (linear with velocity, elliptical with orbital
elements) can be added to `update.comp` + an anim-type enum + params, additively. That is the
right move and should happen early. But the class is fundamentally open — physics and
pathfinding have no closed form — so a scalable path for *arbitrary* CPU-driven motion is also
needed: either a compute integrator fed by per-slot velocity/accel (keeps it GPU-side), or an
explicit, batched, device-local transform-streaming path that accepts O(moving) but pays it
efficiently (see §3.4, §4.4). This is a root design choice, not a shader tweak.

### 2.3 Vertex format is fixed at position/normal/texCoord (root for some genres) - ADDRESSED

`Vertex` is `{Vector3 position; Vector3 normal; Vector2 texCoord}` = 32 bytes (`vertex.h:20`),
duplicated as `PackedVertex` in `flat.mesh`, `flat.vert`, and the `vertexOffset/32u` stride math
in `cull.comp:181`. Absent and unrepresentable without changing the format at all those sites:

- Tangents. `MaterialData` advertises `normalTexture` (`structs.h:183`) but the vertex has no
  tangent basis and `flat.frag` never samples it — normal mapping is effectively unsupported
  despite the material claiming it.
- Vertex colors. Ironic given the stated stylized / non-PBR aesthetic (`notes.md` §5), where
  per-vertex color is the cheap workhorse; low-poly, voxel, and flat-shaded looks want it.
- Skinning (bone weights/indices). No skeletal animation is possible: characters, creatures,
  articulated mechs, animated aliens. A colony of *people* cannot have rigged bodies. Acceptable
  if the art direction is DF-style abstract; a hard wall if not.
- Second UV set (lightmaps), per-vertex AO, any custom attribute.

The "32" magic number embedded in shader index math makes the format change multi-site and
brittle. Verdict: extensible but deliberately, and skinning in particular drags in a whole
skinning compute pass and per-bone matrix storage — decide early whether the game needs rigged
geometry, because retrofitting touches the vertex format, the geometry pool, every geometry
shader, and adds a pass.

### 2.4 The material model is maximal-PBR, contradicting the stated philosophy (tension)

`notes.md` §5 says "No PBR ... optimizes for making a million objects look good." The code is
the opposite: `MaterialData` is a 320-byte exhaustive glTF-PBR block with 14 ratified
extensions (`structs.h:169`), and `flat.frag` runs a full Cook-Torrance BRDF per fragment
(`flat.frag:133`). Consequences for "what an entity can look like": rich per-material PBR is
*available* (a strength for fidelity), but it sets the per-material footprint at 320 bytes and
the per-fragment cost high, and it sized the whole material pipeline for "50 objects look
photorealistic," not "a million look good." This is not a limit on information so much as a
strategic inconsistency: the data structures commit to the expensive path the vision rejected.
Worth a deliberate decision — either embrace PBR and drop the "no PBR" claim, or thin
`MaterialData` toward the stylized target and reclaim the budget.

### 2.5 Owned / variable-size per-entity data has no managed story (extensible, needs policy) - ECS, if needed for testing

ECS components are fixed-stride POD (`ano_ecs_register_component`, stride fixed for life). A
component may hold a pointer (to a name, an inventory, a dialogue tree, a mod blob), but
swap-and-pop removal `memcpy`s raw bytes (`store_remove:166`) and destroy frees nothing it
points to. So names, histories, relationships, inventories — the texture of a DF-scale sim —
must live in side tables keyed by entity, with manual lifetime. This is a normal data-oriented
posture and is fine, but it must be a stated discipline: components stay POD-and-pointer-free so
that bulk serialization (§4.6) and swap-and-pop stay sound. Encode variable data as handles into
arena-owned side stores, never as raw owning pointers in components.

---

## 3. Interactions — what entities, components, and the player can do to each other

### 3.1 Picking / hover / selection has zero foundation, and the world-split fights it (root) - ADDRESSED (GPU id-buffer + camera snapshot back to logic; logic-raycast path still needs the spatial grid §3.4)

The prompt names "entity hover-over detection" specifically. There is no picking path of any
kind: grep finds no id-buffer, no `gl_FragCoord` id write, no readback, no raycast, no
mouse-coordinate plumbing anywhere in `src`, `include`, or `resources/shaders`. Worse, the
two-world split places the prerequisites in the wrong world:

- The camera/view-projection is built on the render thread (`updateUniformBuffer`,
  `instanceInit.c:1176`, called from `drawFrame`) and lives in `GlobalUBO`/`CullUBO` only. The
  logic world never receives it. So the logic side cannot even do a CPU raycast against its own
  simulation transforms, because it does not know the camera.
- The render→logic events ring carries two kinds only: `REVENT_SLOT_RETIRED`, `REVENT_CAPACITY`
  (`anoptic_render_bridge.h:130`). There is no channel for "cursor is over render_id N," no
  picking request, no picking result.

Every core interaction of the target genres is blocked: click-to-select a ship, hover a planet
for a tooltip, drag-select a fleet, click a colonist, target an enemy, paint a build zone,
inspect an asteroid. Two viable designs, both requiring foundational work: (a) a GPU id-buffer
attachment written by the geometry stage + a readback + a new event kind carrying the hit
render_id; or (b) push the camera/viewport back to logic each frame and CPU-raycast logic-side
against a spatial index (§3.4). Either way the *return contract* of the bridge must grow beyond
two enum values. This is the most consequential interaction gap and it is structural, not a
missing feature flag.

### 3.2 Multi-component joins are scattered; the ECS has no archetypes or queries (root performance shape) - ECS, if needed for testing

The hot iteration primitive is `EcsColumn` over a *single* component (`anoptic_ecs.h:170`).
Multi-component systems are told to iterate one column and call `ano_ecs_get`/`ano_ecs_has` for
co-components (`anoptic_ecs.h:156`). But each store's dense order is independent and scrambled by
swap-and-pop (`store_remove:164`), so a system over `(Position, Velocity, Mass)` gets one linear
column and two random-access sparse lookups per element (`ano_ecs_get` = mask test + chunked
sparse indirection + dense fetch, `ano_ecs_get:301`). For a million entities this is cache-linear
for one component and cache-hostile for the rest.

This collides head-on with the engine's own study corpus (`docs/data-oriented.md`), which
preaches structure-of-arrays and splitting hot from cold fields. Splitting fields into separate
components is exactly what makes joins scattered here. The result is a pressure toward *fat*
components (AoS-within-a-component: one `Body {pos,vel,mass}`) to keep the lead iteration linear
— the opposite of the SoA hot/cold split the corpus recommends. You cannot have both a
fine-grained component decomposition *and* linear multi-component iteration in a pure sparse-set
store.

Game impact: any system touching ≥2 components over large N pays the scatter — physics
integration, AI over (perception+goals+position), economy over (stockpile+production+demand),
combat over (transform+health+weapon). These are the *bulk* of a simulation game's per-tick
work, i.e. exactly the workload the engine exists to make fast.

Verdict: root, and hard to retrofit. Sparse-set ECS and archetype/table ECS differ at the
storage layer. If linear multi-component iteration matters for the core systems (it does for
this genre), decide now whether to (a) accept fat components for hot system groups as policy,
or (b) add an archetype/table option for designated hot groups while keeping sparse sets for
sparse/optional components. Both are defensible; silently discovering the scatter after twenty
systems are written is not.

### 3.3 Mass state-change and mass-despawn are O(n) messages against a 4096 ring (root-ish) - ADDRESSED

The command protocol has `RCMD_BULK_CREATE` (one message spawns a contiguous batch,
`anoptic_render_bridge.h:81`) but no bulk update and no bulk destroy. So:

- Destroying a 5000-ship fleet = 5000 `RCMD_DESTROY` messages.
- A solar flare changing 100k colonists' state, or a battle where thousands change mesh/material
  the same tick = one message each.

Against a 4096-entry command ring (`vulkanMaster.c:1611`), `ano_render_submit` returns false past
capacity (`ano_spsc_push:218`) and the producer must "drop, spin, or grow." Dropping desyncs the
render view (a dropped `DESTROY` strands a slot until process exit; a dropped `CREATE` is an
invisible entity); spinning stalls logic on render. This is a throughput cliff at exactly the
mass-event moments games care about. Raising the ring helps linearly and costs memory; it does
not remove the O(n)-messages shape.

Recommended: add `RCMD_BULK_DESTROY` and a `RCMD_BULK_UPDATE` (contiguous render_id range +
field mask + parallel value arrays), symmetric with bulk create, so mass events are O(1)
messages. Also define the producer's overflow policy explicitly (grow vs block vs coalesce) —
"caller decides" is not a policy.

### 3.4 No spatial index anywhere (root for interaction-rich games)

There is no grid, quadtree, octree, or BVH in any of the three modules. The GPU frustum cull
(`cull.comp`) is not a queryable structure and lives in the wrong world for logic. Every
proximity question is therefore an O(n) column scan or a hand-rolled structure: "ships within
weapons range," "asteroids near this miner," "colonists in this room," "units in this
drag-box," collision, AoE, flocking, nearest-neighbor. The study even includes DICE's
grid-of-SoA-cells (`docs/data-oriented.md` §3) as a model, unrealized. For a genre defined by
spatial interaction this is a foundational absence. Extensible (build a reusable spatial system
over the ECS), but its absence silently shapes — and slows — every gameplay system written
before it exists. Build it early and make it the substrate for both range queries and
logic-side picking (§3.1).

### 3.5 No relationships, hierarchy, or cascade/destroy hooks (root-ish) - ECS, if needed for testing

Entities are flat. An entity can store an `EcsEntityId` in a component, but there is no
parent/child, no `ChildOf`, no relation graph, and crucially `ano_ecs_flush_structural`
(`ano_ecs.c:338`) has no on-destroy hook. So: fleets containing ships, ships containing modules,
turrets parented to hulls, colonists belonging to colonies, parent-child transforms — all are
hand-rolled `EcsEntityId` fields with manual integrity. Destroying a fleet does not destroy or
detach its ships; you get dangling references (detectable via `ano_ecs_entity_alive`, but only
if every system remembers to check). No cascade delete, no reference nulling, no reverse lookups.

Game impact: every composite or relational entity (most of them, in these genres) carries
hand-written integrity code, and the natural enforcement point — the deferred destroy flush —
offers no hook. Cheap to add now (a per-component destroy callback, or a relations module that
registers cleanup at the flush), bug-prone to retrofit after systems assume references never
dangle.

### 3.6 Parallel systems cannot safely attach components (root ergonomic, cheap fix) - ECS, if needed for testing

The threading contract (`anoptic_ecs.h:18`) defers destroy and remove to a single-threaded
flush, but `ano_ecs_add` is immediate, append-only, and explicitly *not* safe to call
concurrently for the same component (`anoptic_ecs.h:128`, races `count` in `ano_ecs_add:277`).
The deferral story is asymmetric: you can queue removals from workers but not additions. So a
parallel system that reacts by attaching state — "add `Burning` to everything the fire touched,"
"add `Targeted` to units under the cursor box," "spawn-and-attach on collision" — cannot run
from the worker pool; it must funnel through one thread or a user-built deferred-add queue. This
is a direct limit on multi-threaded gameplay (the engine's reason for existing). Fix is cheap
and should land before systems are written: add a deferred-add queue symmetric with
remove/destroy (stage the payload, apply in the flush).

### 3.7 No change detection / reactivity (extensible) - ECS, if needed for testing

The ECS emits nothing when data changes — no added/removed/changed signals. The only reactive
mechanism is the manually-maintained `dirty` bitfield in `DisplayState`
(`anoptic_render_bridge.h:148`), specific to render extraction. Event-driven gameplay ("when
health crosses zero, die"; "when a tile becomes flooded, notify neighbors") is all polling.
Acceptable for bulk DOD, but reactive logic and the planned event bus (`notes.md` Step 8) will
want a generic change/observer mechanism. Extensible; flag so it is designed once.

---

## 4. Capabilities — engine-level processing

### 4.1 Multi-threaded ECS is a primitive, not a system (partly root) - ECS, if needed for testing

`anoptic_ecs.h` promises "may fan component work out to worker threads," but what exists is the
*primitive* (`EcsColumn` for manual data-parallel splitting) and the rules; there is no job
system, no system scheduler, no read/write conflict tracking, no dependency graph (the job
system is unbuilt per `notes.md` Step 5/8). So today, multi-threaded ECS = the user manually
partitions a column across threads and manually guarantees disjointness. Compared to the DOTS /
flecs model (schedule systems by their component read/write sets, auto-parallelize
non-conflicting ones), this is all on the developer. Combined with §3.6 (no parallel add) and the
single-threaded extract (§4.2), the realistic near-term parallelism is "split one big column,"
not "run the simulation's systems concurrently." This is the gap between the million-entity
*ambition* and the current ECS *capability*. Extensible, but the scheduler is real work and its
absence caps practical throughput now.

### 4.2 The graphics-extract pass is single-threaded by construction (root)

The bridge is strictly SPSC and the logic master is the *sole* command producer, emitting after
the parallel update settles so ordering is total (`anoptic_render_bridge.h:19`). That single-producer
choice — correct for cheap total ordering — also means the per-tick extract that scans
`DisplayState`, reads dirty bits, and emits commands cannot be parallelized: it is one thread.
If extract scans all renderables it is an O(entities) single-thread pass every tick just to find
the dirty minority; avoiding that needs a separately maintained dirty list (more bookkeeping,
and that list's maintenance must itself be thread-safe). At a million renderables this
single-threaded seam is a per-tick floor. Moving to MPSC would reintroduce the ordering problem
the design deliberately avoided. Root trade-off worth stating explicitly; mitigate with a
dirty-list rather than a full scan, and keep extract's per-item work minimal.

### 4.3 Per-slot GPU data is triplicated and lives in host-visible memory (root scaling) - ADDRESSED

Two compounding costs at scale:

- Triplication. Every per-slot buffer exists ×`MAX_FRAMES_IN_FLIGHT` (= 3) and each command is
  applied to all three copies via `pendingFrameMask` (`render_apply_commands:709`). Per slot that
  is roughly: initialTransform 64B + transform 64B + angularVelocity 16B + entity 8B, all ×3,
  plus compacted 4B×7×3 and indirect 20B×7×3. Order of 960 bytes/slot, ≈960 MB at 1M slots,
  ≈9.6 GB at 10M — the per-slot multiplier, not entity logic, is the binding VRAM constraint.
- Host-visible placement. All per-slot buffers are `HOST_VISIBLE | HOST_COHERENT`, persistently
  mapped (e.g. `createTransformBuffer:1064`), never `DEVICE_LOCAL`. The GPU-driven passes read
  this hot, every-frame data over the slow path: `cull.comp` reads transforms/entity/bounds/
  materials each frame, and `update.comp` *writes* transforms into host-visible memory each frame.
  On a discrete GPU this is the wrong memory for per-frame-hot million-element data. The design is
  simple and ideal for small N (CPU writes straight to mapped memory, no staging, no sync) but it
  trades away the device-local bandwidth the million-entity goal needs.

Verdict: root scaling concern hiding under the (genuinely good) dynamic-capacity work. A
scalable design keeps authoritative per-slot data device-local and updates via a staging ring;
that is a meaningful change to the apply path and is cheaper to design before everything assumes
persistently-mapped CPU writes.

### 4.4 The indirect/compacted buffers are sized ×7 pipeline types, ~5 of them dead (concrete waste) - ADDRESSED

`destIdx = pipelineType * maxEntities + writeIdx` (`cull.comp:167`) partitions the indirect and
compacted-index buffers by `PIPELINE_TYPE_COUNT` = 7. But the enum includes the two *compute*
passes (`COMPUTE_CULL`, `COMPUTE_UPDATE`) which never draw, plus `PARTICLE`, `SDF_COMPOSITE`, `UI`
which are unimplemented (`components.h:14`). Only `FLAT` (0) and `TRANSMISSION` (4) are ever
written. So ~71% of the indirect+compacted memory — which is over half the per-slot GPU footprint
(§4.3) — is reserved for partitions that never receive a draw. At 1M slots that is hundreds of MB
of permanently-idle VRAM. Fix is mechanical: size partitions by an array of *drawing* pipeline
types (2 today), not the full enum count. Pure win, no architectural change.

### 4.5 The render-side dispatch bound is the monotonic peak; it never shrinks (root for churn / LOD) - ADDRESSED, needs LOD

`updateCullingBuffers` sets `entityCount = slots.slotHighWater` (`vulkanMaster.c:437`) and the cull
and update passes dispatch over `[0, slotHighWater)` every frame, holes included (dead slots
self-skip via `meshIndex == NO_MESH`). `render_slots_alloc` only ever pops a free hole or extends
the high-water (`render_slots.c:61`); nothing ever lowers `slotHighWater`. So once peak concurrent
renderables is reached, the per-frame compute dispatch cost stays at peak for the rest of the
session, even if 90% later die. There is no high-water compaction. Combined with the bump-arena
GPU allocator that never frees and never shrinks (`gpu_alloc.h:36`, growth is realloc-and-copy with
a `vkDeviceWaitIdle` stall, `ensureEntityCapacity:622`), peak concurrency sets a permanent floor on
both VRAM and per-frame compute.

This directly undercuts the scoped-resolution vision (`notes.md` §4): demoting a star system from
full to coarse fidelity is supposed to *reclaim* cost, but on the render side it reclaims nothing —
the slots stay counted in the dispatch bound and the buffers never give memory back. For a
long-running, scale-varying session (the explicit target) this is a structural mismatch. Needs:
a way to lower the high-water when a trailing run of slots is free (compaction or epoch reset),
and/or per-region buffers that can be released wholesale on demote (which fits the arena model
better than one global growing buffer).

### 4.6 No serialization / save-load, and no scripting / modding (root for genre) - ECS, if needed for testing

- Serialization. None exists. For a simulation game, saving a million entities is mandatory. The
  good news: the SoA stores are *almost* serialization-ideal — dense POD arrays + parallel owners
  (`EcsStore`, `ano_ecs.c:33`) write out in bulk, the sparse map rebuilds, and the dense layout is
  exactly the "four flat arrays → one writev" pattern the corpus praises (`docs/data-oriented.md`
  §4). Preserve that property by policy: keep components POD and pointer-free (§2.5), and treat
  the render_id↔entity mapping and generations as part of the save. Any owning pointer in a
  component breaks bulk save. This is a discipline to lock in now, not code to write now.
- Scripting / modding. None. Components are compile-time POD with C-assigned ids and no
  reflection. One genuine strength: `ano_ecs_register_component` takes a runtime id + stride, so
  *dynamic* component types (e.g. mod-defined data blobs) are already possible at runtime — a
  better starting point than most engines. But there is no name→id registry, no field reflection,
  no script VM, no data-driven component or system definition. Stellaris-scale moddability (scripted
  components, data-defined content) needs a reflection + scripting layer the foundation does not
  hint at. Extensible, large; the runtime-registration hook is the seam to build on.

### 4.7 Lighting does not scale; no shadows; no transparency ordering (root renderer architecture) - ADDRESSED

- Many lights. `flat.frag:198` loops *every* active light for *every* fragment, with no culling,
  no clustering, no tiling, no deferred path. Storage allows 10000 lights but the per-fragment
  loop makes a few dozen the practical ceiling. A space colony — stars, engine glows, weapon
  fire, station windows, explosions — is intrinsically a many-light scene. This is a renderer
  *architecture* limit (forward-loop-all-lights), retrofittable only by moving to clustered/tiled
  forward or deferred — a significant change. Decide early.
- One light per renderable. `light_index` is scalar (`anoptic_render_bridge.h:120`); a ship with
  running lights + engine + cockpit needs multiple light-only entities, each a slot and each a
  full per-fragment loop iteration.
- No shadows. No shadow pass, no shadow atlas, no per-light depth. Additive but large (a pass +
  per-light views, which also runs into the single-view cull, §4.8).
- No transparency ordering. The transmission pass loads opaque depth without writing
  (`g_framePasses[3]`, `vulkanMaster.c:160`) and draws blended geometry in arbitrary indirect
  order — no sorting, no OIT. Overlapping transparent things (nebulae, shields, glass, smoke)
  will show order artifacts. `PARTICLE` is declared but unimplemented, so there is no particle
  system at all yet.

### 4.8 Single view per frame (root for several features) - ADDRESSED, API deferred

`CullUBO` holds one `viewProj` and one frustum-plane set (`structs.h:331`); the whole GPU-driven
frame assumes one camera. No multi-view: no split-screen, no 3D minimap/inset, no security-camera
feeds, no portal/reflection views, and — notably — no shadow views and no cubemap captures, each
of which is "another frustum to cull against." DICE's influence in the corpus culled multiple
frustums at once; here it is one. Adding views means N cull passes or an N-frustum partitioning of
the indirect buffer. Root for any game wanting more than one viewport, and a prerequisite for
shadows.

### 4.9 Cull is frustum-only; no LOD, no occlusion, no screen-area test (root for the scale goal) - ADDRESSED

`cull.comp` does sphere-vs-frustum and nothing else (`isVisible:129`). There is no LOD selection
(one mesh per entity, no LOD chain, no impostors), no occlusion culling (the corpus's BF3
software-occluder idea is unrealized), and no screen-area/distance cull — even though
`docs/data-oriented.md` §3 explicitly recommends screen-area culling as the natural LOD threshold.
So every in-frustum entity draws at full mesh resolution regardless of distance or pixel
coverage. For a million-entity scene this is the central gap between vision and renderer: frustum
culling alone leaves potentially millions of sub-pixel entities all issuing full draws, and the
bounding volume is per-*mesh*, not per-entity (`meshBounds[entity.meshIndex]`, `cull.comp:157`), so
there is no per-instance tightening either. Extensible (add a screen-area test and a per-entity
LOD mesh array in the cull pass, additively) and high-leverage — this is probably the single most
important renderer addition for the stated scale, and the scoped-resolution LOD story has no
renderer support without it.

### 4.10 Cross-thread BULK_CREATE has no lifetime handshake (correctness gap for the real producer) - ADDRESSED

`RenderCreateBatch` is borrowed: the header says the render master "releases the batch when
consumed" (`anoptic_render_bridge.h:97`), but the render side never frees it — `render_apply_commands`
`memcpy`s out of it across `MAX_FRAMES_IN_FLIGHT` frames (`:712`) and there is no consumption-ack
event. It works today only because `initVulkan` drains synchronously on the same thread and frees
the batch *after* the drain loop (`vulkanMaster.c:1679-1686`). For the real async logic-thread
producer this breaks: the producer cannot know when all frames have applied the batch, so it
either frees too early (use-after-free as the render thread applies frame 1 and 2) or never. The
events ring has no batch-retired kind. Flag now: either give the batch arena-with-frame-lifetime
ownership, or add a `REVENT_BATCH_CONSUMED` ack. This is the one item here that is a latent
correctness bug, not just a scaling shape — it will bite the moment the stand-in producer in
`main.c` is replaced by the real `DisplayState` extract.

### 4.11 Input lives in the render world with no path to logic (root-for-now, planned) - ADDRESSED

`glfwPollEvents` runs on the render thread (`anoRenderThreadMain:801`); GLFW requires single-thread
window/event ownership, so input is physically render-side. The events ring has no input kind, so
keyboard/mouse cannot reach the logic master where gameplay lives. `main.c`'s logic loop is
literally blind to input today. The event bus + input plumbing is `notes.md` Step 8 (unbuilt).
This is the same return-channel-poverty as picking (§3.1) and the camera (§3.1): the render→logic
direction was built for slot retirement and nothing else. Design the render→logic contract
(input, camera/viewport, picking results, capacity/backpressure) as one coherent thing rather
than accreting event kinds.

---

## 5. Root causes, ranked

The findings collapse into a small number of foundational decisions. Ranked by how expensive
they get if deferred.

1. The render→logic return channel is two enum values. Picking (§3.1), input (§4.11), and the
   camera/viewport never reaching logic all stem from this, and they block the genre's defining
   interactions plus the attention-driven simulation LOD the whole engine is premised on. Design
   the full back-channel contract now (it is also `notes.md` Step 8's foundation). Highest
   leverage.

2. The five-field renderable vocabulary (§2.1) and the spin-only motion model (§2.2) together
   define what a renderable can express, and both are welded across six sites. Add a general
   per-slot instance-data channel and a small extensible motion-type enum before features start
   bolting onto the current shape.

3. Sparse-set-only storage forces scattered multi-component joins (§3.2) and pushes against the
   engine's own SoA doctrine. Decide the storage strategy for hot system groups (fat components
   vs an archetype option) before the core systems are written — this is the hardest item to
   retrofit.

4. Per-slot data is triplicated, host-visible, ×7-partitioned, and never shrinks (§4.3, §4.4,
   §4.5). This is the real VRAM and per-frame-compute scaling wall, and it contradicts the
   scoped-resolution promise of reclaiming cost on demote. The ×7 waste (§4.4) is a free fix
   today; device-local placement and high-water reclamation are the deeper ones.

5. Forward-loop-all-lights plus frustum-only, LOD-less culling (§4.7, §4.9) are the renderer
   architecture limits standing between the current small-scene PBR forward renderer and the
   million-entity stylized target the vision describes.

6. Whole capability layers are simply absent and shape everything built before them: spatial
   index (§3.4), relationships/hierarchy (§3.5), serialization discipline (§4.6), parallel-add
   (§3.6), and the job/scheduler layer (§4.1). Several are cheap to seed now (deferred-add,
   destroy hooks, POD/serialization policy) and bug-prone to retrofit.

7. One latent correctness bug to fix at cutover, not a scaling shape: cross-thread BULK_CREATE
   lifetime (§4.10).

---

## 6. What is well-founded (so effort is not misspent here)

To calibrate: much of the foundation is sound and genuinely extensible, and the limits above are
narrow against it.

- Generational `(index, generation)` handles with deterministic stale-handle rejection
  (`anoptic_ecs.h:40`) — exactly the corpus's recommended pattern.
- Chunked sparse-set stores with swap-and-pop and lazy chunk allocation (`ano_ecs.c`) — clean,
  cache-friendly for single-component scans, no per-entity waste for unowned components.
- Deferred structural mutation flushed single-threaded at a tick boundary (`ano_ecs_flush_structural`)
  — gives stable iteration and a natural place to add destroy hooks and deferred-add.
- The SPSC ring: head/tail on separate cache lines, monotonic indices so ABA-free by construction,
  acquire/release without CAS (`anoptic_render_bridge.h:196`) — textbook-correct, the right baseline.
- Render-owned stable slots with holes + frame-gated quarantine reuse (`render_slots.c`) — this is
  epoch reclamation with the frame counter as the clock; it correctly deletes the
  defragmentation/remap machinery the early drafts assumed.
- The `render_id → slot` indirection (`render_slots.h`) — lets the renderer relocate GPU data
  invisibly; the right seam, and the natural place to attach the instance-data channel of §2.1.
- Dynamic chunked capacity removing the old hard 10k ceiling (`ensureEntityCapacity`) — the growth
  mechanics (descriptor re-point, layout untouched) are correct; the concerns are placement and
  reclamation (§4.3, §4.5), not the growth itself.
- The mesh/vertex-shader dual geometry path keyed off `DeviceCapabilities.meshShader` — broad
  hardware reach without forking the resource model.

The throughline: the *plumbing* is well built; the *contracts it carries* are narrower than the
target genre needs. Widen the contracts — the renderable attribute channel, the motion vocabulary,
the render→logic back-channel, the storage strategy for joins, and the per-slot memory model —
while the plumbing is still small enough to change cheaply.

---

## 7. Future-proofed architectural directions

This section proposes concrete changes, each grounded in `docs/data-oriented.md` and aimed at a
named root cause from §5. One caveat must lead, because the corpus itself insists on it: it is
pointedly anti-speculative-generality. Acton's "you cannot future-proof — different data is a
different problem" and "solve the common case, not the generic case" are direct warnings against
building frameworks ahead of need. So "future-proofing" here means four disciplined things, in
priority order, and nothing else:

1. Widen the few load-bearing contracts that are cheap to widen now and expensive once code
   leans on them — the render↔logic channel, the per-slot instance block, the motion-type tag.
   These are seams, not frameworks.
2. Delete waste whose shape is already known — the ×7 partitions, realloc-and-copy growth,
   host-visible hot data.
3. Build the corpus's proven reusable structures only when the feature that needs them lands
   (`notes.md` Step 6 already says this): the spatial grid, the dual-queue, the reserve-commit arena.
4. Keep every concurrent operation to the three-phase shape with an immediately-identifiable
   linearization point (Scott; NBTC) so structures stay composable as they multiply.

Everything below obeys those four. None of it is generic machinery for its own sake.

Principle-to-proposal map:

| Corpus principle (`docs/data-oriented.md`) | Proposal | Discharges |
|---|---|---|
| Scott: queues for cross-thread; dual structures for blocking; epoch publish | 7.1 render↔logic channel + published snapshot | RC1 (§3.1, §4.11) |
| Kelley: encodings, not polymorphism; "every byte ×1M" | 7.2 instance block + motion-type tag | RC2 (§2.1, §2.2) |
| Acton/Meyers/Collin/Kelley: SoA linear scan; Lakos: match structure to access | 7.3 hot-group stores (no archetype rewrite) | RC3 (§3.2) |
| Fleury: reserve-commit; Lakos: placement, diffusion, per-subsystem arenas | 7.4 GPU memory: ×7 fix, reserve-commit, device-local, reclaim | RC4 (§4.3–4.5) |
| Collin: cull on screen area; grid of cells; brute force | 7.5 clustered lighting + LOD/screen-area cull | RC5 (§4.7–4.9) |
| Acton: project into a future command buffer; Collin: BF3 grid; Kelley: flat arrays | 7.6 cheap seams: deferred-add, destroy hooks, spatial grid, save policy | RC6 (§3.4–3.6, §4.6) |

### 7.1 A designed render↔logic channel and a published render snapshot (RC1)

Basis: Scott — cross-thread state transfer is a queue problem; the events ring is already SPSC
and ABA-free by monotonic indices; epoch publication is the same pattern as the slot quarantine.
Acton — project a request into a command buffer rather than querying synchronously.

Two parts.

(a) Publish a per-frame render snapshot to logic. The render world produces the camera and
viewport; logic needs them for picking, attention-driven LOD, and any screen-space test. Publish
lock-free (engine policy: no mutex) with a double buffer and an atomic released index — the
producer writes the inactive slot, then release-stores the index; the consumer acquire-loads the
index and reads that slot. Safe without tearing at frame cadence because logic never laps a
once-per-frame producer. This is epoch publication, identical in spirit to the slot clock.

```c
typedef struct RenderSnapshot {
    mat4     viewProj, invViewProj;   // invViewProj lets logic build world-space picking rays
    Vector4  frustum[6];
    uint32_t vpWidth, vpHeight;
    uint64_t frameId;
} RenderSnapshot;
// render: g_snap[(e + 1) & 1] = snap; atomic_store_explicit(&g_snapIndex, e + 1, release);
// logic : uint64_t e = atomic_load_explicit(&g_snapIndex, acquire); const RenderSnapshot *s = &g_snap[e & 1];
```

(b) Generalize the events ring from two enum values into a small typed back-channel, keeping it
SPSC (render is the sole producer of all of it, so ordering stays total and free): add `INPUT`,
`PICK_RESULT`, and `BATCH_CONSUMED` (the last closes §4.10) to `RenderEventKind`, with a tagged
payload. Input rides here; GLFW stays render-side (§4.11) and forwards events as they arrive.

Picking, two composable mechanisms:

- GPU id-buffer (pixel-exact, for hover): the geometry stage writes the renderable's `render_id`
  to an `R32_UINT` attachment (it already resolves the entity per fragment; store `render_id`
  alongside `EntityInfo`, or keep a slot→render_id reverse map). Each frame, copy the cursor
  texel to a host-visible byte and emit `REVENT_PICK_RESULT{render_id}` next frame. Cost: one
  attachment, one texel copy, one frame of latency, zero per-entity CPU work — it scales to any
  scene and handles overdraw/alpha correctly.
- Logic raycast (for region/box/AoE selection): logic builds a ray from `invViewProj` in the
  snapshot and queries the spatial grid (§7.6). No GPU change, no latency.

Use the id-buffer for "what is under the cursor" and the raycast for "what is in this world
region"; together they cover hover, click-select, drag-select, and targeting. This makes the
render→logic direction a designed contract instead of accreting one enum value per feature.

### 7.2 A per-slot instance block and a motion-type encoding (RC2)

Basis: Kelley — encodings instead of polymorphism, and "store the type you have the most of, make
it smaller" (every saved byte ×1M). The bridge's field-bit mask is already this principle applied
to traffic; extend it to per-entity payload.

(a) One per-slot instance block ends the five-field ceiling for good. Add a single tight SSBO
(global + cull layouts go from 9 bindings to 10 — a one-time arity bump in the three layout
builders and the GLSL), addressed by slot, grown by one extra `growBufferSet` line so it inherits
dynamic capacity:

```c
typedef struct InstanceData {      // 16 B = one std430 row; 16 MB at 1M slots ×3 frames is noise
    uint32_t tintRGBA8;            // multiplies baseColor — dissolves the per-tint material explosion
    uint32_t flags;                // selected / highlighted / team / faction bits, read by flat.frag
    uint32_t param0, param1;       // game-defined: anim phase, damage, build progress, heat...
} InstanceData;
```

Protocol: add `RFIELD_INSTANCE` and `RENDER_DIRTY_INSTANCE`. Every future per-entity visual
attribute now rides `flags`/`param` or a second block, never another bespoke binding — encodings,
not new descriptor arity. Crucially this also voids §2.1's worst case: a per-entity tint is no
longer a distinct material against the 10000-material cap, it is four bytes in the instance block.

(b) Replace the bare `angular_velocity` w-flag with a tagged motion encoding. `update.comp`
switches on the tag; closed-form families stay GPU-resident and never restream:

```c
typedef enum MotionType {
    MOTION_STATIC = 0,  // copy initial
    MOTION_SPIN,        // today's spin
    MOTION_ORBIT,       // today's orbit
    MOTION_LINEAR,      // pos = initial + vel * time           — drifting debris, fleets, projectiles
    MOTION_KEPLER,      // evaluate orbital elements at time     — real planetary/asteroid motion
    MOTION_STREAMED,    // CPU owns transform this tick          — physics/pathing escape hatch
} MotionType;
```

`MOTION_LINEAR` and `MOTION_KEPLER` are the direct cure for §2.2: they turn the per-tick teleport
flood for the genre's most common motions back into the advertised zero-traffic, sent-once case —
and `MOTION_KEPLER` answers the irony that the one motion a space sim needs most was the one not
parameterized. `MOTION_STREAMED` confines O(moving) traffic to genuinely arbitrary motion, which
the batched device-local upload of §7.4 then pays efficiently. "Where there is one, there are
many": most movers share a *type*; encode the type, evaluate it on the GPU.

### 7.3 Hot-group stores without an archetype rewrite (RC3)

Basis: Acton/Meyers/Collin/Kelley — multi-component scans must be linear SoA; Lakos — match the
structure to the access (DIV-LUC). And Acton's anti-generality: provide the option for the hot
bundles, do not rewrite the world into a universal archetype engine.

Keep the sparse-set ECS for the sparse/optional 90% (it is ideal there: high-utilization single
arrays = arena, sparse data = side table — exactly Lakos). Add a store group: components
registered together that share one dense index space and one `owners` array, so swap-and-pop moves
all members in lockstep and a group query returns N parallel dense arrays in identical order — a
mini-archetype for one declared bundle only.

```c
bool        ano_ecs_register_group(EcsWorld *w, uint32_t group_id, const EcsComponentId *ids, uint32_t n);
EcsGroupView ano_ecs_group(const EcsWorld *w, uint32_t group_id);  // .cols[k].data parallel, .count shared
```

Hot systems — physics integration, AI perception, economy, combat — iterate the group as true
SoA with no per-element sparse lookup, the linear scan §3.2 says they cannot have today. Honest
trade: group membership fixes co-residency, so a grouped component cannot also be removed
independently of its group without breaking the shared order; which components form a hot bundle
is a deliberate per-bundle decision — which is just Acton's "know your data." Side benefit: a
single shared order makes group reductions reproducible if destroys are deterministic (note that
float reductions over a column are order-sensitive — relevant only if lockstep/replay is ever a
goal). Stopgap with zero new code: a policy of fat hot components (`Body{pos,vel,...}` in one
store) is linear too, at the cost of the hot/cold split. Recommend the group for the few hot
bundles, fat components as the interim.

### 7.4 Per-slot GPU memory: kill the ×7, reserve-commit, go device-local, reclaim (RC4)

Basis: Fleury — reserve a virtual range, commit pages on demand, for non-relocating growth; Lakos
— locality is king, diffusion is the long-run enemy, partition per subsystem, placement is a
first-class payoff; Acton — make the type you have the most of smaller; Meyers — avoid
power-of-two stride collisions when sizing.

Four moves, increasing in effort.

(a) Delete the ×7 waste (free, do first). Size the indirect and compacted buffers by the count of
*drawing* pipeline types (FLAT, TRANSMISSION = 2 today) via a `pipeline → partition` remap, not
`PIPELINE_TYPE_COUNT` = 7. This reclaims over half the per-slot GPU footprint (§4.4) with no
architectural change.

(b) Reserve-commit growth (the corpus table's explicit refinement of this engine). Replace
realloc-and-copy + `vkDeviceWaitIdle` (§4.5) with one large sparse buffer
(`sparseResidencyBuffer`) whose pages are bound on demand via `vkQueueBindSparse` as the
high-water rises. The `VkBuffer` handle and its descriptor never change — no re-point, no idle
stall, no copy — and pages commit behind a fixed virtual range, which is Fleury's reserve/commit
in Vulkan terms. Gate it in `DeviceCapabilities` and keep today's realloc path as the fallback,
mirroring the mesh/vertex dual-path discipline the engine already uses.

(c) Placement: device-local hot data + a staging ring (Lakos). The per-frame-hot buffers the GPU
reads or writes every frame — `transform`, `entity`, `meshBounds`, `materials` — should be
`DEVICE_LOCAL`, with CPU updates flowing through a host-visible staging ring drained by transfer
(or read directly by `update.comp`). "Measure the line, not the call": the win is bandwidth on the
million-element hot read, not allocation speed. The same staging path gives `MOTION_STREAMED`
(§7.2) a single coalesced upload instead of N scattered host writes ×3.

(d) Cut the triplication where it is not needed (Acton make-it-smaller). Only the
GPU-regenerated-per-frame buffers (`transform`, `compacted`, `indirect`) inherently need
`MAX_FRAMES_IN_FLIGHT` copies. The persistent, rarely-written set (`initialTransform`, motion
params, instance data) can be single-buffered device-local, updated through staging behind the
frame-gated barrier the engine already has (the `pendingFrameMask`/quarantine epoch is exactly the
right tool, so this is consistent with the architecture, not a new concurrency model).

(e) Reclamation and scoped resolution (Lakos per-subsystem arena; the engine's own promote/demote).
Two moves. First, lower `slotHighWater` when a trailing run of top slots is free — cheap, and it
moves no live data, so it respects the deliberately-kept "stable slots with holes, no GPU defrag"
decision; it only shrinks the dispatch bound. Second, give each resolution region / star system its
own slot sub-range or buffer set so a demote releases it wholesale (`mi_heap_destroy` /
sparse-unbind) — the corpus's "demote = release/wink" and its diffusion defense (each subsystem
owns its memory) made concrete on the GPU. This is what makes scoped-resolution actually reclaim
render cost, which §4.5 shows it currently never does.

### 7.5 Scalable lighting and LOD/screen-area culling (RC5)

Basis: Collin — cull on screen area not distance, grid of cells, brute force on linear data; the
corpus table's explicit "add screen-area culling for the LOD threshold."

(a) Clustered forward lighting. Add a compute pass that bins lights into view-space froxels (a 3D
cluster grid), writing a per-cluster light-index list; the fragment shader then loops only its
cluster's lights. This turns `flat.frag`'s O(fragments × allLights) loop (§4.7) into
O(fragments × lightsPerCluster), lifting the usable-light ceiling from dozens to thousands — the
genre's actual requirement. It is Collin's grid-of-cells applied to lights, fits the existing
compute-driven frame (one pass, two SSBOs), and stays forward, preserving MSAA and the thin
material path rather than forcing a deferred rewrite.

(b) Screen-area cull and LOD in `cull.comp`. Project the bounding sphere to screen, reject below a
pixel threshold (free entity reduction at distance), and select a LOD mesh from a per-entity LOD
chain by projected size, writing the chosen mesh's draw. Both are additive to the existing cull
pass and constitute the renderer half of scoped resolution (§4.9) — the single highest-leverage
change for the million-entity goal. Add merged/impostor draws for the far tier later.

(c) Multi-view (§4.8) falls out: the cluster grid and the cull pass are per-view, so additional
views become "N cluster grids + N cull passes," a natural extension rather than a rewrite — and
the same machinery is the prerequisite for shadow views.

(d) Software/Hi-Z occlusion (Collin) once the above lands — it removes CPU and GPU for fully
occluded clusters (interiors, the far side of a planet). Larger; defer until measured need.

### 7.6 The cheap seams to land now (RC6)

Basis: Acton — project structural change into a future command buffer (the deferred flush is that
buffer, and the single-producer flush gives total order for free); Collin — the BF3 grid;
Kelley — flat arrays serialize in one `writev`.

- Deferred component add. Mirror the remove/destroy queues with an add queue that stages
  `(entity, comp, payload)` into a scratch arena and applies in `ano_ecs_flush_structural`. ~30
  lines, and it unblocks parallel attach (§3.6) — the asymmetry that currently bars worker threads
  from reacting by attaching state.
- Destroy hooks / relations. The flush is the linearization point: add an optional per-component
  `on_destroy(world, entity, comp*)` invoked there, and/or a small relations side-module (ChildOf
  and friends) that registers cascade cleanup at the flush. Unblocks hierarchy and cascade delete
  (§3.5) at the one place that can enforce them safely.
- Spatial grid = Collin's BF3 structure verbatim: a grid of SoA cells, `AtomicAdd`/bump claim,
  swap-trick removal. Build it once as a reusable system; it serves logic-side range queries
  (combat, AoE, flocking, neighbor finding) and the logic-raycast picking of §7.1. It is the
  single highest-value reusable structure for interactions (§3.4) and it is already designed for
  you in the corpus.
- Serialization discipline (adopt as policy now, costs nothing): components stay POD and
  pointer-free, variable data lives in arena side-tables keyed by entity, and the save image
  carries generations plus the render_id map. The dense stores then serialize in bulk with no
  per-entity work — Kelley's "four flat arrays → one writev." This keeps save/load tractable
  instead of impossible (§4.6); a single owning pointer in a component forfeits it.
- Job and event bus, when the scheduler lands (`notes.md` Step 5/8), not before: use Scott's dual
  data structure so workers sleep until work without busy-retry, keep ordered input on a monotonic
  M&S/Vyukov queue, and reserve the cache-line-striped structure (Step 5B) for bulk traffic.
  Design each job op as three-phase with an identifiable linearization point so the structures
  compose later (NBTC). This addresses §4.1/§4.2 but is explicitly later work.

---

## 8. Sequencing

Order matters because the real `DisplayState` extract will soon replace the `main.c` stand-in
producer and will lean on these contracts — and one of them is a latent bug.

Do before the real producer exists (contract- and correctness-critical):

1. Fix the `RCMD_BULK_CREATE` lifetime handshake (§4.10) — a use-after-free the moment the
   producer is asynchronous. Add `REVENT_BATCH_CONSUMED` or give the batch arena-with-frame
   lifetime.
2. Widen the render→logic channel and add the published snapshot (§7.1) — the extract needs the
   camera and the back-channel shape settled.
3. Add the per-slot instance block and the motion-type tag (§7.2) — the extract emits these;
   baking them in now avoids reworking the producer later.
4. Add `RCMD_BULK_DESTROY` / `RCMD_BULK_UPDATE` and an explicit ring-overflow policy (§3.3).
5. Seed the cheap ECS seams — deferred-add, destroy hooks, serialization policy (§7.6) — because
   they shape how every system is written.

Do after, incrementally, driven by measurement (additive, no contract churn):

- Hot-group stores (§7.3) when the first hot multi-component system needs linear iteration.
- The ×7 fix, then reserve-commit, device-local placement, and reclamation (§7.4), driven by
  measured VRAM and per-frame compute.
- Clustered lighting and screen-area/LOD cull (§7.5) as the scale push toward a million entities.
- The spatial grid (§7.6) when the first range-query system lands.

Throughout: measure the line, not the call (Acton, Kelley, Lakos) — cache-line utilization per
hot loop, wall-clock A/B on real workloads, and the long-running diffusion ratio — before and
after each change, so these remain decisions rather than guesses.

This discharges every root cause in §5 using only the corpus's own techniques — widening the few
expensive-to-change contracts and deleting waste of known shape, rather than building speculative
generality, which is itself the corpus's central instruction.
