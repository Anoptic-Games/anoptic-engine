# Reflection-Compiled Residency Graph

## Status

This document defines the clean-room design for Anoptic's next resource manager. The abandoned `backup-resource-manager` implementation, API, and tests are not normative. Its documents may be mined for requirements and failure cases, but this system begins from first principles.

The working name is **Reflection-Compiled Residency Graph**, abbreviated RCRG.

## Thesis

Cooking and loading are not separate systems. They are different evaluation stages of the same typed resource program.

The offline build graph and runtime residency graph are one statically verified transform graph. The cooker evaluates every machine-independent edge it can. Runtime evaluates only environment-dependent edges such as device realization, streaming, and publication. ECS state supplies demand. Immutable residency epochs publish coherent results atomically.

The resource manager is therefore neither a file cache nor an ownership graph. It is a typed incremental compiler paired with a runtime reconciler.

```mermaid
flowchart LR
    S["Source assets"] --> C["Cooker evaluates portable transforms"]
    C --> CAS["Content-addressed artifacts"]
    CAS --> M["Manifest and packs"]
    E["ECS AssetRef<T> demand"] --> G["Residency goals"]
    M --> R["Runtime evaluates environment-dependent transforms"]
    G --> R
    R --> N["Candidate residency epoch"]
    N --> P["Atomic publication at a safe point"]
    P --> X["Renderer, audio, simulation, and ECS readers"]
```

## What is new

