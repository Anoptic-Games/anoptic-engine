# Bug hunt

This is a remediation-oriented census of `docs/BUGS.md` on `fix-bughunt`, read on 2026-07-21. It groups findings by the engineering discipline that can retire them together, rather than by the module in which each symptom surfaced.

## Accounting

The source contains 70 verified findings and 41 unverified lead bullets, or 111 records as written. Four leads are older restatements of verified findings: `text_bake.c ano_text_window_sum`, `render_slots.c:84`, `ano_render_ui_set validation gap`, and `time_linux.c:132`. The deduplicated census is therefore 107 distinct concerns: 70 verified and 37 lead-only.

Two verified findings have two fine-grained tags: `music_perc.c:121` is both `fixed-array-overflow` and `shift-ub`, while `texture.c:437` is both `no-abort` and `feature-gated-check`. There are consequently 72 tag assignments across 70 findings. The remediation-bucket table assigns every finding one primary bucket and does not double-count either one.

A verified key marked `[X]` in a bucket ledger below is repaired; the entry stays in the census with its `fix` note. Marks are per key, so the `memalign_*` sibling set shows three for one finding. Every mark below, and the Fixed column of the bucket table, stands as of the 2026-07-24 one-off pass; the passes of 2026-07-25 that carried the census to 50 fixed are recorded in `docs/BUGS_DONE.md`, not here.

Amended 2026-07-24 against the same source amendment: the `anoptic_math.h:16` versus `docs/math-conventions.md` lead was chased and split. Its convention half was resolved and struck (the header comment was wrong, the conventions doc right, no code changed); its alignment half was promoted to the verified Latent finding `anoptic_math.h:21` and filed under bucket 5. Verified 69 -> 70, leads 42 -> 41, distinct concerns unchanged at 107.

| Population | Count |
|---|---:|
| Verified Critical | 32 |
| Verified Major | 26 |
| Verified Latent | 12 |
| Verified total | 70 |
| Lead records | 41 |
| Leads duplicating verified findings | 4 |
| Distinct lead-only concerns | 37 |
| Distinct concerns | 107 |

## Fell-swoop buckets

| Bucket | Verified | Fixed | Lead records | Verified/lead overlap | Distinct total |
|---|---:|---:|---:|---:|---:|
| Contract gates | 14 | 11 | 11 | 2 | 23 |
| Quantity safety | 17 | 15 | 2 | 0 | 19 |
| Fallibility and atomic commit | 16 | 2 | 7 | 2 | 21 |
| Ownership and deferred lifetime | 8 | 1 | 6 | 0 | 14 |
| Mirrored state and inventory drift | 10 | 5 | 6 | 0 | 16 |
| State-machine and concurrency lifecycle | 3 | 0 | 6 | 0 | 9 |
| Algorithm and contract one-offs | 2 | 2 | 3 | 0 | 5 |
| Total | 70 | 36 | 41 | 4 | 107 |

The one-off pass of 2026-07-24 closed the two buckets that were always going to fall to local repair 〜 quantity safety (15/17, one of the two remaining a declined repair rather than an open one) and the algorithm one-offs (2/2) 〜 plus most of the contract gates (11/14). It barely dented fallibility (2/16) and ownership (1/8), which is the expected shape: those two are the Vulkan Result discipline and the adopted-payload ownership contract, and neither is reachable one file at a time. Recommended order below is unchanged; step 1's quantity helpers are now largely redundant as remediation and remain worth landing as the shared primitive.

The first four programs retire 77 of the 107 distinct concerns. Contract gates plus quantity safety alone cover 42; Vulkan-style Result discipline and transactional publication cover another 21; explicit ownership closes another 14. Those are the actual broad strokes. The last three programs are smaller audits, and the final bucket is intentionally surgical rather than forced into a false common abstraction.

### 1. Contract gates

Cause: values cross a public API, config, bridge, parser, or third-party seam without the receiving module proving the domain promised by its own interface. The same family includes wrappers that delegate a stronger Anoptic contract to a weaker external API.

Swoop: give each ingress one validator/adopter that checks enums, counts, ranges, sentinels, cross-field invariants, and external postconditions before internal state is touched. Reuse that gate for public calls, config adoption, overrides, bridge commands, and file parsing; do not rely on downstream array users to repeat it.

