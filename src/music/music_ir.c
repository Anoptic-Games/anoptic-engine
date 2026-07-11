/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_ir.c
 * Meter derivations, event validation, context lookup, parameter defaults
 * (musicgen/ir.py). Meter math is the parity-sensitive part: bar_of is
 * Python float floor division (ano_music_floordiv, NOT floor(a/b)), and
 * every round() is banker's (ano_music_round_int). Float op order matches
 * the prototype expression for expression.
 */

#include "music_ir.h"

#include <math.h>

double ano_meter_bar_quarters(AnoMeter m)
{
    return m.numerator * 4.0 / m.denominator;
}

int ano_meter_bar_of(AnoMeter m, double start)
{
    return (int)ano_music_floordiv(start, ano_meter_bar_quarters(m));
}

double ano_meter_beat_in_bar(AnoMeter m, double start)
{
    return start - ano_meter_bar_of(m, start) * ano_meter_bar_quarters(m) + 1.0;
}

int ano_meter_slots(AnoMeter m)
{
    return (int)ano_music_round_int(ano_meter_bar_quarters(m) / ANO_MUSIC_GRID);
}

int ano_meter_slot_of(AnoMeter m, double start)
{
    double inBar = start - ano_meter_bar_of(m, start) * ano_meter_bar_quarters(m);
    return (int)ano_music_round_int(inBar / ANO_MUSIC_GRID);
}

bool ano_meter_is_compound(AnoMeter m)
{
    return m.numerator >= 6 && m.numerator % 3 == 0;
}

int ano_meter_pulses(AnoMeter m)
{
    return ano_meter_is_compound(m) ? m.numerator / 3 : m.numerator;
}

double ano_meter_pulse_quarters(AnoMeter m)
{
    return ano_meter_bar_quarters(m) / ano_meter_pulses(m);
}

int ano_meter_pulse_slots(AnoMeter m)
{
    return (int)ano_music_round_int(ano_meter_pulse_quarters(m) / ANO_MUSIC_GRID);
}

uint32_t ano_meter_metric_weights(AnoMeter m, double out[ANO_METER_MAX_SLOTS])
{
    int slots = ano_meter_slots(m);
    if (slots > ANO_METER_MAX_SLOTS)
        slots = ANO_METER_MAX_SLOTS;
    int eighth = (int)ano_music_round_int(0.5 / ANO_MUSIC_GRID);
    if (eighth < 1)
        eighth = 1;
    int pulseSlots = ano_meter_pulse_slots(m);
    int pulses = ano_meter_pulses(m);
    for (int s = 0; s < slots; ++s) {
        if (s == 0) {
            out[s] = 4.0;
        } else if (s % pulseSlots == 0) {
            int pulse = s / pulseSlots;
            bool isMid = pulses % 2 == 0 && pulse == pulses / 2;
            out[s] = isMid ? 3.5 : 3.0;
        } else if (s % eighth == 0) {
            out[s] = 2.0;
        } else {
            out[s] = 1.0;
        }
    }
    return (uint32_t)slots;
}

uint32_t ano_meter_strong_slots(AnoMeter m, int out[ANO_METER_MAX_SLOTS])
{
    double w[ANO_METER_MAX_SLOTS];
    uint32_t slots = ano_meter_metric_weights(m, w);
    uint32_t n = 0;
    for (uint32_t s = 0; s < slots; ++s)
        if (w[s] >= 3.0)
            out[n++] = (int)s;
    return n;
}

const char *const ANO_LAYER_NAMES[ANO_MUSIC_LAYER_COUNT] = {
    "pad", "bass", "melody", "counter", "arp", "perc",
};

bool ano_note_event_valid(const AnoNoteEvent *ev)
{
    if (ev->layer >= ANO_MUSIC_LAYER_COUNT)
        return false;
    // pitch: uint8 covers 0..255; MIDI range is the real constraint
    if (ev->pitch > 127)
        return false;
    if (ev->velocity < 1 || ev->velocity > 127)
        return false;
    if (ev->start < 0.0 || ev->dur <= 0.0)
        return false;
    if (ev->tie > ANO_MUSIC_TIE_BOTH)
        return false;
    return true;
}

// in/out: the event streams (in == out merges in place). cap bounds out.
// Invariant: a chain's head is the only event kept; its dur accumulates with
// round(.,10) exactly as the prototype does, so a half-open chain left by a
// truncated render still yields the head's own duration.
uint32_t ano_merge_ties(const AnoMusicEvent *in, uint32_t n,
                        AnoMusicEvent *out, uint32_t cap)
{
    int open[ANO_MUSIC_LAYER_COUNT][128]; // (layer, pitch) -> index of the open head
    for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
        for (uint32_t p = 0; p < 128; ++p)
            open[l][p] = -1;

    uint32_t m = 0;
    for (uint32_t i = 0; i < n && m < cap; ++i) {
        AnoMusicEvent ev = in[i]; // copied: in may alias out
        uint32_t l = ev.core.layer, p = ev.core.pitch;
        if (l >= ANO_MUSIC_LAYER_COUNT || p > 127)
            continue;
        if (ev.core.tie == ANO_MUSIC_TIE_IN || ev.core.tie == ANO_MUSIC_TIE_BOTH) {
            int h = open[l][p];
            if (h >= 0 && fabs(out[h].core.start + out[h].core.dur - ev.core.start) < 1e-9) {
                out[h].core.dur = ano_music_round(out[h].core.dur + ev.core.dur, 10);
                if (ev.core.tie == ANO_MUSIC_TIE_IN)
                    open[l][p] = -1; // the chain closed
                continue;
            }
        }
        if (ev.core.tie == ANO_MUSIC_TIE_OUT || ev.core.tie == ANO_MUSIC_TIE_BOTH) {
            ev.core.tie = ANO_MUSIC_TIE_NONE;
            open[l][p] = (int)m;
        }
        out[m++] = ev;
    }
    return m;
}

AnoChord ano_ctx_chord_at(const AnoHarmonicContext *ctx, double beatOffset)
{
    AnoChord current = ctx->chord;
    for (uint32_t i = 0; i < ctx->chordSpanCount; ++i)
        if (ctx->chords[i].off <= beatOffset + 1e-9)
            current = ctx->chords[i].chord;
    return current;
}

const char *const ANO_PATCH_NAMES[ANO_PATCH_COUNT] = {
    "", "warm", "bright", "round", "driven", "soft", "hard", "pluck", "glass",
};

AnoGenParams ano_gen_params_default(void)
{
    AnoGenParams p = {
        .tempoBpm = 100.0,
        .noteDensity = 0.5,
        .roughness = 0.0,
        .articulation = 0.9,
        .velocityCenter = 80,
        .accentDepth = 12,
        .registerCenter = 72,
        .layers = { ANO_MUSIC_PAD, ANO_MUSIC_BASS },
        .layerCount = 2,
        .harmonicRhythm = 1.0,
        .dissonanceBudget = 0.0,
        .cadencePolicy = ANO_CADENCE_AUTHENTIC,
        .texture = ANO_TEX_NONE,
        .filterCutoff = 2500.0,
        .reverbSend = 0.20,
        .delaySend = 0.10,
        .drive = 0.15,
        .stereoWidth = 0.70,
    };
    p.instruments[ANO_MUSIC_PAD]    = ANO_PATCH_WARM;
    p.instruments[ANO_MUSIC_BASS]   = ANO_PATCH_ROUND;
    p.instruments[ANO_MUSIC_MELODY] = ANO_PATCH_SOFT;
    p.instruments[ANO_MUSIC_ARP]    = ANO_PATCH_PLUCK;
    return p;
}
