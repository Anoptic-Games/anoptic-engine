# Bug hunt

This is a remediation-oriented census of `docs/BUGS.md` on `fix-bughunt`, read on 2026-07-21. It groups findings by the engineering discipline that can retire them together, rather than by the module in which each symptom surfaced.

## Accounting

The source contains 69 verified findings and 42 unverified lead bullets, or 111 records as written. Four leads are older restatements of verified findings: `text_bake.c ano_text_window_sum`, `render_slots.c:84`, `ano_render_ui_set validation gap`, and `time_linux.c:132`. The deduplicated census is therefore 107 distinct concerns: 69 verified and 38 lead-only.

Two verified findings have two fine-grained tags: `music_perc.c:121` is both `fixed-array-overflow` and `shift-ub`, while `texture.c:437` is both `no-abort` and `feature-gated-check`. There are consequently 71 tag assignments across 69 findings. The remediation-bucket table assigns every finding one primary bucket and does not double-count either one.

| Population | Count |
|---|---:|
| Verified Critical | 32 |
| Verified Major | 26 |
| Verified Latent | 11 |
| Verified total | 69 |
| Lead records | 42 |
| Leads duplicating verified findings | 4 |
| Distinct lead-only concerns | 38 |
| Distinct concerns | 107 |

## Fell-swoop buckets

| Bucket | Verified | Lead records | Verified/lead overlap | Distinct total |
|---|---:|---:|---:|---:|
| Contract gates | 14 | 11 | 2 | 23 |
| Quantity safety | 17 | 2 | 0 | 19 |
| Fallibility and atomic commit | 16 | 7 | 2 | 21 |
| Ownership and deferred lifetime | 8 | 6 | 0 | 14 |
| Mirrored state and inventory drift | 9 | 6 | 0 | 15 |
| State-machine and concurrency lifecycle | 3 | 6 | 0 | 9 |
| Algorithm and contract one-offs | 2 | 4 | 0 | 6 |
| Total | 69 | 42 | 4 | 107 |

The first four programs retire 77 of the 107 distinct concerns. Contract gates plus quantity safety alone cover 42; Vulkan-style Result discipline and transactional publication cover another 21; explicit ownership closes another 14. Those are the actual broad strokes. The last three programs are smaller audits, and the final bucket is intentionally surgical rather than forced into a false common abstraction.

### 1. Contract gates

Cause: values cross a public API, config, bridge, parser, or third-party seam without the receiving module proving the domain promised by its own interface. The same family includes wrappers that delegate a stronger Anoptic contract to a weaker external API.

Swoop: give each ingress one validator/adopter that checks enums, counts, ranges, sentinels, cross-field invariants, and external postconditions before internal state is touched. Reuse that gate for public calls, config adoption, overrides, bridge commands, and file parsing; do not rely on downstream array users to repeat it.

Verified: `filesystem_linux.c:65`; `memalign_linux.c:13` / `memalign_macos.c:13` / `memalign_win64.c:13`; `ano_meshoptimizer.c:282`; `music_host.c:193`; `music_host.c:194`; `music_host.c:66`; `music_arp.c:106`; `shadow_casters.c:97`; `apply.c:125`; `ano_GltfParser.c:30`; `ano_render_bridge.c:204`; `ano_synth.c:227`; `ano_synth.c:246`; `text_raster_ref.c:92`.

Leads: `filesystem_win64.c:23`; log↔filesystem empty-path handling; `threads_macos.h:15`; `music_host.c cadence_policy == -1`; strings↔log malformed UTF-8; `text_bake.c ano_text_window_sum` (duplicate of verified `text_raster_ref.c:92`); text GPOS caps; engine↔music-panel key/mode indexing; `ano_render_ui_set` stop-window validation (duplicate of verified `ano_render_bridge.c:204`); music inward/outward clamp asymmetry; `tempo_bpm` override validation.

### 2. Quantity safety

