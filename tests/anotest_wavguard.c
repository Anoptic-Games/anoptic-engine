/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_audio_wav_write bad-args guard. A frames count whose byte size wraps uint64
// (frames * channels * sizeof(float) >= 2^64) must be rejected with false, per the header
// contract ("false on I/O or bad args"). Exposes the BUGS.md audio implementation bug: the
// wrapped product slips under the RIFF 32-bit size check, a truncated WAV is written whose
// fact chunk claims the wrapped frame count, and the call returns true 〜 silent success plus
// a lying file for any caller saving a capture. Controls: a sane write round-trips through
// ano_audio_wav_load, and channels == 0 is rejected, so a reject-everything fake fix cannot
// pass. Pure stdio, no audio init, no device. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_audio.h>

#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // scratch anchored to the exe dir; removed on exit
    scratch_anchor_to_exe();
    scratch_make_dir("wavguard.scratch");
    const char *sanePath   = "wavguard.scratch/sane.wav";
    const char *poisonPath = "wavguard.scratch/poison.wav";

    float material[16] = { 0.25f, -0.25f, 0.5f, -0.5f };

    // control: a sane 4-frame mono write succeeds and round-trips exactly
    CHECK(ano_audio_wav_write(sanePath, material, 4u, 1u, 48000u), "sane 4-frame write accepted");
    uint64_t frames = 0; uint32_t channels = 0;
    float *back = ano_audio_wav_load(sanePath, 0u, &frames, &channels);
    CHECK(back != NULL, "sane file loads back");
    if (back) {
        CHECK(frames == 4u && channels == 1u, "sane file round-trips 4 frames / 1 ch");
        CHECK(back[0] == material[0] && back[3] == material[3], "sane samples byte-exact");
        ano_audio_block_free(back);
    }

    // control: the bad-args path is live
    CHECK(!ano_audio_wav_write(sanePath, material, 4u, 0u, 48000u), "channels == 0 rejected");

    // wrap to zero: (1 << 62) frames * 2 ch * 4 bytes == 2^65 == 0 mod 2^64
    CHECK(!ano_audio_wav_write(poisonPath, material, 1ull << 62, 2u, 48000u),
          "byte size wrapping to 0 must be rejected as bad args");

    // wrap to small nonzero: ((1 << 62) + 4) frames * 1 ch * 4 bytes == 16 mod 2^64
    bool lied = ano_audio_wav_write(poisonPath, material, (1ull << 62) + 4u, 1u, 48000u);
    CHECK(!lied, "byte size wrapping to 16 must be rejected as bad args");

    // the lie on disk: success would mean the requested frame count round-trips; it cannot
    if (lied) {
        uint64_t pf = 0; uint32_t pc = 0;
        float *poison = ano_audio_wav_load(poisonPath, 0u, &pf, &pc);
        CHECK(poison != NULL && pf == (1ull << 62) + 4u,
              "a write that returned true must round-trip its frame count");
        if (poison) {
            printf("  poison.wav claims %llu frames (asked for 2^62 + 4)\n", (unsigned long long)pf);
            ano_audio_block_free(poison);
        }
    }

    remove(sanePath);
    remove(poisonPath);
    scratch_remove_dir("wavguard.scratch");

    if (failures) {
        printf("anotest_wavguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_wavguard: all passed\n");
    return 0;
}
