/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_perc.h (private to src/music/)
 * Euclidean percussion with phrase-end fills (musicgen/gen/perc.py): kicks
 * follow E(k, slots) (grouped in compound meters), snares anchor odd pulses
 * with roughness-gated ghosts, hats subdivide by density, and the cadence
 * bar may replace its tail with a fill that earns a crash on the next
 * phrase downbeat. A Groove (A2) pins the phrase's stochastic pattern draws;
 * without one, per-bar rolls are byte-identical to the prototype's.
 */

#ifndef ANO_MUSIC_PERC_H
#define ANO_MUSIC_PERC_H

#include "music_gen.h"
#include "music_ir.h"

// Drum ids in the prototype's DRUMS dict order; names/pitches in tables.
typedef enum AnoDrum
{
    ANO_DRUM_KICK = 0,
    ANO_DRUM_RIM,
    ANO_DRUM_SNARE,
    ANO_DRUM_CHAT,
    ANO_DRUM_OHAT,
    ANO_DRUM_CRASH,
    ANO_DRUM_LTOM,
    ANO_DRUM_MTOM,
    ANO_DRUM_HTOM,
    ANO_DRUM_SHAKER,
    ANO_DRUM_COUNT,
} AnoDrum;

extern const char *const ANO_DRUM_NAMES[ANO_DRUM_COUNT]; // "kick", ...
extern const uint8_t     ANO_DRUM_PITCHES[ANO_DRUM_COUNT]; // 36, 37, ...

typedef struct AnoPercConfig
{
    double  fillBaseProb;      // 0.25
    double  fillTensionWeight; // 0.55
    int     ghostVelocity;     // 52
    int     kickVel, snareVel, chatVel, ohatVel, crashVel; // 100 96 64 70 106
} AnoPercConfig;

AnoPercConfig ano_perc_config_default(void);

// Pattern-identity draws pinned for a phrase (A2).
typedef struct AnoGroove
{
    uint8_t  ghosts[8]; // ghost-snare slots, in _ghost_slots order
    uint32_t ghostCount;
    uint32_t hatDrops; // slot bitmask
    bool     ohat;     // the pre-downbeat hat opens this phrase
} AnoGroove;

// One phrase's groove from a per-(subsystem, phrase) stream.
AnoGroove ano_make_groove(AnoMusicRng *rng, AnoMeter meter, double density,
                          double roughness);

typedef struct AnoPercResult
{
    AnoMusicEvent events[48];
    uint32_t      eventCount;
    bool          fill; // fed back as had_fill next bar
} AnoPercResult;

// One bar of drums. groove NULL = per-bar rolls; hyperFill (B3) scales the
// secondary mid-phrase fill chance.
void ano_generate_perc(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoPhrasePos pos, bool hadFill,
                       const AnoPercConfig *cfg, AnoMusicRng *rng,
                       const AnoGroove *groove, double hyperFill,
                       AnoPercResult *out);

#endif // ANO_MUSIC_PERC_H
