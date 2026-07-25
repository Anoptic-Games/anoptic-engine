/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Offline synth gate: BeatClock oracle, tie-chain == merged note, patch showcase, journey fixture determinism + WAV.
// argv[1] scales churned re-render count. Exit 0 == pass.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>
#include <anoptic_synth.h>

#include "templates/rng.h"
#include "templates/synthfix.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE 48000u

#ifndef ANO_FIXTURE_DIR
#define ANO_FIXTURE_DIR "."
#endif
#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif

/* Render driver */

static float *render_synth(AnoSynth *syn, uint64_t frames)
{
    AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);
    static AnoAudioOfflineEvent evts[64u + 80u * 9u];
    uint32_t n = ano_synth_console_setup(evts, 64);
    n += ano_synth_console_automation(syn, evts + n,
                                      (uint32_t)(sizeof evts / sizeof *evts) - n);
    ano_synth_transport_start(syn, 0);
    AnoAudioOfflineDesc desc = {
        .sampleRate = RATE, .busCount = busCount, .busLayout = layout,
        .events = evts, .eventCount = n,
        .generator = ano_synth_generator, .generatorUser = syn,
    };
    float *out = calloc(frames * ANO_AUDIO_CHANNELS, sizeof(float));
    if (!out || !ano_audio_render_offline(&desc, out, frames)) {
        free(out);
        ano_synth_transport_stop(syn);
        return NULL;
    }
    ano_synth_transport_stop(syn);
    return out;
}