Verified: `filesystem_linux.c:65` [X]; `memalign_linux.c:13` [X] / `memalign_macos.c:13` [X] / `memalign_win64.c:13` [X]; `ano_meshoptimizer.c:282` [X]; `music_host.c:193` [X]; `music_host.c:194` [X]; `music_host.c:66` [X]; `music_arp.c:106` [X]; `shadow_casters.c:97`; `apply.c:125`; `ano_GltfParser.c:30`; `ano_render_bridge.c:204` [X]; `ano_synth.c:227` [X]; `ano_synth.c:246` [X]; `text_raster_ref.c:92` [X].

Leads: `filesystem_win64.c:23`; log↔filesystem empty-path handling; `threads_macos.h:15`; `music_host.c cadence_policy == -1`; strings↔log malformed UTF-8; `text_bake.c ano_text_window_sum` (duplicate of verified `text_raster_ref.c:92`); text GPOS caps; engine↔music-panel key/mode indexing; `ano_render_ui_set` stop-window validation (duplicate of verified `ano_render_bridge.c:204`); music inward/outward clamp asymmetry; `tempo_bpm` override validation.

### 2. Quantity safety

Cause: sizes, counts, offsets, durations, indices, shifts, or rendered footprints are computed in a type that can wrap, truncate, exceed fixed storage, or describe a different metric than the allocation.

Swoop: make checked add, multiply, subtract, narrow, shift, and range-end helpers the only legal path into allocation and capacity checks. Express fixed-array limits next to the producer loop, validate before casting, use unsigned typed shift operands, and size deferred output by rendered bounds rather than stored representation.

Verified: `ano_audio.c:257` [X]; `audio_wav.c:34` [X]; `log_core.c:817`; `music_arp.c:102` [X]; `music_perc.c:121` [X]; `render_slots.c:35` [X]; `record_views.c:302` [X]; `gpu_alloc.c:12` [X] and its `commands.c:173` twin; `ano_strings_ops.c:86` [X]; `ano_synth.c:202` [X]; `text_gpos.c:304` [X]; `time_win64.c:310` and its macOS twin (wontfix); `time_win64.c:337` [X]; `ui_path.c:99` [X]; `ui_build.c:236` [X]; `ui_tiles.c:66` [X]; `ui_raster_ref.c:229` [X].

Leads: `ano_GltfParser.c:76` size_t→u32 accessor counts; `anostr_sort_idx` with `count > UINT32_MAX`.

### 3. Fallibility and atomic commit

Cause: a failed operation logs and continues, reports unconditional success, consumes an unwritten out-parameter, spins without progress, or publishes a prefix before the whole transaction has succeeded. `ANO_FATAL` is currently a log level, not control flow, which makes several nominal error arms dead.

Swoop: establish one Result discipline for Vulkan and other fallible subsystems: initialize out-parameters, check every result at the call site, propagate failure, unwind through one cleanup path, and publish only after all resources and dependent bindings are ready. Batch mutations stage privately and commit once; retry/wait loops distinguish success, exhaustion, cancellation, and zero progress.

Verified: `filesystem_win64.c:137`; `swapchain.c:428`; `commands.c:201`; `texture.c:415`; `texture.c:437`; `render_slots.c:92`; `scene_buffers.c:35`; `vulkanMaster.c:505`; `slot_upload.c:221`; `window.c:214`; `compute.c:83`; `flat.c:90`; `record.c:29`; `time_linux.c:132` [X]; `time_win64.c:252` [X]; `main.c:391`.

Leads: `ano_GltfParser.c:71`; `ano_strings_collate.c:504`; engine↔render_bridge cancellation during blocking retries; `render_slots.c:84` (duplicate of verified `render_slots.c:92`); `apply.c:346` lossless retirement-event publication; dead `REVENT_BATCH_CONSUMED` / `RCMD_BULK_CREATE` protocol; `time_linux.c:132` (duplicate of the verified item).

### 4. Ownership and deferred lifetime

Cause: adopted payloads, temporary Vulkan objects, allocator domains, or deferred pointers have no single owner responsible for every failure and teardown edge. Several registries are the sole path to cleanup but cannot report that adoption failed.

Swoop: encode ownership transfer in APIs and use a cleanup stack or single unwind epilogue for partially built objects. Destroy drains adopted-payload rings before freeing their storage. Registration returns a Result and happens before publication. Deferred records deep-copy caller-owned data. Allocator provenance remains attached across translation-unit and library seams.

