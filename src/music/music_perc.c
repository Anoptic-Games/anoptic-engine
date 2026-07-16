/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Hits sort by (slot, drum NAME, velocity); equal-slot tie-break is strcmp on drum name.
// ohat draw: only at slot slots-2, and only after drop draw passed (and short-circuit).
// Fill velocities: 84 + 7i before dyn scale, then banker's round.

#include <stdio.h>
#include <string.h>

#include "music_perc.h"

const char *const ANO_DRUM_NAMES[ANO_DRUM_COUNT] = {
    "kick", "rim", "snare", "chat", "ohat", "crash", "ltom", "mtom", "htom", "shaker",
};
const uint8_t ANO_DRUM_PITCHES[ANO_DRUM_COUNT] = {
    36, 37, 38, 42, 46, 49, 45, 47, 50, 70,
};

static const int FILL_PATTERNS[3][4] = {
    { -6, -4, -2, 0 }, { -8, -6, -4, -2 }, { -6, -4, -3, -2 },
};
static const int FILL_LENS[3] = { 3, 4, 4 };
static const AnoDrum FILL_VOICES[4] = {
    ANO_DRUM_SNARE, ANO_DRUM_HTOM, ANO_DRUM_MTOM, ANO_DRUM_LTOM,
};

AnoPercConfig ano_perc_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoPercConfig k = {
        .fillBaseProb = 0.25, .fillTensionWeight = 0.55, .ghostVelocity = 52,
        .kickVel = 100, .snareVel = 96, .chatVel = 64, .ohatVel = 70, .crashVel = 106,
    };
    return k;
}

// Ghost-candidate slots, in the prototype's tuple order.
static uint32_t ghost_slots(AnoMeter m, int out[8])
{
    uint32_t n = 0;
    int ps = ano_meter_pulse_slots(m);
    int pulses = ano_meter_pulses(m);
    if (ano_meter_is_compound(m)) { // 8th pickups into each pulse
        for (int p = 1; p <= pulses && n < 8u; ++p)
            out[n++] = p * ps - 2;
    } else if (ano_meter_slots(m) == 16) { // the classic 4/4 set, verbatim
        out[n++] = 3;
        out[n++] = 7;
        out[n++] = 10;
        out[n++] = 15;
    } else {
        for (int p = 1; p <= pulses && n < 8u; ++p)
            out[n++] = p * ps - 1;
    }
    return n;
}

AnoGroove ano_make_groove(AnoMusicRng *rng, AnoMeter meter, double density,
                          double roughness)
{
    AnoGroove g = { 0 };
    double gp = roughness - 0.25;
    double ghostProb = (gp > 0.0 ? gp : 0.0) * 0.6;
    int cand[8];
    uint32_t cn = ghost_slots(meter, cand);
    int slots = ano_meter_slots(meter);
    for (uint32_t i = 0; i < cn; ++i)
        if (cand[i] < slots && ano_music_random(rng) < ghostProb)
            g.ghosts[g.ghostCount++] = (uint8_t)cand[i];
    int hatStep = density > 0.7 ? 1 : 2;
    double hd = 1.0 - density;
    double hatDrop = (hd > 0.0 ? hd : 0.0) * 0.3;
    for (int s = 0; s < slots; s += hatStep)
        if (ano_music_random(rng) < hatDrop)
            g.hatDrops |= 1u << s;
    g.ohat = ano_music_random(rng) < 0.25;
    return g;
}

typedef struct Hit
{
    int     slot;
    AnoDrum drum;
    int     velocity;
} Hit;

static void hit_push(Hit *hits, uint32_t *n, int slot, AnoDrum drum, int vel)
{
    hits[(*n)++] = (Hit){ slot, drum, vel };
}

