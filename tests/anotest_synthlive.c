/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Live synth path: batch score vs bar-by-bar stream from the block loop must be bit-identical.
// Also: lookahead driver has no late/overflow; starved driver increments late counter.
// Exit 0 == pass.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>
#include <anoptic_synth.h>

#include "music/music_conductor.h"

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define RATE  48000u
#define BARS  24u
#define TAIL  2.0f

// Same seed + config for both paths.
static void engine_config(AnoEngineConfig *cfg)
{
    *cfg = ano_engine_config_default();
    cfg->hasMapper = true;
    cfg->mapper = ano_mapping_table_default();
    cfg->hasDramaturg = true;
    cfg->dramaturg = ano_dramaturg_config_default();
    cfg->phraseGroove = true;
    cfg->cadenceRit = 0.02;
    cfg->form.cadential64 = cfg->form.periods = cfg->form.hypermeter = true;
    cfg->form.bassInversions = true;
    cfg->texture.doubling = cfg->texture.animate = cfg->texture.rotate = true;
    cfg->ties.anacrusis = cfg->ties.suspension = cfg->ties.syncopation = true;
    cfg->melody.planApex = cfg->melody.counterpoint = true;
}

// One conductor bar in synth public shapes.
typedef struct BarFeed
{
    AnoMusicalParams params;
    AnoMusicAffect   affect;
    AnoTempoPoint    tempo[8];
    uint32_t         tempoCount;
    AnoNoteEvent     events[ANO_BAR_MAX_EVENTS];
    uint32_t         eventCount;
} BarFeed;

static void capture(AnoBarResult *r, BarFeed *f)
{
    f->params = ano_gen_params_bridge(&r->params);
    f->affect = ano_affect_bridge(r->affect);
    f->tempoCount = r->tempoPointCount;
    for (uint32_t i = 0; i < r->tempoPointCount; ++i)
        f->tempo[i] = (AnoTempoPoint){ r->tempoPoints[i].beat, r->tempoPoints[i].bpm };
    f->eventCount = r->eventCount;
    for (uint32_t i = 0; i < r->eventCount; ++i)
        f->events[i] = r->events[i].core;
}

// --- live driver: top schedule to LOOKAHEAD, then render -------------------
typedef struct LiveDriver
{
    AnoSynth      *synth;
    AnoMusicEngine engine;
    AnoBarResult   result;
    uint32_t       nextBar;
    uint32_t       maxBars;
    uint32_t       lookahead;
    uint32_t       stall;  // top up every Nth block (0 = every block)
    uint32_t       blocks; // one pending bar still continues in time; lateness needs schedule dry
} LiveDriver;

static void top_up(LiveDriver *d, uint64_t startFrame)
{
    while (d->nextBar < d->maxBars
           && ano_synth_live_pending(d->synth, startFrame) < d->lookahead) {
        ano_engine_advance_bar(&d->engine, &d->result);
        BarFeed f;
        capture(&d->result, &f);
        ano_synth_live_bar(d->synth, d->nextBar, f.tempo, f.tempoCount, &f.params,
                           &f.affect, f.events, f.eventCount);
        d->nextBar++;
    }
}

static void live_generator(void *user, float *const *busMix, uint32_t busCount,
                           uint32_t frames, uint64_t startFrame)
{
    LiveDriver *d = user;
    if (!d->stall || d->blocks % d->stall == 0u)
        top_up(d, startFrame); // schedule ahead, then render
    d->blocks++;
    ano_synth_generator(d->synth, busMix, busCount, frames, startFrame);
}

