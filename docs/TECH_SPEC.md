# TECH_SPEC — The Anoptic Engine Music System

This is the technical specification for porting the musicgen prototype into the
Anoptic engine: data structures, APIs, scheduling, state, determinism, the
audio-library requirements, and the conformance strategy. It is the companion
to THEORY_SPEC.md, which defines the musical rules this system implements —
where this document says *what a field means musically*, THEORY_SPEC is the
authority. The prototype froze over a finished IR and rule set (REFINEMENT_PLAN
A1–D3 complete, PLANS.md M27); everything specified here as **contract** was
settled by building and listening, not by prediction.

**How to read it.** Statements come in three strengths:

- **Contract** — the port must reproduce this. Contracts are conformance-testable
  (§14) and changing one means re-validating against the acceptance suite.
- **Guidance** — porting advice: recommended layouts, decompositions,
  allocation strategies. Deviate freely if the contracts still hold.
- **Tuning** — a constant that ships as data (the mapping table, config
  defaults, console values). Tunings are quoted at their prototype values and
  are expected to keep evolving; they must live in data, not in code.

The overriding porting principle, from the plan: *"Everything else ports as
tuning, not architecture."* The architecture chapters (§2–§11, §14) are the
part that must survive translation intact.

---

## 1. Scope and provenance

The prototype (`musicgen/`, Python ≥ 3.12) has exactly one hard dependency —
`mido`, confined to MIDI file I/O. **Everything above MIDI output — IR, theory
kernel, generators, conductor, linter — is dependency-free**, which is why the
whole generation core ports to C directly. The synthesis stack (signalflow) is
not ported; it exists as the *requirements probe* for the engine's in-house C
audio library (§12), and SYNTHESIS.md records what it proved the hard way.

Eight results are exported (PLANS.md §13), and they structure this document:
the two-tier lever architecture and mapping table (§9), the pull-based
`advance_bar()` API with per-bar seeding (§5, §8), the theory linter as the
acceptance suite (§14), the IR schema (§4), the boundary-quantization rules
(§9.3), the identified integration seam (§9.1), the live-control protocol with
its live-vs-rebuild split (§10), and the dramaturg's tension-debt ledger
(state-modeled in §7, musically specified in THEORY_SPEC §8).

---

## 2. System overview

### 2.1 The pipeline (contract)

```
game / script / UI
   │  set_affect(v,e,t[,urgent])  ·  set_override(name,x)  ·  request_key / request_motif
   ▼
control plane          affect → MusicalParams: one pure mapping table;
                       slew, hysteresis, boundary quantization (state in the conductor)
   │  MusicalParams (per bar)
   ▼
conductor              clock, meter, phrase position, harmonic state, cadence
                       scheduling, dramaturg, one-bar chord lookahead
   │  HarmonicContext (per bar)
   ▼  pad · bass · melody (· imitation · counter) · arp · perc     [fixed order]
   │  NoteEvent IR (theory-annotated, grid-aligned)
   ▼
modifier chains        pure IR→IR performance shaping, per layer
   ▼
consumers              MIDI writer · text dump · decision trace · theory linter
                       · synth/audio scheduler
```

The core is **pull-based**: `advance_bar()` produces exactly one bar on demand
and is the piece intended to port into the engine proper. Drivers are thin —
offline loops `advance_bar()` and writes files; live does the same one bar
ahead of the playhead. *Same core, no divergence.*

### 2.2 Module layering (contract for the seams, guidance for the files)

```
L0  THEORY KERNEL   pure functions, no state, no randomness beyond caller rng:
                    pitch · scales · chords · harmony · voicing · counterpoint
                    · guides · modulation
L1  IR              Meter · NoteEvent · HarmonicContext · MusicalParams
                    · merge_ties           (depends only on chords/scales)
L2  GENERATORS      rhythm · structure · form · motif · melody · signatures
                    · imitation · pad · bass · counter · arp · perc · dramaturg
L3  ORCHESTRATION   the conductor: owns the Seeder, owns ALL state, calls the
                    theory walk and every generator, applies modifier chains
∥   PARALLEL        modifiers · verify (linter) · textdump · midi_io — read the
    CONSUMERS       IR, never called during generation
```

The only near-cycle (melody ↔ motif) is a Python artifact; in C the shared
`Motif` struct lives in a common header and the cycle vanishes.

### 2.3 Thread model (contract)

The music engine is **single-threaded by design**. There is no locking, no
async, no concurrency assumption anywhere in generation. All control calls
(`set_*`, `request_*`) and `advance_bar()` must be serialized by the caller —
in practice, a command queue drained at bar edges on the generation thread
(§10.1, §11.3). The audio side has its own, stricter rule: the audio thread
owns the DSP graph (§12.6). Generation is microseconds per bar; it never needs
its own thread pool.

---

## 3. Time, units, and numeric conventions

### 3.1 Units (contract)

- **Time**: float quarter-note beats from piece start. MIDI-natural:
  `ticks = beats × PPQ`.
- **The grid**: `GRID = 0.25` beats (a 16th). Every pre-modifier event's start
  and duration lie on the grid; only modifiers move events off it. The grid is
  float-exact in binary — this is why float time does not drift.
- **Pitch / velocity**: MIDI integers; pitch 0–127, velocity 1–127.
- **Tempo**: BPM floats, carried as absolute-beat-keyed tempo points.
- Wall time enters only at the playback boundary, through the tempo map
  (§11.1).

### 3.2 Meter derivations (contract)

`Meter{numerator, denominator}` is two ints; everything else is computed (and
worth precomputing per distinct meter in C):

| Derived | Formula | 4/4 | 6/8 |
|---|---|---|---|
| `bar_quarters` | `num·4/den` | 4.0 | 3.0 |
| `slots` | `round(bar_quarters/GRID)` | 16 | 12 |
| `is_compound` | `num ≥ 6 and num % 3 == 0` | no | yes |
| `pulses` (felt beats) | `num//3` if compound else `num` | 4 | 2 |
| `pulse_quarters` | `bar_quarters/pulses` | 1.0 | 1.5 |

Metric weights per slot: downbeat 4.0, mid-bar pulse 3.5 (even pulse counts
only), other pulses 3.0, 8ths 2.0, 16ths 1.0. Strong slots = weight ≥ 3.0.
`bar_of` floors; `slot_of` rounds.

### 3.3 Numeric conventions (contract — these bite)

- **Rounding is banker's** (round-half-to-even), as in Python 3, everywhere
  `round()` appears: slot computation, tempo emission (`round(x, 2)`),
  merged durations (`round(x, 10)`), articulation (`round(x, 3)`). C's
  `round()` is half-away-from-zero — implement banker's rounding explicitly.
