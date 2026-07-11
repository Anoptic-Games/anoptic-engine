/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_counter.c
 * Parity notes: the surface sort is STABLE by start (doubling filtered
 * first); candidate lists sort by the total order (|p - target|, p); the
 * two-pass clean search tries 3rds/6ths-preferred before any consonance;
 * the motion chains apply only inside their one-bar expiry; forbidden_direct
 * uses max_step 2 (the kwarg default). The guide thread advances every bar,
 * sounding or not.
 */

#include <string.h>

#include "music_counter.h"

AnoCounterConfig ano_counter_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoCounterConfig k = { .lo = 55, .hi = 79, .velocityOffset = -10,
                               .densityScale = 0.6 };
    return k;
}

// The event sounding at t, or NULL (events pre-sorted by start).
static const AnoMusicEvent *sounding(const AnoMusicEvent *const *events, uint32_t n,
                                     double t)
{
    const AnoMusicEvent *cur = NULL;
    for (uint32_t i = 0; i < n; ++i) {
        if (events[i]->core.start > t + 1e-9)
            break;
        if (t < events[i]->core.start + events[i]->core.dur - 1e-9)
            cur = events[i];
    }
    return cur;
}

static bool pc_in(int pitch, const uint8_t *pcs, uint32_t pcCount)
{
    int pc = (pitch % 12 + 12) % 12;
    for (uint32_t i = 0; i < pcCount; ++i)
        if (pcs[i] == pc)
            return true;
    return false;
}

static bool strong_pref(int ic) // 3rds & 6ths
{
    return ic == 3 || ic == 4 || ic == 8 || ic == 9;
}

