/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composer in the audio callback (ano_synth_attach_music): sample-identical to batch over shared bars.
// Also: endless compose past score, meaning on downbeat, ACMD_MUSIC_* steering, seek rebase.
// Exit 0 == pass.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>
#include <anoptic_music.h>
#include <anoptic_synth.h>

static int failures = 0;
#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            failures++;                                              \
        }                                                            \
    } while (0)

#define RATE   48000u
#define BARS   24u // shared compare window
#define BLOCK  512u
#define TAIL   2.0f
#define BQ     4.0 // 4/4

// Batch = BARS + LOOKAHEAD: a cut score dissolves barline ties (early release) and fails the shared-window compare.
#define SCORE_BARS (BARS + ANO_SYNTH_LIVE_LOOKAHEAD)

// Everything-on schedule (ties, cadence rits, elisions).
static void public_config(AnoMusicConfig *c)
{
    *c = ano_music_config_default();
    c->hasMapper = true;
    c->mapper = ano_mapping_table_default();
    c->hasDramaturg = true;
    c->dramaturg = ano_dramaturg_config_default();
    c->phraseGroove = true;
    c->cadenceRit = 0.02;
    c->wanderPhrases = 4;
    c->form.cadential64 = c->form.periods = c->form.hypermeter = true;
    c->form.bassInversions = c->form.split64 = true;
    c->texture.doubling = c->texture.animate = c->texture.imitation = true;
    c->texture.rotate = c->texture.counter = true;
    c->ties.anacrusis = c->ties.suspension = c->ties.syncopation = true;
    c->clock.codetta = c->clock.extension = c->clock.elision = true;
    c->melody.planApex = c->melody.counterpoint = true;
}

// Mixer stand-in: render, then drain bar/seek events.
typedef struct Drive
{
    AnoSynth       *synth;
    AnoMusicEngine *music;
    AnoAudioEvent   got[64];   // AEVT_MUSIC_BAR / AEVT_MUSIC_SEEKED order
    uint64_t        gotAt[64]; // block startFrame when each surfaced
    uint32_t        gotCount;
} Drive;

static void drive_generator(void *user, float *const *busMix, uint32_t busCount,
                            uint32_t frames, uint64_t startFrame)
{
    Drive *d = user;
    ano_synth_generator(d->synth, busMix, busCount, frames, startFrame);

    AnoAudioEvent e[8];
    uint32_t n = ano_synth_poll(d->synth, e, 8);
    for (uint32_t i = 0; i < n && d->gotCount < 64; ++i) {
        d->gotAt[d->gotCount] = startFrame;
        d->got[d->gotCount++] = e[i];
    }
}

// Same user ptr for generator + control (Drive wraps both; synth refuses wrong magic).
static void drive_control(void *user, const AnoAudioCommand *cmd)
{
    Drive *d = user;
    ano_synth_control(d->synth, cmd);
}

// Driven render: fresh synth + engine. cmds = ACMD_MUSIC_* at frame stamps.
// wireControl false: mixer gets cmds but does not interpret them.
static void render_driven_from(const AnoMusicConfig *cfg, const AnoAudioOfflineDesc *base,
                               float *buf, uint64_t frames, const AnoAudioOfflineEvent *cmds,
                               uint32_t cmdCount, bool wireControl, uint32_t startBar,
                               Drive *d)
{
    AnoSynthDesc sd = { .sampleRate = RATE, .maxVoices = 64 };
    memset(d, 0, sizeof *d);
    d->synth = ano_synth_create(&sd);
    d->music = ano_music_create(cfg, 42);
    for (uint32_t b = 0; b < startBar; ++b) { // off-thread seek half
        static AnoMusicBar skip;
        ano_music_advance_bar(d->music, &skip);
    }
    CHECK(ano_synth_attach_music(d->synth, d->music), "attach");
    ano_synth_transport_start(d->synth, 0);

    AnoAudioOfflineDesc od  = *base;
    od.generator            = drive_generator;
    od.generatorUser        = d;
    od.events               = cmds;
    od.eventCount           = cmdCount;
    od.generatorControl     = wireControl ? drive_control : NULL;
    CHECK(ano_audio_render_offline(&od, buf, frames), "driven render");
}

