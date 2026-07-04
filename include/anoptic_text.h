/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text API
//
// Font loading, glyph-curve baking, and minimal text shaping for the GPU text stack
// (Scanline Sweeper coverage rasterization -- design of record: FONT_RENDER.md).
// FreeType backs the font parsing internally; no FreeType type crosses this header.
//
// Threading: the module owns a private mimalloc heap (single-writer). ano_text_init,
// all font loading/baking, and ano_text_shutdown must run on the SAME thread. Baked
// blobs and shaped output are plain data, readable from any thread once produced.

#ifndef ANOPTICENGINE_ANOPTIC_TEXT_H
#define ANOPTICENGINE_ANOPTIC_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "anoptic_memory.h"

// ---------------------------------------------------------------------------------------------
// Module lifecycle.

// Initializes the text module: private allocation heap + font parser backend.
// Returns 0 on success, a positive errno-style code on failure. Idempotent.
int ano_text_init(void);

// Tears down all module state, including every loaded font. Safe when never initialized.
void ano_text_shutdown(void);

// Reports the backing font-parser (FreeType) version for startup logs and tests.
// Outputs: major/minor/patch through any non-NULL pointers; all zeros before init.
void ano_text_version(int *major, int *minor, int *patch);

// ---------------------------------------------------------------------------------------------
// Fonts.

// Opaque font handle; 0 is invalid. Faces live until ano_text_shutdown.
typedef uint32_t AnoFontId;

// Loads a scalable (outline) font face from a file path. Returns the handle, or 0 on
// failure (missing file, non-outline face, registry full, module not initialized).
AnoFontId ano_text_font_load(const char *path);

// ---------------------------------------------------------------------------------------------
// The baked glyph form consumed by the GPU rasterizer (FONT_RENDER.md section 2).
//
// Point stream: one uint32_t per point -- IEEE binary16 x in bits 0..15, y in bits
// 16..31 (GLSL unpackHalf2x16 order), in em units, y-up, origin at the baseline pen
// position. Curves are directed monotone quadratic Beziers sharing vertices:
//
//   glyph  := contour (SENTINEL contour)*
//   contour := p0 (p1 p2)+          -- curve i+1 starts at curve i's p2
//
// ANO_TEXT_POINT_SENTINEL separates contours (it never collides with a coordinate:
// both halves are +inf, and coordinates are clamped far below half-float infinity).
// Contours are closed (the last p2 equals the contour's p0 bit-exactly) and wound so
// the paper's signed trapezoid sum is POSITIVE inside ink: clockwise outers in y-up
// (fill-right), counter-clockwise holes. Every curve is x- and y-monotone, so its
// control point lies inside its endpoints' box and bbox tests reduce to endpoints.

#define ANO_TEXT_POINT_SENTINEL 0x7C007C00u

typedef struct AnoGlyphEntry {
    uint32_t pointOffset;  // first entry of this glyph in the point stream
    uint32_t curveCount;   // monotone quads across all contours; 0 = blank (space/missing)
    float    bboxMin[2];   // em units, exact bounds of the QUANTIZED curve points
    float    bboxMax[2];
    float    advance;      // horizontal advance, em units
    uint32_t flags;        // ANO_GLYPH_* bits
} AnoGlyphEntry;

#define ANO_GLYPH_MISSING 0x1u  // codepoint absent from the face (blank stand-in)

// One horizontal kerning pair (GPOS PairPos, shaper v1): xAdvance is added to the pen
// between the left and right glyph, em units (negative pulls them together). key packs
// the two directory slots; the bake's table is sorted by key for binary search.
typedef struct AnoKernPair {
    uint32_t key;       // leftSlot << 16 | rightSlot
    float    xAdvance;  // em
} AnoKernPair;

// One baked codepoint range. Directory slot i corresponds to codepoint firstCodepoint+i;
// all arrays live in the heap passed to ano_text_font_bake and die at its teardown.
typedef struct AnoFontBake {
    const uint32_t      *points;      // packed half-pair stream (see grammar above)
    uint32_t             pointCount;
    const AnoGlyphEntry *glyphs;
    uint32_t             glyphCount;
    uint32_t             firstCodepoint;
    const AnoKernPair   *kerns;       // sorted by key; NULL/0 when the face kerns nothing
    uint32_t             kernCount;
    float                ascender;    // em, above baseline (positive)
    float                descender;   // em, below baseline (typically negative)
    float                lineHeight;  // em, baseline-to-baseline advance
    uint32_t             upem;        // source face units-per-em (provenance)
} AnoFontBake;

