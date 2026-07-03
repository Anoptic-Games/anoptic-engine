/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text module internals shared between text.c and text_bake.c, and exposed to the
// white-box unit test (tests include this via the src/ include dir). Deliberately
// FreeType-free: consumers of this header never need FreeType's include paths.

#ifndef ANO_TEXT_INTERNAL_H
#define ANO_TEXT_INTERNAL_H

#include <stdint.h>

#include "anoptic_text.h"

// Backing FT_Face for a font handle as an opaque pointer (cast inside FT-aware TUs);
// NULL when the handle is invalid or the module is down. Implemented in text.c.
void *ano_text_face(AnoFontId font);

// ---------------------------------------------------------------------------------------------
// Bake math, kept pure and non-static for the white-box test.

// float -> IEEE binary16 bits, round-to-nearest-even; |v| >= 65520 clamps to +-inf.
uint16_t ano_half_pack(float v);

// IEEE binary16 bits -> float, exact (every half value is a float).
float ano_half_unpack(uint16_t h);

// One quadratic Bezier in bake space (em units, double while processing).
typedef struct AnoQuad {
    double x[3];  // p0, p1 (control), p2
    double y[3];
} AnoQuad;

// Splits a quad at its interior per-axis derivative extrema so every piece is x- and
// y-monotone. Writes 1..3 pieces to out, returns the count. Piece endpoints chain
// (out[i].p2 == out[i+1].p0); extrema within T of 0/1 (or of each other) are merged,
// mirroring the audited count oracle in FONT_RENDER.md (T = 1e-6).
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

// Approximates one cubic Bezier (points px[4]/py[4]) by quads within tolEm maximum
// deviation, by recursive halving. Writes to out, returns the piece count (>= 1);
// -1 only when maxOut < 1. Halving depth is capped so the count never exceeds maxOut
// (accuracy degrades past the cap rather than failing). Endpoints preserved exactly.
int ano_cubic_to_quads(const double px[4], const double py[4], double tolEm,
                       AnoQuad *out, int maxOut);

#endif
