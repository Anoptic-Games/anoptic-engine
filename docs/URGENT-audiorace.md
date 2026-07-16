# URGENT: suspected data race in the audio/music/synth suites

Opened 2026-07-16. The `tests` workflow fails on module-audio in the same two jobs on consecutive pushes. Evidence below is sparse because unauthenticated API access cannot pull job logs (admin only) and the check-run annotations say nothing beyond `Process completed with exit code 1`. This file records what is established, what is hypothesis, and the fastest paths to the missing evidence. Delete it or fold the findings into the audio module docs once the cause is named.

## Symptom

Two jobs fail, identically, on both of the latest module-audio pushes: `suites (macos-latest, tests-headless)` and `suites (ubuntu-latest, tests-tsan)`. The failing step in each is the nix build of the suite derivation (`.#tests-headless` / `.#tests-tsan`), which dies in checkPhase.

- run 6 on `ba25f21`, 2026-07-16 01:29 UTC: <https://github.com/Anoptic-Games/anoptic-engine/actions/runs/29464232257> (jobs 87513939987 macos, 87513940012 tsan)
- run 5 on `85bb516`, 2026-07-16 00:14 UTC: <https://github.com/Anoptic-Games/anoptic-engine/actions/runs/29460871889> (jobs 87503903828 macos, 87503903841 tsan)

## Established

