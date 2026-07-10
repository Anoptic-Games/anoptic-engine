/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_music.h
 * @brief The music IR: the authoritative event schema shared by the generation
 *        core (src/music/, TECH_SPEC port) and the synthesizer (src/synth/).
 *
 * Header-level dependency only — this header exists before the music module
 * has an implementation, because the IR is the synth's input type (TECH_SPEC
 * section 4). It carries the playable core of a NoteEvent; the inspection
 * annotations (degree, chord symbol, role) live in a dev-build sidecar that
 * arrives with the music module, never here.
 *
 * Units: `start` and `dur` are quarter-note beats from piece start. Beats map
 * to seconds only through the piecewise-constant tempo map (BeatClock,
 * TECH_SPEC section 11.1), built in full before any event is scheduled.
 */

#ifndef ANOPTIC_MUSIC_H
#define ANOPTIC_MUSIC_H

#include <stdint.h>

// The six layers, canonical order, fixed (TECH_SPEC 4.1).
typedef enum AnoMusicLayer
{
    ANO_MUSIC_PAD = 0,
    ANO_MUSIC_BASS,
    ANO_MUSIC_MELODY,
    ANO_MUSIC_COUNTER,
    ANO_MUSIC_ARP,
    ANO_MUSIC_PERC,
    ANO_MUSIC_LAYER_COUNT,
} AnoMusicLayer;

// Tie flags (TECH_SPEC 4.2). A note crossing a barline is a chain of grid- and
// bar-legal halves flagged out -> both... -> in; the chain IS one musical note
// and merge_ties (synth-side) recovers it. An orphan out dissolves into a
// plain note; an orphan in passes through struck.
typedef enum AnoMusicTie
{
    ANO_MUSIC_TIE_NONE = 0,
    ANO_MUSIC_TIE_OUT,
    ANO_MUSIC_TIE_IN,
    ANO_MUSIC_TIE_BOTH,
} AnoMusicTie;

// The playable core of one note. Articulation is applied by the generation
// side's modifier layer before events reach this schema — `dur` is the gated
// sounding duration; a voice's own release tail extends past it.
typedef struct AnoNoteEvent
{
    double  start;    // absolute beats, >= 0
    double  dur;      // beats, > 0
    uint8_t pitch;    // MIDI 0..127
    uint8_t velocity; // 1..127
    uint8_t layer;    // AnoMusicLayer
    uint8_t tie;      // AnoMusicTie
} AnoNoteEvent;

// One tempo anchor: piecewise-constant bpm from `beat` until the next anchor.
// Anchors are monotonic in beat; a point at an existing anchor's beat replaces
// its bpm. Cadence ritardandi are realized purely as extra points.
typedef struct AnoTempoPoint
{
    double beat;
    double bpm;
} AnoTempoPoint;

// The affect triple published to the control plane per bar. The synth consumes
// it for the console's affect-coupled stages (duck depth, shimmer gain and
// density, texture voices).
typedef struct AnoMusicAffect
{
    float valence; // -1 .. 1
    float energy;  //  0 .. 1
    float tension; //  0 .. 1
} AnoMusicAffect;

// The Tier-2 parameter block, produced per bar by the control plane (TECH_SPEC
// 4.4; defaults are tuning). An immutable value struct: the conductor derives
// variants by copy. The DSP-tier fields (filterCutoff .. stereoWidth) reach
// the audio library as retarget values, never per-sample automation. String
// fields of the prototype are interned: `instruments` holds per-layer patch
// ids from the synth's registry (anoptic_synth.h), 0 = the layer's default.
typedef struct AnoMusicalParams
{
    double   tempoBpm;         // 100.0
    float    noteDensity;      // 0.5
    float    roughness;        // 0.0
    float    articulation;     // 0.9 (already baked into event durations)
    uint8_t  velocityCenter;   // 80
    uint8_t  accentDepth;      // 12
    uint8_t  registerCenter;   // 72
    uint8_t  layersActive;     // bitmask by AnoMusicLayer; bit set = sounding
    float    harmonicRhythm;   // 1.0
    float    dissonanceBudget; // 0.0
    uint16_t instruments[ANO_MUSIC_LAYER_COUNT]; // synth patch ids; 0 = default

    // DSP tier
    float filterCutoff; // Hz, 2500.0
    float reverbSend;   // 0.20 (global multiplier on per-layer static sends)
    float delaySend;    // 0.10 (same)
    float drive;        // 0.15
    float stereoWidth;  // 0.70
} AnoMusicalParams;

#endif // ANOPTIC_MUSIC_H
