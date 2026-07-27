# Bugs

Active bug ledger and reusable audit template. The 2026-07 campaign is closed: all 151 tallied findings are retired in `docs/BUGS_DONE.md`, with 149 fixed, one refuted, one wontfix, and zero open.

GitHub Issues are the canonical home for newly discovered standalone defects. Use this file for batch audits, offline findings awaiting an issue, and the shared classification rules below. Once an issue exists, link it from the entry; do not maintain two independent status narratives.


## Use

1. Verify the behavior at source before tallying it. Trace the value, ownership, or state transition through every caller, cleanup path, and platform twin; the shadow-resource false positive is the standing reminder that a local early return is not proof of a leak.
2. Write one entry under the owning module and category. Begin with `file:line`, state the broken contract, establish reachability, name the consequence, record the date, and name the test or explain why no practical test exists.
3. Put root-cause tags on separate bullets immediately below the entry. Add severity and the GitHub issue link when applicable.
4. Keep unverified suspicions under Leads. Leads are not bugs, do not enter totals, and either graduate into a module entry or are deleted when refuted.
5. On disposal, move the entry to `docs/BUGS_DONE.md` and add one dated disposition bullet: `fix`, `refuted`, or `wontfix`. A fenced entry stays active here with a dated `fenced` bullet until its final disposition. Active entries never carry `[X] Fixed`.
6. Keep accounting exact. A duplicated finding counts once; multiple root-cause tags do not create multiple defects; a fenced entry remains open unless its contract explicitly accepts the behavior.


## Entry template

```markdown
file.c:123: concise statement of the broken contract. Evidence and end-to-end trace. Reachability. Concrete consequence. logged YYYY-MM-DD. test: target, manual platform check, or none with reason. issue: #123.
- severity: critical | major | latent
- root-cause-tag
- optional-secondary-tag
```


## Severity

| Level | Meaning |
|---|---|
| Critical | Memory corruption, crash, GPU device loss, invalid-handle use, race, or uninitialized use under plausible execution. |
| Major | Wrong results, contract break, leak, permanent feature loss, or failed recovery under plausible use. |
| Latent | Correctness defect requiring rare refusal, exotic platform state, absurd input, or a currently unreachable seam. |

Severity describes consequence and reachability, not repair size.


## Remediation buckets

These seven buckets are the reusable campaign taxonomy. Assign one primary bucket per finding even when several root-cause tags apply.

| Bucket | Typical cause | Usual repair |
|---|---|---|
| Contract gates | A value crosses an API, config, bridge, parser, or third-party seam without proving the promised domain. | One validator or adopter at ingress, reused by every producer. |
| Quantity safety | Sizes, counts, offsets, durations, indices, shifts, or footprints wrap, truncate, exceed storage, or measure the wrong thing. | Checked arithmetic on the only path to allocation and indexing; bounds beside producers. |
| Fallibility and atomic commit | Failure logs and continues, reports unconditional success, consumes an unwritten out-param, spins without progress, or publishes a prefix. | Domain result, one unwind owner, inert outs, private staging, commit-last publication, bounded retry. |
| Ownership and deferred lifetime | An adopted payload, temporary object, allocation, or deferred pointer has no single owner on every failure and teardown edge. | Encode transfer in the API, trace caller teardown, drain adopted queues, unwind in reverse acquisition order. |
| Mirrored state and inventory drift | A sibling list, table, descriptor binding, platform twin, create/destroy pair, or recreation path omits one member. | One declarative inventory, sibling parity checks, generated tables, and layout static assertions. |
| State-machine and concurrency lifecycle | Mutable scratch is shared, recovery skips reset, teardown skips a transition, or ordering lacks a happens-before proof. | Context ownership or immutable initialization; explicit start, stop, loss, resume, cancellation, and teardown obligations. |
| Algorithm and contract one-offs | A local semantic error does not arise from a broader missing mechanism. | Repair locally and pin the public behavior with a focused module test. |


## Result-domain rule

Failure modes are values when callers must react differently.

- Use `bool` only for genuine binary facts or commands with exactly two policy-equivalent outcomes.
- Use a domain-local result enum when outcomes imply different control flow such as retry, backpressure, cancellation, rejection, degradation, unwind, or initialization failure.
- Use a result struct when success also returns a value or ownership package.
- Mark actionable results `[[nodiscard]]`.
- Switch exhaustively without `default`, so a new result code forces every policy site to be revisited.
- Keep results domain-specific; there is no engine-wide error enum.

`ANO_FATAL` is a log level, not control flow. Logging a refusal never substitutes for returning it.


## Root-cause tags

- checked-arithmetic: integer wrap or allocation sizing escapes its guard.
- no-abort: a failure that should stop does not, or a callee reports success after refusal.
- seam-validation: a value crossing a documented seam is trusted without a domain check.
- ownership-leak: an acquired resource is not discharged by its actual owner on failure or teardown.
- fixed-array-overflow: an unbounded counter or index writes past fixed storage.
- partial-publish: a batch publishes per-element state and then fails without retracting the prefix.
- copy-paste-error: a cloned sibling retains the wrong token, predicate, argument, or field.
- feature-list-drift: a property is present for every sibling but one.
- wrong-error-source: a guard consults the wrong sentinel, status, or error channel.
- feature-gated-check: correctness depends on an unrelated optional feature flag.
- shift-ub: a legal index can shift into or beyond the signed bit domain.
- recovery-desync: recovery restarts a subsystem without resetting invalidated state.
- odd-sibling-out: one sibling omits a guard, ordering, or cleanup shared by the others.
- lookahead-off-by-one: paired window and delay coverage differ by one at a consumption seam.
- unbounded-spin: a retry or wait lacks progress, cancellation, or termination policy.
- dangling-capture: deferred work stores a pointer beyond the caller's lifetime contract.
- size-mismatch: storage is provisioned by a different metric than the consumer uses.
- unguarded-delegation: a public contract is delegated to a third party that does not honor it.
- truncating-cast: a narrowing conversion silently changes a legal duration, size, or domain value.
- retry-exhaustion: exhaustion is indistinguishable from success or consumes an unwritten out-param.
- clock-not-reanchored: a clock source is not rebased after a transition that invalidates its anchor.
- noop-not-honored: a documented no-op input still changes output or state.
- shared-mutable-state: function-static or global scratch races or clobbers independent contexts.
- missed-repoint: recreation leaves a descriptor, reference, or dependent bound to the retired object.
- table-coverage-gap: generated or mirrored tables disagree on their covered domain.
- alignment-contract-gap: declared alignment is weaker than the documented CPU/GPU layout contract.
- partial-out-param: a failure arm writes only part of the ownership or value package.
- silent-drop: an unsatisfied request is neither honored nor refused, so the producer believes it succeeded.
- pending-design-decision: a verified defect is blocked on a contract choice; never the only root-cause tag.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Collections

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Log

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Math

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Strings

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Synth

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Text

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Threads

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs


## Leads

Unverified suspicions only. Do not include them in bug totals.
