/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_bass.c
 * Parity notes: the approach draw fires ONLY when density/lookahead/bar-length
 * gates all pass AND at least one option survives the range filter; option
 * order is below(0.40), above(0.30), chromatic(0.30, only when distinct from
 * the diatonic below); nearest-instance ties break low (min by (|d|, p)).
 */

#include <string.h>

#include "music_bass.h"
#include "music_motif.h" // ANO_NEAR_NONE

AnoBassConfig ano_bass_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoBassConfig k = { .lo = 28, .hi = 50, .velocityOffset = 8,
                            .approachBeats = 1.0 };
    return k;
}

static int nearest_instance(int pc, int near, int lo, int hi)
{
    int first = lo + ((pc - lo) % 12 + 12) % 12;
    int best = first;
    int bestAbs = best - near < 0 ? near - best : best - near;
    for (int p = first + 12; p <= hi; p += 12) {
        int a = p - near < 0 ? near - p : p - near;
        if (a < bestAbs) { // ties keep the lower p (ascending scan)
            best = p;
            bestAbs = a;
        }
    }
    return best;
}

static void bass_note(AnoMusicEvent *e, const AnoHarmonicContext *ctx,
                      double t, double d, int p, int velocity, const char *role)
{
    *e = (AnoMusicEvent){ 0 };
    e->core = (AnoNoteEvent){ t, d, (uint8_t)p, (uint8_t)velocity,
                              ANO_MUSIC_BASS, ANO_MUSIC_TIE_NONE };
    int deg = ano_scale_degree_of(ctx->scale, p);
    e->degree = deg > 0 ? (uint8_t)deg : 0;
    strncpy(e->chordSym, ctx->chordSym, sizeof e->chordSym - 1);
    strncpy(e->role, role, sizeof e->role - 1);
}

void ano_generate_bass(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, int prevRoot, int nextBassPc,
                       const AnoBassConfig *cfg, AnoMusicRng *rng, int pedalDegree,
                       AnoBassResult *out)
{
    *out = (AnoBassResult){ 0 };
    double barLen = ano_meter_bar_quarters(meter);
    double start = ctx->bar * barLen;
    int rootPc = ctx->chordPcs[0];
    int near = prevRoot != ANO_NEAR_NONE ? prevRoot : (cfg->lo + cfg->hi) / 2;
    int root = nearest_instance(rootPc, near, cfg->lo, cfg->hi);
    int velocity = params->velocityCenter + cfg->velocityOffset;
    velocity = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;

    if (pedalDegree) {
        // nearest to the previous root on entry, then to itself: a held point
        int pc = ano_scale_pitch_at(ctx->scale, pedalDegree, 4) % 12;
        int pedal = nearest_instance(pc, near, cfg->lo, cfg->hi);
        bass_note(&out->events[0], ctx, start, barLen, pedal, velocity, "pedal");
        out->eventCount = 1;
        out->root = pedal;
        return;
    }

    bool haveApproach = false;
    AnoMusicEvent approach;
    bool wantsApproach = params->noteDensity >= 0.35
                      && nextBassPc >= 0
                      && nextBassPc != rootPc
                      && barLen >= 2.0;
    if (wantsApproach) {
        int target = nearest_instance(nextBassPc, root, cfg->lo, cfg->hi);
        // (pitch, weight) options: diatonic below, diatonic above, chromatic
        int below = target - 1;
        while (!ano_scale_contains(ctx->scale, below))
            below--; // a diatonic tone always sits within 3 semitones
        int above = target + 1;
        while (!ano_scale_contains(ctx->scale, above))
            above++;
        int    optP[3];
        double optW[3];
        uint32_t optN = 0;
        if (cfg->lo <= below && below <= cfg->hi) {
            optP[optN] = below;
            optW[optN++] = 0.40;
        }
        if (cfg->lo <= above && above <= cfg->hi) {
            optP[optN] = above;
            optW[optN++] = 0.30;
        }
        int chromatic = target - 1;
        if (chromatic != below && cfg->lo <= chromatic && chromatic <= cfg->hi) {
            optP[optN] = chromatic;
            optW[optN++] = 0.30;
        }
        if (optN) {
            uint32_t i = ano_music_choices1(rng, optW, optN);
            bass_note(&approach, ctx, start + barLen - cfg->approachBeats,
                      cfg->approachBeats, optP[i], velocity, "approach");
            haveApproach = true;
        }
    }

    // root/fifth split lands on a pulse
    int halfPulses = ano_meter_pulses(meter) / 2;
    double split = ano_meter_pulse_quarters(meter) * (halfPulses > 1 ? halfPulses : 1);
    if (params->noteDensity < 0.35 || !haveApproach) {
        bass_note(&out->events[out->eventCount++], ctx, start, barLen, root,
                  velocity, "root");
    } else if (params->noteDensity < 0.65 || barLen - split - cfg->approachBeats <= 0.0) {
        bass_note(&out->events[out->eventCount++], ctx, start,
                  barLen - cfg->approachBeats, root, velocity, "root");
    } else {
        int fifthPc = ctx->chordPcCount >= 3 ? ctx->chordPcs[2] : rootPc;
        int fifth = nearest_instance(fifthPc, root, cfg->lo, cfg->hi);
        bass_note(&out->events[out->eventCount++], ctx, start, split, root,
                  velocity, "root");
        bass_note(&out->events[out->eventCount++], ctx, start + split,
                  barLen - split - cfg->approachBeats, fifth, velocity, "chord-tone");
    }
    if (haveApproach)
        out->events[out->eventCount++] = approach;
    out->root = root;
}
