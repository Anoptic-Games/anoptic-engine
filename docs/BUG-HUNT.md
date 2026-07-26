# Bug hunt

Remediation map for the 2026-07-21 census of `docs/BUGS.md`, plus the product decisions that gated the residue. Active defects live in `docs/BUGS.md`. Retired defects, fix records, settled decisions, and campaign history live in `docs/BUGS_DONE.md`.

As of 2026-07-26 the board is **129 fixed of 147 tallied**, with **17 open** and **1 wontfix**. The systemic swoops named below are largely spent. The three product forks that blocked autonomous residue work are settled below; implementation may proceed from those rulings.


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

Seventeen open entries in `docs/BUGS.md`, plus the `time_win64.c:310` wontfix in `docs/BUGS_DONE.md`. No open product decision remains; the three forks below are settled and implementation may proceed.

| Cluster | Entries | Ruled by |
|---|---|---|
| Texture mint/adopt/register | `texture.c:426`, `texture.c:486`, `ano_GltfParser.c:277`, `components.c:72` | Decision 2, then mechanical ownership transfers |
| Shadow-resource unwind | `shadow_resources.c:22` | Existing constructor/unwind contracts |
| Music override ingress | `music_host.c:45`, `:232`, `:226` | Existing music domains |
| glTF primitive default | `ano_GltfParser.c:236` | `ANO_RENDER_NO_MESH` sentinel |
| Mixed colour/data textures | `ano_GltfParser.c:435` | Decision 1 |
| GPOS capacity | `text_gpos.c:294` | Existing malformed/partial contract |
| HUD / owned-payload submit | `main.c:691` (expands to UI + bulk-copy siblings) | Decision 3 |
| Platform / hot-path one-offs | `filesystem_win64.c:33`, `time_win64.c:402`, `memory.c:9`, `instance.c:192`, `ano_strings_collate.c:504` | Sibling / header contracts |


## Result-domain rule

Failure modes should be values when callers must react differently. Not `Result<T, E>` cosplay everywhere 〜 C interfaces that tell the truth.

- Use `bool` for genuine binary facts: found/not found, empty/nonempty, accepted/backpressure when those are truly the only outcomes.
- Use a result enum when outcomes imply different control flow: retry, degrade, reject, unwind, or abort initialization.
- Use a result struct when success also returns a value or ownership package.
- Mark actionable results `[[nodiscard]]`.
- Switch exhaustively without `default`, so adding a result forces every policy site to be revisited.
- Keep results domain-specific; avoid one vague engine-wide error enum.

The census strongly supports this. Several major families 〜 `ANO_FATAL` fallthrough, partial out-parameters, ownership leaks, retry exhaustion, and OOM mistaken for backpressure 〜 are collapsed result domains. The renderer submission enum (Decision 3) is a precedent for that discipline, not merely a HUD fix.


## Settled decisions (2026-07-26)

Three product forks settled the same day. Implementation follows these rulings; do not re-open them inside a patch.

### 1. Mixed colour/data textures

Decision: represent one glTF image used in both colour and data roles as one mutable-format Vulkan image with separate SRGB and UNORM views and separate bindless indices.

This is the normal Vulkan-engine solution for compatible format interpretations. `VK_FORMAT_R8G8B8A8_SRGB` and `VK_FORMAT_R8G8B8A8_UNORM` share a compatible format class, so the texels and allocation need not be duplicated.

Implementation consequences:

1. Mixed-use images are created with `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`.
2. Prefer an attached `VkImageFormatListCreateInfo` listing the two allowed view formats, giving the driver a precise compatibility set.
3. Replace the current sticky `textureSrgb[t]` boolean with a per-texture usage mask: colour use; data use; both.
4. A colour-only texture needs an SRGB view; a data-only texture needs a UNORM view; a mixed texture needs both.
5. The parser needs per-domain bindless indices rather than the current single `bindlessIndices[t]`.
6. Material baking selects the colour or data index according to the material slot 〜 base colour, emissive, sheen colour, and similar colour-bearing slots use SRGB; normal, metallic/roughness, occlusion, transmission factors, thickness, and similar data slots use UNORM.
7. `TextureData` must own one image/allocation and up to two views.
8. Teardown destroys each distinct view once, then destroys the shared image once.
9. A mixed texture consumes two bindless descriptors but only one texture allocation.
10. If the second bindless registration fails, only material slots requiring that missing interpretation become untextured; they must not alias the other interpretation.
11. The texture registry adopts the shared image and both views as one resource record before either view is exposed through bindless registration.
12. Texture tests need colour-only, data-only, and mixed-use assets, verifying that 0.5 samples as SRGB-decoded approximately 0.214 in a colour role and as linear 0.5 in a data role.

