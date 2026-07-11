/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_voicing.c
 * Minimum-movement voicing search (musicgen/theory/voicing.py). Candidates
 * enumerate in itertools.product order (rightmost voice's octave varies
 * fastest), are sorted ascending, deduped by first occurrence, filtered for
 * unisons and wide adjacent gaps, and the FIRST minimum of the float cost
 * wins — Python min() semantics, and float ties are real here (TECH_SPEC
 * §3.3's "sorting by float keys" hazard). Cost arithmetic keeps the
 * prototype's operation order.
 */

#include "music_theory.h"

AnoVoicingConfig ano_voicing_config_default(void)
{
    return (AnoVoicingConfig){
        .voices = 4, .lo = 52, .hi = 79,
        .maxAdjacentGap = 12, .center = 64.0, .maxVoiceMove = 7,
    };
}

#define MAX_VOICES 6
#define MAX_CANDS  256

typedef struct Cand { int p[MAX_VOICES]; } Cand;

// Candidate pc multisets in preference order (voice_pc_options): doubling
// root-then-fifth (never the third); dropping fifth-then-root; at most two.
static uint32_t pc_options(const uint8_t *pcs, uint32_t pcCount, uint32_t voices,
                           uint8_t out[2][MAX_VOICES])
{
    if (pcCount == voices) {
        for (uint32_t i = 0; i < voices; ++i)
            out[0][i] = pcs[i];
        return 1;
    }
    uint32_t count = 0;
    if (pcCount < voices) {
        static const uint32_t DOUBLE_IDX[3] = { 0, 2, 1 };
        for (int k = 0; k < 3 && count < 2u; ++k) {
            uint32_t di = DOUBLE_IDX[k];
            if (di >= pcCount)
                continue;
            for (uint32_t i = 0; i < pcCount; ++i)
                out[count][i] = pcs[i];
            for (uint32_t i = pcCount; i < voices; ++i)
                out[count][i] = pcs[di];
            count++;
        }
    } else {
        static const uint32_t DROP_IDX[3] = { 2, 0, 1 };
        for (int k = 0; k < 3 && count < 2u; ++k) {
            uint32_t n = 0;
            for (uint32_t i = 0; i < pcCount; ++i)
                if (i != DROP_IDX[k])
                    out[count][n++] = pcs[i];
            // trim extensions beyond capacity (list.pop from the end)
            if (n > voices)
                n = voices;
            if (n == voices)
                count++;
        }
    }
    if (count == 0) {
        for (uint32_t i = 0; i < voices; ++i)
            out[0][i] = pcs[i];
        return 1;
    }
    return count;
}

static double voicing_cost(const int *cand, uint32_t n, const int *prev, uint32_t prevLen,
                           const AnoVoicingConfig *cfg)
{
    if (prevLen == 0u || prevLen != n) {
        int sum = 0;
        for (uint32_t i = 0; i < n; ++i)
            sum += cand[i];
        double centering = (double)sum / (double)n - cfg->center;
        if (centering < 0.0) centering = -centering;
        return centering + 0.1 * (double)(cand[n - 1] - cand[0]);
    }
    int movement = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int d = prev[i] - cand[i];
        movement += d < 0 ? -d : d;
    }
    int dTop = cand[n - 1] - prev[n - 1];
    if (dTop < 0) dTop = -dTop;
    int topExcess = dTop - 2;
    double topSmoothness = (topExcess > 0 ? topExcess : 0) * 0.5;
    double perVoiceExcess = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        int d = prev[i] - cand[i];
        if (d < 0) d = -d;
        int over = d - cfg->maxVoiceMove;
        perVoiceExcess += (over > 0 ? over : 0) * 20.0;
    }
    return (double)movement + topSmoothness + perVoiceExcess;
}

uint32_t ano_voice_chord(const uint8_t *chordPcs, uint32_t pcCount,
                         const int *prev, uint32_t prevLen,
                         const AnoVoicingConfig *cfg, int out[6], double *outCost)
{
    AnoVoicingConfig def = ano_voicing_config_default();
    if (!cfg)
        cfg = &def;
    const uint32_t V = cfg->voices;

    uint8_t options[2][MAX_VOICES];
    uint32_t optCount = pc_options(chordPcs, pcCount, V, options);

    static Cand cands[MAX_CANDS]; // single-threaded conductor context
    uint32_t candCount = 0;

    for (uint32_t o = 0; o < optCount; ++o) {
        // per-voice octave option lists
        int opts[MAX_VOICES][4];
        uint32_t optN[MAX_VOICES];
        for (uint32_t v = 0; v < V; ++v) {
            int pc = options[o][v];
            int first = cfg->lo + ((pc - cfg->lo) % 12 + 12) % 12;
            uint32_t n = 0;
            for (int p = first; p <= cfg->hi && n < 4u; p += 12)
                opts[v][n++] = p;
            optN[v] = n;
        }
        // itertools.product: rightmost varies fastest
        uint32_t idx[MAX_VOICES] = {0};
        bool live = true;
        for (uint32_t v = 0; v < V; ++v)
            if (optN[v] == 0u)
                live = false;
        while (live && candCount < MAX_CANDS) {
            int combo[MAX_VOICES];
            for (uint32_t v = 0; v < V; ++v)
                combo[v] = opts[v][idx[v]];
            // sorted(combo) ascending (insertion sort, n <= 6)
            for (uint32_t i = 1; i < V; ++i) {
                int key = combo[i];
                uint32_t j = i;
                while (j > 0 && combo[j - 1] > key) {
                    combo[j] = combo[j - 1];
                    --j;
                }
                combo[j] = key;
            }
            // dedup by first occurrence, then the unison / gap filters
            bool dup = false;
            for (uint32_t c = 0; c < candCount && !dup; ++c) {
                dup = true;
                for (uint32_t v = 0; v < V; ++v)
                    if (cands[c].p[v] != combo[v]) { dup = false; break; }
            }
            if (!dup) {
                bool ok = true;
                for (uint32_t v = 1; v < V; ++v) {
                    int gap = combo[v] - combo[v - 1];
                    if (gap <= 0 || gap > (int)cfg->maxAdjacentGap) { ok = false; break; }
                }
                if (ok) {
                    for (uint32_t v = 0; v < V; ++v)
                        cands[candCount].p[v] = combo[v];
                    candCount++;
                }
            }
            // advance the product counter
            uint32_t v = V;
            while (v-- > 0) {
                if (++idx[v] < optN[v])
                    break;
                idx[v] = 0;
                if (v == 0u)
                    live = false;
            }
        }
    }
    if (candCount == 0u)
        return 0; // prototype raises; the conductor never feeds unplaceable pcs

    uint32_t best = 0;
    double bestCost = voicing_cost(cands[0].p, V, prev, prevLen, cfg);
    for (uint32_t c = 1; c < candCount; ++c) {
        double cost = voicing_cost(cands[c].p, V, prev, prevLen, cfg);
        if (cost < bestCost) { // strict <: first minimum wins, like min()
            bestCost = cost;
            best = c;
        }
    }
    for (uint32_t v = 0; v < V; ++v)
        out[v] = cands[best].p[v];
    if (outCost)
        *outCost = bestCost;
    return V;
}
