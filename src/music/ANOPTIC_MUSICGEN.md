# Anoptic Musicgen

The music module composes. Not "plays a track", not "crossfades stems" — it decides,
one bar at a time, what notes exist, and it does so fast enough to run inside the audio
callback. A piece has no length, no loop point, and no existence before it is heard.

It is a port of a Python prototype (`docs/TECH_SPEC.md`, `docs/THEORY_SPEC.md`) and is
held bit-exact against it. That constraint shapes almost every decision below, so read
the determinism section before the rest.

Roughly 10.8k lines across 22 translation units. The public interface is
`include/anoptic_music.h`; everything in `src/music/` is implementation and may be
rearranged freely.

## Where the boundaries are

```
game ──► anoptic_music.h ──► [ music ] ──► anoptic_synth.h ──► [ synth ] ──► [ audio ]
         (what to play)                     (what it sounds like)             (the desk)
```

The music module depends on nothing but the platform layer. It knows no audio types, no
device, no thread. It emits `AnoNoteEvent`s and an `AnoMusicalParams` block per bar, and
that is the whole of its output.

The synth depends on music (it consumes the IR). Audio depends on neither. The dependency
arrows never reverse — that is why `anoptic_audio.h` contains no music type even though it
carries `ACMD_MUSIC_*` commands: those are scalars.

One consequence worth stating plainly, because getting it wrong cost real time: a patch
name in `AnoMusicalParams.instruments` is an `AnoPatchName` — a *timbre the composer
named*, not a voice id in some backend's registry. The synth decides which of its voices
plays it, through one explicit table (`PATCH_OF_MUSIC` in `src/synth/ano_synth.c`). The
composer never learns what a synth is.

## The determinism contract

The port is bit-exact with CPython 3.12.6. Same seed, same config, same control inputs →
byte-identical events. This is not aesthetics; three things depend on it:

- The Python prototype remains a usable oracle. Every acceptance run diffs against it.
- A save file can be a seed plus a control script instead of a serialized engine.
- A seek can be "rebuild the engine off-thread and adopt it", because a rebuild lands on
  exactly the state that was playing.

What it costs, and where it lives:

