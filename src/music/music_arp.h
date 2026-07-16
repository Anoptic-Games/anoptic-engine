/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Arpeggio layer: chord tones cycled above the pad. Pattern + skip mask fixed per phrase.
// Skips advance the traversal through rests (figuration stays a held pattern).

#ifndef ANO_MUSIC_ARP_H
#define ANO_MUSIC_ARP_H

#include "music_ir.h"

// PATTERNS tuple order (feeds the conductor's choice draw).
typedef enum AnoArpPattern
{
    ANO_ARP_UP = 0,
    ANO_ARP_UPDOWN,
    ANO_ARP_DOWN,
    ANO_ARP_PATTERN_COUNT,
} AnoArpPattern;

typedef struct AnoArpConfig
{
    int baseOctave;     // 5 — pool starts at C5
    int spanOctaves;    // 2
    int velocityOffset; // -16
} AnoArpConfig;

AnoArpConfig ano_arp_config_default(void);

// Per-phrase rest mask as slot bitmask (slot 0 never draws, never skips).
uint32_t ano_arp_make_skips(AnoMusicRng *rng, AnoMeter meter, double density);

typedef struct AnoArpResult
{
    AnoMusicEvent events[ANO_METER_MAX_SLOTS];
    uint32_t      eventCount;
} AnoArpResult;

// hasSkips selects pinned mask (no draws) over per-bar rolls.
void ano_generate_arp(const AnoHarmonicContext *ctx, AnoMeter meter,
                      const AnoGenParams *params, AnoArpPattern pattern,
                      const AnoArpConfig *cfg, AnoMusicRng *rng,
                      bool hasSkips, uint32_t skips, AnoArpResult *out);

#endif // ANO_MUSIC_ARP_H