Verified: `ano_audio.c:204`; `log_core.c:205` [X]; `swapchain.c:110`; `texture.c:426`; `flat.c:244`; `components.c:72`; `ano_GltfParser.c:277`; `ano_render_bridge.c:92`.

Leads: memory↔every-module allocator partition; `instance.c:192`; cgltf↔memory allocator provenance; `vulkanMaster.c:593`; `slot_upload.c` growth VRAM; vulkan_backend↔render_bridge adopted-block contracts.

### 5. Mirrored state and inventory drift

Cause: a hand-maintained sibling, feature list, generated table, descriptor binding, platform implementation, or copied block omits one member or retains one wrong token. Recreated resources do not carry an authoritative list of dependents to repoint.

Swoop: replace parallel lists with one declarative inventory that generates counts, creation, destruction, sharing flags, and descriptor rewrites. Add parity tests for platform siblings and generated Unicode tables, centralize duplicated low-level loops and types, and have resource recreation walk an explicit dependency registry before the old handle is destroyed. Where a C struct mirrors a GLSL block, give the shared types the alignment their layout standard requires and static-assert size and member offsets against the std140/std430 rule rather than trusting member order.

Verified: `audio_linux.c:168` [X]; `ano_meshoptimizer.c:955` [X]; `texture.c:435` [X]; `device.c:663` [X]; `descriptors.c:39`; `commands.c:82`; `slot_upload.c:277`; `ano_strings_collate.c:75`; `threads_macos.c:79`; `anoptic_math.h:21` [X].

Leads: `filesystem_win64.c:33`; `memalign_win64.c:1`; `ano_collate_tables.h` decomp table as possible dead weight (post-closure, zero kept decomps lack a direct CE listing); text↔UI duplicated geometry helpers and `AnoQuad`; render↔text C/GLSL coverage drift; swapchain-recreate descriptor dependency coverage.

### 6. State-machine and concurrency lifecycle

Cause: state that should belong to one context is static, a recovery or teardown transition does not reset/re-anchor all dependent state, or ordering is assumed without a synchronization/lifetime proof.

Swoop: move mutable scratch and lazy tables into engine/context ownership or immutable one-time initialization; make start, stop, loss, suspend, resume, and teardown explicit state transitions with reset obligations; document and test the happens-before edge for every cross-thread field.

Verified: `audio_win64.c:589`; `music_voicing.c:114`; `time_win64.c:148`.

Leads: threads↔log-crash altstack teardown order; `music_host.c override_apply`; log-crash↔log teardown; `transformStream reclaimSeq`; cross-platform time suspend semantics.

### 7. Algorithm and contract one-offs

Cause: these are genuine local semantic defects, not credible instances of a broad missing mechanism. Folding them into a generic framework would obscure the fix.

Swoop: repair each locally and pin it with the named regression test or a focused contract test. Resolve contract disagreements before choosing code behavior.

Verified: `audio_fx.c:100` [X]; `text_shape.c:126` [X].

Leads: dead `src/render/gltf/scratch_process.c`; shared texture color/data-space semantics; `time_win64.c:316` scheduler-yield contract.

## Fine-grained source tags

This preserves the source file's existing taxonomy. Counts are tag assignments, not unique findings.

Root causes only. The source file also carried one status tag, `pending-design-decision`, on the five findings once written up under "Open decisions" below; all five were settled by 2026-07-25 and the tag is gone from the census. It never replaced a root cause and was not counted here, so this table stays at 72 across 70 findings.

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
| alignment-contract-gap | 1 |
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
| Total assignments | 72 |

## Recommended order

1. Land contract gates and quantity helpers together, then route public/config/bridge/parser paths through them. This attacks 31 verified findings immediately and establishes the primitives used by later fixes.
2. Convert Vulkan and bridge operations to Result + unwind + commit semantics. Treat `ANO_FATAL` as diagnostic text unless and until its control-flow contract is deliberately changed.
3. Encode adopted-payload and Vulkan temporary ownership, including destroy-time ring drains and fallible registry adoption.
4. Build the renderer resource/dependency inventory and use it for descriptor budgets, sharing modes, growth/recreation repoints, and cleanup.
5. Audit context state and lifecycle transitions, then take the six local semantic fixes individually.

Each program should close its verified ledger entries and either promote or strike its leads. A lead is not counted as fixed merely because the nearby verified instance was fixed; it must be checked against the new invariant.




## Open decisions

