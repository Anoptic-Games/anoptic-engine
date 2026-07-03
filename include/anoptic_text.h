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

// One baked codepoint range. Directory slot i corresponds to codepoint firstCodepoint+i;
// both arrays live in the heap passed to ano_text_font_bake and die at its teardown.
typedef struct AnoFontBake {
    const uint32_t      *points;      // packed half-pair stream (see grammar above)
    uint32_t             pointCount;
    const AnoGlyphEntry *glyphs;
    uint32_t             glyphCount;
    uint32_t             firstCodepoint;
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

#endif
