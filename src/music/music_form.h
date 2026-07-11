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

// The sequential state is a set of caches over an UNBOUNDED index (the piece
// runs forever), so each is a direct-mapped ring: slot = index mod WINDOW, and
// every slot carries the index that owns it. A stale entry from index-WINDOW can
// then never be mistaken for index, which makes the ring behave exactly like an
// unbounded array — provided no rule looks back further than WINDOW. The deepest
// lookbacks are the texture rotation's 2 phrases and the planner's antecedent at
// phrase-1; bar caches are read inside the phrase that planted them. The windows
// below carry an order of magnitude of margin over both.
//
// The index itself stays ABSOLUTE everywhere: it spells the RNG stream tag
// ("<seed>:<name>:<bar>"), so rebasing it would change the music.
#define ANO_PHRASE_WINDOW 32  // live phrase caches (and scheduled segments)
#define ANO_BAR_WINDOW    256  // live per-bar caches (splits, laments, elisions)

static inline uint32_t ano_ring_phrase(int phrase)
{
    return (uint32_t)((phrase % ANO_PHRASE_WINDOW + ANO_PHRASE_WINDOW)
                      % ANO_PHRASE_WINDOW);
}

static inline uint32_t ano_ring_bar(int bar)
{
    return (uint32_t)((bar % ANO_BAR_WINDOW + ANO_BAR_WINDOW) % ANO_BAR_WINDOW);
}

// Tag arrays are initialized to ANO_SLOT_EMPTY, NOT zeroed: phrase 0 and bar 0
// are real indices, so a zeroed tag would claim them.
#define ANO_SLOT_EMPTY (-1)

static inline bool ano_phrase_live(const int *tag, int phrase)
{
    return phrase >= 0 && tag[ano_ring_phrase(phrase)] == phrase;
}

static inline bool ano_bar_live(const int *tag, int bar)
{
    return bar >= 0 && tag[ano_ring_bar(bar)] == bar;
}

// One scheduled phrase: an "elision" segment starts ON the previous
// segment's cadence bar.
typedef struct AnoSegment
{
    int     start;
    int     bars;
    uint8_t kind; // AnoSegKind
} AnoSegment;

// segments is a ring of the last ANO_PHRASE_WINDOW scheduled phrases;
// segmentCount is the ABSOLUTE number ever scheduled (phrase i lives in slot
// i mod WINDOW), so it doubles as the next phrase index.
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
        return ANO_FORM_NONE; // the "" default
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