Cause: sizes, counts, offsets, durations, indices, shifts, or rendered footprints are computed in a type that can wrap, truncate, exceed fixed storage, or describe a different metric than the allocation.

Swoop: make checked add, multiply, subtract, narrow, shift, and range-end helpers the only legal path into allocation and capacity checks. Express fixed-array limits next to the producer loop, validate before casting, use unsigned typed shift operands, and size deferred output by rendered bounds rather than stored representation.

Verified: `ano_audio.c:257`; `audio_wav.c:34`; `log_core.c:817`; `music_arp.c:102`; `music_perc.c:121`; `render_slots.c:35`; `record_views.c:302`; `gpu_alloc.c:12` and its `commands.c:173` twin; `ano_strings_ops.c:86`; `ano_synth.c:202`; `text_gpos.c:304`; `time_win64.c:310` and its macOS twin; `time_win64.c:337`; `ui_path.c:99`; `ui_build.c:236`; `ui_tiles.c:66`; `ui_raster_ref.c:229`.

Leads: `ano_GltfParser.c:76` size_t→u32 accessor counts; `anostr_sort_idx` with `count > UINT32_MAX`.

### 3. Fallibility and atomic commit

Cause: a failed operation logs and continues, reports unconditional success, consumes an unwritten out-parameter, spins without progress, or publishes a prefix before the whole transaction has succeeded. `ANO_FATAL` is currently a log level, not control flow, which makes several nominal error arms dead.

Swoop: establish one Result discipline for Vulkan and other fallible subsystems: initialize out-parameters, check every result at the call site, propagate failure, unwind through one cleanup path, and publish only after all resources and dependent bindings are ready. Batch mutations stage privately and commit once; retry/wait loops distinguish success, exhaustion, cancellation, and zero progress.

Verified: `filesystem_win64.c:137`; `swapchain.c:428`; `commands.c:201`; `texture.c:415`; `texture.c:437`; `render_slots.c:92`; `scene_buffers.c:35`; `vulkanMaster.c:505`; `slot_upload.c:221`; `window.c:214`; `compute.c:83`; `flat.c:90`; `record.c:29`; `time_linux.c:132`; `time_win64.c:252`; `main.c:391`.

Leads: `ano_GltfParser.c:71`; `ano_strings_collate.c:504`; engine↔render_bridge cancellation during blocking retries; `render_slots.c:84` (duplicate of verified `render_slots.c:92`); `apply.c:346` lossless retirement-event publication; dead `REVENT_BATCH_CONSUMED` / `RCMD_BULK_CREATE` protocol; `time_linux.c:132` (duplicate of the verified item).

### 4. Ownership and deferred lifetime

Cause: adopted payloads, temporary Vulkan objects, allocator domains, or deferred pointers have no single owner responsible for every failure and teardown edge. Several registries are the sole path to cleanup but cannot report that adoption failed.

Swoop: encode ownership transfer in APIs and use a cleanup stack or single unwind epilogue for partially built objects. Destroy drains adopted-payload rings before freeing their storage. Registration returns a Result and happens before publication. Deferred records deep-copy caller-owned data. Allocator provenance remains attached across translation-unit and library seams.

Verified: `ano_audio.c:204`; `log_core.c:205`; `swapchain.c:110`; `texture.c:426`; `flat.c:244`; `components.c:72`; `ano_GltfParser.c:277`; `ano_render_bridge.c:92`.

Leads: memory↔every-module allocator partition; `instance.c:192`; cgltf↔memory allocator provenance; `vulkanMaster.c:593`; `slot_upload.c` growth VRAM; vulkan_backend↔render_bridge adopted-block contracts.

### 5. Mirrored state and inventory drift

Cause: a hand-maintained sibling, feature list, generated table, descriptor binding, platform implementation, or copied block omits one member or retains one wrong token. Recreated resources do not carry an authoritative list of dependents to repoint.

