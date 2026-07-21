<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager


This is the completion plan for the Anoptic resource manager: the full owner model, the full allocator hierarchy contest, and the full lock-free ticket transport. It preserves the useful machinery already landed, corrects the gap between names and implementations, and carries the design all the way to production consumers. There are no half-measures, no reduced contestants, and no declaration of victory over a system that was never actually built. Everything goes 100% all the way >:D

The owner's directives in `docs/resourcemanager-real.md` define the goal. `docs/resourcemgr/resource-manager-SoA.md`, `docs/resourcemgr/resource-manager-plan.md`, `docs/resourcemgr/resource-manager-unified.md`, `docs/references/lockfree.md`, and `docs/text/logger.md` provide research and historical design context. `RESOURCE_MANAGER_IMPL.md` is an implementation journal, not authority to weaken the goal. Where an older plan describes a caller-owned librarian, keeps three save generations, retains a registry mutex forever, or calls a reduced hierarchy complete, this plan supersedes that outcome.

## 0. Completion law

- A model name is a contract. Model A, B, C, D, or E may be used only when the complete ownership hierarchy described here exists in code, drives real engine consumers, passes the common correctness battery, and runs its own hostile benchmark. A reduced proxy is scaffolding and must be named as scaffolding.
- The contest implements all five models in full before it compares them. No favored model receives private shortcuts, no weak model is represented by a compile-time sketch, and no home benchmark is dismissed before it runs.
- The backing allocator domains own their resource registries and their resources. The manager does not merely tag default-heap allocations after the fact. Metadata, names, conditioned objects, payloads, derived resources, and teardown bookkeeping live in the hierarchy being measured.
- Transparent handles are read-only capabilities. Callers receive identity and immutable views, never permission to mutate registry state or allocator state.
- Resource consumption is explicit and ticketed. A destructive transfer, a worker consume operation, and an ordinary borrowed view are different operations with different lifetime contracts; none is disguised as another and none hides an unapproved copy.
- Lock-free means the shared data path contains no mutex and every operation has a stated progress class and a bounded full/empty result. Optional thread parking may use a platform wait primitive after the lock-free fast path, but no mutex protects the registry, handle publication, request queue, consume queue, completion queue, or ticket table.
- Cache layout is part of the algorithm. Atomics, producer cursors, consumer cursors, sequence planes, ticket state, and payload planes are placed deliberately with `ANO_CACHE_LINE` and `ANO_THREAD_LINE`; compile-time layout proofs and hardware-counter evidence are merge requirements.
- Every performance claim names the exact implementation, commit, build, platform, corpus, run count, and raw result. A disabled benchmark plus prose is not evidence. A synthetic proxy cannot stand in for the real consumer whose wound a model claims to solve.
- Correctness bars are gates, not weighted metrics. A model that loses bytes, serves a stale pointer, double-completes a ticket, wedges behind a dead producer, corrupts a save, or frees an allocator before its last reader is disqualified regardless of throughput.
- Completion means the engine actually uses the result: real levels open lifetime domains, real resources inhabit the winning allocators, real renderer/audio/world consumers use handles and tickets, and the prototype switch and losing production paths are removed.

## 1. Final ownership grammar

The resource manager is an owner, not a filesystem convenience wrapper. It owns logical identity, registry state, backing allocator domains, raw bytes, parsed and conditioned resources, derived-resource relationships, lifetime retirement, asynchronous work, consumption, durable saves, pack lookup, and hot-reload publication. Domain extensions own meaning inside the module: graphics parses and conditions graphics, audio parses and conditions audio, world resources parse and condition levels, and config resources validate and migrate settings.

The hierarchy has two layers. A minimal permanent handle directory keeps stable slot identity and generation/tombstone publication so stale copied handles can fail safely after an owning domain winks out. The substantial registry shard—names, hash tables, resource records, dependency metadata, placement metadata, and payloads—belongs to the backing allocator domain selected by the contest. The permanent directory must remain deliberately small; moving the whole registry into a global default heap and calling payload placement ownership is forbidden.

`anores_t` remains the transparent read handle shape unless an interface review proves a stronger layout: `{rid, slot, generation}` by value, sentinel zero, immutable views, generation retirement, no reference-count mutation on reads. The owner publishes a stable SoA snapshot for handle resolution; consumers never enter the mutable owner registry.

## A. Reconcile the specification and finish the functional resource manager

### A.1 Objective

Stage A establishes the honest baseline on which the allocator contest can run. It preserves sound completed work, relabels scaffolding truthfully, closes every functional gap that would otherwise make the contest synthetic, and leaves allocator placement behind a neutral internal interface. It is intentionally open-ended about local implementation tactics: follow the project's module boundaries, C23 conventions, platform layer, tests, and benchmark-first rules, and keep working until the complete behavior exists rather than stopping at the first compilable approximation.

### A.2 Current delta