- **Epsilons**: `1e-9` for "ends meet" (tie merging, `chord_at`, guard-pair
  expiry, sounding-note lookup); `0.01` BPM for tempo-point emission.
- **Float order matters**: weighted choice builds cumulative float sums;
  slew accumulates per beat; `effective_tension` multiplies then clamps and
  its result crosses discrete thresholds (a 1-ULP drift can flip a musical
  decision). Preserve operation order when bit-compatibility is the goal (§8.4).
- **Sorting is stable**, by `(start, pitch)`, applied to the bar's events
  before and after modifiers; equal keys keep emission order (layer order).
  Use a stable sort with exactly that comparator.

---

## 4. The IR

MIDI is the output, not the working representation — it cannot carry "this E5
is a passing tone over V7." The IR can, and all inspection and linting operate
on it. The IR schema is a candidate for the engine's internal music event
format; the annotations enable engine-side debugging, not just prototype-side.

### 4.1 NoteEvent (contract)

| Field | Type | Default | Validation | Notes |
|---|---|---|---|---|
| `start` | float | req. | ≥ 0 | absolute beats |
| `dur` | float | req. | &gt; 0 | musical duration (pre-articulation) |
| `pitch` | int | req. | 0–127 | |
| `velocity` | int | req. | 1–127 | |
| `layer` | enum | req. | one of the six | `pad bass melody counter arp perc` — canonical order, fixed |
| `tie` | enum | none | `∅ / out / in / both` | §4.2 |
| `degree` | int? | null | — | annotation: scale degree 1–7 |
| `chord` | str | "" | — | annotation: roman symbol in context |
| `role` | str | "" | — | annotation: the licensing vocabulary (THEORY_SPEC §18) |

**Guidance**: split the struct. The playable core is
`{start, dur, pitch, velocity, layer, tie}` — six fields, fixed size. The
annotations (`degree, chord, role`) are inspection/lint-only with zero acoustic
effect: carry them in an optional sidecar (parallel array or debug-build
field), intern `role`/`chord` as enums/string-table ids, and compile the
sidecar out of shipping builds while keeping it in dev builds — the greppable
answer (§13) is a doctrine, not a luxury.

### 4.2 Tie chains and `merge_ties` (contract)

A note crossing a barline is a chain of grid- and bar-legal halves flagged
`out → both… → in`; the chain **is** one musical note. `merge_ties` recovers
it, and its exact semantics are a byte-identity anchor:

- Chains are keyed `(layer, pitch)`. An `in`/`both` event continues an open
  chain iff a head exists and `|head.end − ev.start| < 1e-9`; then
  `head.dur = round(head.dur + ev.dur, 10)` and the event is consumed
  (`in` also closes the chain).
- An `out`/`both` event becomes a chain head: a **copy** with tie cleared,
  appended and registered.
- Everything else — including an orphan `in` — passes through **uncopied, in
  order**. Tie-free input passes through with identity preserved.
- Orphan `out` therefore legally dissolves into a plain note; orphan `in`
  passes through struck and the linter flags it.

Consumer obligations (all contract): the MIDI writer merges before writing;
melodic-line lint rules run on merged events; modifiers respect the chain
(never re-attack, jitter, gap, or echo an interior half — THEORY_SPEC §15);
live MIDI suppresses `note_on` for `in/both` and `note_off` for `out/both`;
the offline audio renderer merges into single sustained voices. (The
prototype's realtime synth path re-articulates tied halves — a documented
first-cut gap the engine should close: one voice per merged note.)

### 4.3 HarmonicContext (contract)

The per-bar packet every generator receives:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `bar` | int | req. | 0-based |
| `scale` | Scale | req. | tonic pc + mode |
| `chord` | Chord? | null | downbeat chord (symbolic) |
| `chord_sym` | str | "" | its symbol in context |
| `chord_pcs` | pc tuple | () | **bass-first** sounding pitch classes (inversion-rotated) |
| `next_chord` / `next_chord_sym` | Chord? / str | null / "" | the one-bar lookahead |
| `tension` | float | 0 | effective (arc-shaped) tension |
| `cadence_slot` | enum | ∅ | ∅ / pre-cadence / cadence |
| `cadence_policy` | enum | ∅ | ∅ / authentic / half / deceptive |
| `modulation` | str | "" | key-change annotation |
| `obligation` | str | "" | ∅ / `tonicize:N` / `cadential64` / `lament` |
| `phrase_pos` / `phrase_bars` | int | 0 / 8 | position within phrase |
| `chords` | timeline | () | **optional intra-bar timeline** `((beat_offset, Chord), …)`; empty = one chord per bar |
| `phrase_apex` | int | −1 | apex bar within phrase, −1 unplanned |
| `form` | enum | ∅ | ∅ / antecedent / consequent |

`chord_at(offset)` returns the last timeline entry with
`off ≤ offset + 1e-9`, else `chord`. Two orderings coexist by design and must
both survive the port: `chord_pcs` is **bass-first** (the bass and the lints
key off it); voicing construction wants **root-first** (`Chord.pitch_classes`)
for its doubling preferences. The decision record behind the timeline field is
D3 (THEORY_SPEC §5.5): one chord per bar stays primary; the timeline is
consumed by voice-led harmonic layers and the linter only.

Note: the context is mutated after construction in exactly one case — an
elision overlays `cadence_slot`/`cadence_policy` on the shared bar. Model it
as a value type with that one documented write.

### 4.4 MusicalParams (contract; defaults are tuning)

The Tier-2 parameter block, produced per bar by the control plane:

`tempo_bpm 100.0 · note_density 0.5 · roughness 0.0 · articulation 0.9 ·
velocity_center 80 · accent_depth 12 · register_center 72 ·
layers ("pad","bass") · harmonic_rhythm 1.0 · dissonance_budget 0.0 ·
cadence_policy "authentic" · texture "" · instruments ((pad,warm),(bass,round),
(melody,soft),(arp,pluck)) · filter_cutoff 2500.0 · reverb_send 0.20 ·
delay_send 0.10 · drive 0.15 · stereo_width 0.70`

Treated as an immutable value struct (copy-and-modify); the conductor derives
escalated/hypermetric/textured variants by copy. The DSP-tier fields
(cutoff…width) are the *first* smoothing tier's output (§12.5) — the audio
library receives them as retarget values, never as per-sample automation.

### 4.5 Symbolic harmony types (contract)

