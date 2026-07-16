/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Motif type, contour factories, variation ops, lifecycle, signature-faithful realizers.
// make_motif draw order: rough_cell -> choice(shape) -> randint(span). max/min keep first-of-equals.

#ifndef ANO_MUSIC_MOTIF_H
#define ANO_MUSIC_MOTIF_H

#include <limits.h>

#include "music_gen.h"

#define ANO_NEAR_NONE INT_MIN // "near = None" for realizers

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
    bool   planApex;       // false (A4)
    bool   counterpoint;   // false (A3)
} AnoMelodyConfig;

AnoMelodyConfig ano_melody_config_default(void);

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

int ano_motif_markedness(const AnoMotif *m); // 0..5

// Most-marked of attempts draws, then tail-merge and midpoint-lift repair.
AnoMotif ano_make_signature(AnoMusicRng *rng, double density, double roughness,
                            const AnoMelodyConfig *cfg, int slots, int attempts);

// choices((bars-3, bars-2), (0.45, 0.55)) then randint for the apex pitch.
AnoApexPlan ano_make_apex(AnoMusicRng *rng, int bars, int center, int rangeSemitones);

// Variation ops. ornament draws choice((-1,1)) only when longest note can split.
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


/* Lifecycle + Realization */

typedef enum AnoLifecycleState
{
    ANO_LIFE_INTRODUCED = 0,
    ANO_LIFE_DEVELOPED,
    ANO_LIFE_COMPLETED,
} AnoLifecycleState;

typedef struct AnoMotifLifecycle
{
    AnoMotif motif;
    int      developAfter;    // 2
    uint8_t  state;           // AnoLifecycleState
    int      statements;
    int      completedPhrase; // -1 = None
} AnoMotifLifecycle;

// Advance at phrase boundary. spend admits completed.
uint8_t ano_lifecycle_advance(AnoMotifLifecycle *lc, bool spend, int phrase);

int ano_diatonic_interval(AnoScale scale, int a, int b); // signed scale steps

// Contour-exact unit transpose. Strong beats favor chord tones; entry nearest `near` breaks ties.
// strongMask = slot bitmask. Returns note count.
uint32_t ano_realize_faithful(const AnoMotif *m, AnoScale scale,
                              const uint8_t *chordPcs, uint32_t pcCount,
                              int lo, int hi, uint32_t strongMask, int near,
                              AnoPlacedNote out[ANO_MOTIF_MAX]);

// Completed statement fused with cadence: final note on target pc, held to bar end.
uint32_t ano_realize_cadential(const AnoMotif *m, AnoScale scale,
                               const uint8_t *targetPcs, uint32_t pcCount,
                               int lo, int hi, int near, int slots,
                               AnoPlacedNote out[ANO_MOTIF_MAX]);

double ano_motif_fit(const AnoMotif *m, AnoScale scale,
                     const uint8_t *chordPcs, uint32_t pcCount,
                     int lo, int hi, uint32_t strongMask, int near);

double ano_recognizability(const AnoMotif *m, const int *pitches, uint32_t n,
                           AnoScale scale);

#endif // ANO_MUSIC_MOTIF_H