Rejected alternatives:

- Duplicating the image wastes texture memory and upload bandwidth.
- “Linear wins” or “SRGB wins” leaves one material role objectively wrong.
- Rejecting legal mixed-domain glTF assets unnecessarily narrows importer compatibility.

Gates `ano_GltfParser.c:435`.

### 2. Failed GPU-allocation reclamation

Decision: preserve the existing arena allocator semantics.

A failed texture construction must destroy all Vulkan objects, total every output, and transfer no ownership. Any GPU-arena span consumed before the failure remains unavailable until allocator teardown.

This means:

1. `GpuAllocator` remains a monotonic arena with no individual free.
2. No checkpoint/rollback, free list, or allocator API expansion lands in this remediation.
3. Texture constructors zero all outputs before acquisition.
4. Failed construction destroys any staging buffer, image view, and image it created.
5. The associated `GpuAllocation` output is reset to the empty value even though its arena span remains consumed.
6. The parser receives no partial ownership on failure.
7. Failed registry adoption destroys the caller-owned views and image; the span still retires with the arena.
8. The behavior is documented explicitly: failure leaks no Vulkan object or ownership record, but may consume arena capacity until teardown.
9. Tests assert handle and ownership cleanup, not allocator-offset rollback.
10. Repeated failed hot-loading can reduce remaining arena capacity. That is accepted for now and becomes allocator work only if field evidence shows it matters.

This keeps the bug fix scoped to resource correctness rather than turning it into a GPU allocator redesign.

Gates the texture custody chain (`texture.c:426`, `texture.c:486`, `ano_GltfParser.c:277`, `components.c:72`). Ownership transfers after this ruling are mechanical: callee owns locals until return; `false` leaves all outs inert; `true` transfers a complete package; `ano_vk_register_texture` becomes `[[nodiscard]] bool`; bindless registers only after adoption; parser destroys what it still owns. Arena spans follow items 5 and 8 above.

### 3. Submission result API

Decision: replace ambiguous boolean results with a public result enum. No compatibility wrapper.

#### Result type

```c
typedef enum AnoRenderSubmitResult {
    ANO_RENDER_SUBMIT_ACCEPTED = 0,
    ANO_RENDER_SUBMIT_BACKPRESSURE,
    ANO_RENDER_SUBMIT_OOM,
    ANO_RENDER_SUBMIT_INVALID,
} AnoRenderSubmitResult;
```

`ACCEPTED` means the operation was accepted by the producer endpoint. For an enqueued command, ownership crossed into the bridge. It does not promise that a later render-side registry or device operation has already applied the command.

All result-returning declarations should be `[[nodiscard]]`.

#### API scope

Broader than `ano_render_text_set`. Every producer endpoint that allocates and copies an owned payload currently conflates allocation failure with ring backpressure. The enum therefore replaces `bool` on:

```c
AnoRenderSubmitResult ano_render_submit_bulk_update(
    AnoRenderBridge *bridge,
    const RenderUpdateBatch *batch);

AnoRenderSubmitResult ano_render_submit_bulk_destroy(
    AnoRenderBridge *bridge,
    const uint32_t *render_ids,
    uint32_t count);

AnoRenderSubmitResult ano_render_text_set(
    AnoRenderBridge *bridge,
    uint32_t text_id,
    const AnoGlyphInstance *instances,
    uint32_t count);

AnoRenderSubmitResult ano_render_text_clear(
    AnoRenderBridge *bridge,
    uint32_t text_id);

AnoRenderSubmitResult ano_render_ui_set(
    AnoRenderBridge *bridge,
    uint32_t ui_id,
    uint32_t layer,
    const AnoUiBuilder *ui,
    const AnoGlyphInstance *glyphs,
    uint32_t glyphCount);

AnoRenderSubmitResult ano_render_ui_clear(
    AnoRenderBridge *bridge,
    uint32_t ui_id);
```

The clear functions do not allocate, but they return the same type so count-zero SET operations can delegate without changing result domains and callers can use one exhaustive result switch.

The following remain boolean because they genuinely expose one binary condition and do not allocate an owned payload:

- `ano_render_submit`: accepted or command-ring full.
- `ano_render_stream_begin`: slice available or still GPU-owned.
- `ano_render_stream_commit`: command accepted or ring full.
- `ano_render_light_attach`, update, update-fields, and detach: accepted or ring full.
- `ano_render_poll_event` and snapshot/view acquisition: value available or unavailable.

