/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * synth_internal.h (private to src/synth/)
 * The synth world: voice pool, patch constants, BeatClock, the frame-stamped
 * schedule, and the shared per-block controls (cutoff bus, sweeps, duck,
 * shimmer). Owned by the logic thread while idle; by the generator's calling
 * thread (the mixer) once the transport starts. The render path performs zero
 * allocation and takes zero locks; every stochastic element is seeded at
 * transport start (finding 8).
 *
 * Depends on the audio module's DSP primitive library (src/audio/dsp/) — the
 * one sanctioned cross-module private include (AUDIO_PLAN §2.2: synth depends
 * on audio's buses and DSP lib).
 */

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

#define ANO_SYNTH_IDLE UINT64_MAX // transport start sentinel: render nothing

// Pool ceilings beyond the voice pool (all preallocated).
#define ANO_SYNTH_MAX_SWEEPS 6  // overlapping one-shot cutoff sweeps
#define ANO_SYNTH_MAX_DUCKS  8  // overlapping kick duck envelopes
#define ANO_SYNTH_SPAN_MAX   4096u // per-span scratch (the audio block ceiling)

// Wavetable bank shape (prototype: 4 frames x 2048, harmonics 1..23).
#define ANO_SYNTH_WT_FRAMES 4u
#define ANO_SYNTH_WT_LEN    2048u

// Voice classes (patches map onto these; constants live in synth_voices.c).
typedef enum AnoSynthVoiceClass
{
    ANO_SYNTH_VC_PAD = 0, // 3-saw subtractive (warm/bright)
    ANO_SYNTH_VC_WTPAD,   // morphing wavetable
    ANO_SYNTH_VC_BASS,    // saw + sub sine, filter-envelope pluck (round/driven)
    ANO_SYNTH_VC_LEAD,    // dual-osc, delayed vibrato (soft/hard/mellow)
    ANO_SYNTH_VC_SAMPLER, // repitched bell
    ANO_SYNTH_VC_FM,      // 2-op FM pluck (pluck/glass)
    ANO_SYNTH_VC_CHIME,   // 5-partial tubular additive
    ANO_SYNTH_VC_DRUM,    // GM-pitch-keyed recipes
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
    uint64_t total; // frames until the slot frees
    float    freq;  // Hz at allocation (keytracking baked from it)
    float    amp;   // (velocity/127)^1.5 x class trim
    float    panL, panR; // constant-power gains (mono voices)
    float    cutMult;    // baked keytrack x variant multiplier
    float    resQ;       // filter Q
    AnoDspAsr      env;  // amplitude envelope (one-shot, sized at alloc)
    AnoDspSvfCoef  fc;   // main filter coef (updated per span or per sample)
    AnoDspSvfState f0, f1; // stereo filter state (mono voices use f0)

    union {
        struct { // PAD
            float ph[3];
            float dt[3];       // per-osc phase increments
            float og[3][2];    // per-osc L/R pan gains
            bool  stereo;      // bright: filter runs per channel
        } pad;
        struct { // WTPAD
            float     ph, dt;
            float     morphAmp; // min(1, amp*1.4)
            AnoDspAsr morph;
        } wt;
        struct { // BASS
            float     sawPh, subPh;
            float     sweepBase, sweepScale;
            float     drive;   // 0 = clean; else tanh(x*drive)*0.8
            AnoDspAsr fenv;
        } bass;
        struct { // LEAD
            float     ph1, ph2;
            AnoDspTri tri1, tri2;
            uint8_t   t1, t2;  // 0 sine, 1 tri, 2 saw, 3 square
            float     a1, a2;
            float     vibPh, vibRate, vibDepth;
            uint64_t  vibDelay; // frames over which depth ramps in
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
            AnoDspAsr e2, e3; // recipe-specific secondary envelopes
            AnoDspRng rng, rng2;
            AnoDspSvfCoef  c1, c2;
            AnoDspSvfState s1, s2;
        } drum;
    } u;
} AnoSynthVoice;

// Merged, frame-stamped note (the schedule entry).
typedef struct AnoSynthNote
{
    uint64_t frame; // score frames
    uint32_t seq;   // stable tiebreaker (post-merge emission order)
    float    durS;  // gated sounding seconds through the tempo map
    AnoNoteEvent ev;
} AnoSynthNote;

typedef struct AnoSynthBar
{
    uint64_t frame; // score frames
    float    barSeconds;
    AnoMusicalParams params;
    AnoMusicAffect   affect;
    AnoMusicMeaning  meaning; // music-driven only; rides to the bar's downbeat
    bool             hasMeaning;
} AnoSynthBar;

typedef struct AnoSynthAnchor
{
    double beat, time, bpm; // piecewise-constant from beat until the next anchor
} AnoSynthAnchor;

// The generator hooks take a void* the caller supplies (anoptic_audio.h), so the
// compiler cannot check what arrives. This can: a wrong pointer is otherwise a
// silent no-op — the worst kind of bug, because the audio keeps playing.
#define ANO_SYNTH_MAGIC 0x53594E54u // 'SYNT'

struct AnoSynth
{
    uint32_t magic;
    uint32_t sampleRate;
    uint32_t maxVoices;
    float    smoothCoef; // per-sample one-pole for the ~30 ms window

    // Baked banks (create-time, deterministic content).
    float   *wtBank;     // ANO_SYNTH_WT_FRAMES * ANO_SYNTH_WT_LEN
    float   *bell;       // bellFrames mono at sampleRate
    uint64_t bellFrames;

    // Score (score_begin/end or live_begin lifetime; exact-count allocations).
    //
    // anchors/bars/notes are RINGS in live mode and plain arrays in batch. The
    // counts and cursors are ABSOLUTE in both, and the mask is what differs:
    // (cap - 1) live, UINT32_MAX batch — so `x[i & mask]` is `x[i]` in batch and
    // the same cursors, the same deadline loop and the same generator serve both.
    double         barQuarters;
    AnoSynthAnchor *anchors;
    uint32_t       anchorCount, anchorCap, anchorMask;
    AnoSynthBar    *bars;
    uint32_t       barCount, barCap, barMask;
    AnoNoteEvent   *raw;      // batch load staging (unmerged)
    uint32_t       rawCount, rawCap;
    AnoSynthNote   *notes;    // merged + frame-stamped + deadline-ordered
    uint32_t       noteCount, noteCap, noteMask;
    uint64_t       lastNoteEnd; // score frames (batch)
    bool           scoreReady;

    // Live mode: the tie chains still open across the barline, keyed as in
    // merge_ties but holding the ABSOLUTE note index of the chain's head so a
    // continuation arriving with the next bar can still extend it.
    bool     live;
    uint32_t liveNextBar; // the bar index live_bar expects next
    int32_t  openChain[ANO_MUSIC_LAYER_COUNT][128];
    uint32_t liveLate, liveOverflow;

    // The attached composer (borrowed). musicBar is the pump's landing pad —
    // 7 KB, too fat for the audio stack. The meaning queue holds each bar's
    // annotation from the moment it is composed to the moment it sounds.
    AnoMusicEngine *music;
    AnoMusicBar     musicBar;
    uint32_t        musicBarUs, musicBarUsMax;
    AnoMusicMeaning meaningQueue[ANO_SYNTH_MEANING_QUEUE];
    uint32_t        meaningHead, meaningTail; // absolute; head - tail = depth

    // Transport. IDLE = generator renders nothing and touches nothing.
    _Atomic uint64_t startFrame;

    // Runtime (generator-owned once started; reset by transport_start).
    uint32_t noteCursor, barCursor;
    uint32_t dropped;
    AnoSynthVoice *voices;

    // Shared controls (the prototype's console control nodes).
    AnoAudioSmooth cutoff;     // Hz, per-sample glide toward the bar target
    AnoAudioSmooth duckDepth;  // 0.4 * energy^2
    AnoAudioSmooth shimGain;   // 0.35 * tension^2
    float          lfoPhase;   // 0.11 Hz sine -> cutoff ratio +/-0.12
    float          sweepVal;   // summed sweep envelopes, cached per sample
    float          lastBarCutoff; // sweep trigger memory
    uint16_t       instruments[ANO_MUSIC_LAYER_COUNT]; // current patch ids
    struct { AnoDspAsr env; int barsLeft; } sweeps[ANO_SYNTH_MAX_SWEEPS];
    AnoDspAsr ducks[ANO_SYNTH_MAX_DUCKS];
    bool      duckLive[ANO_SYNTH_MAX_DUCKS];

    // Shimmer lane (granular over the pad strip's history).
    AnoDspGrainEngine grain;
    float            *grainRing;
    uint32_t          grainCap;

    // Per-span scratch.
    float *duckGain; // ANO_SYNTH_SPAN_MAX

    mi_heap_t *heap;
};

// --- synth_voices.c ---

// Resolve patch/layer to a configured voice in `v` (constants baked, envelopes
// sized, rngs seeded). freq/amp/durS from the note. false = drop (unknown).
bool ano_synth_voice_spawn(AnoSynth *s, AnoSynthVoice *v, const AnoSynthNote *n);

// Update per-span filter coefficients from the staged cutoff for v's layer.
void ano_synth_voice_span_coef(AnoSynth *s, AnoSynthVoice *v, const float *staged);

// One sample; accumulates the voice's stereo contribution.
void ano_synth_voice_step(AnoSynth *s, AnoSynthVoice *v, const float *staged,
                          float *l, float *r);

// Bake the deterministic banks at create time.
void ano_synth_bake_wavetable(float *bank);
void ano_synth_bake_bell(float *out, uint64_t frames, float sampleRate);

#endif // ANO_SYNTH_INTERNAL_H