int main(void)
{
    AnoAudioBusDesc buses[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(buses, ANO_SYNTH_CONSOLE_BUSES);
    CHECK(busCount == ANO_SYNTH_CONSOLE_BUSES, "console layout");

    AnoSynthDesc sd = { .sampleRate = RATE, .maxVoices = 64 };
    AnoEngineConfig cfg;
    engine_config(&cfg);

    // --- capture once (both paths replay the same bars) ---
    static BarFeed feed[BARS];
    static AnoMusicEngine eng;
    static AnoBarResult res;
    uint32_t totalEvents = 0, totalTempo = 0;
    ano_engine_init(&eng, 42, &cfg);
    for (uint32_t b = 0; b < BARS; ++b) {
        ano_engine_advance_bar(&eng, &res);
        capture(&res, &feed[b]);
        totalEvents += feed[b].eventCount;
        totalTempo += feed[b].tempoCount;
    }
    CHECK(totalEvents > 200u, "the piece has substance");

    // --- A: batch path ---
    AnoSynth *batch = ano_synth_create(&sd);
    CHECK(batch != NULL, "batch synth");
    CHECK(ano_synth_score_begin(batch, 4.0, BARS, totalTempo, totalEvents),
          "score_begin");
    for (uint32_t b = 0; b < BARS; ++b)
        for (uint32_t i = 0; i < feed[b].tempoCount; ++i)
            CHECK(ano_synth_score_tempo(batch, feed[b].tempo[i].beat,
                                        feed[b].tempo[i].bpm),
                  "score_tempo");
    for (uint32_t b = 0; b < BARS; ++b)
        CHECK(ano_synth_score_bar(batch, b, &feed[b].params, &feed[b].affect),
              "score_bar");
    for (uint32_t b = 0; b < BARS; ++b)
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
        .blockFrames = 512,
        .busCount = busCount,
        .busLayout = buses,
        .generator = ano_synth_generator,
        .generatorUser = batch,
    };
    CHECK(ano_audio_render_offline(&od, bufA, frames), "batch render");
    CHECK(ano_synth_dropped(batch) == 0u, "batch dropped no voices");

    // --- B: live path from the block loop ---
    AnoSynth *live = ano_synth_create(&sd);
    CHECK(live != NULL, "live synth");
    static LiveDriver drv;
    drv.synth = live;
    drv.nextBar = 0;
    drv.maxBars = BARS;
    drv.lookahead = ANO_SYNTH_LIVE_LOOKAHEAD;
    drv.stall = 0;
    drv.blocks = 0;
    ano_engine_init(&drv.engine, 42, &cfg); // same seed

    CHECK(ano_synth_live_begin(live, 4.0), "live_begin");
    // Prime lookahead before transport start.
    while (drv.nextBar < ANO_SYNTH_LIVE_LOOKAHEAD) {
        ano_engine_advance_bar(&drv.engine, &drv.result);
        BarFeed f;
        capture(&drv.result, &f);
        CHECK(ano_synth_live_bar(live, drv.nextBar, f.tempo, f.tempoCount, &f.params,
                                 &f.affect, f.events, f.eventCount),
              "live_bar prime");
        drv.nextBar++;
    }
    ano_synth_transport_start(live, 0);

    AnoAudioOfflineDesc ol = od;
    ol.generator = live_generator;
    ol.generatorUser = &drv;
    CHECK(ano_audio_render_offline(&ol, bufB, frames), "live render");

    CHECK(drv.nextBar == BARS, "live streamed every bar");
    CHECK(ano_synth_live_late(live) == 0u, "no tie arrived late");
    CHECK(ano_synth_live_overflow(live) == 0u, "no note overflowed the ring");
    CHECK(ano_synth_dropped(live) == 0u, "live dropped no voices");

    // --- equivalence ---
    size_t samples = (size_t)frames * ANO_AUDIO_CHANNELS;
    size_t diff = 0;
    double worst = 0.0;
    for (size_t i = 0; i < samples; ++i)
        if (bufA[i] != bufB[i]) {
            diff++;
            double d = fabs((double)bufA[i] - (double)bufB[i]);
            if (d > worst)
                worst = d;
        }
    if (diff)
        printf("  live vs batch: %zu/%zu samples differ, worst %.9f\n", diff, samples,
               worst);
    CHECK(diff == 0, "live renders BIT-IDENTICAL to batch");

    // Audible, not silence.
    double peak = 0.0;
    for (size_t i = 0; i < samples; ++i)
        if (fabs((double)bufA[i]) > peak)
            peak = fabs((double)bufA[i]);
    CHECK(peak > 0.01, "the render is audible");

    // --- starved driver: late counter fires ---
    // stall=900 (~9.6 s) > 2 pending bars (~4.8 s): schedule runs dry, late > 0.
    AnoSynth *starved = ano_synth_create(&sd);
    static LiveDriver sd2;
    sd2.synth = starved;
    sd2.nextBar = 0;
    sd2.maxBars = BARS;
    sd2.lookahead = ANO_SYNTH_LIVE_LOOKAHEAD;
    sd2.stall = 900u;  // ~9.6 s: longer than the 2 pending bars (~4.8 s)
    sd2.blocks = 0;
    ano_engine_init(&sd2.engine, 42, &cfg);
    CHECK(ano_synth_live_begin(starved, 4.0), "starved live_begin");
    ano_engine_advance_bar(&sd2.engine, &sd2.result);
    BarFeed f0;
    capture(&sd2.result, &f0);
    ano_synth_live_bar(starved, 0, f0.tempo, f0.tempoCount, &f0.params, &f0.affect,
                       f0.events, f0.eventCount);
    sd2.nextBar = 1;
    ano_synth_transport_start(starved, 0);
    AnoAudioOfflineDesc os = od;
    os.generator = live_generator;
    os.generatorUser = &sd2;
    CHECK(ano_audio_render_offline(&os, bufB, frames), "starved render");
    CHECK(ano_synth_live_late(starved) > 0u,
          "a starved driver is reported, not silently wrong");

    free(bufA);
    free(bufB);
    ano_synth_destroy(batch);
    ano_synth_destroy(live);
    ano_synth_destroy(starved);

    if (failures) {
        printf("anotest_synthlive: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_synthlive: all passed\n");
    return 0;
}
