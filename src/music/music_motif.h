/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_motif.h (private to src/music/)
 * Motif machinery: the Motif type, contour shapes, the drawing factories
 * (make_motif / make_signature / make_apex), the variation operators and
 * phrase-variant plan (musicgen/gen/melody.py's leaf half), and the
 * signature-faithful realizers with the lifecycle (musicgen/gen/motif.py).
 * Draw parity: make_motif draws rough_cell then choice(shape) then
 * randint(span); phrase_variant and ornament draw only on the paths the
 * prototype does. All max()/min() selections keep first-of-equals.
 */

#ifndef ANO_MUSIC_MOTIF_H
#define ANO_MUSIC_MOTIF_H

#include <limits.h>

#include "music_gen.h"

// "near = None" sentinel for the realizers (ref falls back to (lo+hi)//2).
#define ANO_NEAR_NONE INT_MIN

// CONTOUR_SHAPES in prototype tuple order (feeds rng.choice).
// One realized note: (slot, dur_slots, pitch).
typedef struct AnoPlacedNote
{
    int slot;
    int durSlots;
    int pitch;
} AnoPlacedNote;

typedef struct AnoMelodyConfig
{
    int    rangeSemitones; // 12
    double barRestMax;     // 0.30
    int    spanMin;        // 2
    int    spanMax;        // 4
    bool   planApex;       // false (A4; off = byte-identical)
    bool   counterpoint;   // false (A3; off = byte-identical)
} AnoMelodyConfig;

AnoMelodyConfig ano_melody_config_default(void);

// Single-peak contour plan: the apex bar within the phrase and its pitch.
typedef struct AnoApexPlan
{
    int pos;
    int pitch;
} AnoApexPlan;

// Contour offsets for a shape over n notes spanning `span` diatonic steps.
void ano_contour_offsets(AnoContourShape shape, uint32_t n, int span,
                         int out[ANO_MOTIF_MAX]);

// rough_cell + choice(shape) + randint(span), in that draw order.
AnoMotif ano_make_motif(AnoMusicRng *rng, double density, double roughness,
                        const AnoMelodyConfig *cfg, int slots);

// Distinctiveness score 0..5 (motto length, >= 2 durations, a leap, a step,
// a direction change).
int ano_motif_markedness(const AnoMotif *m);

// Most-marked of `attempts` draws, then tail-merge and midpoint-lift repair.
AnoMotif ano_make_signature(AnoMusicRng *rng, double density, double roughness,
                            const AnoMelodyConfig *cfg, int slots, int attempts);

// choices((bars-3, bars-2), (0.45, 0.55)) then randint for the apex pitch.
AnoApexPlan ano_make_apex(AnoMusicRng *rng, int bars, int center, int rangeSemitones);

// Variation operators (draw-free except ornament, which draws choice((-1,1))
// only when the longest note can split).
AnoMotif ano_motif_sequence(const AnoMotif *m, int steps);
AnoMotif ano_motif_invert(const AnoMotif *m);
AnoMotif ano_motif_displace(const AnoMotif *m, int slots);
AnoMotif ano_motif_truncate(const AnoMotif *m);
AnoMotif ano_motif_ornament(const AnoMotif *m, AnoMusicRng *rng);

// admissible_transforms order: identity, inversion, displacement, truncation.
typedef enum AnoTransform
{
    ANO_XFORM_IDENTITY = 0,
    ANO_XFORM_INVERSION,
    ANO_XFORM_DISPLACEMENT,
    ANO_XFORM_TRUNCATION,
    ANO_XFORM_COUNT,
} AnoTransform;

AnoMotif ano_motif_transform(const AnoMotif *m, AnoTransform t, int slots);

// Sentence-form plan ops (phrase_variant traces).
typedef enum AnoVariantOp
{
    ANO_VAR_STATEMENT = 0,
    ANO_VAR_SEQUENCE, // *outStep carries the drawn step
    ANO_VAR_RESTATEMENT,
    ANO_VAR_ORNAMENT,
    ANO_VAR_INVERT,
    ANO_VAR_DISPLACE,
    ANO_VAR_TRUNCATE,
} AnoVariantOp;

AnoMotif ano_phrase_variant(const AnoMotif *m, int pos, AnoMusicRng *rng, int slots,
                            AnoVariantOp *outOp, int *outStep);

// ---------------------------------------------------------------------------
// Lifecycle + signature-faithful realization (gen/motif.py)
// ---------------------------------------------------------------------------

typedef enum AnoLifecycleState
{
    ANO_LIFE_INTRODUCED = 0,
    ANO_LIFE_DEVELOPED,
    ANO_LIFE_COMPLETED,
} AnoLifecycleState;

typedef struct AnoMotifLifecycle
{
    AnoMotif motif;
    int      developAfter;    // 2: disguised statements before it may complete
    uint8_t  state;           // AnoLifecycleState
    int      statements;
    int      completedPhrase; // -1 = None
} AnoMotifLifecycle;

// Advance at a phrase boundary; returns this phrase's state. `spend` is the
// dramaturg's release — the only gate admitting `completed`.
uint8_t ano_lifecycle_advance(AnoMotifLifecycle *lc, bool spend, int phrase);

// Signed scale steps from a to b (both snapped to the scale first).
int ano_diatonic_interval(AnoScale scale, int a, int b);

// Contour-exact realization transposed as a unit: strong beats favor chord
// tones at the best base, entry nearest `near` breaks ties. strongMask is a
// slot bitmask. Returns the note count.
uint32_t ano_realize_faithful(const AnoMotif *m, AnoScale scale,
                              const uint8_t *chordPcs, uint32_t pcCount,
                              int lo, int hi, uint32_t strongMask, int near,
                              AnoPlacedNote out[ANO_MOTIF_MAX]);

// The completed statement fused with the cadence: the final note lands on a
// target pc and holds to the bar end.
uint32_t ano_realize_cadential(const AnoMotif *m, AnoScale scale,
                               const uint8_t *targetPcs, uint32_t pcCount,
                               int lo, int hi, int near, int slots,
                               AnoPlacedNote out[ANO_MOTIF_MAX]);

// Fraction of strong-beat notes on chord tones at the connecting
// transposition (1.0 when no strong-beat notes).
double ano_motif_fit(const AnoMotif *m, AnoScale scale,
                     const uint8_t *chordPcs, uint32_t pcCount,
                     int lo, int hi, uint32_t strongMask, int near);

// Fraction of successive contour intervals preserved in realized pitches.
double ano_recognizability(const AnoMotif *m, const int *pitches, uint32_t n,
                           AnoScale scale);

#endif // ANO_MUSIC_MOTIF_H
