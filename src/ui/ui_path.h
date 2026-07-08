/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scanline-sweeper bake helpers shared with src/text (both compile into anoptic_core,
// so these non-static symbols link directly). Declared here — narrow — so the UI module
// never includes text's private text_internal.h. The AnoQuad mirror must match the
// definition there; ui_path.c static_asserts its size to catch drift.

#ifndef ANO_UI_PATH_H
#define ANO_UI_PATH_H

#include <stdint.h>

// The contour separator ANO_UI_CURVE_SENTINEL is public ABI (anoptic_ui.h) — bridge
// validation replays the walk. Identical to ANO_TEXT_POINT_SENTINEL; one grammar.

// One quadratic Bezier in bake space (double while processing): p0, p1 (control), p2.
typedef struct AnoQuad {
    double x[3];
    double y[3];
} AnoQuad;

// float -> binary16 bits (round-to-nearest-even) and back; bit-exact, matching GLSL
// packHalf2x16/unpackHalf2x16. Defined in src/text/text_bake.c.
uint16_t ano_half_pack(float v);
float    ano_half_unpack(uint16_t h);

// Splits a quad at its interior per-axis extrema into 1..3 chained monotone pieces,
// writes them to out, returns the count. Defined in src/text/text_bake.c.
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

#endif