All settled. Five verified entries outlived the 2026-07-24 one-off pass because each was blocked on a contract question with more than one defensible answer 〜 answers that write different code, where picking one silently inside a bug fix would settle a question the rest of the module has to live with. The distinction that put them here: a one-off has a correct answer derivable from the surrounding code 〜 a sibling that already guards, a header that already states the domain, a generated twin that already rejects. These had no such oracle in tree.

They resolved in two waves on 2026-07-25. log_core.c:817 went first, settled by a third answer neither horn anticipated (flush mid-pass: full width, no drops, flat memory). The remaining four fell to the five-tier policy (`.claude/skills/invariants/SKILL.md`): the destroy-time ring drain landed owner-side in both render_bridge and audio 〜 tier 1 completes the one asymmetric sibling among the drop paths, tier 2 rejects a type-erased release callback in the transport 〜 which also fixes the `anoring_spsc` migration contract at its minimum; the audio rides-home promise took a teardown clause outright, a two-phase drain being a liveness obligation exported to a party the module cannot enforce; music_voicing.c:114's ownership framing dissolved at the verify gate (the scratch is call-transient, so thread_local is the zero-obligation home, and music_theory.c:48's lazy bake died at tier 3 with it); and ano_strings_collate.c:75's closure direction dissolved under measurement 〜 the divergence set is empty, so the decomp trim drops dangling redirects and assert_trim_closure holds both generated tables closed from here on.

All four implementations landed the same day; every guard is green and the headless suite has no red test left. The write-ups, determinations and verification record are retired to `docs/BUGS_DONE.md`, "Settled open decisions".

## The last 27 〜 2026-07-25 enumeration

77 tallied − 50 fixed = 27 unremediated: 26 open entries plus the one declined repair. Enumerated 2026-07-25, with the Render/Vulkan block split into genuine renderer defects (ingest, VRAM handling, rendering logic) versus resource management (parsing, ownership or lifetime of assets before and after they reach the GPU). The split was surveyed against the code entry by entry and adjudicated to one boundary: an entry is resource management only when the defect sits in a resource's custody chain 〜 something acquired and never discharged, or an ownership record dropped, stranded, or double-booked 〜 and renderer whenever the defect is in how the GPU is driven with resources already held.

### Audio (1)

1. audio_win64.c:589 — DSound's DSBSTATUS_BUFFERLOST recovery calls Restore then Play without rewriting or silencing the ring and without resetting writeCursor, so up to four blocks of undefined restored buffer contents play and the stale cursor writes out of phase with the restarted play cursor. recovery-desync; no trigger seam (needs real dsound.dll focus loss).

### Filesystem (1)

2. filesystem_win64.c:137 — ano_fs_write's loop has no forward-progress check, so a WriteFile returning TRUE with 0 bytes written spins forever instead of returning −1; the Linux twin is immune by POSIX. unbounded-spin.

### Render / Vulkan 〜 renderer (15)

3. shadow_casters.c:97 — register_static_shadow indexes shadowTypeUsed[3] with the raw bridge light type; nothing validates it between the seam and the array.
4. slot_upload.c:221 — ensureEntityCapacity's && growth chain publishes per arm as it walks; a mid-chain failure leaves half the buffers swapped.
5. slot_upload.c:277 — growth re-points descriptors through updateUboDescriptorSets alone; the shadowsetup compute set's binding 1 keeps a live binding to the destroyed transform SSBO.
6. descriptors.c:39 — the descriptor-pool sampler budget enumerates every consumer by name but has no term for the task-cull Hi-Z sampler binding 13 adds when taskCull is on.
7. commands.c:82 — per-frame camera UBOs are minted EXCLUSIVE with no asyncLc arm while the cluster descriptor sets bind them from the other queue.
8. commands.c:201 — stagingTransfer's copy-failure arm is dead code: copyBuffer returns true unconditionally.
9. texture.c:415 — createTextureImage discards createDataBuffer's bool and memcpys the decoded image through the unwritten out-params (the NULL write the texacquire guard trips).
10. texture.c:437 — generateMipmaps' bool is discarded while the fallback whole-chain layout transition sits commented out beneath it.
11. swapchain.c:428 — createImageView's failure arm logs and returns a local the failed vkCreateImageView left undefined.
12. scene_buffers.c:35 — createMaterialBuffer's vkCreateBuffer failure logs ANO_FATAL (which doesn't abort) and falls through into vkGetBufferMemoryRequirements on an undefined handle.
13. vulkanMaster.c:505 — initVulkan's depth and Hi-Z arms log FATAL "Quitting init" and fall through, while every sibling arm proves the intended contract with unInitVulkan(); return false.
14. window.c:214 — initWindow guards glfwInit but never checks glfwCreateWindow's NULL return.
15. compute.c:83 — ano_vk_init_compute discards createShaderModule's documented NULL failure sentinel at all nine consumption sites.
16. flat.c:90 — every graphics-side pipeline builder discards the same NULL shader-module sentinel and bakes the dead handle in.
17. record.c:29 — recordCommandBuffer checks vkBeginCommandBuffer but only logs; the function is void so drawFrame has no failure channel, and the asyncLc split repeats the shape twice.

