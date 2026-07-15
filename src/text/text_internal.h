/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text internals shared by text.c / text_bake.c, exposed to the white-box unit test. FreeType-free.

#ifndef ANO_TEXT_INTERNAL_H
#define ANO_TEXT_INTERNAL_H

#include <stdint.h>

#include "anoptic_text.h"


/* Face helpers */

// Backing FT_Face for a handle as opaque pointer. NULL if invalid or module down.
void *ano_text_face(AnoFontId font);

// FreeType version via non-NULL pointers. All zeros before init.
void ano_text_version(int *major, int *minor, int *patch);


/* Bake wire format */

// Shared by bake, shaper, ref rasterizer, GPU shaders.
// Point stream: uint32_t per point, binary16 x in 0..15, y in 16..31 (GLSL unpackHalf2x16), em, y-up, origin at baseline pen.
// Curves are directed monotone quadratic Beziers sharing vertices:

//   glyph  := contour (SENTINEL contour)*
//   contour := p0 (p1 p2)+          -- curve i+1 starts at curve i's p2

// ANO_TEXT_POINT_SENTINEL separates contours, never a coordinate.
// Contours close bit-exactly. Fill-right: clockwise outers in y-up, CCW holes. Every curve x- and y-monotone, control inside endpoint box.

#define ANO_TEXT_POINT_SENTINEL 0x7C007C00u

// AnoGlyphEntry.flags bits.
#define ANO_GLYPH_MISSING 0x1u  // codepoint absent from the face (blank stand-in)

// Horizontal kern pair (GPOS PairPos): xAdvance added between glyphs, em, negative pulls together. Sorted by key.
struct AnoKernPair {
    uint32_t key;       // leftSlot << 16 | rightSlot
    float    xAdvance;  // em
};

// Directory map: codepoints first..last occupy slots from slotBase. Ranges sorted ascending and disjoint.
struct AnoGlyphRange {
    uint32_t first, last, slotBase;
};

#define ANO_TEXT_SLOT_NONE UINT32_MAX

// Directory slot for a codepoint, or ANO_TEXT_SLOT_NONE. Pure, any thread.
uint32_t ano_text_bake_slot(const AnoFontBake *bake, uint32_t codepoint);

// Pen advance, em, for codepoints with no slot.
#define ANO_TEXT_GAP_EM 0.5f

// Kern between two slots, em. 0 if absent or out of range. Pure, any thread.
float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot);


/* Bake math */

// float -> binary16 bits, round-to-nearest-even. |v| >= 65536 -> +-inf; RNE may overflow below that.
uint16_t ano_half_pack(float v);

// binary16 bits -> float, exact.
float ano_half_unpack(uint16_t h);

// Quadratic Bezier in bake space (em, double while processing).
typedef struct AnoQuad {
    double x[3];  // p0, p1 (control), p2
    double y[3];
} AnoQuad;

// Split quad at interior per-axis extrema into 1..3 chained monotone pieces. Extrema within 1e-6 of 0/1 or each other merge.
int ano_quad_split_monotone(const AnoQuad *q, AnoQuad out[3]);

// Approximate cubic by quads within tolEm. Count >= 1, or -1 if maxOut < 1. Never exceeds maxOut. Endpoints exact.
int ano_cubic_to_quads(const double px[4], const double py[4], double tolEm,
                       AnoQuad *out, int maxOut);


/* GPOS kerning */

// FreeType-free. Accumulate latn/DFLT 'kern' PairPos xAdvance (lookups sorted by index, first applying subtable per pair) into dense[s1*slotCount+s2], font units, caller-zeroed. slotGids > 0xFFFF = absent. Bounds-checked. Malformed -> nonzero with dense possibly partial. 0 = success including "no kerns".
int ano_gpos_extract_kerns(const uint8_t *gpos, uint32_t len, const uint32_t *slotGids,
                           uint32_t slotCount, int32_t *dense);


/* Reference rasterization */

// Unclamped coverage sum for one em-space window of one glyph's curve stream. Pure, any thread.
float ano_text_window_sum(const uint32_t *pts, const AnoGlyphEntry *g, float wx, float wy,
                          float w, float h);

// CPU ref rasterizer: float mirror of GPU coverage shader, per-glyph [0,1] clamp, no gamma. FT bitmap layout: row 0 = TOP, pixel (r,c) covers x in [(left+c)/S,(left+c+1)/S), y in [(top-r-1)/S,(top-r)/S), S=pixelsPerEm, pen at (0,0). out = width*rows coverage bytes. maxSumOut optional unclamped peak (>1 = same-winding overlap).
void ano_text_raster_ref(const uint32_t *points, const AnoGlyphEntry *glyph,
                         float pixelsPerEm, int left, int top, int width, int rows,
                         uint8_t *out, float *maxSumOut);

// FreeType AA ground truth (linear 8-bit, unhinted) at pixelsPerEm into buf. FT bearings. Returns 0 or EINVAL/EIO/ENOMEM. Module thread.
int ano_text_ref_ft_render(AnoFontId font, uint32_t codepoint, uint32_t pixelsPerEm,
                           uint8_t *buf, uint32_t cap, int *width, int *rows,
                           int *left, int *top);

#endif
