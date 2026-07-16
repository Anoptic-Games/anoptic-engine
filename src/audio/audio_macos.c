/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* macOS device backend: the default-output AudioUnit (AUHAL). The unit is
 * asked for interleaved f32 stereo at the engine rate on its input scope and
 * converts rate/format to whatever the hardware runs — and, because it is the
 * DefaultOutput subtype, it follows default-device changes on its own, which
 * covers the baseline device-change story without a listener. The render
 * callback runs on CoreAudio's realtime thread and only pulls the lock-free
 * block ring (the platform-file exception applies to the control path:
 * AudioUnit setup/teardown hold OS-internal locks). Protocol reference:
 * miniaudio's Core Audio backend (docs/references/miniaudio-legend.md 33469+,
 * AUHAL setup 35769, render callback 35089).
 *
 * Baseline scope: default device, no explicit device selection, no
 * kAudioObjectPropertyDefaultOutputDevice listener. UNTESTED on real macOS as
 * of writing — validation is deferred to a macOS session per the plan.
 */

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include "audio_internal.h"
#include "audio_pull.h"

#include <anoptic_log.h>

typedef struct AnoCoreAudioState
{
    AudioUnit    unit;
    bool         unitLive; // AudioComponentInstanceNew succeeded
    bool         started;  // AudioOutputUnitStart succeeded
    AnoAudioPull pull;
} AnoCoreAudioState;

// The AUHAL pull: fill every buffer in the list (interleaved stereo = one
// buffer; anything else is zeroed defensively — we set the format, so a
// mismatch means the unit was reconfigured under us).
static OSStatus coreaudio_render(void *user, AudioUnitRenderActionFlags *flags,
                                 const AudioTimeStamp *ts, UInt32 bus,
                                 UInt32 frames, AudioBufferList *io)
{
    (void)flags; (void)ts; (void)bus; (void)frames;
    AnoAudioMixer     *mx = user;
    AnoCoreAudioState *st = mx->deviceState;
    for (UInt32 i = 0; i < io->mNumberBuffers; ++i) {
        AudioBuffer *b = &io->mBuffers[i];
        if (b->mNumberChannels == ANO_AUDIO_CHANNELS && b->mData) {
            uint32_t n = b->mDataByteSize / (ANO_AUDIO_CHANNELS * (uint32_t)sizeof(float));
            ano_audio_pull_frames(mx, &st->pull, (float *)b->mData, n);
        } else if (b->mData) {
            memset(b->mData, 0, b->mDataByteSize);
        }
    }
    return noErr;
}

static void coreaudio_teardown(AnoCoreAudioState *st)
{
    if (st->started)
        AudioOutputUnitStop(st->unit);
    if (st->unitLive) {
        AudioUnitUninitialize(st->unit);
        AudioComponentInstanceDispose(st->unit);
    }
    mi_free(st);
}

static bool coreaudio_start(AnoAudioMixer *mx)
{
    AnoCoreAudioState *st = mi_heap_calloc(mx->heap, 1, sizeof *st);
    if (!st)
        return false;

    AudioComponentDescription desc = {
        .componentType         = kAudioUnitType_Output,
        .componentSubType      = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp)
        goto fail;
    if (AudioComponentInstanceNew(comp, &st->unit) != noErr)
        goto fail;
    st->unitLive = true;

    // interleaved f32 stereo at the engine rate; AUHAL converts to hardware
    AudioStreamBasicDescription fmt = {
        .mSampleRate       = (Float64)mx->sampleRate,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket   = ANO_AUDIO_CHANNELS * sizeof(float),
        .mFramesPerPacket  = 1,
        .mBytesPerFrame    = ANO_AUDIO_CHANNELS * sizeof(float),
        .mChannelsPerFrame = ANO_AUDIO_CHANNELS,
        .mBitsPerChannel   = 32,
    };
    if (AudioUnitSetProperty(st->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof fmt) != noErr)
        goto fail;

    AURenderCallbackStruct cb = { .inputProc = coreaudio_render, .inputProcRefCon = mx };
    if (AudioUnitSetProperty(st->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr)
        goto fail;

    // keep render quanta bounded regardless of what the HAL negotiates
    UInt32 maxSlice = 4096;
    AudioUnitSetProperty(st->unit, kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global, 0, &maxSlice, sizeof maxSlice);

    // publish state before the unit starts: the render callback reads it
    mx->deviceState = st;
    atomic_store_explicit(&mx->deviceRun, true, memory_order_release);

    if (AudioUnitInitialize(st->unit) != noErr)
        goto fail_published;
    if (AudioOutputUnitStart(st->unit) != noErr)
        goto fail_published; // teardown uninitializes via unitLive
    st->started = true;
    ano_log(ANO_INFO, "audio/coreaudio: default-output AUHAL up, %u Hz f32 stereo (unit converts).",
            mx->sampleRate);
    return true;

fail_published:
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    mx->deviceState = NULL;
fail:
    coreaudio_teardown(st);
    return false;
}

static void coreaudio_stop(AnoAudioMixer *mx)
{
    AnoCoreAudioState *st = mx->deviceState;
    if (!st)
        return;
    // AudioOutputUnitStop synchronizes with the render thread: after it
    // returns no callback is in flight, so the state can be freed.
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    coreaudio_teardown(st);
    mx->deviceState = NULL;
}

const AnoAudioDeviceApi *ano_audio_device_coreaudio(void)
{
    static const AnoAudioDeviceApi api = {
        .name  = "coreaudio",
        .start = coreaudio_start,
        .stop  = coreaudio_stop,
    };
    return &api;
}
