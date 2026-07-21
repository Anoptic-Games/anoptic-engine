/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The mixer and the null sink. STUB.
//
// TODO(W8, M16): the mixer walks ONE PLANE PER CHANNEL out of a conditioned clip block and
// emits into the sink. The NULL sink folds every emitted byte into an FNV-1a-64 running
// hash: that hash is an ORACLE, not a placeholder. A mixer that drops a voice, misreads a
// plane, or mixes a stale block changes it, and the test says so.

#include <anoptic_audio.h>

#include <string.h>

static ano_audio_stats g_stats;
static bool            g_live;

int ano_audio_init(const ano_audio_cfg *cfg)
{
    (void)cfg;
    memset(&g_stats, 0, sizeof g_stats);
    g_stats.sink_hash = 0xcbf29ce484222325u;    // the FNV-1a-64 offset basis
    g_live = true;
    return 0;                                   // TODO(W8, M16)
}

void ano_audio_shutdown(void)
{
    g_live = false;
    memset(&g_stats, 0, sizeof g_stats);
}

ano_voice ano_audio_play(const ano_res_read *read, anores_t clip, float gain, bool looping)
{
    (void)read; (void)clip; (void)gain; (void)looping;
    return (ano_voice){0};                      // TODO(W8, M16)
}

int ano_audio_stop(ano_voice v)
{
    (void)v;
    return -1;                                  // TODO(W8, M16)
}

bool ano_audio_voice_live(ano_voice v)
{
    (void)v;
    return false;                               // TODO(W8, M16)
}

size_t ano_audio_mix(const ano_res_read *read, size_t frames)
{
    (void)read; (void)frames;
    return 0;                                   // TODO(W8, M16)
}

ano_audio_stats ano_audio_stats_get(void)
{
    return g_live ? g_stats : (ano_audio_stats){0};
}
