/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Bus insert-effect slots (private to src/audio/). Chain fixed at init.
// Process interleaved stereo in place. No alloc after init. Delay mem from module heap.

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
    AnoAudioSmooth threshold, ratio, makeup;
    float attackCoef, releaseCoef;
    float env;  // stereo-linked envelope
    float gain; // previous sample gain (feedback)
} AnoAudioFxComp;

typedef struct AnoAudioFxLim
{
    AnoAudioSmooth ceiling;
    float          releaseCoef;
    float          gain;
    uint32_t       lookahead; // samples (5 ms at init)
    AnoDspDelay    dl[2];
    AnoDspWinMax   wm;
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
    AnoDspDelay    pre;
    AnoDspAllpass  ap[2];
    AnoDspDelay    line[4];
    uint32_t       lineLen[4];
    float          lineSec[4];
    float          dampState[4];
    float          dampCoef;
    float          lineGain[4];
    AnoDspBiquad      shelfC;
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
    bool     bypass;
    float    fs;     // engine rate at init
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

// NONE = empty pass-through. false on alloc failure.
bool ano_audio_fx_init(AnoAudioFx *fx, uint32_t kind, mi_heap_t *heap,
                       uint32_t sampleRate, float coefBlock);

// Retarget one param at block boundary. Unknown/OOR clamped or dropped (debug warn).
void ano_audio_fx_set(AnoAudioFx *fx, uint32_t paramId, float value);

// Interleaved stereo in place. NONE/bypass = no-op.
void ano_audio_fx_process(AnoAudioFx *fx, float *stereo, uint32_t frames, uint32_t sampleRate);

#endif // ANO_AUDIO_FX_H
