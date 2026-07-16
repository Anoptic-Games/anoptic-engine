/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scanline-sweeper bake helpers shared with src/text (both in anoptic_core).
// Narrow decls only: keeps text_internal.h out of the UI module.
// AnoQuad mirror must match text_internal.h. ui_path.c static_asserts its size.

#ifndef ANO_UI_PATH_H
#define ANO_UI_PATH_H

#include <stdint.h>


/* Curve Stream */

// ANO_UI_CURVE_SENTINEL is public ABI (anoptic_ui.h) — bridge validation replays the
// walk. Identical to ANO_TEXT_POINT_SENTINEL; one grammar.

// One quadratic Bezier in bake space (double): p0, p1 (control), p2.
typedef struct AnoQuad {
    double x[3];
    double y[3];
} AnoQuad;

// float <-> binary16 (round-to-nearest-even). Bit-exact with GLSL packHalf2x16.
// Defined in src/text/text_bake.c.
uint16_t ano_half_pack(float v);
float    ano_half_unpack(uint16_t h);

// Split a quad at its interior per-axis extrema into 1..3 chained monotone pieces,
// writes them to out, returns the count. Defined in src/text/text_bake.c.
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

#endif