### Render / Vulkan 〜 resource management (7)

18. apply.c:125 — the RCMD_CREATE arm forwards cmd.render_id into render_slots_alloc with no mapped-check on either side; the alloc contract's one invariant ("render_id unmapped") is enforced by nobody.
19. render_slots.c:92 — render_slots_alloc_range publishes each element's mapping as it walks, then fails mid-batch with only a scalar return, leaving the prefix live and aliasable.
20. swapchain.c:110 — initSwapChain consumes the by-value support-details struct and returns on both arms without freeing its two calloc'd arrays (formats, presentModes).
21. texture.c:426 — both failure returns orphan the live staging VkBuffer acquired at :415; only the success epilogue discharges it.
22. components.c:72 — ano_vk_register_texture returns void and its realloc-failure arm drops the TextureData record, so the glTF caller can't hear the refusal and the texture orphans permanently (still drawn, unreachable at teardown).
23. flat.c:244 — flat_init_with_cull leaks its three shader-code buffers and minted modules on failure paths.
24. ano_GltfParser.c:277 — parseGltf hears createTextureImage's false and discharges nothing; the failure route frees the host arrays holding the only copies of the device handles.

### Threads (1)

25. threads_macos.c:79 — the Darwin barrier gap-fill can release waiters early and silently erase an arrival racing the reset under over-subscribed reuse (more threads than count, cohort by cohort); exactly-count usage is provably correct, and glibc's Linux implementation handles the shared-cohort case.

### Time (1 + the wontfix)

26. time_win64.c:148 — TSC mode never re-anchors across S3 sleep/S4 hibernate, so the first post-resume stamp lands below every pre-sleep stamp: the monotonic promise breaks exactly on machines that elected the fast path, and every held delta wraps toward 2⁶⁴. clock-not-reanchored.
27. time_win64.c:310 (+ macOS twin) — wontfix, the one declined repair; analysis stands, counts as unremediated.


## The 2026-07-25 remediation 〜 campaign record

The 27 enumerated above were taken the same day by a four-round fix campaign. This is its accounting; the fixes themselves are retired to `docs/BUGS_DONE.md` and what remains open is in `docs/BUGS.md`.

Nineteen of the 27 were fixed: the 15 renderer entries and the four platform singletons `audio_win64.c:589`, `filesystem_win64.c:137`, `threads_macos.c:79` and `time_win64.c:148`. The seven resource-management entries were fenced deliberately and stay open with their guards red on purpose 〜 `apply.c:125`, `render_slots.c:92`, `swapchain.c:110`, `texture.c:426`, `components.c:72`, `flat.c:244`, `ano_GltfParser.c:277`. The one declined repair, `time_win64.c:310`, was not touched. The fence held under its own evidence rather than by assertion: four independent rounds worked inside those same files and each stopped exactly at the boundary, twice declining a tier-1 acquisition helper in `flat.c` precisely because it would have implemented half of `flat.c:244` inside a shader-module fix, and three times landing texture work that sits strictly before the acquisition `texture.c:426` owns.

Round 1 took the 19 census entries across eight clusters and logged 21 strays 〜 defects found beside a fix, on the same seam or in the same file, that were not census entries. Rounds 2, 3 and 4 worked those strays and the ones they in turn surfaced: round 2 fixed 15 and logged 16 new, round 3 fixed 17 and logged 11 new, round 4 fixed 12 and logged 17 residuals which were ledgered rather than worked. The stray flow converged 〜 21, 16, 11 〜 and round 4's residuals are not a fourth wave of the same kind: they are the resource-management fence family, warning hygiene, documentation lag, and seams blocked on the open design questions below. Forty-four stray fixes landed in all. Forty-one are source defects; three are test-side (two guards authored to pin invariants nothing in `tests/` read, one guard re-authored because an adjudicated ruling inverted what its tail phase asserted).

