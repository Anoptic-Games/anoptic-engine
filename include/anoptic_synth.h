/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_synth.h
 * @brief The synthesizer: renders music IR (anoptic_music.h) into the audio
 *        module's bus graph through the AnoAudioGenerator seam.
 *
 * The synth owns only what is per-voice — a preallocated voice pool, patches
 * as data, the BeatClock, and a deadline-sorted schedule. The console (layer
 * strips, sends, returns, master glue) is instantiated as ordinary audio-module
 * bus configuration via the layout/setup helpers below; per-bar console motion
 * rides ACMD_BUS_SET / ACMD_FX_SET commands the automation helper emits.
 *
 * Threading contract: score loading and transport control happen on the logic
 * thread while the synth is idle (before ano_synth_transport_start, or after
 * the score has fully decayed). Once started, the generator callback — invoked
 * by the mixer thread once per block — is the sole toucher of runtime state;
 * it never allocates, locks, or reaches the bridge. Offline, the same
 * generator runs on the calling thread via AnoAudioOfflineDesc.
 */

#ifndef ANOPTIC_SYNTH_H
#define ANOPTIC_SYNTH_H

#include <stdint.h>
#include <stdbool.h>

#include <anoptic_audio.h>
#include <anoptic_music.h>

// ---------------------------------------------------------------------------
// The console shape (fixed bus indices; values are tuning)
// ---------------------------------------------------------------------------

// Engine master is bus 0 (clip guard); the music console hangs under it.
#define ANO_SYNTH_BUS_MASTER  1u // console master: drive -> glue comp -> DC -> limiter
#define ANO_SYNTH_BUS_REVERB  2u // reverb return (pure-wet FDN); strips send here
#define ANO_SYNTH_BUS_DELAY   3u // ping-pong return (tempo-synced dotted 8th)
#define ANO_SYNTH_BUS_STRIP0  4u // + AnoMusicLayer: six layer strips in canonical order
#define ANO_SYNTH_BUS_SHIMMER 10u // granular shimmer lane (dry x0.2, full into reverb)
#define ANO_SYNTH_CONSOLE_BUSES 11u

// ---------------------------------------------------------------------------
// Patches
// ---------------------------------------------------------------------------

// The patch registry: every constant lives in synth-internal data tables —
// retuning never touches code. Ids are stable (they ride MusicalParams
// .instruments); 0 selects the layer's default (pad WARM, bass ROUND, melody
// SOFT, counter MELLOW, arp PLUCK). BREEZE / WHISTLE / BAD_GROUND are reserved
// texture patches, unimplemented in Phase 4 — selecting one falls back to the
// layer default.
typedef enum AnoSynthPatch
{
    ANO_SYNTH_PATCH_DEFAULT = 0,
    ANO_SYNTH_PATCH_WARM,       // pad: tight-detune 3-saw, mono, slow attack
    ANO_SYNTH_PATCH_BRIGHT,     // pad: wide-detune 3-saw, stereo spread, fast attack
    ANO_SYNTH_PATCH_MORPH,      // pad: 2D-morphing wavetable
    ANO_SYNTH_PATCH_BREEZE,     // pad texture (reserved)
    ANO_SYNTH_PATCH_ROUND,      // bass: saw + sub sine, filter-envelope pluck
    ANO_SYNTH_PATCH_DRIVEN,     // bass: same, tanh pre-drive, hotter sweep
    ANO_SYNTH_PATCH_BAD_GROUND, // bass texture (reserved)
    ANO_SYNTH_PATCH_SOFT,       // lead: tri+saw, delayed vibrato 5.5 Hz
    ANO_SYNTH_PATCH_HARD,       // lead: saw+square, faster hotter vibrato
    ANO_SYNTH_PATCH_MELLOW,     // lead: tri+sine, the countermelody voice
    ANO_SYNTH_PATCH_KEYS,       // melody: repitched bell sampler
    ANO_SYNTH_PATCH_WHISTLE,    // melody texture (reserved)
    ANO_SYNTH_PATCH_PLUCK,      // arp: 2-op FM, ratio 3.007 index 1.8
    ANO_SYNTH_PATCH_GLASS,      // arp: 2-op FM, ratio 7.003 index 2.6
    ANO_SYNTH_PATCH_CHIMES,     // arp: 5-partial tubular additive
    ANO_SYNTH_PATCH_COUNT,
} AnoSynthPatch;

// Name <-> id for fixture/config text. Unknown name returns 0 (layer default).
uint32_t    ano_synth_patch_id(const char *name);
const char *ano_synth_patch_name(uint32_t id);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

typedef struct AnoSynth AnoSynth;

typedef struct AnoSynthDesc
{
    uint32_t sampleRate; // must equal the audio world's rate; default 48000
    uint32_t maxVoices;  // voice pool size; default 96
} AnoSynthDesc;

