/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_control.h (private to src/music/)
 * Tier-1 affect levers and the affect -> musical-parameter mapping table
 * (musicgen/control/levers.py + mapping.py). MappingTable is THE tunable
 * design artifact: every perceptual constant lives here, and every mapper is
 * a pure function of (affect, table). Slew and hysteresis STATE live in the
 * conductor; the hysteresis RULES live here. Draw-free, but float op order
 * and comparator strictness (> vs >=) still bind: targets feed rounds,
 * thresholds, and eventually stream-tagged draws downstream.
 */

#ifndef ANO_MUSIC_CONTROL_H
#define ANO_MUSIC_CONTROL_H

#include <anoptic_music.h>

#include "music_theory.h"

// The game-facing API: three floats (doubles here; Python floats are).
typedef struct AnoAffect
{
    double valence; // -1 .. +1
    double energy;  //  0 .. 1
    double tension; //  0 .. 1
} AnoAffect;

static inline AnoAffect ano_affect_default(void)
{
    return (AnoAffect){ 0.0, 0.5, 0.3 };
}

AnoAffect ano_affect_clamped(AnoAffect a);

// (layer, energy threshold): hysteresis keeps a live layer on until energy
// drops below threshold - layerHysteresis.
typedef struct AnoLayerGate
{
    uint8_t layer; // AnoMusicLayer
    double  threshold;
} AnoLayerGate;

typedef struct AnoInstrumentTier
{
    uint8_t patch; // AnoPatchName
    double  threshold;
} AnoInstrumentTier;

typedef struct AnoInstrumentRow
{
    uint8_t layer; // AnoMusicLayer
    uint8_t tierCount;
    AnoInstrumentTier tiers[3];
} AnoInstrumentRow;

typedef struct AnoMappingTable
{
    // tempo (BPM): dominated by energy, tinted by valence
    double tempoBase;        // 70.0
    double tempoEnergy;      // 80.0
    double tempoValence;     // 8.0
    double tempoRange[2];    // 60.0, 160.0
    double tempoSlewPerBeat; // 2.0

    // melody register center (MIDI)
    double registerBase;    // 72.0
    double registerValence; // 4.0
    double registerTension; // 2.0

    // note density / rhythmic roughness
    double densityBase;      // 0.15
    double densityEnergy;    // 0.75
    double roughnessBase;    // 0.10
    double roughnessEnergy;  // 0.30
    double roughnessTension; // 0.20
    double roughnessMax;     // 0.60

    // articulation gate: legato 1.05 .. staccato 0.45
    double articulationLegato;     // 1.05
    double articulationEnergyDrop; // 0.60
    double articulationSlewPerBar; // 0.15

    // dynamics
    double velocityBase;       // 56.0
    double velocityEnergy;     // 44.0
    double velocitySlewPerBar; // 10.0
    double accentBase;         // 4.0
    double accentEnergy;       // 14.0

    uint8_t      layerGateCount; // 5
    AnoLayerGate layerGates[8];
    double       layerHysteresis; // 0.10

    // mode selection (brightness axis), phrase-quantized in the conductor
    double modeHysteresis; // 0.60

    // instrument swaps by energy, phrase-quantized in the conductor
    uint8_t          instrumentRowCount; // 4
    AnoInstrumentRow instrumentRows[ANO_MUSIC_LAYER_COUNT];
    double           instrumentHysteresis; // 0.08

    // cadence policy by tension
    double cadenceAuthenticMax; // 0.35
    double cadenceHalfMax;      // 0.65

    // slow harmonic rhythm (2-bar chords) when calm
    double harmonicSlowEnergy;  // 0.30
    double harmonicSlowTension; // 0.50

    // DSP tier
    double cutoffBaseHz;         // 350.0
    double cutoffEnergyOctaves;  // 4.2
    double cutoffValenceOctaves; // 0.5
    double reverbSendBase;       // 0.10
    double reverbSendTension;    // 0.30
    double reverbSendStillness;  // 0.18
    double delaySendBase;        // 0.04
    double delaySendActivity;    // 0.24
    double driveBase;            // 0.05
    double driveEnergy;          // 0.45
    double widthBase;            // 0.55
    double widthValence;         // 0.25
} AnoMappingTable;

AnoMappingTable ano_mapping_table_default(void);

double ano_map_tempo(AnoAffect a, const AnoMappingTable *t);
int    ano_map_register(AnoAffect a, const AnoMappingTable *t);
double ano_map_density(AnoAffect a, const AnoMappingTable *t);
double ano_map_roughness(AnoAffect a, const AnoMappingTable *t);
double ano_map_articulation(AnoAffect a, const AnoMappingTable *t);
double ano_map_velocity(AnoAffect a, const AnoMappingTable *t);
int    ano_map_accent(AnoAffect a, const AnoMappingTable *t);

// Valence -1..+1 mapped onto the EMS brightness axis -2..+3.
double ano_map_brightness_target(double valence);

// Nearest usable mode by brightness; float-abs ties break brighter-first
// (the prototype's min() key tuple over BRIGHTNESS insertion order).
AnoMode ano_nearest_mode(double valence);

// Deadband: stay on current until the target drifts >= modeHysteresis away.
// current == ANO_MODE_NONE is the prototype's None (first pick).
AnoMode ano_pick_mode(AnoMode current, double valence, const AnoMappingTable *t);

// Step up the brightness ordering, clamped at lydian; locrian passes through.
AnoMode ano_brighter_mode(AnoMode mode, int steps);

// Energy-tiered patch per layer with a deadband: the sitting patch's
// threshold is lowered by instrumentHysteresis. current/out are per-layer
// AnoPatchName arrays; layers absent from the table come out ANO_PATCH_NONE.
void ano_pick_instruments(const uint8_t current[ANO_MUSIC_LAYER_COUNT], double energy,
                          const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT]);

// Layer gates in table order; membership in current[] arms the hysteresis.
// Returns the gated count. energy is compared STRICTLY > (the prototype's).
uint32_t ano_gate_layers(const uint8_t *current, uint32_t currentCount, double energy,
                         const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT]);

AnoCadencePolicy ano_pick_cadence_policy(double tension, const AnoMappingTable *t);

double ano_map_harmonic_rhythm(AnoAffect a, const AnoMappingTable *t);

double ano_music_slew(double current, double target, double maxStep);

// DSP tier
double ano_map_filter_cutoff(AnoAffect a, const AnoMappingTable *t);
double ano_map_reverb_send(AnoAffect a, const AnoMappingTable *t);
double ano_map_delay_send(AnoAffect a, const AnoMappingTable *t);
double ano_map_drive(AnoAffect a, const AnoMappingTable *t);
double ano_map_stereo_width(AnoAffect a, const AnoMappingTable *t);

#endif // ANO_MUSIC_CONTROL_H
