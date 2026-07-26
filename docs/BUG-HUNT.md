# Bug hunt

Remediation map for the 2026-07-21 census of `docs/BUGS.md`. Active defects live in `docs/BUGS.md`. Retired defects, fix records, and campaign history live in `docs/BUGS_DONE.md`.

As of 2026-07-26 the board is **139 fixed of 151 tallied**, with **11 open** and **1 wontfix**. The systemic swoops named below are largely spent, and the last three entries blocked on a product decision are closed 〜 their fix records are in `docs/BUGS_DONE.md`, and nothing open is waiting on a contract choice.

Open lines in `docs/BUGS.md` are unfinished code. Fixes below are keyed only by those `file:line` entries. An open entry does not mean the fix is undecided.


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

Tag assignments from the source census (not unique findings). The status tag `pending-design-decision` once marked five entries; those retired write-ups live under Settled open decisions in `docs/BUGS_DONE.md`. It is not counted here. The tag currently marks nothing.

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


## Result-domain rule

Failure modes are values when callers must react differently. Not `Result<T, E>` cosplay 〜 C interfaces that tell the truth.

- `bool` for genuine binary facts: found/not found, empty/nonempty, accepted/backpressure when those are the only outcomes.
- A domain-local result type when outcomes imply different control flow: retry, degrade, reject, unwind, or abort initialization.
- A result struct when success also returns a value or ownership package.
- Actionable results are `[[nodiscard]]`.
- Switch exhaustively without `default`, so adding a code forces every policy site to revisit.
- Results stay domain-specific; there is no engine-wide error enum.

Collapsed result domains are how the census’s big families look in practice: `ANO_FATAL` fallthrough, partial out-parameters, ownership leaks, retry exhaustion, and OOM mistaken for backpressure.


## Fixes for open entries

Eleven open lines in `docs/BUGS.md`, plus `time_win64.c:310` wontfix. Each subsection is the fix for that entry (or shared fix for a listed set). No other names. The `main.c:691`, `ano_GltfParser.c:435` and texture-custody subsections retired with their entries on 2026-07-26.


### `shadow_resources.c:22`

Labelled unwind (or local acquisition ledger): inert-init destinations; `goto fail` instead of mid-function returns; one cleanup path in reverse dependency order; return `false` only after local construction state is discharged. Success path unchanged.


### `ano_GltfParser.c:236`

At primitive record creation set `geometryPoolIndex = ANO_RENDER_NO_MESH`. Only a successful geometry upload overwrites it. Every early exit then fails closed.


### `text_gpos.c:294`

Filter to PairPos before spending the lookup budget. Return nonzero when relevant lookups or subtables still exceed capacity. `0` remains success including legitimately no kerns.


### `music_host.c:45`, `music_host.c:232`, `music_host.c:226`

Reject invalid override values at ingress. Cadence: pin of `-1` refused; config `NONE` is fallback, never a table index; returned policy ∈ `[0, ANO_CADENCE_POLICY_COUNT)`. Register center: MIDI 0–127, reject non-finite/non-integral/out of range (no silent clamp). Tempo: same mapped domain as the mapping table (apparently 60–160), reject NaN/inf/≤0/out of range. Explicit RELEASE remains the clear path.


### `filesystem_win64.c:33`

Trim the executable filename first, then validate the directory length against `MAXPATH` (POSIX sibling order). Keep a post-trim bound on the copy.


### `time_win64.c:402`

`ano_sleep` for requests at or below 1 ms (including exactly 1000 µs) must enter the scheduler at least once and must not return early merely because a yield returned. `ano_busywait` remains the explicit spin API.


### `memory.c:9`

`ano_heap_release`: destroy only if `*in` is non-NULL, then clear.


### `instance.c:192`

Stop `strdup`ing extension names. Allocate the pointer array only; store borrowed GLFW/literal pointers; free the array on both arms after `vkCreateInstance`.


### `ano_strings_collate.c:504`

Allocation-failure sort fallbacks must remain stable. Not `qsort`. Prefer an in-place stable hybrid (insertion on small runs, stable merge). Equal keys keep input order; symbol numeric value is not a tie-break.


## Deferred surface notes

Non-blocking. For a later layout pass.

- Monitors ledger: hardened but unread; `initWindow` re-queries GLFW. Keep for a future config surface, or delete and let `initWindow` own the only query.
- Bindless capacity: consumers latch the registrar’s refusal word. Prefer a checked face that answers slot and closure together over a stale second reader.
- `PipelinePrototype` inline `PipelineImplementation[3]`: would delete the count/pointer pair and the unchecked-calloc family. Touches `components.h`.

Campaign history is in `docs/BUGS_DONE.md`.