// Allocate the synth world on its own heap: voice pool, wavetable bank, bell
// sample, shimmer history. Logic-thread only. NULL desc = defaults.
AnoSynth *ano_synth_create(const AnoSynthDesc *desc);
void      ano_synth_destroy(AnoSynth *s);

// ---------------------------------------------------------------------------
// Score loading (logic thread, synth idle)
// ---------------------------------------------------------------------------
// Feed order: begin -> tempo points (monotonic) -> bars (ascending) -> events
// (prototype emission order; tie halves unmerged) -> end. `end` merges tie
// chains (one voice, one envelope per musical note), builds the full tempo map
// FIRST, then converts beats to frames into a deadline-sorted schedule with a
// stable sequence tiebreaker. Counts in `begin` size the exact allocations.

bool ano_synth_score_begin(AnoSynth *s, double barQuarters, uint32_t barCount,
                           uint32_t tempoCount, uint32_t eventCount);
bool ano_synth_score_tempo(AnoSynth *s, double beat, double bpm);
bool ano_synth_score_bar(AnoSynth *s, uint32_t bar, const AnoMusicalParams *p,
                         const AnoMusicAffect *a);
bool ano_synth_score_event(AnoSynth *s, const AnoNoteEvent *ev);
bool ano_synth_score_end(AnoSynth *s);

// Frames from score start until the last note's end plus `tailSeconds`.
// Valid after score_end.
uint64_t ano_synth_score_frames(const AnoSynth *s, float tailSeconds);

// Seconds from score start to `beat` through the loaded tempo map (the
// BeatClock's time_at). Valid after score_end; live drivers use it to pace
// command submission against the playhead.
double ano_synth_time_at(const AnoSynth *s, double beat);

// ---------------------------------------------------------------------------
// Transport + the generator
// ---------------------------------------------------------------------------

// Reset all runtime state (voices, cursors, smoothers, seeded rngs, shimmer
// history) and start the score at absolute world frame `worldFrame`. Callable
// only while the synth is idle — the generator touches no runtime state until
// this publishes. Offline renders start at 0.
void ano_synth_transport_start(AnoSynth *s, uint64_t worldFrame);

// Return to idle: the generator stops touching state at its next call (any
// block mid-render finishes first — stop, then wait one block before reloading
// a score under a live mixer). Offline renders may stop immediately.
void ano_synth_transport_stop(AnoSynth *s);

// The AnoAudioGenerator: pass as .generator with the synth as .generatorUser
// in AnoAudioConfig (realtime) or AnoAudioOfflineDesc (offline). Renders
// voices into the strip buses sample-accurately (spans split at every note
// onset and bar edge), ducks pad+arp under kicks, and feeds the shimmer lane.
void ano_synth_generator(void *user, float *const *busMix, uint32_t busCount,
                         uint32_t frames, uint64_t startFrame);

// Voices dropped because the pool was full (diagnostic; reset by transport).
uint32_t ano_synth_dropped(const AnoSynth *s);

// ---------------------------------------------------------------------------
// Live scoring (audio thread): bars streamed in as they are generated
// ---------------------------------------------------------------------------
// The score path above is a batch: the whole piece is loaded while the synth is
// idle, then played. Live is the same schedule built one bar at a time, so a
// conductor can drive it from inside the block loop and the music never has to
// exist ahead of time. The schedule becomes a ring; everything else — the tie
// merge, the beat clock, the deadline order, the span splitting — is the batch
// path's code, so the two produce bit-identical audio (the test asserts it).
//
// A bar must be appended before the playhead REACHES it, with one bar to spare:
// a note that ties out of bar N is not final until bar N+1 arrives to extend it,
// and it must not sound before then. `ano_synth_live_pending` counts the bars
// appended but not yet started, and the driver tops up while that is below
// ANO_SYNTH_LIVE_LOOKAHEAD. Falling behind does not corrupt the schedule — the
// tie dissolves into a plain note and ano_synth_live_late() counts it.
#define ANO_SYNTH_LIVE_LOOKAHEAD 2u

// Begin a live score. Callable only while idle (like score_begin). Follow with
// LOOKAHEAD bars, then ano_synth_transport_start.
bool ano_synth_live_begin(AnoSynth *s, double barQuarters);

// Append one bar: its tempo points (monotonic, absolute beats), its params and
// affect, and its events in the generator's emission order with tie halves
// UNMERGED — exactly an AnoBarResult. Bars must arrive in ascending order with
// no gaps. Called from the audio thread, from the same context that calls the
// generator (they share the schedule; nothing else may touch it).
bool ano_synth_live_bar(AnoSynth *s, uint32_t bar,
                        const AnoTempoPoint *tempo, uint32_t tempoCount,
                        const AnoMusicalParams *p, const AnoMusicAffect *a,
                        const AnoNoteEvent *events, uint32_t eventCount);

