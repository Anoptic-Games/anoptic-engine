/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: fx_limiter lookahead window one sample short. The winmax analysis window is
// sized == lookahead (audio_fx.c:100) while the emitted sample each step is the one written
// lookahead steps ago (audio_fx.c:391), and the wedge expires stamp + win <= n
// (dynamics.h:73), so a peak's stamp leaves the window on the exact push that emits it: the
// "instant attack" gain has already taken one release step back toward 1.0 when the peak is
// multiplied out, breaching the ceiling by releaseCoef * (peak - ceiling) on every transient
// (docs/BUGS.md, Audio / Implementation). At the public 1 ms release floor
// (ANO_AUDIO_P_LIM_RELEASE_MS) a 10x impulse under the default 0.92 ceiling emits ~1.107 〜
// past digital full scale. Controls pin correct behavior on good input 〜 sub-ceiling audio
// passes at unity, a sustained overload holds the ceiling exactly in its interior 〜 so a fix
// that mutes or rejects cannot pass. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_audio.h>
#include <anoptic_memory.h>

#include "audio/audio_fx.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE   48000u
#define BLOCK  256u
#define FRAMES 1024u // 4 blocks; lookahead at 48k is 240 frames

// Process one interleaved-stereo buffer through fx in mixer-cadence blocks.
static void run_blocks(AnoAudioFx *fx, float *stereo, uint32_t frames)
{
    for (uint32_t f = 0; f < frames; f += BLOCK)
        ano_audio_fx_process(fx, stereo + 2u * f, BLOCK, RATE);
}

static float peak_of(const float *buf, uint32_t f0, uint32_t f1)
{
    float peak = 0.0f;
    for (uint32_t i = 2u * f0; i < 2u * f1; ++i) {
        float a = buf[i] < 0.0f ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    return peak;
}

static float buf[FRAMES * 2u];

int main(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    CHECK(heap != NULL, "module heap created");
    if (!heap) return 1;

    // control: sub-ceiling program passes at unity gain past the lookahead warmup
    {
        AnoAudioFx fx;
        CHECK(ano_audio_fx_init(&fx, ANO_AUDIO_FX_LIMITER, heap, RATE, 0.5f), "control limiter init");
        for (uint32_t i = 0; i < FRAMES * 2u; ++i) buf[i] = 0.5f;
        run_blocks(&fx, buf, FRAMES);
        CHECK(peak_of(buf, 0, FRAMES) <= 0.5f + 1.0e-4f, "sub-ceiling audio not boosted");
        CHECK(buf[2u * 800u] >= 0.5f - 1.0e-4f, "sub-ceiling audio not attenuated");
    }

    // control: sustained 4x overload holds the 0.92 default ceiling in its interior
    {
        AnoAudioFx fx;
        CHECK(ano_audio_fx_init(&fx, ANO_AUDIO_FX_LIMITER, heap, RATE, 0.5f), "burst limiter init");
        for (uint32_t i = 0; i < FRAMES * 2u; ++i) buf[i] = i < 600u * 2u ? 4.0f : 0.0f;
        run_blocks(&fx, buf, FRAMES);
        float interior = peak_of(buf, 300u, 500u);
        CHECK(interior <= 0.92f + 1.0e-3f, "sustained overload limited to the ceiling");
        CHECK(interior >= 0.90f, "sustained overload limited, not silenced");
    }

    printf("controls done: %d failure(s); triggering 10x impulse at the 1 ms release floor\n", failures);
    fflush(stdout);

    // trigger: one 10x-hot sample; its window stamp expires on the very push that emits it,
    // so today the output takes a released gain and lands ~1.107 over the 0.92 ceiling.
    {
        AnoAudioFx fx;
        CHECK(ano_audio_fx_init(&fx, ANO_AUDIO_FX_LIMITER, heap, RATE, 0.5f), "trigger limiter init");
        ano_audio_fx_set(&fx, ANO_AUDIO_P_LIM_RELEASE_MS, 1.0f);
        for (uint32_t i = 0; i < FRAMES * 2u; ++i) buf[i] = 0.0f;
        buf[2u * 400u] = 10.0f;
        buf[2u * 400u + 1u] = 10.0f;
        run_blocks(&fx, buf, FRAMES);
        float peak = peak_of(buf, 0, FRAMES);
        printf("impulse output peak %.6f against ceiling 0.92\n", peak);
        CHECK(peak <= 0.92f * (1.0f + 1.0e-3f), "lookahead limiter holds its ceiling on a transient");
        CHECK(peak > 0.5f, "impulse emitted, not silenced");
    }

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