// Bakes an inclusive codepoint range of a loaded face into GPU-ready blobs allocated
// from the caller's heap. Missing codepoints bake as blank ANO_GLYPH_MISSING entries.
// Returns 0 on success, EINVAL (bad handle/range/args), ENOMEM, or EIO (face error).
int ano_text_font_bake(AnoFontId font, uint32_t firstCodepoint, uint32_t lastCodepoint,
                       mi_heap_t *heap, AnoFontBake *out);

// ---------------------------------------------------------------------------------------------
// Shaping (v0): UTF-8 -> positioned glyph instances, pure functions over a bake.
//
// Unlike load/bake, shaping touches no parser state: it may run on ANY thread, over
// any bake, concurrently. This is the logic-side entry point -- game code shapes text
// into instance arrays and ships them to the renderer as plain data.
//
// AnoGlyphInstance is the GPU ABI, one std430 SSBO element per glyph. Field order is
// chosen so a GLSL `vec4 inv; vec4 color; vec2 origin; uint glyphID; uint flags;`
// declaration lands on identical offsets (0/16/32/40/44, stride 48).
//   inv     -- 2x2 pixel->em inverse as rows: em.x = dot(inv.xy, d), em.y = dot(inv.zw, d)
//              with d = pixel - origin. Scale, the screen-y-down vs em-y-up flip, and
//              future skew/rotation all fold here; v0 emits (1/size, 0, 0, -1/size).
//   color   -- premultiplied linear RGBA.
//   origin  -- this glyph's baseline pen position, screen pixels, y-down.
//   glyphID -- directory slot in the bake the run was shaped against.
//   flags   -- reserved (f32-curve escape, effects).

typedef struct AnoGlyphInstance {
    float    inv[4];
    float    color[4];
    float    origin[2];
    uint32_t glyphID;
    uint32_t flags;
} AnoGlyphInstance;

static_assert(sizeof(AnoGlyphInstance) == 48, "GPU ABI: 48-byte std430 element");
static_assert(offsetof(AnoGlyphInstance, color) == 16 && offsetof(AnoGlyphInstance, origin) == 32
                  && offsetof(AnoGlyphInstance, glyphID) == 40,
              "GPU ABI: GLSL-compatible offsets");

// Pen advance, in em, for codepoints the bake has no slot for (visible gap, no tofu).
#define ANO_TEXT_GAP_EM 0.5f

// Kern adjustment between two directory slots, em units; 0 when the pair is absent or
// a slot is out of range. Pure, any thread (binary search over the bake's pair table).
float ano_text_kern(const AnoFontBake *bake, uint32_t leftSlot, uint32_t rightSlot);

// Shapes UTF-8 bytes into glyph instances at sizePx pixels per em, the pen starting at
// origin (screen pixels, y-down baseline). Writes up to cap instances to out; returns
// the TOTAL count the text needs (size with out=NULL, cap=0). Blank glyphs (space,
// missing) advance the pen without emitting; '\n' returns the pen to origin[0] and
// steps one lineHeight down; '\r' is ignored; codepoints outside the bake's range
// advance ANO_TEXT_GAP_EM; malformed UTF-8 is consumed byte-wise as out-of-range.
// Adjacent in-range glyphs on one line receive pair kerning (ano_text_kern); a
// newline or out-of-range gap resets the pair chain. penOut (optional, 2 floats)
// receives the final pen so runs can continue seamlessly (e.g. a color change
// mid-line passes penOut as the next call's origin; kerning does NOT bridge calls).
uint32_t ano_text_shape(const AnoFontBake *bake, const char *utf8, uint32_t len,
                        float sizePx, const float origin[2], const float color[4],
                        AnoGlyphInstance *out, uint32_t cap, float *penOut);

// Measures without emitting: width = the widest line's pen advance, height = started
// line count times line height (a trailing '\n' starts a line), both in pixels.
void ano_text_measure(const AnoFontBake *bake, const char *utf8, uint32_t len,
                      float sizePx, float *width, float *height);

#endif
