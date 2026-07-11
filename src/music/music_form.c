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

int ano_clock_frontier(const AnoPhraseClock *c)
{
    if (c->segmentCount == 0)
        return 0;
    const AnoSegment *last = &c->segments[c->segmentCount - 1];
    return last->start + last->bars;
}

AnoPhrasePos ano_clock_position(const AnoPhraseClock *c, int bar)
{
    for (int idx = (int)c->segmentCount - 1; idx >= 0; --idx) {
        const AnoSegment *seg = &c->segments[idx];
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
    while ((int)c->segmentCount <= phrase && c->segmentCount < ANO_MAX_SEGMENTS) {
        c->segments[c->segmentCount] =
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
    if ((int)c->segmentCount == phrase && c->segmentCount < ANO_MAX_SEGMENTS) {
        c->segments[c->segmentCount] = seg;
        c->segmentCount++;
    }
    return seg;
}
