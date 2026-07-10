# THEORY_SPEC — The Music of Anoptic Musicgen

This document is the music-theory specification of the generator: every rule that
shapes the final composition, stated as music, with the conditions under which it
applies and the numbers that define it. It is the companion to PLANS.md (project
history and milestones) and the predecessor of the C-engine technical spec (data
structures, APIs, scheduling — deliberately absent here).

**How to read it.** Rules come in two strengths:

- **Law** — a rule the music must obey. Every law has (or must gain) a mirror in
  the theory linter, and a violation is a defect. Laws are written here with
  *must / never*.
- **Tuning** — a constant chosen by ear (a probability, a velocity offset, a
  crest depth). Tunings are quoted so the intent is legible, but they may be
  retuned without amending this document, provided the surrounding law still
  holds. Tunings are written with *currently / by default*.

**Divergence protocol.** Where the implementation is found to disagree with this
document, exactly one of two things is true: the intent evolved — in which case
this document is amended, with the reason recorded — or the code is wrong, in
which case it is fixed. Silent divergence is the one forbidden state. The
automated form of this protocol is the lint covenant (§19): every generative
rule is independently re-derived from the realized output by a linter that does
not trust the generator's claims.

---

## 1. The aesthetic stance

The generator produces **coherent tonal music indefinitely from rules** — no
machine learning, no corpus, no pre-composed material — steered in real time by
three semantic levers. The music is functional-tonal, common-practice-informed,
built for the way games consume music: a continuous stream of chained phrases
(no da capo forms), with every transition landing on a musical boundary rather
than a hard cut.