static void render_driven(const AnoMusicConfig *cfg, const AnoAudioOfflineDesc *base,
                          float *buf, uint64_t frames, const AnoAudioOfflineEvent *cmds,
                          uint32_t cmdCount, bool wireControl, Drive *d)
{
    render_driven_from(cfg, base, buf, frames, cmds, cmdCount, wireControl, 0, d);
}

static void drive_free(Drive *d)
{
    ano_synth_destroy(d->synth);
    ano_music_destroy(d->music);
}

int main(void)
{
    AnoAudioBusDesc buses[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(buses, ANO_SYNTH_CONSOLE_BUSES);
    CHECK(busCount == ANO_SYNTH_CONSOLE_BUSES, "console layout");

    AnoSynthDesc sd = { .sampleRate = RATE, .maxVoices = 64 };
    AnoMusicConfig cfg;
    public_config(&cfg);

    // --- batch reference capture ---
    static AnoMusicBar feed[SCORE_BARS];
    uint32_t totalEvents = 0, totalTempo = 0;
    AnoMusicEngine *capture = ano_music_create(&cfg, 42);
    CHECK(capture != NULL, "music engine");
    for (uint32_t b = 0; b < SCORE_BARS; ++b) {
        ano_music_advance_bar(capture, &feed[b]);
        totalEvents += feed[b].eventCount;
        totalTempo += feed[b].tempoCount;
    }
    ano_music_destroy(capture);
    CHECK(totalEvents > 200u, "the piece has substance");

    // --- A: batch path (whole score idle) ---
    AnoSynth *batch = ano_synth_create(&sd);
    CHECK(ano_synth_score_begin(batch, BQ, SCORE_BARS, totalTempo, totalEvents),
          "score_begin");
    for (uint32_t b = 0; b < SCORE_BARS; ++b)
        for (uint32_t i = 0; i < feed[b].tempoCount; ++i)
            CHECK(ano_synth_score_tempo(batch, feed[b].tempo[i].beat,
                                        feed[b].tempo[i].bpm),
                  "score_tempo");
    for (uint32_t b = 0; b < SCORE_BARS; ++b)
        CHECK(ano_synth_score_bar(batch, b, &feed[b].params, &feed[b].affect),
              "score_bar");
    for (uint32_t b = 0; b < SCORE_BARS; ++b)
        for (uint32_t i = 0; i < feed[b].eventCount; ++i)
            CHECK(ano_synth_score_event(batch, &feed[b].events[i]), "score_event");
    CHECK(ano_synth_score_end(batch), "score_end");

    uint64_t frames = ano_synth_score_frames(batch, TAIL);
    CHECK(frames > RATE, "score has length");
    ano_synth_transport_start(batch, 0);

    float *bufA = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufA);
    float *bufB = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufB);
    CHECK(bufA && bufB, "render buffers");

    AnoAudioOfflineDesc od = {
        .sampleRate = RATE,
        .blockFrames = BLOCK,
        .busCount = busCount,
        .busLayout = buses,
        .generator = ano_synth_generator,
        .generatorUser = batch,
    };
    CHECK(ano_audio_render_offline(&od, bufA, frames), "batch render");

    // --- B: composer in the callback ---
    static Drive drv;
    render_driven(&cfg, &od, bufB, frames, NULL, 0, true, &drv);
    AnoSynth       *drivenSynth = drv.synth;
    AnoMusicEngine *music       = drv.music;

    CHECK(ano_synth_live_late(drivenSynth) == 0u, "no tie arrived late");
    CHECK(ano_synth_live_overflow(drivenSynth) == 0u, "no note overflowed the ring");
    CHECK(ano_synth_dropped(drivenSynth) == 0u, "no voice was dropped");

    // --- equivalence over shared bars (cut at BARS; SCORE_BARS has lookahead) ---
    uint64_t cut = (uint64_t)(ano_synth_time_at(batch, (double)BARS * BQ) * RATE);
    CHECK(cut > 0 && cut <= frames, "the shared span is the score");

    size_t samples = (size_t)cut * ANO_AUDIO_CHANNELS;
    size_t diff = 0;
    double worst = 0.0;
    for (size_t i = 0; i < samples; ++i) {
        double d = fabs((double)bufA[i] - (double)bufB[i]);
        if (bufA[i] != bufB[i])
            diff++;
        if (d > worst)
            worst = d;
    }
    if (diff) {
        size_t first = samples;
        for (size_t i = 0; i < samples && first == samples; ++i)
            if (bufA[i] != bufB[i])
                first = i;
        // First-diff frame: late-window tip = truncation; elsewhere = live diverge.
        printf("  %zu of %zu samples differ, worst %.3g, first at frame %zu of %llu\n",
               diff, samples, worst, first / ANO_AUDIO_CHANNELS,
               (unsigned long long)cut);
    }
    CHECK(diff == 0, "hosting the composer in the callback is sample-identical");

    double rms = 0.0;
    for (size_t i = 0; i < samples; ++i)
        rms += (double)bufB[i] * (double)bufB[i];
    CHECK(sqrt(rms / (double)samples) > 0.01, "and it is audible");

    // --- piece does not end ---
    CHECK(ano_music_next_bar(music) > (int)SCORE_BARS, "the composer ran past the score");

    // --- meaning lands on its own downbeat ---
    CHECK(drv.gotCount >= BARS, "every bar reported its meaning");
    bool ordered = true, timely = true;
    for (uint32_t k = 0; k < drv.gotCount; ++k) {
        if (drv.got[k].kind != AEVT_MUSIC_BAR || drv.got[k].u.music.bar != (int)k)
            ordered = false;
        if (k < BARS) {
            uint64_t downbeat =
                (uint64_t)(ano_synth_time_at(batch, (double)k * BQ) * RATE);
            // Composed up to LOOKAHEAD earlier; surfaced in the block that crosses its downbeat.
            if (!(downbeat >= drv.gotAt[k] && downbeat < drv.gotAt[k] + BLOCK))
                timely = false;
        }
    }
    CHECK(ordered, "meanings arrive in bar order, none skipped");
    CHECK(timely, "a bar's meaning surfaces in the block its downbeat falls in");

    uint32_t cadences = 0;
    for (uint32_t k = 0; k < drv.gotCount; ++k)
        cadences += drv.got[k].u.music.isCadence;
    CHECK(cadences > 1u, "cadences reached the game");

    // --- callback cost vs block budget ---
    uint32_t blockUs = (uint32_t)((uint64_t)BLOCK * 1000000u / RATE);
    uint32_t worstUs = ano_synth_music_bar_us_max(drivenSynth);
    printf("  bar composition: worst %u us of the %u us block (%.1f%%)\n", worstUs,
           blockUs, 100.0 * (double)worstUs / (double)blockUs);
    CHECK(worstUs < blockUs, "composing a bar fits inside one block");

    // Same number in telemetry.
    AnoAudioTelemetry tel = { 0 };
    ano_synth_stats(drivenSynth, &tel);
    CHECK(tel.genUsMax == worstUs, "the composer's cost reaches the telemetry frame");
    CHECK(tel.genLate == 0u && tel.genDropped == 0u, "and it reports nothing lost");

    // Wrong user ptr refused (hooks share one ptr).
    AnoAudioEvent spill[4];
    CHECK(ano_synth_poll(&drv, spill, 4) == 0u, "a user pointer that is not a synth is refused");

    // --- control plane: ACMD_MUSIC_* via offline command list ---
    {
        static Drive steered;
        float *bufC = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufC);
        CHECK(bufC != NULL, "steer buffer");

        // Away from baseline tonic so arrival is unambiguous.
        int home = drv.got[0].u.music.keyTonic;
        int away = (home + 7) % 12; // dominant
        AnoAudioOfflineEvent cmds[] = {
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_KEY, .paramId = (uint32_t)away,
                                       .urgent = true } },
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_OVERRIDE, .tag = "reverb_send",
                                       .value = 0.9f } },
            // Unknown tag: refused + logged.
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_OVERRIDE, .tag = "revreb_send",
                                       .value = 0.9f } },
        };
        render_driven(&cfg, &od, bufC, frames, cmds, 3, true, &steered);

        bool arrived = false, marked = false;
        for (uint32_t k = 0; k < steered.gotCount; ++k) {
            if (steered.got[k].u.music.keyTonic == away)
                arrived = true;
            if (steered.got[k].u.music.keyArrived && steered.got[k].u.music.keyTonic == away)
                marked = true;
        }
        CHECK(arrived, "the requested key arrived");
        CHECK(marked, "and the bar it landed on says so");
        CHECK(steered.got[0].u.music.keyTonic == home, "it was not already there");

        // Different key -> different audio.
        size_t sdiff = 0;
        for (size_t i = 0; i < samples; ++i)
            if (bufB[i] != bufC[i])
                sdiff++;
        CHECK(sdiff > samples / 10, "steering changed what is heard");

        // No control hook: same cmds change nothing (mixer does not interpret).
        static Drive ignored;
        float *bufD = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufD);
        render_driven(&cfg, &od, bufD, frames, cmds, 3, false, &ignored);
        CHECK(memcmp(bufB, bufD, (size_t)frames * ANO_AUDIO_CHANNELS * sizeof *bufB) == 0,
              "with no control hook, the mixer changes no music of its own");

        free(bufC);
        free(bufD);
        drive_free(&steered);
        drive_free(&ignored);
    }

    // --- seek: off-thread snapshot + callback rebase ---
    // Seek-at-0 == attach engine already at SEEK_TO (sample-identical).
