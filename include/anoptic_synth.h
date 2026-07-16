/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Synth: music IR (anoptic_music.h) into audio buses via AnoAudioGenerator.
// Owns voice pool, patches, BeatClock, deadline schedule. Console = bus layout/setup + ACMD_BUS_SET / ACMD_FX_SET.
// Score load + transport on logic thread while idle. Once started, generator (mixer thread) is sole runtime toucher: no alloc, lock, or bridge. Offline: same generator on caller via AnoAudioOfflineDesc.

#ifndef ANOPTIC_SYNTH_H
#define ANOPTIC_SYNTH_H

#include <stdint.h>
#include <stdbool.h>

#include <anoptic_audio.h>
#include <anoptic_music.h>


/* Console Shape */

// Fixed bus indices. Engine master = bus 0.
#define ANO_SYNTH_BUS_MASTER  1u // drive -> glue comp -> DC -> limiter
#define ANO_SYNTH_BUS_REVERB  2u // pure-wet FDN return
#define ANO_SYNTH_BUS_DELAY   3u // tempo-synced dotted-8th ping-pong
#define ANO_SYNTH_BUS_STRIP0  4u // + AnoMusicLayer: six strips, canonical order
#define ANO_SYNTH_BUS_SHIMMER 10u // granular shimmer (dry x0.2, full into reverb)
#define ANO_SYNTH_CONSOLE_BUSES 11u


/* Patches */

// Registry ids (MusicalParams.instruments). 0 = layer default (pad WARM, bass ROUND, melody SOFT, counter MELLOW, arp PLUCK). BREEZE / WHISTLE / BAD_GROUND reserved -> layer default.
typedef enum AnoSynthPatch
{
    ANO_SYNTH_PATCH_DEFAULT = 0,
    ANO_SYNTH_PATCH_WARM,       // pad: tight-detune 3-saw, mono, slow attack
    ANO_SYNTH_PATCH_BRIGHT,     // pad: wide-detune 3-saw, stereo, fast attack
    ANO_SYNTH_PATCH_MORPH,      // pad: 2D-morphing wavetable
    ANO_SYNTH_PATCH_BREEZE,     // pad texture (reserved)
    ANO_SYNTH_PATCH_ROUND,      // bass: saw + sub sine, filter-envelope pluck
    ANO_SYNTH_PATCH_DRIVEN,     // bass: same + tanh pre-drive
    ANO_SYNTH_PATCH_BAD_GROUND, // bass texture (reserved)
    ANO_SYNTH_PATCH_SOFT,       // lead: tri+saw, delayed vibrato 5.5 Hz
    ANO_SYNTH_PATCH_HARD,       // lead: saw+square, hotter vibrato
    ANO_SYNTH_PATCH_MELLOW,     // lead: tri+sine (countermelody)
    ANO_SYNTH_PATCH_KEYS,       // melody: repitched bell sampler
    ANO_SYNTH_PATCH_WHISTLE,    // melody texture (reserved)
    ANO_SYNTH_PATCH_PLUCK,      // arp: 2-op FM, ratio 3.007 index 1.8
    ANO_SYNTH_PATCH_GLASS,      // arp: 2-op FM, ratio 7.003 index 2.6
    ANO_SYNTH_PATCH_CHIMES,     // arp: 5-partial tubular additive
    ANO_SYNTH_PATCH_COUNT,
} AnoSynthPatch;

// Name <-> id. Unknown name -> 0.
uint32_t    ano_synth_patch_id(const char *name);
const char *ano_synth_patch_name(uint32_t id);

// AnoPatchName (music) -> AnoSynthPatch (this registry). Separate id spaces.
uint32_t ano_synth_patch_of(uint32_t musicPatch);


/* Lifecycle */

typedef struct AnoSynth AnoSynth;

typedef struct AnoSynthDesc
{
    uint32_t sampleRate; // = audio world rate; default 48000
    uint32_t maxVoices;  // pool size; default 96
} AnoSynthDesc;

// Own heap: voice pool, wavetable bank, bell sample, shimmer history. Logic thread. NULL desc = defaults.
AnoSynth *ano_synth_create(const AnoSynthDesc *desc);
void      ano_synth_destroy(AnoSynth *s);


/* Score Loading */

// Logic thread, synth idle. Order: begin -> tempo (monotonic) -> bars (ascending) -> events (emission order, ties unmerged) -> end.
// end: merge ties, build full tempo map, beats -> frames, deadline-sorted schedule (stable seq tiebreaker). begin counts size allocations.
bool ano_synth_score_begin(AnoSynth *s, double barQuarters, uint32_t barCount,
                           uint32_t tempoCount, uint32_t eventCount);
bool ano_synth_score_tempo(AnoSynth *s, double beat, double bpm);
bool ano_synth_score_bar(AnoSynth *s, uint32_t bar, const AnoMusicalParams *p,
                         const AnoMusicAffect *a);
bool ano_synth_score_event(AnoSynth *s, const AnoNoteEvent *ev);
bool ano_synth_score_end(AnoSynth *s);

// Score start -> last note end + tailSeconds. After score_end.
uint64_t ano_synth_score_frames(const AnoSynth *s, float tailSeconds);

// Score start -> beat via tempo map (BeatClock time_at). After score_end. Live: pace cmds against playhead.
double ano_synth_time_at(const AnoSynth *s, double beat);


