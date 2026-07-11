/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * audio_pull.h (private to src/audio/)
 * Shared cooked-block ring consumer with partial-block carry, used by every
 * device backend. A device quantum rarely equals the mixer block, so the
 * consumer carries a partially-drained block across calls (in
 * mx->deviceScratch — one backend runs at a time). An empty ring fills
 * silence; underruns count only after the first block has arrived (startup is
 * not an underrun). Lock-free: safe on OS callback threads (PipeWire RT,
 * CoreAudio render, WASAPI event loop).
 */

#ifndef ANO_AUDIO_PULL_H
#define ANO_AUDIO_PULL_H

#include <string.h>

#include "audio_internal.h"

typedef struct AnoAudioPull
{
    uint32_t offset;  // frames consumed from the carried block
    uint32_t frames;  // frames in the carried block; 0 = none carried
    bool     started; // first block seen
} AnoAudioPull;

static inline void ano_audio_pull_frames(AnoAudioMixer *mx, AnoAudioPull *p,
                                         float *dst, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        if (p->frames == 0u) {
            if (ano_audio_ring_pop(&mx->blockRing, mx->deviceScratch)) {
                p->offset  = 0u;
                p->frames  = mx->blockFrames;
                p->started = true;
            } else {
                if (p->started)
                    atomic_fetch_add_explicit(&mx->underruns, 1u, memory_order_relaxed);
                memset(dst + (size_t)done * ANO_AUDIO_CHANNELS, 0,
                       (size_t)(frames - done) * ANO_AUDIO_CHANNELS * sizeof(float));
                return;
            }
        }
        uint32_t avail = p->frames - p->offset;
        uint32_t n     = frames - done < avail ? frames - done : avail;
        memcpy(dst + (size_t)done * ANO_AUDIO_CHANNELS,
               mx->deviceScratch + (size_t)p->offset * ANO_AUDIO_CHANNELS,
               (size_t)n * ANO_AUDIO_CHANNELS * sizeof(float));
        done      += n;
        p->offset += n;
        if (p->offset == p->frames)
            p->frames = 0u;
    }
}

#endif // ANO_AUDIO_PULL_H
