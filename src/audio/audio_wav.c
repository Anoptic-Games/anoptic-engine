/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// WAV write/load. Interleaved f32 <-> IEEE-float (or PCM) WAV. stdio.

#include <anoptic_audio.h>
#include <anoptic_memory.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

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

    // RIFF + fmt + fact + data = 58
    uint8_t h[58];
    put_u32(h + 0, 0x46464952u);            // "RIFF"
    put_u32(h + 4, 50u + dataBytes);        // file size - 8
    put_u32(h + 8, 0x45564157u);            // "WAVE"
    put_u32(h + 12, 0x20746d66u);           // "fmt "
    put_u32(h + 16, 18u);
    put_u16(h + 20, 3u);                    // WAVE_FORMAT_IEEE_FLOAT
    put_u16(h + 22, (uint16_t)channels);
    put_u32(h + 24, sampleRate);
    put_u32(h + 28, byteRate);
    put_u16(h + 32, align);
    put_u16(h + 34, 32u);
    put_u16(h + 36, 0u);                    // cbSize
    put_u32(h + 38, 0x74636166u);           // "fact"
    put_u32(h + 42, 4u);
    put_u32(h + 46, (uint32_t)frames);
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

/* Loader */

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

// Windowed-sinc resample (16 taps/side, Blackman). Load-time only. mi_malloc'd *outData.
static uint64_t wav_resample(const float *src, uint64_t srcFrames, uint32_t channels,
                             uint32_t srcRate, uint32_t dstRate, float **outData)
{
    const int T = 16;
    uint64_t dstFrames = (uint64_t)((double)srcFrames * (double)dstRate / (double)srcRate);
    if (dstFrames == 0u)
        dstFrames = 1u;
    float *dst = mi_malloc((size_t)dstFrames * channels * sizeof(float));
    if (!dst)
        return 0u;
    const double step = (double)srcRate / (double)dstRate;
    const double c    = step > 1.0 ? 1.0 / step : 1.0; // cutoff when decimating
    for (uint64_t n = 0; n < dstFrames; ++n) {
        double t  = (double)n * step;
        int64_t i0 = (int64_t)t;
        double fr = t - (double)i0;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            double acc = 0.0;
            for (int k = -T + 1; k <= T; ++k) {
                int64_t idx = i0 + k;
                if (idx < 0) idx = 0;
                if (idx >= (int64_t)srcFrames) idx = (int64_t)srcFrames - 1;
                double u = (double)k - fr;
                double s = u == 0.0 ? 1.0 : sin(3.14159265358979 * c * u) / (3.14159265358979 * c * u);
                double w = 0.42 + 0.5 * cos(3.14159265358979 * u / T)
                         + 0.08 * cos(2.0 * 3.14159265358979 * u / T);
                acc += (double)src[(size_t)idx * channels + ch] * c * s * w;
            }
            dst[(size_t)n * channels + ch] = (float)acc;
        }
    }
    *outData = dst;
    return dstFrames;
}

float *ano_audio_wav_load(const char *path, uint32_t targetRate,
                          uint64_t *outFrames, uint32_t *outChannels)
{
    if (!path || !outFrames || !outChannels)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 44) { fclose(f); return NULL; }
    uint8_t *raw = mi_malloc((size_t)fsize);
    if (!raw) { fclose(f); return NULL; }
    bool readOk = fread(raw, 1, (size_t)fsize, f) == (size_t)fsize;
    fclose(f);
    if (!readOk) { mi_free(raw); return NULL; }
    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) {
        mi_free(raw);
        return NULL;
    }

    // word-aligned chunk walk
    uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    uint64_t dataBytes = 0;
    for (size_t at = 12; at + 8 <= (size_t)fsize;) {
        uint32_t csize = get_u32(raw + at + 4);
        const uint8_t *body = raw + at + 8;
        if (at + 8 + csize > (size_t)fsize)
            break;
        if (memcmp(raw + at, "fmt ", 4) == 0 && csize >= 16) {
            tag      = get_u16(body);
            channels = get_u16(body + 2);
            rate     = get_u32(body + 4);
            bits     = get_u16(body + 14);
            if (tag == 0xFFFEu && csize >= 40)
                tag = get_u16(body + 24); // WAVE_FORMAT_EXTENSIBLE
        } else if (memcmp(raw + at, "data", 4) == 0) {
            data      = body;
            dataBytes = csize;
        }
        at += 8 + csize + (csize & 1u);
    }
    bool fmtOk = data && rate != 0u && channels >= 1u && channels <= 2u
                 && ((tag == 1u && (bits == 16u || bits == 24u || bits == 32u))
                     || (tag == 3u && bits == 32u));
    if (!fmtOk) {
        mi_free(raw);
        return NULL;
    }

    const uint32_t frameBytes = channels * bits / 8u;
    uint64_t frames = dataBytes / frameBytes;
    if (frames == 0u) { mi_free(raw); return NULL; }
    float *pcm = mi_malloc((size_t)frames * channels * sizeof(float));
    if (!pcm) { mi_free(raw); return NULL; }
    const uint64_t samples = frames * channels;
    if (tag == 3u) {
        memcpy(pcm, data, (size_t)samples * sizeof(float));
    } else if (bits == 16u) {
        for (uint64_t i = 0; i < samples; ++i) {
            int16_t v = (int16_t)get_u16(data + i * 2u);
            pcm[i] = (float)v * (1.0f / 32768.0f);
        }
    } else if (bits == 24u) {
        for (uint64_t i = 0; i < samples; ++i) {
            const uint8_t *p = data + i * 3u;
            int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            pcm[i] = (float)v * (1.0f / 8388608.0f);
        }
    } else { // PCM 32
        for (uint64_t i = 0; i < samples; ++i) {
            int32_t v = (int32_t)get_u32(data + i * 4u);
            pcm[i] = (float)v * (1.0f / 2147483648.0f);
        }
    }
    mi_free(raw);

    if (targetRate == 0u || targetRate == rate) {
        *outFrames = frames;
        *outChannels = channels;
        return pcm;
    }
    float *res = NULL;
    uint64_t resFrames = wav_resample(pcm, frames, channels, rate, targetRate, &res);
    mi_free(pcm);
    if (!res)
        return NULL;
    *outFrames = resFrames;
    *outChannels = channels;
    return res;
}
