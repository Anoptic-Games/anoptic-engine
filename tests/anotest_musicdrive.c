/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Phase 7: the composer hosted INSIDE the audio callback (ano_synth_attach_music).
 *
 * The claim being tested is that this changes nothing. The batch path — the
 * whole piece loaded while idle, then played — is the proven one; attaching a
 * music engine moves composition into the block loop, two bars ahead of the
 * playhead, and the audio must come out sample-identical over the bars the two
 * share. If in-thread composition perturbed the schedule at all (a tempo point
 * arriving after a note was stamped, a tie not yet extendable, a bar edge
 * landing in the wrong span), it would show up here as a differing sample.
 *
 * Then the things only the live path can be asked:
 *   - the piece does not end. The batch score is BARS long; the attached one
 *     keeps composing past it, for as long as the transport runs.
 *   - a bar's MEANING arrives when the bar SOUNDS, not when it was composed.
 *     The generator runs ahead; a game that flinches at a cadence must flinch
 *     on the cadence, so the meaning is held to its own downbeat.
 *   - the game can STEER it. ACMD_MUSIC_* ride the offline command list here, so
 *     the steering is reproducible and the music's answer can be asserted rather
 *     than listened for — and with no control hook wired the same commands change
 *     nothing, which is what proves the mixer never interprets them itself.
 *
 * Exit 0 == pass. */

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
#define BARS   24u // the bars the two renders are compared over
#define BLOCK  512u
#define TAIL   2.0f
#define BQ     4.0 // 4/4

// The batch reference must be LONGER than the window it is compared over. A
// score that simply STOPS at bar N has different music in bar N-1: a note tied
// out of it finds no continuation and dissolves into a plain note, and since
// articulation gates the release at a fraction of the duration, the shortened
// note starts releasing BEFORE the barline. The driven engine, which never
// stops, extends the note instead. So the reference carries the same lookahead
// the live path does, and the truncation artifact is pushed clear of the window.
#define SCORE_BARS (BARS + ANO_SYNTH_LIVE_LOOKAHEAD)

// Everything on: the richest schedule the engine can produce (ties across the
// barline, mid-bar tempo dips into cadences, elisions) — the cases where
// composing one bar at a time is hardest to make identical to composing all of
// them at once.
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

// The mixer's job, in miniature: render, then drain what started sounding.
typedef struct Drive
{
    AnoSynth       *synth;
    AnoMusicEngine *music;
    AnoMusicMeaning got[64];
    uint64_t        gotAt[64]; // the block in which each surfaced
    uint32_t        gotCount;
} Drive;

static void drive_generator(void *user, float *const *busMix, uint32_t busCount,
                            uint32_t frames, uint64_t startFrame)
{
    Drive *d = user;
    ano_synth_generator(d->synth, busMix, busCount, frames, startFrame);

    AnoMusicMeaning m[8];
    uint32_t n = ano_synth_music_drain(d->synth, m, 8);
    for (uint32_t i = 0; i < n && d->gotCount < 64; ++i) {
        d->gotAt[d->gotCount] = startFrame;
        d->got[d->gotCount++] = m[i];
    }
}

// The hooks all take the SAME user pointer. This test wraps the generator (to
// drain the meanings as a mixer would), so it must wrap the control hook too and
// hand the synth on — passing the Drive straight to ano_synth_control is exactly
// the mistake the synth's magic check now refuses.
static void drive_control(void *user, const AnoAudioCommand *cmd)
{
    Drive *d = user;
    ano_synth_control(d->synth, cmd);
}

