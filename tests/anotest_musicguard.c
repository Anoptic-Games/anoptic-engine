/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: engine-instance independence under the documented off-thread seek shape.
// anoptic_music.h:481 promises "Same config+seed+bar => byte-identical", and the hosting
// doc (src/music/ANOPTIC_MUSICGEN.md) prescribes rebuilding a second engine off-thread
// while the callback-hosted composer keeps advancing the live one 〜 but ano_voice_chord
// keeps its candidate table in a plain function-scope static (music_voicing.c:114,
// "single-threaded conductor context"), shared across every engine in the process
// (docs/BUGS.md, Music / Implementation). Two engines advancing concurrently interleave
// writes into that table: one engine's dedupe/cost scan and final copy read the other's
// candidates, a wrong pad voicing enters prevVoicing, and the bar stream / snapshot stop
// being a pure function of (config, seed, bar). The control pins the solo replay
// byte-identical (harness sanity); the trigger runs the same span while a disturber
// engine hammers its own bars on a second thread and asserts the stream and snapshot
// still match the solo run. Headless, no device, no audio. Exit 0 == pass.

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_music.h>
#include <anoptic_threads.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define GUARD_SEED  42ull
#define GUARD_BARS  1024u
#define GUARD_ROUNDS 8

// Inputs: h fold state, data/n bytes. Output: FNV-1a folded state.
static uint64_t fnv1a64(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Inputs: fold state + one composed bar. Output: state folded over the bar's
// playable content (defined fields only; struct padding never hashed).
static uint64_t bar_fold(uint64_t h, const AnoMusicBar *b)
{
    h = fnv1a64(h, &b->eventCount, sizeof b->eventCount);
    for (uint32_t i = 0; i < b->eventCount; ++i) {
        const AnoNoteEvent *e = &b->events[i];
        h = fnv1a64(h, &e->start, sizeof e->start);
        h = fnv1a64(h, &e->dur, sizeof e->dur);
        h = fnv1a64(h, &e->pitch, 1);
        h = fnv1a64(h, &e->velocity, 1);
        h = fnv1a64(h, &e->layer, 1);
        h = fnv1a64(h, &e->tie, 1);
    }
    h = fnv1a64(h, &b->tempoCount, sizeof b->tempoCount);
    for (uint32_t i = 0; i < b->tempoCount; ++i) {
        h = fnv1a64(h, &b->tempo[i].beat, sizeof b->tempo[i].beat);
        h = fnv1a64(h, &b->tempo[i].bpm, sizeof b->tempo[i].bpm);
    }
    return h;
}

// Inputs: seed, bar count, snapshot buffer (ano_music_snapshot_size() bytes).
// Output: event-stream fold; *snap holds the engine bytes after the last bar.
// Default config (pad + bass): every bar routes through ano_voice_chord.
static uint64_t run_span(uint64_t seed, uint32_t bars, void *snap)
{
    AnoMusicEngine *e = ano_music_create(NULL, seed);
    AnoMusicBar *bar = malloc(sizeof *bar);
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < bars; ++i) {
        ano_music_advance_bar(e, bar);
        h = bar_fold(h, bar);
    }
    ano_music_snapshot(e, snap, ano_music_snapshot_size());
    ano_music_destroy(e);
    free(bar);
    return h;
}

typedef struct Disturber
{
    atomic_bool stop;
    uint64_t    seed;
} Disturber;

// The off-thread seek half: an independent engine advancing on its own thread
// (ANOPTIC_MUSICGEN.md: rebuild off-thread while the callback keeps composing).
static void *disturb(void *arg)
{
    Disturber *d = arg;
    AnoMusicEngine *e = ano_music_create(NULL, d->seed);
    AnoMusicBar *bar = malloc(sizeof *bar);
    while (!atomic_load_explicit(&d->stop, memory_order_relaxed))
        ano_music_advance_bar(e, bar);
    ano_music_destroy(e);
    free(bar);
    return NULL;
}

int main(void)
{
    size_t ss = ano_music_snapshot_size();
    void *ref = malloc(ss), *ctl = malloc(ss), *trial = malloc(ss);

    // control: a solo replay of the same (config, seed, bars) is byte-identical
    uint64_t hRef = run_span(GUARD_SEED, GUARD_BARS, ref);
    uint64_t hCtl = run_span(GUARD_SEED, GUARD_BARS, ctl);
    CHECK(hCtl == hRef, "control: solo replay reproduces the event stream");
    CHECK(memcmp(ctl, ref, ss) == 0, "control: solo replay snapshot byte-identical");

    // trigger: the same span with a second engine composing concurrently 〜 the
    // header contract makes the result independent of any other engine instance
    for (int round = 0; round < GUARD_ROUNDS; ++round) {
        Disturber d;
        atomic_init(&d.stop, false);
        d.seed = 0x9E3779B97F4A7C15ull + (uint64_t)round;
        anothread_t t;
        CHECK(ano_thread_create(&t, NULL, disturb, &d) == 0, "disturber thread starts");
        uint64_t hTrial = run_span(GUARD_SEED, GUARD_BARS, trial);
        atomic_store(&d.stop, true);
        ano_thread_join(t, NULL);

        bool streamOk = hTrial == hRef;
        bool snapOk = memcmp(trial, ref, ss) == 0;
        CHECK(streamOk, "concurrent engines: bar stream is a pure function of (config, seed, bar)");
        CHECK(snapOk, "concurrent engines: snapshot byte-identical to the solo run");
        if (!streamOk || !snapOk) {
            printf("  diverged in round %d of %d\n", round + 1, GUARD_ROUNDS);
            break; // one witness is the finding; skip the remaining rounds
        }
    }

    free(ref);
    free(ctl);
    free(trial);
    if (failures == 0)
        printf("anotest_musicguard: all checks passed.\n");
    return failures ? 1 : 0;
}