- `Scale{tonic pc 0–11, mode}` — immutable; intervals precomputable per mode.
- `Chord{degree 1–7, extensions ⊆ {7,9,sus2,sus4}, inversion, source_mode?,
  applied?}` — **symbolic**: pitch classes are computed against a context
  scale (`pitch_classes` root-first, `voiced_pcs` bass-first, `bass_pc`,
  `quality`, `symbol`). `source_mode` (borrowed) and `applied` (secondary
  dominant) are mutually exclusive. These integer/pc computations must be
  reproduced exactly — they are the harmony's identity.

---

## 5. The engine API

### 5.1 Surface (contract)

```
engine = MusicEngine(seed, config)          // config = EngineConfig tree (§6)

engine.set_affect(valence?, energy?, tension?, urgent=false)
engine.set_override(name, value)            // name ∈ OVERRIDABLE, else error
engine.clear_override(name)                 // absent name is a no-op
engine.request_key(tonic, urgent=false)     // pc 0–11 or note name
engine.request_motif(tag)                   // authored-signature request

result = engine.advance_bar()               // the one generation call
```

`OVERRIDABLE` = every MusicalParams field name plus `"mode"`. Control calls
never take effect immediately: they mutate intent, and the next
`advance_bar()` samples it at the proper boundary (§9.3). `request_key` with
the current tonic is a no-op; a later request replaces a pending one.
`request_motif` persists until honored; unknown tags are no-ops.

### 5.2 BarResult (contract)

| Field | Contents |
|---|---|
| `bar` | 0-based index of the bar just produced |
| `events` | **post-modifier** events — what plays; may be off-grid; sorted (start, pitch) |
| `raw_events` | **pre-modifier** IR — grid-legal; what the theory lints judge |
| `context` | the bar's HarmonicContext |
| `params` | the MusicalParams in force (post-escalation/hypermeter/texture) |
| `affect` | (valence, energy, tension) snapshot |
| `tempo_points` | `(absolute_beat, bpm)` changes emitted this bar (often empty) |
| `trace` | the decision log, one string per decision (§13.3) |

The `events` / `raw_events` split is load-bearing: players consume `events`;
the acceptance suite consumes `raw_events` (plus `events` for post-stage
bounds). Both must be exposed.

### 5.3 Lookahead skew (contract)

Chords are generated one bar ahead (an internal queue of depth 2), so
generators can see `next_chord` — the bass approaches it, the pad prepares
suspensions into it, pickups target it. Consequence for callers: a lever
change during bar N first influences **harmony at N+2** and everything else at
N+1. Phrase-level decisions (cadence rationing, period commitment, clock
surgery, split-6/4) are all taken at a phrase's first bar — at least two bars
before any slot they change — so the lookahead never sees two answers.
Phrase-scoped RNG keys (§8.2) are what keep those decisions stable across the
skew.

### 5.4 The bar procedure (contract for the order)

The per-bar decision order is load-bearing and specified musically in
THEORY_SPEC §16. Structurally: clock extension → dramaturg → codetta/elision →
wander → period commitment → instrument swap → mode pick → params (mapped or
static, plus rit) → directive application + hypermeter → texture → lifecycle →
apex → chord-queue top-up and pop (assert queue sync) → context assembly →
signature director → **pad, bass, melody (+imitation, +counter), arp, perc** →
stable sort → per-layer modifier chains → stable sort → modulation arrival →
`bar += 1`. The generator emission order exists because of same-bar data
dependencies (§5.5); do not parallelize layers within a bar.

### 5.5 Same-bar data dependencies (contract)

1. Melody reads the **realized bass events** (outer-voice guard) plus the
   previous bar's downbeat bass root.
2. Imitation reads the **realized melody events** (clash scoring).
3. Counter reads **both realized melody and bass** (species rules;
   complementarity mask; rests when the melody saturates).
4. Arp output is post-filtered against imitation events (same-pitch masking —
   one channel cannot voice both).
5. Pad reads the **next** chord's pcs (connective animation) and previews next
   bar's voicing + suspension with the same deterministic optimizer (tie
   preparation).
6. The upcoming chord is realized against **next bar's scale**, not this
   bar's — the distinction matters during modulation windows.
7. On a split-6/4 bar the conductor voices the two half-bar blocks inline and
   the pad generator is not called; voice-leading memory is updated directly.

### 5.6 Generator calling convention (guidance)

The common shape: `generate_X(ctx, meter, params, state…, rng) → (events,
new_state, trace)`. State is **injected and returned, never held** — the
conductor owns every instance. Deviations to preserve: arp returns no state
(per-phrase identity lives in caller-held caches; its rng is unused when the
skip mask is pinned); imitation is fully deterministic (no rng — a fixed retry
ladder); pad's state is the returned voicing; bass's is a bare root int;
perc's a bare fill flag; melody takes two auxiliary rng streams (`pickup`,
`syncopate`) beside its main one.

---

## 6. The configuration schema

The full `EngineConfig` tree, all values tuning unless noted. Every feature
flag is **contract-bound to the byte-identical-off covenant**: with a flag
off, output is byte-for-byte the pre-feature baseline. This is the permanent
regression anchor and must survive the port (it is what makes features
provably inert).

| Config | Fields (defaults) |
|---|---|
| **EngineConfig** | meter 4/4 · params (static path) · key_tonic 0 · mode null (valence-driven) · valence 0.3 / energy 0.5 / tension 0.45 · phrase_bars 8 · wander_phrases null · cadence_policies null · dramaturg null · motif_library () · motif_leniency 0.5 · cadence_rit 0.0 · phrase_groove false · form · texture · ties · clock · mapper null (static path) · chains default · harmony · voicing · bass · melody · counter · arp · perc |
| **FormConfig** | cadential_64 F · periods F · period_prob 0.65 · hypermeter F · bass_inversions F · split_64 F |
| **TextureConfig** | doubling F · animate F · imitation F · rotate F · counter F |
| **TieConfig** | anacrusis F · suspension F · syncopation F |
| **ClockConfig** | codetta F · extension F · elision F · codetta_payoff 0.45 · codetta_bars 2 · extension_tension 0.7 · elision_energy 0.75 |
| **DramaturgConfig** | enabled T · leniency 0.5 · accrue_above 0.55 · debt_gain 0.12 · escalate_phrases 2 · hold_tier "arp" · register_cap_max 6 · escalation_cap 4 · big_spend 0.7 · max_debt 96 · earned_dissonance T · motif_lifecycle T · lament_bass T |
| **MelodyConfig** | range_semitones 12 · bar_rest_max 0.30 · span 2–4 · plan_apex F · counterpoint F |
| **HarmonyConfig** | dominant_tension_bias 1.6 · tonic_calm_bias 1.2 · repeat_penalty 0.25 · borrow_prob_max 0.35 · phrase_open_tonic_boost 1.6 · tonic_suppress 0.05 |
| **VoicingConfig** | voices 4 · lo 52 / hi 79 · max_adjacent_gap 12 · center 64.0 · max_voice_move 7 |
| **BassConfig** | lo 28 / hi 50 · velocity_offset +8 · approach_beats 1.0 |
| **CounterConfig** | lo 55 / hi 79 · velocity_offset −10 · density_scale 0.6 |
| **ArpConfig** | base_octave 5 · span_octaves 2 · velocity_offset −16 |
| **PercConfig** | fill_base_prob 0.25 · fill_tension_weight 0.55 · ghost_velocity 52 · base velocities (kick 100, snare 96, chat 64, ohat 70, crash 106) |
| **MappingTable** | §9.4 — THE tunable artifact |