Correctness is necessary but not sufficient. The honest test, in the project's
own words, is that a render *"survives a 3-minute background-listen without
irritation."* The named enemy is output that is *"technically correct but
boring/noodly"* — the unconstrained random walk being *"the #1 'sounds
procedural' tell."* The target quality is **perceived intent**: the sense that
*"a mind composed this."* Concretely, that quality is pursued through a specific
inventory of signals, each of which is a chapter of this spec: the
antecedent–consequent period (*"arguably the single strongest 'a mind composed
this' signal in tonal music"*), imitation (*"the listener hears the voices
listening to each other"*), prepared cadences, one planned climax per phrase,
groove identity as a contract, a recurring motif that is only ever completed at
a dramatic payoff, and systematic (never random) performance deviation.

Two structural convictions run through everything:

1. **Setup is a ledger, not a plan.** A game cannot schedule payoffs, so the
   engine makes payoff *magnitude* a function of accumulated state: sustained
   tension quietly banks debt by withholding resolution, and the release spends
   the whole ledger at once. *"The game decides when; the engine decides how big
   and with what materials."* (§9)
2. **Every rule is checkable.** "Why did it play that?" must always have a
   greppable answer, and every rule the generator obeys is re-verified from the
   realized notes by an independent linter. The linter *is* the acceptance spec
   for any reimplementation. (§19)

---

## 2. The affect space

The entire control surface is three floats. Everything musical is a pure
function of these three plus a mapping table.

| Axis | Range | Meaning |
|---|---|---|
| **valence** | −1 … +1 | unpleasant/dark ↔ pleasant/bright |
| **energy** | 0 … 1 | arousal, activation, intensity |
| **tension** | 0 … 1 | unresolvedness, suspense — deliberately independent: *calm-but-tense exists* |

The design directions, verbatim: *"tempo is the strongest arousal lever; mode
brightness follows valence; density, roughness, articulation, dynamics, and
layer count follow energy; dissonance budget and cadence policy follow
tension."*

### 2.1 The parameter map (tunings)

| Musical parameter | Function of affect | Notes |
|---|---|---|
| tempo | 70 + 80·energy + 8·valence BPM, clamp 60–160 | energy dominates; valence tints |
| note density | 0.15 + 0.75·energy | drives every rhythm draw |
| roughness | 0.10 + 0.30·energy + 0.20·tension, **capped 0.60** | syncopation + ghosting (§14.5) |
| articulation | 1.05 − 0.60·energy | legato → staccato gate ratio |
| dynamic center | 56 + 44·energy | velocity center |
| accent depth | round(4 + 14·energy) | metric accent amount |
| register center | round(72 + 4·valence + 2·tension) | tension lifts the line slightly |
| mode brightness | target = 2.5·valence + 0.5 on the −2…+3 axis | §4.1 |
| dissonance budget | = tension, verbatim | chord-extension tiers (§5.4) |
| cadence policy | tension &lt; 0.35 authentic; &lt; 0.65 half; else deceptive | §6.2 |
| harmonic rhythm | 0.5 chords/bar iff energy &lt; 0.30 AND tension &lt; 0.50, else 1.0 | slow only when calm *and* slack |
| stereo width / sends / cutoff / drive | brightness widens; tension or calm wets the reverb; energy opens the filter and drive | mix-level tunings |

Layer gates by energy (with 0.10 release hysteresis): pad always on, bass
&gt; 0.12, melody &gt; 0.28, percussion &gt; 0.34, arp &gt; 0.62 — the arp is the
top tier, which is why the dramaturg can hold it hostage (§9.4). Instrument
timbres swap on separate energy tiers (hysteresis 0.08), phrase-quantized:
timbre re-orchestration never changes the notes.

### 2.2 Update semantics (law)

Affect may be set at any wall-clock moment; the music samples it only at
musical boundaries — the iMUSE discipline applied to parameters:

- **per beat** — tempo (slewed, max 2 BPM/beat);
- **per bar** — density, roughness, layers, dynamics (velocity slew 10/bar,
  articulation slew 0.15/bar);
- **per phrase** — mode and cadence policy; an `urgent` request demotes these
  to the next barline.

Any Tier-2 parameter (every musical parameter above, plus mode and cadence
policy, plus texture) can be **overridden** — pinned to a value while
everything else stays live. Overrides sit near the top of every precedence
chain in this document.

---

## 3. Musical time

### 3.1 The grid (law)

All pre-performance events lie on a **16th-note grid** (0.25 quarter-note
beats). Only the performance layer (§16) may move events off-grid. Raw events
never cross barlines; a musical note that does is written as a chain of tied,
bar-legal halves (§15).

### 3.2 Meter (law)

Meters are general over numerator/denominator; the felt-beat model is the
load-bearing idea. A compound meter (6/8, 9/8, 12/8) groups in threes: **the
felt pulse is the dotted unit**, so 6/8 has two pulses, not six. Metric weights
per slot: downbeat 4.0; mid-bar pulse 3.5 (only when the pulse count is even);
other felt pulses 3.0; 8ths 2.0; 16ths 1.0. **Strong slots** are those with
weight ≥ 3.0 — every chord-tone rule in this spec keys off that set. 6/8
therefore accents its two dotted quarters, never six 8ths.

### 3.3 Hypermeter (B3)

Bars group in fours the way beats group in bars. Bar weight within the 4-group
is (1.0, 0.4, 0.7, 0.4); in phrases of 8 or more bars the mid-phrase downbeat
is the second-strongest bar (0.85). Consequences: bar-level dynamics breathe
(currently ± a few velocity points around the profile), and the mid-phrase
downbeat may earn a fill-and-crash like a phrase boundary does (§14.4).

### 3.4 The phrase and its tension arc

The default phrase is 8 bars. Within a phrase, the last bar is the **cadence**
slot, the second-to-last the **pre-cadence**, the first the **open** slot, the
rest **free**. Local tension follows a fixed micro-arc multiplied onto the
tension lever, *"rising toward the pre-cadence bar and settling at the
cadence"*:

- 8-bar: ×(0.85, 0.90, 1.00, 1.05, 1.10, 1.20, **1.30**, 0.75)
- 4-bar: ×(0.90, 1.00, **1.20**, 0.75)
- any other length (elastic phrases): rise 0.85 → 1.30 by bars−2, settle 0.75
- codetta: flat ×0.7 throughout — *"a tonic prolongation breathes, it does not
  build."*

This *effective* tension is what the harmonic walk, extension tiers, and fill
probabilities consume. Cadence-policy selection and the 6/4 triggers read the
**raw** tension lever instead — policy is a phrase-level promise, not a bar
color.

---

## 4. Tonality: modes and keys

### 4.1 The mode axis (law)

Six church modes on a brightness axis, bright → dark: **lydian +3, ionian +2,
mixolydian +1, dorian 0, aeolian −1, phrygian −2. Locrian is deliberately
excluded** — the control layer may only select from these six. Valence maps
onto the axis (target = 2.5·valence + 0.5, nearest mode, ties break brighter),
giving the bands: lydian above +0.8, ionian to +0.4, mixolydian to 0.0, dorian
to −0.4, aeolian to −0.8, phrygian below. A hysteresis deadband (0.60 on the
brightness axis) keeps the sitting mode until valence genuinely commits.

Mode changes are **phrase-quantized** (barline-quantized under an urgent
request), frozen during a modulation window so the pivot analysis stays true,
and held through a codetta so a payoff's brightening outlasts it. A dramaturg
spend may lift the mode 1 step brighter on the same tonic (2 steps for a big
spend, payoff ≥ 0.7) — the parallel-mode brightening that makes the payoff
glow.

### 4.2 Modulation (M7, law)

A key change is a **transition event**, never a transposition. The canonical
form is the three-bar pivot modulation riding a phrase cadence:

1. **Pivot bar** (bars−3): a triad diatonic in both keys, still heard in the
   old key. Pivots are ranked pre-dominant-first in the *new* key
   (ii best, then IV, vi, I, iii; V and vii avoided), and penalized if they are
   dominant-function in the *old* key (they pull backward). Diminished triads
   never pivot — *"too unstable to anchor a key change."*
2. **Dominant bar** (the pre-cadence): V7 of the new key (V9 when effective
   tension ≥ 0.75). The analytical scale flips here.
3. **Arrival bar** (the cadence): the new tonic. The phrase's cadence is forced
   authentic — *"the modulation IS this phrase's cadence,"* and the ordinary
   cadence laws judge it like any other arrival.

When the keys share no usable triad, the pivot stage is skipped (direct
modulation through the new V7). An **urgent** modulation ignores phrase
geometry and disarms any cadence slot it overlaps — the emergency door, not
the norm.

**Wander** (optional): left alone for N phrases, the key drifts ±1 fifth,
leaning sharpwards when valence is bright (&gt; +0.15) and flatwards when dark
(&lt; −0.15), with a spring pulling home once the piece sits 2 or more fifths
away. The tonal center is a neighborhood, not a random walk.

---

## 5. Harmony

### 5.1 Vocabulary (law)

Chords are **symbolic scale degrees with quality emergent from the mode** —
never absolute pitch sets. A mode swap re-realizes the same progression with
new colors; that function-preserving property is the reason valence can
recolor a piece without rewriting it. The vocabulary: triads and stacked-third
extensions **7 and 9 only** (no 11/13), sus2/sus4 (mutually exclusive),
inversions up to the chord size, borrowed chords (same tonic, another mode's
coloring), and applied dominants. Functional assignment is fixed: degrees
1/3/6 are Tonic, 2/4 Pre-dominant, 5/7 Dominant; an applied chord is always
dominant-function.

### 5.2 The progression walk (law + tunings)

The next chord is chosen by a weighted walk over **functions** (T → PD → D),
realized as degrees in the current mode. The transition matrix (tuning):

| from \ to | T | PD | D |
|---|---|---|---|
| T | 0.15 | 0.55 | 0.30 |
| PD | 0.25 | 0.15 | 0.60 |
| D | **0.70** | 0.10 | 0.20 |

D → PD retrogression is kept rare (0.10) by design. Degree weights within a
function: T = I 1.0 / vi 0.35 / iii 0.10; PD = IV 1.0 / ii 0.60; D = V 1.0 /
vii° 0.15. Modifiers: dominant weight grows with tension (×(1+0.6·tension)),
tonic with calm; a phrase's opening bar boosts tonic ×1.6; repeating the
previous degree is penalized ×0.25; and while the dramaturg withholds, the
tonic degree inside a Tonic draw is crushed ×0.05 — *"circle the tonic via
vi/iii instead of landing on I."* Bar 0 of the piece is always I: establish
the tonic before anything else is allowed to happen.

### 5.3 Chromaticism

- **Modal mixture** (law): only degrees 4, 6, 7 may be borrowed; only the
  aeolian coloring is borrowed; only from modes brighter than aeolian
  (*"already dark; nothing to borrow"*); and only under negative valence
  (probability 0.35·|valence| for valence &lt; 0, zero otherwise). Mixture is a
  darkness color, not a spice rack.
- **Applied dominants** (law): V/x is a major-minor seventh a perfect fifth
  above its target, realized chromatically — its major third is the target's
  leading tone. Deployment is earned, not ambient: only the dramaturg's
  sustained withholding plants one (§9.6), tonicizing the deceptive target vi
  at the pre-cadence, and only when vi is a stable major/minor triad in the
  current mode. Every applied dominant carries the obligation that the next
  bar's chord *is* its target (§18).

### 5.4 The dissonance budget (tunings)

Chord-extension richness is the audible face of the tension lever, in four
tiers of effective tension: below 0.25 bare triads; to 0.5 the cadential V
gains a 7th (others occasionally a 9th); to 0.75 sevenths become common and
sus4 appears; at 0.75+ the dominant is always V7/9 and colors saturate. This
budget is *ambient* color — structural dissonance (suspensions, appoggiaturas,
pedals) is a separate, obligation-bearing system (§9.6, §18) and must read as
setup and payoff, never as spice.

### 5.5 Harmonic rhythm (law — decision record D3)

**One chord per bar is the primary shape of harmony.** Two refinements exist:

- *Slower*: under low energy and low tension (both, §2.1) free odd bars hold
  the previous chord — one chord per two bars. Cadence, pre-cadence, and open
  bars never hold.
- *Faster*: exactly one intra-bar acceleration exists — the **split cadential
  6/4** (§6.4), where the pre-cadence bar carries I6/4 on the downbeat and V at
  the mid-bar pulse. It is carried by an *optional per-bar chord timeline*
  consumed by the harmonic layers and the linter only; the downbeat chord
  remains the bar's identity for every melodic generator.

The decision record, verbatim: *"Generalizing to arbitrary 2/bar would mean
per-segment chord membership in every melodic generator, segmented
voice-leading memory, and windowed versions of every chord rule — a rewrite
priced against audible gain that the prototype shows is confined to the
cadence approach, which the timeline already covers."* Cadence-approach
acceleration elsewhere remains the three-bar cadential 6/4 formula.

---

## 6. Cadence

### 6.1 The three policies (law)

| Policy | Pre-cadence | Cadence target |
|---|---|---|
| authentic | Dominant function (V 90% / vii° 10%) | I |
| half | Pre-dominant function | V |
| deceptive | Dominant function | vi (may take aeolian coloring) |

There is no plagal *policy*; a plagal IV appears only as the codetta's optional
tonic-prolongation glance (§8.2). A cadential 6/4 or an active lament ground
**forces the pre-cadence to root-position V** — vii° may not substitute where
a 6/4 or a ground has promised the dominant.

### 6.2 The precedence chain (law)

When several systems want a phrase's cadence, precedence is, from the top:

**modulation &gt; override &gt; dramaturg &gt; period planner &gt; cycle / mapper**

The dramaturg outranking the planner is a musical statement, recorded verbatim:
*"while withholding, a consequent's promised PAC becomes another deception and
the period rolls forward — and a dramaturg spend landing on a consequent is the
maximal arrival (PAC + cadential 6/4 + the cadence-fused motif statement +
brightening at once)."* At the bottom, the mapper picks by raw tension
(authentic &lt; 0.35 ≤ half &lt; 0.65 ≤ deceptive), sampled once per phrase.

### 6.3 The cadential 6/4 (B1, law)

The prepared arrival: **I6/4 → V → I** across the free/pre-cadence/cadence
slots, so the authentic cadence *"reads as promised, not merely correct."*
Planted three bars from the phrase end when the phrase's policy is authentic
and no modulation is in flight. It always fires on a dramaturg spend and on a
period consequent (the promised arrivals); otherwise it requires raw tension
≥ 0.25 — *"a prepared cadence earns its weight."* The 6/4 carries an
obligation: it must discharge onto a root-position V within the bar (split
form) or the next bar (§18).

### 6.4 The split 6/4 (D3, law)

The compressed, driving form: the pre-cadence bar itself carries I6/4 on the
downbeat and V at the mid-bar pulse. Requires the plain 6/4's conditions
**plus energy ≥ 0.6**; when it fires it owns the phrase's cadence approach
(the three-bar form stands down). The classical genius of the idiom is honored
deliberately: the **bass needs nothing** (the dominant pitch pervades both
chords); the pad re-voices at the pulse (*"the harmonic motion IS the
event"* — ornament and animation stand down); melody, arp, and counter keep
the downbeat chord, whose tones over the V half *are* the 4–3/6–5 suspension
complex. The decision is cached per phrase so the one-bar chord lookahead can
never see two different answers.

### 6.5 The cadence approach (law)

Two approach rules bind every cadence the counterpoint guard can see:

- **Outer voices converge or diverge into the cadence, never chase**: the
  melody's approach direction runs contrary to the bass's root arrival, judged
  **downbeat-root to downbeat-root** across the barline (the last strong pair
  may sit on a fifth or approach tone, whose direction says nothing about the
  harmonic arrival). At least half of all cadence arrivals must be reached in
  contrary or oblique motion.
- **The cadence bar is an embellished approach, not chord-tone outlining**: an
  appoggiatura-style lean, a stepwise run (every hop a diatonic step, at most
  four), and a held target drawn from the policy's preferred degrees
  (authentic/deceptive: 1̂ then 3̂; half: 2̂ then 5̂). The melodic strong-beat
  chord-tone law explicitly exempts this bar.

### 6.6 The cadential breath (A1, tuning)

A micro-ritardando shades the cadence bar's last beats, a tempo at the next
downbeat: authentic breathes fully, half at half depth, **deceptive stays a
tempo — the surprise wants no warning.** A dramaturg spend deepens the breath
in proportion to its payoff: a bigger arrival gets a longer exhale.

---

## 7. Form

### 7.1 Periods: question and answer (B2, law)

An antecedent–consequent pair: two phrases opening identically, the first
ending on a half cadence, the second answering with an authentic one. Periods
are committed pairwise at even phrase boundaries (currently with probability
0.65), and only when nothing else owns the ground: no modulation pending, both
halves default-length, and the dramaturg not already rationing that phrase's
cadence.

The "same question" is structural, not approximate: the consequent reuses the
antecedent's opening **chord**; replays the antecedent's opening-bar melody
**verbatim** when harmony and scale still match (and the replay walks the
outer-voice frame cleanly — otherwise the aliased motif answers in rhythm
only); develops the antecedent's motif rather than drawing its own; and keeps
the antecedent's texture. A consequent is also one of the promised arrivals
that always earns the cadential 6/4. If the dramaturg withholds the promised
PAC, the period rolls forward as another deception — the answer is deferred,
not faked.

### 7.2 Elastic phrase lengths (D2, law)

Phrase geometry is a **scheduled clock** of segments (start, length, kind), not
arithmetic. With nothing scheduled it reproduces fixed 8-bar phrases exactly.
Three authored moves exist, all decided at a phrase's first bar — *"at least
two bars before any bar whose slot they change, safely outside the one-bar
chord lookahead"* — and commitments only ever touch the frontier (the first
unscheduled phrase):

- **Codetta** — a large dramaturg spend (payoff ≥ 0.45) appends a 2-bar tonic
  afterglow so *"payoffs breathe instead of re-entering the loop."* Inside it:
  harmony prolongs the tonic (an optional plagal IV glance, currently 50%);
  the melody echoes the cadence's tail an octave up, then rests; the kit thins
  to half density; tension sits flat (§3.4); and the entire phrase-boundary
  machinery — dramaturg, mode change, instrument swaps, texture rotation,
  apex, imitation, lifecycle — stands down. The codetta is the only segment
  outside the debt loop: it neither accrues nor spends.
- **Extension** — a hot withhold (tension ≥ 0.7, already withholding) may
  stretch the current phrase by 2 bars (currently probability 0.6). Decided
  *before* the dramaturg reads the phrase, so the ledger accrues the stretched
  length: *"the deceptive cadence genuinely arrives late, and the debt says
  so."* The tension micro-arc runs parametrically over the new length.
- **Elision** — a high-energy phrase (energy ≥ 0.75) promising an authentic
  cadence, with no payoff pending, may hand its cadence bar to the next phrase
  as its downbeat (currently probability 0.5): *"the arrival IS the
  departure."* The shared bar's chord is **forced to I** — that is what lets
  one bar honestly serve as both resolution and opening — and it carries the
  old phrase's cadence as an annotation over the new phrase's downbeat, which
  the linter verifies resolves as promised. Codetta and elision are mutually
  exclusive in a phrase; both yield to a pending key change.

Phrase identity in every per-phrase contract (texture claims, imitation
recognizability) is the rank of the phrase's start bar, so elastic lengths
never desynchronize the laws.

---

## 8. The dramaturg: tension debt and payoff

The mapper is memoryless — identical levers give identical bars — so authored
long-range shape cannot come from levers alone. The dramaturg sits between
affect and realization and keeps a **tension-debt ledger**.

### 8.1 The economy (law)

Decisions are taken once per phrase, at its first bar, so the chosen cadence is
in place before that cadence's chord is generated ahead:

- **Accrue** when tension ≥ 0.55: the phrase's cadence is rationed to
  deceptive, the ledger adds the phrase's (possibly extended) bar count plus
  one rolled-forward deception, and withholding begins or continues.
- **Spend** when tension falls below the release level *and* debt exists:
  the cadence is forced authentic and the whole ledger is spent at once.
  The release level is leniency-scaled (0.55·(0.4+0.6·leniency); at default
  leniency 0.5, tension must fall below ≈0.385) — lenient releases while
  tension is still fairly high (short buildups), strict only once tension is
  genuinely low.
- **Idle** in between: the cadence is handed back to the ordinary chain.

**Payoff magnitude is strictly monotone in accrued debt** — the system's
signature acceptance property, an automated A/B rather than a listening
session. Currently payoff = 1 − 1/(1 + 0.12·debt), saturating below 1, where
debt counts bars withheld plus *twice* the rolled-forward deceptions (a denied
promise weighs more than a bar of waiting).

### 8.2 What withholding withholds (law)

Simultaneity is what reads as payoff, so the withhold must be equally
multi-channel. While accruing, all at once:

1. the **authentic cadence** is refused (deceptive instead);
2. the harmonic walk **circles the tonic** (root-position I suppressed ×0.05);
3. the **top instrument tier (the arp) is locked out** of the layer gates;
4. the **melody's ambit contracts** (2 semitones per escalation rung, up to 6,
   never below a 6-semitone range) — and reopens fully on the spend: the
   audible bloom;
