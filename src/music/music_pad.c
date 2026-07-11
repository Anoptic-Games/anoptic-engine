/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_pad.c
 * Parity notes: the ornament searches are max-by-tuple with strict >
 * (suspension key (pitch, -step): highest suspension, then the tighter
 * step; appoggiatura key (onto-tonic, target) with the whole-step lean
 * winning per target via the loop break); the connective picker prefers a
 * 3rd-sized gap then the HIGHEST voice, its passing tone the min by
 * (|q - mid|, q) against the FLOAT midpoint; comping draws rough_cell over
 * the pulse grid at base_step 1.
 */

#include <string.h>

#include "music_gen.h"
#include "music_motif.h" // ANO_NEAR_NONE
#include "music_pad.h"

#define PAD_VELOCITY_OFFSET (-6)

static const int COMPING_ORDER[4] = { 0, 2, 1, 3 }; // low, mid-high, mid-low, top

static bool pc_in(int pitch, const uint8_t *pcs, uint32_t pcCount)
{
    int pc = (pitch % 12 + 12) % 12;
    for (uint32_t i = 0; i < pcCount; ++i)
        if (pcs[i] == pc)
            return true;
    return false;
}

// (voice index, passing pitch) for the connective mode, or false.
static bool connective_voice(const int *voicing, const int *nextVoicing, uint32_t n,
                             AnoScale scale, int *outVoice, int *outPassing)
{
    bool have = false;
    bool bestThird = false;
    int bestI = 0, bestP = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int cur = voicing[i], nxt = nextVoicing[i];
        int gap = nxt - cur < 0 ? cur - nxt : nxt - cur;
        if (gap < 2)
            continue;
        int lo = cur < nxt ? cur : nxt, hi = cur < nxt ? nxt : cur;
        double mid = (cur + nxt) / 2.0;
        // min(between, key=(|q - mid|, q))
        bool haveBetween = false;
        int pass = 0;
        double passAbs = 0.0;
        for (int q = lo + 1; q < hi; ++q) {
            if (!ano_scale_contains(scale, q))
                continue;
            double a = q - mid < 0.0 ? mid - q : q - mid;
            if (!haveBetween || a < passAbs) { // ties keep lower q (ascending)
                haveBetween = true;
                pass = q;
                passAbs = a;
            }
        }
        if (!haveBetween)
            continue;
        bool third = gap == 3 || gap == 4;
        // key (third, i), strict >
        if (!have || (third && !bestThird) || (third == bestThird && (int)i > bestI)) {
            have = true;
            bestThird = third;
            bestI = (int)i;
            bestP = pass;
        }
    }
    *outVoice = bestI;
    *outPassing = bestP;
    return have;
}

// (target, suspended) of the best prepared suspension, or false.
static bool suspension_pair(const int *voicing, uint32_t n,
                            const int *prevVoicing, uint32_t prevCount,
                            const uint8_t *chordPcs, uint32_t chordPcCount,
                            AnoScale scale, int *outTarget, int *outSuspended)
{
    if (!prevVoicing || prevCount == 0)
        return false;
    bool have = false;
    int bestS = 0, bestStep = 0, bestTarget = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int target = voicing[i];
        for (int step = 1; step <= 2; ++step) {
            int s = target + step;
            bool prepared = false;
            for (uint32_t j = 0; j < prevCount; ++j)
                if (prevVoicing[j] == s)
                    prepared = true;
            if (!prepared || pc_in(s, chordPcs, chordPcCount)
                || !ano_scale_contains(scale, s))
                continue;
            // key (s, -step), strict >: highest suspension, then tighter step
            if (!have || s > bestS || (s == bestS && -step > -bestStep)) {
                have = true;
                bestS = s;
                bestStep = step;
                bestTarget = target;
            }
        }
    }
    *outTarget = bestTarget;
    *outSuspended = bestS;
    return have;
}

