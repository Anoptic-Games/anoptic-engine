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
