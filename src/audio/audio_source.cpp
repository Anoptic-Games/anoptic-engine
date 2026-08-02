/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Compile-time-specialized source rendering. Shape dispatch happens once per block.

#include <math.h>

#include <anoptic_audio.h>

#include "audio_source.h"
#include "cpp/ano_types.h"

namespace {

constexpr float kTau = 6.28318530717958647692f;
constexpr float kPi4 = 0.78539816339744830962f;

using SourceFlags = ano::EnumFlags<
    AnoAudioSourceFlags,
    ANO_AUDIO_SOURCE_LOOP | ANO_AUDIO_SOURCE_POSITIONAL>;
using SourceKind = ano::EnumValue<AnoAudioSourceKind>;

static_assert(ano::Data<AnoAudioSource>);
static_assert(ano::Data<SourceFlags>);
static_assert(ano::Data<SourceKind>);

enum class VoiceShape {
    Tone,
    MonoBuffer,
    StereoBuffer,
};

// PLAYING/STOPPING into bus mix. May flip RETIRING mid-block.
// Smoothers advance once per frame so a skipped sample can never shift them.
template<VoiceShape Shape, bool Loop, bool Positional>
void render(AnoAudioSource *s, float *mix, uint32_t frames, float fsInv)
{
    static_assert(Shape != VoiceShape::Tone || !Loop);
    static_assert(Shape != VoiceShape::StereoBuffer || !Positional);

    for (uint32_t i = 0; i < frames; ++i) {
        // duration expiry begins the same release ramp a STOP does
        if (s->remaining == 0u && s->state == ANO_AUDIO_SRC_PLAYING) {
            s->state = ANO_AUDIO_SRC_STOPPING;
            s->gain.target = 0.0f;
        }
        float g = ano_audio_smooth_step(&s->gain);
        if (s->state == ANO_AUDIO_SRC_STOPPING && g < ANO_AUDIO_RETIRE_EPS) {
            s->state = ANO_AUDIO_SRC_RETIRING;
            return;
        }
        float p   = ano_audio_smooth_step(&s->pan);
        float sg  = ano_audio_smooth_step(&s->spatGain);
        float amp = g * sg;

        float vl, vr;
        if constexpr (Shape == VoiceShape::Tone) {
            float f = ano_audio_smooth_step(&s->freq);
            s->phase += static_cast<double>(f * fsInv);
            if (s->phase >= 1.0)
                s->phase -= 1.0;
            vl = vr = sinf(static_cast<float>(s->phase) * kTau);
        } else {
            float r = ano_audio_smooth_step(&s->rate);
            uint64_t i0 = static_cast<uint64_t>(s->cursor);
            if (i0 >= s->bufFrames) {
                if constexpr (!Loop) {
                    // data ended: release ramp over silence
                    if (s->state == ANO_AUDIO_SRC_PLAYING) {
                        s->state = ANO_AUDIO_SRC_STOPPING;
                        s->gain.target = 0.0f;
                    }
                    vl = vr = 0.0f;
                    goto place;
                } else {
                    while (s->cursor >= static_cast<double>(s->bufFrames))
                        s->cursor -= static_cast<double>(s->bufFrames);
                    i0 = static_cast<uint64_t>(s->cursor);
                }
            }
            uint64_t i1 = i0 + 1u;
            if (i1 >= s->bufFrames) {
                if constexpr (Loop)
                    i1 = 0u;
                else
                    i1 = s->bufFrames - 1u;
            }
            float fr = static_cast<float>(s->cursor - static_cast<double>(i0));
            const float *d = s->bufData;
            if constexpr (Shape == VoiceShape::MonoBuffer) {
                float v = d[i0] + (d[i1] - d[i0]) * fr;
                vl = vr = v;
            } else {
                vl = d[2u * i0] + (d[2u * i1] - d[2u * i0]) * fr;
                vr = d[2u * i0 + 1u] + (d[2u * i1 + 1u] - d[2u * i0 + 1u]) * fr;
            }
            s->cursor += static_cast<double>(r);
        }

    place:
        if constexpr (Positional) {
            s->airLp += s->airCoef * (vl - s->airLp);
            vl = vr = s->airLp;
        }

        if constexpr (Shape == VoiceShape::StereoBuffer) {
            // stereo: linear balance
            float bl = p > 0.0f ? 1.0f - p : 1.0f;
            float br = p < 0.0f ? 1.0f + p : 1.0f;
            mix[2u * i]      += vl * amp * bl;
            mix[2u * i + 1u] += vr * amp * br;
        } else {
            // mono: constant-power
            float a = (p + 1.0f) * kPi4;
            mix[2u * i]      += vl * amp * cosf(a);
            mix[2u * i + 1u] += vr * amp * sinf(a);
        }
        s->remaining--; // the UINT64_MAX sentinel counts down; 2^64 frames never arrives
    }
}

} // namespace

extern "C" void ano_audio_source_render(AnoAudioSource *s, float *mix,
                                          uint32_t frames, float fsInv)
{
    ano::assume(s->kind < ANO_AUDIO_SOURCE_COUNT);
    const SourceKind kind = *SourceKind::from_raw(s->kind);
    const SourceFlags flags = SourceFlags::masked(s->flags);
    const bool positional = flags.contains<ANO_AUDIO_SOURCE_POSITIONAL>();
    switch (kind.get()) {
    case ANO_AUDIO_SOURCE_TONE:
        if (positional)
            render<VoiceShape::Tone, false, true>(s, mix, frames, fsInv);
        else
            render<VoiceShape::Tone, false, false>(s, mix, frames, fsInv);
        return;
    case ANO_AUDIO_SOURCE_BUFFER:
        break;
    case ANO_AUDIO_SOURCE_COUNT:
        std::unreachable();
    }

    const bool loop = flags.contains<ANO_AUDIO_SOURCE_LOOP>();
    if (s->bufChannels == 1u) {
        if (loop && positional)
            render<VoiceShape::MonoBuffer, true, true>(s, mix, frames, fsInv);
        else if (loop)
            render<VoiceShape::MonoBuffer, true, false>(s, mix, frames, fsInv);
        else if (positional)
            render<VoiceShape::MonoBuffer, false, true>(s, mix, frames, fsInv);
        else
            render<VoiceShape::MonoBuffer, false, false>(s, mix, frames, fsInv);
    } else if (loop) {
        render<VoiceShape::StereoBuffer, true, false>(s, mix, frames, fsInv);
    } else {
        render<VoiceShape::StereoBuffer, false, false>(s, mix, frames, fsInv);
    }
}
