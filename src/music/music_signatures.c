/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// overdue x fit. Library order breaks ties. Request ranks 1e6 + fit.

#include <string.h>

#include "music_signatures.h"

// Selection constants (prototype module-level).
#define OVERDUE_SCALE  16.0
#define THRESH_STRICT  0.55
#define THRESH_LENIENT 0.15
#define FIT_MIN        0.34
#define NEVER          999

// ano_director_init clamps against both constants; weld each to the buffer it stands for
// so a widened struct cannot drift away from its bound.
static_assert(sizeof ((AnoMotifDirector *)0)->library / sizeof(AnoSignatureMotif) == ANO_SIG_MAX
              && sizeof ((AnoMotifDirector *)0)->barsSince / sizeof(int) == ANO_SIG_MAX,
              "director library capacity must equal ANO_SIG_MAX");
static_assert(sizeof ((AnoMotif *)0)->rhythm / sizeof(AnoRhythmNote) == ANO_MOTIF_MAX
              && sizeof ((AnoMotif *)0)->contour / sizeof(int) == ANO_MOTIF_MAX,
              "motif clamp bound must equal the motif buffers' extent");

// In: library/count authored motifs (the internal AnoEngineConfig path reaches here
// without passing the public seam's expand()). Out: *d owns a clamped copy.
// Invariant: every stored motif.n <= ANO_MOTIF_MAX, the extent of the rhythm/contour
// buffers the transforms and realizers write through it.
void ano_director_init(AnoMotifDirector *d, const AnoSignatureMotif *library,
                       uint32_t count)
{
    *d = (AnoMotifDirector){ 0 };
    if (count > ANO_SIG_MAX)
        count = ANO_SIG_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        d->library[i] = library[i];
        if (d->library[i].motif.n > ANO_MOTIF_MAX) // authored count can't exceed the buffers
            d->library[i].motif.n = ANO_MOTIF_MAX;
        d->barsSince[i] = -1; // dict-absent
    }
    d->libraryCount = count;
}

void ano_director_age(AnoMotifDirector *d, int bars)
{
    for (uint32_t i = 0; i < d->libraryCount; ++i)
        if (d->barsSince[i] >= 0)
            d->barsSince[i] += bars;
}

void ano_director_observe(AnoMotifDirector *d, const char *tag, int bars)
{
    ano_director_age(d, bars);
    for (uint32_t i = 0; i < d->libraryCount; ++i)
        if (strcmp(d->library[i].tag, tag) == 0)
            d->barsSince[i] = 0;
}

// (fit, transform, motif) of the best-fitting admissible transform; first
// max wins. Transforms always at 16 slots (the prototype default).
static double best_transform(const AnoSignatureMotif *sig, AnoScale scale,
                             const uint8_t *chordPcs, uint32_t pcCount,
                             int lo, int hi, uint32_t strongMask, int near,
                             AnoTransform *outXform, AnoMotif *outMotif)
{
    double bestFit = -1.0;
    for (int t = 0; t < ANO_XFORM_COUNT; ++t) {
        AnoMotif m = ano_motif_transform(&sig->motif, (AnoTransform)t, 16);
        double fit = ano_motif_fit(&m, scale, chordPcs, pcCount, lo, hi,
                                   strongMask, near);
        if (bestFit < 0.0 || fit > bestFit) {
            bestFit = fit;
            *outXform = (AnoTransform)t;
            *outMotif = m;
        }
    }
    return bestFit;
}

bool ano_director_select(AnoMotifDirector *d, AnoScale scale,
                         const uint8_t *chordPcs, uint32_t pcCount,
                         int lo, int hi, uint32_t strongMask,
                         double leniency, int near, const char *requested,
                         uint32_t *outSig, AnoTransform *outXform, AnoMotif *outMotif)
{
    double threshold = THRESH_STRICT - leniency * (THRESH_STRICT - THRESH_LENIENT);
    bool haveBest = false;
    double bestRank = 0.0;
    for (uint32_t i = 0; i < d->libraryCount; ++i) {
        const AnoSignatureMotif *sig = &d->library[i];
        int since = d->barsSince[i] >= 0 ? d->barsSince[i] : NEVER;
        double overdue = since * sig->importance;
        double pressure = overdue / OVERDUE_SCALE;
        if (pressure > 1.0)
            pressure = 1.0; // min(1.0, ...)
        AnoTransform xform;
        AnoMotif motifT;
        double fit = best_transform(sig, scale, chordPcs, pcCount, lo, hi,
                                    strongMask, near, &xform, &motifT);
        bool forced = requested && requested[0] && strcmp(requested, sig->tag) == 0;
        if (!((forced && fit >= FIT_MIN) || (!forced && pressure * fit >= threshold)))
            continue;
        double rank = forced ? 1e6 + fit : pressure * fit;
        if (!haveBest || rank > bestRank) {
            haveBest = true;
            bestRank = rank;
            *outSig = i;
            *outXform = xform;
            *outMotif = motifT;
        }
    }
    return haveBest;
}
