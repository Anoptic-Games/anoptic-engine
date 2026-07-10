/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* The null device backend: drains the cooked-block ring at the nominal block
 * cadence and discards the samples. Gives headless runs, CI, and tests the
 * exact producer/consumer shape a real device imposes — pacing, underrun
 * accounting, thread lifecycle — with no OS audio dependency. */

#include "audio_internal.h"

#include <anoptic_time.h>

// in:  arg = AnoAudioMixer*
// out: NULL
// inv: touches only blockRing (consumer side), deviceScratch, underruns, deviceRun
static void *null_device_main(void *arg)
{
    AnoAudioMixer *mx = arg;
    const uint64_t periodUs = (uint64_t)mx->blockFrames * 1000000ull / mx->sampleRate;
    bool started = false; // no underruns before the mixer's first block arrives
    while (atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
        if (ano_audio_ring_pop(&mx->blockRing, mx->deviceScratch))
            started = true;
        else if (started)
            atomic_fetch_add_explicit(&mx->underruns, 1u, memory_order_relaxed);
        ano_sleep(periodUs);
    }
    return NULL;
}

static bool null_device_start(AnoAudioMixer *mx)
{
    atomic_store_explicit(&mx->deviceRun, true, memory_order_release);
    return ano_thread_create(&mx->deviceThread, NULL, null_device_main, mx) == 0;
}

static void null_device_stop(AnoAudioMixer *mx)
{
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
}

const AnoAudioDeviceApi *ano_audio_device_null(void)
{
    static const AnoAudioDeviceApi api = {
        .name  = "null",
        .start = null_device_start,
        .stop  = null_device_stop,
    };
    return &api;
}