/* Transport */

// Stage a start at worldFrame. The runtime reset (voices, cursors, smoothers, rngs, shimmer) lands on the rendering thread at its next hook, before the next rendered block. Idle only. Offline: worldFrame 0.
void ano_synth_transport_start(AnoSynth *s, uint64_t worldFrame);

// Idle: generator stops at next call. Live mixer: wait one block before reload. Offline: stop immediate.
void ano_synth_transport_stop(AnoSynth *s);

// AnoAudioGenerator: .generator + .generatorUser. Sample-accurate spans at note onsets and bar edges. Ducks pad+arp under kicks. Feeds shimmer.
void ano_synth_generator(void *user, float *const *busMix, uint32_t busCount,
                         uint32_t frames, uint64_t startFrame);

// Pool-full drops. Reset by transport.
uint32_t ano_synth_dropped(const AnoSynth *s);


/* Live Scoring */

// Live = same schedule as batch, one bar at a time (audio thread); schedule is a ring. Bit-identical (shared merge/clock/deadline/spans).
// Append before playhead reaches the bar. Keep pending >= ANO_SYNTH_LIVE_LOOKAHEAD (tie out of N needs N+1). Late does not corrupt: ties -> plain notes; ano_synth_live_late counts them.
#define ANO_SYNTH_LIVE_LOOKAHEAD 2u

// Idle only. Then LOOKAHEAD bars, then transport_start.
bool ano_synth_live_begin(AnoSynth *s, double barQuarters);

// One bar: tempo (monotonic absolute beats), params, affect, events (emission order, ties unmerged). Ascending, no gaps. Audio thread only; shares schedule with generator — nothing else may touch it.
// Wrapped drivers calling this before the block-0 hooks see pre-reset state: reset-transparent on a FRESH synth only; reuse requires an intervening hook.
bool ano_synth_live_bar(AnoSynth *s, uint32_t bar,
                        const AnoTempoPoint *tempo, uint32_t tempoCount,
                        const AnoMusicalParams *p, const AnoMusicAffect *a,
                        const AnoNoteEvent *events, uint32_t eventCount);

// Bars appended but not yet started at worldFrame — driver's top-up gate.
uint32_t ano_synth_live_pending(const AnoSynth *s, uint64_t worldFrame);

// Late ties (sounded before extend) and ring-full drops. Zero if LOOKAHEAD held.
uint32_t ano_synth_live_late(const AnoSynth *s);
uint32_t ano_synth_live_overflow(const AnoSynth *s);


/* Music Driver */

// Attach music engine: generator tops schedule to LOOKAHEAD in-thread. Idle only, and before the mixer goes live. Engine outlives attach; audio thread only after attach.
// Mid-piece attach/SEEK rebases schedule (not renumber) via constant beat offset. Same machinery as ACMD_MUSIC_SEEK.
bool ano_synth_attach_music(AnoSynth *s, AnoMusicEngine *music);

// Idle only. Scheduled bars still play; schedule stops growing.
void ano_synth_detach_music(AnoSynth *s);

// AEVT_MUSIC_BAR at barline when sounding starts (held from composition by LOOKAHEAD). AEVT_MUSIC_SEEKED on seek consume. Mixer drains via ano_synth_poll; undrained past queue drops oldest.
#define ANO_SYNTH_EVENT_QUEUE 16u

// Last / max bar composition cost (us) since transport_start.
uint32_t ano_synth_music_bar_us(const AnoSynth *s);
uint32_t ano_synth_music_bar_us_max(const AnoSynth *s);

// Generator back-channel (anoptic_audio.h): .generatorControl / .generatorPoll / .generatorStats + .generatorUser. Offline: .generatorControl only.
void     ano_synth_control(void *user, const AnoAudioCommand *cmd);
uint32_t ano_synth_poll(void *user, AnoAudioEvent *out, uint32_t cap);
void     ano_synth_stats(void *user, AnoAudioTelemetry *t);

// .generatorCommands: live console moves at sounding barline. Batch uses ano_synth_console_automation instead.
uint32_t ano_synth_commands(void *user, AnoAudioCommand *out, uint32_t cap);

// ACMD_MUSIC_* on a caller-owned engine (same mapping as ano_synth_control). ACMD_MUSIC_SEEK ignored.
bool ano_music_apply_command(AnoMusicEngine *e, const AnoAudioCommand *cmd);


/* Console Helpers */

// ANO_SYNTH_CONSOLE_BUSES bus descriptors. Return count, or 0 if cap too small.
uint32_t ano_synth_console_layout(AnoAudioBusDesc *out, uint32_t cap);

// Frame-0 setup: strip EQ, glue makeup, drive trim. Count written, or 0 if cap < 64.
uint32_t ano_synth_console_setup(AnoAudioOfflineEvent *out, uint32_t cap);

// Per-bar automation from loaded score (sends, pad width, drive, tempo-synced delay). Score-relative frames; live driver adds transport offset ahead of playhead. After score_end. Live piece: ano_synth_commands instead.
#define ANO_SYNTH_BAR_CMDS (ANO_MUSIC_LAYER_COUNT + 3u)
uint32_t ano_synth_console_automation(const AnoSynth *s, AnoAudioOfflineEvent *out,
                                      uint32_t cap);

#endif // ANOPTIC_SYNTH_H
