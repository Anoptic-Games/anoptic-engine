/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text module internals shared between text.c and text_bake.c, exposed to the
// white-box unit test. FreeType-free.

#ifndef ANO_TEXT_INTERNAL_H
#define ANO_TEXT_INTERNAL_H

#include <stdint.h>

#include "anoptic_text.h"

// Backing FT_Face for a font handle as an opaque pointer, NULL when invalid or the
// module is down. Implemented in text.c.
void *ano_text_face(AnoFontId font);

// Reports the FreeType version through any non-NULL pointers, all zeros before init.
void ano_text_version(int *major, int *minor, int *patch);

// The bake's wire format, decoded only by the bake, the
// shaper, the reference rasterizer, and the GPU shaders.
//
// Point stream: one uint32_t per point, binary16 x in bits 0..15 and y in bits 16..31
// (GLSL unpackHalf2x16 order), em units, y-up, origin at the baseline pen position.
// Curves are directed monotone quadratic Beziers sharing vertices:
//
//   glyph  := contour (SENTINEL contour)*
//   contour := p0 (p1 p2)+          -- curve i+1 starts at curve i's p2
//
// ANO_TEXT_POINT_SENTINEL separates contours and never collides with a coordinate.
// Contours close bit-exactly and wind so the signed trapezoid sum is positive inside
// ink: clockwise outers in y-up (fill-right), counter-clockwise holes. Every curve is
// x- and y-monotone, its control point inside its endpoints' box.

#define ANO_TEXT_POINT_SENTINEL 0x7C007C00u

// AnoGlyphEntry.flags bits.
#define ANO_GLYPH_MISSING 0x1u  // codepoint absent from the face (blank stand-in)

// One horizontal kerning pair (GPOS PairPos): xAdvance is added to the pen between
// the two glyphs, em units, negative pulls them together. Sorted by key.
struct AnoKernPair {
    uint32_t key;       // leftSlot << 16 | rightSlot
    float    xAdvance;  // em
};

// One directory mapping: codepoints first..last occupy consecutive slots starting at
// slotBase. Ranges are sorted ascending and disjoint.
struct AnoGlyphRange {
    uint32_t first, last, slotBase;
};

#define ANO_TEXT_SLOT_NONE UINT32_MAX

// Directory slot for a codepoint, ANO_TEXT_SLOT_NONE when the bake holds none.
// Pure, any thread. Implemented in text_shape.c.
uint32_t ano_text_bake_slot(const AnoFontBake *bake, uint32_t codepoint);

// Pen advance, in em, for codepoints the bake has no slot for.
#define ANO_TEXT_GAP_EM 0.5f

// Kern adjustment between two directory slots, em units, 0 when the pair is absent or
// a slot is out of range. Pure, any thread.
float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot);

// Bake math.

// float -> binary16 bits, round-to-nearest-even. |v| >= 65520 clamps to +-inf.
uint16_t ano_half_pack(float v);

// binary16 bits -> float, exact.
float ano_half_unpack(uint16_t h);

// One quadratic Bezier in bake space (em units, double while processing).
typedef struct AnoQuad {
    double x[3];  // p0, p1 (control), p2
    double y[3];
} AnoQuad;

// Splits a quad at its interior per-axis extrema into 1..3 chained monotone pieces.
// Writes to out, returns the count. Extrema within 1e-6 of 0/1 or of each other merge.
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

// Approximates one cubic Bezier by quads within tolEm max deviation. Writes to out,
// returns the piece count (>= 1), or -1 when maxOut < 1. Count never exceeds maxOut.
// Endpoints preserved exactly.
int ano_cubic_to_quads(const double px[4], const double py[4], double tolEm,
                       AnoQuad *out, int maxOut);

// GPOS kerning extraction, FreeType-free.

// Accumulates horizontal 'kern' PairPos xAdvance adjustments (latn/DFLT, lookups in
// list order, first applying subtable per pair) from a raw GPOS table into
// dense[s1 * slotCount + s2], font units, caller-zeroed. slotGids maps directory
// slots to glyph ids, values > 0xFFFF mark absent slots. Every read is bounds-checked.
// Malformed tables return nonzero with dense partially written, callers discard it.
// Returns 0 on success, including "face kerns nothing".
int ano_gpos_extract_kerns(const uint8_t *gpos, uint32_t len, const uint32_t *slotGids,
                           uint32_t slotCount, int32_t *dense);

// Reference rasterization.

// Unclamped coverage sum for one em-space window (position wx/wy, size w/h) of one
// glyph's curve stream. The reference rasterizer's inner loop. Pure, any thread.
float ano_text_window_sum(const uint32_t *pts, const AnoGlyphEntry *g, float wx, float wy,
                          float w, float h);

// CPU reference rasterizer: scalar float mirror of the GPU coverage shader, per-glyph
// [0,1] clamp, no gamma. Grid follows FreeType bitmap conventions: row 0 is the TOP
// row, pixel (r,c) covers the em window x in [(left+c)/S, (left+c+1)/S) and y in
// [(top-r-1)/S, (top-r)/S) with S = pixelsPerEm, baseline pen origin at (0,0).
// out receives width*rows coverage bytes, row-major. maxSumOut (optional) reports the
// largest UNCLAMPED coverage sum. Values > 1 expose same-winding overlaps.
void ano_text_raster_ref(const uint32_t *points, const AnoGlyphEntry *glyph,
                         float pixelsPerEm, int left, int top, int width, int rows,
                         uint8_t *out, float *maxSumOut);

// Ground truth for the reference rasterizer: renders one codepoint through FreeType's
// own AA rasterizer (linear 8-bit coverage, unhinted) at pixelsPerEm. Copies the
// bitmap tightly into buf (capacity cap) and outputs its dimensions and bearings
// (FT conventions). Returns 0, or EINVAL/EIO/ENOMEM. Runs on the module thread.
int ano_text_ref_ft_render(AnoFontId font, uint32_t codepoint, uint32_t pixelsPerEm,
                           uint8_t *buf, uint32_t cap, int *width, int *rows,
                           int *left, int *top);

#endif
