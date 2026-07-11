/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Phase 7's exit criterion, and the first time the whole stack is one thing:
 * an ENDLESS piece composed inside the audio callback, steered live from
 * gameplay, with save and seek by deterministic reconstruction.
 *
 *   game (this thread)          bridge            audio thread
 *   ------------------          ------            ------------
 *   threat rises  ------ ACMD_MUSIC_AFFECT ----->  the composer answers
 *                 ------ ACMD_MUSIC_KEY    ----->  it modulates
 *                 ------ ACMD_MUSIC_MOTIF  ----->  it states the theme
 *   reacts to     <----- AEVT_MUSIC_BAR    ------  a bar starts sounding
 *   cadences                                       (its meaning, on its downbeat)
 *   load a save   ------ ACMD_MUSIC_SEEK   ----->  adopts it at the barline
 *                 <----- AEVT_MUSIC_SEEKED ------  the snapshot is consumed
 *
 * There is no score. Nothing is generated ahead of time, nothing loops, and the
 * game never touches the music engine — it lives on the audio thread, two bars
 * ahead of the playhead, and every bar of it is composed in the ~1.5% of the
 * block period the telemetry reports back.
 *
 * The save is not a serialized engine: it is a SEED, a CONFIG and the cues the
 * game issued. Loading replays them off the audio thread and hands the audio
 * thread the resulting bytes. Two replays of the same script must be
 * byte-identical, or a save file means nothing — that is asserted here.
 *
 * Asserts only what holds on ANY backend (CI cascades to null): the mixer beats,
 * the music becomes audible and STAYS audible, the steering lands (the key
 * arrives, cadences are reported), the seek lands, and the audio thread never
 * fell behind (genLate/genDropped zero, worst bar well inside the block).
 *
 * ANO_AUDIO_BACKEND=pipewire|alsa|null pins the backend. argv[1] is the length
 * in seconds — default 30 for CI; run it with 180 for the background listen,
 * which is what this test is really for. Exit 0 == pass. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>
#include <anoptic_music.h>
#include <anoptic_synth.h>
#include <anoptic_time.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE 48000u

// ---------------------------------------------------------------------------
// The score's personality (what a game would author once)
// ---------------------------------------------------------------------------

static void author_config(AnoMusicConfig *c)
{
    *c = ano_music_config_default();
    c->hasMapper = true;
    c->mapper = ano_mapping_table_default();
    c->hasDramaturg = true;
    c->dramaturg = ano_dramaturg_config_default();
    c->phraseGroove = true;
    c->cadenceRit = 0.03;
    c->wanderPhrases = 6;
    c->form.cadential64 = c->form.periods = c->form.hypermeter = true;
    c->form.bassInversions = c->form.split64 = true;
    c->texture.doubling = c->texture.animate = c->texture.imitation = true;
    c->texture.rotate = c->texture.counter = true;
    c->ties.anacrusis = c->ties.suspension = c->ties.syncopation = true;
    c->clock.codetta = c->clock.extension = c->clock.elision = true;
    c->melody.planApex = c->melody.counterpoint = true;
    c->useChains = c->performChains = true;

    // Two authored themes the game can ask for by name. Pitches are never
    // authored — a motif is a rhythm and a shape, re-realized into whatever
    // harmony it lands in, which is how it stays recognizable as the music moves.
    AnoMotif hero = { 0 }, threat = { 0 };
    static const int HR[8] = { 0, 4, 4, 2, 6, 2, 8, 4 };
    static const int HC[4] = { 0, 2, 1, 0 };
    hero.n = 4; hero.shape = ANO_SHAPE_ARCH;
    for (int j = 0; j < 4; ++j) {
        hero.rhythm[j] = (AnoRhythmNote){ HR[2 * j], HR[2 * j + 1] };
        hero.contour[j] = HC[j];
    }
    static const int TR[6] = { 0, 2, 2, 2, 4, 4 };
    static const int TC[3] = { 0, -1, -2 };
    threat.n = 3; threat.shape = ANO_SHAPE_DESCENT;
    for (int j = 0; j < 3; ++j) {
        threat.rhythm[j] = (AnoRhythmNote){ TR[2 * j], TR[2 * j + 1] };
        threat.contour[j] = TC[j];
    }
    c->motifLibrary[0] = (AnoSignatureMotif){ "hero", hero, 0.9 };
    c->motifLibrary[1] = (AnoSignatureMotif){ "threat", threat, 0.5 };
    c->motifLibraryCount = 2;
}

