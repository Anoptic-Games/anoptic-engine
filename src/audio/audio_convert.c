/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Final device quantization. TPDF is applied exactly once, immediately before s16 storage.

#include "audio_internal.h"

void ano_audio_convert_f32_s16(const float *input, int16_t *output,
                               uint32_t sampleCount, AnoDspRng *dither)
{
    for (uint32_t i = 0; i < sampleCount; ++i) {
        float value = input[i];
        value = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
        float quantized = value * 32767.0f + ano_dsp_tpdf(dither);
        if (quantized > 32767.0f) quantized = 32767.0f;
        if (quantized < -32768.0f) quantized = -32768.0f;
        output[i] = static_cast<int16_t>(
            quantized >= 0.0f ? quantized + 0.5f : quantized - 0.5f);
    }
}
