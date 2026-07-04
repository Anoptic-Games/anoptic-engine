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

// ---------------------------------------------------------------------------------------------
// GPOS kerning extraction (shaper v1), FreeType-free for the white-box test.

// Accumulates horizontal 'kern' PairPos xAdvance adjustments (latn/DFLT default
// LangSys; lookups in list order; first applying subtable per pair) from a raw GPOS
// table into dense[s1 * slotCount + s2], font units, caller-zeroed. slotGids maps
// directory slots to font glyph ids; values > 0xFFFF mark absent slots. Every read is
// bounds-checked: malformed tables return nonzero (EIO) with dense partially written
// -- callers discard it. Returns 0 on success, including "face kerns nothing".
int ano_gpos_extract_kerns(const uint8_t *gpos, uint32_t len, const uint32_t *slotGids,
                           uint32_t slotCount, int32_t *dense);

// ---------------------------------------------------------------------------------------------
// Shaping internals.

// Decodes the next codepoint from s[0..len), len >= 1. Strict: overlongs, surrogates,
// and values past U+10FFFF yield U+FFFD. *consumed always advances (structurally valid
// sequences consume fully, garbage consumes 1 byte) so scanning cannot stall.
uint32_t ano_utf8_next(const char *s, uint32_t len, uint32_t *consumed);

// ---------------------------------------------------------------------------------------------
// Reference rasterization (FONT_RENDER.md step 3).

// Unclamped coverage sum for one em-space window (position wx/wy, size w/h, em units)
// of one glyph's curve stream -- the reference rasterizer's inner loop, exposed so the
// GPU comparison harness can evaluate the shader's exact windows. Pure, any thread.
float ano_text_window_sum(const uint32_t *pts, const AnoGlyphEntry *g, float wx, float wy,
                          float w, float h);

// CPU reference rasterizer: scalar float mirror of the GPU coverage shader (stream
// grammar walk, the paper's clamped trapezoid integration, per-glyph [0,1] clamp, no
// gamma). Grid follows FreeType bitmap conventions: row 0 is the TOP row; pixel (r,c)
// covers the em window x in [(left+c)/S, (left+c+1)/S), y in [(top-r-1)/S, (top-r)/S)
// with S = pixelsPerEm and the baseline pen origin at pixel-grid (0,0). out receives
// width*rows coverage bytes, row-major. maxSumOut (optional) reports the largest
// UNCLAMPED coverage sum seen -- values > 1 expose overlapping same-winding contours.
void ano_text_raster_ref(const uint32_t *points, const AnoGlyphEntry *glyph,
                         float pixelsPerEm, int left, int top, int width, int rows,
                         uint8_t *out, float *maxSumOut);

// Ground truth for the reference rasterizer: renders one codepoint through FreeType's
// own anti-aliased rasterizer (linear 8-bit coverage, unhinted) at pixelsPerEm. Copies
// the bitmap tightly into buf (capacity cap) and outputs its dimensions and bearings
// (FT conventions: left/top relative to the baseline pen origin). Returns 0 on
// success, EINVAL/EIO/ENOMEM(-style) on failure. Runs on the module thread.
int ano_text_ref_ft_render(AnoFontId font, uint32_t codepoint, uint32_t pixelsPerEm,
                           uint8_t *buf, uint32_t cap, int *width, int *rows,
                           int *left, int *top);

#endif
