/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Affect levers + affect -> Tier-2 mapping. Pure functions of (affect, table).
// Slew/hysteresis state lives in the conductor; rules live here. Draw-free.
// Float op order and > vs >= bind (gate_layers >, pick_instruments >=).

#ifndef ANO_MUSIC_CONTROL_H
#define ANO_MUSIC_CONTROL_H

#include <anoptic_music.h>

#include "music_theory.h"

// Doubles here (Python floats are). Affect axes for mappers.
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

// Live layer held until energy drops below threshold - layerHysteresis.
double ano_map_tempo(AnoAffect a, const AnoMappingTable *t);
int    ano_map_register(AnoAffect a, const AnoMappingTable *t);
double ano_map_density(AnoAffect a, const AnoMappingTable *t);
double ano_map_roughness(AnoAffect a, const AnoMappingTable *t);
double ano_map_articulation(AnoAffect a, const AnoMappingTable *t);
double ano_map_velocity(AnoAffect a, const AnoMappingTable *t);
int    ano_map_accent(AnoAffect a, const AnoMappingTable *t);

// Valence -1..+1 -> EMS brightness axis -2..+3.
double ano_map_brightness_target(double valence);

// Nearest usable mode by brightness; float-abs ties break brighter-first
// (min key (|delta|, -brightness) over BRIGHTNESS insertion order).
AnoMode ano_nearest_mode(double valence);

// Deadband: stay until target drifts >= modeHysteresis. NONE = first pick.
AnoMode ano_pick_mode(AnoMode current, double valence, const AnoMappingTable *t);

// Step up brightness ordering, clamped at lydian; locrian passes through.
AnoMode ano_brighter_mode(AnoMode mode, int steps);

// Energy-tiered patch per layer with instrumentHysteresis deadband.
void ano_pick_instruments(const uint8_t current[ANO_MUSIC_LAYER_COUNT], double energy,
                          const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT]);

// Layer gates in table order. energy compared STRICTLY >. Returns gated count.
uint32_t ano_gate_layers(const uint8_t *current, uint32_t currentCount, double energy,
                         const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT]);

AnoCadencePolicy ano_pick_cadence_policy(double tension, const AnoMappingTable *t);

double ano_map_harmonic_rhythm(AnoAffect a, const AnoMappingTable *t);

double ano_music_slew(double current, double target, double maxStep);

/* DSP tier */
double ano_map_filter_cutoff(AnoAffect a, const AnoMappingTable *t);
double ano_map_reverb_send(AnoAffect a, const AnoMappingTable *t);
double ano_map_delay_send(AnoAffect a, const AnoMappingTable *t);
double ano_map_drive(AnoAffect a, const AnoMappingTable *t);
double ano_map_stereo_width(AnoAffect a, const AnoMappingTable *t);

#endif // ANO_MUSIC_CONTROL_H