`RCMD_BULK_CREATE` remains on the borrowed `ano_render_submit` path; the existing startup batch owns its storage and no copy allocation occurs inside the producer endpoint.

#### Exact result semantics

`ACCEPTED` 〜 returned when an owned payload was successfully allocated, packed, and enqueued; a clear command was enqueued; a documented zero-count bulk operation performed its no-op; or a documented count-zero text or empty UI operation successfully delegated to CLEAR. For an owned command, the caller relinquishes the packed block only on this result.

`BACKPRESSURE` 〜 returned only when the command ring refuses the push. If a packed block was already allocated: `ano_render_command_release` frees it; no command remains enqueued; the caller retains its original source arrays; retrying later is safe. No warning or error for ordinary backpressure.

`OOM` 〜 returned only when the producer cannot allocate the packed render-owned block. No command is enqueued; no ownership transfers; the caller’s arrays remain untouched; retry policy belongs to the caller’s content semantics; OOM is never represented as backpressure.

`INVALID` 〜 returned when caller input violates the endpoint contract. Examples: text count nonzero while `instances == NULL`; UI builder NULL; UI caps exceeded; UI glyph count nonzero while `glyphs == NULL`; a UI primitive carries an invalid clip, paint, glyph, stop, or path reference; bulk count nonzero while required arrays are NULL; a bulk field bit names an absent parallel value array; bulk size arithmetic cannot be represented. Invalid input is deterministic and must never be retried unchanged. The bridge may emit one contextual warning naming the invalid ID or field; callers should not duplicate it.

Existing documented behavior remains: text counts above `ANO_RENDER_TEXT_MAX` are truncated and can still be accepted; a non-NULL empty UI builder means CLEAR; zero-count bulk update/destroy is an accepted no-op; a NULL UI builder becomes INVALID rather than silently clearing an existing block.

#### Ownership consequences

| Result | Packed block exists after return | Command enqueued | Ownership |
|---|---:|---:|---|
| `ACCEPTED` | Yes, when the operation carries one | Yes | Bridge/render side |
| `BACKPRESSURE` | No | No | Caller retains original inputs |
| `OOM` | No | No | Caller retains original inputs |
| `INVALID` | No | No | Caller retains original inputs |

`ano_render_command_release` remains the sole drop-path decoder for owned command payloads. Bridge teardown remains responsible for draining and releasing accepted commands that were never consumed.

#### Engine call-site consequences

`hud_text_submit` returns `AnoRenderSubmitResult` and forwards `ano_render_text_set` exactly.

Four startup HUD submissions (title, notice, Unicode sample, Homer sample): `ACCEPTED` → next block; `BACKPRESSURE` → retry while `!g_logicShouldStop`, yielding; `OOM` → omit that optional block and continue; `INVALID` → drop the programmer-invalid block and continue; shutdown during backpressure → exit the logic thread. Eliminates the OOM infinite loop and shutdown deadlock.

Transient notice clear: `noticeCleared` becomes true only on `ACCEPTED`; `BACKPRESSURE` retries next tick; `OOM`/`INVALID` impossible for a valid clear, but the switch stays exhaustive.

Camera readout (replaceable state): `ACCEPTED` lands; `BACKPRESSURE`/`OOM`/`INVALID` drop this update 〜 no retry loop.

`submit_menu`, `submit_music`, `submit_bar` return `AnoRenderSubmitResult`. Dirty-state consumers must not treat every nonzero as success: `ACCEPTED` clears dirty (bar also sets `barSubmitted` / `barVpH`); `BACKPRESSURE` retains dirty and retries next tick; `OOM` retains desired state but must not retry allocation every 2 ms 〜 short deadline or wait for the next relevant state change; `INVALID` clears dirty for that exact built block (retrying identical data cannot succeed). Hidden menu/music delegates to CLEAR with the same ACCEPTED/BACKPRESSURE rules.

`submit_blocking` continues to call boolean `ano_render_submit` (no allocation ambiguity) but still needs shutdown-aware cancellation as part of the `main.c:691` liveness repair.

#### Bulk submission consequences

No in-tree callers of `ano_render_submit_bulk_update` / `ano_render_submit_bulk_destroy` today, but their public contracts already conflate OOM and ring-full. They adopt the enum now so future game code does not repeat the HUD defect. Additional `INVALID` validation: nonzero count requires `render_ids`; each field bit requires its array; checked byte-size accumulation before allocation; unknown field bits rejected or masked per `RenderFieldBits`; zero count remains `ACCEPTED` without allocation or enqueue.

