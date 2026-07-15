/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text API: font load, glyph bake, shape for the GPU text stack.
// No FreeType type crosses this header.
// Threading: init/load/bake/shutdown on ONE thread. Bake/shape data readable any thread.

#ifndef ANOPTICENGINE_ANOPTIC_TEXT_H
#define ANOPTICENGINE_ANOPTIC_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "anoptic_memory.h"
#include "anoptic_strings.h"


/* Module lifecycle */

// Init. Returns 0 or errno. Idempotent.
int ano_text_init(void);

// Teardown including fonts. Safe if never init'd.
void ano_text_shutdown(void);


/* Fonts */

// Opaque font handle. 0 invalid. Faces live until shutdown.
typedef uint32_t AnoFontId;

// Load scalable outline face from path. Handle or 0.
AnoFontId ano_text_font_load(anostr_t path);

// ano_text_font_load over a path literal.
#define ano_text_font_load_lit(pathlit) ano_text_font_load(anostr_lit(pathlit))

// Scalable face from memory blob (FT_New_Memory_Face). Blob bytes must outlive the face; RM-owned ano_res_bytes of a live handle is the intended source. Empty -> 0.
AnoFontId ano_text_font_load_memory(anostr_t blob);


/* GPU bake */

// Renderer uploads points/glyphs as opaque blobs. Wire format in text_internal.h.

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

// Baked glyph set. Slots assigned in range order. Arrays on caller's heap.
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

// Bake ranges -> GPU blobs on heap. Ranges sorted ascending + disjoint, 4096 slots max. Metrics from ranges[0]. Missing -> blank. Kern never bridges faces; skipped entirely above 1024 slots. Returns 0/EINVAL/ENOMEM/EIO.
int ano_text_font_bake_ranges(const AnoBakeRange *ranges, uint32_t rangeCount,
                              mi_heap_t *heap, AnoFontBake *out);

// Single-range convenience over ano_text_font_bake_ranges.
int ano_text_font_bake(AnoFontId font, uint32_t firstCodepoint, uint32_t lastCodepoint,
                       mi_heap_t *heap, AnoFontBake *out);


/* Shaping */

// v0: UTF-8 -> glyph instances over a bake. Any thread, concurrent.
// AnoGlyphInstance: std430 SSBO element (offsets 0/16/32/40/44, stride 48).
//   inv: 2x2 pixel->em inverse as rows on (pixel - origin). v0: (1/size, 0, 0, -1/size).
//   color: premultiplied linear RGBA. origin: baseline pen, screen px, y-down.
//   glyphID: bake directory slot. flags: reserved.

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

// Styled span for shape_runs/measure_runs. byteCounts consecutive and sum to text length. Lead byte's run styles the codepoint. byteCount 0 is a no-op.
typedef struct AnoTextRun {
    uint32_t byteCount;
    float    sizePx;    // pixels per em, must be > 0
    float    color[4];  // premultiplied linear RGBA
} AnoTextRun;

// Shape UTF-8 at sizePx from origin (screen px, y-down baseline). Writes <=cap, returns total need (out=NULL,cap=0 sizes). Blank advances without emit. '\n' resets penX to origin[0], penY += lineHeight*sizePx; '\r' ignored. Out-of-bake codepoints (incl. U+FFFD from bad UTF-8) advance half-em. Adjacent in-range glyphs kern; newline/gap resets chain. penOut optional final pen. Kern does not bridge calls.
uint32_t ano_text_shape(const AnoFontBake *bake, anostr_t text,
                        float sizePx, const float origin[2], const float color[4],
                        AnoGlyphInstance *out, uint32_t cap, float *penOut);

// Measure in px. Width = max line pen. Height = started lines * lineHeight * sizePx (trailing '\n' starts a line).
void ano_text_measure(const AnoFontBake *bake, anostr_t text,
                      float sizePx, float *width, float *height);

// Shape with style runs. One pen: same-size runs bit-identical to unsplit (kern bridges). SIZE change resets chain. '\n' steps by lineHeight * that run's sizePx. Rejects sizePx<=0 or byteCount sum != len. Else same as ano_text_shape.
uint32_t ano_text_shape_runs(const AnoFontBake *bake, anostr_t text,
                             const AnoTextRun *runs, uint32_t runCount,
                             const float origin[2],
                             AnoGlyphInstance *out, uint32_t cap, float *penOut);

// Measure over runs. Width = max line pen. Height = sum of newline steps (lineHeight * that run's sizePx) + final lineHeight * last run's sizePx.
void ano_text_measure_runs(const AnoFontBake *bake, anostr_t text,
                           const AnoTextRun *runs, uint32_t runCount,
                           float *width, float *height);


/* Literal macros */

// String-literal face macros wrapping anostr_lit, length folded at compile time.

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
