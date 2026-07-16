/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private synth world: voice pool, patch constants, BeatClock, frame-stamped schedule, per-block controls (cutoff, sweeps, duck, shimmer).
// Logic thread while idle AND no live mixer hooks; mixer thread once any transport has started. Render: no alloc, no locks. Stochastic seeds at transport_start.
// transport_start only STAGES (epoch bump + startFrame publish); the first mixer-side hook after it runs the reset — logic must never touch runtime state past the first start.
// DSP primitives from src/audio/dsp/ (sanctioned cross-module private include).

#ifndef ANO_SYNTH_INTERNAL_H
#define ANO_SYNTH_INTERNAL_H

#include <stdatomic.h>

#include <anoptic_memory.h>
#include <anoptic_synth.h>

#include "../audio/dsp/smooth.h"
#include "../audio/dsp/svf.h"
#include "../audio/dsp/osc.h"
#include "../audio/dsp/env.h"
#include "../audio/dsp/noise.h"
#include "../audio/dsp/grain.h"

#define ANO_SYNTH_IDLE UINT64_MAX // transport sentinel: render nothing

#define ANO_SYNTH_MAX_SWEEPS 6  // overlapping one-shot cutoff sweeps
#define ANO_SYNTH_MAX_DUCKS  8  // overlapping kick duck envelopes
#define ANO_SYNTH_SPAN_MAX   4096u // per-span scratch (= audio block ceiling)

#define ANO_SYNTH_WT_FRAMES 4u
#define ANO_SYNTH_WT_LEN    2048u

typedef enum AnoSynthVoiceClass
{
    ANO_SYNTH_VC_PAD = 0, // 3-saw subtractive
    ANO_SYNTH_VC_WTPAD,   // morphing wavetable
    ANO_SYNTH_VC_BASS,    // saw + sub sine, filter-envelope pluck
    ANO_SYNTH_VC_LEAD,    // dual-osc, delayed vibrato
    ANO_SYNTH_VC_SAMPLER, // repitched bell
    ANO_SYNTH_VC_FM,      // 2-op FM pluck
    ANO_SYNTH_VC_CHIME,   // 5-partial tubular additive
    ANO_SYNTH_VC_DRUM,    // GM-pitch recipes
} AnoSynthVoiceClass;

typedef enum AnoSynthDrumKind
{
    ANO_SYNTH_DRUM_KICK = 0,
    ANO_SYNTH_DRUM_SNARE,
    ANO_SYNTH_DRUM_RIM,
    ANO_SYNTH_DRUM_CHAT,
    ANO_SYNTH_DRUM_OHAT,
    ANO_SYNTH_DRUM_TOM,
    ANO_SYNTH_DRUM_CRASH,
    ANO_SYNTH_DRUM_SHAKER,
} AnoSynthDrumKind;

typedef struct AnoSynthVoice
{
    bool     active;
    uint8_t  cls;   // AnoSynthVoiceClass
    uint8_t  layer; // AnoMusicLayer
    uint64_t age;   // frames rendered
    uint64_t total; // frames until slot frees
    float    freq;  // Hz at alloc (keytrack source)
    float    amp;   // (velocity/127)^1.5 x class trim
    float    panL, panR; // constant-power (mono voices)
    float    cutMult;    // keytrack x variant
    float    resQ;
    AnoDspAsr      env;
    AnoDspSvfCoef  fc;
    AnoDspSvfState f0, f1; // stereo; mono uses f0

    union {
        struct { // PAD
            float ph[3];
            float dt[3];
            float og[3][2]; // per-osc L/R
            bool  stereo;   // bright: filter per channel
        } pad;
        struct { // WTPAD
            float     ph, dt;
            float     morphAmp; // min(1, amp*1.4)
            AnoDspAsr morph;
        } wt;
        struct { // BASS
            float     sawPh, subPh;
            float     sweepBase, sweepScale;
            float     drive; // 0 = clean; else tanh(x*drive)*0.8
            AnoDspAsr fenv;
        } bass;
        struct { // LEAD
            float     ph1, ph2;
            AnoDspTri tri1, tri2;
            uint8_t   t1, t2; // 0 sine, 1 tri, 2 saw, 3 square
            float     a1, a2;
            float     vibPh, vibRate, vibDepth;
            uint64_t  vibDelay; // frames to full depth
        } lead;
        struct { // SAMPLER
            double cur, rate;
        } smp;
        struct { // FM
            float     carPh, modPh;
            float     ratio, index;
            AnoDspAsr menv;
        } fm;
        struct { // CHIME
            float     ph[5];
            AnoDspAsr penv[5];
            AnoDspAsr chiff;
            AnoDspSvfCoef  bpc;
            AnoDspSvfState bps;
            AnoDspRng rng;
        } chime;
        struct { // DRUM
            uint8_t   kind; // AnoSynthDrumKind
            float     ph;
            AnoDspAsr e2, e3;
            AnoDspRng rng, rng2;
            AnoDspSvfCoef  c1, c2;
            AnoDspSvfState s1, s2;
        } drum;
    } u;
} AnoSynthVoice;