#### C-language migration hazard

Changing `bool` to an enum does not guarantee a compiler error at old call sites. C permits enum values in boolean conditions. Every caller must compare a named enumerator explicitly (`if (result == ANO_RENDER_SUBMIT_ACCEPTED)`). `if (result)` / `while (!result)` are forbidden: with `ACCEPTED == 0`, old boolean-style code inverts success and can spin on an accepted submission.

Mitigations: `[[nodiscard]]` on every result-returning API; update every in-tree call in the same commit as the public signature; exhaustive switches without `default` under enum-switch diagnostics; search the tree for every affected function after conversion; regression tests on caller policy, not only raw enum values; clean rebuild (`_Bool` and enum return conventions need not be binary-compatible across stale objects). No compatibility wrapper and no transitional boolean alias.

#### Test consequences

Behavior belongs in the existing render-bridge suite (`tests/anotest_render_bridge.c`), not new per-bug executables. Cover: accepted text SET; text count-zero CLEAR; text invalid pair; text OOM; text ring-full after allocation proving the block is released; accepted UI SET; empty UI CLEAR; NULL/invalid UI; UI OOM; UI ring-full with no owned-block leak; accepted bulk update/destroy; bulk zero-count no-op; missing required bulk arrays; bulk OOM; bulk ring-full cleanup; bridge destruction with accepted text/UI/bulk blocks still queued. OOM needs a deterministic allocator-refusal seam via test linkage or an existing allocator fault mechanism, not a production-visible debugging API.

Engine-level or black-box coverage should additionally prove: startup continues after text OOM; startup backpressure exits on shutdown; notice clear retries only on backpressure; menu/music/bar dirty flags clear only on `ACCEPTED`; invalid UI does not retry forever; OOM does not produce a 2 ms allocation storm; render-thread shutdown joins cleanly from every retry state.

#### Documentation consequences (when implemented)

Together: define `AnoRenderSubmitResult` and change the six owned-payload/clear declarations in `include/anoptic_render.h`; update `src/render_bridge/ano_render_bridge.c` and `src/vulkan_backend/bridge/producer.c`; revise `docs/text/font-render.md` and `docs/ui/ui-render.md`; on landing, close `main.c:691` in `docs/BUGS.md`, move the fix record to `docs/BUGS_DONE.md`, and note that the audit expanded the repair to the sibling UI and bulk-copy endpoints while preserving the distinction between the source defect and consequential API cleanup.

#### Non-consequences

Unchanged: `RenderCommand` layout; ring storage or synchronization; `bulk_owned`; render-thread command decoding; text/UI registry adoption; GPU ABI; bare CREATE/UPDATE/DESTROY command behavior; light or stream submission semantics; back-channel polling and snapshots. The change is producer-side: allocation, validation, and queue pressure become different observable outcomes, and each caller adopts the correct policy.


## Deferred surface notes

These do not block any open entry. Recorded so a later layout pass does not rediscover them.

- Monitors ledger: `enumerateMonitors` is hardened but unread; `initWindow` re-queries GLFW. Keep as a future config surface, or delete the ledger and let `initWindow` own the only query.
- Bindless capacity predicate: consumers latch the registrar's refusal word locally. A checked face that answers slot and closure together is cleaner than a stale second reader; interacts with whether slots are ever released.
- `PipelinePrototype` inline array: counts are 1–3; an inline `PipelineImplementation[3]` would delete the `(implementationCount, implementations)` pair and the unchecked-calloc family by construction. Touches `components.h`.


## Recommended order

1. Platform one-offs 〜 ≤1 ms yield, NULL heap release, extension-name leak, game-path trim, stable sort fallback.
2. Music ingress trio 〜 reject bad overrides; cadence `NONE` is fallback not an index; shared domain constants.
3. Decision 3 〜 `AnoRenderSubmitResult` on owned-payload endpoints; HUD/UI/bulk callers; shutdown-aware `submit_blocking`.
4. GPOS filter-before-budget; glTF primitives default to `ANO_RENDER_NO_MESH`.
5. Decision 1 〜 mutable-format dual views and per-domain bindless indices.
6. Decision 2 〜 texture custody state machine (flip the three fenced guards together; arena spans retire at teardown).
7. `createShadowResources` labelled unwind using the same cleanup idiom.

Campaign history (the 27 of 2026-07-25, rounds 1–6, trivial-fix wave, adjudicated contracts, platform checklists) is in `docs/BUGS_DONE.md`.