| Area | Current implementation | Required complete state |
|---|---|---|
| Module and namespace | `anoptic_resources.h` and `src/resources/` exist; logical-path validation, ordered roots, prefix mounts, write-root shadowing, base resources, mount freeze, and platform read/write seams are real. | Preserve these invariants, finish platform validation on native Windows/Linux/macOS and 9P/SMB, and keep logger/crash filesystem clients outside the resource namespace as directed. |
| Whole-file reads | `res_read_all` treats size as a hint, reads to EOF in bounded chunks, aligns the buffer, and writes a guard NUL. | Preserve the byte-truth contract and extend the same correctness to ranged reads, pack reads, codec reads, and every backend. |
| Identity and handles | FNV-1a-64 identity, permanent dense slots, generations, sentinel views, single-copy lookup, release, and unload exist. | Move compiled naming sites to the intended SID path, make the public/read side stable without the prototype mutex, and keep all mutable registry operations inside the owning domain/service. |
| Registry storage | Rows, slots, and names use ordinary mimalloc and may move under `mi_realloc`; one mutex guards map, rows, payloads, groups, and statistics. | Registry shards must be allocated by the contestant's backing ownership domains, stable handle publication must not relocate under readers, and Stage C must remove the data mutex completely. |
| Multipool use | A real power-of-two multipool serves manager payloads at or below the 1 MiB threshold; larger payloads use direct mimalloc blocks. Production shaders and fonts therefore use the group-0 multipool, while many large model/texture/conditioned blocks bypass it. | Placement must be owned by the selected full hierarchy, not one global threshold. Metadata, variable objects, bulk data, consumable transfers, and oversize cases each need the allocator class promised by that model. |
| Lifetime hierarchy | Internal ambient groups create per-group multipools and retire them by `multipool_destroy`, but only tests and `anotest_resbench` call `res_group_begin/end/retire`; every production load uses immortal group 0. | Real level/world/streaming consumers must create explicit lifetime domains, resources must enter them by explicit token rather than a cross-thread ambient global, and retirement must exercise the winning production hierarchy. |
| Wink-out | Parse scratch uses a scoped heap and monotonic arena correctly. Resource groups use `ano_mem_parent_default()` and walk chunks on destroy; they do not own a backing `mi_heap_t`. | Each wink-out model must own its root heap on one owner thread and retire the complete domain through the promised heap/domain operation after the reclamation barrier. |
| Model E claim | The current path is one global multipool plus optional scoped multipools and a monotonic glTF scratch arena. Registry metadata remains outside, bulk data uses direct blocks, role-specific pools/stripes do not exist, and production opens no scopes. | Relabel this path as hierarchy scaffolding. Full Model E is implemented only in Stage B as the complete C×D hybrid with real lifetime domains, real role split, real backing heaps, real consumers, and real zero-copy transfer classes. |
| Allocator roster | Multipool, monotonic, and fixed pool implementations exist and are well tested; the fixed pool has no production resource consumer; stripe remains a documented idea. | Give every shipped allocator a real resource consumer and home benchmark. Implement cache-striped placement where the SoA registry/ticket/payload design consumes it; omit no promised tool merely because the prototype can compile without it. |
| Graphics extension | glTF parsing, buffer loading, validation, geometry/material/node conditioning, logical image paths, image decode, and offset-shaped scene blocks exist. The renderer copies a conditioned scene into legacy `ModelAsset` allocations and unloads the scene handle; consumers do not yet retain uniform manager views/handles. | Complete the promised graphics domain: skins, skeletons, animations, embedded images, every supported material/resource class, conditioned/baked equivalence, and transparent or ticketed delivery to the renderer. No path-based parser may remain as an alternate asset-loading route. |
| Fonts and images | Engine fonts are loaded from manager blobs through `FT_New_Memory_Face`, but the public path-based `ano_text_font_load`/`FT_New_Face` route remains. Encoded images are manager resources, while decoded pixels are caller-owned mimalloc blocks. | Remove or strictly quarantine path-opening asset APIs, put font/image meaning behind the appropriate resource extension, and define decoded/conditioned placement and consume semantics through the contest rather than accidental caller ownership. |
| Other resource domains | No complete world/level, config/keybinding, script, audio-resource, or save-migration consumer exists. The demo models remain hardcoded string literals in renderer startup. | Land real data-driven level/world resources, config and keybindings with quarantine/migration, audio/resource integration where the consumer exists, and resource-declared dependencies/prefetch. Eliminate hardcoded demo asset ownership from renderer code. |
| Durable writes and saves | Atomic replace, framing, validation, corruption fallback, fault injection, orphan recovery, temp GC, save statistics, and user-directed deletion exist. Saves are never auto-deleted after the later correction. One global mutex serializes every slot, and load retains only the newest 64 normal candidates plus eight orphan temps in memory, so enough corrupt recent generations can hide an older valid save. | Preserve this machinery, make first-valid-generation fallback true without a fixed candidate blind spot, add actual config/keybinding/save-migration clients, replace global serialization with owner-side per-slot ordering, route all writes through the final owner/ticket system, and keep copy-at-submit plus exact completion status. |
| Allocation path | Small reads first allocate a default-heap buffer and then copy into the multipool; conditioned graphics allocate a default-heap result and adoption may copy it again; only direct-class blocks are read/adopt-to-home. | Add allocation-destination planning so each contestant can read/condition directly into its home or into an explicit staging/transfer allocation. Count every copy; no copy is invisible in a benchmark claim. |
| Hierarchy contest | Five models were described, but only A and a reduced scoped-multipool prototype were implemented. The recorded “D side” used the identical 2 MiB direct-allocation path in both builds. | Stage B implements A, B, C, D, and E fully and independently, then runs all home and hostile benches. The current A-versus-scoped-pool result remains historical prototype evidence only. |
| Interconnect | Fixed-stride SPSC/MPSC/SPMC/MPMC rings, a unique-number dispenser, a versioned byte publisher, bridge policy, and waiter tests exist. The per-item SPMC claims a shared head before copying; a stalled consumer can delay slot reuse. `anoseqpub` version-checks ordinary bytes copied with `memcpy`, which is not sufficient for strict C23 publication if a writer can mutate those bytes concurrently. `anobridge_waiter` still parks through a mutex/condition variable. | Complete the logger-derived cache-striped primitive, use immutable atomic publication rather than a racy byte seqlock, provide PAL atomic wait/notify only outside linearization, migrate the logger or its shared core so there is one proven implementation, and make resources use the interconnect end to end in Stage C. |
| Async resource path | There is no loader/service thread, resource request grammar, public async API, ticket lifecycle, pump, completion lane, priority band, ranged request, or streaming oracle. | Stage C supplies the complete request/consume/completion system, one-owner registry, exact ticket states, bounded backpressure, and runtime integration. |
| Streaming economy | No production 512 KiB chunk pool, ranged load path, codec pipeline, decompression worker, or measured queue-depth backend exists. | Implement and prove chunked/ranged reads, LZ4 and plain zstd paths, worker pipelining, raw bypass, and the backend ladder under the final ticket grammar. |
| Pack and bake | The scene block is offset-shaped, but there is no `anopak`, builder, checked TOC, mount, baked model, runtime fix-up path, or hot reload. | Implement the full frozen pack format, deterministic builder, checksum/refusal behavior, loose-file shadowing, baked model path, zero-runtime-parse shipped scene, and ticketed hot reload. |
| Verification evidence | Resource, pool, group, graphics, fault, collection, bridge, and optional benchmark binaries exist; the journal records several successful runs. Remote CI is absent and the heavy benchmarks are disabled by default. | Keep the tests, correct their claims, add missing end-to-end oracles, store raw benchmark reports, run the required native/sanitized/platform matrix, and make completion independently reproducible. |