Swoop: replace parallel lists with one declarative inventory that generates counts, creation, destruction, sharing flags, and descriptor rewrites. Add parity tests for platform siblings and generated Unicode tables, centralize duplicated low-level loops and types, and have resource recreation walk an explicit dependency registry before the old handle is destroyed.

Verified: `audio_linux.c:168`; `ano_meshoptimizer.c:955`; `texture.c:435`; `device.c:663`; `descriptors.c:39`; `commands.c:82`; `slot_upload.c:277`; `ano_strings_collate.c:75`; `threads_macos.c:79`.

Leads: `filesystem_win64.c:33`; `memalign_win64.c:1`; `ano_unicode_tables.h` case-trim coverage; text↔UI duplicated geometry helpers and `AnoQuad`; render↔text C/GLSL coverage drift; swapchain-recreate descriptor dependency coverage.

### 6. State-machine and concurrency lifecycle

Cause: state that should belong to one context is static, a recovery or teardown transition does not reset/re-anchor all dependent state, or ordering is assumed without a synchronization/lifetime proof.

Swoop: move mutable scratch and lazy tables into engine/context ownership or immutable one-time initialization; make start, stop, loss, suspend, resume, and teardown explicit state transitions with reset obligations; document and test the happens-before edge for every cross-thread field.

Verified: `audio_win64.c:589`; `music_voicing.c:114`; `time_win64.c:148`.

Leads: threads↔log-crash altstack teardown order; `music_theory.c:48`; `music_host.c override_apply`; log-crash↔log teardown; `transformStream reclaimSeq`; cross-platform time suspend semantics.

### 7. Algorithm and contract one-offs

Cause: these are genuine local semantic defects, not credible instances of a broad missing mechanism. Folding them into a generic framework would obscure the fix.

Swoop: repair each locally and pin it with the named regression test or a focused contract test. Resolve contract disagreements before choosing code behavior.

Verified: `audio_fx.c:100`; `text_shape.c:126`.

Leads: `anoptic_math.h:16` versus `docs/math-conventions.md`; dead `src/render/gltf/scratch_process.c`; shared texture color/data-space semantics; `time_win64.c:316` scheduler-yield contract.

## Fine-grained source tags

This preserves the source file's existing taxonomy. Counts are tag assignments, not unique findings.

| Source tag | Assignments |
|---|---:|
| seam-validation | 13 |
| checked-arithmetic | 11 |
| no-abort | 10 |
| ownership-leak | 7 |
| fixed-array-overflow | 3 |
| copy-paste-error | 2 |
| feature-gated-check | 2 |
| feature-list-drift | 2 |
| odd-sibling-out | 2 |
| partial-publish | 2 |
| shift-ub | 2 |
| wrong-error-source | 2 |
| clock-not-reanchored | 1 |
| dangling-capture | 1 |
| lookahead-off-by-one | 1 |
| missed-repoint | 1 |
| noop-not-honored | 1 |
| recovery-desync | 1 |
| retry-exhaustion | 1 |
| shared-mutable-state | 1 |
| size-mismatch | 1 |
| table-coverage-gap | 1 |
| truncating-cast | 1 |
| unbounded-spin | 1 |
| unguarded-delegation | 1 |
| Total assignments | 71 |

## Recommended order

1. Land contract gates and quantity helpers together, then route public/config/bridge/parser paths through them. This attacks 31 verified findings immediately and establishes the primitives used by later fixes.
2. Convert Vulkan and bridge operations to Result + unwind + commit semantics. Treat `ANO_FATAL` as diagnostic text unless and until its control-flow contract is deliberately changed.
3. Encode adopted-payload and Vulkan temporary ownership, including destroy-time ring drains and fallible registry adoption.
4. Build the renderer resource/dependency inventory and use it for descriptor budgets, sharing modes, growth/recreation repoints, and cleanup.
5. Audit context state and lifecycle transitions, then take the six local semantic fixes individually.

Each program should close its verified ledger entries and either promote or strike its leads. A lead is not counted as fixed merely because the nearby verified instance was fixed; it must be checked against the new invariant.
