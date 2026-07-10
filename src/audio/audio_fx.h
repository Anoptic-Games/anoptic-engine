/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * audio_fx.h (private to src/audio/)
 * The typed insert-effect framework behind a bus chain slot. Chains are fixed
 * at init (structural); parameters retarget live through per-block smoothers.
 * Every effect processes an interleaved-stereo block in place, keeps fully
 * initialized state (finding 8), validates parameter ranges loudly in debug
 * builds (finding 7), and never allocates after init — all delay memory is
 * carved from the module heap up front.
 */

#ifndef ANO_AUDIO_FX_H
#define ANO_AUDIO_FX_H

#include <anoptic_audio.h>
#include <mimalloc.h>

#include "dsp/smooth.h"
#include "dsp/svf.h"
#include "dsp/biquad.h"
#include "dsp/delay.h"
#include "dsp/dynamics.h"

typedef struct AnoAudioFxFilter
{
    uint32_t       mode; // AnoAudioFilterMode
    AnoAudioSmooth cutoff, q;
    AnoDspSvfCoef  c;
    AnoDspSvfState s[2];
} AnoAudioFxFilter;

typedef struct AnoAudioFxEq3
{
    AnoAudioSmooth lowDb, lowF, midDb, midF, midQ, highDb, highF;
    AnoDspBiquad      cl, cm, ch;
    AnoDspBiquadState sl[2], sm[2], sh[2];
} AnoAudioFxEq3;

typedef struct AnoAudioFxDc
{
    float R;             // pole (~5 Hz)
    float x1[2], y1[2];
} AnoAudioFxDc;

typedef struct AnoAudioFxDrive
{
    AnoAudioSmooth amount, trim;
} AnoAudioFxDrive;

typedef struct AnoAudioFxComp
{
    AnoAudioSmooth threshold, ratio, makeup; // makeup: specified and bounded, never implicit
    float attackCoef, releaseCoef;           // detector poles
    float env;                               // stereo-linked envelope (feedback topology)
    float gain;                              // previous sample's gain (one-sample feedback)
} AnoAudioFxComp;

typedef struct AnoAudioFxLim
{
    AnoAudioSmooth ceiling;
    float          releaseCoef;
    float          gain;      // current gain (instant attack, one-pole release)
    uint32_t       lookahead; // samples (5 ms, fixed at init)
    AnoDspDelay    dl[2];     // the delayed copy the gain applies to
    AnoDspWinMax   wm;        // gapless window max over |input|
} AnoAudioFxLim;

typedef struct AnoAudioFxChorus
{
    AnoAudioSmooth rate, depth, mix; // Hz, ms, 0..1
    double         phase;            // LFO cycles [0, 1)
    AnoDspDelay    dl[2];
} AnoAudioFxChorus;

typedef struct AnoAudioFxReverb
{
    AnoAudioSmooth predelayMs, t60, dampHz, mix;
    AnoDspDelay    pre;         // stereo input is mono-summed into the tank
    AnoDspAllpass  ap[2];       // diffusers
    AnoDspDelay    line[4];     // FDN lines (fixed inharmonic lengths)
    uint32_t       lineLen[4];
    float          lineSec[4];
    float          dampState[4];
    float          dampCoef;    // per-block from dampHz
    float          lineGain[4]; // per-block from t60
    AnoDspBiquad      shelfC;   // output high shelf (-4 dB @ 4.5 kHz)
    AnoDspBiquadState shelfS[2];
} AnoAudioFxReverb;

typedef struct AnoAudioFxPingpong
{
    AnoAudioSmooth timeMs, feedback, mix;
    AnoDspDelay    dl[2];
} AnoAudioFxPingpong;

typedef struct AnoAudioFxWidth
{
    AnoAudioSmooth amount; // 0 mono .. 1 unity .. 2 wide
} AnoAudioFxWidth;

typedef struct AnoAudioFx
{
    uint32_t kind;   // AnoAudioEffectKind
    bool     bypass; // ANO_AUDIO_P_BYPASS (instant)
    float    fs;     // engine rate, baked at init (time-constant setters need it)
    union {
        AnoAudioFxFilter   filter;
        AnoAudioFxEq3      eq3;
        AnoAudioFxDc       dc;
        AnoAudioFxDrive    drive;
        AnoAudioFxComp     comp;
        AnoAudioFxLim      lim;
        AnoAudioFxChorus   chorus;
        AnoAudioFxReverb   reverb;
        AnoAudioFxPingpong pp;
        AnoAudioFxWidth    width;
    } u;
} AnoAudioFx;

// in:  slot, AnoAudioEffectKind, module heap, engine rate, per-block smoother pole
// out: false on allocation failure. NONE initializes an empty pass-through slot.
bool ano_audio_fx_init(AnoAudioFx *fx, uint32_t kind, mi_heap_t *heap,
                       uint32_t sampleRate, float coefBlock);

// Retarget one parameter (mixer thread, block boundary). Unknown ids and
// out-of-range values are clamped or dropped loudly in debug builds.
void ano_audio_fx_set(AnoAudioFx *fx, uint32_t paramId, float value);

// Process an interleaved-stereo block in place. NONE/bypass is a no-op.
void ano_audio_fx_process(AnoAudioFx *fx, float *stereo, uint32_t frames, uint32_t sampleRate);

#endif // ANO_AUDIO_FX_H