void ano_generate_perc(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoPhrasePos pos, bool hadFill,
                       const AnoPercConfig *cfg, AnoMusicRng *rng,
                       const AnoGroove *groove, double hyperFill,
                       AnoPercResult *out)
{
    *out = (AnoPercResult){ 0 };
    int slots = ano_meter_slots(meter);
    double density = params->noteDensity, roughness = params->roughness;
    double dyn = params->velocityCenter / 80.0;
    int ps = ano_meter_pulse_slots(meter);
    int pulses = ano_meter_pulses(meter);

    Hit hits[64];
    uint32_t hn = 0;

    if (ano_meter_is_compound(meter)) {
        // grouped kicks as a slot set: even pulses (+ shuffle 8th, + pickup)
        bool kick[ANO_METER_MAX_SLOTS] = { false };
        for (int p = 0; p < pulses; p += 2)
            kick[p * ps] = true;
        if (density > 0.55)
            for (int p = 0; p < pulses; p += 2)
                kick[p * ps + 4] = true;
        if (density > 0.75)
            kick[slots - 2] = true;
        for (int s = 0; s < slots; ++s) // sorted(set)
            if (kick[s])
                hit_push(hits, &hn, s, ANO_DRUM_KICK, cfg->kickVel);
    } else {
        int kickK = 2 + (int)ano_music_round_int(density * 3);
        uint8_t ks[ANO_RHYTHM_MAX];
        uint32_t kn = ano_euclid(kickK, slots, 0, ks);
        for (uint32_t i = 0; i < kn; ++i)
            hit_push(hits, &hn, ks[i], ANO_DRUM_KICK, cfg->kickVel);
    }

    for (int p = 1; p < pulses; p += 2) // the generalized backbeat
        hit_push(hits, &hn, p * ps, ANO_DRUM_SNARE, cfg->snareVel);
    if (groove) {
        for (uint32_t i = 0; i < groove->ghostCount; ++i)
            hit_push(hits, &hn, groove->ghosts[i], ANO_DRUM_SNARE, cfg->ghostVelocity);
    } else {
        double gp = roughness - 0.25;
        double ghostProb = (gp > 0.0 ? gp : 0.0) * 0.6;
        int cand[8];
        uint32_t cn = ghost_slots(meter, cand);
        for (uint32_t i = 0; i < cn; ++i)
            if (cand[i] < slots && ano_music_random(rng) < ghostProb)
                hit_push(hits, &hn, cand[i], ANO_DRUM_SNARE, cfg->ghostVelocity);
    }

    int hatStep = density > 0.7 ? 1 : 2;
    double hd = 1.0 - density;
    double hatDrop = (hd > 0.0 ? hd : 0.0) * 0.3;
    for (int s = 0; s < slots; s += hatStep) {
        AnoDrum drum;
        if (groove) {
            if (groove->hatDrops >> s & 1u)
                continue;
            drum = s == slots - 2 && groove->ohat ? ANO_DRUM_OHAT : ANO_DRUM_CHAT;
        } else {
            if (ano_music_random(rng) < hatDrop)
                continue;
            // the ohat draw fires only at slots-2 (`and` short-circuit)
            drum = s == slots - 2 && ano_music_random(rng) < 0.25 ? ANO_DRUM_OHAT
                                                                  : ANO_DRUM_CHAT;
        }
        int accent = s % ps == 0 ? 6 : 0;
        hit_push(hits, &hn, s, drum, cfg->chatVel + accent);
    }

    bool fill = false;
    bool fillBar = ano_phrase_slot(pos) == ANO_SLOT_CADENCE
                || (hyperFill > 0 && pos.pos == pos.bars / 2 - 1);
    if (fillBar) {
        double fillProb = cfg->fillBaseProb + ctx->tension * cfg->fillTensionWeight;
        if (ano_phrase_slot(pos) != ANO_SLOT_CADENCE)
            fillProb *= hyperFill; // mid-phrase fills are the rarer punctuation
        fill = ano_music_random(rng) < fillProb;
        if (fill) {
            uint32_t pi = ano_music_choice(rng, 3); // randrange over FILL_PATTERNS
            int pattern[4];
            int pn = FILL_LENS[pi];
            for (int i = 0; i < pn; ++i)
                pattern[i] = slots + FILL_PATTERNS[pi][i];
            uint32_t kept = 0; // filter preserves order
            for (uint32_t i = 0; i < hn; ++i)
                if (hits[i].slot < pattern[0])
                    hits[kept++] = hits[i];
            hn = kept;
            for (int i = 0; i < pn; ++i) {
                AnoDrum voice = FILL_VOICES[i < 3 ? i : 3];
                hit_push(hits, &hn, pattern[i], voice, 84 + i * 7);
            }
        }
    }

    if (hadFill && (pos.pos == 0 || (hyperFill > 0 && pos.pos == pos.bars / 2)))
        hit_push(hits, &hn, 0, ANO_DRUM_CRASH, cfg->crashVel);

    // sorted(hits): tuple order (slot, drum NAME string, velocity)
    for (uint32_t i = 1; i < hn; ++i) {
        Hit key = hits[i];
        uint32_t j = i;
        while (j > 0) {
            const Hit *q = &hits[j - 1];
            int c = q->slot != key.slot ? (q->slot < key.slot ? -1 : 1)
                  : strcmp(ANO_DRUM_NAMES[q->drum], ANO_DRUM_NAMES[key.drum]);
            if (c < 0 || (c == 0 && q->velocity <= key.velocity))
                break;
            hits[j] = hits[j - 1];
            --j;
        }
        hits[j] = key;
    }

    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    for (uint32_t i = 0; i < hn; ++i) {
        int vel = (int)ano_music_round_int(hits[i].velocity * dyn);
        vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
        AnoMusicEvent *e = &out->events[out->eventCount++];
        *e = (AnoMusicEvent){ 0 };
        e->core = (AnoNoteEvent){ barStart + hits[i].slot * ANO_MUSIC_GRID,
                                  ANO_MUSIC_GRID, ANO_DRUM_PITCHES[hits[i].drum],
                                  (uint8_t)vel, ANO_MUSIC_PERC, ANO_MUSIC_TIE_NONE };
        snprintf(e->role, sizeof e->role, "drum:%s", ANO_DRUM_NAMES[hits[i].drum]);
    }
    out->fill = fill;
}