The arithmetic behind the tally table in `docs/BUGS.md`. Before the campaign the board read 50 fixed of 77 tallied, i.e. 27 unremediated. Fixed rises by 19 census retirements plus 41 stray fixes, so 50 + 60 = 110. Tallied rises by those same 41 stray fixes, which enter the census already fixed, plus the ten findings the campaign logged and did not fix, so 77 + 51 = 128. Open is 128 − 110 = 18: the seven fenced resource-management entries, the one declined repair, and the ten new entries. Per module, fixed and tallied move as Audio 6|7 to 10|12 (one census entry, three Windows stray fixes, two new open entries), Filesystem 1|2 to 2|2, Threads 0|1 to 1|1, Time 3|5 to 4|5, and Render/Vulkan 10|32 to 63|78 (15 census plus 38 stray fixes, and eight new open entries). Every other module is unchanged. The three test-side fixes carry no tally line, which is why 44 stray fixes move the board by 41. The severity tables and the fine-grained tag table above describe the census as found and are deliberately not rewritten.

Verification. The render suite stands at 87 of 94 with exactly the seven fenced reds and no other failure; the headless suite is 57 of 57. All 15 renderer census guards flipped red to green with no assertion touched 〜 the fixes met the tests the census had already written, which is the strongest single statement the campaign can make about itself. Two guard files needed link-stub retypes only, where a fix changed a stubbed signature: `recordCommandBuffer` becoming `[[nodiscard]] bool` broke `anotest_initdepthguard`'s void stub and left `anotest_recordbeginguard`'s own call discarding a result. Seventeen new guards were authored and registered, three in round 1 and fourteen across rounds 2 to 4, each observed red before its fix or built as a negative control against the pre-fix tree. Four fixes have no runnable gate anywhere in this campaign: the three Win64 ones, which are inspection-verified against documented API contracts with real-hardware checklists carried in `docs/BUGS_DONE.md`, and the Darwin barrier, which is the exception 〜 verified on-host with a standalone cohort-accounting harness as a negative control and clean under ThreadSanitizer, with its in-tree guard green in the 57/57 run.

Method. Every fix walked the five-tier hierarchy and recorded a rejection line for each rung it passed over, which is what makes the record auditable rather than merely confident: the rejections are where the reasoning is, and several are more informative than the fixes 〜 tier 2 declined nine times over because a Vulkan struct field is a raw handle and no proof-token survives the boundary, tier 4 declined wherever N mints have no dominating ingress because that is tier 5 wearing a hat, tier 5 declined in a spin barrier because a guard that narrows a window without closing it makes the barrier usually right, which is worse than honestly wrong. Where a fix would have settled a contract question with more than one defensible answer, the question was lifted out and adjudicated rather than decided inside the patch; seven were, and each is now written into the tree at a named place, recorded in `docs/BUGS_DONE.md` under "Adjudicated contracts (2026-07-25)". The rounds ran as fleets over disjoint file whitelists, which is what kept concurrent fixers from colliding and is also why so many strays exist: an agent that finds a defect one file outside its surface must log it rather than fix it, and the campaign's shape is the accumulation and then the draining of that log.

Rounds 5 and 6 〜 2026-07-25. On the user's instruction the round-4 disposition above was reversed for the six entries it had ledgered rather than fixed. The standing order is that an in-scope defect found during the campaign gets fixed, and all six were in scope: `audio_mixer.c:616`, `audio_win64.c:389`, `scene_buffers.c:43`, `texture.c:381`, `apply.c:66` and `apply.c:57`. Round 5 fixed all six plus the `backend.h` doc-lag the light work had outrun; round 6 took the four strays round 5 raised 〜 the `geometry_pool_upload` refusal sentinel with its fallback-mesh unwind, the `anoptic_render.h` `castsShadow` static-lane documentation, `refresh_static_shadow`'s declaration home, and a stale comment in `anotest_staticrowdecodeguard` 〜 and fixed every one inside the round. So the stray flow that ran 21, 16, 11, 17 ran genuinely dry: round 6 deferred nothing. Eleven fixes. The board moves 110|128 to 120|132 with open falling 18 to 12, since six were already tallied as open entries, four enter the census already fixed, and the eleventh is test-side and carries no tally line 〜 the same rule that made 44 stray fixes move the board by 41; the per-module arithmetic is in `docs/BUGS.md` beside the table. Five guards were authored and are now registered: `anotest_audiopaceguard` headless, and `anotest_scenebindguard`, `anotest_texpixeldomainguard`, `anotest_staticcastdropguard` and `anotest_fallbackmeshguard` render-side, each observed red against its own pre-fix TU and green after. Two of the eleven have no runnable gate: `audio_win64.c:389`, Win32-only, whose checklist is under "Platform verification posture" in `docs/BUGS_DONE.md`, and the three comment-and-declaration fixes, which have no runtime surface. Three more contract questions were adjudicated rather than decided inside a patch, bringing that record to ten. The seven-entry resource-management fence held through both rounds: no discharge, zeroing or comment was added on any fenced arm, and `anotest_texstagingguard` stays red with exactly its two failures.

