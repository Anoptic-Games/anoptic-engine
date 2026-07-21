/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Rhythm + phrase structure. rough_cell: merge draw only if successor exists; split only if dur >= 2;
// drop never draws for first note 〜 a skipped draw shifts every later decision on the stream.
// effective_tension keeps float op order, including the (1.30 - 0.85) constant.

#include "music_gen.h"

uint32_t ano_euclid(int k, int n, int rotation, uint8_t out[ANO_RHYTHM_MAX])
{
    if (k < 0)
        k = 0;
    if (k > n)
        k = n;
    uint32_t count = 0;
    for (int i = 0; i < n && count < ANO_RHYTHM_MAX; ++i)
        if ((i * k) % n < k)
            out[count++] = (uint8_t)(((i + rotation) % n + n) % n);
    // sorted() ascending (insertion sort; rotation scrambles the order)
    for (uint32_t i = 1; i < count; ++i) {
        uint8_t key = out[i];
        uint32_t j = i;
        while (j > 0 && out[j - 1] > key) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return count;
}

uint32_t ano_rough_cell(AnoMusicRng *rng, double density, double roughness,
                        int slots, int baseStep, AnoRhythmNote out[ANO_RHYTHM_MAX])
{
    AnoRhythmNote notes[ANO_RHYTHM_MAX];
    uint32_t n = 0;
    for (int s = 0; s < slots && n < ANO_RHYTHM_MAX; s += baseStep)
        notes[n++] = (AnoRhythmNote){ s, baseStep };

    // merge adjacent pairs ~ roughness (syncopation across beat boundaries)
    AnoRhythmNote merged[ANO_RHYTHM_MAX];
    uint32_t m = 0;
    uint32_t i = 0;
    while (i < n) {
        if (i + 1 < n && ano_music_random(rng) < roughness * 0.6) {
            merged[m++] = (AnoRhythmNote){ notes[i].slot,
                                           notes[i].durSlots + notes[i + 1].durSlots };
            i += 2;
        } else {
            merged[m++] = notes[i];
            i += 1;
        }
    }

    // split long notes at high density
    AnoRhythmNote split[ANO_RHYTHM_MAX];
    uint32_t sp = 0;
    double negD = density - 0.6;
    double splitProb = (negD > 0.0 ? negD : 0.0) * 0.8;
    for (uint32_t j = 0; j < m && sp + 1 < ANO_RHYTHM_MAX; ++j) {
        int s = merged[j].slot;
        int d = merged[j].durSlots;
        if (d >= 2 && ano_music_random(rng) < splitProb) {
            split[sp++] = (AnoRhythmNote){ s, d / 2 };
            split[sp++] = (AnoRhythmNote){ s + d / 2, d - d / 2 };
        } else {
            split[sp++] = (AnoRhythmNote){ s, d };
        }
    }

    // drop notes at low density (rests are content); the first never draws
    double negK = 1.0 - density;
    double dropProb = (negK > 0.0 ? negK : 0.0) * 0.55;
    uint32_t kept = 0;
    for (uint32_t j = 0; j < sp; ++j)
        if (j == 0 || ano_music_random(rng) >= dropProb)
            out[kept++] = split[j];
    if (kept < 2) {
        kept = sp < 2 ? sp : 2; // kept = split[:2]
        for (uint32_t j = 0; j < kept; ++j)
            out[j] = split[j];
    }
    return kept;
}

static const double ARC4[4] = { 0.90, 1.00, 1.20, 0.75 };
static const double ARC8[8] = { 0.85, 0.90, 1.00, 1.05, 1.10, 1.20, 1.30, 0.75 };

AnoPhrasePos ano_phrase_position(int bar, int phraseBars)
{
    return (AnoPhrasePos){ .phrase = bar / phraseBars, .pos = bar % phraseBars,
                           .bars = phraseBars, .kind = ANO_SEG_REGULAR };
}

AnoCadenceSlot ano_phrase_slot(AnoPhrasePos p)
{
    if (p.pos == p.bars - 1)
        return ANO_SLOT_CADENCE;
    if (p.pos == p.bars - 2)
        return ANO_SLOT_PRE_CADENCE;
    if (p.pos == 0)
        return ANO_SLOT_OPEN;
    return ANO_SLOT_FREE;
}

static double clamp01(double x)
{
    // max(0.0, min(1.0, x))
    double m = x < 1.0 ? x : 1.0;
    return m > 0.0 ? m : 0.0;
}

double ano_effective_tension(double base, AnoPhrasePos p)
{
    if (p.kind == ANO_SEG_CODETTA)
        return clamp01(base * 0.7);
    double factor;
    if (p.bars == 4) {
        factor = ARC4[p.pos];
    } else if (p.bars == 8) {
        factor = ARC8[p.pos];
    } else if (p.bars <= 1) {
        factor = 1.0;
    } else if (p.pos == p.bars - 1) {
        factor = 0.75;
    } else {
        int peak = p.bars - 2 > 1 ? p.bars - 2 : 1;
        double frac = (double)p.pos / (double)peak;
        if (frac > 1.0)
            frac = 1.0; // min(1.0, pos / peak)
        factor = 0.85 + (1.30 - 0.85) * frac;
    }
    return clamp01(base * factor);
}

static const double HYPER_PROFILE[4] = { 1.0, 0.4, 0.7, 0.4 };

double ano_hyper_weight(int pos, int bars)
{
    double weight = HYPER_PROFILE[pos % 4];
    if (bars >= 8 && pos == bars / 2)
        weight = 0.85;
    return weight;
}