### A.3 Multipool truth

The specialized multipool is not imaginary: `ano_mem_multipool_alloc` is on the live resource payload path, its power-of-two classes and sized frees are implemented, its statistics and chunk ownership are tested, and manager-owned shader/font/small-resource bytes currently inhabit the group-0 instance. The monotonic parse arena is also a genuine use of the planned allocator composition.

The hierarchy is nevertheless incomplete. Current production therefore runs the effective Model A/group-0 path: its multipool is immortal, production creates no lifetime groups, group pools have default-heap parents instead of owning heaps, rows/names/hash tables bypass the hierarchy, large resources bypass it, decoded images bypass it, fixed pools and stripes have no resource client, pooled destructive release copies, and the so-called E path does not implement D's role split. Stage A must preserve the real primitives while removing every misleading completion label.

### A.4 Bring the system into line

- Establish this document's terminology in code and documentation: rename the current E switch/scaffold so no test, log, environment variable, or journal entry presents it as the full contestant; annotate historical results rather than deleting history.
- Introduce a neutral internal ownership/placement interface capable of expressing all five complete models without changing public handle semantics or giving one contestant private API advantages.
- Finish the resource-domain functionality needed to exercise ownership honestly: data-driven levels and lifetime domains, full graphics conditioning including skinning/animation, config/keybindings, save migration, dependency disclosure, typed derived resources, and real renderer/audio/world consume sites.
- Finish the synchronous reference versions of ranges, chunking, codecs, pack lookup, deterministic baking, loose shadowing, and hot-reload validation so their correctness does not depend on Stage C's concurrency.
- Make every load, parse, condition, copy, promotion, hand-off, and retirement visible to allocator statistics and benchmark counters; the contest cannot judge work it cannot see.
- Remove alternate asset IO and parsing paths outside resource extensions, including legacy path-based font loading unless it is explicitly retained as a tool-only importer that still enters through the namespace.
- Resolve cross-lifetime sharing without silently adding reference counts. Promotion, duplication, shared immutable domains, or another explicit policy must be implemented and benchmarked by the models whose wound requires it.
- Preserve the strong completed correctness work: hostile path grammar, ordered namespace, EOF-truth reads, generation sentinels, collision refusal, durable replace, save framing, corruption degradation, orphan recovery, and platform abstraction.
- Update the older plans and implementation journal when the implementation changes, but never rewrite an old claim to make it appear prescient; record the correction and the evidence.

### A.5 Stage A exit bar

Stage A ends only when all non-allocator-specific resource behavior is complete behind the neutral interface, every promised production resource class has a real owner and consumer, the synchronous reference path passes its complete correctness suite on native Windows/Linux/macOS plus the remote-filesystem floor, and the current hierarchy is described honestly as scaffolding awaiting the contest. No model is declared shipped at this 
point.

## B. The allocator hierarchy contest: A, B, C, D, and E fight

### B.1 Contest law

Every contestant implements the same resource semantics, handles the same real corpus, uses the same parser/codec/pack code, and exposes the same instrumentation. Shared code is encouraged for correctness machinery, but sharing may not erase the ownership difference being tested. Each model must allocate its registry shards, names, metadata, payloads, derived objects, and teardown records according to its own hierarchy; changing only one payload pointer is not a contestant.

Before timing, every model must pass the entire resource correctness battery, ASan/UBSan, TSan where concurrency is active, fault injection, pack corruption, stale-handle, group retirement, consume hand-off, and native platform tests. A failed correctness gate removes the model from performance comparison until fixed; it does not lose by default with an unfinished implementation.

### B.2 Common contestant contract