5. **texture clamps to homophonic** — the rich rungs are debt currency, and
   the spend releases the richest available state: the countermelody's
   entrance IS a payoff gesture;
6. the **escalation ladder** keeps the buildup breathing (§8.3);
7. **structural dissonance obligations** are planted (§8.4);
8. the **motif lifecycle** is gated: the faithful statement can only land on a
   spend (§11.3).

On the spend, everything releases together, plus the parallel-mode brightening
(§4.1), the deepened cadential breath (§6.6), the cadential 6/4 (§6.3), and —
for big spends — the codetta (§7.2).

### 8.3 Escalation: a coiled spring, not a plateau (tuning)

The named hazard: *"withholding can sound stuck, not tense."* Every 2
withholding phrases the buildup climbs a rung: loudness, density, and accent
push up proportionally (currently up to +14 velocity, +0.20 density, +4 accent
at full escalation), the register cap tightens, and — from rung 1 — the
buildup takes a **structural ground**, alternating per completed buildup:

- **Dominant pedal** (even buildups): the bass abandons its walk and holds
  scale degree 5 — maximal cadential pull — re-articulated on one fixed pitch.
  A pedal is a licensed non-chord bass that carries a termination obligation:
  the contiguous run must end at a cadence.
- **Lament ground** (odd buildups): a 4-bar ostinato walking the descending
  tetrachord 1̂–7̂–6̂–5̂ as i → v6 → iv6 → V, phrase-anchored so every
  withholding phrase restates it. The ground must discharge onto a
  root-position dominant.