typedef struct AnoSynthNote
{
    uint64_t frame; // score frames
    uint32_t seq;   // stable tiebreaker (post-merge order)
    float    durS;  // gated sounding seconds through tempo map
    AnoNoteEvent ev;
} AnoSynthNote;

typedef struct AnoSynthBar
{
    uint64_t frame; // score frames
    float    barSeconds;
    AnoMusicalParams params;
    AnoMusicAffect   affect;
    AnoMusicMeaning  meaning; // music-driven; rides to downbeat
    bool             hasMeaning;
} AnoSynthBar;

typedef struct AnoSynthAnchor
{
    double beat, time, bpm; // piecewise-constant until next
} AnoSynthAnchor;

#define ANO_SYNTH_MAGIC 0x53594E54u // 'SYNT'

struct AnoSynth
{
    uint32_t magic;
    uint32_t sampleRate;
    uint32_t maxVoices;
    float    smoothCoef; // ~30 ms one-pole

    float   *wtBank; // ANO_SYNTH_WT_FRAMES * ANO_SYNTH_WT_LEN
    float   *bell;
    uint64_t bellFrames;

    // Score: anchors/bars/notes are rings live, arrays batch. Counts/cursors absolute; mask = (cap-1) live, UINT32_MAX batch — same deadline loop / generator serve both.
    double         barQuarters;
    AnoSynthAnchor *anchors;
    uint32_t       anchorCount, anchorCap, anchorMask;
    AnoSynthBar    *bars;
    uint32_t       barCount, barCap, barMask;
    AnoNoteEvent   *raw; // batch staging (unmerged)
    uint32_t       rawCount, rawCap;
    AnoSynthNote   *notes; // merged, frame-stamped, deadline-ordered
    uint32_t       noteCount, noteCap, noteMask;
    uint64_t       lastNoteEnd; // score frames (batch)
    bool           scoreReady;

    // Live: open tie chains across barline -> absolute head note index.
    bool     live;
    uint32_t liveNextBar; // bar index live_bar expects next
    int32_t  openChain[ANO_MUSIC_LAYER_COUNT][128];
    uint32_t liveLate, liveOverflow;

    AnoMusicEngine *music; // borrowed
    AnoMusicBar     musicBar; // pump scratch (~7 KB)
    uint32_t        musicBarUs, musicBarUsMax;

    // Engine bar numbering vs schedule: constant beat offset (attach mid-piece / SEEK). Zero if schedule started at engine bar 0.
    double musicBeatOffset;

    // evt/cmd queues are mixer-thread-only once any transport has started: producers
    // (synth_emit / synth_apply_bar) and consumers (poll / commands) all run under the hooks.
    AnoAudioEvent evtQueue[ANO_SYNTH_EVENT_QUEUE];
    uint32_t      evtHead, evtTail; // absolute; head - tail = depth

#define ANO_SYNTH_CMD_QUEUE 32u
    // Live: console moves at sounding barline. Batch stamps via ano_synth_console_automation.
    AnoAudioCommand cmdQueue[ANO_SYNTH_CMD_QUEUE];
    uint32_t        cmdHead, cmdTail;

    _Atomic uint64_t startFrame;     // IDLE = generator touches nothing
    _Atomic uint64_t transportEpoch; // logic bumps per transport_start; staged-reset ticket
    uint64_t         epochSeen;      // mixer-side: last epoch whose reset has run

    uint32_t noteCursor, barCursor;
    uint32_t dropped;
    AnoSynthVoice *voices;

    AnoAudioSmooth cutoff;     // Hz
    AnoAudioSmooth duckDepth;  // 0.4 * energy^2
    AnoAudioSmooth shimGain;   // 0.35 * tension^2
    float          lfoPhase;   // 0.11 Hz -> cutoff +/-0.12
    float          sweepVal;
    float          lastBarCutoff;
    uint16_t       instruments[ANO_MUSIC_LAYER_COUNT];
    struct { AnoDspAsr env; int barsLeft; } sweeps[ANO_SYNTH_MAX_SWEEPS];
    AnoDspAsr ducks[ANO_SYNTH_MAX_DUCKS];
    bool      duckLive[ANO_SYNTH_MAX_DUCKS];

    AnoDspGrainEngine grain;
    float            *grainRing;
    uint32_t          grainCap;

    float *duckGain; // ANO_SYNTH_SPAN_MAX

    mi_heap_t *heap;
};


/* synth_voices.c */

// Configure voice from note. false = drop.
bool ano_synth_voice_spawn(AnoSynth *s, AnoSynthVoice *v, const AnoSynthNote *n);

// Per-span filter coef from staged cutoff for v's layer.
void ano_synth_voice_span_coef(AnoSynth *s, AnoSynthVoice *v, const float *staged);

// One sample; accumulate stereo.
void ano_synth_voice_step(AnoSynth *s, AnoSynthVoice *v, const float *staged,
                          float *l, float *r);

void ano_synth_bake_wavetable(float *bank);
void ano_synth_bake_bell(float *out, uint64_t frames, float sampleRate);

#endif // ANO_SYNTH_INTERNAL_H
