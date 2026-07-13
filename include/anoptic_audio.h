/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Audio -- a real mixer with a headless-but-genuine consumer.
//
// The Stage A sink is NULL: it consumes every mixed frame and folds it into an FNV-1a-64
// running hash. That is not a stub -- it is an ORACLE. A mixer that drops a voice, misreads
// a plane, or mixes a stale block changes the hash, and the test says so. A real device
// backend is a second sink behind the same interface and changes nothing above it.
//
// The mixer walks ONE PLANE PER CHANNEL out of a conditioned clip block
// (anoptic_res_audio.h). No interleaving, no per-sample branch on channel count.
//
// FROZEN (freeze item 12).

#ifndef ANOPTICENGINE_ANOPTIC_AUDIO_H
#define ANOPTICENGINE_ANOPTIC_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_resources.h"

#define ANO_AUDIO_MAX_VOICES 64
#define ANO_AUDIO_BLOCK      256    // frames per mix block

typedef enum ano_audio_sink_kind {
    ANO_AUDIO_SINK_NULL = 0,   // the FNV byte oracle; the Stage A production sink
    ANO_AUDIO_SINK_DEVICE,     // Stage C: a real device backend, same interface
} ano_audio_sink_kind;

typedef struct ano_audio_cfg {
    ano_audio_sink_kind sink;
    uint32_t            sample_rate;   // 0 = 48000
    uint32_t            channels;      // 0 = 2
} ano_audio_cfg;

// A voice handle. Zero is invalid. Generations never silently wrap.
typedef struct ano_voice { uint32_t slot; uint32_t gen; } ano_voice;

int  ano_audio_init(const ano_audio_cfg *cfg);
void ano_audio_shutdown(void);

// Start a conditioned clip. The clip handle must stay live for the voice's lifetime: the
// mixer BORROWS its planes and never copies them. Sentinel voice when the voice table is
// full or the clip is stale.
ano_voice ano_audio_play(const ano_res_read *read, anores_t clip, float gain, bool looping);
int       ano_audio_stop(ano_voice v);
bool      ano_audio_voice_live(ano_voice v);

// Mix and emit exactly `frames` frames to the sink. Owner-thread; synchronous by design in
// Stage A. Output: frames actually emitted.
size_t ano_audio_mix(const ano_res_read *read, size_t frames);

typedef struct ano_audio_stats {
    uint64_t frames_emitted;
    uint64_t bytes_emitted;
    uint64_t sink_hash;        // FNV-1a-64 over every emitted byte. THE ORACLE.
    uint32_t voices_live;
    uint32_t voices_started;
    uint32_t voices_finished;
    uint32_t underruns;
} ano_audio_stats;

ano_audio_stats ano_audio_stats_get(void);

#endif // ANOPTICENGINE_ANOPTIC_AUDIO_H