### 8.4 Earned dissonance (M14, law)

Structural dissonance is obligation-bearing, distinct from the ambient
tension-tiered color, and deployed through the accrue→spend arc:

- **Suspensions** at the pre-cadence and cadence of every dramaturg-controlled
  phrase: prepared by a genuinely held voice, dissonant over the new chord,
  resolving down by step at the mid-bar pulse. While withholding they resolve
  into deceptive cadences (local relief, the debt stands); on the spend one
  resolves into the tonic — the payoff is itself a resolved dissonance.
- **Cadential appoggiatura** on every authentic arrival the dramaturg controls:
  an *unprepared* accented lean resolving down onto a chord tone — the payoff
  lean, preferring a whole-step over a semitone and the tonic as its target.
- **The applied dominant** V/vi at a sustained withhold's pre-cadence (§5.3).
- An infeasible request plants nothing — never a dangling obligation.

### 8.5 The landmark override

An authored signature motif of landmark importance (≥ 0.8) forces its phrase
authentic and cashes the ledger directly: **the leitmotif arrival IS the
payoff.** The per-bar withholding textures (pedal, tonic-circling) are left
running so they discharge into the now-authentic cadence.

---

## 9. Melody

### 9.1 The doctrine (law)

*"Pitch selection is constraint-first, never a free random walk: strong beats
snap to chord tones, weak beats move by scale steps, leaps beyond a fourth
recover by an opposite step, and the register window folds pitches back toward
center."* Two realization regimes exist and everything melodic is one or the
other:

- **constraint-first** — the harmony bends the material (the normal case);
- **faithful** — an identity (motif statement, imitation entry) is transposed
  *as a whole*, interval contour exact, licensed as a unit rather than
  note-by-note (§11, §12.6).

### 9.2 Line construction (law + tunings)

- **Strong slots take chord tones; weak slots take scale steps.** The melodic
  scale follows the *chord's* source mode over borrowed and applied chords, so
  chromatic harmony bends the line with it. The law: at least 80% of strong-slot
  melody notes must be chord tones (cadence bars and whole-cell statements
  exempt).
- **Register**: a two-octave default ambit around the register center, cap and
  floor breathing with affect; the dramaturg may contract it (§8.2). Escaping
  pitches step back to the nearest scale tone at the window edge rather than
  folding an octave — the fold manufactures an unexamined plunge.
- **Contours**: each bar realizes a motif cell shaped arch / descent / ascent /
  zigzag over a diatonic span of a 3rd–5th, anchored near the previous pitch.
- **Leaps**: anything beyond a perfect fourth is a leap and **must recover by
  one opposite step** (at least 90% of leaps, judged across the whole piece; a
  long rest breaks the line and cancels the obligation). Mid-bar plunges past
  a 6th are converted to steps outright. Bars never end mid-leap: a final leap
  contracts to a step, and under the counterpoint guard the contraction itself
  must keep the frame (else it steps the other way).
- **Rests are content**: a free bar may rest (probability shrinking with
  density, up to 0.30), but signature events, the payoff drive, the apex bar,
  and cadence bars never rest.

### 9.3 The apex: one climax per phrase (A4, law)

