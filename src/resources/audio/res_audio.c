/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The audio extension: RIFF/WAVE understanding. STUB.
//
// TODO(W8, M16): ingest a WAV into a PLANAR f32 plane-set block, ONE PLANE PER CHANNEL,
// because the mixer walks one plane per channel. Registers itself as a res_ext ('APCM'),
// which means adding audio edits ZERO model literals and no core file -- that is the whole
// point of the extension registry, and this domain is the proof of it.

#include <anoptic_res_audio.h>

#include "../resources_ext.h"
#include "../resources_internal.h"

anores_t ano_resaudio_clip(ano_res_lifetime lifetime, const ano_res_read *read, anores_t src)
{
    (void)lifetime; (void)read; (void)src;
    return (anores_t){0};                       // TODO(W8, M16)
}

anoresaudio_clip ano_resaudio_view(const ano_res_read *read, anores_t clip)
{
    (void)read; (void)clip;
    return (anoresaudio_clip){0};               // TODO(W8, M16)
}
