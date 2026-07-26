# Bug hunt

Remediation map for the 2026-07-21 census of `docs/BUGS.md`, plus the product decisions that still gate work. Active defects live in `docs/BUGS.md`. Retired defects, fix records, settled decisions, and campaign history live in `docs/BUGS_DONE.md`.

As of 2026-07-26 the board is **129 fixed of 147 tallied**, with **17 open** and **1 wontfix**. The systemic swoops named below are largely spent. What remains is isolated residue plus three human calls.


## Accounting

Census read of `docs/BUGS.md` on `fix-bughunt`, 2026-07-21. Four leads duplicated verified findings (`text_bake.c ano_text_window_sum`, `render_slots.c:84`, `ano_render_ui_set` validation, `time_linux.c:132`), so the distinct total is 107. Two findings carry two tags (`music_perc.c:121`, `texture.c:437`) → 72 tag assignments across 70 verified findings. The bucket table assigns one primary bucket each and does not double-count.

Amended 2026-07-24: the math-conventions lead split; convention half struck (header comment wrong), alignment half promoted to verified `anoptic_math.h:21`. Verified 69 → 70, leads 42 → 41, distinct concerns unchanged at 107.

The Fixed column in the bucket table is the **2026-07-24 one-off snapshot** (36 of 70). Later passes are tallied in `docs/BUGS.md` (Remediation status) and recorded in `docs/BUGS_DONE.md`.

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

Disciplines that retire findings together. Per-key ledgers from the 2026-07-24 snapshot are retired: every verified key from that pass is in `docs/BUGS_DONE.md` or the active `docs/BUGS.md` board. The table and the swoop definitions stay as the census taxonomy.

| Bucket | Verified | Fixed (2026-07-24) | Lead records | Verified/lead overlap | Distinct total |
|---|---:|---:|---:|---:|---:|
| Contract gates | 14 | 11 | 11 | 2 | 23 |
| Quantity safety | 17 | 15 | 2 | 0 | 19 |
| Fallibility and atomic commit | 16 | 2 | 7 | 2 | 21 |
| Ownership and deferred lifetime | 8 | 1 | 6 | 0 | 14 |
| Mirrored state and inventory drift | 10 | 5 | 6 | 0 | 16 |
| State-machine and concurrency lifecycle | 3 | 0 | 6 | 0 | 9 |
| Algorithm and contract one-offs | 2 | 2 | 3 | 0 | 5 |
| Total | 70 | 36 | 41 | 4 | 107 |

### 1. Contract gates

Cause: values cross a public API, config, bridge, parser, or third-party seam without the receiving module proving the domain its interface promises.

Swoop: one validator/adopter per ingress; reuse it for public calls, config, overrides, bridge commands, and parsers.

### 2. Quantity safety

Cause: sizes, counts, offsets, durations, indices, shifts, or rendered footprints wrap, truncate, exceed fixed storage, or describe a different metric than the allocation.

Swoop: checked arithmetic helpers as the only path into allocation and capacity checks; fixed-array limits next to producers; size deferred output by rendered bounds.

### 3. Fallibility and atomic commit

Cause: failure logs and continues, reports unconditional success, consumes an unwritten out-param, spins without progress, or publishes a prefix before the transaction succeeds. `ANO_FATAL` is a log level, not control flow.

Swoop: Result + unwind + commit-last; stage privately and publish once; retry loops distinguish success, exhaustion, cancellation, and zero progress.

### 4. Ownership and deferred lifetime

Cause: adopted payloads, temporary Vulkan objects, or deferred pointers have no single owner on every failure and teardown edge.

Swoop: encode transfer in APIs; one unwind epilogue for partial builds; destroy drains adopted rings before freeing storage; registration returns a Result before publication.

### 5. Mirrored state and inventory drift

Cause: a hand-maintained sibling list, generated table, descriptor binding, or platform twin omits or mis-tokens one member; recreations miss dependents.

Swoop: one declarative inventory for create/destroy/rebind; parity tests for siblings and generated tables; std430/std140 alignment welded by static_assert.

### 6. State-machine and concurrency lifecycle

Cause: mutable scratch is static; recovery/teardown skips reset; ordering is assumed without a happens-before proof.

Swoop: engine/context ownership or immutable init; explicit start/stop/loss/resume/teardown with reset obligations.

### 7. Algorithm and contract one-offs

Cause: local semantic defects, not instances of a missing broad mechanism.

Swoop: repair locally; pin with a focused regression test.


## Fine-grained source tags

Tag assignments from the source census (not unique findings). The status tag `pending-design-decision` once marked five entries; those settled 2026-07-25 and live under Settled open decisions in `docs/BUGS_DONE.md`. It is not counted here.

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


