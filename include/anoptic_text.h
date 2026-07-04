/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text API
//
// Font loading, glyph-curve baking, and minimal text shaping for the GPU text stack.
// Design of record: FONT_RENDER.md. No FreeType type crosses this header.
//
// Threading: init, all font loading/baking, and shutdown must run on the SAME thread.
// Baked blobs and shaped output are plain data, readable from any thread.

#ifndef ANOPTICENGINE_ANOPTIC_TEXT_H
#define ANOPTICENGINE_ANOPTIC_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "anoptic_memory.h"
#include "anoptic_strings.h"

// ---------------------------------------------------------------------------------------------
// Module lifecycle.

// Initializes the text module. Returns 0 or an errno-style code. Idempotent.
int ano_text_init(void);

// Tears down all module state, including loaded fonts. Safe when never initialized.
void ano_text_shutdown(void);

// ---------------------------------------------------------------------------------------------
// Fonts.

// Opaque font handle; 0 is invalid. Faces live until ano_text_shutdown.
typedef uint32_t AnoFontId;

// Loads a scalable (outline) font face from a file path. Returns the handle, 0 on failure.
AnoFontId ano_text_font_load(anostr_t path);

// ano_text_font_load over a path literal.
#define ano_text_font_load_lit(pathlit) ano_text_font_load(anostr_lit(pathlit))

// ---------------------------------------------------------------------------------------------
// The baked glyph form consumed by the GPU rasterizer. The renderer uploads the
// points/glyphs as opaque blobs; the wire format lives in src/text/text_internal.h.

// One directory entry, the per-glyph GPU ABI.
typedef struct AnoGlyphEntry {
    uint32_t pointOffset;  // first entry of this glyph in the point stream
    uint32_t curveCount;   // monotone quads across all contours, 0 = blank
    float    bboxMin[2];   // em units, exact bounds of the quantized curve points
    float    bboxMax[2];
    float    advance;      // horizontal advance, em units
    uint32_t flags;        // ANO_GLYPH_* bits (text_internal.h)
} AnoGlyphEntry;

// Shaper-internal tables the bake carries. Complete types in text_internal.h.
typedef struct AnoKernPair   AnoKernPair;   // GPOS pair kerning
typedef struct AnoGlyphRange AnoGlyphRange; // codepoint range -> directory slot map

// One codepoint range to bake. Ranges may draw from different faces.
typedef struct AnoBakeRange {
    AnoFontId font;
    uint32_t  first;  // inclusive
    uint32_t  last;   // inclusive
} AnoBakeRange;

// A baked glyph set. Directory slots are assigned range by range in input order.
// All arrays live in the heap passed to the bake call.
typedef struct AnoFontBake {
    const uint32_t      *points;      // packed half-pair stream (opaque, upload wholesale)
    uint32_t             pointCount;
    const AnoGlyphEntry *glyphs;
    uint32_t             glyphCount;
    const AnoGlyphRange *ranges;      // codepoint -> slot map, sorted by first
    uint32_t             rangeCount;
    const AnoKernPair   *kerns;       // sorted by key, NULL/0 when nothing kerns
    uint32_t             kernCount;
    float                ascender;    // em, above baseline (positive)
    float                descender;   // em, below baseline (typically negative)
    float                lineHeight;  // em, baseline-to-baseline advance
    uint32_t             upem;        // source face units-per-em (provenance)
} AnoFontBake;

// Bakes codepoint ranges of loaded faces into GPU-ready blobs on the caller's heap.
// Ranges must be sorted ascending and disjoint, 4096 slots max. Metrics come from
// ranges[0]'s face. Missing codepoints bake as blank entries. Kern pairs never
// bridge faces. Returns 0, EINVAL, ENOMEM, or EIO.
int ano_text_font_bake_ranges(const AnoBakeRange *ranges, uint32_t rangeCount,
                              mi_heap_t *heap, AnoFontBake *out);

// Single-range convenience over ano_text_font_bake_ranges.
int ano_text_font_bake(AnoFontId font, uint32_t firstCodepoint, uint32_t lastCodepoint,
                       mi_heap_t *heap, AnoFontBake *out);