// Bars appended but not yet started at `worldFrame` — the driver's top-up gate.
uint32_t ano_synth_live_pending(const AnoSynth *s, uint64_t worldFrame);

// Ties that arrived too late to extend a note that had already sounded (they
// became plain notes), and notes dropped because the ring was full. Both are
// zero on a driver that respects the lookahead; non-zero means it fell behind.
uint32_t ano_synth_live_late(const AnoSynth *s);
uint32_t ano_synth_live_overflow(const AnoSynth *s);

// ---------------------------------------------------------------------------
// The music engine as the driver: `audio <- synth <- music`
// ---------------------------------------------------------------------------
// Attach a music engine and the generator feeds itself: at every block it tops
// the schedule up to ANO_SYNTH_LIVE_LOOKAHEAD bars, composing in-thread. The
// caller starts the transport and the piece plays for as long as it is asked
// to — no score, no ahead-of-time generation, no bound on its length.
//
// Attach performs the live_begin (the meter comes from the engine) and primes
// the lookahead, so a hand-rolled live_bar driver is only for a caller feeding
// bars from somewhere other than a music engine. Idle only. The engine must
// outlive the attachment; the synth does not own it, and once attached only the
// audio thread may touch it.
//
// The engine need NOT be at bar 0 — one restored to bar 900 simply starts the
// schedule from bar 900's music. The schedule is rebased, not renumbered: the
// engine keeps its own bar numbering (it is what spells its RNG streams, so
// renumbering would change the music), and the difference between that and the
// schedule's own count is carried as a constant beat offset. This is the same
// machinery ACMD_MUSIC_SEEK uses, which is why the two agree exactly.
bool ano_synth_attach_music(AnoSynth *s, AnoMusicEngine *music);

// Idle only. Bars already scheduled still play; the schedule simply stops
// growing, and the engine is the caller's again.
void ano_synth_detach_music(AnoSynth *s);

// What the composer has to say about the block just rendered, as audio events —
// AEVT_MUSIC_BAR for each bar that STARTED SOUNDING (the meaning is held back
// from composition, LOOKAHEAD bars early, to the barline it belongs to, so a
// game reacting to a cadence reacts when the cadence is audible), and
// AEVT_MUSIC_SEEKED when a seek snapshot has been consumed. The mixer drains it
// every block through ano_synth_poll; a caller that leaves it un-drained past
// ANO_SYNTH_EVENT_QUEUE loses the oldest.
#define ANO_SYNTH_EVENT_QUEUE 16u

// What composing a bar on the audio thread actually costs: the last bar, and
// the worst since transport_start, in microseconds. The number that matters is
// this against the block period — it is the whole risk of hosting the composer
// in the callback, and it is what the telemetry ships.
uint32_t ano_synth_music_bar_us(const AnoSynth *s);
uint32_t ano_synth_music_bar_us_max(const AnoSynth *s);

// The generator back-channel, implemented (anoptic_audio.h). Pass these as
// .generatorControl / .generatorPoll / .generatorStats with the synth as
// .generatorUser, and the attached music engine becomes steerable from the
// logic thread — ACMD_MUSIC_* down through the bridge, AEVT_MUSIC_BAR back up,
// the composer's cost in the telemetry frame. The engine itself is never
// touched off the audio thread. Offline takes .generatorControl only.
void     ano_synth_control(void *user, const AnoAudioCommand *cmd);
uint32_t ano_synth_poll(void *user, AnoAudioEvent *out, uint32_t cap);
void     ano_synth_stats(void *user, AnoAudioTelemetry *t);

// ---------------------------------------------------------------------------
// Console helpers
// ---------------------------------------------------------------------------

// Fill `out` with the ANO_SYNTH_CONSOLE_BUSES bus descriptors (master + console
// master + returns + strips + shimmer lane, static trims and sends baked in).
// Returns the bus count, 0 if cap is too small. Pass to AnoAudioConfig/
// OfflineDesc as busLayout with busCount = the return value.
uint32_t ano_synth_console_layout(AnoAudioBusDesc *out, uint32_t cap);

// One-time console setup commands (frame 0): per-strip EQ curves, glue-comp
// makeup, drive trim. Returns the event count written, 0 if cap < 64.
uint32_t ano_synth_console_setup(AnoAudioOfflineEvent *out, uint32_t cap);

// Per-bar console automation from the loaded score: send levels (per-layer
// static x global param), pad width, master drive, tempo-synced delay time.
// Frames are score-relative — offline passes them straight; a live driver adds
// the transport offset and submits ahead of the playhead. Returns the count
// written, 0 if cap is too small (9 events per bar). Valid after score_end.
uint32_t ano_synth_console_automation(const AnoSynth *s, AnoAudioOfflineEvent *out,
                                      uint32_t cap);

#endif // ANOPTIC_SYNTH_H