// One driven render, from a fresh synth and a fresh engine on the same seed.
// `cmds` are the producer's ACMD_MUSIC_* stamped with the frame they land on —
// what ano_audio_submit would deliver at a block boundary, made reproducible.
// wireControl = false leaves generatorControl NULL: the commands still reach the
// mixer, which is what proves the mixer does not interpret them itself.
static void render_driven(const AnoMusicConfig *cfg, const AnoAudioOfflineDesc *base,
                          float *buf, uint64_t frames, const AnoAudioOfflineEvent *cmds,
                          uint32_t cmdCount, bool wireControl, Drive *d)
{
    AnoSynthDesc sd = { .sampleRate = RATE, .maxVoices = 64 };
    memset(d, 0, sizeof *d);
    d->synth = ano_synth_create(&sd);
    d->music = ano_music_create(cfg, 42);
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

    // --- the same music, captured once for the batch reference ---------------
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

    // --- A: the proven path — the whole score, loaded while idle -------------
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

    // --- B: the composer in the callback -------------------------------------
    static Drive drv;
    render_driven(&cfg, &od, bufB, frames, NULL, 0, true, &drv);
    AnoSynth       *drivenSynth = drv.synth;
    AnoMusicEngine *music       = drv.music;

    CHECK(ano_synth_live_late(drivenSynth) == 0u, "no tie arrived late");
    CHECK(ano_synth_live_overflow(drivenSynth) == 0u, "no note overflowed the ring");
    CHECK(ano_synth_dropped(drivenSynth) == 0u, "no voice was dropped");

    // --- the equivalence, over the bars the two scores share -----------------
    // The window ends at bar BARS's downbeat: the reference runs LOOKAHEAD bars
    // past it (see SCORE_BARS) so that nothing inside the window is a truncation
    // artifact, and nothing composed for a later bar can sound before it.
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
        // where it starts is the diagnosis: a tiny worst-case confined to the
        // last bars is the reference truncating (see SCORE_BARS), anywhere else
        // is the live schedule genuinely diverging
        printf("  %zu of %zu samples differ, worst %.3g, first at frame %zu of %llu\n",
               diff, samples, worst, first / ANO_AUDIO_CHANNELS,
               (unsigned long long)cut);
    }
    CHECK(diff == 0, "hosting the composer in the callback is sample-identical");

    double rms = 0.0;
    for (size_t i = 0; i < samples; ++i)
        rms += (double)bufB[i] * (double)bufB[i];
    CHECK(sqrt(rms / (double)samples) > 0.01, "and it is audible");

    // --- the piece does not end ----------------------------------------------
    // The batch score was BARS long. The attached engine kept composing through
    // the tail, because nothing told it to stop.
    CHECK(ano_music_next_bar(music) > (int)SCORE_BARS, "the composer ran past the score");

    // --- the meaning lands on its own downbeat -------------------------------
    CHECK(drv.gotCount >= BARS, "every bar reported its meaning");
    bool ordered = true, timely = true;
    for (uint32_t k = 0; k < drv.gotCount; ++k) {
        if (drv.got[k].bar != (int)k)
            ordered = false;
        if (k < BARS) {
            uint64_t downbeat =
                (uint64_t)(ano_synth_time_at(batch, (double)k * BQ) * RATE);
            // composed up to LOOKAHEAD bars earlier; surfaced in the very block
            // that crosses its downbeat
            if (!(downbeat >= drv.gotAt[k] && downbeat < drv.gotAt[k] + BLOCK))
                timely = false;
        }
    }
    CHECK(ordered, "meanings arrive in bar order, none skipped");
    CHECK(timely, "a bar's meaning surfaces in the block its downbeat falls in");

    uint32_t cadences = 0;
    for (uint32_t k = 0; k < drv.gotCount; ++k)
        cadences += drv.got[k].isCadence;
    CHECK(cadences > 1u, "cadences reached the game");

    // --- what it costs the audio thread --------------------------------------
    // The block period is the budget: BLOCK / RATE. This is the whole risk of
    // hosting a composer in the callback, so it is measured, not assumed.
    uint32_t blockUs = (uint32_t)((uint64_t)BLOCK * 1000000u / RATE);
    uint32_t worstUs = ano_synth_music_bar_us_max(drivenSynth);
    printf("  bar composition: worst %u us of the %u us block (%.1f%%)\n", worstUs,
           blockUs, 100.0 * (double)worstUs / (double)blockUs);
    CHECK(worstUs < blockUs, "composing a bar fits inside one block");

    // and that number is what the mixer ships: telemetry is how a game finds out
    AnoAudioTelemetry tel = { 0 };
    ano_synth_stats(drivenSynth, &tel);
    CHECK(tel.genUsMax == worstUs, "the composer's cost reaches the telemetry frame");
    CHECK(tel.genLate == 0u && tel.genDropped == 0u, "and it reports nothing lost");

    // The hooks take a void*, and all four take the SAME one. The wrong pointer
    // must be refused rather than obeyed -- this test made that exact mistake,
    // and it presented as steering that silently never arrived.
    AnoAudioEvent spill[4];
    CHECK(ano_synth_poll(&drv, spill, 4) == 0u, "a user pointer that is not a synth is refused");

    // --- the control plane: steering the composer through the bridge ----------
    // ACMD_MUSIC_* are submitted by the logic thread, forwarded by the mixer
    // (which owns no music) and applied by the synth on the thread that owns the
    // engine. Here they ride the offline command list, so the steering is
    // reproducible and the response can be asserted rather than listened for.
    {
        static Drive steered;
        float *bufC = calloc((size_t)frames * ANO_AUDIO_CHANNELS, sizeof *bufC);
        CHECK(bufC != NULL, "steer buffer");

        // where the baseline is NOT, so the arrival is unambiguous
        int home = drv.got[0].keyTonic;
        int away = (home + 7) % 12; // the dominant: a key it will actually reach
        AnoAudioOfflineEvent cmds[] = {
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_KEY, .paramId = (uint32_t)away,
                                       .urgent = true } },
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_OVERRIDE, .tag = "reverb_send",
                                       .value = 0.9f } },
            // a name nobody defines: REFUSED and logged, never silently ignored
            { .frame = BLOCK, .cmd = { .kind = ACMD_MUSIC_OVERRIDE, .tag = "revreb_send",
                                       .value = 0.9f } },
        };
        render_driven(&cfg, &od, bufC, frames, cmds, 3, true, &steered);

        bool arrived = false, marked = false;
        for (uint32_t k = 0; k < steered.gotCount; ++k) {
            if (steered.got[k].keyTonic == away)
                arrived = true;
            if (steered.got[k].keyArrived && steered.got[k].keyTonic == away)
                marked = true;
        }
        CHECK(arrived, "the requested key arrived");
        CHECK(marked, "and the bar it landed on says so");
        CHECK(steered.got[0].keyTonic == home, "it was not already there");

        // the music genuinely changed: a different key is different audio
        size_t sdiff = 0;
        for (size_t i = 0; i < samples; ++i)
            if (bufB[i] != bufC[i])
                sdiff++;
        CHECK(sdiff > samples / 10, "steering changed what is heard");

        // ...and with no control hook wired, the very same commands do NOTHING.
        // The mixer forwards what it cannot interpret; it does not interpret it.
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

    // --- the attach contract -------------------------------------------------
    {
        AnoSynth *s2 = ano_synth_create(&sd);
        AnoMusicEngine *m2 = ano_music_create(&cfg, 7);
        AnoMusicBar throwaway;
        ano_music_advance_bar(m2, &throwaway);
        // the live schedule starts at bar 0: a mid-piece engine is a SEEK, which
        // must rebase the clock, not silently start 2 bars in
        CHECK(!ano_synth_attach_music(s2, m2), "a mid-piece engine is refused");
        ano_music_destroy(m2);
        ano_synth_destroy(s2);
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