// ---------------------------------------------------------------------------------------------
// Shaping (v0): UTF-8 -> positioned glyph instances, pure functions over a bake.
// May run on ANY thread, over any bake, concurrently.
//
// AnoGlyphInstance is the GPU ABI, one std430 SSBO element per glyph (offsets
// 0/16/32/40/44, stride 48, GLSL-compatible).
//   inv     -- 2x2 pixel->em inverse as rows, applied to (pixel - origin).
//              v0 emits (1/size, 0, 0, -1/size).
//   color   -- premultiplied linear RGBA.
//   origin  -- baseline pen position, screen pixels, y-down.
//   glyphID -- directory slot in the bake.
//   flags   -- reserved.

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

// One styled span of a shape_runs/measure_runs call: the next byteCount bytes render
// at sizePx with this color. Runs are consecutive and their byteCount sum IS the text
// length. A codepoint takes the style of the run holding its LEAD byte. byteCount 0
// is a legal no-op.
typedef struct AnoTextRun {
    uint32_t byteCount;
    float    sizePx;    // pixels per em, must be > 0
    float    color[4];  // premultiplied linear RGBA
} AnoTextRun;

// Shapes a UTF-8 string into glyph instances at sizePx, the pen starting at origin
// (screen pixels, y-down baseline). Writes up to cap instances to out and returns the
// TOTAL count the text needs (size with out=NULL, cap=0). Blank glyphs advance without
// emitting. '\n' returns the pen to origin[0] and steps one lineHeight down, '\r' is
// ignored. Codepoints without a slot advance a fixed half-em gap, as does malformed
// UTF-8 (decoded as U+FFFD per byte). Adjacent in-range glyphs on one line receive
// pair kerning. A newline or gap resets the pair chain. penOut (optional, 2 floats)
// receives the final pen so a later call can continue. Kerning does NOT bridge calls.
uint32_t ano_text_shape(const AnoFontBake *bake, anostr_t text,
                        float sizePx, const float origin[2], const float color[4],
                        AnoGlyphInstance *out, uint32_t cap, float *penOut);

// Measures without emitting, in pixels. Width = the widest line's pen advance.
// Height = started line count times line height (a trailing '\n' starts a line).
void ano_text_measure(const AnoFontBake *bake, anostr_t text,
                      float sizePx, float *width, float *height);

// ano_text_shape with per-glyph style runs. One pen walks the whole text: same-size
// runs yield bit-identical positions to the unsplit shape (kerning bridges the
// boundary). A SIZE change resets the pair chain, and '\n' steps by the lineHeight of
// its own run. Rejects (returns 0) any run with sizePx <= 0 or a byteCount sum that
// mismatches anostr_len(text). Otherwise identical semantics to ano_text_shape.
uint32_t ano_text_shape_runs(const AnoFontBake *bake, anostr_t text,
                             const AnoTextRun *runs, uint32_t runCount,
                             const float origin[2],
                             AnoGlyphInstance *out, uint32_t cap, float *penOut);

// ano_text_measure over runs. Width = the widest line's pen advance. Height = the sum
// over started lines of the lineHeight in effect at each line's END.
void ano_text_measure_runs(const AnoFontBake *bake, anostr_t text,
                           const AnoTextRun *runs, uint32_t runCount,
                           float *width, float *height);

// ---------------------------------------------------------------------------------------------
// String-literal faces, mirroring the logger's macro style. These wrap anostr_lit so
// call sites can pass plain literals (length folded at compile time, no allocation).

#define ano_text_shape_lit(bake, textlit, sizePx, origin, color, out, cap, penOut) \
    ano_text_shape((bake), anostr_lit(textlit), (sizePx), (origin), (color), (out), (cap), (penOut))

#define ano_text_measure_lit(bake, textlit, sizePx, width, height) \
    ano_text_measure((bake), anostr_lit(textlit), (sizePx), (width), (height))

#define ano_text_shape_runs_lit(bake, textlit, runs, runCount, origin, out, cap, penOut) \
    ano_text_shape_runs((bake), anostr_lit(textlit), (runs), (runCount), (origin), (out), (cap), \
                        (penOut))

#define ano_text_measure_runs_lit(bake, textlit, runs, runCount, width, height) \
    ano_text_measure_runs((bake), anostr_lit(textlit), (runs), (runCount), (width), (height))

#endif
