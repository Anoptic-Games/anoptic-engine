/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- the audio extension. RIFF/WAVE understanding lives INSIDE
// src/resources/audio/ and appears nowhere else. A clip is ingested into a PLANAR f32
// plane-set block: ONE PLANE PER CHANNEL, because the mixer walks one plane per channel.
// That is the SoA thesis paying rent inside a decoder, not a slogan.
//
// FROZEN (freeze item 12). The clip view borrows manager memory and dies with the read
// scope, like every other view in the engine.

#ifndef ANOPTICENGINE_ANOPTIC_RES_AUDIO_H
#define ANOPTICENGINE_ANOPTIC_RES_AUDIO_H

#include <stdint.h>

#include "anoptic_resources.h"

#define ANORESAUDIO_TAG_PCM   0x4D435041u   // 'APCM'
#define ANORESAUDIO_MAX_CHANNELS 8

// A conditioned clip: planar float32, one plane per channel, every plane
// RES_PLANE_GRAIN-aligned inside one manager-owned block. frames is the per-plane length.
// A zeroed struct means the handle was stale, sentinel, or the block failed validation.
typedef struct anoresaudio_clip {
    const float *plane[ANORESAUDIO_MAX_CHANNELS];
    uint32_t     channels;
    uint32_t     frames;          // samples PER CHANNEL
    uint32_t     sample_rate;     // Hz, as the file declares
    uint32_t     _pad;
} anoresaudio_clip;

// Ingest a WAV resource into a conditioned planar-f32 clip. src is a live handle to the
// encoded bytes (ano_res_get). The clip becomes an owned resource under
// res_rid_derived(src_rid, 'APCM') -- single-copy, no string key. Sentinel on refusal
// (unsupported format, hostile chunk table), one log line.
anores_t ano_resaudio_clip(ano_res_lifetime lifetime, const ano_res_read *read, anores_t src);

// The clip view for a conditioned handle. Its pointers die with read; zeroed on refusal.
anoresaudio_clip ano_resaudio_view(const ano_res_read *read, anores_t clip);

#endif // ANOPTICENGINE_ANOPTIC_RES_AUDIO_H
