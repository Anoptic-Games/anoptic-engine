/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Affect -> Tier-2 mappers. gate_layers: energy >; pick_instruments: >=.
// nearest_mode: BRIGHTNESS insertion order, min key (|delta|, -brightness). Rounds are banker's.

#include <math.h>

#include <string.h>

#include "music_control.h"
#include "music_ir.h" // AnoPatchName: the value domain of instrument arrays

static double clampd(double x, double lo, double hi)
{
    // Python _clamp: max(lo, min(hi, x))
    double m = x < hi ? x : hi;
    return m > lo ? m : lo;
}

AnoAffect ano_affect_clamped(AnoAffect a)
{
    return (AnoAffect){
        .valence = clampd(a.valence, -1.0, 1.0),
        .energy  = clampd(a.energy, 0.0, 1.0),
        .tension = clampd(a.tension, 0.0, 1.0),
    };
}

AnoMappingTable ano_mapping_table_default(void)
{
    AnoMappingTable z;
    memset(&z, 0, sizeof z); // padding is part of the engine's snapshot
    AnoMappingTable t = {
        .tempoBase = 70.0, .tempoEnergy = 80.0, .tempoValence = 8.0,
        .tempoRange = { 60.0, 160.0 }, .tempoSlewPerBeat = 2.0,
        .registerBase = 72.0, .registerValence = 4.0, .registerTension = 2.0,
        .densityBase = 0.15, .densityEnergy = 0.75,
        .roughnessBase = 0.10, .roughnessEnergy = 0.30,
        .roughnessTension = 0.20, .roughnessMax = 0.60,
        .articulationLegato = 1.05, .articulationEnergyDrop = 0.60,
        .articulationSlewPerBar = 0.15,
        .velocityBase = 56.0, .velocityEnergy = 44.0, .velocitySlewPerBar = 10.0,
        .accentBase = 4.0, .accentEnergy = 14.0,
        .layerGateCount = 5,
        .layerGates = {
            { ANO_MUSIC_PAD, -1.0 },
            { ANO_MUSIC_BASS, 0.12 },
            { ANO_MUSIC_MELODY, 0.28 },
            { ANO_MUSIC_PERC, 0.34 },
            { ANO_MUSIC_ARP, 0.62 },
        },
        .layerHysteresis = 0.10,
        .modeHysteresis = 0.60,
        .instrumentRowCount = 4,
        .instrumentRows = {
            { ANO_MUSIC_PAD, 2, { { ANO_PATCH_WARM, 0.0 }, { ANO_PATCH_BRIGHT, 0.60 } } },
            { ANO_MUSIC_BASS, 2, { { ANO_PATCH_ROUND, 0.0 }, { ANO_PATCH_DRIVEN, 0.62 } } },
            { ANO_MUSIC_MELODY, 2, { { ANO_PATCH_SOFT, 0.0 }, { ANO_PATCH_HARD, 0.55 } } },
            { ANO_MUSIC_ARP, 2, { { ANO_PATCH_PLUCK, 0.0 }, { ANO_PATCH_GLASS, 0.72 } } },
        },
        .instrumentHysteresis = 0.08,
        .cadenceAuthenticMax = 0.35, .cadenceHalfMax = 0.65,
        .harmonicSlowEnergy = 0.30, .harmonicSlowTension = 0.50,
        .cutoffBaseHz = 350.0, .cutoffEnergyOctaves = 4.2, .cutoffValenceOctaves = 0.5,
        .reverbSendBase = 0.10, .reverbSendTension = 0.30, .reverbSendStillness = 0.18,
        .delaySendBase = 0.04, .delaySendActivity = 0.24,
        .driveBase = 0.05, .driveEnergy = 0.45,
        .widthBase = 0.55, .widthValence = 0.25,
    };
    memcpy(&z, &t, sizeof t);
    return z;
}

