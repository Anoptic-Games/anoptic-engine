/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_melody.c
 * Parity notes: pc-candidate lists sort by the total order (|p - target|, p),
 * so Python's set-iteration order never matters; every draw gate reproduces
 * the prototype's `and` short-circuits; the guard's forbidden_direct calls
 * use max_step 2 (the prototype's keyword default); the anacrusis shortens
 * the held target with round(x, 10); doubling is interval arithmetic, never
 * scale walking.
 */

#include <string.h>

#include "music_melody.h"

#define GRID ANO_MUSIC_GRID

// ---------------------------------------------------------------------------
// pitch machinery
// ---------------------------------------------------------------------------

#define PC_CANDS_MAX 64

// All in-range instances of the pcs, nearest-to-target first (ties low).
// Falls back to [target] when the window holds no instance.
static uint32_t pc_candidates(const uint8_t *pcs, uint32_t pcCount, int target,
                              int lo, int hi, int out[PC_CANDS_MAX])
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < pcCount; ++i) {
        bool dup = false; // set(pcs): each pc contributes once
        for (uint32_t j = 0; j < i; ++j)
            if (pcs[j] == pcs[i])
                dup = true;
        if (dup)
            continue;
        for (int p = lo + ((pcs[i] - lo) % 12 + 12) % 12; p <= hi && n < PC_CANDS_MAX; p += 12)
            out[n++] = p;
    }
    // sort by (|p - target|, p) — total, so pre-sort order is immaterial
    for (uint32_t i = 1; i < n; ++i) {
        int key = out[i];
        int ka = key - target < 0 ? target - key : key - target;
        uint32_t j = i;
        while (j > 0) {
            int q = out[j - 1];
            int qa = q - target < 0 ? target - q : q - target;
            if (qa < ka || (qa == ka && q < key))
                break;
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    if (n == 0)
        out[n++] = target;
    return n;
}

static int nearest_pc_pitch(const uint8_t *pcs, uint32_t pcCount, int target,
                            int lo, int hi)
{
    int cands[PC_CANDS_MAX];
    pc_candidates(pcs, pcCount, target, lo, hi, cands);
    return cands[0];
}

static bool pc_in(int pitch, const uint8_t *pcs, uint32_t pcCount)
{
    int pc = (pitch % 12 + 12) % 12;
    for (uint32_t i = 0; i < pcCount; ++i)
        if (pcs[i] == pc)
            return true;
    return false;
}

static int clampi(int x, int lo, int hi)
{
    // Python min(max(x, lo), hi)
    int m = x > lo ? x : lo;
    return m < hi ? m : hi;
}

static int velocity_of(const AnoGenParams *params, int emphasis)
{
    int v = params->velocityCenter + emphasis;
    return v < 1 ? 1 : v > 127 ? 127 : v;
}

// ---------------------------------------------------------------------------
// the A3 outer-voice guard
// ---------------------------------------------------------------------------

typedef struct MelGuard
{
    const AnoMusicEvent *bass;
    uint32_t bassCount;
    double barStart, barLen;
    bool   hasPrev;
    double prevT;
    int    prevM, prevB;
    bool   hasPrev2; // the pair BEFORE the last observe
    double prev2T;
    int    prev2M, prev2B;
    int    prevRoot; // ANO_NEAR_NONE = None
} MelGuard;

static bool guard_bass_at(const MelGuard *g, double t, int *outPitch)
{
    bool have = false;
    for (uint32_t i = 0; i < g->bassCount; ++i) {
        double s = g->bass[i].core.start;
        if (s > t + 1e-9)
            break;
        if (t < s + g->bass[i].core.dur - 1e-9) {
            have = true;
            *outPitch = g->bass[i].core.pitch;
        }
    }
    return have;
}

static bool guard_pair(const MelGuard *g, double t, int *outM, int *outB)
{
    if (!g->hasPrev || t - g->prevT > g->barLen + 1e-9)
        return false;
    *outM = g->prevM;
    *outB = g->prevB;
    return true;
}

static int guard_pick(const MelGuard *g, const uint8_t *pcs, uint32_t pcCount,
                      int target, int lo, int hi, int slot)
{
    int cands[PC_CANDS_MAX];
    uint32_t n = pc_candidates(pcs, pcCount, target, lo, hi, cands);
    double t = g->barStart + slot * GRID;
    int bass, prevM, prevB;
    if (!guard_bass_at(g, t, &bass) || !guard_pair(g, t, &prevM, &prevB))
        return cands[0];
    for (uint32_t i = 0; i < n; ++i) {
        int p = cands[i];
        if (ano_forbidden_parallel(prevB, prevM, bass, p))
            continue;
        if (slot == 0 && ano_forbidden_direct(prevB, prevM, bass, p, 2))
            continue;
        return p;
    }
    return cands[0];
}

static void guard_observe(MelGuard *g, int slot, int pitch)
{
    double t = g->barStart + slot * GRID;
    int bass;
    if (guard_bass_at(g, t, &bass)) {
        g->hasPrev2 = g->hasPrev;
        g->prev2T = g->prevT;
        g->prev2M = g->prevM;
        g->prev2B = g->prevB;
        g->hasPrev = true;
        g->prevT = t;
        g->prevM = pitch;
        g->prevB = bass;
    }
}

static bool guard_clean_replacement(const MelGuard *g, int slot, int pitch)
{
    double t = g->barStart + slot * GRID;
    int bass;
    if (!guard_bass_at(g, t, &bass) || !g->hasPrev2 || t - g->prev2T > g->barLen + 1e-9)
        return true;
    return !(ano_forbidden_parallel(g->prev2B, g->prev2M, bass, pitch)
             || (slot == 0 && ano_forbidden_direct(g->prev2B, g->prev2M, bass, pitch, 2)));
}

static bool guard_recovery_collides(const MelGuard *g, int slot, int recPitch)
{
    double t = g->barStart + slot * GRID;
    int bass, prevM, prevB;
    if (!guard_bass_at(g, t, &bass) || !guard_pair(g, t, &prevM, &prevB))
        return false;
    return ano_forbidden_parallel(prevB, prevM, bass, recPitch)
        || (slot == 0 && ano_forbidden_direct(prevB, prevM, bass, recPitch, 2));
}

// ---------------------------------------------------------------------------
// constraint-first placement
// ---------------------------------------------------------------------------

static int place_snap(const MelGuard *guard, uint32_t strongMask,
                      const uint8_t *pcs, uint32_t pcCount,
                      int target, int lo, int hi, int slot)
{
    if (guard && (strongMask >> slot & 1u))
        return guard_pick(guard, pcs, pcCount, target, lo, hi, slot);
    return nearest_pc_pitch(pcs, pcCount, target, lo, hi);
}

static uint32_t mel_place(const AnoMotif *cell, const AnoHarmonicContext *ctx,
                          AnoScale mscale, const AnoGenParams *params,
                          const AnoMelodyState *state, int lo, int hi,
                          uint32_t strongMask, int peak, MelGuard *guard,
                          int pinFirst, AnoPlacedNote out[], int *outAnchor)
{
    int anchorTarget = state->prevPitch != ANO_NEAR_NONE ? state->prevPitch
                                                         : params->registerCenter;
    anchorTarget = clampi(anchorTarget, lo + 3, hi - 3);
    int anchor = nearest_pc_pitch(ctx->chordPcs, ctx->chordPcCount, anchorTarget, lo, hi);
    int prev = state->prevPitch;
    int recovery = 0;
    int lastIndex = (int)cell->n - 1;
    int peakI = -1;
    if (peak != ANO_NEAR_NONE && cell->n > 1) {
        peakI = 0;
        for (uint32_t i = 1; i < cell->n; ++i)
            if (cell->contour[i] > cell->contour[peakI])
                peakI = (int)i; // first max
        if (peakI == lastIndex)
            peakI = -1; // the final-note contraction would undo it
    }

    for (int i = 0; i < (int)cell->n; ++i) {
        int slot = cell->rhythm[i].slot;
        int offset = cell->contour[i];
        int pitch;
        if (i == 0 && slot == 0 && pinFirst != ANO_NEAR_NONE) {
            pitch = pinFirst; // the tied continuation — given, not chosen
        } else if (i == peakI && !(recovery && prev != ANO_NEAR_NONE)) {
            pitch = place_snap(guard, strongMask, ctx->chordPcs, ctx->chordPcCount,
                               peak, lo, hi, slot);
        } else if (recovery && prev != ANO_NEAR_NONE) {
            // a leap resolves by an opposite step; from a chromatic pitch a
            // true semitone step, never a snapped diatonic overshoot
            pitch = ano_diatonic_shift(mscale, prev, recovery);
            for (int k = 1; k <= 2; ++k) {
                int p = prev + recovery * k;
                if (ano_scale_contains(mscale, p) || pc_in(p, ctx->chordPcs, ctx->chordPcCount)) {
                    pitch = p;
                    break;
                }
            }
        } else {
            int target = ano_diatonic_shift(mscale, anchor, offset);
            if (strongMask >> slot & 1u) {
                pitch = place_snap(guard, strongMask, ctx->chordPcs, ctx->chordPcCount,
                                   target, lo, hi, slot);
            } else {
                pitch = ano_snap_to_scale(mscale, clampi(target, lo, hi));
                int d = prev != ANO_NEAR_NONE ? pitch - prev : 0;
                if (prev != ANO_NEAR_NONE && (d < 0 ? -d : d) > 5) {
                    bool stepInstead = !pc_in(pitch, ctx->chordPcs, ctx->chordPcCount)
                                    || (guard && (d < 0 ? -d : d) > 9);
                    if (!stepInstead && guard && i + 1 < (int)cell->n) {
                        int nslot = cell->rhythm[i + 1].slot;
                        int rec = ano_diatonic_shift(mscale, pitch, pitch > prev ? -1 : 1);
                        stepInstead = (strongMask >> nslot & 1u)
                                   && guard_recovery_collides(guard, nslot, rec);
                    }
                    if (stepInstead)
                        pitch = ano_diatonic_shift(mscale, prev, pitch > prev ? 1 : -1);
                }
            }
        }
        if (guard && !(lo <= pitch && pitch <= hi)) {
            // re-enter by stepping to the window's nearest scale tone; an
            // octave fold would manufacture an unseen plunge
            int step = pitch > hi ? -1 : 1;
            pitch = pitch > hi ? hi : lo;
            while (!ano_scale_contains(mscale, pitch))
                pitch += step;
        }
        while (pitch > hi)
            pitch -= 12;
        while (pitch < lo)
            pitch += 12;
        if (i == lastIndex && prev != ANO_NEAR_NONE) {
            int d = pitch - prev;
            if ((d < 0 ? -d : d) > 5) {
                // bars never end mid-leap: contract to a step that holds the frame
                int contracted = ano_diatonic_shift(mscale, prev, pitch > prev ? 1 : -1);
                if (guard && (strongMask >> slot & 1u)
                    && guard_recovery_collides(guard, slot, contracted)) {
                    int other = ano_diatonic_shift(mscale, prev, pitch > prev ? -1 : 1);
                    if (!guard_recovery_collides(guard, slot, other))
                        contracted = other;
                }
                pitch = contracted;
            }
        }
        int interval = prev == ANO_NEAR_NONE ? 0 : pitch - prev;
        recovery = (interval < 0 ? -interval : interval) <= 5 ? 0
                 : interval > 0 ? -1 : 1;
        out[i] = (AnoPlacedNote){ slot, cell->rhythm[i].durSlots, pitch };
        if (guard && (strongMask >> slot & 1u))
            guard_observe(guard, slot, pitch);
        prev = pitch;
    }
    *outAnchor = anchor;
    return cell->n;
}

// Fragmentary introduction: the first half, its tail nudged to a hanging
// 2^/7^ — standing down where the nudge would leap or break the frame.
static uint32_t mel_introduce(const AnoMotif *motif, const AnoHarmonicContext *ctx,
                              AnoScale mscale, const AnoGenParams *params,
                              const AnoMelodyState *state, int lo, int hi,
                              uint32_t strongMask, MelGuard *guard, int pinFirst,
                              AnoPlacedNote out[], int *outAnchor)
{
    uint32_t k = (motif->n + 1) / 2;
    if (k < 1)
        k = 1;
    AnoMotif frag = *motif;
    if (k < frag.n)
        frag.n = k;
    uint32_t n = mel_place(&frag, ctx, mscale, params, state, lo, hi, strongMask,
                           ANO_NEAR_NONE, guard, pinFirst, out, outAnchor);
    if (n == 1 && out[0].slot == 0 && pinFirst != ANO_NEAR_NONE)
        return n; // the tied continuation is not nudgeable
    if (n) {
        int slot = out[n - 1].slot;
        int last = out[n - 1].pitch;
        int prev = n > 1 ? out[n - 2].pitch
                 : state->prevPitch != ANO_NEAR_NONE ? state->prevPitch : last;
        int prev2 = n > 2 ? out[n - 3].pitch : state->prevPitch;
        uint8_t unstable[2] = {
            (uint8_t)(ano_scale_pitch_at(ctx->scale, 2, 4) % 12),
            (uint8_t)(ano_scale_pitch_at(ctx->scale, 7, 4) % 12),
        };
        int cands[PC_CANDS_MAX];
        uint32_t cn = 0;
        for (int u = 0; u < 2; ++u) {
            if (u == 1 && unstable[1] == unstable[0])
                break; // set() dedupe
            for (int p = lo + ((unstable[u] - lo) % 12 + 12) % 12;
                 p <= hi && cn < PC_CANDS_MAX; p += 12)
                cands[cn++] = p;
        }
        int pool[PC_CANDS_MAX];
        uint32_t pn = 0;
        if (prev2 != ANO_NEAR_NONE && (prev - prev2 < 0 ? prev2 - prev : prev - prev2) > 5) {
            int direction = prev > prev2 ? -1 : 1;
            for (uint32_t c = 0; c < cn; ++c) {
                int v = (cands[c] - prev) * direction;
                if (1 <= v && v <= 2)
                    pool[pn++] = cands[c];
            }
        } else {
            for (uint32_t c = 0; c < cn; ++c) {
                int d = cands[c] - prev;
                if ((d < 0 ? -d : d) <= 5)
                    pool[pn++] = cands[c];
            }
            if (pn == 0 && !guard) // unguarded, the tail may reach any instance
                for (uint32_t c = 0; c < cn; ++c)
                    pool[pn++] = cands[c];
        }
        if (pn && guard && (strongMask >> slot & 1u)) {
            uint32_t m = 0;
            for (uint32_t c = 0; c < pn; ++c)
                if (guard_clean_replacement(guard, slot, pool[c]))
                    pool[m++] = pool[c];
            pn = m;
        }
        if (pn) {
            // min by (|p - last|, p)
            int best = pool[0];
            int bestAbs = best - last < 0 ? last - best : best - last;
            for (uint32_t c = 1; c < pn; ++c) {
                int a = pool[c] - last < 0 ? last - pool[c] : pool[c] - last;
                if (a < bestAbs || (a == bestAbs && pool[c] < best)) {
                    best = pool[c];
                    bestAbs = a;
                }
            }
            out[n - 1].pitch = best;
            if (guard && (strongMask >> slot & 1u))
                guard_observe(guard, slot, best); // replaces the observed pick
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// event assembly helpers
// ---------------------------------------------------------------------------

static void mel_event(AnoMusicEvent *e, const AnoHarmonicContext *ctx,
                      double t, double d, int p, int velocity, const char *role,
                      uint8_t tie)
{
    *e = (AnoMusicEvent){ 0 };
    e->core = (AnoNoteEvent){ t, d, (uint8_t)p, (uint8_t)velocity,
                              ANO_MUSIC_MELODY, tie };
    int deg = ano_scale_degree_of(ctx->scale, p);
    e->degree = deg > 0 ? (uint8_t)deg : 0;
    strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
    strncpy(e->role, role, sizeof e->role - 1);
}

// D1 pickups on the cadence bar; mutates events/newState/guard in place.
static bool mel_anacrusis(AnoMusicEvent *events, uint32_t *eventCount,
                          AnoMelodyState *newState, const AnoHarmonicContext *ctx,
                          AnoMeter meter, const AnoGenParams *params, int lo, int hi,
                          AnoMusicRng *rng, MelGuard *guard, uint32_t strongMask)
{
    if (*eventCount == 0 || ctx->modulation[0] || !ctx->nextChord.valid)
        return false;
    if (ano_music_random(rng) >= 0.2 + 0.6 * params->noteDensity)
        return false;
    static const double NW[3] = { 0.4, 0.4, 0.2 };
    int n = 1 + (int)ano_music_choices1(rng, NW, 3);
    double eighth = 2 * GRID;
    double barQ = ano_meter_bar_quarters(meter);
    double barEnd = ctx->bar * barQ + barQ;
    AnoMusicEvent *target = &events[*eventCount - 1];
    while (n && barEnd - n * eighth < target->core.start + 1.0 - 1e-9)
        n -= 1;
    if (!n)
        return false;
    uint8_t nextPcs[5];
    ano_chord_pitch_classes(ctx->nextChord, ctx->scale, nextPcs);
    uint8_t goalPc = nextPcs[1]; // the 3rd: the sweet imperfect consonance
    int land = nearest_pc_pitch(&goalPc, 1, target->core.pitch, lo, hi);
    int d = land >= (int)target->core.pitch ? 1 : -1;
    int run[3];
    for (int i = 1; i < n; ++i)
        run[i - 1] = ano_diatonic_shift(ctx->scale, land, -d * (n - i));
    run[n - 1] = land;
    for (int i = 0; i < n; ++i)
        if (!(lo <= run[i] && run[i] <= hi))
            return false;
    {
        int e0 = run[0] - (int)target->core.pitch;
        if ((e0 < 0 ? -e0 : e0) > 4)
            return false; // no room to connect stepwise
    }
    if (guard) {
        int slots = ano_meter_slots(meter);
        for (int i = 0; i < n; ++i) {
            int slot = slots - 2 * (n - i);
            if ((strongMask >> slot & 1u) && guard_recovery_collides(guard, slot, run[i]))
                return false;
        }
    }
    target->core.dur = ano_music_round(barEnd - n * eighth - target->core.start, 10);
    for (int i = 0; i < n; ++i) {
        double start = barEnd - (n - i) * eighth;
        mel_event(&events[(*eventCount)++], ctx, start, eighth, run[i],
                  velocity_of(params, -8), "pickup",
                  i == n - 1 ? ANO_MUSIC_TIE_OUT : ANO_MUSIC_TIE_NONE);
        if (guard) {
            int slot = ano_meter_slot_of(meter, start);
            if (strongMask >> slot & 1u)
                guard_observe(guard, slot, run[i]);
        }
    }
    newState->prevPitch = land;
    newState->pendingTie = land;
    if (guard) {
        newState->hasPrevOuter = guard->hasPrev;
        newState->outerT = guard->prevT;
        newState->outerMelody = guard->prevM;
        newState->outerBass = guard->prevB;
    }
    return true;
}

// C1 parallel doubling: a diatonic 3rd below, a 6th where the 3rd is not a
// chord tone on a strong slot; unfit notes go undoubled.
static uint32_t mel_double_line(const AnoMusicEvent *events, uint32_t count,
                                const AnoHarmonicContext *ctx, AnoMeter meter,
                                uint32_t strongMask, AnoMusicEvent *out)
{
    AnoScale mscale = ctx->chord.valid ? ano_chord_scale_for(ctx->chord, ctx->scale)
                                       : ctx->scale;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const AnoMusicEvent *e = &events[i];
        int p = e->core.pitch;
        int thirds[2] = { p - 3, p - 4 };
        int sixths[2] = { p - 8, p - 9 };
        int third = INT_MIN, sixth = INT_MIN;
        for (int k = 0; k < 2; ++k)
            if (third == INT_MIN && ano_scale_contains(mscale, thirds[k]))
                third = thirds[k];
        for (int k = 0; k < 2; ++k)
            if (sixth == INT_MIN && ano_scale_contains(mscale, sixths[k]))
                sixth = sixths[k];
        int pitch = INT_MIN;
        if (strongMask >> ano_meter_slot_of(meter, e->core.start) & 1u) {
            int pair[2] = { third, sixth };
            for (int k = 0; k < 2 && pitch == INT_MIN; ++k)
                if (pair[k] != INT_MIN && pc_in(pair[k], ctx->chordPcs, ctx->chordPcCount))
                    pitch = pair[k];
            if (pitch == INT_MIN) { // chromatic chords: any chord-member 3rd/6th
                int all[4] = { thirds[0], thirds[1], sixths[0], sixths[1] };
                for (int k = 0; k < 4 && pitch == INT_MIN; ++k)
                    if (pc_in(all[k], ctx->chordPcs, ctx->chordPcCount))
                        pitch = all[k];
            }
        } else {
            pitch = third;
            if (pitch == INT_MIN)
                for (int k = 0; k < 2 && pitch == INT_MIN; ++k)
                    if (pc_in(thirds[k], ctx->chordPcs, ctx->chordPcCount))
                        pitch = thirds[k];
        }
        if (pitch == INT_MIN)
            continue;
        int vel = e->core.velocity - 8;
        if (vel < 1)
            vel = 1;
        mel_event(&out[n++], ctx, e->core.start, e->core.dur, pitch, vel,
                  "doubling", ANO_MUSIC_TIE_NONE);
    }
    return n;
}

// ---------------------------------------------------------------------------
// cadence formulas
// ---------------------------------------------------------------------------

// Policy-appropriate cadence-target pcs, filtered to chord members.
static uint32_t cadence_target_pcs(const AnoHarmonicContext *ctx, uint8_t out[5])
{
    static const int DEGS[3][2] = { { 1, 3 }, { 2, 5 }, { 1, 3 } }; // auth/half/dec
    const int *degs = DEGS[ctx->cadencePolicy];
    uint32_t n = 0;
    for (int i = 0; i < 2; ++i) {
        uint8_t pc = (uint8_t)(ano_scale_pitch_at(ctx->scale, degs[i], 4) % 12);
        if (pc_in(pc, ctx->chordPcs, ctx->chordPcCount))
            out[n++] = pc;
    }
    if (n == 0)
        for (uint32_t i = 0; i < ctx->chordPcCount; ++i)
            out[n++] = ctx->chordPcs[i];
    return n;
}

static void mel_cadence_statement(const AnoMotif *motif, const AnoHarmonicContext *ctx,
                                  AnoMeter meter, const AnoGenParams *params,
                                  const AnoMelodyState *state, int lo, int hi,
                                  AnoMelodyResult *out)
{
    uint8_t targets[5];
    uint32_t tn = cadence_target_pcs(ctx, targets);
    AnoPlacedNote placed[ANO_MOTIF_MAX];
    uint32_t n = ano_realize_cadential(motif, ctx->scale, targets, tn, lo, hi,
                                       state->prevPitch, ano_meter_slots(meter), placed);
    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    for (uint32_t i = 0; i < n; ++i) {
        int emphasis = n > 1
            ? -2 + (int)ano_music_round_int(8.0 * i / (double)(n - 1)) : 6;
        mel_event(&out->events[out->eventCount++], ctx,
                  barStart + placed[i].slot * GRID, placed[i].durSlots * GRID,
                  placed[i].pitch, velocity_of(params, emphasis), "motif",
                  ANO_MUSIC_TIE_NONE);
    }
    int target = placed[n - 1].pitch;
    out->state = *state;
    out->state.prevPitch = target;
    out->state.prevAnchor = target;
    out->state.pendingTie = ANO_NEAR_NONE;
}

static void mel_cadence_bar(const AnoHarmonicContext *ctx, AnoMeter meter,
                            const AnoGenParams *params, const AnoMelodyState *state,
                            int lo, int hi, MelGuard *guard, uint32_t strongMask,
                            AnoMelodyResult *out)
{
    AnoScale scale = ctx->scale;
    uint8_t targets[5];
    uint32_t tn = cadence_target_pcs(ctx, targets);
    int center = state->prevPitch != ANO_NEAR_NONE ? state->prevPitch
                                                   : params->registerCenter;
    if (guard)
        center = clampi(center, lo, hi); // re-enter a moved window at its edge

    int provisional = nearest_pc_pitch(targets, tn, center, lo, hi);
    int direction = provisional > center ? 1 : -1;
    if (guard && state->prevPitch != ANO_NEAR_NONE) {
        int bassNow;
        bool haveBass = guard_bass_at(guard, guard->barStart, &bassNow);
        int bPrev = guard->prevRoot;
        if (bPrev == ANO_NEAR_NONE) { // direct callers without the conductor
            int pm, pb;
            bPrev = guard_pair(guard, guard->barStart, &pm, &pb) ? pb : ANO_NEAR_NONE;
        }
        if (haveBass && bPrev != ANO_NEAR_NONE && bassNow != bPrev)
            direction = bassNow > bPrev ? -1 : 1; // contrary to the root arrival
    }
    int first = state->prevPitch != ANO_NEAR_NONE
              ? ano_diatonic_shift(scale, center, direction)
              : ano_diatonic_shift(scale, provisional, -direction);
    first = clampi(first, lo, hi);
    if (guard) {
        if (!ano_scale_contains(scale, first)) { // the clamp may land off-scale
            first = ano_snap_to_scale(scale, first);
            if (first > hi)
                first = ano_diatonic_shift(scale, first, -1);
        }
        int bassNow, pm, pb;
        if (guard_bass_at(guard, guard->barStart, &bassNow)
            && guard_pair(guard, guard->barStart, &pm, &pb)) {
            bool clean = !(ano_forbidden_parallel(pb, pm, bassNow, first)
                           || ano_forbidden_direct(pb, pm, bassNow, first, 2));
            if (!clean) { // nearest scale tone that opens cleanly
                int bestFound = first;
                int bestAbs = -1;
                for (int span = 0; span <= hi - lo && bestAbs < 0; ++span)
                    for (int sgn = -1; sgn <= 1 && bestAbs < 0; sgn += 2) {
                        int p = first + sgn * span; // -span before +span: ties low
                        if (sgn == 1 && span == 0)
                            continue;
                        if (p < lo || p > hi || !ano_scale_contains(scale, p))
                            continue;
                        if (!(ano_forbidden_parallel(pb, pm, bassNow, p)
                              || ano_forbidden_direct(pb, pm, bassNow, p, 2))) {
                            bestFound = p;
                            bestAbs = span;
                        }
                    }
                first = bestFound;
            }
        }
    }
    int target = nearest_pc_pitch(targets, tn, first, lo, hi);
    if (guard && state->prevPitch != ANO_NEAR_NONE) {
        int e0 = first - state->prevPitch;
        if ((e0 < 0 ? -e0 : e0) > 5) {
            // entry leap: run toward a target on the opposite side
            int entry = first > state->prevPitch ? 1 : -1;
            int cands[PC_CANDS_MAX];
            uint32_t cn = pc_candidates(targets, tn, first, lo, hi, cands);
            for (uint32_t c = 0; c < cn; ++c)
                if ((cands[c] - first) * entry < 0) {
                    target = cands[c];
                    break;
                }
        }
    }
    if (target == first)
        target = nearest_pc_pitch(targets, tn, first + direction * 2, lo, hi);

    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    double eighth = 2 * GRID, quarter = 4 * GRID;

    int run[4];
    int runLen = 0;
    int p = first;
    while ((target - p < 0 ? p - target : target - p) > 2 && runLen < 4) {
        p = ano_diatonic_shift(scale, p, target > p ? 1 : -1);
        if (p == target)
            break;
        run[runLen++] = p;
    }

    double targetStart;
    // role_of: chord-tone / appoggiatura / borrowed
    #define ROLE_OF(px) (pc_in((px), ctx->chordPcs, ctx->chordPcCount) ? "chord-tone" \
                         : ano_scale_contains(scale, (px)) ? "appoggiatura" : "borrowed")
    if (runLen) {
        mel_event(&out->events[out->eventCount++], ctx, barStart, eighth, first,
                  velocity_of(params, -2), ROLE_OF(first), ANO_MUSIC_TIE_NONE);
        for (int i = 0; i < runLen; ++i) {
            const char *role = ROLE_OF(run[i]);
            if (strcmp(role, "appoggiatura") == 0)
                role = "passing";
            mel_event(&out->events[out->eventCount++], ctx,
                      barStart + (i + 1) * eighth, eighth, run[i],
                      velocity_of(params, -6), role, ANO_MUSIC_TIE_NONE);
        }
        targetStart = barStart + (runLen + 1) * eighth;
    } else {
        mel_event(&out->events[out->eventCount++], ctx, barStart, quarter, first,
                  velocity_of(params, -2), ROLE_OF(first), ANO_MUSIC_TIE_NONE);
        targetStart = barStart + quarter;
    }
    double barQ = ano_meter_bar_quarters(meter);
    mel_event(&out->events[out->eventCount++], ctx, targetStart,
              ctx->bar * barQ + barQ - targetStart, target,
              velocity_of(params, 6), ROLE_OF(target), ANO_MUSIC_TIE_NONE);
    #undef ROLE_OF

    if (guard)
        for (uint32_t i = 0; i < out->eventCount; ++i) {
            int slot = ano_meter_slot_of(meter, out->events[i].core.start);
            if (strongMask >> slot & 1u)
                guard_observe(guard, slot, out->events[i].core.pitch);
        }
    out->state = *state;
    out->state.prevPitch = target;
    out->state.prevAnchor = target;
    out->state.pendingTie = ANO_NEAR_NONE;
    if (guard) {
        out->state.hasPrevOuter = guard->hasPrev;
        out->state.outerT = guard->prevT;
        out->state.outerMelody = guard->prevM;
        out->state.outerBass = guard->prevB;
    }
}

// ---------------------------------------------------------------------------
// the bar dispatcher
// ---------------------------------------------------------------------------

void ano_generate_melody(const AnoHarmonicContext *ctx, AnoMeter meter,
                         const AnoGenParams *params, AnoPhrasePos pos,
                         const AnoMotif *motif, const AnoMelodyState *state,
                         const AnoMelodyConfig *cfg, AnoMusicRng *rng,
                         AnoMelodyStage lifecycle, const AnoMotif *signature,
                         const AnoApexPlan *apex,
                         const AnoMusicEvent *bass, uint32_t bassCount,
                         const AnoPlacedNote *replay, uint32_t replayCount,
                         bool doubleLine, int prevBass,
                         AnoMusicRng *anacrusisRng, AnoMusicRng *syncopateRng,
                         AnoMelodyResult *out)
{
    *out = (AnoMelodyResult){ 0 };
    out->state = *state;
    int lo = params->registerCenter - cfg->rangeSemitones;
    int hi = params->registerCenter + cfg->rangeSemitones;
    if (lifecycle == ANO_MEL_COMPLETED)
        apex = NULL; // the cadence-fused statement owns the phrase's shape
    bool apexBar = apex && pos.pos == apex->pos;
    if (apex) {
        int cap = apex->pitch - (apexBar ? 0 : 1);
        int h = hi < cap ? hi : cap;
        hi = h > lo + 6 ? h : lo + 6;
    }
    const AnoMotif *sig = signature ? signature : motif;
    bool sigEvent = (lifecycle == ANO_MEL_INTRODUCED || lifecycle == ANO_MEL_DEVELOPED
                     || lifecycle == ANO_MEL_STATED)
                 && pos.pos == pos.bars / 2;

    uint32_t strongMask = 0;
    {
        int ss[ANO_METER_MAX_SLOTS];
        uint32_t sn = ano_meter_strong_slots(meter, ss);
        for (uint32_t i = 0; i < sn; ++i)
            strongMask |= 1u << ss[i];
    }
    double barQ = ano_meter_bar_quarters(meter);

    MelGuard guardStore, *guard = NULL;
    if (cfg->counterpoint && bass && bassCount > 0) {
        guardStore = (MelGuard){
            .bass = bass, .bassCount = bassCount,
            .barStart = ctx->bar * barQ, .barLen = barQ,
            .hasPrev = state->hasPrevOuter,
            .prevT = state->outerT, .prevM = state->outerMelody, .prevB = state->outerBass,
            .prevRoot = prevBass,
        };
        guard = &guardStore;
    }

    if (ano_phrase_slot(pos) == ANO_SLOT_CADENCE && ctx->cadencePolicy != ANO_CADENCE_NONE) {
        if (lifecycle == ANO_MEL_COMPLETED)
            mel_cadence_statement(sig, ctx, meter, params, state, lo, hi, out);
        else
            mel_cadence_bar(ctx, meter, params, state, lo, hi, guard, strongMask, out);
        if (anacrusisRng && lifecycle != ANO_MEL_COMPLETED)
            mel_anacrusis(out->events, &out->eventCount, &out->state, ctx, meter,
                          params, lo, hi, anacrusisRng, guard, strongMask);
        if (doubleLine && out->eventCount) {
            AnoMusicEvent dbl[ANO_MELODY_MAX_EVENTS];
            uint32_t dn = mel_double_line(out->events, out->eventCount, ctx, meter,
                                          strongMask, dbl);
            for (uint32_t i = 0; i < dn; ++i)
                out->events[out->eventCount++] = dbl[i];
        }
        return;
    }

    double restProb = cfg->barRestMax - params->noteDensity * 0.4;
    if (restProb < 0.0)
        restProb = 0.0;
    if (lifecycle != ANO_MEL_COMPLETED && !sigEvent && !apexBar
        && ano_phrase_slot(pos) == ANO_SLOT_FREE
        && ano_music_random(rng) < restProb) {
        // a pending tie dissolves here (orphan "out" = plain note)
        out->state = (AnoMelodyState){
            .prevPitch = state->prevPitch, .prevAnchor = state->prevAnchor,
            .hasPrevOuter = state->hasPrevOuter, .outerT = state->outerT,
            .outerMelody = state->outerMelody, .outerBass = state->outerBass,
            .pendingTie = ANO_NEAR_NONE,
        };
        return;
    }

    AnoScale mscale = ctx->chord.valid ? ano_chord_scale_for(ctx->chord, ctx->scale)
                                       : ctx->scale;
    bool faithful = false;

    // B2 period answer: the consequent replays the antecedent's bar verbatim,
    // standing down if the window moved or the frame would break.
    AnoPlacedNote placed[ANO_MOTIF_MAX];
    uint32_t placedN = 0;
    int anchor = 0;
    bool replayed = false;
    if (replayCount > 0 && lifecycle != ANO_MEL_COMPLETED) {
        bool ok = true;
        for (uint32_t i = 0; i < replayCount; ++i)
            if (!(lo <= replay[i].pitch && replay[i].pitch <= hi))
                ok = false;
        if (ok && state->prevPitch != ANO_NEAR_NONE) {
            int entryIv = replay[0].pitch - state->prevPitch;
            if ((entryIv < 0 ? -entryIv : entryIv) > 5) {
                int back = replayCount > 1 ? replay[1].pitch - replay[0].pitch : 0;
                ok = back != 0 && (back > 0) != (entryIv > 0)
                  && (back < 0 ? -back : back) <= 2;
            }
        }
        if (ok && guard) {
            // walk the frame across the WHOLE replay over the fresh bass
            bool   hasPair = guard->hasPrev;
            double pairT = guard->prevT;
            int    pairM = guard->prevM, pairB = guard->prevB;
            for (uint32_t i = 0; i < replayCount; ++i) {
                int s = replay[i].slot;
                if (!(strongMask >> s & 1u))
                    continue;
                double t = ctx->bar * barQ + s * GRID;
                int bassNow;
                if (!guard_bass_at(guard, t, &bassNow))
                    continue;
                if (hasPair && t - pairT <= barQ + 1e-9) {
                    if (ano_forbidden_parallel(pairB, pairM, bassNow, replay[i].pitch)
                        || (s == 0 && ano_forbidden_direct(pairB, pairM, bassNow,
                                                           replay[i].pitch, 2))) {
                        ok = false;
                        break;
                    }
                }
                hasPair = true;
                pairT = t;
                pairM = replay[i].pitch;
                pairB = bassNow;
            }
        }
        if (ok) {
            for (uint32_t i = 0; i < replayCount; ++i)
                placed[i] = replay[i];
            placedN = replayCount;
            anchor = replay[0].pitch;
            replayed = true;
            if (guard)
                for (uint32_t i = 0; i < placedN; ++i)
                    if (strongMask >> placed[i].slot & 1u)
                        guard_observe(guard, placed[i].slot, placed[i].pitch);
        }
    }

    // D1: host a pending tie by pinning the downbeat pick; a pin that would
    // land afterbeat perfects against the last strong pair stands down.
    int pin0 = ANO_NEAR_NONE;
    if (state->pendingTie != ANO_NEAR_NONE
        && lo <= state->pendingTie && state->pendingTie <= hi) {
        pin0 = state->pendingTie;
        if (guard) {
            double t0 = ctx->bar * barQ;
            int bassNow, pm, pb;
            if (guard_bass_at(guard, t0, &bassNow) && guard_pair(guard, t0, &pm, &pb)) {
                if (ano_forbidden_parallel(pb, pm, bassNow, pin0)
                    || ano_forbidden_direct(pb, pm, bassNow, pin0, 2))
                    pin0 = ANO_NEAR_NONE;
            }
        }
    }

    if (replayed) {
        // placed stands
    } else if (lifecycle == ANO_MEL_COMPLETED) {
        // payoff drive: the phrase develops the signature constraint-first
        AnoVariantOp op;
        int step;
        AnoMotif variant = ano_phrase_variant(sig, pos.pos, rng, ano_meter_slots(meter),
                                              &op, &step);
        placedN = mel_place(&variant, ctx, mscale, params, state, lo, hi, strongMask,
                            ANO_NEAR_NONE, guard, pin0, placed, &anchor);
    } else if (sigEvent && lifecycle == ANO_MEL_INTRODUCED) {
        placedN = mel_introduce(sig, ctx, mscale, params, state, lo, hi, strongMask,
                                guard, pin0, placed, &anchor);
    } else if (sigEvent && lifecycle == ANO_MEL_DEVELOPED) {
        placedN = mel_place(sig, ctx, mscale, params, state, lo, hi, strongMask,
                            ANO_NEAR_NONE, guard, pin0, placed, &anchor);
    } else if (sigEvent) { // "stated": the faithful authored recurrence (M17)
        placedN = ano_realize_faithful(sig, mscale, ctx->chordPcs, ctx->chordPcCount,
                                       lo, hi, strongMask, state->prevPitch, placed);
        anchor = placedN ? placed[0].pitch : params->registerCenter;
        faithful = true;
    } else {
        AnoVariantOp op;
        int step;
        AnoMotif variant = ano_phrase_variant(motif, pos.pos, rng, ano_meter_slots(meter),
                                              &op, &step);
        placedN = mel_place(&variant, ctx, mscale, params, state, lo, hi, strongMask,
                            apexBar ? apex->pitch : ANO_NEAR_NONE, guard, pin0,
                            placed, &anchor);
    }

    // D1 tie bookkeeping: the hosted continuation, then maybe a pushed tail.
    uint8_t ties[ANO_MOTIF_MAX] = { 0 };
    if (pin0 != ANO_NEAR_NONE && placedN && !faithful && !replayed
        && placed[0].slot == 0 && placed[0].pitch == pin0)
        ties[0] = ANO_MUSIC_TIE_IN;
    int pendingOut = ANO_NEAR_NONE;
    if (syncopateRng && placedN && !faithful
        && params->roughness > 0.35
        && placed[placedN - 1].slot + placed[placedN - 1].durSlots == ano_meter_slots(meter)
        && ano_music_random(syncopateRng) < params->roughness - 0.3) {
        ties[placedN - 1] = ties[placedN - 1] == ANO_MUSIC_TIE_IN ? ANO_MUSIC_TIE_BOTH
                                                                  : ANO_MUSIC_TIE_OUT;
        pendingOut = placed[placedN - 1].pitch;
    }

    double barStart = ctx->bar * barQ;
    for (uint32_t i = 0; i < placedN; ++i) {
        int pitch = placed[i].pitch;
        const char *role;
        if (faithful) {
            role = "motif"; // licensed as a whole; note heuristics don't apply
        } else if (pc_in(pitch, ctx->chordPcs, ctx->chordPcCount)) {
            role = "chord-tone";
        } else if (ano_scale_contains(ctx->scale, pitch)) {
            bool hasPrevP = i > 0;
            int prevP = hasPrevP ? placed[i - 1].pitch : 0;
            bool nextEq = i + 1 < placedN && placed[i + 1].pitch == prevP;
            role = hasPrevP && nextEq ? "neighbor" : "passing";
        } else {
            role = "borrowed";
        }
        mel_event(&out->events[out->eventCount++], ctx,
                  barStart + placed[i].slot * GRID, placed[i].durSlots * GRID,
                  pitch, velocity_of(params, 0), role, ties[i]);
    }

    if (doubleLine && out->eventCount) {
        AnoMusicEvent dbl[ANO_MELODY_MAX_EVENTS];
        uint32_t dn = mel_double_line(out->events, out->eventCount, ctx, meter,
                                      strongMask, dbl);
        for (uint32_t i = 0; i < dn; ++i)
            out->events[out->eventCount++] = dbl[i];
    }

    out->state = (AnoMelodyState){
        .prevPitch = placedN ? placed[placedN - 1].pitch : state->prevPitch,
        .prevAnchor = anchor,
        .hasPrevOuter = guard ? guard->hasPrev : state->hasPrevOuter,
        .outerT = guard ? guard->prevT : state->outerT,
        .outerMelody = guard ? guard->prevM : state->outerMelody,
        .outerBass = guard ? guard->prevB : state->outerBass,
        .pendingTie = pendingOut,
    };
}
