/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Dense mode registry shared by the C theory kernel and its C++ contract.

#ifndef ANO_MUSIC_MODES_H
#define ANO_MUSIC_MODES_H

#include <stdint.h>

#include <anoptic_music.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *ano_mode_name(AnoMode mode);
int ano_mode_brightness(AnoMode mode);
const uint8_t *ano_mode_intervals(AnoMode mode);

#ifdef __cplusplus
}
#endif

#endif // ANO_MUSIC_MODES_H
