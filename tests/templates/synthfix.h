/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// .anofix score loader (export_fixture.py format): header, tempo, bars, events -> ano_synth_score_*.
// Header-only template; no CMake registration.

#ifndef ANOTEST_SYNTHFIX_H
#define ANOTEST_SYNTHFIX_H

#include <stdio.h>

#include <anoptic_synth.h>

// Load path into s. True on complete well-formed fixture.
static bool synthfix_load(AnoSynth *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("synthfix: cannot open %s\n", path);
        return false;
    }
    bool ok = false;
    uint32_t version = 0, bars = 0, events = 0, tempo = 0;
    double meter = 0.0;
    if (fscanf(f, "anosynthfix %u meter %lf bars %u events %u tempo %u",
               &version, &meter, &bars, &events, &tempo) != 5 || version != 1u)
        goto out;
    if (!ano_synth_score_begin(s, meter, bars, tempo, events))
        goto out;
    for (uint32_t i = 0; i < tempo; ++i) {
        double beat, bpm;
        if (fscanf(f, " t %lf %lf", &beat, &bpm) != 2
            || !ano_synth_score_tempo(s, beat, bpm))
            goto out;
    }
    for (uint32_t i = 0; i < bars; ++i) {
        uint32_t idx;
        AnoMusicAffect a;
        double bpm;
        float cut, rev, dly, drv, wid;
        char pad[32], bass[32], melody[32], arp[32];
        if (fscanf(f, " bar %u %f %f %f %lf %f %f %f %f %f %31s %31s %31s %31s",
                   &idx, &a.valence, &a.energy, &a.tension, &bpm,
                   &cut, &rev, &dly, &drv, &wid, pad, bass, melody, arp) != 14)
            goto out;
        AnoMusicalParams p = {
            .tempoBpm = bpm, .filterCutoff = cut, .reverbSend = rev,
            .delaySend = dly, .drive = drv, .stereoWidth = wid,
        };
        p.instruments[ANO_MUSIC_PAD]    = (uint16_t)ano_music_patch_id(pad);
        p.instruments[ANO_MUSIC_BASS]   = (uint16_t)ano_music_patch_id(bass);
        p.instruments[ANO_MUSIC_MELODY] = (uint16_t)ano_music_patch_id(melody);
        p.instruments[ANO_MUSIC_ARP]    = (uint16_t)ano_music_patch_id(arp);
        if (!ano_synth_score_bar(s, idx, &p, &a))
            goto out;
    }
    for (uint32_t i = 0; i < events; ++i) {
        double start, dur;
        uint32_t pitch, vel, layer, tie;
        if (fscanf(f, " e %lf %lf %u %u %u %u", &start, &dur, &pitch, &vel, &layer, &tie) != 6)
            goto out;
        AnoNoteEvent ev = { .start = start, .dur = dur, .pitch = (uint8_t)pitch,
                            .velocity = (uint8_t)vel, .layer = (uint8_t)layer,
                            .tie = (uint8_t)tie };
        if (!ano_synth_score_event(s, &ev))
            goto out;
    }
    ok = ano_synth_score_end(s);
out:
    if (!ok)
        printf("synthfix: parse failed in %s\n", path);
    fclose(f);
    return ok;
}

#endif // ANOTEST_SYNTHFIX_H