- `music_det.{h,c}` — BLAKE2b-8, CPython's MT19937 with its exact draw semantics
  (`random`, `randrange`'s rejection sampling, `choice`, `choices`), banker's rounding, and
  `ano_music_floordiv`. That last one matters: Python's `//` on floats is *not*
  `floor(a/b)`; it is `fmod`-based with a snap-to-integral, and they disagree at
  boundaries.
- `-ffp-contract=off` on every TU in this module (see `CMakeLists.txt`). Float op order is
  part of the contract; an FMA contraction is a different number.
- Every knob that multiplies into a draw or a cost is `double`, never `float`.

The design that makes parity tractable: **every draw is a fresh stream, tagged.** A draw is
seeded from `(seed, tag, bar)` — `"42:mod:melody:17"` — not pulled from one long sequence.
So parity is not a question of "did we draw the same number of times overall"; it reduces
to *condition gating* and *state-mutation order*. Get the branch right and the number is
right. That is why almost every ported vector passed on its first build.

The seam where the two implementations are compared is `ano_events_digest`: events
serialized in a fixed field order with `%.13a` doubles (which is exactly Python's
`float.hex()`) and hashed with BLAKE2b-8. Both codebases compute it; the test compares
the hash.

## The bar pipeline

`ano_engine_advance_bar` is the whole engine. It runs in this order, and the order is
load-bearing — a state mutation moved across a draw changes the music:

1. Phrase clock. Extension (a hot withhold stretches the pre-dominant), codetta, elision.
2. Dramaturg. The phrase's cadence rationing, decided once at phrase position 0.
3. Period commitment (antecedent/consequent) at even phrase boundaries.
4. Key wander, and the pending modulation (pivot → dominant → arrival).
5. Instrument swaps and mode — phrase-quantized; `urgent` demotes them to the next barline.
6. Parameters: affect through the mapping table, slewed, with the cadential ritardando
   shading the emitted tempo points. (Or pinned, on the static path.)
7. Dramaturg withholding: locked tiers, escalation.
8. Hypermetric weight, texture rotation, the countermelody gate.
9. Motif lifecycle, the phrase's apex plan, the one-bar chord lookahead.
10. The harmonic context for the bar, then the signature director (an authored motif can
    seize the bar).
11. Generators, in gate order: pad, bass, melody (then imitation, then countermelody), arp,
    perc.
12. Sort into canonical raw-IR order. This is the `raw_events` the oracle diffs.
13. Per-layer performance chains (the modifiers).
14. Final sort. The key flip, if a modulation arrived.

Steps 12 and 14 are two separate comparison points. `raw` is the composition; `final` is
the performance. Both are digested and both are pinned.

## The files

Foundation:

| file | what it holds |
| --- | --- |
| `music_det` | the determinism kernel (above) |
| `music_theory` | scales, chords, intervals, pitch-class arithmetic |
| `music_harmony` | roman numerals, pivot-chord search |
| `music_voicing` | voice-leading cost, the search that picks a voicing |
| `music_ir` | meter, the annotated event, the per-bar harmonic context, the parameter block, and the bridge to the synth |
| `music_control` | affect → parameters: the mapping table and its ~20 mappers |
| `music_gen` | euclidean rhythms, `rough_cell`, phrase positions, the tension arcs |

Form and intent — the part that makes it music rather than a chord loop:

| file | what it holds |
| --- | --- |
| `music_form` | the phrase clock (segments, elisions) and the period planner |
| `music_dramaturg` | the ledger: withhold tension, spend it, alternate the ground |
| `music_motif` | motifs as rhythm + contour + shape, their transforms, and their realization into whatever harmony they land in |
| `music_signatures` | the director: which authored motif to state, and when it is overdue |
| `music_imitation` | canon — the melody's own head, answered |

Generators, one per layer:

| file | layer |
| --- | --- |
| `music_pad` | block/connective/comping voicings, suspensions, appoggiaturas, cross-bar tie prep |
| `music_bass` | root/fifth/approach tiers, pedals, inversions |
| `music_melody` | the largest: contour placement, the outer-voice guard (no parallel fifths/octaves against the bass), apex planning, anacruses, cadence statements |
| `music_counter` | the countermelody, checked against both the melody and the bass |
| `music_arp` | pattern traversal with pinned skips |
| `music_perc` | euclidean kit patterns, fills, crashes |

The rest:

| file | what it holds |
| --- | --- |
| `music_modifiers` | the performance chain: humanize, swing, articulate, accent, perform, echo, strum, transpose |
| `music_conductor` | the engine — the pipeline above, plus the harmonic walk (`gen_chord`) |
| `music_verify` | the lint oracle (see below) |
| `music_host` | the public API: the opaque engine, the curated config, snapshot/restore |

## Unbounded runtime

A piece has no end, so no state may grow with the bar count.

The phrase- and bar-indexed caches are **direct-mapped rings tagged with the index that
owns each slot** (`music_form.h`: `ANO_PHRASE_WINDOW` 32, `ANO_BAR_WINDOW` 256). The index
stays *absolute* — it spells the RNG stream tag, so rebasing it would compose different
music — and only the storage wraps. A slot carries the index that owns it, and a read that
misses the tag is a miss, not stale data.

This replaced flat arrays with bounds guards, which did not crash: they silently stopped
firing. The form machinery quietly died around bar 510 and nothing noticed, because the
music kept being *plausible*. The long-run golden (1200 bars, per-bar digests from bar
1000, diffed against CPython) exists to make that failure loud.

The engine is **pointer-free by construction**. Its state is its bytes: `ano_music_snapshot`
is a `memcpy`, and two engines built from the same config and seed and advanced to the same
bar are byte-identical, padding included (which is why the config constructors return
`static const` objects — C leaves padding indeterminate under an initializer, and stack
garbage in the padding would make a snapshot meaningless).

## Hosting it on the audio thread

A bar costs ~150–500 µs to compose. A block at 512 frames / 48 kHz is 10.6 ms. So the
composer runs *inside the callback*, two bars ahead of the playhead — no producer thread,
no queue, no ahead-of-time generation. `ano_synth_attach_music` wires it, and the generator
tops itself up every block.

The control plane is `ACMD_MUSIC_*` over the audio bridge, forwarded by the mixer (which
interprets nothing — it owns no music) to the synth, which applies it on the thread that
owns the engine. Bars come back as `AEVT_MUSIC_BAR`, carrying the bar's *meaning* — key,
chord, cadence, motif landing — and they arrive when the bar **sounds**, not when it was
composed. A game that flinches at a cadence must flinch on the cadence.

Seek is: rebuild off-thread (~120 ms per 1000 bars, so never in the callback), hand the
bytes over as `ACMD_MUSIC_SEEK`, and the synth adopts them at the next barline. The
schedule is *rebased*, not renumbered: the engine keeps its own bar numbering (it spells the
RNG streams) and the difference is carried as a constant beat offset.

Measured on a device, three minutes, steered live: worst bar 480 µs of the 10,666 µs block
(4.5%), no late bars, no drops, no underruns.

## The two oracles

**Parity** — `ano_events_digest` against CPython. `tests/anotest_music.c` carries the
vectors: per-primitive unit vectors, generator walks, two whole-piece walks (minimal and
everything-on), a 1200-bar long run, and the §14.4 acceptance matrix (12 seeds × dramaturg
on/off × 5 affect trajectories = 120 configs × 32 bars, raw and final digests both).

**Theory** — `music_verify.{h,c}`, a port of the prototype's linter. It judges the output
against the rules the music is supposed to obey: grid and scale membership, pad voicing,
bass-root placement, melodic line over merged ties, doubling, counterpoint, tie coherence,
the drum map, cadence realization, and whether a planted obligation (a cadential 6/4, a
lament, a tonicization) was ever discharged. The acceptance matrix is green on all seven
rule families in both stages.

The linter is not just a gate — it is a tripwire. It found three real bugs the digests could
not, because the digests only prove *the same thing happened*, not that it was any good.

Two subtleties in it that are easy to get wrong:

- Violation *order* is not a contract (the prototype's own follows dict insertion). The
  `(rule, bar)` multiset is what is compared.
- A truncated render is not a defective one. Contexts at or past the lint `horizon` are
  lookahead: the lookup reaches into them so obligations can discharge, but no
  context-iterating rule judges them. Otherwise every render reports an obligation it
  simply had no room to keep.

## Traps

Things that cost time, so that they cost it once:

- Modifier-displaced onsets. A strum or a humanize can nudge a note *backwards* across a
  chord boundary by a few milliseconds. Harmonic rules must resolve the bar and in-bar
  offset from the grid slot the note was displaced *from* (`grid_pos`), not from where the
  jitter left it. Exact, because no modifier moves a note by half a grid step.
- The layers list is *ordered*, not a bitmask, on the generation side. The gate emits pad,
  bass, melody, perc, arp and the conductor iterates in that order — a bitmask loses draw
  order and therefore loses the music. It collapses to a bitmask only at the synth boundary,
  where nothing asks in what order the generators ran.
- Two id spaces for patches (see the top). The composer names timbres.
- `ano_mapping_table_default()` is oracle-bound: the parity golden is generated from it. A
  new palette is a new table (`ano_mapping_table_electronic` is one), not an edit to that
  one.

## Deferred

`foreshadow()` and multi-bar signatures (TECH_SPEC §16 — nothing here precludes them).
Trace strings on dramaturg decisions are elided. Cross-platform float identity is targeted
for the generation core (integer/branch logic over pinned float op order) but is not
promised for the DSP downstream of it.