static float buf_peak(const float *b, uint64_t samples)
{
    float peak = 0.0f;
    for (uint64_t i = 0; i < samples; ++i) {
        float a = fabsf(b[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

static bool buf_finite(const float *b, uint64_t samples)
{
    for (uint64_t i = 0; i < samples; ++i)
        if (!isfinite(b[i]))
            return false;
    return true;
}

/* Probe scores */

typedef struct ProbeNote { double start, dur; uint8_t pitch, vel, layer, tie; } ProbeNote;

static bool load_probe(AnoSynth *syn, uint32_t bars, double bpm,
                       const ProbeNote *notes, uint32_t count)
{
    if (!ano_synth_score_begin(syn, 4.0, bars, 1, count))
        return false;
    if (!ano_synth_score_tempo(syn, 0.0, bpm))
        return false;
    AnoMusicalParams p = { .tempoBpm = bpm, .filterCutoff = 2500.0f,
                           .reverbSend = 0.2f, .delaySend = 0.1f,
                           .drive = 0.15f, .stereoWidth = 0.7f };
    AnoMusicAffect a = { 0.0f, 0.3f, 0.1f };
    for (uint32_t b = 0; b < bars; ++b)
        if (!ano_synth_score_bar(syn, b, &p, &a))
            return false;
    for (uint32_t i = 0; i < count; ++i) {
        AnoNoteEvent ev = { notes[i].start, notes[i].dur, notes[i].pitch,
                            notes[i].vel, notes[i].layer, notes[i].tie };
        if (!ano_synth_score_event(syn, &ev))
            return false;
    }
    return ano_synth_score_end(syn);
}

/* Heap churn */

static void churn(test_rng *rng, uint32_t iters)
{
    void *p[64] = {0};
    for (uint32_t i = 0; i < iters; ++i) {
        uint32_t k = rng_below(rng, 64);
        if (p[k]) {
            free(p[k]);
            p[k] = NULL;
        } else {
            p[k] = malloc(16u + rng_below(rng, 8192u));
            if (p[k])
                memset(p[k], (int)(i & 0xFF), 16);
        }
    }
    for (uint32_t k = 0; k < 64; ++k)
        free(p[k]);
}

int main(int argc, char **argv)
{
    uint32_t soak = 2;
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s > 0) soak = (uint32_t)s;
    }

    AnoSynth *syn = ano_synth_create(&(AnoSynthDesc){ .sampleRate = RATE });
    CHECK(syn != NULL, "synth world up");
    if (!syn) return 1;

    // --- BeatClock oracle: 120 bpm to beat 8, then 60 bpm ---
    {
        ProbeNote quiet = { 0.0, 1.0, 60, 64, ANO_MUSIC_PAD, 0 };
        CHECK(ano_synth_score_begin(syn, 4.0, 4, 2, 1), "clock score begin");
        CHECK(ano_synth_score_tempo(syn, 0.0, 120.0), "tempo point at 0");
        CHECK(ano_synth_score_tempo(syn, 8.0, 60.0), "tempo point at 8");
        CHECK(!ano_synth_score_tempo(syn, 4.0, 90.0), "regressing tempo point rejected");
        AnoMusicalParams p = { .tempoBpm = 120.0, .filterCutoff = 2500.0f };
        AnoMusicAffect a = {0};
        for (uint32_t b = 0; b < 4; ++b) ano_synth_score_bar(syn, b, &p, &a);
        AnoNoteEvent ev = { quiet.start, quiet.dur, quiet.pitch, quiet.vel, quiet.layer, 0 };
        ano_synth_score_event(syn, &ev);
        CHECK(ano_synth_score_end(syn), "clock score end");
        CHECK(fabs(ano_synth_time_at(syn, 4.0) - 2.0) < 1e-12, "time_at within first segment");
        CHECK(fabs(ano_synth_time_at(syn, 8.0) - 4.0) < 1e-12, "time_at at the anchor");
        CHECK(fabs(ano_synth_time_at(syn, 12.0) - 8.0) < 1e-12, "time_at extrapolates at 60 bpm");
    }

    // --- tie chains: out->both->in == one merged note, != three struck notes ---
    {
        const ProbeNote tied[3] = {
            { 0.0, 4.0, 60, 80, ANO_MUSIC_PAD, 1 }, // out
            { 4.0, 4.0, 60, 80, ANO_MUSIC_PAD, 3 }, // both
            { 8.0, 4.0, 60, 80, ANO_MUSIC_PAD, 2 }, // in
        };
        const ProbeNote merged[1] = { { 0.0, 12.0, 60, 80, ANO_MUSIC_PAD, 0 } };
        const ProbeNote struck[3] = {
            { 0.0, 4.0, 60, 80, ANO_MUSIC_PAD, 0 },
            { 4.0, 4.0, 60, 80, ANO_MUSIC_PAD, 0 },
            { 8.0, 4.0, 60, 80, ANO_MUSIC_PAD, 0 },
        };
        uint64_t frames = 0;
        float *a = NULL, *b = NULL, *c = NULL;
        if (load_probe(syn, 3, 120.0, tied, 3)) {
            frames = ano_synth_score_frames(syn, 1.5f);
            a = render_synth(syn, frames);
        }
        if (load_probe(syn, 3, 120.0, merged, 1))
            b = render_synth(syn, frames);
        if (load_probe(syn, 3, 120.0, struck, 3))
            c = render_synth(syn, frames);
        CHECK(a && b && c, "tie probe renders complete");
        if (a && b && c) {
            size_t bytes = frames * ANO_AUDIO_CHANNELS * sizeof(float);
            CHECK(memcmp(a, b, bytes) == 0, "tie chain renders as the single merged note");
            CHECK(memcmp(a, c, bytes) != 0, "re-articulation would be detected");
            CHECK(buf_peak(a, frames * 2) > 0.01f, "tie probe is audible");
        }
        free(a); free(b); free(c);
    }

    // --- orphan out dissolves into a plain note ---
    {
        const ProbeNote orphan[1] = { { 0.0, 4.0, 64, 80, ANO_MUSIC_MELODY, 1 } };
        const ProbeNote plain[1]  = { { 0.0, 4.0, 64, 80, ANO_MUSIC_MELODY, 0 } };
        uint64_t frames = 0;
        float *a = NULL, *b = NULL;
        if (load_probe(syn, 2, 120.0, orphan, 1)) {
            frames = ano_synth_score_frames(syn, 1.5f);
            a = render_synth(syn, frames);
        }
        if (load_probe(syn, 2, 120.0, plain, 1))
            b = render_synth(syn, frames);
        CHECK(a && b, "orphan probe renders complete");
        if (a && b)
            CHECK(memcmp(a, b, frames * ANO_AUDIO_CHANNELS * sizeof(float)) == 0,
                  "orphan out == plain note");
        free(a); free(b);
    }

    // --- patch showcase: morph pad, bell keys, chimes, glass FM, all drums ---
    {
        CHECK(ano_synth_score_begin(syn, 4.0, 4, 1, 24), "showcase begin");
        ano_synth_score_tempo(syn, 0.0, 100.0);
        AnoMusicalParams p = { .tempoBpm = 100.0, .filterCutoff = 3200.0f,
                               .reverbSend = 0.25f, .delaySend = 0.1f,
                               .drive = 0.15f, .stereoWidth = 0.9f };
        // Composer patch name == synth voice name for that id.
        for (uint32_t i = 0; i < ANO_PATCH_COUNT; ++i)
            CHECK(strcmp(ano_music_patch_name(i),
                         ano_synth_patch_name(ano_synth_patch_of(i))) == 0,
                  "every timbre the composer can name is played by the voice of that name");

        p.instruments[ANO_MUSIC_PAD]    = ANO_PATCH_MORPH;
        p.instruments[ANO_MUSIC_MELODY] = ANO_PATCH_KEYS;
        p.instruments[ANO_MUSIC_ARP]    = ANO_PATCH_CHIMES;
        AnoMusicAffect a = { 0.2f, 0.5f, 0.4f };
        for (uint32_t b = 0; b < 4; ++b) {
            if (b == 2) p.instruments[ANO_MUSIC_ARP] = ANO_PATCH_GLASS;
            ano_synth_score_bar(syn, b, &p, &a);
        }
        // Morph pad, bell phrase, chime/glass arps.
        AnoNoteEvent ev = { 0.0, 8.0, 48, 72, ANO_MUSIC_PAD, 0 };
        ano_synth_score_event(syn, &ev);
        for (int i = 0; i < 4; ++i) {
            ev = (AnoNoteEvent){ 1.0 + i * 2.0, 1.5, (uint8_t)(72 + 2 * i), 76, ANO_MUSIC_MELODY, 0 };
            ano_synth_score_event(syn, &ev);
            ev = (AnoNoteEvent){ 0.5 + i * 3.5, 0.5, (uint8_t)(76 + i), 70, ANO_MUSIC_ARP, 0 };
            ano_synth_score_event(syn, &ev);
        }
        // Every drum recipe once (kick ducks).
        static const uint8_t drums[10] = { 36, 37, 38, 42, 46, 45, 47, 50, 49, 70 };
        for (int i = 0; i < 10; ++i) {
            ev = (AnoNoteEvent){ 8.0 + i * 0.75, 0.25, drums[i], 90, ANO_MUSIC_PERC, 0 };
            ano_synth_score_event(syn, &ev);
        }
        // Driven bass + hard lead on the same score.
        ev = (AnoNoteEvent){ 12.0, 2.0, 36, 84, ANO_MUSIC_BASS, 0 };
        ano_synth_score_event(syn, &ev);
        CHECK(ano_synth_score_end(syn), "showcase end");

        uint64_t frames = ano_synth_score_frames(syn, 2.5f);
        float *out = render_synth(syn, frames);
        CHECK(out != NULL, "showcase renders");
        if (out) {
            uint64_t samples = frames * ANO_AUDIO_CHANNELS;
            CHECK(buf_finite(out, samples), "showcase output finite");
            CHECK(buf_peak(out, samples) > 0.02f, "showcase audible");
            uint64_t tail = (uint64_t)(0.2f * RATE) * ANO_AUDIO_CHANNELS;
            CHECK(buf_peak(out + samples - tail, tail) < 5e-3f, "showcase decays to silence");
            CHECK(ano_synth_dropped(syn) == 0u, "no voices dropped");
        }
        free(out);
    }

    // --- journey fixture: churned-heap determinism, then full WAV ---
    {
        CHECK(synthfix_load(syn, ANO_FIXTURE_DIR "/journey_s42.anofix"), "journey fixture loads");

        // First 16 bars x soak on churned heap: byte-identical.
        uint64_t gate = (uint64_t)(ano_synth_time_at(syn, 16.0 * 4.0) * RATE);
        float *ref = render_synth(syn, gate);
        CHECK(ref != NULL, "journey gate render");
        test_rng rng = rng_make(0x50A4u);
        for (uint32_t it = 0; ref && it < soak; ++it) {
            churn(&rng, 4000);
            float *again = render_synth(syn, gate);
            CHECK(again != NULL, "journey re-render");
            if (again)
                CHECK(memcmp(ref, again, gate * ANO_AUDIO_CHANNELS * sizeof(float)) == 0,
                      "byte-identical on a churned heap");
            free(again);
        }
        free(ref);

        // Full piece -> listenable WAV.
        uint64_t frames = ano_synth_score_frames(syn, 2.5f);
        float *out = render_synth(syn, frames);
        CHECK(out != NULL, "journey full render");
        if (out) {
            uint64_t samples = frames * ANO_AUDIO_CHANNELS;
            CHECK(buf_finite(out, samples), "journey output finite");
            float peak = buf_peak(out, samples);
            CHECK(peak > 0.05f, "journey audible");
            CHECK(ano_synth_dropped(syn) == 0u, "journey: no voices dropped");
            const char *wav = ANO_TEST_OUTDIR "/journey_s42_synth.wav";
            CHECK(ano_audio_wav_write(wav, out, frames, ANO_AUDIO_CHANNELS, RATE),
                  "journey WAV written");
            printf("info: journey 〜 %.1f s, peak %.3f, %s\n",
                   (double)frames / RATE, (double)peak, wav);
        }
        free(out);
    }

    ano_synth_destroy(syn);

    // --- score_event field ranges: pitch 0..127, velocity 1..127 ---
    {
        AnoSynth *g = ano_synth_create(NULL);
        CHECK(g != NULL, "range-guard synth created");
        if (g) {
            CHECK(ano_synth_score_begin(g, 4.0, 1, 1, 8), "score_begin accepted");
            CHECK(ano_synth_score_tempo(g, 0.0, 120.0), "score_tempo accepted");
            AnoMusicalParams params = { .tempoBpm = 120.0 };
            AnoMusicAffect affect = { 0 };
            CHECK(ano_synth_score_bar(g, 0, &params, &affect), "score_bar accepted");

            // control: a contract-clean event passes, so the gate is live
            AnoNoteEvent ok = { .start = 0.0, .dur = 1.0, .pitch = 60, .velocity = 100,
                                .layer = ANO_MUSIC_MELODY, .tie = ANO_MUSIC_TIE_NONE };
            CHECK(ano_synth_score_event(g, &ok), "in-range event accepted");

            AnoNoteEvent vel0 = ok;
            vel0.velocity = 0;
            CHECK(!ano_synth_score_event(g, &vel0), "velocity 0 rejected");
            AnoNoteEvent dur0 = ok;
            dur0.dur = 0.0;
            CHECK(!ano_synth_score_event(g, &dur0), "dur 0 rejected");

            // above the documented ceiling: would render at ~2x the amplitude contract
            AnoNoteEvent hot = ok;
            hot.velocity = 200;
            CHECK(!ano_synth_score_event(g, &hot), "velocity 200 rejected as out of contract");

            // above MIDI 127: would alias pitch & 0x7F (130 -> 2) in the tie chains
            AnoNoteEvent high = ok;
            high.pitch = 130;
            CHECK(!ano_synth_score_event(g, &high), "pitch 130 rejected as out of contract");

            ano_synth_destroy(g);
        }
    }

    // --- score_tempo obeys the documented load order like its siblings ---
    {
        AnoSynth *a = ano_synth_create(NULL);
        AnoSynth *b = ano_synth_create(NULL);
        CHECK(a != NULL && b != NULL, "tempo-order synths created");
        if (a && b) {
            // control: the documented order (begin -> tempo -> bars -> events -> end) works
            CHECK(ano_synth_score_begin(a, 4.0, 1, 2, 4), "score_begin accepted");
            CHECK(ano_synth_score_tempo(a, 0.0, 120.0), "in-order score_tempo accepted");
            CHECK(ano_synth_score_tempo(a, 2.0, 90.0), "second monotonic tempo point accepted");
            CHECK(!ano_synth_score_tempo(a, 1.0, 100.0), "non-monotonic beat rejected");
            CHECK(!ano_synth_score_tempo(a, 3.0, 0.0), "bpm 0 rejected");

            // control: on a fresh synth every sibling entry point reports the misuse as false
            AnoMusicalParams params = { .tempoBpm = 120.0 };
            AnoMusicAffect affect = { 0 };
            CHECK(!ano_synth_score_bar(b, 0, &params, &affect), "score_bar before begin returns false");
            AnoNoteEvent ev = { .start = 0.0, .dur = 1.0, .pitch = 60, .velocity = 100,
                                .layer = ANO_MUSIC_MELODY, .tie = ANO_MUSIC_TIE_NONE };
            CHECK(!ano_synth_score_event(b, &ev), "score_event before begin returns false");
            CHECK(!ano_synth_score_end(b), "score_end before begin returns false");

            fflush(stdout);   // the misuse below must not swallow buffered FAIL lines
            CHECK(!ano_synth_score_tempo(b, 0.0, 120.0), "score_tempo before begin returns false");
        }
        ano_synth_destroy(b);
        ano_synth_destroy(a);
    }

    // --- score_begin sizes what it promises, tempoCount at the uint32 edge ---
    {
        // control: tempoCount 1 holds exactly one added point, tempoCount 0 holds none
        AnoSynth *a = ano_synth_create(NULL);
        AnoSynth *b = ano_synth_create(NULL);
        CHECK(a != NULL && b != NULL, "anchor-cap control synths created");
        if (a && b) {
            CHECK(ano_synth_score_begin(a, 4.0, 1, 1, 0), "begin with tempoCount 1 accepted");
            CHECK(ano_synth_score_tempo(a, 4.0, 90.0), "declared tempo point accepted");
            CHECK(!ano_synth_score_tempo(a, 8.0, 80.0), "point past declared capacity rejected");

            CHECK(ano_synth_score_begin(b, 4.0, 1, 0, 0), "begin with tempoCount 0 accepted");
            CHECK(!ano_synth_score_tempo(b, 4.0, 90.0), "added point on zero declared capacity rejected");
        }
        ano_synth_destroy(b);
        ano_synth_destroy(a);

        // tempoCount UINT32_MAX: begin sizes the anchor array from tempoCount + 1 for the
        // implicit beat-0 seed, so the promise must either be refused or honored.
        fflush(stdout);
        AnoSynth *c = ano_synth_create(NULL);
        CHECK(c != NULL, "anchor-cap edge synth created");
        if (c) {
            if (ano_synth_score_begin(c, 4.0, 1, UINT32_MAX, 0))
                CHECK(ano_synth_score_tempo(c, 4.0, 90.0),
                      "a begin that promised UINT32_MAX tempo points takes the first one");
            ano_synth_destroy(c);
        }
    }

    if (failures) {
        printf("anotest_synth: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_synth: all passed\n");
    return 0;
}
