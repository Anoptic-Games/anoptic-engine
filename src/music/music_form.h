/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_form.h (private to src/music/)
 * Phrase-level form (musicgen/gen/form.py): the scheduled PhraseClock —
 * committed segments (codetta / extension / elision) extrapolated with the
 * default length beyond the frontier; with nothing scheduled position()
 * reproduces ano_phrase_position exactly — and the antecedent-consequent
 * PeriodPlanner (dumb state; commitment logic lives in the conductor).
 * Cadence precedence: modulation > override > dramaturg > planner > mapper.
 */

#ifndef ANO_MUSIC_FORM_H
#define ANO_MUSIC_FORM_H

#include "music_ir.h"
#include "music_motif.h"

#define ANO_MAX_SEGMENTS 64
#define ANO_MAX_PHRASES  128

// One scheduled phrase: an "elision" segment starts ON the previous
// segment's cadence bar.
typedef struct AnoSegment
{
    int     start;
    int     bars;
    uint8_t kind; // AnoSegKind
} AnoSegment;

typedef struct AnoPhraseClock
{
    int        phraseBars; // 8
    AnoSegment segments[ANO_MAX_SEGMENTS];
    uint32_t   segmentCount;
} AnoPhraseClock;

static inline AnoPhraseClock ano_phrase_clock(int phraseBars)
{
    return (AnoPhraseClock){ .phraseBars = phraseBars };
}

// The bar where extrapolation begins (after the last segment).
int ano_clock_frontier(const AnoPhraseClock *c);

// Later segments win a shared bar (an elision's opening downbeat belongs to
// the NEW phrase).
AnoPhrasePos ano_clock_position(const AnoPhraseClock *c, int bar);

// Fill default segments through `phrase` (observably a no-op; moves the
// frontier so a deviation can be appended).
void ano_clock_materialize_through(AnoPhraseClock *c, int phrase);

// Commit the frontier phrase with an explicit length/kind; `overlap` starts
// it that many bars INSIDE the previous segment (elision = 1).
AnoSegment ano_clock_schedule(AnoPhraseClock *c, int phrase, int bars,
                              AnoSegKind kind, int overlap);

// B2 sequential state: roles committed pairwise at even phrase boundaries;
// the antecedent's opening is recorded when its first bar sounds so the
// consequent can answer the same question.
typedef struct AnoPeriodPlanner
{
    int8_t roles[ANO_MAX_PHRASES]; // AnoPhraseForm; NONE is the "" default
    bool     hasOpening[ANO_MAX_PHRASES];
    AnoChord openingChord[ANO_MAX_PHRASES];
    AnoScale openingScale[ANO_MAX_PHRASES];
    AnoPlacedNote openingMelody[ANO_MAX_PHRASES][ANO_MOTIF_MAX];
    uint32_t      openingMelodyN[ANO_MAX_PHRASES];
} AnoPeriodPlanner;

static inline AnoPhraseForm ano_planner_role(const AnoPeriodPlanner *p, int phrase)
{
    if (phrase < 0 || phrase >= ANO_MAX_PHRASES)
        return ANO_FORM_NONE;
    return (AnoPhraseForm)p->roles[phrase];
}

static inline void ano_planner_commit(AnoPeriodPlanner *p, int phrase)
{
    if (phrase >= 0 && phrase < ANO_MAX_PHRASES)
        p->roles[phrase] = ANO_FORM_ANTECEDENT;
    if (phrase + 1 >= 0 && phrase + 1 < ANO_MAX_PHRASES)
        p->roles[phrase + 1] = ANO_FORM_CONSEQUENT;
}

#endif // ANO_MUSIC_FORM_H