## Residue today (2026-07-26)

Seventeen open entries in `docs/BUGS.md`, plus the `time_win64.c:310` wontfix in `docs/BUGS_DONE.md`.

| Cluster | Entries | Needs a human call? |
|---|---|---|
| Texture mint/adopt/register | `texture.c:426`, `texture.c:486`, `ano_GltfParser.c:277`, `components.c:72` | Call 2 (arena policy), then mechanical |
| Shadow-resource unwind | `shadow_resources.c:22` | No |
| Music override ingress | `music_host.c:45`, `:232`, `:226` | No |
| glTF primitive default | `ano_GltfParser.c:236` | No |
| Mixed colour/data textures | `ano_GltfParser.c:435` | Call 1 |
| GPOS capacity | `text_gpos.c:294` | No |
| HUD/audio submit cancel | `main.c:691` | Call 3 |
| Platform / hot-path one-offs | `filesystem_win64.c:33`, `time_win64.c:402`, `memory.c:9`, `instance.c:192`, `ano_strings_collate.c:504` | No |

Everything except the three calls below is derivable from existing contracts once those defaults are chosen. Implementation notes for the autonomous work live with the entries in `docs/BUGS.md` and the residue brief that produced this map.


## Open decisions

Three product forks. Landing either branch inside a bug fix would settle them silently. Recommended defaults unblock the rest of the board without further forks.

### 1. Mixed colour/data textures

When one glTF image serves both colour and data slots, choose the resource representation.

- Recommended: one mutable-format image, separate SRGB and UNORM views, separate bindless indices per material role.
- Alternatives: duplicate the GPU image, or reject mixed-domain assets.

Affects memory, Vulkan compatibility policy, and the material/resource model. Gates `ano_GltfParser.c:435`.

### 2. Failed GPU-allocation reclamation

`GpuAllocator` is intentionally arena-based and cannot free individual spans. Decide whether a failed texture load may consume arena space until teardown.

- Recommended for this campaign: preserve arena semantics; destroy every Vulkan object; total every output; document that failed allocation spans retire at allocator teardown.
- Stronger: transactional checkpoint/rollback or reusable GPU allocation (broadens into allocator work).

Gates the texture custody chain (`texture.c:426` / `:486`, `ano_GltfParser.c:277`, `components.c:72`). With the recommended default, ownership transfers are mechanical: callee owns locals until return; `false` leaves all outs inert; `true` transfers a complete package; `ano_vk_register_texture` becomes `[[nodiscard]] bool`; bindless registers only after adoption; parser destroys what it still owns.

### 3. Submission result API

`ano_render_text_set` returns false for both ring-full and allocation failure; the public header says false means ring full.

- Recommended: result-returning API (accepted / backpressure / OOM / invalid) with a compatibility bool wrapper.
- Alternatives: break/replace the bool API, or keep bool and only bound/cancel retries (reason stays ambiguous).

Gates `main.c:691` and the adjacent `submit_blocking` / audio console startup loops. Once chosen: retry backpressure while `!g_logicShouldStop`; degrade on OOM; do not spin on invalid; audio keeps ring-full-only false but needs cancellation and a bounded startup deadline.


## Deferred surface notes

These do not block any open entry. Recorded so a later layout pass does not rediscover them. Not product calls for the current residue.

- Monitors ledger: `enumerateMonitors` is hardened but unread; `initWindow` re-queries GLFW. Keep as a future config surface, or delete the ledger and let `initWindow` own the only query.
- Bindless capacity predicate: consumers latch the registrar's refusal word locally. A checked face that answers slot and closure together is cleaner than a stale second reader; interacts with whether slots are ever released.
- `PipelinePrototype` inline array: counts are 1–3; an inline `PipelineImplementation[3]` would delete the `(implementationCount, implementations)` pair and the unchecked-calloc family by construction. Touches `components.h`.


## Recommended order

Assuming the recommended defaults above:

1. Platform one-offs 〜 ≤1 ms yield, NULL heap release, extension-name leak, game-path trim, stable sort fallback.
2. Music ingress trio 〜 reject bad overrides; cadence `NONE` is fallback not an index; shared domain constants.
3. Land call 3 → HUD/audio cancellation and truthful submit results.
4. GPOS filter-before-budget; glTF primitives default to `ANO_RENDER_NO_MESH`.
5. Land call 1 → dual colour/data representation.
6. Land call 2 → texture custody state machine (flip the three fenced guards together).
7. `createShadowResources` labelled unwind using the same cleanup idiom.

Campaign history (the 27 of 2026-07-25, rounds 1–6, trivial-fix wave, adjudicated contracts, platform checklists) is in `docs/BUGS_DONE.md`.
