/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Phrase clock + antecedent-consequent PeriodPlanner.
// Cadence precedence: modulation > override > dramaturg > planner > mapper.

#ifndef ANO_MUSIC_FORM_H
#define ANO_MUSIC_FORM_H

#include "music_ir.h"
#include "music_motif.h"

// Direct-mapped rings over unbounded phrase/bar indices. Slot = index mod WINDOW.
// Tag holds owning index; stale WINDOW-ago entry never matches. Lookback must stay within WINDOW
// (deepest: texture rotation 2 phrases; planner antecedent at phrase-1; bar caches read in-phrase).
// Index stays absolute (RNG stream tags); rebasing changes the music.
#define ANO_PHRASE_WINDOW 32
#define ANO_BAR_WINDOW    256

static inline uint32_t ano_ring_phrase(int phrase)
{
    return (uint32_t)((phrase % ANO_PHRASE_WINDOW + ANO_PHRASE_WINDOW)
                      % ANO_PHRASE_WINDOW);
}

static inline uint32_t ano_ring_bar(int bar)
{
    return (uint32_t)((bar % ANO_BAR_WINDOW + ANO_BAR_WINDOW) % ANO_BAR_WINDOW);
}

// Tags init to ANO_SLOT_EMPTY, not zero (phrase 0 / bar 0 are real).
#define ANO_SLOT_EMPTY (-1)

static inline bool ano_phrase_live(const int *tag, int phrase)
{
    return phrase >= 0 && tag[ano_ring_phrase(phrase)] == phrase;
}

static inline bool ano_bar_live(const int *tag, int bar)
{
    return bar >= 0 && tag[ano_ring_bar(bar)] == bar;
}


/* Phrase Clock */

// Elision segment starts ON the previous segment's cadence bar.
typedef struct AnoSegment
{
    int     start;
    int     bars;
    uint8_t kind; // AnoSegKind
} AnoSegment;

// segments ring of last ANO_PHRASE_WINDOW; segmentCount is absolute (next phrase index).
typedef struct AnoPhraseClock
{
    int        phraseBars; // 8
    AnoSegment segments[ANO_PHRASE_WINDOW];
    uint32_t   segmentCount;
} AnoPhraseClock;

static inline AnoPhraseClock ano_phrase_clock(int phraseBars)
{
    return (AnoPhraseClock){ .phraseBars = phraseBars };
}

int ano_clock_frontier(const AnoPhraseClock *c); // bar where extrapolation begins

// Later segments win a shared bar.
AnoPhrasePos ano_clock_position(const AnoPhraseClock *c, int bar);

void ano_clock_materialize_through(AnoPhraseClock *c, int phrase);

// Commit frontier phrase. overlap starts that many bars inside previous (elision = 1).
AnoSegment ano_clock_schedule(AnoPhraseClock *c, int phrase, int bars,
                              AnoSegKind kind, int overlap);


/* Period Planner */

// Roles committed pairwise at even phrase boundaries; antecedent opening recorded for answer.
typedef struct AnoPeriodPlanner
{
    int    roleTag[ANO_PHRASE_WINDOW]; // owning phrase, ANO_SLOT_EMPTY = unset
    int8_t roles[ANO_PHRASE_WINDOW];   // AnoPhraseForm
    int      openingTag[ANO_PHRASE_WINDOW];
    AnoChord openingChord[ANO_PHRASE_WINDOW];
    AnoScale openingScale[ANO_PHRASE_WINDOW];
    AnoPlacedNote openingMelody[ANO_PHRASE_WINDOW][ANO_MOTIF_MAX];
    uint32_t      openingMelodyN[ANO_PHRASE_WINDOW];
} AnoPeriodPlanner;

void ano_planner_init(AnoPeriodPlanner *p);

static inline AnoPhraseForm ano_planner_role(const AnoPeriodPlanner *p, int phrase)
{
    if (!ano_phrase_live(p->roleTag, phrase))
        return ANO_FORM_NONE;
    return (AnoPhraseForm)p->roles[ano_ring_phrase(phrase)];
}

static inline void ano_planner_commit(AnoPeriodPlanner *p, int phrase)
{
    if (phrase < 0)
        return;
    p->roleTag[ano_ring_phrase(phrase)] = phrase;
    p->roles[ano_ring_phrase(phrase)] = ANO_FORM_ANTECEDENT;
    p->roleTag[ano_ring_phrase(phrase + 1)] = phrase + 1;
    p->roles[ano_ring_phrase(phrase + 1)] = ANO_FORM_CONSEQUENT;
}

static inline bool ano_planner_has_opening(const AnoPeriodPlanner *p, int phrase)
{
    return ano_phrase_live(p->openingTag, phrase);
}

#endif // ANO_MUSIC_FORM_H