AnoMappingTable ano_mapping_table_electronic(void)
{
    // Same defaults; different instrument rows (timbres may cross layer grain).
    AnoMappingTable t = ano_mapping_table_default();
    t.instrumentRows[0] = (AnoInstrumentRow){
        ANO_MUSIC_PAD, 2, { { ANO_PATCH_WARM, 0.0 }, { ANO_PATCH_BRIGHT, 0.60 } } };
    t.instrumentRows[1] = (AnoInstrumentRow){
        ANO_MUSIC_BASS, 2, { { ANO_PATCH_MORPH, 0.0 }, { ANO_PATCH_ROUND, 0.62 } } };
    t.instrumentRows[2] = (AnoInstrumentRow){
        ANO_MUSIC_MELODY, 2, { { ANO_PATCH_ROUND, 0.0 }, { ANO_PATCH_DRIVEN, 0.55 } } };
    t.instrumentRows[3] = (AnoInstrumentRow){
        ANO_MUSIC_ARP, 2, { { ANO_PATCH_PLUCK, 0.0 }, { ANO_PATCH_SOFT, 0.72 } } };
    return t;
}

double ano_map_tempo(AnoAffect a, const AnoMappingTable *t)
{
    double raw = t->tempoBase + t->tempoEnergy * a.energy + t->tempoValence * a.valence;
    return clampd(raw, t->tempoRange[0], t->tempoRange[1]);
}

int ano_map_register(AnoAffect a, const AnoMappingTable *t)
{
    return (int)ano_music_round_int(
        t->registerBase + t->registerValence * a.valence + t->registerTension * a.tension);
}

double ano_map_density(AnoAffect a, const AnoMappingTable *t)
{
    return clampd(t->densityBase + t->densityEnergy * a.energy, 0.0, 1.0);
}

double ano_map_roughness(AnoAffect a, const AnoMappingTable *t)
{
    double raw = t->roughnessBase + t->roughnessEnergy * a.energy
               + t->roughnessTension * a.tension;
    return clampd(raw, 0.0, t->roughnessMax);
}

double ano_map_articulation(AnoAffect a, const AnoMappingTable *t)
{
    return t->articulationLegato - t->articulationEnergyDrop * a.energy;
}

double ano_map_velocity(AnoAffect a, const AnoMappingTable *t)
{
    return t->velocityBase + t->velocityEnergy * a.energy;
}

int ano_map_accent(AnoAffect a, const AnoMappingTable *t)
{
    return (int)ano_music_round_int(t->accentBase + t->accentEnergy * a.energy);
}

double ano_map_brightness_target(double valence)
{
    return -2.0 + 5.0 * (valence + 1.0) / 2.0;
}

// BRIGHTNESS insertion order, bright to dark (locrian absent).
static const struct { AnoMode mode; int b; } EMS[6] = {
    { ANO_MODE_LYDIAN, 3 },  { ANO_MODE_IONIAN, 2 },  { ANO_MODE_MIXOLYDIAN, 1 },
    { ANO_MODE_DORIAN, 0 },  { ANO_MODE_AEOLIAN, -1 }, { ANO_MODE_PHRYGIAN, -2 },
};

AnoMode ano_nearest_mode(double valence)
{
    double target = ano_map_brightness_target(valence);
    AnoMode best = EMS[0].mode;
    double bestAbs = fabs(EMS[0].b - target);
    int bestNeg = -EMS[0].b;
    for (int i = 1; i < 6; ++i) {
        double d = fabs(EMS[i].b - target);
        int neg = -EMS[i].b;
        // tuple key (|delta|, -brightness), strict <: first minimum wins
        if (d < bestAbs || (d == bestAbs && neg < bestNeg)) {
            best = EMS[i].mode;
            bestAbs = d;
            bestNeg = neg;
        }
    }
    return best;
}

AnoMode ano_pick_mode(AnoMode current, double valence, const AnoMappingTable *t)
{
    if (current == ANO_MODE_NONE)
        return ano_nearest_mode(valence);
    for (int i = 0; i < 6; ++i)
        if (EMS[i].mode == current) {
            if (fabs(ano_map_brightness_target(valence) - EMS[i].b) < t->modeHysteresis)
                return current;
            return ano_nearest_mode(valence);
        }
    return ano_nearest_mode(valence); // unreachable in the pipeline (locrian)
}