- Each model is selectable in one build through a test-only model selector; production contains no permanent runtime polymorphism requirement beyond what the contest needs.
- Each model owns a complete allocator-domain descriptor and registry shard, while the minimal stable handle directory remains common and separately measured.
- Each model supports engine-forever, level/world, streaming/transient, save/config, and tool/import lifetimes, including explicit cross-lifetime policy.
- Each model supports variable metadata, small payloads, large payloads, parse staging, derived resources, readonly views, destructive/worker consumption, reload, promotion, and bulk retirement.
- Each model reports requested bytes, serving bytes, live/peak bytes and blocks, chunk bytes/count, parent calls, class hits, oversize hits, internal/external fragmentation, copies and bytes copied, promotion cost, release mode, retire cost, registry probes, cache/TLB counters where available, and residual footprint after every cycle.
- Every allocation and free is attributed to a resource kind, lifetime, role, and operation so aggregate wins cannot hide one catastrophically weak class.
- Raw `mi_malloc`, default-parent allocation, a private scratch heap, or any other escape is legal only when it is a declared primitive of that contestant, owned by its root, charged to its reports, and subjected to its teardown and transfer rules. A hidden allocator cannot absorb a model's predicted wound.
- Each model runs with real resource scopes in the engine, not only direct calls from a benchmark TU.

### B.3 Model A — one complete multipool

Model A owns the entire resource system with one backing heap and one full multipool hierarchy. Registry rows, names, dependency records, conditioned metadata, ordinary payloads, parse/import scratch, and allocator bookkeeping all live in this ownership domain; only objects whose external consume contract requires an independently transferable block may use an explicitly tracked transfer allocation from the same root. Category and lifetime are registry fields, not allocator boundaries. Unload and group retirement perform the required per-resource destruction without pretending to wink out a subset; a hidden monotonic lifetime heap may not make Model A look like Model C or E.

Model A's home is steady mixed-size churn and maximum reuse/density. Its hostile ground is repeated partial lifetime retirement, cross-level residue, very large live-set changes, and category-local traversal. The implementation must make its predicted wound measurable rather than outsourcing lifetime work to a hidden per-group allocator.

### B.4 Model B — full kind-major ownership

Model B creates complete backing domains by resource kind: graphics models/meshes, textures/images, shaders/pipelines, fonts/text, levels/worlds, saves/config, audio, scripts, and any other real class established in Stage A. Each kind owns its registry shard, names, metadata multipool, payload policy, derived objects, and stats; its class-specific size histogram may tune multipool classes without changing public semantics. Whole-kind teardown is a real domain wink-out where the class contract allows it.

Model B's home is kind-local iteration, kind-specific size distributions, cache/TLB locality, subsystem restart, and fragmentation over a long mixed-lifetime soak. Its hostile ground is level retirement spanning many kinds, shared resources, cross-kind dependencies, and promotion. No kind may be represented by a tag in the Model A pool.

### B.5 Model C — full lifetime-major ownership

Model C creates complete backing domains by lifetime: engine-forever, world/level, streaming window, transient/import, save/config, and other demonstrated lifetimes. Each domain owns its registry shard and one or more multipools required to serve its full size distribution. Opening a level in the real engine opens its domain; every disclosed dependency enters that domain or an explicit shared/promoted domain; retirement invalidates the published generations, crosses the reclamation barrier, and destroys the backing heap in the promised wink-out.

Model C's home is repeated real level/world load-unload, streaming-window turnover, residual footprint, retire latency, and lifetime-local traversal. Its hostile ground is cross-level sharing, promotion, long-lived resources referenced by short-lived worlds, and category iteration. The current ambient test group is not Model C; lifetime tokens must be explicit and production-owned.

### B.6 Model D — full role-split ownership

Model D separates roles completely. Stable registry metadata, names, dependency graphs, small conditioned structs, and control records live in metadata multipools; variable ordinary payloads live in appropriate payload multipools; fixed streaming chunks live in bounded `ano_mem_pool` instances; bulk SoA arrays and ticket/consume planes use cache-striped placement; transfer-eligible GPU/audio/world payloads use allocations that can move ownership without an interior-pointer copy. Every resource records the role domains it spans, and retire/release handles all of them exactly once.

Model D's home is ranged streaming, parse-to-condition-to-consume throughput, GPU/audio hand-off, zero-copy rate across the full size distribution, SoA traversal, and bounded chunk reuse. Its hostile ground is metadata-heavy tiny resources, multi-domain teardown complexity, bookkeeping overhead, and fragmentation between roles. A 2 MiB direct allocation shared with every other model is not evidence for Model D.

### B.7 Model E — the full C×D hybrid

Model E is implemented as the complete hybrid originally promised: lifetime-major backing heaps, and inside every lifetime domain a role split of registry/metadata multipools, variable payload multipools, bounded streaming pools, cache-striped SoA storage, transfer-compatible consume allocations, and transient monotonic staging. The real level domain owns its registry shard and all level resources; the engine domain owns permanent resources; shared immutable domains and explicit promotion answer cross-level sharing; worker staging winks out after adoption; final retirement invalidates handles, waits for the lock-free reclamation condition, and destroys the whole backing heap.

Model E's home is the combined production workload: level disclosure and parallel ingest, continuous streaming, derived-resource conditioning, renderer/audio consumption, hot reload, promotion, and world retirement under frame load. Its hostile ground is control complexity, duplicated slack across small domains, over-partitioning, promotion traffic, and total metadata cost. Model E receives no credit for components that do not exist and no presumption of victory because it was favored on paper.

### B.8 Corpus and battlefields