- Deterministic with respect to the tree, not a one-off flake: the same two jobs failed the same way in two runs 75 minutes apart, and the only tree difference between them is `.github/workflows/cachix.yml`, which cannot affect test outcomes.
- Not a build or toolchain failure: the macOS job compiles and links all 189 targets before checkPhase, and the cachix workflow on the same SHA built and cached the full release engine (`.#play`) green on both platforms. "macOS doesn't build" is ruled out; the engine builds, a test fails.
- Confined to the tail of the suite run: on macOS, 21 suites pass and 8 are disabled benches out of 33; every earlier audio-family suite passes (`anoptic_audio`, `audiotone`, `audioscene`, `audiodsp`, `synth` at 20.0s, `musichost`, `synthlive` at 8.1s). The visible log ends after `Start 32: anoptic_synthscene`; the four unresolved suites are `anoptic_musicdrive` (#30), `anoptic_musicscene` (#31), `anoptic_synthscene` (#32), and #33 (`anotest_music` is built but its start line is never seen).
- Cross-platform: the identical signature on Linux under TSan means a macOS-only explanation (CoreAudio backend, dispatch semantics) requires two independent bugs; one shared-code cause is the parsimonious read.
- No main baseline exists: `tests.yml` has zero runs on main, so these suites have only ever run against branch audio/music code; nothing here says main is affected.
- The macOS job finished in 2m10s total, so the death is fast — an assert, crash, or short ctest timeout, not a multi-hour hang hitting the job timeout.

## Failure modes consistent with the evidence

Ranked by parsimony; none is confirmed until a log tail or local repro lands.

1. Data race in shared audio/music/synth code, detected by TSan on Linux and manifesting as a crash or corrupted-state assert in a scene suite on macOS. The suspect suites drive the full stack concurrently: music driver feeding the synth feeding the audio pull path. Candidate surfaces: the pull-thread handoff (`src/audio/audio_pull.h`, `audio_bridge.h`), parameter smoothing state (`src/audio/dsp/smooth.h`), music-to-synth IR handoff (`src/music/music_det.h`, `music_ir.h`), and synth voice state (`src/synth/synth_internal.h`).
2. Deadlock or livelock in one late suite, killed by a per-test ctest timeout. The passing `synth` (20s) and `synthlive` (8s) runs bound how long healthy suites take; a scene suite wedging on a lock ordering the earlier suites never exercise fits the log going quiet mid-run.
3. Plain logic assert in `musicdrive`/`musicscene`/`synthscene`/`music`, with the TSan failure being a separate, unrelated race. Possible, but two simultaneous bugs in the same family is the weaker read.
4. Inter-suite interference under parallel ctest: the Start lines interleave, so suites run concurrently and could contend on a shared resource (device mock, temp file, port). Noted for completeness; nothing observed points at it specifically.

## Next steps, in value order

1. Pull the failing step's log tail — everything after `Start 32: anoptic_synthscene` on the macOS job, and the TSan report (`WARNING: ThreadSanitizer: data race` plus stacks) on the Linux job. Needs repo-admin auth (`gh run view --log` or the Actions UI). This single artifact likely names the bug.
2. Reproduce locally: `nix build .#tests-tsan --no-link -L` on Linux/WSL (or the tsan-runner harness), `nix build .#tests-headless --no-link -L` on an arm64 mac.
3. Isolate: rerun only the unresolved suites (`ctest -R 'musicdrive|musicscene|synthscene|music$'`), then serially (`-j1`) to separate a real race from parallel-ctest interference, hypothesis 4.

---

# Confirmed: data races in the audio/synth transport (full report, same day)

Same day, next-steps item 2 was executed: a local WSL repro of the exact CI derivations confirmed and localized the races. Hypothesis 1 above is correct; everything below is evidence, not conjecture.

## Repro

- `nix build "github:Anoptic-Games/anoptic-engine/ba25f214aad6a0b7893d9adbb43dc355ceac9b08#tests-tsan" --no-link -L` (WSL Debian, nix 2.34.8): exit 1, 5 of 24 suites fail — `anoptic_audio`, `anoptic_audiotone`, `anoptic_audioscene`, `anoptic_musicscene`, `anoptic_synthscene`. Matches CI job `suites (ubuntu-latest, tests-tsan)` on runs 29460871889 (`85bb516`) and 29464232257 (`ba25f21`).
- Same SHA, `#tests-headless` on the same machine: 25 of 25 pass. The CI macOS headless job fails on the same suite family, so the failure is platform-conditional, not test-logic-conditional.
- CI macOS logs remain unread (admin-only via API); the macOS-side assert/crash line is the one still-missing artifact. The parsimonious read: x86-64's TSO ordering happens to survive races that ARM's weaker model does not, so Linux headless passes on luck while macOS headless fails for real.

## Race cluster 1: seqlock value copy — `src/audio/audio_bridge.h:151` and `:162`

TSan pairing, seen in every failing suite: mixer thread in `ano_audio_seq_store` (via `ano_audio_publish_telemetry`, called from `ano_audio_mixer_main` at `audio_mixer.c:666`) against the logic thread in `ano_audio_seq_load` (via `ano_audio_acquire_telemetry`, `ano_audio.c:259`).

The seqlock's even/odd version protocol is algorithmically sound — torn copies are detected and retried — but the guarded value bytes are copied with plain `uint8_t` loads and stores on both sides concurrently. Under the C11 memory model concurrent non-atomic access to the same bytes is a data race regardless of whether the result is later discarded: formally UB, and TSan flags every overlap. This is the highest-frequency report and by itself fails `anoptic_audio`, `audiotone`, and `audioscene`.

The implementation is a deliberate verbatim twin of `ano_seqpub_store`/`ano_seqpub_load` in `src/render_bridge/render_bridge.h:113-134` (private headers do not cross modules, so the code was copied). The twin has the identical formal race; `anotest_render_bridge` passes TSan only because its schedule never overlaps store and load on the same lane, not because the code is clean. Any fix must land in both twins.

## Race cluster 2: cross-thread `AnoSynth` reset — `src/synth/ano_synth.c`

TSan pairings from `anoptic_musicscene`: the logic thread in `ano_synth_transport_start` writing `dropped` (:795), `musicBarUsMax` (:796), `evtHead`/`evtTail` (:797), `cmdHead`/`cmdTail` (:798) against the mixer thread reading the same fields in the generator callbacks it invokes every block — `ano_synth_poll` (:770, from `audio_mixer.c:612`), `ano_synth_stats` (:781/:783, from `audio_mixer.c:665`), `ano_synth_commands` (:1201, from `audio_mixer.c:497`). All of these fields are plain `uint32_t` (`synth_internal.h:205-236`); only `startFrame` is `_Atomic`.

Unlike cluster 1 there is no protecting protocol: `transport_start` memsets the voice pool and zeroes both queue index pairs while the mixer may be mid-drain. On ARM nothing orders the resets against the mixer's reads — the mixer can observe `cmdHead` from the new epoch with `cmdTail` from the old one, so the `tail < head` gate and `% QUEUE` slot arithmetic index garbage and feed nonsense commands and events into the desk. That is a real corruption path, consistent with scene suites dying on macOS with no TSan involved.

The stated ownership contract (`synth_internal.h:7`: logic thread while idle, generator thread once started) is violated by the engine itself, not just the tests: `src/engine/main.c` calls `ano_synth_transport_start(g_synth, ...)` on the logic thread after `ano_audio_init` has already started the mixer thread with the generator callbacks registered — the same shape the failing tests exercise.

Two adjacent latent races in the same cluster, not yet TSan-flagged only because the runs die first: the producer-side overflow policy writes the consumer's index — `evtTail` is advanced by the producer on overflow (:537-538) while the consumer drains it (:771), and likewise `cmdTail` (:853-854 vs :1202). Two writers to one plain index breaks SPSC discipline in steady state, independent of transport resets.

## Why each CI job fails

- `suites (ubuntu-latest, tests-tsan)`: TSan aborts the five suites on the races above. Deterministic, reproduced locally, exit 1.
- `suites (macos-latest, tests-headless)`: no TSan; cluster 2 corrupts live queue state on weakly-ordered ARM in the suites that start transport while the mixer runs. Exact failure line unconfirmed pending CI logs.
- Local Linux `tests-headless` passing is TSO plus scheduling luck, not evidence of health.

## Fix direction

Planned separately (see the fix plan): cluster 1 — replace the seqlock byte-copy loops with relaxed atomic word copies over aligned `_Atomic` word storage, applied to both twins, keeping the existing promotion note to a shared collections header; cluster 2 — restore single-thread ownership of `AnoSynth` runtime state by routing transport start/stop through the existing lossless logic-to-mixer command ring, and make each queue index single-writer (overflow counts a drop instead of advancing the consumer's tail). Verification gate: local `#tests-tsan` 24/24 before any push; CI macOS headless green is the observable that closes the original report.

---

# Fix applied (2026-07-16)

Cluster 1: seqlock lanes are now `_Atomic uint64_t` word arrays; store/load copy `stride/8` words with relaxed atomics inside the unchanged version protocol (per-word `memcpy` marshals to/from the typed structs). Applied verbatim to both twins — `src/audio/audio_bridge.h` + `src/audio/ano_audio.c` and `src/render_bridge/render_bridge.h` + `src/render_bridge/ano_render_bridge.c` — with `_Static_assert`s pinning payload sizes to 8-byte multiples. Lane init is `atomic_init` loops, not memset. Promotion to a shared collections header remains a follow-up, per the notes in both files.

Cluster 2: the reset is deferred to the rendering thread. `ano_synth_transport_start` now only bumps `_Atomic transportEpoch` and release-stores `startFrame`; `synth_transport_sync`, called first by all five generator hooks (control/poll/stats/generator/commands), consumes the epoch exactly once and runs `synth_runtime_reset` mixer-side. All five hooks is load-bearing (musicdrive seeks via the control hook before block 0); epoch detection rather than startFrame-change is load-bearing (anotest_synth restarts with the identical worldFrame). `attach_music` no longer touches mixer-owned fields. Public API unchanged; contracts updated in `include/anoptic_synth.h` and `src/synth/synth_internal.h`. The doc's "two writers per queue index" concern dissolved under the verified topology: queue producers and consumers both run on the mixer thread; `transport_start` was the only cross-thread writer.

Deviation from the fix-direction sketch above: no `AnoAudioCommand` transport variant was added — the deferred-reset handshake achieves the same single-thread ownership with zero API surface change; the caller audit (all 8 call sites) confirmed no caller observes reset effects before the first hook runs.

Status: local gates passed from the fixed tree — `#tests-tsan` 24/24 (was 5 failures), `#tests-headless` 25/25. Awaiting CI on both jobs; macOS headless green closes this file.

---

# Investigation: adopting backup-resource-manager's lock-free collections

Question raised post-fix: how much of this remediation could be served by the standard lock-free loops and interconnects in `include/anoptic_collections.h` + `src/collections/` on the `backup-resource-manager` branch (`2146d3c`, 2026-07-15, unmerged) — the generic module both bridge headers' promotion notes already anticipate.

Inventory there: `anoticket` (wait-free FAA tickets), `anoring_spsc` (owner-cursor, no CAS), `anoring_mpsc`/`spmc`/`mpmc` (Vyukov sequence planes), `anoseqpub` (latest-wins 1W/NR epoch publication). C23 atomics throughout; planes allocated via `ano_mem_parent` from `anoptic_memory_pools.h`.

## Findings

- `anoseqpub` would reintroduce cluster 1 verbatim. Its publish is odd-marker, release fence, `memcpy(p->value, v, stride)`, even release; its read is acquire, `memcpy(out, p->value, stride)`, acquire fence, recheck — the same plain-byte concurrent copy, the same C11 data race, the same TSan signature this file documents at `audio_bridge.h:151`. The branch never noticed because `anotest_collections` stresses only the rings across threads: `test_seqpub_unit` publishes and reads on one thread, and the tests workflow has zero runs on that branch. The word-lane fix applied here is exactly what `anoseqpub` needs before it can host either bridge (its value plane is parent-allocated at init, so switching the plane to `_Atomic uint64_t` words is contained; stride needs rounding to 8 or a `%8` precondition).
- The rings remediate nothing because nothing was broken there. Bridge ring payload memcpys are ordinary SPSC slot handoffs — ownership transfers through the acquire/release cursors, so plain copies are already defined behavior — and `anoring_spsc` is field-for-field the same design as `AnoAudioRing`/`AnoSpscRing`, down to the `(tail - head) > mask` full test. Migration value is deduplication only: three private ring copies exist today (`audio_bridge.h`, `render_bridge.h`, `log_ring.h`) and the promotion both bridges promise would delete two.
- Cluster 2 is untouched by any primitive in the module: it was an ownership-protocol violation (logic thread resetting mixer-owned state), not a missing interconnect. `anoseqpub` could carry the start request as a latest-wins `{epoch, worldFrame}` mailbox — semantically the right shape — but it is strictly heavier than the two-atomic handshake now in place (heap plane + version word + retry loop versus one added `_Atomic`), and the exactly-once epoch consumption on the mixer side is needed either way. The synth's internal evt/cmd queues need no lock-free structure at all: both ends run on the mixer thread, so putting them in `anoring_*` would add atomic traffic to a single-threaded path.
- Porting cost is a real dependency, not a cherry-pick: collections needs `ano_mem_parent` (`anoptic_memory_pools.h`), absent on module-audio, and otherwise arrives with the resource-manager branch context (13k insertions since merge-base) unless shimmed onto `mi_heap`. New-file conflicts are nil; the allocator seam makes it a deliberate migration, not a bugfix vehicle.

## Verdict

None of today's remediation comes for free from backup-resource-manager: its seqpub shares cluster 1's bug, and no primitive addresses cluster 2. The dependency points the other way — today's word-lane seqlock belongs in `anoseqpub` when collections land, at which point both bridges (and their rings) migrate and the twins die, per their own promotion notes. Follow-ups for that merge: port the word-lane copy into `ano_seqpub_publish`/`ano_seqpub_read`, add a concurrent 1W/NR seqpub stress to `anotest_collections`, and gate the collections branch on `tests-tsan` (it currently has no TSan history).
