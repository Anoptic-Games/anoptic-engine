/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Collision retry list. Fixed candidate order; strict < replacement (first least-clashing wins; break on zero).
// Melody probe: first event sounding at onset whose role is not "doubling". Degrees from ctx->scale.

#include <string.h>

#include "music_imitation.h"

// 2nds, 7ths, tritone against the sounding melody.
static bool clash_interval(int ic)
{
    return ic == 1 || ic == 2 || ic == 6 || ic == 10 || ic == 11;
}

AnoMotif ano_imitation_cell(const AnoMotif *motif)
{
    uint32_t k = (motif->n + 1) / 2;
    if (k < 1)
        k = 1;
    AnoMotif cell = *motif;
    if (k < cell.n)
        cell.n = k;
    return cell;
}

// The cell entered `offset` slots later, truncated at the barline; false
// when nothing survives.
static bool shifted(const AnoMotif *cell, int offset, int slots, AnoMotif *out)
{
    *out = (AnoMotif){ 0 };
    out->shape = cell->shape;
    uint32_t n = 0;
    for (uint32_t i = 0; i < cell->n; ++i) {
        int s = cell->rhythm[i].slot + offset;
        if (s < slots) {
            int d = cell->rhythm[i].durSlots;
            if (d > slots - s)
                d = slots - s;
            out->rhythm[n] = (AnoRhythmNote){ s, d };
            out->contour[n] = cell->contour[n];
            n++;
        }
    }
    out->n = n;
    return n > 0;
}

static int collisions(const AnoPlacedNote *placed, uint32_t n,
                      const AnoMusicEvent *melody, uint32_t melodyCount,
                      double barStart)
{
    int hits = 0;
    for (uint32_t i = 0; i < n; ++i) {
        double t = barStart + placed[i].slot * ANO_MUSIC_GRID;
        const AnoMusicEvent *m = NULL;
        for (uint32_t j = 0; j < melodyCount; ++j) {
            const AnoMusicEvent *e = &melody[j];
            if (strcmp(e->role, "doubling") == 0)
                continue;
            double end = e->core.start + e->core.dur;
            if (e->core.start - 1e-9 <= t && t < end - 1e-9) {
                m = e;
                break; // next(...): first match
            }
        }
        if (m) {
            int a = placed[i].pitch, b = m->core.pitch;
            int loP = a < b ? a : b, hiP = a < b ? b : a;
            if (clash_interval(ano_interval_class(loP, hiP)))
                hits++;
        }
    }
    return hits;
}

void ano_generate_imitation(const AnoHarmonicContext *ctx, AnoMeter meter,
                            const AnoMotif *motif,
                            const AnoMusicEvent *melodyEvents, uint32_t melodyCount,
                            uint8_t hostLayer, int lo, int hi, int velocity,
                            AnoImitationResult *out)
{
    *out = (AnoImitationResult){ .candidateIdx = -1 };
    AnoMotif cell = ano_imitation_cell(motif);
    AnoScale mscale = ctx->chord.valid ? ano_chord_scale_for(ctx->chord, ctx->scale)
                                       : ctx->scale;
    int strongSlots[ANO_METER_MAX_SLOTS];
    uint32_t strongN = ano_meter_strong_slots(meter, strongSlots);
    uint32_t strongMask = 0;
    for (uint32_t i = 0; i < strongN; ++i)
        strongMask |= 1u << strongSlots[i];
    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    int slots = ano_meter_slots(meter);

    AnoMotif half;
    bool haveHalf = shifted(&cell, slots / 2, slots, &half);

    // fixed retry order: on the bar, +half-bar, up/down a 3rd, both combined
    AnoMotif candidates[6];
    bool live[6] = { true, haveHalf, true, true, haveHalf, haveHalf };
    candidates[0] = cell;
    if (haveHalf)
        candidates[1] = half;
    candidates[2] = ano_motif_sequence(&cell, 2);
    candidates[3] = ano_motif_sequence(&cell, -2);
    if (haveHalf) {
        candidates[4] = ano_motif_sequence(&half, 2);
        candidates[5] = ano_motif_sequence(&half, -2);
    }

    int bestN = -1, bestIdx = -1;
    AnoMotif bestVariant;
    AnoPlacedNote bestPlaced[ANO_MOTIF_MAX];
    uint32_t bestCount = 0;
    for (int c = 0; c < 6; ++c) {
        if (!live[c])
            continue;
        AnoPlacedNote placed[ANO_MOTIF_MAX];
        uint32_t n = ano_realize_faithful(&candidates[c], mscale,
                                          ctx->chordPcs, ctx->chordPcCount,
                                          lo, hi, strongMask, ANO_NEAR_NONE, placed);
        if (n == 0)
            continue;
        int hits = collisions(placed, n, melodyEvents, melodyCount, barStart);
        if (bestN < 0 || hits < bestN) {
            bestN = hits;
            bestIdx = c;
            bestVariant = candidates[c];
            memcpy(bestPlaced, placed, n * sizeof placed[0]);
            bestCount = n;
        }
        if (hits == 0)
            break;
    }
    if (bestIdx < 0)
        return; // no realizable entry

    int vel = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
    for (uint32_t i = 0; i < bestCount; ++i) {
        AnoMusicEvent *e = &out->events[i];
        e->core = (AnoNoteEvent){
            .start = barStart + bestPlaced[i].slot * ANO_MUSIC_GRID,
            .dur = bestPlaced[i].durSlots * ANO_MUSIC_GRID,
            .pitch = (uint8_t)bestPlaced[i].pitch,
            .velocity = (uint8_t)vel,
            .layer = hostLayer,
            .tie = ANO_MUSIC_TIE_NONE,
        };
        int deg = ano_scale_degree_of(ctx->scale, bestPlaced[i].pitch);
        e->degree = deg > 0 ? (uint8_t)deg : 0;
        strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
        strncpy(e->role, "imitation", sizeof e->role - 1);
    }
    out->eventCount = bestCount;
    out->emitted = bestVariant;
    if (bestCount < bestVariant.n)
        out->emitted.n = bestCount;
    out->hasEmitted = true;
    out->candidateIdx = bestIdx;
    out->clashes = bestN;
}
