/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_form.c
 * The scheduled phrase clock. All-integer logic; the parity contract is the
 * reversed segment scan (later segments win a shared bar) and the frontier
 * extrapolation reproducing pure div/mod when nothing is scheduled.
 */

#include "music_form.h"

void ano_planner_init(AnoPeriodPlanner *p)
{
    *p = (AnoPeriodPlanner){ 0 };
    for (uint32_t i = 0; i < ANO_PHRASE_WINDOW; ++i) {
        p->roleTag[i] = ANO_SLOT_EMPTY;
        p->openingTag[i] = ANO_SLOT_EMPTY;
    }
}

int ano_clock_frontier(const AnoPhraseClock *c)
{
    if (c->segmentCount == 0)
        return 0;
    const AnoSegment *last = &c->segments[ano_ring_phrase((int)c->segmentCount - 1)];
    return last->start + last->bars;
}

// The reverse scan stops at the live window: a segment older than that has been
// overwritten, and no caller asks about one — position() is queried for the
// current bar and the one-bar chord lookahead only.
AnoPhrasePos ano_clock_position(const AnoPhraseClock *c, int bar)
{
    int oldest = (int)c->segmentCount - ANO_PHRASE_WINDOW;
    if (oldest < 0)
        oldest = 0;
    for (int idx = (int)c->segmentCount - 1; idx >= oldest; --idx) {
        const AnoSegment *seg = &c->segments[ano_ring_phrase(idx)];
        if (seg->start <= bar && bar < seg->start + seg->bars)
            return (AnoPhrasePos){ .phrase = idx, .pos = bar - seg->start,
                                   .bars = seg->bars, .kind = (AnoSegKind)seg->kind };
    }
    int base = ano_clock_frontier(c);
    if (bar < base) // inside no segment but before the frontier (defensive)
        return (AnoPhrasePos){ .phrase = bar / c->phraseBars,
                               .pos = bar % c->phraseBars, .bars = c->phraseBars };
    int n = (int)c->segmentCount + (bar - base) / c->phraseBars;
    return (AnoPhrasePos){ .phrase = n, .pos = (bar - base) % c->phraseBars,
                           .bars = c->phraseBars };
}

void ano_clock_materialize_through(AnoPhraseClock *c, int phrase)
{
    while ((int)c->segmentCount <= phrase) {
        c->segments[ano_ring_phrase((int)c->segmentCount)] =
            (AnoSegment){ ano_clock_frontier(c), c->phraseBars, ANO_SEG_REGULAR };
        c->segmentCount++;
    }
}

AnoSegment ano_clock_schedule(AnoPhraseClock *c, int phrase, int bars,
                              AnoSegKind kind, int overlap)
{
    ano_clock_materialize_through(c, phrase - 1);
    // only the frontier is schedulable (the prototype asserts)
    AnoSegment seg = { ano_clock_frontier(c) - overlap, bars, (uint8_t)kind };
    if ((int)c->segmentCount == phrase) {
        c->segments[ano_ring_phrase(phrase)] = seg;
        c->segmentCount++;
    }
    return seg;
}
