/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_audio_buffer_register bad-args guard. A frames count whose byte size wraps
// uint64 (frames * channels * sizeof(float) >= 2^64) must be rejected with false, per the
// header contract ("false = backpressure or bad args"). Exposes the BUGS.md audio
// implementation bug: the wrapped product passes the SIZE_MAX check, a near-empty block is
// adopted with the huge frame count in its header, and the call returns true — arming a
// mixer-thread out-of-bounds read for any voice that plays the buffer. Null device, no cue
// ever plays the poisoned id, so the failing run stays memory-safe. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_audio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // deterministic backend: no device dependence, CI-safe
    AnoAudioConfig cfg = { .backend = ANO_AUDIO_BACKEND_NULL_DEV };
    CHECK(ano_audio_init(&cfg), "audio world up (null backend)");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge handle valid after init");
    if (!b) return 1;

    float material[128] = { 0.25f, -0.25f, 0.5f, -0.5f };

    // control: a sane registration goes through (the path is live, not reject-everything)
    CHECK(ano_audio_buffer_register(b, 900, material, 64u, 2u), "sane 64-frame registration accepted");

    // wrap to zero: (1 << 62) frames * 2 ch * 4 bytes == 2^65 == 0 mod 2^64
    CHECK(!ano_audio_buffer_register(b, 901, material, 1ull << 62, 2u),
          "byte size wrapping to 0 must be rejected as bad args");

    // wrap to small nonzero: ((1 << 62) + 2) frames * 2 ch * 4 bytes == 16 mod 2^64
    CHECK(!ano_audio_buffer_register(b, 902, material, (1ull << 62) + 2u, 2u),
          "byte size wrapping to 16 must be rejected as bad args");

    ano_audio_shutdown();

    if (failures) {
        printf("anotest_audioguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audioguard: all passed\n");
    return 0;
}