Each phrase plans **exactly one melodic peak**: placed late (bars−3 or −2,
where the micro-arc peaks), pitched in the ambit's upper third. The apex bar
may touch that pitch; **every other bar's ceiling is the apex minus one** —
single-peak contour by construction. The apex may be reached by leap (the
forced recovery supplies the gap-fill descent for free), never rests, and
stands down entirely on a payoff phrase, where the cadence-fused motif
statement owns the shape. The performance hairpin (§16) crests wherever the
apex actually is.

### 9.4 Non-chord tones (law)

The melody's licensed non-chord vocabulary: **passing** and **neighbor** tones
on weak slots (a neighbor must literally return), the **cadential
appoggiatura** (§6.5), and leap-recovery resolutions. Pad-layer suspensions and
appoggiaturas carry genuine resolution obligations (§8.4, §18); melodic ones
are governed by the melody's own strong-beat and leap laws instead. Everything
else out-of-scale must be licensed by role: borrowed tones, bass approach
tones, whole-cell motif/imitation statements, echoes.

### 9.5 Anacrusis and syncopation (D1, tunings on a law)

Two gestures reclaim what the barline invariant once forbade:

- **The pickup**: on a cadence bar, a 1–3-note stepwise run of 8ths may lead
  into the next phrase, ending **on the next chord's third** — the sweet
  imperfect consonance; landing on the root would hang an octave over the
  arriving bass. The held cadence target keeps at least a quarter note; the
  last pickup ties across the barline as a classic anticipation, and the next
  bar *hosts* that pitch as its pinned downbeat, so the entry is oblique by
  construction. Pickups sound from under the line's dynamic and are spared the
  cadential luftpause — *"they ARE the breath."*
- **Cross-bar syncopation**: a rough bar (roughness &gt; 0.35) whose last note
  reaches the barline may push it through — the downbeat attack disappears.

A hosted tie is pinned *inside* placement, so contour, leap bookkeeping, and
the outer-voice frame all build from the note that actually sounds. A tie into
a bar that cannot host it dissolves legally (§15).

---

## 10. Motif and identity

### 10.1 Three kinds of material

1. **The disposable phrase motif** — a fresh cell per phrase (rhythm from the
   shared vocabulary, contour from the four shapes) developed in **sentence
   form**: statement (bar 1), sequence (bars 2 and 5), restatement (bar 4),
   ornament (bar 7), and inversion/displacement/truncation elsewhere.
2. **The persistent signature** (M15) — one motto per piece, built once from
   the opening affect and kept.
3. **Authored signatures** (M17) — a curated leitmotif library the game can
   request, placed only at musically sound moments.

A signature is woven in as **one audible event per phrase** — an event within
the phrase, not a substitute for its material.

### 10.2 Markedness: a motto must be distinctive (tuning)

A signature is drawn from the "motto zone" (moderate density, some rhythmic
bite), scored 0–5 for distinctiveness — length 4–7, two duration values, a
leap, a step, a direction turn — best of several attempts, then repaired until
it has both a leap and a turn. A motto too plain to recognize cannot do its
dramatic job.

### 10.3 The lifecycle (law)

**introduced → developed → completed.** The introduction is a fragmentary
glimpse — the cell's head only, its tail nudged to a hanging tendency tone
(2̂ or 7̂) — *"so the later completed statement reads as an arrival."*
Development recurs in disguise (constraint-first realization, harmony bending
the shape). The **completed statement — the whole cell, faithful, interval
contour exact — is licensed only as a dramaturg payoff**, and only after at
least two disguised statements have made the shape familiar. On the payoff
phrase the melody *drives*: every bar develops the signature itself, never
rests, and the cadence bar fuses signature and resolution — the cell transposed
so its final note lands on the cadence target, *"a flourish that resolves,
instead of a generic two-note approach after a phrase of noodling."* After
landing, the lifecycle drops back to developed: the arrival is a one-phrase
event.

**Recognizability** — the fraction of the cell's successive intervals that
survive realization — is the lifecycle's acceptance number: a faithful path
scores 1.0 by construction, and the linter holds faithful statements and
imitation entries to ≥ 0.9.

### 10.4 The director (M17, law + tunings)

Authored signatures launch on a pressure-vs-fit judgment: pressure grows as a
motif goes overdue (scaled by its importance), fit measures how cleanly the
best admissible transform (identity, inversion, displacement, truncation) drops
onto the coming harmony; launch requires pressure·fit above a threshold the
leniency knob lowers (0.55 strict → 0.15 lenient), with an absolute fit floor
(0.34). A game **request** wins outright once it meets the fit floor. Landmarks
(importance ≥ 0.8) take the completed staging and cash the ledger (§8.5).
Leitmotifs recur regardless of tension — identity is not hostage to the
dramaturgy.

---

## 11. Counterpoint and the voices

### 11.1 The interval law (law)

Intervals are pitch-class intervals mod 12 — compounds fold onto simple forms,
as the rules intend. **Perfect** = unison/octave and fifth. **Consonant against
the bass** = perfects plus 3rds and 6ths; the fourth counts as a dissonance
above the bass, as in species practice. Four motions: parallel, similar,
contrary, oblique (a held voice is oblique).

Two prohibitions bind every guarded voice pair:

- **No consecutive perfects of the same class with both voices moving** — the
  parallel fifths/octaves ban, including the contrary ("antiparallel") form.
  A repeated verticality or an oblique hold is legal.
- **No direct (hidden) perfect**: similar motion into a perfect with the upper
  voice leaping (more than a whole step). A stepwise upper voice is exempt —
  the classical horn-fifths allowance. Enforced into downbeats.

A pair older than one bar expires — a bar of silence breaks the frame, in the
generator and the linter alike.

### 11.2 The outer-voice frame (A3, law)

*"The soprano–bass frame carries tonal music."* The (melody, bass) pair at
every strong-slot melody onset walks the interval law; strong chord-tone picks
are steered away from consecutive perfects (always) and direct perfects (into
downbeats) against the realized bass. The guard **prefers, never fails**: when
every candidate is forbidden it takes the nearest and the linter has the final
word. Whole-cell statements stay unguarded — their identity is licensed as a
unit — but their sounding pairs still feed the frame. Cadences close the frame
in contrary motion (§6.5).

### 11.3 The pad: inner voices (law + tunings)

- **Voicing**: four voices in a fixed alto window (E3–G5), strictly ascending,
  no adjacent gap over an octave, no unison doubling. Doubling preference:
  root first, then fifth, **never the third**; when dropping, the fifth first
  (the bass already owns the root). Between chords the winner minimizes total
  semitone motion, with the top voice granted a free whole step and **no
  single voice ever moving more than a fifth**. Voicing is a pure function of
  (chord, previous voicing) — no randomness.
- **The block is always the voice-leading target.** Whatever animates or
  ornaments a bar, voice-leading memory and suspension preparation are
  computed from the underlying block: figuration never corrupts the harmony's
  spine.
