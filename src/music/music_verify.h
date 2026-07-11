/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_verify.h (private to src/music/)
 * The theory linter (musicgen/verify.py): executable sanity checks over the
 * generated IR — TECH_SPEC section 8.4's acceptance oracle, phase 2. A render
 * that lints clean is not merely bit-identical to the prototype, it is
 * musically well-formed by the prototype's own rules.
 *
 * ano_lint covers the rules that need only events plus contexts: scale
 * membership with role-licensed chromaticism, annotation consistency, grid
 * alignment, pad voicing, bass root and chord membership, melodic line,
 * doubling, counterpoint, tie coherence, the drum map, cadence realization,
 * and the structural obligations (a suspension resolves, a pedal terminates
 * at a cadence, a secondary dominant discharges). The five standalone
 * contracts each need something extra — per-bar params, or the engine's
 * phrase caches — and are called separately, exactly as in the prototype.
 *
 * A violation's rule and bar are the checkable contract (the parity test
 * compares them against the CPython linter); the message is human text and
 * carries no contract. Rules are dormant on output that plants nothing they
 * judge, so pre-feature renders lint clean.
 */

#ifndef ANO_MUSIC_VERIFY_H
#define ANO_MUSIC_VERIFY_H

#include "music_form.h"
#include "music_ir.h"
#include "music_motif.h"

// ---------------------------------------------------------------------------
// Limits and report
// ---------------------------------------------------------------------------

// LintLimits. Ranges are inclusive MIDI bounds; ratios are floors (or, for
// counter_overlap, a ceiling) on piece-level aggregate rules.
typedef struct AnoLintLimits
{
    int    maxVoiceMove;          // 7 semitones between successive pad voicings
    int    padLo, padHi;          // 52, 79
    int    bassLo, bassHi;        // 26, 55
    int    melodyLo, melodyHi;    // 54, 90
    double melodyStrongChordRatio; // 0.8
    double leapResolutionRatio;    // 0.9
    int    leapSemitones;          // 5: beyond this an interval is a leap
    int    counterLo, counterHi;   // 55, 79 (the tenor gap)
    double counterConsonanceRatio; // 0.7
    double counterOverlapRatio;    // 0.4
    const uint8_t *drumPitches;    // gen.perc DRUMS; defaults to ANO_DRUM_PITCHES
    uint32_t       drumPitchCount;
} AnoLintLimits;

AnoLintLimits ano_lint_limits_default(void);

// "pre" lints generator output (grid-aligned, obligations intact); "post"
// lints after the modifier chains, which move events off the grid and stagger
// the pad's starts — the slot-based and voicing rules do not apply there.
typedef enum AnoLintStage
{
    ANO_LINT_PRE = 0,
    ANO_LINT_POST,
} AnoLintStage;

#define ANO_LINT_MAX_VIOLATIONS 256

typedef struct AnoViolation
{
    const char *rule; // static string: "scale", "pad-range", "tie", ...
    int         bar;  // 0-based; -1 for piece-level aggregate rules
    char        message[192];
} AnoViolation;

typedef struct AnoLintReport
{
    AnoViolation v[ANO_LINT_MAX_VIOLATIONS];
    uint32_t     count;
    uint32_t     dropped; // violations beyond capacity
} AnoLintReport;

void ano_lint_report_reset(AnoLintReport *r);

// Format a violation as the prototype's __str__ ("[rule] bar N: message").
const char *ano_violation_str(const AnoViolation *v, char *buf, uint32_t cap);

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

// Every lint appends to the report; the caller resets it. Contexts are keyed
// by ctx->bar (a bar with no context is "uncovered" — only an echo tail may
// live there). params, where taken, is parallel to contexts: params[i] is the
// parameter block of bar contexts[i].bar.
//
// horizon marks a TRUNCATED render: contexts at or beyond it are lookahead —
// they let an obligation planted in the last rendered bar discharge past the
// edge (a cadential 6/4 there resolves onto the V of the bar after) without
// themselves being judged, since they host no events. Render N + k bars, lint
// the events of the first N, pass every context, set horizon = N. A negative
// horizon judges every context, which is what a complete piece wants.
//
// The per-layer rules work in fixed line buffers (1024 events per layer). A
// layer past that would be truncated, so ano_lint reports a "capacity"
// violation rather than let a truncated lint pass as a clean one; run it
// alongside the standalone contracts, which share the same bound.

void ano_lint(const AnoMusicEvent *events, uint32_t n,
              const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
              AnoMeter meter, AnoLintStage stage,
              const AnoLintLimits *limits, AnoLintReport *out);

// A2: within a phrase, under bar-to-bar-stable shaping params, the non-fill
// percussion pattern and the arp's onset mask must not be re-rolled — pattern
// identity is what makes harmonic change legible. Pre-modifier IR.
void ano_lint_groove(const AnoMusicEvent *events, uint32_t n,
                     const AnoHarmonicContext *contexts,
                     const AnoGenParams *params, uint32_t nctx, int horizon,
                     AnoMeter meter, AnoLintReport *out);

// A3: the outer-voice frame — no consecutive or direct perfects between the
// melody and bass at successive strong-slot onsets; cadences prefer a
// contrary/oblique approach (contraryMin, prototype default 0.5).
void ano_lint_outer(const AnoMusicEvent *events, uint32_t n,
                    const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                    AnoMeter meter, double contraryMin, AnoLintReport *out);

// B2: a committed antecedent-consequent pair must answer — the consequent's
// opening bar carries the antecedent's melodic rhythm, unless a landmark
// payoff overrode it (the arrival wins).
void ano_lint_periods(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                      AnoMeter meter, AnoLintReport *out);

// C4: the Tier-2 texture state is a claim about the sounding events —
// "doubled" means doubling sounds, "imitative" means an entry exists, the
// lean states mean no polyphony sounds and "monophonic" thins the pad to
// dyads. Dormant when params carry no texture.
void ano_lint_texture(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts,
                      const AnoGenParams *params, uint32_t nctx, int horizon,
                      AnoMeter meter, AnoLintReport *out);

// C3: each phrase's imitation entry must still carry its source cell's
// contour (recognizability >= threshold, prototype default 0.9). cellTag/cells
// are the engine's per-phrase cache (AnoConductorState.imitationTag /
// imitationCells) — the tagged ring, read by phrase rank.
void ano_lint_imitation(const AnoMusicEvent *events, uint32_t n,
                        const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                        const int *cellTag, const AnoMotif *cells,
                        AnoMeter meter, double threshold, AnoLintReport *out);

#endif // ANO_MUSIC_VERIFY_H