All sub-configs are immutable value structs read fresh each bar, which is what
makes them **hot-swappable**: replacing a config sub-object between bars is an
atomic, race-free feature toggle (§10.1). `chains = {}` disables all
modifiers; `dramaturg = null` and `mapper = null` select the byte-identical
reduced paths.

---

## 7. The state model

All sequential state lives in one struct (`ConductorState`) — the
*stateless-except-declared-state* property. Save/resume, replay, exact A/B,
and network sync follow from serializing this one struct plus (seed, config,
affect). Classification (contract):

### 7.1 The sequential core (must be threaded bar to bar; serialize for save)

- **Cursor & harmony**: `bar`, `prev_chord`, `chord_queue` (the lookahead
  buffer, `(bar, chord, trace)` entries), `key_tonic`, `pending_key`,
  `modulation` (the committed 3-bar window), `last_key_phrase`.
- **Voice memories**: `prev_voicing` (pad), `prev_bass_root`, `pad_tie` (D1),
  `melody` = {prev_pitch, prev_anchor, prev_outer (t, mel, bass), pending_tie},
  `counter` = {prev_pitch, guide_pc, vs_melody, vs_bass}.
- **Form**: `clock` (the PhraseClock segment list — commitments only ever
  append at the frontier), `elisions`, `cadence_tail`, `planner` (period
  roles + recorded antecedent openings), `inversion_run`, `lament_bars`,
  `splits`.
- **Identity & dramaturgy**: `motif_lifecycle`, `motif_director`
  (bars-since per tag), `requested_motif`, `ledger` (the dramaturg's debt
  state — pure function of seed + affect trajectory + bar), `last_fill`.
- **Mapper slew/hysteresis block**: `current_mode/tempo/velocity/articulation`,
  `active_layers`, `current_instruments`, `last_emitted_tempo`,
  `tempo_restore`.

### 7.2 Re-derivable caches (memoization, not authority)

Per-phrase dictionaries: `motifs`, `grooves`, `arp_skips`, `apexes`,
`phrase_textures`, `imitation_cells`, `split_phrases`, `phrase_policies`.
Each is a pure function of (seed, affect trajectory, bar); they exist so the
one-bar lookahead sees the same answer the sounding bar later gets. **Caveat**:
several read earlier cache entries (texture rotation reads the previous two
phrases), so "re-derivable" means *by replaying from bar 0*, not recomputable
in isolation. **Guidance**: phrase indices are dense and monotonic — ring
buffers keyed `phrase % W` (small W) replace hashmaps; or simply serialize
them with the core, they are tiny.

### 7.3 Transients

`pending_signature`, `pending_lifecycle` — recomputed each phrase boundary;
need not survive a save.

### 7.4 Seek and state transfer (contract)

There is no random access. Jumping to bar N = construct fresh from (seed,
config), replay affect/overrides history, call `advance_bar()` N times with
output discarded. Generation is microseconds per bar, so this is the intended
mechanism — **determinism substitutes for state transfer** (the playground's
seek works exactly this way). A save file can therefore be either the
serialized state struct *or* the (seed, config, control-event log) — the
latter is smaller and doubles as a replay.

---

## 8. The determinism contract

### 8.1 The doctrine (contract)

Bar N's material depends only on (master seed, declared musical state at N) —
never on how many random draws other subsystems or earlier bars consumed. Two
renders with the same seed and different lever curves stay note-identical
until the levers actually diverge. Mechanism: a **fresh RNG per (subsystem,
bar-or-phrase) key**, derived from the master seed by hashing.