- Allocation microstructure: every size-class boundary, alignments through 4096, randomized frees, phase changes, oversize transitions, exhaustion, reset, destroy, promotion, and adversarial distributions derived from recorded real histograms.
- Steady-state churn: long-running mixed requests with real resource sizes, repeated hits, unload/reload, duplicate identity, hot reload, and controlled working-set pressure.
- Kind-major workload: dense iteration and mutation of each real category, subsystem teardown/restart, category-specific histograms, and cross-kind dependency walks.
- Lifetime-major workload: at least dozens of complete world/level cycles over the real scene corpus, shared-resource promotion, overlapping worlds, save/config survival, streaming during retirement, and residual measurement after every cycle.
- Role-split workload: actual ranged IO, codec output, mesh/texture/font/audio conditioning, mixed-size destructive consume, GPU staging, audio buffers, fixed chunks, and pointer-equality ownership oracles where zero-copy is promised.
- Full-engine workload: foreground-verified runtime scenes with rendering, text/UI, audio, streaming, hot reload, and frame-time capture; allocator work is correlated with wall and pass timing rather than run in isolation only.
- Pack/bake workload: loose versus packed lookup, deterministic bake, cold/warm mount, checksum rejection, baked model load/fix-up, and shipped zero-parse scene.
- Remote-filesystem workload: 9P and SMB correctness and latency with dishonest size/mtime, short reads, delayed visibility, missing directory sync, and interrupted writes; correctness is compared separately from local-drive throughput.
- Contention workload: owner/worker/consumer topologies used by Stage C, including false sharing, producer/consumer imbalance, preemption, full/empty pressure, cancellation, and shutdown.
- Corpus completeness is executable: every preregistered real asset, level, trace, network condition, and external-consumer case must be present and validated before a run can produce a comparison table. A missing case makes the benchmark fail nonzero rather than silently skip the contestant's difficult ground.

### B.9 Measurement discipline

The contest preregisters scenarios, metrics, corpus revisions, run counts, warmup, outlier policy, platform/build matrix, and decision rules before collecting winner data. Linux x86-64, native Windows x64, and Apple Silicon macOS are required; Debug, Release+LTO, ASan/UBSan, and TSan responsibilities are stated per run. ARM results include disassembly proof for LSE atomics when atomic performance is discussed.

Each timed series records p50, p95, p99, p99.9, maximum, throughput, wall time, CPU time, bytes/sec, operations/sec, live/peak/residual memory, copies, allocator parent traffic, cache misses, coherence traffic where available, TLB behavior, and frame impact. Report both cold and warm behavior. Preserve raw machine-readable samples under `docs/benchmarks/` beside a terse interpretation and exact reproduction command.

No contestant wins from one aggregate score hiding a fatal wound. Correctness and bounded-memory requirements are absolute. Performance decisions use the preregistered real workload priorities, a Pareto comparison, and a held-out production trace that was not used to tune allocator parameters; if the result is ambiguous, improve the complete contenders or gather a discriminating workload and rerun. Do not invent a reduced hybrid after seeing the data.

### B.10 Bitter end

When the evidence names a winner, rerun its full suite and runtime verification alone, then remove A/B/C/D/E production switches and every losing path from the engine. Keep the designs, raw reports, and commit references as scholarship; keep a standalone baseline harness only if it remains useful and cannot leak into production. The winner becomes the one allocator hierarchy, the public documentation is updated to name it, and all future work targets that hierarchy rather than maintaining five half-engines.

### B.11 Stage B exit bar

Stage B ends only when five full implementations have fought on every battlefield, the decision is reproducible across the required platforms, the winning hierarchy owns real production registries and resources, true lifetime teardown works, mixed-size consume behavior is proven, losers are removed from production, and no “v1,” proxy, degenerate case, or unimplemented side is carrying the winner's name.

## C. Convert the winner to the total lock-free handle and ticket system

### C.1 Objective and topology

Stage C changes how work and ownership move without weakening what Stage A and B established. The winning backing allocator domains remain the owners of registry shards and resources. One resource owner/service thread mutates those domains and performs or schedules IO; arbitrary engine threads submit requests through lock-free ingress; readonly handles resolve through a published SoA directory; the owner publishes consume work through cache-striped SPMC lanes; consumers return completion through the cheapest correct topology; every operation is correlated by a generational ticket.

The default topology is deliberate: registered submitters use cache-aligned producer-local SPSC ingress lanes drained fairly by the owner, decomposing MPSC without a shared reserve-to-publish gap; a shared many-producer fallback is allowed only if it has a strict helping/recovery proof. SPMC consume lanes distribute homogeneous work from one owner to many consumers. Known workers return completions on SPSC lanes; any shared MPSC return path meets the same strict proof. Separate consume lanes by capability—graphics upload, audio decode/mix preparation, world conditioning, generic workers—rather than filtering incompatible work inside one FIFO.

### C.2 Lineage and algorithm choice

Michael–Scott supplies the foundational lock-free FIFO, helping, linearization, ABA, and safe-reclamation lessons; it remains a correctness/reference baseline, not an automatic production choice. A node queue would import hazard/epoch reclamation into the queue itself before resource reclamation even begins. The bounded array lineage is the primary fit: Vyukov per-slot sequences, monotonic cycle numbers, FAA ticket allocation, and the LCRQ/SCQ/wCQ lesson that common-path work should claim array positions productively and reserve heavier helping for a proven starvation case.

The current `anoring_spmc` is useful prior art but not the finished resource consume structure. See also `ano_log.h` and its (very fast) implementation. The final design must incorporate the logger's cache-aligned striping: claim and publish useful groups of work per coherence transfer, isolate hot cursors by `ANO_THREAD_LINE`, separate atomic control/sequence planes from dense payload planes, and prevent consumers from causing false sharing in resource metadata. Stripe width and control layout are selected by proof and benchmark, not by aesthetic guess.

### C.3 Cache-striped SoA consume structure