#define SEEK_TO 40u
    {
        static Drive seeked, direct;
        float *bufE = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufE);
        float *bufF = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufF);
        CHECK(bufE && bufF, "seek buffers");

        // Producer: fast-forward + snapshot.
        static AnoMusicBar skip;
        void *snap = malloc(ano_music_snapshot_size());
        AnoMusicEngine *off = ano_music_create(&cfg, 42);
        for (uint32_t b = 0; b < SEEK_TO; ++b)
            ano_music_advance_bar(off, &skip);
        CHECK(ano_music_snapshot(off, snap, ano_music_snapshot_size()), "snapshot");

        AnoAudioOfflineEvent seekAt0[] = {
            { .frame = 0, .cmd = { .kind = ACMD_MUSIC_SEEK, .block = snap } },
        };
        render_driven(&cfg, &od, bufE, frames, seekAt0, 1, true, &seeked);
        render_driven_from(&cfg, &od, bufF, frames, NULL, 0, true, SEEK_TO, &direct);

        CHECK(memcmp(bufE, bufF, (size_t)frames * ANO_AUDIO_CHANNELS * sizeof *bufE) == 0,
              "a seek to bar N is sample-identical to attaching an engine at bar N");
        CHECK(seeked.gotCount > 2u && direct.gotCount > 2u, "both played");

        // SEEKED before any seeked music; bar numbering continues from engine.
        CHECK(seeked.got[0].kind == AEVT_MUSIC_SEEKED, "the seek acknowledged first");
        CHECK(seeked.got[0].u.seekedBar == (int)SEEK_TO, "at the bar it was given");
        CHECK(seeked.got[1].kind == AEVT_MUSIC_BAR
                  && seeked.got[1].u.music.bar == (int)SEEK_TO,
              "and the music that follows IS bar SEEK_TO");
        bool numbered = true;
        for (uint32_t k = 1; k < seeked.gotCount; ++k)
            if (seeked.got[k].kind != AEVT_MUSIC_BAR
                || seeked.got[k].u.music.bar != (int)(SEEK_TO + k - 1u))
                numbered = false;
        CHECK(numbered, "the bars keep counting from where the engine was");
        CHECK(ano_synth_live_late(seeked.synth) == 0u
                  && ano_synth_live_overflow(seeked.synth) == 0u
                  && ano_synth_dropped(seeked.synth) == 0u,
              "the seek cost nothing: no lateness, no overflow, no dropped voice");

        // --- mid-flight: pre-seek audio untouched ---
        static Drive jumped;
        float *bufG = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufG);
        uint64_t at = (uint64_t)(ano_synth_time_at(batch, 6.0 * BQ) * RATE) - BLOCK;
        AnoAudioOfflineEvent seekMid[] = {
            { .frame = at, .cmd = { .kind = ACMD_MUSIC_SEEK, .block = snap } },
        };
        render_driven(&cfg, &od, bufG, frames, seekMid, 1, true, &jumped);

        CHECK(memcmp(bufB, bufG, (size_t)at * ANO_AUDIO_CHANNELS * sizeof *bufB) == 0,
              "a seek does not disturb what has already sounded");

        // Post-seek bars are the new piece on already-scheduled barlines.
        int jump = -1;
        for (uint32_t k = 0; k < jumped.gotCount; ++k)
            if (jumped.got[k].kind == AEVT_MUSIC_SEEKED)
                jump = (int)k;
        CHECK(jump > 0, "the mid-flight seek landed");
        CHECK(jumped.got[jump + 1].u.music.bar == (int)SEEK_TO,
              "the next bar to sound is the seeked one");
        CHECK(jumped.got[jump - 1].u.music.bar < (int)SEEK_TO,
              "and the one before it was still the old piece");

        // First seeked bar on the old schedule's barline; after that, new tempo map.
        uint64_t barline = (uint64_t)(ano_synth_time_at(batch, (double)jump * BQ) * RATE);
        CHECK(barline >= jumped.gotAt[jump + 1] && barline < jumped.gotAt[jump + 1] + BLOCK,
              "the seeked bar lands on the barline the old schedule had already fixed");
        CHECK(ano_synth_live_late(jumped.synth) == 0u
                  && ano_synth_dropped(jumped.synth) == 0u,
              "the mid-flight seek dropped nothing either");

        // --- other meter refused; running music untouched ---
        static Drive kept;
        float *bufH = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufH);
        AnoMusicConfig waltz = cfg;
        waltz.meter = (AnoMeter){ 3, 4 };
        AnoMusicEngine *w = ano_music_create(&waltz, 42);
        void *snapW = malloc(ano_music_snapshot_size());
        CHECK(ano_music_snapshot(w, snapW, ano_music_snapshot_size()), "waltz snapshot");
        AnoAudioOfflineEvent seekWaltz[] = {
            { .frame = at, .cmd = { .kind = ACMD_MUSIC_SEEK, .block = snapW } },
        };
        render_driven(&cfg, &od, bufH, frames, seekWaltz, 1, true, &kept);

        bool refused = true;
        for (uint32_t k = 0; k < kept.gotCount; ++k)
            if (kept.got[k].kind == AEVT_MUSIC_SEEKED)
                refused = false;
        CHECK(refused, "a snapshot in another meter is refused");
        CHECK(memcmp(bufB, bufH, (size_t)frames * ANO_AUDIO_CHANNELS * sizeof *bufB) == 0,
              "and the music that was playing is untouched");

        free(bufH);
        free(snapW);
        ano_music_destroy(w);
        drive_free(&kept);

        free(bufE);
        free(bufF);
        free(bufG);
        free(snap);
        ano_music_destroy(off);
        drive_free(&seeked);
        drive_free(&direct);
        drive_free(&jumped);
    }

    free(bufA);
    free(bufB);
    ano_synth_destroy(batch);
    drive_free(&drv);

    if (failures) {
        printf("anotest_musicdrive: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musicdrive: all passed\n");
    return 0;
}