void ano_generate_counter(const AnoHarmonicContext *ctx, AnoMeter meter,
                          const AnoGenParams *params,
                          const AnoMusicEvent *melodyEvents, uint32_t melodyCount,
                          const AnoMusicEvent *bassEvents, uint32_t bassCount,
                          const AnoCounterState *state, const AnoCounterConfig *cfg,
                          AnoMusicRng *rng, AnoCounterResult *out)
{
    *out = (AnoCounterResult){ 0 };
    out->state = *state;
    AnoScale mscale = ctx->chord.valid ? ano_chord_scale_for(ctx->chord, ctx->scale)
                                       : ctx->scale;
    uint32_t strongMask = 0;
    {
        int ss[ANO_METER_MAX_SLOTS];
        uint32_t sn = ano_meter_strong_slots(meter, ss);
        for (uint32_t i = 0; i < sn; ++i)
            strongMask |= 1u << ss[i];
    }
    double barQ = ano_meter_bar_quarters(meter);
    double barStart = ctx->bar * barQ;

    // surface = melody minus doubling, stable-sorted by start
    const AnoMusicEvent *surface[128];
    uint32_t sfn = 0;
    for (uint32_t i = 0; i < melodyCount && sfn < 128u; ++i)
        if (strcmp(melodyEvents[i].role, "doubling") != 0)
            surface[sfn++] = &melodyEvents[i];
    for (uint32_t i = 1; i < sfn; ++i) {
        const AnoMusicEvent *key = surface[i];
        uint32_t j = i;
        while (j > 0 && surface[j - 1]->core.start > key->core.start) {
            surface[j] = surface[j - 1];
            --j;
        }
        surface[j] = key;
    }
    const AnoMusicEvent *bass[64];
    uint32_t bn = 0;
    for (uint32_t i = 0; i < bassCount && bn < 64u; ++i)
        bass[bn++] = &bassEvents[i];
    for (uint32_t i = 1; i < bn; ++i) {
        const AnoMusicEvent *key = bass[i];
        uint32_t j = i;
        while (j > 0 && bass[j - 1]->core.start > key->core.start) {
            bass[j] = bass[j - 1];
            --j;
        }
        bass[j] = key;
    }

    // 1. complementarity: keep the holes the melody leaves
    AnoRhythmNote cell[ANO_RHYTHM_MAX];
    uint32_t cn = ano_rough_cell(rng, params->noteDensity * cfg->densityScale,
                                 params->roughness, ano_meter_slots(meter), 2, cell);
    uint32_t melodyMask = 0;
    for (uint32_t i = 0; i < sfn; ++i)
        melodyMask |= 1u << ano_meter_slot_of(meter, surface[i]->core.start);
    AnoRhythmNote kept[ANO_RHYTHM_MAX];
    uint32_t kn = 0;
    for (uint32_t i = 0; i < cn; ++i)
        if (!(melodyMask >> cell[i].slot & 1u))
            kept[kn++] = cell[i];
    if (kn == 0)
        return; // a saturated melody: the counter rests the bar

    // the guide thread continues regardless of what sounds
    int guide;
    if (ctx->chord.valid)
        guide = ano_next_guide(state->guidePc, ctx->chord, ctx->scale);
    else
        guide = state->guidePc >= 0 ? state->guidePc : ctx->chordPcs[0];
    int center = state->prevPitch != ANO_NEAR_NONE ? state->prevPitch
                                                   : (cfg->lo + cfg->hi) / 2;
    int target = center; // min(..., default=center)
    {
        bool have = false;
        int bestAbs = 0;
        for (int p = cfg->lo + ((guide - cfg->lo) % 12 + 12) % 12; p <= cfg->hi; p += 12) {
            int a = p - center < 0 ? center - p : p - center;
            if (!have || a < bestAbs) { // ties keep the lower p (ascending)
                have = true;
                target = p;
                bestAbs = a;
            }
        }
    }

    AnoPlacedNote placed[ANO_RHYTHM_MAX];
    uint32_t pn = 0;
    int prev = state->prevPitch;
    bool   hasVm = state->hasVsMelody, hasVb = state->hasVsBass;
    double vmT = state->vsMelodyT, vbT = state->vsBassT;
    int    vmC = state->vsMelodyC, vmM = state->vsMelodyM;
    int    vbC = state->vsBassC, vbB = state->vsBassB;
    for (uint32_t ki = 0; ki < kn; ++ki) {
        int slot = kept[ki].slot;
        double t = barStart + slot * ANO_MUSIC_GRID;
        const AnoMusicEvent *m = sounding(surface, sfn, t);
        const AnoMusicEvent *b = sounding(bass, bn, t);
        int ceiling = cfg->hi;
        if (m && m->core.pitch < ceiling)
            ceiling = m->core.pitch; // never above the sounding melody
        if (ceiling < cfg->lo)
            continue; // the melody dove into the tenor; yield
        int pitch;
        if (strongMask >> slot & 1u) {
            // chord-member candidates up to the ceiling, nearest-first
            int cands[64];
            uint32_t nc = 0;
            for (uint32_t i = 0; i < ctx->chordPcCount; ++i) {
                bool dup = false;
                for (uint32_t j = 0; j < i; ++j)
                    if (ctx->chordPcs[j] == ctx->chordPcs[i])
                        dup = true;
                if (dup)
                    continue;
                for (int p = cfg->lo + ((ctx->chordPcs[i] - cfg->lo) % 12 + 12) % 12;
                     p <= ceiling && nc < 64u; p += 12)
                    cands[nc++] = p;
            }
            for (uint32_t i = 1; i < nc; ++i) { // sort by (|p - target|, p)
                int key = cands[i];
                int ka = key - target < 0 ? target - key : key - target;
                uint32_t j = i;
                while (j > 0) {
                    int q = cands[j - 1];
                    int qa = q - target < 0 ? target - q : q - target;
                    if (qa < ka || (qa == ka && q < key))
                        break;
                    cands[j] = cands[j - 1];
                    --j;
                }
                cands[j] = key;
            }
            if (nc == 0)
                continue;
            pitch = -1;
            for (int pass = 0; pass < 2 && pitch < 0; ++pass) {
                bool prefer = pass == 0;
                for (uint32_t c = 0; c < nc; ++c) {
                    int p = cands[c];
                    if (m) {
                        // candidates never exceed the ceiling (= the melody
                        // pitch), so (p, m) is already (lower, upper)
                        int ic = ano_interval_class(p, m->core.pitch);
                        if (!ano_is_consonant(p, m->core.pitch)
                            || (prefer && !strong_pref(ic)))
                            continue;
                        if (hasVm && t - vmT <= barQ + 1e-9) {
                            if (ano_forbidden_parallel(vmC, vmM, p, m->core.pitch)
                                || (slot == 0 && ano_forbidden_direct(vmC, vmM, p,
                                                                      m->core.pitch, 2)))
                                continue;
                        }
                    }
                    if (b && hasVb && t - vbT <= barQ + 1e-9) {
                        if (ano_forbidden_parallel(vbB, vbC, b->core.pitch, p)
                            || (slot == 0 && ano_forbidden_direct(vbB, vbC,
                                                                  b->core.pitch, p, 2)))
                            continue;
                    }
                    pitch = p;
                    break;
                }
            }
            if (pitch < 0)
                continue; // nothing consonant and motion-clean: rest
        } else {
            // weak beats: one diatonic step toward the guide target
            int base = prev != ANO_NEAR_NONE ? prev : target;
            if (base == target)
                pitch = ano_snap_to_scale(mscale, base);
            else
                pitch = ano_diatonic_shift(mscale, base, target > base ? 1 : -1);
            int lowClamped = pitch > cfg->lo ? pitch : cfg->lo;
            pitch = lowClamped < ceiling ? lowClamped : ceiling;
            if (!ano_scale_contains(mscale, pitch)
                && !pc_in(pitch, ctx->chordPcs, ctx->chordPcCount)) {
                pitch = ano_snap_to_scale(mscale, pitch);
                if (pitch > ceiling)
                    pitch = ano_diatonic_shift(mscale, pitch, -1);
            }
        }
        placed[pn++] = (AnoPlacedNote){ slot, kept[ki].durSlots, pitch };
        if (strongMask >> slot & 1u) {
            if (m) {
                hasVm = true;
                vmT = t;
                vmC = pitch;
                vmM = m->core.pitch;
            }
            if (b) {
                hasVb = true;
                vbT = t;
                vbC = pitch;
                vbB = b->core.pitch;
            }
        }
        prev = pitch;
    }

    int velocity = params->velocityCenter + cfg->velocityOffset;
    velocity = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
    for (uint32_t i = 0; i < pn; ++i) {
        int pitch = placed[i].pitch;
        const char *role = pc_in(pitch, ctx->chordPcs, ctx->chordPcCount)
                         ? "chord-tone"
                         : ano_scale_contains(ctx->scale, pitch) ? "passing" : "borrowed";
        AnoMusicEvent *e = &out->events[out->eventCount++];
        *e = (AnoMusicEvent){ 0 };
        e->core = (AnoNoteEvent){ barStart + placed[i].slot * ANO_MUSIC_GRID,
                                  placed[i].durSlots * ANO_MUSIC_GRID,
                                  (uint8_t)pitch, (uint8_t)velocity,
                                  ANO_MUSIC_COUNTER, ANO_MUSIC_TIE_NONE };
        int deg = ano_scale_degree_of(ctx->scale, pitch);
        e->degree = deg > 0 ? (uint8_t)deg : 0;
        strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
        strncpy(e->role, role, sizeof e->role - 1);
    }
    out->state.prevPitch = prev;
    out->state.guidePc = guide;
    out->state.hasVsMelody = hasVm;
    out->state.vsMelodyT = vmT;
    out->state.vsMelodyC = vmC;
    out->state.vsMelodyM = vmM;
    out->state.hasVsBass = hasVb;
    out->state.vsBassT = vbT;
    out->state.vsBassC = vbC;
    out->state.vsBassB = vbB;
}