- The consume queue is bounded and array-based and carries compact generational ticket values only. Durable operation state, result, ownership disposition, and acknowledgement live in the ticket table, so a slow job never pins a dispatcher stripe; no hot-path nodes, allocation, or safe-memory-reclamation burden exists inside the queue.
- Producer and consumer cursors occupy distinct `ANO_THREAD_LINE` regions. Immutable capacity/mask data does not share either hot line.
- Control is SoA: each cache-line-aligned stripe has one counted `{lap, valid, claimed}` control word, while compact ticket values occupy a separate plane. Ticket-table planes hold handle, operation, destination, range, pointer/offset, size, status, result, and ownership token away from the dispatcher control line.
- The single producer owns its tail and pays no RMW. When a stripe's prior lap is fully claimed, it fills a full batch or latency-flushed partial batch, then publishes the counted valid set with one release store. Consumers acquire the control, speculatively copy one or more unclaimed ticket values into thread-local storage, then use one CAS against the same lap/valid word to add the corresponding claim bits. The successful CAS is the claim linearization point; after it the consumer needs no stripe access and later ACK/RETURN affects only the ticket table. A failed CAS discards the local copies and proves that another consumer progressed or the lap advanced.
- Copy-before-claim must itself be C23 data-race-free across lap reuse. The leading design stores each compact ticket cell as one lock-free atomic machine word: the producer fills it with relaxed atomic stores before release-publication, consumers use relaxed atomic loads after acquiring control, and the counted CAS validates the lap after the copy. If atomic ticket cells do not win, replace the protocol with another proven single-word counted/helping design; ordinary non-atomic speculative reads racing producer reuse are forbidden.
- Stripe reuse begins when every valid claim bit is set, not when the resource jobs complete. A consumer preempted before CAS owns nothing; one preempted after CAS already has its ticket locally and cannot wedge the stripe. A worker that dies later may strand that ticket's operation and require a disposition-specific recovery policy, but dispatcher progress continues and exactly-once external effects are never generically replayed.
- Stripe reuse is cycle-tagged. ABA is excluded by a process-lifetime width proof or a refusal/global-quiescent-reset rule. If one native control word cannot carry a safe lap and bitsets, use SCQ-style single-word indirection; never silently wrap and never depend on non-portable double-wide CAS.
- Partial final stripes, cancellation, failure, shutdown, and producer/consumer preemption are first-class states. A gap cannot wedge later work indefinitely. If the chosen bounded protocol cannot recover from a producer dying in its claim-to-publish window, that window is restricted to bounded plain stores like the logger and the limitation is stated; otherwise SCQ/wCQ-style helping is implemented and benchmarked.
- The logger is migrated to the shared striped primitive or both systems share a lower-level stripe reservation/publication implementation with identical proofs. Two subtly different copies of the same cache algorithm are forbidden.
- Static assertions prove alignment, plane offsets, stripe capacity, atomic lock-freedom on supported targets, and ticket packing. Release builds retain cheap counters sufficient to diagnose full/empty pressure and stripe utilization.

### C.4 Transparent readonly handles

The read path stays simpler than the consume path. `anores_t` indexes a stable permanent publication directory. The owner finishes an immutable descriptor—counted owner/domain identity, immutable data pointer or offset, byte size, typed-view metadata, state, rid, and row generation—then release-publishes one lock-free atomic pointer to it; an all-atomic snapshot is an allowed measured alternative. A reader acquire-loads the immutable descriptor, verifies owner/rid/slot/generation/state, and returns an immutable view or the empty sentinel. Current `anoseqpub` may not publish resource rows as-is: versioned `memcpy` of ordinary storage that a writer can modify concurrently is a C23 data race. The reader never touches the mutable hash map, allocator, dependency graph, or owner queue on a cache hit.

Published rows are stable and SoA; the current relocatable `mi_realloc` row array is not acceptable. Growth uses non-moving chunks, a bounded preallocated directory justified by population evidence, or another proven publication scheme. The substantial registry data remains in the winning allocator domain; the stable directory stores only the routing and tombstone information required for transparent handles. Owner identities and slots are monotone for the process or counted by a generation carried in every handle; owner, row, ticket, and stripe generation wrap causes refusal or a global-quiescent reset, never silent reuse.

Readonly lookup is not permission to reclaim concurrently. Expose an explicit read scope such as `read_begin/read_end`, or a precisely proven frame/pump scope, so `ano_res_bytes()` never returns a raw borrowed pointer without an announced lifetime. Handle generation retirement first removes publication, tags the old immutable descriptor and payload with a retire epoch, then waits until every registered reader lane is quiescent or beyond that epoch before the backing domain or block dies. A stalled lane pins reclamation rather than permitting reuse; lane unregistration is part of thread shutdown. Stale access always observes a sentinel and no reader can dereference reclaimed memory.

### C.5 Tickets

The ticket is an eight-byte generational capability, conceptually `{index, generation}` even if the final bit packing changes. FAA may dispense monotonic identities, but uniqueness alone is not a lifecycle: every ticket slot has a bounded state machine and cannot be reused until completion has been observed and acknowledged.

Minimum ticket states are FREE, RESERVED, SUBMITTED, ACTIVE, COMPLETE, FAILED, CANCELLED, ACKNOWLEDGED, and RETIRED where the operation needs them; equivalent compressed states are allowed only with a written transition proof. Every accepted ticket reaches exactly one terminal result. Rejected submission leaves caller ownership untouched. Shutdown completes or cancels every accepted ticket and leaks none.

Tickets correlate load, range, parse, derived-resource, consume, release, save, reload, and cancellation work. A ticket carries status separately from `anores_t`: missing resource, invalid handle, stale generation, IO failure, parse failure, codec failure, queue full, cancellation, and successful sentinel-like empty content are not conflated.