- **Suspension mechanics** (§8.4): a genuinely held pitch from the previous
  voicing, now a diatonic non-chord tone one step above a voiced chord tone,
  resolving down onto it at the mid-bar pulse — the classic
  preparation→dissonance→resolution, preferring the highest voice and the
  semitone step. With ties enabled, the preparation bar *ties* the preparing
  voice across the barline instead of re-striking it, previewed
  deterministically one bar ahead; a mispredicted preparation dissolves into a
  legal orphan tie.
- **Animation** (C2): on ornament-free bars, low density (&lt; 0.40) walks one
  voice through a diatonic passing tone toward its next-bar pitch on the last
  pulse (preferring a voice moving by a 3rd — exactly one tone lies between);
  moderate density (&lt; 0.62) breaks the voicing into an Alberti-adjacent
  figure on the pulse grid (low, mid-high, mid-low, top); above that, block —
  the other layers carry the motion. Animation stands down in the suspension
  zone (the last three bars of a dramaturg-controlled phrase): a suspension
  must be *prepared* by a sounding held voice, which a figurated bar cannot
  guarantee.
- **Thinning**: the monophonic texture state strips the pad to a root+fifth
  dyad — the leanest state, free of thirds entirely.

### 11.4 The bass (law + tunings)

The bass sounds the chord's actual bass pitch class — inversions come for
free — in a low window (E1–D3), each root chosen nearest the previous bar's
root so the line moves smoothly. Density tiers: a single sustained root; root
plus an approach tone into the next chord (diatonic below/above or the
chromatic tone below, the latter licensed as an approach); root/fifth split on
a felt pulse plus approach — the fifth reinforces the meter, never an offbeat.

**Inversions are a harmonic decision, not a bass habit** (B4): at free bars, a
first inversion is preferred exactly when its bass pitch class *steps* from the
previous bass (steps beat statics beat leaps; root position is the resting
default; never at phrase anchors, cadences, modulations, applied dominants, sus
voicings, or three bars running). The bass is a voice, not a root-reporter.
Pedal and lament behaviors are the dramaturg's (§8.3).

### 11.5 The countermelody (C5, law)

A second real line in the tenor gap (G3–G5), constrained *by* the melody,
whose realized bar exists first. The constraint stack, in priority order:

1. **Rhythmic complementarity is the contract**: the counter's rhythm is
   masked against the melody's onsets — move where it holds, hold where it
   moves. A saturated melody leaves no holes: the counter **rests the bar**
   rather than shadow an onset. (Law: at most 40% of its off-downbeat onsets
   may coincide with melody onsets.)
2. **Strong beats are chord members, consonant with the sounding melody** —
   3rds and 6ths preferred, perfect consonances rationed to what the walk
   cannot avoid (law: ≥ 70% imperfect-or-consonant against the melody).
3. **The interval law against melody AND bass** — no consecutive or direct
   perfects in either pair, same one-bar expiry as the outer frame.
4. **Never above the sounding melody** (unison allowed when the melody dives;
   the counter yields when even that leaves no room).
5. **Where nothing is both consonant and motion-clean, the counter rests —
   the species answer.**

Weak beats move by **one diatonic step toward the guide tone** — gravity and
leap-recovery come free; this line never leaps there. It sits under the
melody's dynamic and moves at a fraction of its density.

### 11.6 Guide tones (law)

The skeleton the counter's strong beats reach for: the 3rds and 7ths of
successive chords (the 5th standing in for a missing 7th; sus chords
contribute their replacement color) threaded into a minimal-motion line —
nearest pitch class by folded distance, ties low, the first chord taking its
3rd, the strongest quality-carrier. The thread advances every bar regardless
of what sounds, because adjacent functional chords share or step between their
guide tones almost everywhere.

### 11.7 Doubling (C1, law)