Every constituent idea has prior art. Offline dependency computation appears in [Unreal's Zen Loader](https://dev.epicgames.com/documentation/unreal-engine/zen-loader-in-unreal-engine) and the [O3DE Asset Pipeline](https://docs.o3de.org/docs/user-guide/assets/pipeline/). Immutable content-addressed artifacts and action caching are proven by [Bazel's Remote APIs](https://github.com/bazelbuild/remote-apis) and Nix derivations. Typed handles and hot loading appear in [Distill](https://docs.rs/distill-loader). [DirectStorage](https://github.com/microsoft/DirectStorage) and [RTX IO](https://developer.nvidia.com/rtx-io) demonstrate useful batching, decompression, and data-placement goals, but they are vendor-specific prior art rather than architectural dependencies.

The architectural contribution is their composition:

1. One reflected declaration defines an artifact's structure, dependencies, validation, schema identity, migration obligations, and admissible transformations.
2. One compile-time type graph covers import, cooking, packing, loading, device realization, and residency.
3. Offline and runtime are stages of evaluation over that graph rather than independent implementations joined by convention.
4. ECS components express typed residency demand instead of owning resource pointers or manually balancing acquire and release calls.
5. Coherent groups are constructed privately and published through immutable, structurally shared residency epochs.
6. Content identity, derivation identity, and provenance make every resident object explainable and reproducible.

To our knowledge, this is the first resource architecture to derive a unified cooker/runtime transform graph from standardized C++26 reflection and reconcile it directly against ECS residency demand. This is a design claim to prove, not a novelty claim to publish without a deeper literature and prior-art review.

## Design laws

1. Types and laws are compile time; asset instances and schedules are runtime.
2. Reflection discovers structure and orchestrates generation; `consteval` computes and validates; templates implement reusable kernels; explicit code owns runtime behavior.
3. No runtime reflection database exists unless a measured tool or diagnostic requires a generated table.
4. Resource values are immutable after construction. Mutation creates a successor artifact or residency epoch.
5. Logical identity, content identity, and resident location are separate concepts.
6. ECS stores stable typed references, never durable pointers, descriptor indices, allocation addresses, or backend handles.
7. The resource system plans dependencies and residency. Renderer, audio, and other modules retain ownership of their devices and state machines.
8. A coherent resource group is either wholly visible or not visible. Partial publication is forbidden.
9. Untrusted bytes are validated at ingress. Trusted cooked artifacts remain bounds-checked against their manifest before use.
10. Shipping runtime does not parse source formats when a cooked representation exists.
11. Cross-module surfaces remain C-shaped and use C ABI where practical. Implementation remains plain data, value semantics, `.c` and `.h`, C++26, `-nostdlib++`, no exceptions, no RTTI, and no virtual polymorphism.
12. Every artifact and realization family has one required portable route. Optional acceleration may replace an executor step but never resource semantics, correctness, or availability.
13. Manifests, packs, codecs, schemas, and logical assets are vendor-neutral. A build for ordinary Linux, Windows, or macOS hardware is a first-class product rather than a compatibility fallback.

## The three graphs

The phrase "one graph" has three concrete levels. Confusing them would create a magical and unimplementable design.

| Graph | Construction time | Nodes | Purpose |
| --- | --- | --- | --- |
| Type graph | Compilation | Reflected artifact types and transform recipes | Prove the resource language is closed, typed, unambiguous, and schedulable |
| Artifact-instance graph | Cooking or development | Particular sources, settings, derived artifacts, and dependency edges | Incremental building, caching, packing, provenance, and invalidation |
| Residency graph | Runtime | Required artifact instances, streaming atoms, device objects, and commit groups | Demand reconciliation, scheduling, realization, publication, and eviction |

The `consteval` compiler processes the finite type graph. It does not attempt to enumerate a project's assets during C++ compilation. The cooker instantiates that verified language over actual project content. Runtime instantiates only the portion demanded by the active world and environment.

## Resource vocabulary

| Term | Meaning |
| --- | --- |
| Logical asset | Stable author-facing identity, surviving recooks and content changes |
| Artifact | Immutable typed bytes or data produced by a transform |
| Artifact ID | Hash of canonical artifact bytes and type/schema domain |
| Recipe | Typed transformation from declared inputs and settings to declared outputs |
| Action key | Hash of recipe identity and revision, input artifact IDs, normalized settings, target, and relevant tool versions |
| Asset reference | `AssetRef<T>` containing a stable logical identity constrained to expected type `T` |
| Streaming atom | Independently requestable quality or locality unit such as a texture mip range, mesh LOD, audio chunk, shader group, or world cell |
| Residency goal | Desired asset or atom plus quality floor, importance, deadline, scope, and fallback policy |
| Realization | Environment-dependent artifact such as a GPU image, pipeline, audio-device buffer, or mapped runtime view |
| Commit group | Set of resources that must become visible together, such as a prefab, world cell, UI screen, or audio scene |
| Residency epoch | Immutable mapping from logical assets and atoms to validated resident realizations |

## Declarative resource language

Artifact types are ordinary C+Ultra aggregate declarations. Typed annotations express only facts C++ names and types cannot already encode.

```cpp
struct TextureAsset final {
    [[=resource::required]]
    AssetRef<ImagePixels> pixels;

    [[=resource::optional]]
    AssetRef<TextureMetadata> metadata;

    ColourDomain colour_domain;
    TextureUsage usage;
};

struct MaterialAsset final {
    [[=resource::dependency]] AssetRef<TextureAsset> base_colour;
    [[=resource::dependency]] AssetRef<TextureAsset> normal;
    [[=resource::range{0.0f, 1.0f}]] float roughness;
    [[=resource::range{0.0f, 1.0f}]] float metallic;
};
```

Reflection derives field inventories, dependency walkers, canonical schema descriptions, validators, migration inventories, debug names, packing descriptions, and ECS reference extraction. It does not serialize arbitrary object representations blindly; every persisted representation has an explicit canonical encoding policy.

Transforms are ordinary functions with typed inputs and outputs plus stage and execution annotations.

```cpp
[[=resource::transform{.stage = resource::Stage::cook,
                       .revision = 3}]]
TextureMipChain cook_texture(const ImagePixels&, const TextureSettings&);

[[=resource::transform{.stage = resource::Stage::runtime,
                       .executor = resource::Executor::render}]]
GpuTexture realize_texture(const TextureMipChain&, const RenderDeviceCaps&);
```

The exact surface syntax is provisional. The semantic requirement is not: the compiler must be able to recover each recipe's input types, output type, stage, executor, revision, determinism policy, and failure policy from its declaration.

## Compile-time graph compiler

The graph compiler reflects the closed inventory of artifact declarations and recipe functions using `std::meta::info`, C++26 annotations, parameter reflection, expansion statements, and splicing. GCC 16.1 reflection ranges are stabilized with `std::define_static_array(...)` before `template for` expansion.

Ordinary `consteval` code performs the following work:

- inventories artifact types and recipes;
- derives and freezes deterministic type and recipe ordering;
- validates that every reflected schema uses supported field categories and explicit policies where required;
- proves every recipe parameter and result is a declared artifact, settings object, capability object, or approved execution context;
- rejects duplicate producers where the selection would be ambiguous;
- rejects illegal stage inversions, such as a portable cooked artifact depending on a runtime device object;
- rejects undeclared dependency-bearing fields;
- computes the admissible transform routes between source, portable, runtime, and device forms;
- rejects an accelerated transform family that lacks a complete portable route with the same typed result and validation contract;
- detects forbidden cycles and requires explicitly modeled indirection for legal recursive relationships;
- generates direct dependency walkers, validators, dispatch tables, schema descriptors, and ECS `AssetRef<T>` extractors;
- generates compile-time diagnostics at the declaration responsible for an incomplete or contradictory contract;
- emits only the static tables and specialized functions required at runtime.

The compiler does not generate arbitrary function bodies because C++26 reflection cannot do so. Reflection expands concrete field accesses and direct calls into generic handwritten kernels. Complex decoding, compression, upload, and migration algorithms remain explicit functions.

## Identity and reproducibility

The system separates three identities:

```text
LogicalAssetId  = stable project identity
ActionKey       = H(recipe identity, explicit recipe revision, input artifact IDs,
                    canonical settings, target facts, relevant tool versions)
ArtifactId      = H(type domain, schema fingerprint, canonical output bytes)
```

The schema fingerprint is computed from reflected canonical structure: type identity, persisted field names, field types, ordering policy, annotations that affect representation, and explicit schema version. It never hashes in-memory padding.

C++26 reflection cannot fingerprint a function body. Every recipe therefore has an explicit revision. Development builds may include the source or build revision to guarantee conservative invalidation; stable shipping pipelines use explicit recipe revisions and CI checks. The artifact's output hash remains authoritative regardless of action-cache identity.

Each produced artifact records provenance: recipe identity and revision, input artifact IDs, schema fingerprint, target facts, tool versions, output ID, and validation result. A runtime failure can therefore answer exactly which derivation produced the bytes.

## Cooker

The cooker is an evaluator for the portable portion of the reflected graph.

1. Discover source assets and assign or recover stable logical IDs.
2. Normalize importer settings and source dependencies.
3. Instantiate the verified type graph into an artifact-instance DAG.
4. Compute action keys and reuse matching immutable outputs from the content-addressed store.
5. Execute invalidated recipes in dependency order with bounded parallelism and declared executor requirements.
6. Validate every output against its reflected schema and semantic validator.
7. Store canonical artifacts by content identity with provenance records.
8. Partition artifacts into packs and independently streamable atoms using runtime locality and commit-group information.
9. Emit a manifest mapping logical IDs to typed artifact roots, dependencies, atoms, hashes, byte ranges, codecs, and schema versions.

The same inputs, normalized settings, recipe revisions, and tool versions should produce byte-identical portable artifacts whenever the recipe declares itself deterministic. A nondeterministic recipe must say so and forfeits cross-build action-cache reuse.

## Content-addressed store and packs

The content-addressed store contains immutable artifacts. Duplicate bytes are naturally deduplicated. Corruption is detected by identity mismatch. Incremental builds invalidate descendants of changed action keys rather than rebuilding the project.

The CAS is a build and development abstraction, not the shipping I/O layout. Shipping packs arrange immutable artifacts and streaming atoms for locality, compression, mapping, and platform delivery. A manifest preserves content identity while translating it to pack, range, and codec information.

Pack construction should jointly optimize:

- commit-group locality;
- predicted co-residency;
- sequential I/O;
- codec block boundaries;
- patch granularity;
- independently streamable quality levels;
- direct mapping or decompression destinations;
- avoidance of unrelated high-churn assets in the same patch unit.

## Portable I/O and realization baseline

Anoptic has one canonical resource pipeline across operating systems and hardware vendors. The shared implementation owns dependency closure, range coalescing, scheduling, codec selection, validation, staging, realization planning, and publication. Platform filesystem backends provide the best native asynchronous range-read primitive through anoptic_filesystem.h; graphics and audio modules provide their existing portable realization surfaces. Platform adaptation stops at those module boundaries.

The required baseline path is:

1. Convert the desired artifact closure into a small set of aligned, locality-aware pack ranges.
2. Submit bounded asynchronous reads through the platform filesystem backend.
3. Receive bytes into persistent pooled or ring-buffered staging storage rather than per-resource allocations.
4. Validate block headers, sizes, hashes, and destination bounds before decoding.
5. Decode with portable scalar or SIMD CPU kernels directly into the final CPU representation or an upload-ready staging region.
6. Batch graphics realization through the engine's cross-vendor graphics backend and audio realization through the audio bridge.
7. Publish exactly the same typed resident result through the same epoch machinery on every platform.

Most of the win should come from work that is portable by construction: offline locality, fewer and larger reads, exact range knowledge, no runtime source parsing, bounded allocation, parallel decode, direct destinations, batched uploads, dependency-aware scheduling, and no redundant copies. These advantages remain available on Linux, AMD, Intel, Apple, older hardware, virtualized environments, and ordinary Windows machines.

A cross-vendor GPU compute decoder may supplement the CPU decoder when the graphics API exposes the required portable features and measurement justifies it. It must consume the same pack blocks, produce the same canonical decoded bytes, and retain the CPU implementation as the universal route. The system must not require CUDA, NVIDIA-specific compression, or a vendor-only shader extension.

DirectStorage, RTX IO, or a future equivalent may eventually be added as optional executor adapters. They are selected only after runtime capability discovery and only when an A/B benchmark shows a material advantage over the portable path. They cannot change the manifest, artifact ID, codec semantics, validation contract, dependency graph, or publication behavior. Failure or absence of an accelerator immediately selects the portable route.

Reflection and consteval enforce that relationship. Every codec and realization family declares one portable recipe. Optional routes declare the same logical input and output types, an equivalence class, required capabilities, and fallback route. The compile-time graph compiler rejects missing fallbacks, divergent result schemas, capability cycles, and recipes that make a vendor route the only path to a required artifact.

The goal is not to accumulate branded fast paths. The goal is to make the portable implementation good enough that they are unnecessary. Better packing and scheduling may outperform a naive use of a privileged API. If an operating system or driver exposes a genuinely exclusive kernel-to-device path, portable user-space code cannot honestly promise to reproduce that mechanism; the architecture ensures that such an advantage is isolated to one executor step rather than becoming control over Anoptic's resource format or design.

## Runtime reconciler

Runtime loads manifests, not source schemas. It maintains desired state, observed state, work in flight, candidate epochs, and published epochs.

The reconciler repeatedly performs a bounded sequence:

1. Consume incremental residency-goal changes from ECS and explicit system scopes.
2. Resolve logical IDs to typed manifest roots.
3. Compute the required dependency and quality closure.
4. Compare desired closure with published and in-flight state.
5. Schedule only missing I/O, decompression, validation, mapping, and device realization work.
6. Assemble completed resources into candidate commit groups.
7. Publish a successor epoch only at an owner-approved safe point.
8. Retire unreachable resources after no reader can observe an older epoch and eviction hysteresis permits release.

This is reconciliation rather than imperative `load()` and `unload()`. Callers declare what the world should be able to use; the system converges toward that state under time, memory, and executor constraints.

## ECS composition

ECS components store typed stable references:

```cpp
struct Renderable final {
    AssetRef<MeshAsset> mesh;
    AssetRef<MaterialAsset> material;
};

struct SoundEmitter final {
    AssetRef<AudioClipAsset> clip;
};
```

They do not store pointers into resource arenas, GPU handles, descriptor slots, pack offsets, or epoch-specific resident indices.

Reflection inventories `AssetRef<T>` fields recursively and generates component-specific extraction functions. Extraction runs when a relevant component is inserted, removed, or changed through the ECS command path. The resource manager does not reflectively scan every entity every frame.

Each contributing component or system scope produces residency goals. Goals are reference-counted or multiplicity-counted by origin, but resource lifetime is derived from the aggregate desired state rather than manual ownership calls. Removal of the last goal makes a resource eligible for eviction; it does not synchronously destroy it.

Hot paths may use an ephemeral resolved component or dense binding cache containing manifest indices or resident slots tagged with the epoch that produced them. It is derived state, invalidated by epoch change, never persisted, and never treated as ownership.

World cells and prefabs should be cooked into archetype-shaped component columns where practical. Loading then validates immutable columns and bulk-instantiates them into ECS storage instead of reconstructing an object graph entity by entity. Asset references inside those columns contribute demand through the same generated extraction machinery.

## Residency goals

A residency goal describes intent rather than an imperative operation. Its conceptual fields are:

```text
typed logical asset
minimum usable quality
desired quality
importance
deadline or latency class
scope and commit group
fallback policy
origin token
```

Goals may come from visible entities, predicted world cells, UI navigation, audio scheduling, gameplay systems, editor tools, or explicit preload scopes. The active world's hard requirements and speculative prediction remain distinguishable.

The scheduler considers deadline, importance, quality gain, dependency critical path, I/O locality, decompression cost, executor contention, memory pressure, and recent residency. A useful starting heuristic is benefit divided by remaining cost with deadline escalation and strong hysteresis. It is not frozen as an API or claimed as globally optimal.

Cancellation removes obsolete desired work, but completed immutable artifacts remain reusable and in-flight operations are cancelled only when cancellation is cheaper and safe.

## Streaming atoms and quality

A monolithic asset is often the wrong unit of scheduling. Reflected schemas may declare legal streaming structure, while explicit cook algorithms choose actual partitioning.

Examples include texture mip groups, mesh LODs or clusters, animation segments, audio pages, shader/pipeline families, virtual-texture tiles, and world cells. Each atom declares prerequisites and the quality or coverage it contributes.

Quality is monotonic within one logical version: publication may advance from fallback to minimum usable to desired quality without invalidating the logical reference. Incompatible replacements, schema changes, or hot reloads create a successor version and publish transactionally.

## Immutable residency epochs

A residency epoch is an immutable, typed mapping from manifest asset indices and streaming atoms to resident realizations. Readers acquire the current epoch once at an appropriate boundary and perform dense lookups without taking resource-manager locks.

A candidate epoch is assembled privately. All required artifacts are validated, all executor-owned realizations are complete, and all commit-group invariants pass before publication. One atomic epoch publication makes the new coherent view visible. The previous epoch remains alive until readers leave it, after which unreachable resources may be reclaimed.

Epochs must use structural sharing or paged copy-on-write tables so publishing a small change does not copy the entire resident world. Publication granularity may be global or partitioned by independently safe domains, but no reader may combine mutually inconsistent versions within one commit group.

Natural publication points include frame boundaries, audio control-block boundaries, ECS structural command barriers, and editor transaction boundaries. The owning module selects its safe point.

## Module and executor ownership

The resource manager owns identity, dependency planning, artifact validation, residency policy, and epoch construction. It does not absorb renderer, audio, filesystem, threading, or memory internals.

| Executor | Owned work |
| --- | --- |
| I/O | Portable coalesced manifest and pack range reads through `anoptic_filesystem.h` and its native platform backend |
| CPU | Validation, transcoding, decompression, migration, and portable construction |
| Render | GPU allocation, upload, image/view creation, pipeline realization, and render-safe retirement |
| Audio | Audio-device preparation, page publication, and audio-safe retirement |
| ECS/world | Structural instantiation and goal-delta production |

Cross-module realization uses narrow C-shaped command and completion contracts. The resource manager submits typed recipes or plans through the owning module's public interface and receives opaque completion tokens or results. Backend handles never leak into persisted artifacts or arbitrary ECS components.

The intended module surface is `include/anoptic_resources.h` with implementation in `src/resources/`. Private reflection schemas and graph-compilation machinery remain inside that module or a genuinely shared compile-time meta interface when multiple modules must consume them. There is no generic `src/cpp/` dumping ground.

## Hot reload

Hot reload is incremental compilation followed by transactional publication.

1. A source or setting changes.
2. The cooker invalidates only affected actions and rebuilds their descendants.
3. A new manifest generation identifies successor artifacts while the old generation remains usable.
4. Runtime stages changed portable artifacts and environment-dependent realizations privately.
5. Semantic validators and commit-group checks run against the complete successor set.
6. The appropriate safe point publishes a successor epoch.
7. Readers already using the old epoch finish safely; failed reloads leave the old epoch published.

No in-place mutation crosses reader boundaries. A broken shader, material, mesh, or audio page cannot half-update a scene.

## Schema evolution and migration

Every persisted artifact schema has an explicit version. Reflection derives its canonical field inventory and rejects unversioned representation changes under the chosen policy.

Migrations are explicit typed transforms between schema versions and participate in the same graph. The compile-time compiler proves that every supported historical version has exactly one admissible route to the current canonical form, or that the version is deliberately unsupported. Shipping packs normally contain current forms; editor and compatibility tools may execute migration paths.

Migration code remains ordinary explicit code. Reflection supplies member discovery, renamed-field annotations, defaults, range checks, route completeness, and generated structural copying where semantically valid. It must never guess domain transformations.

## Safety model

| Time | Guarantees |
| --- | --- |
| Compile time | Closed artifact and recipe inventory, supported field policies, unique and legal transform routes, stage ordering, executor declarations, schema/version obligations, typed ECS references, and generated dispatch exhaustiveness |
| Cook time | Source validation, bounded arithmetic, deterministic canonicalization where declared, dependency completeness, semantic checks, output hashing, provenance, and pack-range construction |
| Runtime ingress | Manifest version and hash checks, range/size/codec validation, decompression limits, path and URI policy, integer-overflow rejection, and artifact/schema agreement |
| Runtime publication | Complete dependency closure, successful realization, commit-group coherence, epoch consistency, owner safe-point approval, and deferred reclamation |

Malformed data must fail before publication. Resource limits are explicit and checked before allocation or decompression. External paths cannot escape approved roots. Integer products and offsets use checked arithmetic. A manifest cannot reinterpret bytes as a different reflected artifact type merely because layouts happen to match.

## Memory and performance posture

Portable artifacts favor immutable contiguous representations, relative offsets, stable indices, and load-in-place validation. Pointer fix-up forests and per-object ownership allocations are rejected by default. The same canonical artifact and pack block must be consumable on every supported platform unless its type explicitly represents a platform-specific final realization.

The runtime should exploit mapped pack ranges, exact arena sizing, batch I/O, codec-aligned blocks, direct decoding destinations, portable parallel decompression, batched cross-vendor uploads, and dense epoch lookup tables. Optional device-directed paths may replace a measured bottleneck without changing this data model. Hot reads must not traverse strings, hash maps, reflection metadata, or dependency graphs.

Graph sophistication is paid at compilation, cooking, goal changes, and asynchronous reconciliation boundaries. Frame, audio, and ECS iteration paths consume specialized tables and stable indices equivalent to carefully handwritten code.

Memory budgets apply by domain, quality class, and executor. Eviction accounts for shared dependencies and realization costs rather than treating byte size as the only cost. Recently expensive resources receive hysteresis to prevent churn.

## Non-goals

- A universal runtime reflection system.
- Reflecting every engine struct or turning the ECS into an object database.
- Replacing clear algorithms with metaprogramming.
- A polymorphic `Resource` class hierarchy.
- Shared-pointer ownership graphs or synchronous reference-count destruction.
- Runtime parsing of glTF, images, shader source, or other authoring formats in shipping builds.
- Making the resource module own renderer, audio, filesystem, allocator, or threading implementation details.
- Requiring DirectStorage, RTX IO, CUDA, a vendor compression format, or vendor-specific GPU features for correctness or acceptable performance.
- Forking manifests, packs, schemas, or resource semantics by operating system or GPU vendor.
- Promising transparent support for every future source-format extension.
- Depending on the C++ runtime library, RTTI, exceptions, virtual dispatch, or `.hpp`/`.cpp` naming.

## Initial vertical slice

The first proof is one complete Sponza path, not a horizontal framework rollout.

1. Declare reflected schemas for the portable scene, mesh, material, texture, and world-cell artifacts already produced or consumed by the engine.
2. Reuse the Anoptic-native glTF decoder only as a cooker ingress and produce canonical scene artifacts.
3. Compile recipe and schema inventories with C++26 reflection and `consteval` validation.
4. Build a local CAS, provenance records, one manifest generation, and locality-aware pack output.
5. Launch the existing release engine from the cooked pack with no runtime glTF or image-source parsing.
6. Represent the scene request as an ECS `AssetRef<WorldCellAsset>` residency goal.
7. Realize render resources through the renderer bridge, assemble a candidate epoch, and publish at a frame boundary.
8. Change one material or texture, rebuild only its affected descendants, and publish a coherent hot-reload epoch while the old scene remains usable.
9. Corrupt and truncate artifacts deliberately and verify rejection before publication.
10. Compare final geometry, materials, textures, transforms, and rendered output against the existing path.

The vertical slice succeeds only if it demonstrates the architecture end to end. A collection of reflected serializers without ECS demand, staged evaluation, and transactional publication is not the proposed system.

## Measurements

The baseline and vertical slice must record:

- production LoC and duplicated schema/registry LoC;
- clean and incremental cook wall time;
- action-cache hit rate and bytes rebuilt after representative edits;
- cold and warm startup time;
- I/O requests, bytes read, decompressed bytes, allocations, bytes copied, and peak memory;
- time to minimum usable and desired quality;
- frame hitch and audio-underrun distributions during streaming;
- hot-reload latency and publication interruption;
- generated code and data size;
- C++ compilation time and GCC memory use;
- malformed-input rejection, schema mismatch rejection, and compile-time contract probes.
- output parity among portable CPU, portable GPU, and any vendor-accelerated route;
- representative Linux, Windows, macOS, NVIDIA, AMD, and Intel results before accepting a supposedly universal optimization;
- direct comparison against vendor APIs before adding their maintenance and testing burden.

The architecture earns adoption by improving the judge: fewer representations that can drift, more invalid programs rejected before execution, more deterministic incremental work, and runtime performance no worse than the specialized manual path. Elegance alone is insufficient.

## Delivery sequence

| Phase | Deliverable | Exit condition |
| --- | --- | --- |
| 0 | Reflected artifact and recipe language | GCC 16.1 compiles the inventory under production flags; invalid schemas and routes fail clearly |
| 1 | Portable artifact compiler and local CAS | Deterministic Sponza artifacts rebuild incrementally with provenance |
| 2 | Manifest, packs, and portable runtime range loader | Release runtime consumes the same cooked assets without source parsing on every supported platform |
| 3 | ECS goals and dependency closure | Component changes produce incremental typed demand without frame-wide scans |
| 4 | Renderer realization and residency epochs | Sponza publishes coherently at frame boundaries with safe old-epoch retirement |
| 5 | Hot reload and schema migrations | Changed assets rebuild and publish transactionally; supported old schemas have verified routes |
| 6 | Streaming quality and scheduler | Mips, LODs, audio pages, or world cells converge under explicit budgets and deadlines |
| 7 | Production hardening | Corruption, cancellation, device loss, memory pressure, and interrupted builds preserve invariants |
| 8 | Adoption | Existing runtime source-loading path is deleted only after measured parity or improvement |

## Locked decisions

- The type graph is closed and compiled; artifact instances remain data.
- Reflection is compile time and disappears into specialized operations and minimal tables.
- Offline and runtime transforms share one declared graph.
- The CAS and shipping pack layout are separate layers.
- ECS expresses demand through typed stable references.
- Immutable epochs and commit groups provide coherent publication.
- Device-owning modules retain their state machines and handles.
- One portable path is mandatory; accelerated executor routes are optional, semantically equivalent, and independently removable.
- Vendor-neutral packs and manifests are the only canonical shipping format.
- Recipe revisions are explicit because reflection cannot hash function bodies.
- The new architecture does not inherit the abandoned branch's API or implementation.

## Open engineering questions

- Exact stable logical-ID format and authoring workflow.
- Canonical byte encoding and endian policy for each artifact family.
- Whether epoch publication is global, domain-partitioned, or both.
- The smallest useful commit-group boundary for world streaming.
- The first scheduler cost model and source of demand predictions.
- Pack partitioning strategy and patch-distribution constraints.
- Whether a portable compute-shader decompressor beats the SIMD CPU route broadly enough to justify its complexity.
- Development-daemon transport, if any, between cooker and running engine.
- Driver and device facts that must enter realization cache identity.
- How CI enforces recipe-revision changes when implementation semantics change.
- Which diagnostics justify retained generated runtime names in Release.

## Falsification

This design should be abandoned or substantially simplified if the vertical slice shows that compile-time graph machinery creates unacceptable compiler instability or build cost, structural sharing cannot bound epoch memory, reconciliation adds material latency versus direct scheduling, generated specialization inflates the runtime image without hot-path benefit, or the unified graph makes module ownership less explicit.

The breakthrough is not that reflection can enumerate fields. The breakthrough is that the engine can compile its resource laws from the same declarations used by cooker, ECS, runtime, renderer, and audio, then execute only the stage of that typed program appropriate to the current machine and world.