### C.6 Request ingress and owner service

Registered callers submit fixed-size request descriptors through bounded cache-aligned SPSC lanes; the owner drains active lanes fairly and registration/unregistration is explicit. An unregistered-producer fallback and any shared completion lane use an SCQ/LPRQ/helping-grade bounded algorithm or carry a written proof that a stalled producer cannot wedge a reserved slot; the current bare reserve-then-publish MPSC is not assumed strictly lock-free. Large or variable request data uses an explicitly owned envelope or a shared cache-striped variable-record primitive; allocation and ownership transfer occur before publication, and a failed enqueue leaves the envelope with the caller. Requests include logical identity/path ownership, operation, range, band, destination lane, lifetime token, and ticket.

The resource owner owns the winning allocators, registry shards, duplicate-load state, save sequencing, pack mounts, hot-reload swaps, and publication directory. Duplicate GETs coalesce behind one physical operation and complete every waiter with the same generation. No mutex guards the map because no other thread mutates or reads it directly. Owner-only direct functions prevent synchronous wrappers or worker callbacks from recursively submitting to the owner and deadlocking.

The public asynchronous surface includes submit, poll, pump/acknowledge, cancel, and consume operations; exact names freeze only after an interface review. Existing synchronous calls become bootstrap/tool wrappers over tickets or owner-only primitives and must not reintroduce a data mutex. Blocking wait on a ticket is built on poll/pump plus an optional platform wait, never by holding registry state.

### C.7 Resource consume

Ordinary handle reads borrow immutable manager-owned memory. Resource consume is an explicit ticketed operation published by the manager to the appropriate SPMC lane. A consumer claims it exactly once, validates the handle generation captured by the ticket, performs the typed operation, and completes with a result/ownership disposition.

Two consume classes are explicit. Borrow-consume lets a worker or subsystem process immutable manager-owned data while the ticket pins its retirement epoch; completion releases the pin and memory remains in the manager. Destructive-consume first withdraws the live handle publication and waits for every pre-existing read scope to cross its grace point, then publishes a counted transfer token—not an already-retired ordinary handle—to the SPMC lane. It is legal only for a placement class whose allocation can be transferred and reclaimed by the recipient without copying or freeing an interior multipool pointer. The allocator contest must therefore give consumable resources a transfer-compatible home. Hidden pooled copy-out is not destructive zero-copy.

Derived-resource consume may return a new manager handle instead of external ownership: a worker parses/decodes/conditions in its own staging allocator, sends the result and ticket back, the owner adopts it into the winning home, publishes the derived handle, and retires staging. Parser/worker allocators stay single-owner throughout; memory crosses threads only by an explicit transfer message.

### C.8 Reclamation and lifetime domains

The winner's allocator domain may wink out only after its published rows are invalidated, all accepted tickets touching it are terminal, all destructive transfers have completed, and every registered readonly-consumer lane has crossed the required grace point. Implement an engine-appropriate epoch/interval scheme or equally strong quiescent protocol; state the stalled-thread behavior and bound retained memory. A frame pump is a natural quiescent point but is not assumed sufficient until every non-frame consumer participates.

Resource reclamation is distinct from queue reclamation. The bounded ticket rings reuse in-place slots through cycles and need no node SMR; resource payloads and registry shards still require a grace condition because transparent views can outlive the lookup instruction. Tests must independently attack both problems.

### C.9 Backpressure, priority, and progress

Every bounded operation defines full/empty behavior. Async submit and consume never silently drop and never spin without a cap; false-on-full returns ownership to the documented side. Completion capacity is tied to the outstanding-ticket bound so a completed operation always has somewhere to go. High-water and saturation telemetry is release-visible. Blocking wrappers may park only after publishing their reservation and only through a platform abstraction over atomic wait/notify or an equally proven scheduler event; parking remains outside every linearization path, and the current mutex/condition-variable bridge waiter is not the final resource mechanism.

Implement the two promised bands, NOW and LATER, without head-of-line ambiguity. A blocked wait on LATER may publish a boost through the control path. More bands, byte budgets, work stealing, parallel IO owners, io_uring, and IOCP remain later rungs only if the complete two-band system and its starvation benchmark prove the need; “later rung” never excuses an incomplete current rung.

State progress precisely: ticket issuance may be wait-free when it is one FAA; bounded enqueue/dequeue may be lock-free or wait-free according to the selected algorithm; readonly handle resolution should be wait-free under a stable publication; disk IO and parsing are asynchronous work and do not inherit a lock-free completion-time promise. Verify ARM LSE code generation before attributing FAA scalability to Apple or generic aarch64 hardware.

### C.10 Complete streaming, pack, and hot reload on tickets

- Rung 0 ships completely: one owner schedules blocking ranged reads, platform `pread`/equivalent and advisory access where valid, bounded 512 KiB chunk pools, worker decode, per-consumer completion, and exact request telemetry.
- LZ4 latency and plain zstd bulk paths operate inside the ticket pipeline, overlap read/decode, bypass already-compressed data, and beat raw-drive effective bandwidth on the preregistered corpus or are removed with the failed claim recorded.
- `anopak` lookup, checksum validation, aligned payload reads, codec selection, baked model fix-up, loose-file shadowing, and deterministic builder all use the same request/ticket grammar; packed and loose results are byte/typed-view equivalent.
- Hot reload detects candidate changes, confirms content rather than trusting mtime, loads a new generation out of place, validates it fully, publishes at a safe boundary, and retires the old generation through the grace protocol. Readers see old-complete or new-complete, never a mixed object.
- Parallel IO workers begin only after the single-owner queue-depth benchmark shows the device idle while work waits. The completion shape does not change when a backend rung changes.