## Open design questions

Three forks the campaign could not close, because every one of them is a product or ownership decision rather than a correctness reading, and landing either branch ahead of the owner would settle it silently. Rounds 5 and 6 struck four of the original seven; what they settled and where is recorded at the foot of this section. None of the three below blocks an open entry any more 〜 each is a shape question about surface the campaign left working.

The Monitors ledger has no reader. `enumerateMonitors` is now hardened, total and correctly ordered, but nothing in the tree reads `monitorInfos[i].modes` or `monitorCount` for anything load-bearing 〜 `initWindow` re-queries GLFW for its own list and bound. Keep it as a ledger waiting for its first consumer (a monitor/resolution config surface, which the `vulkanConfig` getters clearly anticipate), or delete `enumerateMonitors`, `cleanupMonitors`, `MonitorInfo` and `Monitors.monitorInfos` outright and let `initWindow`'s own query be the single monitor query in tree, which dissolves this round's whole ordering problem by construction rather than by discipline. Deleting is the larger correctness win and removes public-ish surface across `structs.h`, `instanceInit.h` and `vulkanConfig`.

Should `texture.h` export a bindless-capacity predicate? Every consumer currently latches the registrar's published refusal word locally, which keeps `texture.c` the sole decoder of fullness at the cost of one wasted decode per consumer per pass and a latch re-implemented in each future consumer. A predicate mints a second reader of the condition that goes stale against any interleaved registration; the campaign's own suffixed-checked-variant shape 〜 a registrar face that answers the slot and reports closure together 〜 is the better answer and changes a signature four texture guards compile against. It also interacts with whether bindless slots will ever be released, which is the whole premise of the latch.

Should `PipelinePrototype`'s `(implementationCount, implementations)` pair collapse into the type? Commit-last at twelve writers and count-first at teardown now hold the pair honest, but implementation counts are 1, 2 or 3, so an inline `PipelineImplementation[3]` would delete the pair and the entire unchecked-calloc family by construction 〜 there would be no allocation to fail and no half-built state to observe. It touches `components.h` and is the structural move if the prototype layout is ever revisited.

Struck by rounds 5 and 6, all four settled the same day and written into the tree. The `castsShadow` 0 -> 1 grant question took the first horn: the drop stays, grants stay CREATE-only, and the refusal is made audible with one ANO_ERROR at the drain seam 〜 the second speaker was accepted because the alternative reverses a clause settled twice, and the rate bound on that new diagnostic became its own adjudication (unbounded per command, matching `gate_light_domain`). The fallback CUBE unwinds like its texture sibling, and the question that made it hard 〜 a refusal spelled 0, which is `FALLBACK_MESH_INDEX` 〜 was dissolved rather than answered: `ANO_MESH_NONE` moves the refusal out of the grantable domain, following the round-3 bindless precedent, after which the arm's test can fire at all. `refresh_static_shadow` is declared in `backend.h` beside its two siblings and the contract prose, and the `backend.h:34-35` amendment landed a round earlier, so the provisional home is gone. And nothing new observes that a device thread is gone, because nothing needed to: `blockRing.head` is the consumer's own cursor and the producer already loads it every turn, so head unmoved for a whole ring drain is a strictly stronger observation than the liveness flag this question proposed 〜 the flag needs five backend TUs to remember the store and still misses a device thread that is alive but wedged, which is exactly the WASAPI packet wedge round 5 fixed beside it. The rulings are in `docs/BUGS_DONE.md`, "Adjudicated contracts (2026-07-25)".