// (target, appoggiatura) of the best unprepared lean, or false.
static bool appoggiatura_pair(const int *voicing, uint32_t n,
                              const AnoHarmonicContext *ctx, int hi,
                              int *outTarget, int *outLean)
{
    int tonicPc = ano_scale_pitch_at(ctx->scale, 1, 4) % 12;
    bool have = false;
    bool bestTonic = false;
    int bestTarget = 0, bestA = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int target = voicing[i];
        for (int k = 0; k < 2; ++k) {
            int step = k == 0 ? 2 : 1; // the whole-step lean reads stronger
            int a = target + step;
            if (a > hi || pc_in(a, ctx->chordPcs, ctx->chordPcCount)
                || !ano_scale_contains(ctx->scale, a))
                continue;
            bool tonic = (target % 12 + 12) % 12 == tonicPc;
            // key (tonic, target), strict >
            if (!have || (tonic && !bestTonic)
                || (tonic == bestTonic && target > bestTarget)) {
                have = true;
                bestTonic = tonic;
                bestTarget = target;
                bestA = a;
            }
            break; // the first valid step wins for this target
        }
    }
    *outTarget = bestTarget;
    *outLean = bestA;
    return have;
}

static void pad_note(AnoMusicEvent *e, const AnoHarmonicContext *ctx,
                     double t, double d, int pitch, int velocity,
                     const char *role, uint8_t tie)
{
    *e = (AnoMusicEvent){ 0 };
    e->core = (AnoNoteEvent){ t, d, (uint8_t)pitch, (uint8_t)velocity,
                              ANO_MUSIC_PAD, tie };
    int deg = ano_scale_degree_of(ctx->scale, pitch);
    e->degree = deg > 0 ? (uint8_t)deg : 0;
    strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
    if (!role[0])
        role = ano_scale_contains(ctx->scale, pitch) ? "chord-tone" : "borrowed";
    strncpy(e->role, role, sizeof e->role - 1);
}