Prototype mechanics, exactly: tag = `"{master}:{key1}:{key2}…"` (decimal ints,
strings verbatim, `:`-joined) → BLAKE2b, digest_size 8 → 8 bytes big-endian →
64-bit seed → CPython `random.Random` (MT19937). Two consequences the code
relies on: (a) every `stream(...)` call returns a **fresh** generator — asking
for the same key twice in one bar yields the same first draw (the clock key is
deliberately read this way by both the extension and elision checks); (b)
generators **draw to stay aligned** even when a value goes unused (Humanize
draws for tied notes it won't move; the arp advances through rests) so that a
skipped note cannot shift later draws.

### 8.2 The stream-key registry (contract)

Per-**bar** streams: `harmony · codetta · pad · bass · melody · pickup ·
syncopate · counter · arp · perc · (mod, layer)` — the last is the modifier
chains' three-component key. Per-**phrase** streams: `texture · wander · clock
· period · apex · arp-pattern · arp-groove · perc-pattern · motif`. One-shot:
`signature`. Phrase-scoped keys are what make groove pinning, apex planning,
and texture rotation stable across the bars of a phrase and across the
lookahead. Adding a feature means adding a key, never sharing one — this
registry is the compatibility surface between engine versions.

### 8.3 Draw-level compatibility (contract if bit-parity is the goal)

The RNG methods actually used, in order of porting difficulty: `random()`
(53-bit double from two 32-bit words), `randint/randrange` (rejection sampling
via getrandbits), `choice`, `choices(weights=…)` (cumulative float sums +
bisect — order-sensitive), `gauss` (CPython's cached-pair algorithm — used
**only** by the Humanize modifier). Plus CPython's `init_by_array` MT19937
seeding from the 64-bit integer.

### 8.4 Recommended conformance strategy (guidance)

Two-phase:

1. **Bit-compatible generation core.** Reproduce BLAKE2b-8 tagging, CPython
   MT19937 seeding, `random/randint/choice/choices`, banker's rounding, and
   the epsilons. Validate by **byte-diffing `raw_events`** against Python
   goldens across the acceptance matrix (§14.4). Note `raw_events` is
   pre-modifier: `gauss` is *not needed* for this level — the hard part of
   CPython parity is confined to the post-modifier surface, and modifiers can
   be diffed separately or validated by bounds-lints only.
2. **After parity is proven**, the engine may swap the PRNG (keep the
   tag→hash→fresh-stream architecture; that part is the contract) and
   re-baseline its own goldens. From then on the lint suite, the property
   tests (byte-identical-off, determinism double-render, monotone payoff),
   and the frozen goldens carry conformance.

Either way, the CI gates are: render twice → bit-identical; every toggle off →
bit-identical to baseline; and (for audio, §12.7) render twice on a
deliberately churned heap → bit-identical.

---

## 9. The control plane

### 9.1 The integration seam (contract)

The engine's game-facing contract is the affect API: three clamped floats
(valence −1…+1, energy 0…1, tension 0…1) plus the request/override calls.
**Game state → affect is deliberately out of scope** — it is a separate,
game-specific model (the plan cites MarioAI-style linear metric weighting).
The engine must not grow game-semantic inputs; everything arrives as affect,
overrides, and the two requests (key, motif) — plus, later, `foreshadow()`
(§16).

### 9.2 Affect and overrides (contract)

`set_affect` merges the supplied axes and clamps; it is legal at any wall-clock
moment and takes effect only when sampled at boundaries. `set_override(name,
value)` pins one Tier-2 parameter while the rest stay live; the mapper's
would-be value remains computable (the UI ghost). Override value coercion
(contract, for a typed protocol): `layers`/`instruments` are tuples;
`mode/cadence_policy/texture` strings; `velocity_center/accent_depth/
register_center` ints; everything else float.

### 9.3 Boundary quantization (contract)

| Class | Parameters | Effect boundary |
|---|---|---|
| per-beat, slewed | tempo (≤ 2 BPM/beat) | each beat |
| per-bar, slewed | velocity_center (≤ 10/bar), articulation (≤ 0.15/bar) | each bar |
| per-bar, instant | density, roughness, accent, register, layers (gated + hysteresis), harmonic_rhythm, dissonance_budget, all DSP-tier fields | each bar |
| per-phrase, urgent→barline | mode, instruments | phrase start |
| per-phrase, no urgent path | cadence_policy (sampled once per phrase) | phrase start |
| per-phrase, rotated | texture | phrase start |
| key change | request_key | pivot at bars−3 riding the cadence; urgent = earliest ungenerated bar, disarming overlapped cadence slots |

The `urgent` flag's exact lifecycle (contract): the instrument block reads it
before the mode block clears it; the mode block is skipped during a modulation
window and then does **not** clear the flag — a deferred urgent mode change
fires after the modulation arrives. First bar snaps (no slew) to the affect
active at first pull, not at construction.

### 9.4 The MappingTable (tuning; the schema is contract)

One immutable struct holds **every perceptual constant**; every mapping
function is a pure `(affect, table) → target`; slew/hysteresis *state* lives
in the conductor, only the *rules* live in the table. This separation is the
contract; the values ship as data. The schema (defaults in parentheses;
formulas in THEORY_SPEC §2.1):

- **tempo**: base (70), per-energy (80), per-valence (8), range (60–160),
  slew/beat (2)
- **register**: base (72), per-valence (4), per-tension (2)
- **density**: base (0.15), per-energy (0.75); **roughness**: base (0.10),
  per-energy (0.30), per-tension (0.20), cap (0.60)
- **articulation**: legato (1.05), energy drop (0.60), slew/bar (0.15)
- **dynamics**: velocity base (56), per-energy (44), slew/bar (10); accent
  base (4), per-energy (14)
- **layer gates**: pad −1.0 / bass 0.12 / melody 0.28 / perc 0.34 / arp 0.62,
  hysteresis 0.10
- **mode**: hysteresis 0.60
- **instrument tiers**: pad warm→bright@0.60, bass round→driven@0.62, melody
  soft→hard@0.55, arp pluck→glass@0.72; hysteresis 0.08
- **cadence**: authentic &lt; 0.35, half &lt; 0.65; **harmonic rhythm**: slow
  below energy 0.30 ∧ tension 0.50
- **DSP tier**: cutoff base 350 Hz + 4.2 octaves/energy + 0.5/positive-valence;
  reverb 0.10 + 0.30·tension + 0.18·stillness; delay 0.04 + 0.24·tension·energy;
  drive 0.05 + 0.45·energy²; width 0.55 + 0.25·brightness

### 9.5 Automation curves (contract for the format)

An affect trajectory is a sorted list of breakpoints `(bar, {valence, energy,
tension})` — all three axes required per point; linear interpolation between
brackets; clamped to the endpoints outside. Loopable by `bar % loop_bars`.
Evaluated on the generation thread keyed by the engine's own bar (survives
seek). This is the offline stand-in for a game feeding `set_affect` live, and
the replay-log format for saves.

---

## 10. The runtime control protocol

The playground is the working draft of the native runtime control API. Its
architecture — a UI as a pure control-and-visualization surface over a
persistent local player, all state in one server-side mirror, all control as
typed commands applied at bar edges — is the shape to keep.

### 10.1 The live-vs-rebuild split (contract)

**Hot-swap at the next bar edge, no interruption, sequential state preserved**
(commands queued from any thread, drained on the generation thread):

- affect (with urgent), overrides set/clear, key request;
- the entire MappingTable, swapped atomically (live heuristic A/B at one seed
  is the headline experiment this enables);
- the DramaturgConfig (knobs swap; **the ledger persists** — state lives in
  ConductorState, not config);
- every feature flag: the 21 "perform keys" (shaping + cadence_rit +
  phrase_groove + plan_apex + counterpoint + the five form flags + the five
  texture flags + the three tie flags + the three clock flags) rebuild their
  frozen config sub-objects in place — read per bar, so the swap is atomic
  and per-phrase caches survive;
- the automation curve.

**Rebuild required (structural)**: DSP-graph changes — console config, sample
loading. The audio graph is destroyed and rebuilt (brief gap); **the engine
and its sequential state are untouched**, and the last bar's params re-apply
to the fresh graph. Engine-side rebuilds happen only for seed change and seek,
both via deterministic reconstruction (§7.4).

### 10.2 Telemetry (contract for the payload, guidance for transport)

Per bar, one packet carrying the whole inspection surface: bar index; context
(key/mode name, chord and next-chord symbols, chord pcs, tension, cadence
slot/policy, modulation); the **params that actually played**; the **mapped
ghost targets** (pre-slew, pre-hysteresis — what the mapper would produce, so
a pinned override reads as a departure); the affect snapshot; tempo points;
the trace lines; post- and pre-modifier events; a rolling lint verdict; the
pinned-parameter list. Plus a low-rate meter frame (output level, CPU).

### 10.3 Schema introspection (guidance)

The control surface is **data-driven**: the engine exposes a schema —
overridable names with kind/range/step/boundary class, mapping-table fields
with defaults, feature flags, mode/policy enums, layer and patch tables — and
the UI renders from it. The steerable-parameter set is discovered, not
hardcoded; follow/pin (mapper-driven vs pinned) and the boundary badge (beat /
bar / phrase, the iMUSE-latency countdown) are UI expressions of §9.2/§9.3.

---

## 11. Scheduling and playback

### 11.1 The tempo map (contract)

`BeatClock`: a piecewise-constant, incrementally extended list of anchors
`(beat, wall_time, bpm)`. `add_tempo_point` appends (a point at the last
anchor's beat replaces its bpm; a point before it is an error — anchors are
monotonic). `time_at(beat)` interpolates within the containing segment.
Dependency-free; shared by every playback path. Cadence ritardandi are
realized purely as extra tempo points — there is no other tempo mechanism.

### 11.2 Live scheduling (contract)

One-bar lookahead: generate bar N+1 while N plays (lookahead ≈ 2.5 s, prime
≈ 0.3 s). Events map to wall time through the BeatClock and are dispatched
from a deadline heap against **absolute monotonic deadlines** — no cumulative
drift; jitter is bounded by sleep granularity (≤ 20 ms). Heap ordering uses a
**stable sequence counter** as tiebreaker — never object identity (§12.7).
Tie chains are MIDI-natural live: suppress the join's off/on; minimum note
20 ms. Program changes are sent only when a layer's patch actually changes.

### 11.3 Offline rendering (contract)

Build the **entire tempo map first**, then schedule — merged notes span bars,
so beat→time conversion must be total before any event is placed. Schedule
entries are `(frame, kind, payload)` with kind priorities params &lt; note &lt;
remove so retargets land before onsets at the same frame. Rendering steps the
graph in sub-blocks that stop at every event boundary and every voice's exact
end frame — **sample-accurate event timing**, not block-quantized. Offline
merges tie chains into single sustained voices (one envelope for one musical
note). Capture is synchronous, in-graph, into a preallocated buffer (§12
finding 2). Default 44.1 kHz, block 1024, 2.5 s tail; runs ~10× realtime with
the full DSP stack.

### 11.4 Realtime rendering (contract)

**One thread renders and mutates, interleaved between blocks** — the engine
drives the render loop itself and writes cooked blocks to the device (block
512 ≈ 11 ms at 48 kHz: the control cadence and onset granularity). Commands
apply at loop top / bar edges. This single-threaded contract is the crash-free
shape the prototype converged on after cross-thread graph mutation segfaulted
(§12 finding 9); the C engine may split threads **only** by making the audio
thread the sole graph owner with a block-boundary command queue — which is the
same contract with the roles renamed.

---

## 12. The audio library

The synthesis stack was built as the requirements probe for the engine's
in-house C audio library: *"Everything below was needed to make ~3 minutes of
lever-driven music sound intentional; nothing is speculative."* This section
is that inventory plus the nine findings, restated as requirements.

### 12.1 The findings, as requirements (contract)

1. **No implicit gain.** Dynamics processors have specified, bounded makeup
   behavior. (The library the prototype used auto-made-up 34× and pinned 27%
   of samples.)
2. **Synchronous in-graph capture** into preallocated buffers for headless
   rendering; never route offline capture through a realtime recorder.
3. **Separate deadline diagnostics from headless rendering** — expose
   per-block CPU stats; "deadline missed" is meaningless faster-than-realtime.
4. **Voices are preset parameter blocks resolved at allocation.** Same
   topology per voice class, different constants per tier; a sounding voice
   keeps its patch; no live-patching of topology, no per-voice branching in
   the render loop.
5. **Feedback at three granularities**: sample (filters, followers),
   short-loop (flanger/comb, sub-block), and block (FDN). Block-based
   feedback must reject loop delays shorter than one block *loudly*.
6. **Native detector primitives**: asymmetric one-pole peak follower
   (`env = max(|x|, a·env)`), gapless sliding-window max (doubling cascade,
   O(log n)), sample-accurate (not block-quantized) detector timing, and a
   linear ramp-to-target-in-T (a one-pole never converges inside a lookahead).
7. **Parameter ranges are safety contracts**: every buffer-position input
   declares clamp-or-wrap, enforced in the node, loud in debug builds. (A
   musical parameter segfaulted the process through an unguarded interpolated
   read.)
8. **Determinism gauntlet**: every stochastic primitive takes an explicit seed
   (no global entropy); all DSP state initialized (an uninitialized feedback
   phase read stale heap); never use heap-object identity as a scheduling
   tiebreaker (stable sequence counters); CI gate: *render twice on a
   deliberately churned heap, diff bit-exactly*.
9. **The audio thread owns the graph.** Structural changes — voice
   alloc/free, send rerouting — are enqueued and applied by that thread at
   block boundaries, never by mutating live nodes or references from
   elsewhere. This argues for a **pre-allocated voice pool where "allocate"
   is a state flip, not a graph edit**.

### 12.2 DSP primitive inventory (contract for capability, tuning for values)

Oscillators: band-limited saw/square/triangle/sine; seeded noise; 2D-morphing
wavetables (morph input clamped &lt; 1.0 — finding 7); sampler (rate repitch
2^(Δn/12), root-key, loop points, SR correction); granular engine over a live
history ring buffer (seeded stochastic grain clock); random impulse,
sample-and-hold. Envelopes: ASR/ADSR with curve shaping, retriggerable, usable
as modulation sources; one-shot-per-event lifecycle; linear ramps. Filters:
SVF (LP/HP/BP + resonance) as the workhorse; biquad shelf/peak; 3-band channel
EQ; DC blocker. Delays: single-tap with runtime-variable time (predelay,
chorus, ping-pong, limiter lookahead are all this one primitive), feedback
comb, allpass diffusers. Effects/dynamics: Householder 4-line FDN with in-loop
damping and per-line T60 gains; chorus (dual-rate modulated taps); tempo-synced
ping-pong; tanh saturator; feedback compressor with fixed makeup; lookahead
limiter (5 ms window-max detector, one-pole release, gain on a delayed copy;
per-channel today, a linked option flagged); hard clip; constant-power pan,
mid/side width, mono summing; TPDF dither (±1 LSB, seeded, only at final
16-bit quantization). Smoothing: one-pole on every audible parameter.

### 12.3 The voice model (contract)

A voice is a plain node chain with an **explicit lifecycle**: the scheduler
attaches it to its layer strip and detaches it at a known end time — no
garbage-collected voices. Durations arrive in seconds (the scheduler converts
beats through the tempo map). Amplitude: `(velocity/127)^1.5`. Filter
keytracking is baked at allocation (`(f/261.63)^kt`, clamped 0.5–2.5) — pitch
is known then, so tracking is free at render time. Per-layer shared control
nodes (e.g. the cutoff smoother) fan out to every sounding voice of the layer:
one retarget sweeps them all, no per-voice bookkeeping. Instrument tiers are
presets (finding 4) resolved at allocation, boundary-quantized upstream.

The prototype's patch set (subtractive pads with unison detune, sub-oscillator
bass with filter-envelope pluck, delayed-vibrato leads in three variants
including the countermelody's "mellow", 2-op FM arp plucks, a morphing
wavetable pad, a repitched sampler, a granular octave-up shimmer fed by the
pad's own history, synthesized drums keyed by GM pitch, and four environmental
texture voices) is **tuning** — a starting library, every constant of which
lives in the patch data.

### 12.4 The console topology (contract for the shape, tuning for values)

Per-layer strip (trim → 3-band EQ → layer extras) → dry sum; post-strip sends
into two buses — reverb (20 ms predelay → 2 diffusion allpasses → the FDN,
T60 2.2 s, damping 4.2 kHz, inharmonic lines 33.7/45.3/57.7/68.9 ms → high
shelf −4 dB @ 4.5 kHz) and tempo-synced dotted-8th ping-pong delay (feedback
0.42) — then master: tanh drive → glue compressor (0.30 / 2.5:1 / 12 ms /
180 ms) → fixed makeup 1.5 into a tanh knee → DC block → lookahead limiter
(ceiling 0.92) → clip guard ±0.95 → TPDF dither at export. Pad-only extras:
chorus and stereo width; sidechain ducking of pad+arp from the kick
(schedule-driven one-shot envelopes by default — each kick spawns one, flams
pump deeper — or a detector-follower option); a mod matrix (typed routes:
ratio into cutoff, additive-clipped into width, additive into shimmer) over
shared LFO/drift/affect sources; one-shot audio-rate cutoff sweeps spawned by
large upward retargets (≥ 1.6×), summing, immutable once born.

### 12.5 The two smoothing tiers (contract)

Deliberately two: the **mapper slews musically** (per bar/beat,
boundary-quantized — the conductor port provides this), and the **console
glides at audio rate** (one-pole, ~20–45 ms) so retargets never zipper. The C
audio library needs only the second tier. Do not merge them: the first tier is
musical semantics, the second is anti-zipper hygiene.

### 12.6 Threading (contract)

Finding 9, restated as the engine rule: the audio thread owns the graph;
everything structural is a queued command applied at block boundaries;
"allocate" from a pre-allocated pool is a state flip; no allocation, no locks,
no reference-count churn on the audio thread. The prototype's single-threaded
realtime player is the degenerate (and proven) case of this rule.

### 12.7 Audio determinism (contract)

Same gauntlet as §8.4's CI gates, extended to DSP state: seeded stochastic
nodes, initialized delay/feedback phases, stable scheduling tiebreakers, and
the churned-heap double-render bit-diff as a CI gate. The meter/follower is
excluded from offline graphs by default so metering can never perturb a
render.

---

## 13. Output and inspection

Inspectability is a design bias, not tooling: *"'Why did it play that?' must
always have a greppable answer."* Every render emits `.mid` + `.txt` (dump) +
`.trace.txt`, and optionally `.wav`. Engine builds must preserve the ability
to emit all three in development configurations.

### 13.1 The MIDI contract (contract)

SMF Type 1, **PPQ 480**. Track 0 = conductor: time signature, the tempo map,
and per-bar markers (`bar N: chord [cadence] [modulation]` — DAW-visible
analysis). One track per layer with events, in layer order. Channel/program
table: pad 0/89, bass 1/33, melody 2/11, arp 3/46, **counter 4/71**, perc
9/none (GM drums, no program change). Semantic patch names map to GM programs
per layer (warm/bright, round/driven, soft/hard, pluck/glass, mellow/bowed,
plus environmental analogs); instrument changes emit `program_change` at their
beat; a default program is emitted at tick 0 when none is scheduled. Tie
chains are merged before writing (one on/off per musical note). Same-tick
ordering: program_change &lt; note_off &lt; note_on — adjacent same-pitch notes
must never cancel. Zero-length notes after rounding get one tick. Round-trip
invariant: after merging, note count, channel, pitch, velocity exact; start
and duration within 2.5 ticks (absorbs sub-tick humanization); both sides
sorted by quantized tick so jitter cannot reorder the comparison.

### 13.2 The text dump (contract for content, guidance for layout)

Per bar: a header line (bar · scale · chord → next · tension · cadence slot
(policy) · modulation · tempo), an optional levers line (affect, tempo,
density/rough/articulation/velocity/register, DSP sends, active layers), an
instruments line when patches change, then one aligned line per note grouped
by layer: beat-in-bar, duration symbol, pitch name, degree (`^5`), role,
velocity. Ties render as `~A4` (continuation in) / `A4~` (ties out) / both.
A flat one-line-per-event mode serves value-level greps. The dump is emitted
alongside every render.

### 13.3 The decision trace (contract for coverage)

Every consequential decision logs one line with its inputs: the harmonic walk
(with its weights), cadence choices, mode switches with hysteresis state,
dramaturg WITHHOLD/SPEND lines (debt, rung, payoff, escalations), clock
surgery (extension/codetta/elision with reasons), texture commitments, apex
plans, period commitments and withholds, imitation entries (candidate chosen,
clashes tolerated), signature/landmark statements, per-layer generator notes,
fills/crashes, cadence rits. The trace is the human-facing dual of the
determinism contract: between them, every note is explainable and
reproducible.

---

## 14. The acceptance suite

### 14.1 The linter is the spec (contract)

The theory linter re-derives every generative rule from the realized IR — the
generator claims a role, the linter verifies it discharges. It operates purely
on IR (no MIDI, no audio), which makes it the **external conformance oracle
for the C engine**: the engine emits IR (its lean events + annotation sidecar,
serialized), a thin adapter rehydrates Python IR objects, and the whole family
runs:

```
violations = lint(raw, ctxs, stage="pre") + lint(final, ctxs, stage="post")
           + lint_outer(raw, ctxs) + lint_periods(raw, ctxs)
           + lint_groove(raw, ctxs, params_by_bar)
           + lint_texture(raw, ctxs, params_by_bar)
           + lint_imitation(raw, ctxs, imitation_cells)
assert violations == []
```

`stage="pre"` runs the grid check and the slot-based melodic/obligation
analysis on `raw_events` (melodic rules on merged ties); `stage="post"` runs
range/membership bounds on what actually plays. Violations are
`(rule, bar, message)`; every rule family is dormant on output that doesn't
exercise its feature, so partial ports lint cleanly on what they have. The
seven families and all thresholds are specified in THEORY_SPEC §18; the
species/counterpoint primitives are pure pitch-pair functions and should also
be ported natively for generation-time guarding (the Python copy remains the
oracle).

### 14.2 Test patterns to reproduce (contract)

- **Poisoned plant**: render clean IR, corrupt one event (wrong doubling
  interval, broken imitation contour, orphan `in`, undischarged obligation),
  assert the matching rule fires. Every obligation-style rule has one.
- **Byte-identity**: every feature flag off ⇒ output byte-identical to
  baseline; plus the *surgical* form — feature on changes only its own layers.
  (Enabled by per-key streams: a changed subsystem cannot reshuffle others.)
- **Determinism**: render twice, assert equality; for audio, the churned-heap
  variant.
- **Property tests**: payoff magnitude monotone in accrued debt (the
  dramaturg's automated A/B); round-trip cleanliness on tie-bearing IR.

### 14.3 The prototype's coverage (reference)

392 tests across 37 files; per-milestone plants, byte-identity, determinism,
round-trip, plus unit coverage of the theory kernel, mapping, and synth
console. Demos double as executable acceptance scenarios (axes, tension sweep,
seeds, journey, payoff ladder, texture debt…), each of which asserts
lint-clean and round-trip-clean via a common emit path.

### 14.4 The acceptance matrix (contract)

Full-stack sweeps: **all feature flags enabled at once**, swept over ≥ 4–5
affect points and releasing tension arcs × ≥ 12 seeds × dramaturg on/off
(120+ configurations at the freeze; 160 for the counterpoint wave), asserting
zero violations across all seven lint families both stages. Tension arcs must
*release* (withhold→spend) so the dramaturg's full lifecycle is exercised. The
C engine ships when this matrix is green against the Python oracle, and keeps
it green in CI thereafter.

---

## 15. Porting notes (Python → C decisions)

- **Immutability map**: value structs (configs, MappingTable, Meter, Chord,
  Scale, Motif, Directive, plans) vs mutated-in-place state (NoteEvent —
  merge_ties grows the head's dur; HarmonicContext — the elision overlay;
  ConductorState and its sub-states). `dataclasses.replace` ⇒
  copy-struct-then-mutate helpers; every copy is fresh, no aliasing.
- **Allocation**: the natural shape is a **per-bar arena** — all events,
  traces, and scratch of one `advance_bar()` come from a resettable arena;
  sequential state lives in fixed structs; phrase caches in small ring
  buffers. Nothing in generation needs the general heap per note.
- **Strings ⇒ enums**: layers, modes, policies, textures, tie values, segment
  kinds, contour shapes, patch names are closed sets. `role`/`chord`
  annotations may stay interned strings in dev builds and vanish in release.
  Careful: RNG stream keys stringify into the hash tag — key spellings are
  part of the determinism contract (§8.2).
- **Dict-keyed-by-phrase ⇒ ring buffers**; frozensets of slots ⇒ bitmasks
  (slots ≤ 24); tuple record types ⇒ small named structs; `(layer, pitch)`
  chain keys ⇒ composite key or 2D indexing.
- **Closures ⇒ context structs** (inversion scoring, guarded snapping, cadence
  cleanliness checks are all small function-with-environment patterns).
- **Asserts are design invariants**, not runtime guards: chord-queue sync
  (`queued_bar == bar`), clock frontier discipline, mapper presence. Enforce
  in debug, treat firing as a defect.
- **None sentinels ⇒ tagged optionals** (prev_chord, pending_key, modulation,
  lifecycle, director, apex…).
- **Kwargs-only APIs ⇒ options structs** (set_affect, request_key).
- **`cached_property` ⇒ precomputation** (scale intervals, meter tables).
- The **stable sort + emission order** requirement (§3.3) and the **draw-to-
  stay-aligned** discipline (§8.1) are the two easiest contracts to break
  silently — pin both with byte-diff tests early.

---

## 16. Forward compatibility

Two planned milestones must not be precluded by the port:

- **`foreshadow(tag, eta_bars, confidence)` (M16)** — the game plants setup
  when it *does* know something is coming: the tagged motif enters *pp*, a
  dominant pedal starts, texture thins to leave room to bloom. Architecturally
  it generalizes the one-bar lookahead to a **repairable phrase-level plan**:
  plan the arc optimistically, keep it cheap to repair, trace both the plan
  and every repair ("planned PAC at bar 41; invalidated by tension spike at
  39; converted to half cadence"). Hard cancellation policy: if the event
  never fires, the setup decays into something still intentional — deceptive
  resolution does most of the work. Determinism keys on (trajectory,
  foreshadow schedule). **Design consequence now**: leave room beside the
  chord queue for a multi-bar plan object with traced invalidation; do not
  bake "lookahead depth == 1" into data layouts.
- **Multi-bar signature sequences (M18)** — authored identities spanning a
  phrase, generalizing the director's selection/appropriateness/overdue
  machinery beyond one-bar cells. **Design consequence now**: the motif cell
  must not be assumed bar-sized in the IR of identities (the realization
  functions already take slot spans; keep the phrase-spanning door open).

---

## 17. The porting map: architecture vs tuning

**Architecture — port intact, conformance-tested:**
the IR (events, ties + merge semantics, context with the optional intra-bar
timeline, params, six layers); the pull-based `advance_bar()` with its
internal decision order and same-bar dependency graph; the one-bar lookahead
and first-bar commitment discipline; per-key fresh-stream seeding with the
stream registry; stateless-except-declared-state and the state classification;
byte-identical-off feature gating; the boundary-quantization classes and the
urgent lifecycle; the cadence-precedence chain and the obligation system (as
specified in THEORY_SPEC); the PhraseClock; the dramaturg ledger with monotone
payoff; the MIDI/dump/trace output contracts; the lint covenant with the
Python linter as oracle; the audio-thread-owns-graph rule, the voice-pool and
preset-at-allocation model, the two smoothing tiers, and the determinism
gauntlets.

**Tuning — ship as data, keep evolving:**
every MappingTable constant; every config default in §6; the harmonic walk's
weights and the dissonance tiers; the performance layer's amounts; the patch
library and console values; velocities, registers, probabilities throughout.
The prototype remains the tuning bench: constants can keep evolving there
after the port, because everything that would make retuning unsafe — the rule
set and its verification — is frozen above.