The cheapest polyphony in existence, heard as richness rather than a second
voice: a companion a **diatonic 3rd below** the melody, switching to a 6th
where the 3rd is not a chord tone on a strong slot, computed by interval
arithmetic (the surface may be chromatic). A note whose double fits neither
interval legally goes undoubled — the cadential lean stays solo. The double
lives inside the melody layer, under its dynamic, and is exempt from
line-level laws (it is the surface's shadow, not a second surface) while being
held to its own whitelist: below the surface, a 3rd or 6th, chord-member on
strong slots, scale-member on weak.

### 11.8 Imitation (C3, law)

One entry per phrase, the bar after the statement: a second voice restates the
motif's **head** (its first half) faithfully, in the arp's register when the
arp plays — or the pad's top voice while the dramaturg holds the arp hostage:
*"the echo survives the withholding."* Placement walks a fixed retry ladder —
on the bar; a half-bar late; up or down a diatonic 3rd; combinations — taking
the first candidate with **zero clashes** against the sounding melody (clash =
2nds, 7ths, tritone at entry onsets) and otherwise the least-clashing:
*"an imitative texture tolerates a passing 2nd sooner than it tolerates
silence."* An entry therefore always lands, and must reproduce its source cell
at recognizability ≥ 0.9.

---

## 12. Texture

### 12.1 The ladder (C4, law)

Texture is a first-class musical parameter with five states, lean → rich:

**monophonic → homophonic → doubled → imitative → counter**

Each state means what sounds: monophonic thins the pad to a dyad and disables
figuration; homophonic is the plain block texture; doubled adds the parallel
3rds/6ths; imitative licenses the phrase's echo entry; counter adds the second
line. A phrase's texture claim is a contract the linter verifies against what
actually sounds — including the *negative* claims (a homophonic phrase must
contain **no** polyphony).

### 12.2 Choosing a texture (law + tunings)

Richness follows energy (tinted a step richer when valence is bright); the
available pool grows with the enabled features, with the lean pair always
present. Committed once per phrase, precedence from the top:

1. an explicit override pins it;
2. a codetta keeps the payoff's texture — the afterglow does not rotate;
3. a **dramaturg spend takes the richest available state** — texture as debt
   currency, the countermelody's entrance as payoff gesture;
4. **withholding clamps to homophonic** — withheld with the rest;
5. a period consequent keeps its antecedent's texture — the answer keeps the
   question's;
6. otherwise **rotation with memory**: never the same state twice running, and
   an occasional (0.25) return to the state of two phrases ago — variety with
   a thread of recall, not a shuffle.

---

## 13. Rhythm, groove, and percussion

### 13.1 The rhythmic vocabulary (law + tunings)

Two primitives generate every rhythm:

- **Euclidean patterns** E(k, n) — maximally even k-in-n onsets (the tresillo,
  four-on-the-floor) — the percussion backbone.
- **The rough cell** — an even 8th-note pulse transformed by three stochastic
  passes: **merge** adjacent notes (probability 0.6·roughness — merges across
  beat boundaries *are* the syncopation), **split** long notes (only above
  density 0.6 — busyness), and **drop** notes (probability 0.55·(1−density) —
  *rests are content*). The first onset always survives on its slot, and at
  least two notes always remain. This one cell is the shared vocabulary of the
  melody's motifs, the pad's comping, and the counter's complementary line.

Density and roughness are the only stochastic steering of rhythmic placement;
everything else (kick count, hat subdivision, arp rate) is a deterministic
function of density.

### 13.2 Groove as a contract (A2, law)

*"Pattern identity is what makes harmonic change legible."* The stochastic
pattern draws — the ghost-snare set, the hat-drop mask, the open-hat choice,
the arp's traversal direction and skip mask — are **pinned once per phrase**:
the same probabilities, drawn from a per-phrase stream instead of per-bar. The
groove becomes an explicit identity the ear can track harmony through.
**Fills stay per-bar: they are the licensed variation.** Under stable shaping
parameters, the linter requires the non-fill pattern to be bar-identical
within the phrase.

### 13.3 The arpeggiator (tunings)

Chord tones cycled in a fixed two-octave pool above the pad (C5–C7), traversal
up / down / up-down fixed per phrase, 16ths above density 0.65 and 8ths below,
sparse skips as density falls (the downbeat never skips), traversal advancing
*through* rests so the figure stays a held pattern. It enters last of all
layers (energy &gt; 0.62) and is the tier the dramaturg withholds.

### 13.4 Percussion (tunings on laws)

- **Kick**: Euclidean E(k, slots) with k scaled by density (2–5 in 4/4). In
  compound meters, grouped kicks instead — the even felt pulses, gaining the
  shuffle 8th and a pre-downbeat pickup as density rises — because Euclidean
  spreading fights the 3+3 grouping.
- **Snare**: the backbeat generalized — the odd felt pulses (2 and 4 in 4/4,
  the second dotted quarter in 6/8). Ghost pickups appear as roughness rises
  (dead below 0.25), on the classic slot set in 4/4.
- **Hats**: subdivide by density (16ths above 0.7), accent the felt pulses, an
  occasional open hat on the pre-downbeat slot.
- **Fills and crashes**: fills are eligible on the cadence bar (and, under
  hypermeter, before the mid-phrase downbeat at reduced odds), probability
  growing with the tension arc (0.25 + 0.55·tension); a fill replaces the
  bar's tail with a snare/tom figure, and **earns a crash on the following
  downbeat — fills double as audible transition markers** (the iMUSE boundary
  principle). In a codetta the kit thins to half density: the afterglow keeps
  time without driving.

All drum velocities scale with the dynamic center; drums are unpitched and
exempt from every pitch law, but must stay on the declared kit.

---

## 14. Ties: crossing the barline

The grid invariant (§3.1) once quietly forbade two ubiquitous human gestures —
the pickup and the tied suspension. The tie flag restores them without
breaking the invariant (law):

- A note crossing the barline is a **chain of grid- and bar-legal halves**
  (out → both… → in); the chain *is* one musical note, recovered by merging.
- Merging keeps the head's onset and dynamics and sums the durations; tie-free
  material passes through untouched (the byte-identity anchor).
- An orphan "out" (a tie into a rest or an unhosting bar) legally dissolves
  into a plain note. **An orphan "in" — a continuation that never sounded — is
  a violation.**
- Consumers must treat the chain as one note: melodic-line laws judge merged
  notes (a tied-into downbeat is not an attack, samples no strong-beat ratio,
  fakes no leap); playback strikes no join; the performance layer never
  re-attacks, jitters, gaps, or echoes an interior half.

The three gestures built on the flag: the anacrusis (§9.5), the genuinely held
suspension preparation (§11.3), and cross-bar syncopation (§9.5).

---

## 15. The performed surface

### 15.1 The invariance doctrine (A1, law)

Performance shaping **never changes which notes exist**. The pre-performance
score is identical whether shaping runs or not; any audible difference *is*
the performance. Shaping is deterministic and structural — *"Humanize is
noise; this is expression: systematic deviation tied to structure."* (The one
additive device, the echo, tags its copies as echoes, licensed as reverb-like
bleed, never as wrong notes.)

### 15.2 The devices (tunings)

- **The hairpin**: each phrase swells to a crest **at the planned apex** and
  decays — loudness and pitch climax coincide.
- **Contour tracking**: higher is slightly louder, on the lines that are lines
  (melody fully, counter shallower).
- **Agogics**: the phrase-opening downbeat stretches slightly.
- **The luftpause**: a sliver of silence carved before the next phrase's
  downbeat — melody breathes between phrases. Pickups are spared: they are
  the breath.
- **Lay-back**: the line sits behind the beat when sparse, pushes ahead when
  dense, neutral at the middle.
- **Metric accent**: velocity follows the metric weight profile (downbeat up,
  16ths down), applied before any timing device.
- **Micro-timing and micro-dynamics**: small bounded jitter, after everything
  structural; a note never moves before its bar.
- **Idiomatic devices**: chords strum low-to-high with ends held in place; the
  arp carries a dotted-8th echo.