void ano_generate_pad(const AnoHarmonicContext *ctx, AnoMeter meter,
                      const AnoGenParams *params,
                      const int *prevVoicing, uint32_t prevCount,
                      const AnoVoicingConfig *cfg, bool suspend, bool appoggiatura,
                      const uint8_t *nextPcs, uint32_t nextPcCount,
                      AnoPadAnimate animate, AnoMusicRng *rng, bool thin,
                      const AnoPadTiePrep *tiePrep, int prevTie,
                      AnoPadResult *out)
{
    *out = (AnoPadResult){ 0 };
    // voicing wants ROOT-first pcs (doubling preferences); chordPcs is
    // bass-first (equal unless inverted)
    uint8_t pcs[5];
    uint32_t pcCount;
    if (ctx->chord.valid) {
        pcCount = ano_chord_pitch_classes(ctx->chord, ctx->scale, pcs);
    } else {
        pcCount = ctx->chordPcCount;
        memcpy(pcs, ctx->chordPcs, sizeof pcs);
    }
    AnoVoicingConfig vc = *cfg;
    if (thin) { // bare root+fifth dyad, free of thirds
        pcs[1] = pcCount > 2 ? pcs[2] : pcs[0];
        pcCount = 2;
        vc.voices = 2;
    }
    int voicing[6];
    uint32_t vn = ano_voice_chord(pcs, pcCount, prevVoicing, prevCount, &vc,
                                  voicing, NULL);
    for (uint32_t i = 0; i < vn; ++i)
        out->voicing[i] = voicing[i];
    out->voiceCount = vn;

    double barLen = ano_meter_bar_quarters(meter);
    double start = ctx->bar * barLen;
    int velocity = params->velocityCenter + PAD_VELOCITY_OFFSET;
    velocity = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;

    // ornament: a prepared suspension if available, else the payoff lean
    bool haveOrn = false;
    int ornTarget = 0, ornDiss = 0;
    const char *ornRole = "";
    if (suspend && suspension_pair(voicing, vn, prevVoicing, prevCount,
                                   ctx->chordPcs, ctx->chordPcCount, ctx->scale,
                                   &ornTarget, &ornDiss)) {
        haveOrn = true;
        ornRole = "suspension";
    }
    if (!haveOrn && appoggiatura
        && appoggiatura_pair(voicing, vn, ctx, vc.hi, &ornTarget, &ornDiss)) {
        haveOrn = true;
        ornRole = "appoggiatura";
    }

    if (!haveOrn && animate == ANO_PAD_CONNECTIVE && nextPcs && nextPcCount) {
        int nxt[6];
        uint32_t nn = ano_voice_chord(nextPcs, nextPcCount, voicing, vn, &vc, nxt, NULL);
        int vi, vp;
        if (nn == vn && connective_voice(voicing, nxt, vn, ctx->scale, &vi, &vp)) {
            double walkAt = barLen - ano_meter_pulse_quarters(meter);
            for (uint32_t j = 0; j < vn; ++j) {
                if ((int)j == vi) {
                    pad_note(&out->events[out->eventCount++], ctx, start, walkAt,
                             voicing[j], velocity, "", ANO_MUSIC_TIE_NONE);
                    pad_note(&out->events[out->eventCount++], ctx, start + walkAt,
                             barLen - walkAt, vp, velocity,
                             pc_in(vp, ctx->chordPcs, ctx->chordPcCount) ? "" : "passing",
                             ANO_MUSIC_TIE_NONE);
                } else {
                    pad_note(&out->events[out->eventCount++], ctx, start, barLen,
                             voicing[j], velocity, "", ANO_MUSIC_TIE_NONE);
                }
            }
            return;
        }
    }
    if (!haveOrn && animate == ANO_PAD_COMPING && rng) {
        AnoRhythmNote cell[ANO_RHYTHM_MAX];
        uint32_t cn = ano_rough_cell(rng, params->noteDensity, params->roughness,
                                     ano_meter_pulses(meter), 1, cell);
        double pq = ano_meter_pulse_quarters(meter);
        for (uint32_t j = 0; j < cn; ++j) {
            int voice = COMPING_ORDER[j % 4] % (int)vn;
            pad_note(&out->events[out->eventCount++], ctx,
                     start + cell[j].slot * pq, cell[j].durSlots * pq,
                     voicing[voice], velocity, "", ANO_MUSIC_TIE_NONE);
        }
        return;
    }
    if (!haveOrn) {
        int held = ANO_NEAR_NONE;
        if (tiePrep) {
            // predict next bar's voicing + suspension with the machinery next
            // bar will run; the preparing voice ties out (D1)
            int nxtVoicing[6];
            uint32_t nn = ano_voice_chord(tiePrep->pcs, tiePrep->pcCount, voicing, vn,
                                          &vc, nxtVoicing, NULL);
            int pt, psn;
            if (suspension_pair(nxtVoicing, nn, voicing, vn, tiePrep->chordPcs,
                                tiePrep->chordPcCount, tiePrep->scale, &pt, &psn))
                held = psn;
        }
        for (uint32_t j = 0; j < vn; ++j)
            pad_note(&out->events[out->eventCount++], ctx, start, barLen, voicing[j],
                     velocity, "",
                     voicing[j] == held ? ANO_MUSIC_TIE_OUT : ANO_MUSIC_TIE_NONE);
        return;
    }

    // the ornament owns the bar: dissonance resolves at the mid-bar pulse
    int halfPulses = ano_meter_pulses(meter) / 2;
    double resAt = ano_meter_pulse_quarters(meter) * (halfPulses > 1 ? halfPulses : 1);
    for (uint32_t j = 0; j < vn; ++j) {
        if (voicing[j] == ornTarget) {
            bool genuinelyHeld = strcmp(ornRole, "suspension") == 0 && prevTie == ornDiss;
            pad_note(&out->events[out->eventCount++], ctx, start, resAt, ornDiss,
                     velocity, ornRole,
                     genuinelyHeld ? ANO_MUSIC_TIE_IN : ANO_MUSIC_TIE_NONE);
            pad_note(&out->events[out->eventCount++], ctx, start + resAt,
                     barLen - resAt, ornTarget, velocity, "resolution",
                     ANO_MUSIC_TIE_NONE);
        } else {
            pad_note(&out->events[out->eventCount++], ctx, start, barLen, voicing[j],
                     velocity, "", ANO_MUSIC_TIE_NONE);
        }
    }
}