AnoMode ano_brighter_mode(AnoMode mode, int steps)
{
    // sorted(BRIGHTNESS, key=brightness) ascending
    static const AnoMode ORDER[6] = {
        ANO_MODE_PHRYGIAN, ANO_MODE_AEOLIAN, ANO_MODE_DORIAN,
        ANO_MODE_MIXOLYDIAN, ANO_MODE_IONIAN, ANO_MODE_LYDIAN,
    };
    if (steps <= 0)
        return mode;
    int idx = -1;
    for (int i = 0; i < 6; ++i)
        if (ORDER[i] == mode)
            idx = i;
    if (idx < 0)
        return mode; // not in BRIGHTNESS (locrian)
    int j = idx + steps;
    if (j > 5)
        j = 5;
    return ORDER[j];
}

void ano_pick_instruments(const uint8_t current[ANO_MUSIC_LAYER_COUNT], double energy,
                          const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT])
{
    for (int l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
        out[l] = ANO_PATCH_NONE;
    for (uint32_t r = 0; r < t->instrumentRowCount; ++r) {
        const AnoInstrumentRow *row = &t->instrumentRows[r];
        uint8_t chosen = row->tiers[0].patch;
        for (uint32_t k = 0; k < row->tierCount; ++k) {
            double eff = row->tiers[k].threshold
                       - (current[row->layer] == row->tiers[k].patch
                          ? t->instrumentHysteresis : 0.0);
            if (energy >= eff)
                chosen = row->tiers[k].patch;
        }
        out[row->layer] = chosen;
    }
}

uint32_t ano_gate_layers(const uint8_t *current, uint32_t currentCount, double energy,
                         const AnoMappingTable *t, uint8_t out[ANO_MUSIC_LAYER_COUNT])
{
    uint32_t n = 0;
    for (uint32_t g = 0; g < t->layerGateCount; ++g) {
        bool held = false;
        for (uint32_t i = 0; i < currentCount; ++i)
            if (current[i] == t->layerGates[g].layer)
                held = true;
        double eff = t->layerGates[g].threshold - (held ? t->layerHysteresis : 0.0);
        if (energy > eff)
            out[n++] = t->layerGates[g].layer;
    }
    return n;
}

AnoCadencePolicy ano_pick_cadence_policy(double tension, const AnoMappingTable *t)
{
    if (tension < t->cadenceAuthenticMax)
        return ANO_CADENCE_AUTHENTIC;
    if (tension < t->cadenceHalfMax)
        return ANO_CADENCE_HALF;
    return ANO_CADENCE_DECEPTIVE;
}

double ano_map_harmonic_rhythm(AnoAffect a, const AnoMappingTable *t)
{
    if (a.energy < t->harmonicSlowEnergy && a.tension < t->harmonicSlowTension)
        return 0.5; // one chord per two bars
    return 1.0;
}

double ano_music_slew(double current, double target, double maxStep)
{
    return current + clampd(target - current, -maxStep, maxStep);
}

double ano_map_filter_cutoff(AnoAffect a, const AnoMappingTable *t)
{
    double pos = a.valence > 0.0 ? a.valence : 0.0; // max(0.0, valence)
    double octaves = t->cutoffEnergyOctaves * a.energy + t->cutoffValenceOctaves * pos;
    return t->cutoffBaseHz * pow(2.0, octaves);
}

double ano_map_reverb_send(AnoAffect a, const AnoMappingTable *t)
{
    double raw = t->reverbSendBase + t->reverbSendTension * a.tension
               + t->reverbSendStillness * (1.0 - a.energy);
    return clampd(raw, 0.0, 0.65);
}

double ano_map_delay_send(AnoAffect a, const AnoMappingTable *t)
{
    return clampd(t->delaySendBase + t->delaySendActivity * a.tension * a.energy, 0.0, 0.5);
}

double ano_map_drive(AnoAffect a, const AnoMappingTable *t)
{
    return clampd(t->driveBase + t->driveEnergy * a.energy * a.energy, 0.0, 0.6);
}

double ano_map_stereo_width(AnoAffect a, const AnoMappingTable *t)
{
    return t->widthBase + t->widthValence * (a.valence + 1.0) / 2.0;
}
