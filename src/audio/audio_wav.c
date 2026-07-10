/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Development WAV writer: interleaved f32 -> IEEE-float WAV (format tag 3,
 * with the fact chunk the spec requires for non-PCM). Little-endian bytes are
 * written explicitly; the raw sample block is written as-is, which is correct
 * on every engine target (x86-64 and AArch64 are little-endian IEEE-754).
 * Plain stdio: general-purpose file creation is portable C, and the
 * filesystem module's writer is append-only (log-shaped). */

#include <anoptic_audio.h>

#include <stdio.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool ano_audio_wav_write(const char *path, const float *interleaved,
                         uint64_t frames, uint32_t channels, uint32_t sampleRate)
{
    if (!path || !interleaved || channels == 0u || sampleRate == 0u)
        return false;
    uint64_t dataBytes64 = frames * channels * sizeof(float);
    if (dataBytes64 > 0xFFFFFFFFull - 58u) // RIFF sizes are 32-bit
        return false;
    const uint32_t dataBytes = (uint32_t)dataBytes64;
    const uint32_t byteRate  = sampleRate * channels * (uint32_t)sizeof(float);
    const uint16_t align     = (uint16_t)(channels * sizeof(float));

    // RIFF(12) + fmt(8+18) + fact(8+4) + data header(8) = 58 bytes
    uint8_t h[58];
    put_u32(h + 0, 0x46464952u);            // "RIFF"
    put_u32(h + 4, 50u + dataBytes);        // file size - 8
    put_u32(h + 8, 0x45564157u);            // "WAVE"
    put_u32(h + 12, 0x20746d66u);           // "fmt "
    put_u32(h + 16, 18u);                   // fmt chunk size (with cbSize)
    put_u16(h + 20, 3u);                    // WAVE_FORMAT_IEEE_FLOAT
    put_u16(h + 22, (uint16_t)channels);
    put_u32(h + 24, sampleRate);
    put_u32(h + 28, byteRate);
    put_u16(h + 32, align);
    put_u16(h + 34, 32u);                   // bits per sample
    put_u16(h + 36, 0u);                    // cbSize
    put_u32(h + 38, 0x74636166u);           // "fact"
    put_u32(h + 42, 4u);
    put_u32(h + 46, (uint32_t)frames);      // samples per channel
    put_u32(h + 50, 0x61746164u);           // "data"
    put_u32(h + 54, dataBytes);

    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    if (ok && dataBytes > 0u)
        ok = fwrite(interleaved, 1, dataBytes, f) == dataBytes;
    if (fclose(f) != 0)
        ok = false;
    return ok;
}