Per-voice assignment is a hierarchy of agency: the **melody** gets the full
treatment (deepest hairpin, contour, agogics, lay-back) — a real line, shaped
like one; the **counter** is subordinate (shallower, no agogics — the melody
owns the phrase's breath); pad, bass, and arp get the phrase swell only;
**drums get no phrase shaping at all** — one-shots have no audible duration to
shape. The cadential ritardando (§6.6) is the only tempo gesture; there is no
global rubato.

---

## 16. The bar cycle: how a bar is decided

The engine is pull-based: each bar is produced on demand, chords generated
**one bar ahead** so every layer can see what is coming — the bass approaches
the next root, the pad prepares the next suspension, the pickup targets the
next chord's third. A lever change during bar N therefore first reaches
harmony at N+2, everything else at N+1. Every phrase-level deviation
(cadence rationing, period commitment, clock surgery, split 6/4) is decided at
a phrase's first bar — at least two bars before any slot it changes, safely
outside the lookahead.

The per-bar order is load-bearing; each step exists because of what follows
it:

1. **Extension** decides first — before the dramaturg reads the phrase — so
   the ledger accrues the stretched length.
2. **The dramaturg** rules the phrase (accrue / spend / idle) at its first
   bar; codettas are exempt (outside the debt loop).
3. **Codetta or elision** is scheduled once the phrase's policy is settled;
   they read the directive's payoff.
4. **Wander** may queue a key change if the key has sat too long.
5. **The period planner** commits question/answer pairs — before the mapper
   samples the phrase's policy, so the antecedent's half cadence is in force
   from bar one.
6. **Instrument tiers** swap on the boundary (timbre is harmless to a pivot
   analysis); **mode** re-picks unless a modulation window holds it, applying
   any spend brightening.
7. **The mapper** samples affect into musical parameters (with slew and the
   cadential rit); the **directive** then subtracts what is withheld and adds
   the escalation; **hypermeter** shades the bar's dynamics.
8. **Texture** commits per phrase (§12.2); the counter layer joins when its
   state is in force.
9. **The motif lifecycle** advances on the boundary (spend-aware); the
   **apex** is planned once per phrase.
10. **Harmony** tops up the one-bar-ahead queue, resolving in precedence
    order: modulation chord → elision's forced I → codetta prolongation →
    consequent's replayed opening → split 6/4 → cadential 6/4 → lament
    ground → slow-harmonic-rhythm hold → the functional walk → inversion
    planning.
11. **The context** is assembled: effective tension, cadence slot and policy,
    obligations, phrase geometry, apex, the split timeline, the elision's
    dual annotation.
12. **The director** weighs authored signatures (a landmark may cash the
    ledger here).
13. **The voices realize in dependency order** — pad (split re-voicing /
    suspension / appoggiatura / animation / tie preparation), then bass
    (pedal, approach), then melody (which reads the realized bass through the
    outer-voice guard), then imitation and counter (which read the realized
    melody *and* bass), then arp (masked under imitation), then percussion
    (groove contract, fills, codetta thinning).
14. **Performance chains** shape each layer; a committed modulation adopts its
    new tonic at the arrival bar.

This ordering *is* the flow of constraints: time decisions flow downward
(clock → dramaturg → policy → harmony), and within the bar, constraint flows
from the outer frame inward (bass before melody before counter) so every
dependent voice reads realized notes, never intentions.

---

## 17. Obligations: the ledger of promises

An obligation is a first-class promise attached to a bar or a note, which the
linter **independently re-derives from the realized output** — the generator
claims a role; the linter verifies it discharges. The normative set:

| Obligation | Planted by | Must discharge as | Window |
|---|---|---|---|
| **suspension** | a preparable held voice (dramaturg cadence zone) | prepared by a same-pitch note ending at its onset; resolves **down by step** to a chord tone | at its release (mid-bar pulse) |
| **appoggiatura** (pad) | the payoff lean on a controlled authentic cadence | unprepared; resolves **down by step** to a chord tone | at its release |
| **pedal** | the dramaturg's dominant pedal | a contiguous one-pitch bass run ending at a cadence | run's end, or the next bar's cadence |
| **tonicize:N** | an applied dominant | the next bar's chord *is* degree N | one bar |
| **cadential 6/4** | I6/4 in the cadence approach | discharge onto **root-position V** | within the bar (split form) or the next |
| **lament** | the lament ground | the contiguous run reaches root-position V | run's end, or the next bar |

Two meta-laws govern the system: an infeasible request plants nothing (never a
dangling obligation), and obligation-free output trivially satisfies the whole
family (the checks are dormant, not waived).

---

## 18. The verification covenant

**Every rule the generator obeys becomes a linter rule, and every
obligation-style rule gets a test that plants a deliberate violation and
asserts the linter catches it.** The linter operates on the realized notes,
never on the generator's intentions, and is the acceptance suite promised to
any native reimplementation. Melodic-line laws judge merged (logical) notes;
grid and voicing laws run pre-performance; range and map laws run at both
stages.

The seven law families, with their operative thresholds:

1. **Core** — grid alignment; scale membership or licensed role; annotation
   honesty; pad voicing (range E3–G5, no unisons, chord membership per the
   timeline in force, voice moves ≤ 7 semitones); bass (range, root on beat 1,
   chord tones off it, licensed approaches/pedals); melody (range, ≥ 80%
   strong-beat chord tones, ≥ 90% leap recovery); tie coherence (no orphan
   "in"); doubling whitelist; countermelody species set (range, no crossing,
   strong-beat membership, ≥ 70% consonance, ≤ 40% overlap, no
   parallels/directs against melody or bass); percussion kit; cadence
   realization per policy (the split bar judged by its approaching segment).
2. **Groove** — pattern identity within the phrase; the fill is the licensed
   variation.
3. **Outer voices** — no consecutive perfects at strong melody onsets; no
   direct perfects into downbeats; ≥ 50% of cadences approached in
   contrary/oblique motion (elided cadences exempt — crashed into, not settled
   into).
4. **Periods** — a consequent must have its antecedent, and must answer with
   the question's opening rhythm (a landmark payoff overrides the answer — the
   arrival wins).
5. **Texture** — every phrase's texture claim verified against what sounds,
   positive and negative.
6. **Imitation** — every entry reproduces its source cell at recognizability
   ≥ 0.9.
7. **Obligations** — the table of §17.

Exemption is by **role**, in two tiers with distinct meanings: chromatic roles
(approach, borrowed, echo, motif, doubling, imitation) license *scale
departure*; the licensed-non-chord set (those plus passing, neighbor, pedal,
suspension, appoggiatura) licenses *non-chord status*. Whole-cell identities
(motif, imitation) are licensed as units — their law is recognizability, not
per-note membership. Ratio laws carry minimum sample sizes so they never fire
on noise.

---

## 19. Determinism and real-time doctrine

These are engineering doctrines with direct musical consequences, so they are
law here too:

- **Per-decision seeding.** A bar's material depends only on (master seed,
  declared musical state at that bar) — never on how many random draws earlier
  bars or other subsystems consumed. Two renders with the same seed and
  different lever curves stay note-identical until the levers actually
  diverge: an A/B isolates the lever's effect, not reshuffled randomness.
  Phrase-scoped decisions draw from phrase-scoped streams, which is what makes
  them stable across the one-bar lookahead.
- **Stateless except declared state.** The only sequential state is the
  explicit musical state (harmony memory, voice-leading memory, the ledger,
  the clock, the caches) — save/resume, replay, and exact A/B come free. The
  ledger itself is a pure function of (seed, affect trajectory, bar).
- **Byte-identical defaults.** Every feature is gated, and the disabled path
  reproduces the prior output byte-for-byte — the permanent regression anchor.
- **Boundary quantization** (§2.2) and the **one-bar lookahead / first-bar
  commitment discipline** (§16) are the two real-time contracts: control may
  arrive at any moment, but music changes only at musical joints, and no
  decision is ever made after a bar that its consequences precede.

---

## 20. Decision records and open ground

Recorded decisions that bound this spec:

- **Harmonic rhythm (D3)**: one chord per bar is primary; the optional
  intra-bar timeline serves the cadence approach only (§5.5).
- **No Locrian** on the mode axis; **no 11th/13th extensions**; **no plagal
  cadence policy** (plagal color is a codetta gesture).
- **Deliberately out of scope**: machine learning and corpus training (*"we
  want explainable rules"*); the game-state→affect model (the affect API is
  the integration seam; the mapping from game state to affect is a separate,
  game-specific model); authored timbre design beyond what the synthesis
  requirements probe needed.
- **Deferred by ear**: modal-mixture obligations at cadences (collides with
  the applied dominant at the same slot); ionian leading tones on modal
  modulation dominants (revisit if arrivals feel under-sold).
- **Planned, not landed**: `foreshadow()` — game-supplied lookahead planting
  setup that either blooms on time or decays into something still intentional
  (deceptive resolution doing the work); and multi-bar authored signature
  sequences. Both extend this spec when they land; nothing here precludes
  them.

The spec-freeze statement this document inherits: the IR (ties, six layers,
the texture parameter, the optional chord timeline), the phrase clock, and the
counterpoint/species law families were settled by building and listening, not
by prediction. *"Everything else ports as tuning, not architecture."*
