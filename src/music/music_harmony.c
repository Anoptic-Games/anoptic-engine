/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Functional progression walk. Draw ORDER/COUNT are parity: weight lists keep dict insertion order;
// force_dominant and borrow gate short-circuit draws exactly as Python or/early-return.

#include "music_theory.h"

AnoHarmonyConfig ano_harmony_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoHarmonyConfig k = {
        .dominantTensionBias  = 1.6,
        .tonicCalmBias        = 1.2,
        .repeatPenalty        = 0.25,
        .borrowProbMax        = 0.35,
        .phraseOpenTonicBoost = 1.6,
        .tonicSuppress        = 0.05,
    };
    return k;
}

// FUNCTION_TRANSITIONS rows in the prototype's dict insertion order — the
// order feeds choices() cumulative sums, so it is load-bearing.
typedef struct FnRow { char fn[3]; double w[3]; } FnRow;
static const FnRow TRANS_T  = { { 'T', 'P', 'D' }, { 0.15, 0.55, 0.30 } };
static const FnRow TRANS_PD = { { 'P', 'D', 'T' }, { 0.15, 0.60, 0.25 } };
static const FnRow TRANS_D  = { { 'T', 'D', 'P' }, { 0.70, 0.20, 0.10 } };

// FUNCTION_CHORDS: per-function (degree, weight) lists, prototype order.
static const int    T_DEG[3] = { 1, 6, 3 };
static const double T_W[3]   = { 1.00, 0.35, 0.10 };
static const int    PD_DEG[2] = { 4, 2 };
static const double PD_W[2]   = { 1.00, 0.60 };
static const int    D_DEG[2] = { 5, 7 };
static const double D_W[2]   = { 1.00, 0.15 };

static const int CADENCE_TARGET[3]      = { 1, 5, 6 };   // authentic, half, deceptive
static const char PRE_CADENCE_FUNCTION[3] = { 'D', 'P', 'D' };

static int choose_degree(char function, int prevDegree, const AnoHarmonyConfig *cfg,
                         AnoMusicRng *rng, bool suppressTonic)
{
    const int    *deg;
    const double *base;
    uint32_t n;
    switch (function) {
    case 'T': deg = T_DEG; base = T_W; n = 3; break;
    case 'P': deg = PD_DEG; base = PD_W; n = 2; break;
    default:  deg = D_DEG; base = D_W; n = 2; break;
    }
    double w[3];
    for (uint32_t i = 0; i < n; ++i)
        w[i] = base[i] * (deg[i] == prevDegree ? cfg->repeatPenalty : 1.0);
    if (suppressTonic && function == 'T')
        for (uint32_t i = 0; i < n; ++i)
            if (deg[i] == 1)
                w[i] *= cfg->tonicSuppress;
    return deg[ano_music_choices1(rng, w, n)];
}

// aeolian borrowing gate: NO draw when the degree or mode disqualifies
static int8_t maybe_borrow(int degree, AnoMode mode, double valence,
                           const AnoHarmonyConfig *cfg, AnoMusicRng *rng)
{
    if (degree != 4 && degree != 6 && degree != 7)
        return ANO_MODE_NONE;
    if (ano_mode_brightness(mode) <= ano_mode_brightness(ANO_MODE_AEOLIAN))
        return ANO_MODE_NONE;
    double neg = -valence > 0.0 ? -valence : 0.0;
    if (ano_music_random(rng) < cfg->borrowProbMax * neg)
        return ANO_MODE_AEOLIAN;
    return ANO_MODE_NONE;
}

// the dissonance-budget tiers; draw counts vary per path, verbatim
static uint8_t choose_extensions(int degree, double tension, bool isCadentialDominant,
                                 AnoMusicRng *rng)
{
    if (tension < 0.25)
        return 0;
    if (tension < 0.5) {
        if (degree == 5 && isCadentialDominant)
            return ANO_EXT_7;
        return ano_music_random(rng) < 0.20 ? ANO_EXT_9 : 0;
    }
    if (tension < 0.75) {
        if (degree == 5)
            return ano_music_random(rng) < 0.25 ? (ANO_EXT_7 | ANO_EXT_9) : ANO_EXT_7;
        double r = ano_music_random(rng);
        if (r < 0.35) return ANO_EXT_7;
        if (r < 0.55) return ANO_EXT_9;
        if (r < 0.65) return ANO_EXT_SUS4;
        return 0;
    }
    if (degree == 5)
        return ANO_EXT_7 | ANO_EXT_9; // no draw
    double r = ano_music_random(rng);
    if (r < 0.50)
        return ano_music_random(rng) < 0.30 ? (ANO_EXT_7 | ANO_EXT_9) : ANO_EXT_7;
    if (r < 0.80)
        return ANO_EXT_9;
    return ANO_EXT_SUS4;
}

AnoChord ano_next_chord(AnoChord prev, AnoCadenceSlot slot, AnoCadencePolicy policy,
                        double tension, double valence, AnoMode mode,
                        bool phraseStart, bool pieceStart,
                        const AnoHarmonyConfig *cfg, AnoMusicRng *rng,
                        bool suppressTonic, int tonicize, bool forceDominant)
{
    if (pieceStart)
        return ano_chord(1, choose_extensions(1, tension, false, rng));

    if (slot == ANO_SLOT_CADENCE) {
        int degree = CADENCE_TARGET[policy];
        int8_t source = policy == ANO_CADENCE_DECEPTIVE
                      ? maybe_borrow(degree, mode, valence, cfg, rng) : ANO_MODE_NONE;
        AnoChord c = ano_chord(degree, choose_extensions(degree, tension, degree == 5, rng));
        c.sourceMode = source;
        return c;
    }

    if (slot == ANO_SLOT_PRE_CADENCE) {
        if (tonicize) // the applied dominant resolves at the cadence; no draws
            return ano_chord_applied_dominant(tonicize, true);
        char function = PRE_CADENCE_FUNCTION[policy];
        int degree;
        if (function == 'D')
            // Python `or` short-circuit: forceDominant skips the draw
            degree = (forceDominant || ano_music_random(rng) < 0.90) ? 5 : 7;
        else
            degree = choose_degree(function, prev.valid ? prev.degree : 0, cfg, rng, false);
        return ano_chord(degree, choose_extensions(degree, tension, degree == 5, rng));
    }

    char prevFunction = prev.valid ? ano_chord_function(prev) : 'T';
    FnRow row = prevFunction == 'T' ? TRANS_T : prevFunction == 'P' ? TRANS_PD : TRANS_D;
    for (int i = 0; i < 3; ++i) {
        if (row.fn[i] == 'D')
            row.w[i] *= 1.0 + (cfg->dominantTensionBias - 1.0) * tension;
        if (row.fn[i] == 'T') {
            row.w[i] *= 1.0 + (cfg->tonicCalmBias - 1.0) * (1.0 - tension);
            if (phraseStart)
                row.w[i] *= cfg->phraseOpenTonicBoost;
        }
    }
    char function = row.fn[ano_music_choices1(rng, row.w, 3)];
    int degree = choose_degree(function, prev.valid ? prev.degree : 0, cfg, rng, suppressTonic);
    int8_t source = maybe_borrow(degree, mode, valence, cfg, rng);
    AnoChord c = ano_chord(degree, choose_extensions(degree, tension, false, rng));
    c.sourceMode = source;
    return c;
}
