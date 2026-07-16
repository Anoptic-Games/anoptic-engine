/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Euclidean percussion + phrase-end fills. Groove (A2) pins phrase pattern draws.

#ifndef ANO_MUSIC_PERC_H
#define ANO_MUSIC_PERC_H

#include "music_gen.h"
#include "music_ir.h"

// Drum ids in DRUMS dict order; names/pitches in tables.
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

extern const char *const ANO_DRUM_NAMES[ANO_DRUM_COUNT];
extern const uint8_t     ANO_DRUM_PITCHES[ANO_DRUM_COUNT];

typedef struct AnoPercConfig
{
    double  fillBaseProb;      // 0.25
    double  fillTensionWeight; // 0.55
    int     ghostVelocity;     // 52
    int     kickVel, snareVel, chatVel, ohatVel, crashVel; // 100 96 64 70 106
} AnoPercConfig;

AnoPercConfig ano_perc_config_default(void);

typedef struct AnoGroove
{
    uint8_t  ghosts[8];
    uint32_t ghostCount;
    uint32_t hatDrops; // slot bitmask
    bool     ohat;     // pre-downbeat hat opens this phrase
} AnoGroove;

AnoGroove ano_make_groove(AnoMusicRng *rng, AnoMeter meter, double density,
                          double roughness);

typedef struct AnoPercResult
{
    AnoMusicEvent events[48];
    uint32_t      eventCount;
    bool          fill; // fed back as had_fill next bar
} AnoPercResult;

// groove NULL = per-bar rolls. hyperFill (B3) scales secondary mid-phrase fill chance.
void ano_generate_perc(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoPhrasePos pos, bool hadFill,
                       const AnoPercConfig *cfg, AnoMusicRng *rng,
                       const AnoGroove *groove, double hyperFill,
                       AnoPercResult *out);

#endif // ANO_MUSIC_PERC_H