// ---------------------------------------------------------------------------
// The save file
// ---------------------------------------------------------------------------
// Not the engine's bytes: the seed, the config, and every cue the game issued,
// stamped with the bar it was composed into. Replaying that reconstructs the
// state exactly (ano_music_apply_command is the same interpretation the audio
// thread used), and the reconstruction is what gets handed back as a seek.

#define MAX_CUES 64

typedef struct Cue
{
    int             bar; // the composition bar the cue landed in
    AnoAudioCommand cmd;
} Cue;

typedef struct Script
{
    Cue      cue[MAX_CUES];
    uint32_t n;
} Script;

// The bar the engine is composing while bar `sounding` plays: it runs
// ANO_SYNTH_LIVE_LOOKAHEAD bars ahead, and the bar after the last one it has
// composed is the first a cue submitted now can reach.
static int composing_bar(int sounding)
{
    return sounding + (int)ANO_SYNTH_LIVE_LOOKAHEAD + 1;
}

static void record(Script *s, int soundingBar, const AnoAudioCommand *cmd)
{
    if (s->n < MAX_CUES)
        s->cue[s->n++] = (Cue){ composing_bar(soundingBar), *cmd };
}

// Rebuild the engine the game was hearing, from the save alone. This runs on the
// GAME thread — a thousand bars costs ~120 ms, which is why it can never be done
// in the callback, and why the seek exists at all.
static AnoMusicEngine *reconstruct(const AnoMusicConfig *cfg, uint64_t seed,
                                   const Script *s, int toBar)
{
    static AnoMusicBar sink;
    AnoMusicEngine *e = ano_music_create(cfg, seed);
    if (!e)
        return NULL;
    for (int b = 0; b < toBar; ++b) {
        for (uint32_t i = 0; i < s->n; ++i)
            if (s->cue[i].bar == b)
                ano_music_apply_command(e, &s->cue[i].cmd);
        ano_music_advance_bar(e, &sink);
    }
    // A cue stamped AT toBar has not been composed yet — it is pending for the
    // next bar the engine will write. Stopping the loop short of it would drop
    // the most recent steering the save was supposed to capture.
    for (uint32_t i = 0; i < s->n; ++i)
        if (s->cue[i].bar == toBar)
            ano_music_apply_command(e, &s->cue[i].cmd);
    return e;
}

// ---------------------------------------------------------------------------
// Bridge plumbing
// ---------------------------------------------------------------------------

static void must_submit(AnoAudioBridge *b, const AnoAudioCommand *c)
{
    while (!ano_audio_submit(b, c))
        ano_sleep(1000);
}

typedef bool (*telem_pred)(const AnoAudioTelemetry *t);
static bool wait_telemetry(AnoAudioBridge *b, telem_pred pred, uint32_t timeoutMs)
{
    uint32_t start = ano_timestamp_ms();
    for (;;) {
        AnoAudioTelemetry t;
        if (ano_audio_acquire_telemetry(b, &t) && pred(&t))
            return true;
        if (ano_timestamp_ms() - start > timeoutMs)
            return false;
        ano_sleep(5000);
    }
}
static bool pred_heartbeat(const AnoAudioTelemetry *t) { return t->blockIndex >= 3u; }
static bool pred_audible(const AnoAudioTelemetry *t)   { return t->masterPeak > 0.02f; }

static const char *const MODE_NAME[] = { "ionian", "dorian", "phrygian", "lydian",
                                         "mixolydian", "aeolian", "locrian" };
static const char *const PC_NAME[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };

int main(int argc, char **argv)
{
    uint32_t seconds = 30;
    if (argc > 1)
        seconds = (uint32_t)atoi(argv[1]);
    if (seconds < 8u)
        seconds = 8u;

    const uint64_t seed = 2718u;
    AnoMusicConfig cfg;
    author_config(&cfg);

    // the composer, and the synth that hosts it
    AnoSynth *syn = ano_synth_create(&(AnoSynthDesc){ .sampleRate = RATE });
    AnoMusicEngine *music = ano_music_create(&cfg, seed);
    CHECK(syn && music, "synth + composer");
    if (!syn || !music)
        return 1;
    CHECK(ano_synth_attach_music(syn, music), "the composer drives the generator");

    AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);

    // all five seams wired: render down, control down, events up, stats up, and
    // the console the music asks for coming back the other way
    AnoAudioConfig acfg = {
        .sampleRate = RATE, .busCount = busCount, .busLayout = layout,
        .generator         = ano_synth_generator,
        .generatorUser     = syn,
        .generatorControl  = ano_synth_control,
        .generatorPoll     = ano_synth_poll,
        .generatorStats    = ano_synth_stats,
        .generatorCommands = ano_synth_commands,
    };
    CHECK(ano_audio_init(&acfg), "audio world up");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge valid");
    if (!b)
        return 1;
    CHECK(wait_telemetry(b, pred_heartbeat, 3000), "mixer heartbeat");

    // one-time console setup (EQ curves, glue makeup, drive trim); the per-bar
    // moves need no help — the sounding bar automates the desk itself
    static AnoAudioOfflineEvent setup[64];
    uint32_t setupCount = ano_synth_console_setup(setup, 64);
    for (uint32_t i = 0; i < setupCount; ++i)
        must_submit(b, &setup[i].cmd);

    AnoAudioTelemetry t;
    CHECK(ano_audio_acquire_telemetry(b, &t), "telemetry frame");
    ano_synth_transport_start(syn, (t.blockIndex + 8u) * t.blockFrames);
    CHECK(wait_telemetry(b, pred_audible, 5000), "the music comes up");

    // -----------------------------------------------------------------------
    // The game
    // -----------------------------------------------------------------------
    // A threat level, on the arc a level would actually take: calm, something
    // approaching, a fight, and the quiet after it. Time-scaled, so a 30 s CI run
    // and a 180 s listen both traverse the whole shape.
    static Script script;
    Script saved = { 0 };
    int    savedBar = -1;
    bool   loaded = false, seekAcked = false;
    int    seekedTo = -1;

    uint32_t bars = 0, cadences = 0, motifs = 0, keyArrivals = 0;
    int  lastBar = -1, stage = -1;
    bool everSilent = false;

    const uint32_t startMs = ano_timestamp_ms();
    const uint32_t runMs   = seconds * 1000u;
    for (;;) {
        uint32_t nowMs = ano_timestamp_ms() - startMs;
        if (nowMs >= runMs)
            break;
        double phase = (double)nowMs / (double)runMs; // 0 .. 1 through the arc

        // --- what the audio thread has to say --------------------------------
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e)) {
            if (e.kind == AEVT_MUSIC_SEEKED) {
                seekAcked = true;
                seekedTo = e.u.seekedBar;
                printf("      · seek consumed — the save's bar %d is next\n", seekedTo);
            }
            if (e.kind != AEVT_MUSIC_BAR)
                continue;
            bars++;
            lastBar = e.u.music.bar;
            cadences += e.u.music.isCadence;
            motifs += e.u.music.motifStated;
            keyArrivals += e.u.music.keyArrived;
            if (e.u.music.keyArrived)
                printf("      · bar %-4d the key arrives: %s %s\n", e.u.music.bar,
                       PC_NAME[e.u.music.keyTonic % 12],
                       MODE_NAME[e.u.music.mode % 7]);
            if (e.u.music.motifStated)
                printf("      · bar %-4d the theme is stated\n", e.u.music.bar);
        }
        if (lastBar < 0) { // nothing has sounded yet; nothing to steer against
            ano_sleep(20000);
            continue;
        }

        // --- gameplay -> affect, quantized to the bar it will be composed into -
        // Steering is keyed to the bar, not to the wall clock, which is exactly
        // what makes the save replayable: the cue lands where it is recorded.
        int want = phase < 0.10 ? 0 : phase < 0.30 ? 1 : phase < 0.70 ? 2 : 3;
        if (want != stage) {
            stage = want;
            static const struct { float v, en, tn; const char *say; } ARC[4] = {
                { 0.55f, 0.20f, 0.10f, "calm" },
                { 0.10f, 0.45f, 0.45f, "something is coming" },
                { -0.60f, 0.95f, 0.90f, "the fight" },
                { 0.35f, 0.30f, 0.15f, "the quiet after" },
            };
            AnoAudioCommand c = { .kind = ACMD_MUSIC_AFFECT, .urgent = stage == 2,
                                  .affect = { ARC[stage].v, ARC[stage].en,
                                              ARC[stage].tn } };
            must_submit(b, &c);
            record(&script, lastBar, &c);
            printf("game: %-20s (bar %d)\n", ARC[stage].say, lastBar);

            if (stage == 1) { // it is coming: pull the key toward the dominant.
                // Early, because a modulation is not instant — it takes a pivot
                // and a dominant before it arrives, and the game wants it to land
                // ON the fight, not after it.
                AnoAudioCommand k = { .kind = ACMD_MUSIC_KEY, .paramId = 7,
                                      .urgent = true };
                must_submit(b, &k);
                record(&script, lastBar, &k);
            }
            if (stage == 2) { // the fight: name the threat
                AnoAudioCommand m = { .kind = ACMD_MUSIC_MOTIF, .tag = "threat" };
                must_submit(b, &m);
                record(&script, lastBar, &m);
            }
            if (stage == 3) { // the quiet: open the room up, and let the hero speak
                AnoAudioCommand r = { .kind = ACMD_MUSIC_OVERRIDE, .tag = "reverb_send",
                                      .value = 0.75f };
                must_submit(b, &r);
                record(&script, lastBar, &r);
                AnoAudioCommand m = { .kind = ACMD_MUSIC_MOTIF, .tag = "hero" };
                must_submit(b, &m);
                record(&script, lastBar, &m);
            }
        }

        // --- save, and later load it ------------------------------------------
        if (savedBar < 0 && phase > 0.35) {
            saved = script;                     // the cues so far...
            savedBar = composing_bar(lastBar);  // ...and where we were
            printf("game: SAVE at bar %d (%u cues)\n", savedBar, saved.n);
        }
        if (savedBar >= 0 && !loaded && phase > 0.80) {
            loaded = true;
            printf("game: LOAD — rebuilding bar %d off the audio thread\n", savedBar);

            // A save file is only a save file if it reconstructs the same state
            // every time. Build it twice and compare the bytes.
            uint32_t t0 = ano_timestamp_ms();
            AnoMusicEngine *a = reconstruct(&cfg, seed, &saved, savedBar);
            AnoMusicEngine *c2 = reconstruct(&cfg, seed, &saved, savedBar);
            uint32_t buildMs = ano_timestamp_ms() - t0;
            CHECK(a && c2, "the save reconstructs");

            size_t sz = ano_music_snapshot_size();
            void *snapA = malloc(sz), *snapB = malloc(sz);
            CHECK(ano_music_snapshot(a, snapA, sz) && ano_music_snapshot(c2, snapB, sz),
                  "snapshot the rebuild");
            CHECK(memcmp(snapA, snapB, sz) == 0,
                  "two reconstructions of one save are byte-identical");
            printf("      · %zu-byte state rebuilt in %u ms (never on the audio thread)\n",
                   sz, buildMs);

            AnoAudioCommand sk = { .kind = ACMD_MUSIC_SEEK, .block = snapA };
            must_submit(b, &sk);
            // the block is BORROWED: it must stay valid until the audio thread
            // says it has consumed it
            uint32_t waited = 0;
            while (!seekAcked && waited < 2000u) {
                AnoAudioEvent ev;
                while (ano_audio_poll_event(b, &ev))
                    if (ev.kind == AEVT_MUSIC_SEEKED) {
                        seekAcked = true;
                        seekedTo = ev.u.seekedBar;
                    }
                ano_sleep(5000);
                waited += 5;
            }
            CHECK(seekAcked, "the audio thread consumed the save");
            CHECK(seekedTo == savedBar, "and adopted the bar it was given");
            printf("      · seek consumed — the save's bar %d is next\n", seekedTo);
            free(snapA);
            free(snapB);
            ano_music_destroy(a);
            ano_music_destroy(c2);
        }

        // --- the meter -------------------------------------------------------
        if (ano_audio_acquire_telemetry(b, &t)) {
            if (t.masterPeak < 0.001f && bars > 2u)
                everSilent = true; // an endless piece must never stop being one
            if (t.blockIndex % 512u == 0u)
                printf("info: bar %-4d │ peak %.3f │ bar composed in %u us of %u │ "
                       "underruns %u\n",
                       lastBar, (double)t.masterPeak, t.genUs,
                       (uint32_t)((uint64_t)t.blockFrames * 1000000u / t.sampleRate),
                       t.underruns);
        }
        ano_sleep(20000);
    }

    // -----------------------------------------------------------------------
    // What actually happened
    // -----------------------------------------------------------------------
    CHECK(bars > 4u, "the piece played");
    CHECK(!everSilent, "and it never once ran out of music");
    CHECK(script.n >= 4u, "the game steered it");
    // A modulation takes a pivot and a dominant before it lands, so it needs bars
    // to happen in at all.
    if (bars >= 8u)
        CHECK(keyArrivals > 0u, "the commanded modulation arrived");

    // And the steering MOVES the cadences. An urgent modulation re-plans the
    // phrase it lands in: the cadence this piece would have reached at bar 7
    // unsteered is displaced to bar 15. So a run only gets to be asked about a
    // cadence if it is long enough to contain one AFTER being steered — which the
    // 30 s CI pass is not, and the 180 s listen very much is. (That cadences reach
    // the game at all is pinned deterministically in anotest_musicdrive.)
    if (bars >= 16u)
        CHECK(cadences > 0u, "it cadenced, and the game was told");
    else
        printf("note: %u bars — too short to reach a cadence once the steering has "
               "displaced it; run it longer to gate that\n", bars);
    if (savedBar >= 0)
        CHECK(loaded && seekAcked, "the save was written and loaded back");

    ano_synth_transport_stop(syn);
    ano_sleep(50000); // let the mixer pass the stop and the tails ring down

    if (ano_audio_acquire_telemetry(b, &t)) {
        uint32_t blockUs = (uint32_t)((uint64_t)t.blockFrames * 1000000u / t.sampleRate);
        printf("info: %u s │ %llu blocks │ bars %u │ cadences %u │ motifs %u │ "
               "keys %u │ cues %u\n",
               seconds, (unsigned long long)t.blockIndex, bars, cadences, motifs,
               keyArrivals, script.n);
        printf("info: worst bar composed in %u us of the %u us block (%.1f%%) │ "
               "late %u │ dropped %u │ underruns %u │ clipped %u\n",
               t.genUsMax, blockUs, 100.0 * (double)t.genUsMax / (double)blockUs,
               t.genLate, t.genDropped, t.underruns, t.clippedSamples);

        // The plan's one real risk: that composing a bar inside the callback does
        // not fit. It is measured here, on a device, not assumed.
        CHECK(t.genUsMax > 0u, "the composer's cost was measured");
        CHECK(t.genUsMax < blockUs, "composing a bar fits inside one block");
        CHECK(t.genLate == 0u, "no bar arrived after the playhead needed it");
        CHECK(t.genDropped == 0u, "nothing was dropped for want of room");
    }

    ano_audio_shutdown();
    ano_synth_destroy(syn);
    ano_music_destroy(music);

    if (failures) {
        printf("anotest_musicscene: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musicscene: all passed (%u s)\n", seconds);
    return 0;
}