### C.11 Verification and proof obligations

- Linearization points and acquire/release pairs are documented beside every ticket, stripe, publication, cancellation, and reuse transition; “works on x86” is not a proof.
- Conservation fuzz submits randomized operations from many producers under full/empty pressure and proves every accepted ticket completes exactly once, payload/status matches the synchronous oracle, per-resource ordering holds where promised, and no rejected message loses ownership.
- Duplicate-load fuzz proves one physical load/parse and one published generation for concurrent identical identities, including failure and retry.
- Consume fuzz proves one claimant, correct typed result, no hidden copy where zero-copy is promised, no premature allocator retirement, and correct recovery from consumer failure/cancellation.
- Publication fuzz races handle reads against reload/unload/group retirement and proves consistent snapshots, sentinel staleness, and reclamation only after grace.
- Stripe fuzz forces wrap, partial stripes, every cycle transition, producer/consumer preemption, delayed consumers, cancellation, shutdown, and simulated narrow-counter wrap; count, sum, xor, generation, and ownership oracles all hold.
- Fault tests terminate producers and consumers at every claim/publish/complete boundary permitted by the design and verify the documented progress behavior. A wedged slot is either impossible by construction, bounded by a declared scheduler assumption, or repaired by helping.
- TSan runs the complete non-GPU concurrency surface with zero engine suppressions; ASan/UBSan covers ownership and retirement; native GPU/audio runtime verification covers the external consumers TSan cannot instrument reliably.
- Benchmarks compare the final striped SPMC against the existing per-slot Vyukov SPMC, a mutex queue baseline, and a Michael–Scott reference where useful; the production choice must win at the actual resource consume topology and stride distribution.
- Hardware counters and layout probes measure cache-line transfers, false sharing, atomic retries, stripe occupancy, wasted entries, and tail latency. Apple Silicon and x86 results are both required; generic aarch64 is included when available.

### C.12 Migration sequence

1. Freeze the ticket grammar, operation/status grammar, lifetime token, and ownership dispositions with tests before adding the service thread.
2. Build stable SoA handle publication and reclamation while the current synchronous owner remains; switch `ano_res_bytes` and typed views to the publication path.
3. Build the striped SPMC consume primitive and shared logger stripe core; prove it independently against the existing collection baselines.
4. Add MPSC request ingress, the resource owner thread, ticket table, completion lanes, duplicate coalescing, and clean shutdown; keep synchronous wrappers as oracle/compatibility paths over the new machinery.
5. Move registry mutation, allocator ownership, group/lifetime operations, save sequencing, adopt/release/unload, and statistics entirely onto the owner. Delete `g_reg.mtx`, `g_res.save_mtx`, ambient `cur_group`, relocatable rows, and direct graphics/save-TU mutation only after grep and concurrency tests prove no alternate path remains.
6. Move graphics/audio/world/config parsing and derived-resource adoption through worker tickets and winning allocator homes; enable real renderer/audio/world consume lanes.
7. Move ranges, chunks, codecs, packs, baking, and hot reload onto the ticket pipeline; run equivalence and performance bars after each capability lands.
8. Remove prototype interconnects, alternate path loaders, false Model E labels, and obsolete synchronous internals; update all plans, journal state, public contracts, and benchmark records.

### C.13 Stage C exit bar

Stage C ends only when no resource data mutex exists, the winning allocator domains own real registry shards and resources, transparent readonly handles resolve through stable SoA publication, all mutable operations ride the owner interconnect, resource consume rides cache-striped lock-free SPMC lanes, every accepted generational ticket completes exactly once, reclamation is proven, the complete streaming/codec/pack/bake/hot-reload path uses the same grammar, and the full platform/sanitizer/runtime/benchmark matrix is green. The deletion gate is literal: no `g_reg.mtx`, `save_mtx`, ambient `cur_group`, relocatable row array, graphics/save-TU direct mutation, hidden pooled-copy TAKE, synchronous mutation bypass, or mutex/condition-variable waiter remains in a resource correctness path.

## 2. Final done means

- Every loadable belongs to an explicit winning allocator domain; the domain owns its substantial registry and all resource memory, and real engine lifetimes retire through the winning teardown mechanism.
- A, B, C, D, and E were each complete when measured. The raw contest is preserved, one winner remains in production, and no reduced implementation carries a model name.
- All asset meaning lives in resource extensions; graphics includes skins/animations and every supported image/material path, levels are data-driven, configs/keybindings persist safely, saves migrate safely, and renderer/audio/world code does not open or parse resource files.
- Logical names, SID identity, ordered mounts, remote-FS byte truth, durable writes, save preservation, pack integrity, loose shadowing, and generation sentinels hold across all supported platforms.
- Readonly handles are transparent, immutable, wait-free to resolve under the published-directory contract, and stale-safe. Mutable registry state is single-owner and unreachable to callers.
- Requests and completions use the cheapest lock-free topology; resource consumption uses cache-striped SoA SPMC lanes derived from the logger and the Michael–Scott/Vyukov queue lineage; no data mutex, hidden copy, unbounded spin, lost wakeup, or unbounded ABA window survives.
- Ranged streaming, bounded chunk pools, codecs, deterministic packs, baked load-in-place resources, ticketed hot reload, and the backend ladder are complete and measured. The shipped scene loads with zero runtime parse work.
- Every correctness and performance claim is reproducible from checked-in tests, raw reports, exact commands, and named commits. “Done” describes the running engine, not the next rung in a journal.
