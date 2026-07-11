/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_arp.c
 * Parity notes: the skip draw short-circuits on slot 0 (no draw), both in
 * the per-phrase mask and the per-bar rolls; the pool is sorted so Python's
 * set-of-pcs order is immaterial; the updown sequence is pool + reversed
 * interior (pool[-2:0:-1]); accented slots add 4 with no re-clamp.
 */

#include <string.h>

#include "music_arp.h"

AnoArpConfig ano_arp_config_default(void)
{
    return (AnoArpConfig){ .baseOctave = 5, .spanOctaves = 2, .velocityOffset = -16 };
}

uint32_t ano_arp_make_skips(AnoMusicRng *rng, AnoMeter meter, double density)
{
    int step = density > 0.65 ? 1 : 2;
    double neg = 1.0 - density;
    double skipProb = (neg > 0.0 ? neg : 0.0) * 0.35;
    uint32_t mask = 0;
    int slots = ano_meter_slots(meter);
    for (int s = 0; s < slots; s += step)
        if (s != 0 && ano_music_random(rng) < skipProb)
            mask |= 1u << s;
    return mask;
}

void ano_generate_arp(const AnoHarmonicContext *ctx, AnoMeter meter,
                      const AnoGenParams *params, AnoArpPattern pattern,
                      const AnoArpConfig *cfg, AnoMusicRng *rng,
                      bool hasSkips, uint32_t skips, AnoArpResult *out)
{
    *out = (AnoArpResult){ 0 };
    int lo = (cfg->baseOctave + 1) * 12;
    int hi = lo + cfg->spanOctaves * 12;
    int pool[16];
    uint32_t poolN = 0;
    for (uint32_t i = 0; i < ctx->chordPcCount; ++i) {
        bool dup = false;
        for (uint32_t j = 0; j < i; ++j)
            if (ctx->chordPcs[j] == ctx->chordPcs[i])
                dup = true;
        if (dup)
            continue;
        for (int p = lo + ((ctx->chordPcs[i] - lo) % 12 + 12) % 12;
             p <= hi && poolN < 16u; p += 12)
            pool[poolN++] = p;
    }
    // sorted() ascending
    for (uint32_t i = 1; i < poolN; ++i) {
        int key = pool[i];
        uint32_t j = i;
        while (j > 0 && pool[j - 1] > key) {
            pool[j] = pool[j - 1];
            --j;
        }
        pool[j] = key;
    }
    if (poolN == 0)
        return;

    int seq[32];
    uint32_t seqN = 0;
    if (pattern == ANO_ARP_DOWN) {
        for (uint32_t i = poolN; i-- > 0;)
            seq[seqN++] = pool[i];
    } else if (pattern == ANO_ARP_UPDOWN) {
        for (uint32_t i = 0; i < poolN; ++i)
            seq[seqN++] = pool[i];
        for (int i = (int)poolN - 2; i >= 1; --i) // pool[-2:0:-1]
            seq[seqN++] = pool[i];
    } else {
        for (uint32_t i = 0; i < poolN; ++i)
            seq[seqN++] = pool[i];
    }

    int step = params->noteDensity > 0.65 ? 1 : 2;
    double neg = 1.0 - params->noteDensity;
    double skipProb = (neg > 0.0 ? neg : 0.0) * 0.35;
    int velocity = params->velocityCenter + cfg->velocityOffset;
    velocity = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    int slots = ano_meter_slots(meter);

    uint32_t idx = 0;
    for (int slot = 0; slot < slots; slot += step) {
        bool skip = hasSkips ? (skips >> slot & 1u) != 0
                             : slot != 0 && ano_music_random(rng) < skipProb;
        if (skip) {
            idx += 1; // keep the traversal moving through rests
            continue;
        }
        int pitch = seq[idx % seqN];
        idx += 1;
        int accent = slot % 8 == 0 ? 4 : 0;
        AnoMusicEvent *e = &out->events[out->eventCount++];
        *e = (AnoMusicEvent){ 0 };
        e->core = (AnoNoteEvent){ barStart + slot * ANO_MUSIC_GRID,
                                  step * ANO_MUSIC_GRID, (uint8_t)pitch,
                                  (uint8_t)(velocity + accent),
                                  ANO_MUSIC_ARP, ANO_MUSIC_TIE_NONE };
        int deg = ano_scale_degree_of(ctx->scale, pitch);
        e->degree = deg > 0 ? (uint8_t)deg : 0;
        strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
        strncpy(e->role, ano_scale_contains(ctx->scale, pitch) ? "chord-tone" : "borrowed",
                sizeof e->role - 1);
    }
}
