/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic UI API
//
// Primitive ABI and pure builder verbs for the GPU UI overlay lane. Design of
// record: docs/ui/ui-render.md. Layout, styling, and hit-testing live in caller
// code (the logic thread). This header only packs primitives.
//
// Threading: every function is pure over caller memory. Any thread, no
// allocation, no module state. Colors are premultiplied linear RGBA.
//
// Coordinate contract: builder coordinates are LOGICAL UNITS of the block's
// surface, y-down, origin top-left. A surface owns the logical->device mapping.
// The renderer folds that mapping into the tables exactly once, at compose,
// through the ano_ui_*_scale verbs below. v0 has one surface: the screen
// overlay, scale = the platform content scale, logical extent published by
// RenderSnapshot (anoptic_render.h). Layout and hit-testing never see a pixel.
// All pixel-domain machinery below (AA ramps, tile grids, the reference
// evaluator) runs AFTER the fold, in device pixels.

#ifndef ANOPTICENGINE_ANOPTIC_UI_H
#define ANOPTICENGINE_ANOPTIC_UI_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------
// Primitive kinds and flags.

typedef enum AnoUiPrimKind {
    ANO_UI_RRECT  = 0, // rounded rect: fill (param[0]==0) or border ring (param[0]=width, inside the boundary)
    ANO_UI_SHADOW = 1, // Gaussian rrect shadow/glow: radii[0] uniform corner, param[0] sigma
    ANO_UI_IMAGE  = 2, // rrect-masked textured quad: aux0 texture index, param[0] lod, color = tint
    ANO_UI_PATH   = 3, // filled monotone-quad outline: aux0/aux1 = curve stream offset/count
    ANO_UI_GLYPHS = 4, // AnoGlyphInstance range: aux0/aux1 = first/count, color = tint
} AnoUiPrimKind;

// flags bits [0:1]: register blend mode applied in painter's order.
#define ANO_UI_BLEND_OVER 0x0u // premultiplied src-over
#define ANO_UI_BLEND_ADD  0x1u // rgb-additive glow, coverage does not occlude
#define ANO_UI_BLEND_MASK 0x3u
// flag bits [2+].
#define ANO_UI_FLAG_INNER 0x4u // SHADOW: inner shadow (blur of the complement, masked inside)
// bits 3+ reserved: nine-slice, pixel-snap hints.

// paintRef/clipRef sentinel: none. Doubles as the builder's table-full error return.
#define ANO_UI_REF_NONE 0xFFFFFFFFu

// ---------------------------------------------------------------------------------------------
// The primitive GPU ABI, one std430 SSBO element per prim (offsets 0/16/24/28/32/40/
// 48/64/80/84/88/92, stride 96, GLSL twin in resources/shaders/uicoverage.glsl).
//   inv    : 2x2 pixel->prim inverse as rows, applied to (pixel - origin).
//            Builders emit identity.
//   origin : prim center, y-down. Logical units at build time, device pixels
//            after the compose fold.
//   half   : half extents in prim space. SHADOW prims cull with a +3*sigma pad.
//   param  : kind-specific ([0]: border width | sigma | lod).
//   radii  : per-corner radii (tl, tr, br, bl), pre-clamped by the builder
//            (the CSS adjacent-corner rule).
//   color  : premultiplied linear RGBA. Tint multiplier for IMAGE/GLYPHS, the
//            fill when paintRef == ANO_UI_REF_NONE.

typedef struct AnoUiPrim {
    float    inv[4];
    float    origin[2];
    uint32_t kind;
    uint32_t flags;
    float    half[2];
    float    param[2];
    float    radii[4];
    float    color[4];
    uint32_t paintRef;
    uint32_t clipRef;
    uint32_t aux0;
    uint32_t aux1;
} AnoUiPrim;

static_assert(sizeof(AnoUiPrim) == 96, "GPU ABI: 96-byte std430 element");
static_assert(offsetof(AnoUiPrim, origin) == 16 && offsetof(AnoUiPrim, kind) == 24
                  && offsetof(AnoUiPrim, half) == 32 && offsetof(AnoUiPrim, param) == 40
                  && offsetof(AnoUiPrim, radii) == 48 && offsetof(AnoUiPrim, color) == 64
                  && offsetof(AnoUiPrim, paintRef) == 80,
              "GPU ABI: GLSL-compatible offsets");

// One clip entry: an axis-aligned rect (exact window clamp) plus an optional rounded
// term (coverage multiply). rrHalf[0] < 0 means no rounded term. 48 B std430.
typedef struct AnoUiClip {
    float rect[4];      // minX, minY, maxX, maxY
    float rrCenter[2];
    float rrHalf[2];
    float rrRadii[4];   // per-corner (tl, tr, br, bl)
} AnoUiClip;

static_assert(sizeof(AnoUiClip) == 48 && offsetof(AnoUiClip, rrCenter) == 16
                  && offsetof(AnoUiClip, rrRadii) == 32,
              "GPU ABI: 48-byte clip entry");

// Paint kinds for AnoUiPaint.kind. A prim's paintRef selects one (RRECT and PATH
// fills). ANO_UI_REF_NONE leaves the prim's own color as the flat fill. The gradient
// parameter t comes from the pixel through xform (g = xform * [px,py,1]): linear
// t = g.x, radial t = |g|, conic t = atan2(g.y,g.x)/2pi + 0.5.
#define ANO_UI_GRAD_LINEAR 0u
#define ANO_UI_GRAD_RADIAL 1u
#define ANO_UI_GRAD_CONIC  2u

// Gradient paint ABI. Stops live in the block's stop array [stopFirst, +stopCount),
// sorted ascending by t, interpolated in premultiplied linear. t clamps to the end
// stops (CSS pad). Referenced by AnoUiPrim.paintRef.
typedef struct AnoUiPaint {
    uint32_t kind;      // ANO_UI_GRAD_*
    uint32_t stopFirst;
    uint32_t stopCount;
    uint32_t flags;
    float    xform[6];  // 2x3 pixel->gradient space, rows: g.x = xform[0..2].(px,py,1)
    float    pad[2];
} AnoUiPaint;

typedef struct AnoUiStop {
    float color[4];     // premultiplied linear
    float t;
    float pad[3];
} AnoUiStop;

static_assert(sizeof(AnoUiPaint) == 48 && sizeof(AnoUiStop) == 32, "GPU ABI: paint tables");

// ---------------------------------------------------------------------------------------------
// Builder: packs prims and side tables into caller arrays. Never allocates. A full
// array makes the verb return ANO_UI_REF_NONE and change nothing. Emission order IS
// paint order (later prims render on top).

typedef struct AnoUiBuilder {
    AnoUiPrim  *prims;   uint32_t primCap;   uint32_t primCount;
    AnoUiClip  *clips;   uint32_t clipCap;   uint32_t clipCount;
    AnoUiPaint *paints;  uint32_t paintCap;  uint32_t paintCount;
    AnoUiStop  *stops;   uint32_t stopCap;   uint32_t stopCount;
    uint32_t   *curves;  uint32_t curveCap;  uint32_t curveCount; // packed path curve words
} AnoUiBuilder;

// Zeroes counts and binds the caller arrays. Any table may be NULL with cap 0. The
// curve buffer starts detached (paths need ano_ui_builder_curves before ano_ui_path_fill).
void ano_ui_builder_init(AnoUiBuilder *b,
                         AnoUiPrim *prims, uint32_t primCap,
                         AnoUiClip *clips, uint32_t clipCap,
                         AnoUiPaint *paints, uint32_t paintCap,
                         AnoUiStop *stops, uint32_t stopCap);

// Attaches the curve-stream scratch buffer that ano_ui_path_fill bakes into (packed
// binary16 point words, the text sweeper's grammar). Detach with NULL/cap 0.
void ano_ui_builder_curves(AnoUiBuilder *b, uint32_t *curves, uint32_t curveCap);

// Premultiplied linear from sRGB-authored straight rgba.
void ano_ui_color_srgb(const float srgba[4], float out[4]);

// Rounded rect from a min/max box. radii = per-corner (tl, tr, br, bl), clamped to
// non-negative and scaled down together if adjacent corners would overlap.
// borderWidth 0 fills; > 0 draws a ring of that width inside the boundary.
// Returns the prim index, ANO_UI_REF_NONE when full.
uint32_t ano_ui_rrect(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], const float color[4], float borderWidth,
                      uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// Gaussian shadow of the rrect (uniform cornerRadius). sigma clamps to >= 1e-3.
// ANO_UI_FLAG_INNER selects an inner shadow. ANO_UI_BLEND_ADD turns the same
// math into a glow.
uint32_t ano_ui_shadow(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                       float cornerRadius, float sigma, const float color[4],
                       uint32_t clipRef, uint32_t flags);

// Textured quad masked by the rrect. The full texture maps to the box (uv 0..1).
// tint multiplies premultiplied components. lod is the explicit mip.
uint32_t ano_ui_image(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], uint32_t texIndex, float lod,
                      const float tint[4], uint32_t clipRef, uint32_t flags);

// Filled path covering curve stream words [curveOffset, ...) for curveCount monotone
// quads. bbox is the conservative pixel bounds used for culling and the prim-local
// origin. Low-level: the caller supplies a pre-baked stream. Most callers want
// ano_ui_path_fill, which bakes arbitrary segments.
uint32_t ano_ui_path(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                     uint32_t curveOffset, uint32_t curveCount, const float color[4],
                     uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// One path segment (overlay pixels, y-down). MOVE opens a contour; LINE/QUAD extend it
// from the previous point; each contour auto-closes back to its opening point.
#define ANO_UI_SEG_MOVE 0u // start a new contour at (p[0], p[1])
#define ANO_UI_SEG_LINE 1u // straight edge to (p[0], p[1])
#define ANO_UI_SEG_QUAD 2u // quadratic to (p[2], p[3]) via control (p[0], p[1])

// Contour separator word in the packed curve stream (both binary16 halves +inf), the
// text sweeper's grammar. Part of the ABI: bridge validation and the evaluators walk it.
#define ANO_UI_CURVE_SENTINEL 0x7C007C00u
typedef struct AnoUiPathSeg {
    uint32_t kind;
    float    p[4];
} AnoUiPathSeg;

// Fills a path (contours of lines/quads, auto-closed) with color/paint, baking it into
// the builder's attached curve buffer as monotone quads. Fill is nonzero-winding and
// caller-winding-independent. Holes come from oppositely wound inner contours. The
// prim's bbox and prim-local frame are derived from the points. Returns the prim index,
// or ANO_UI_REF_NONE when a table or the curve buffer is full or the path is empty.
uint32_t ano_ui_path_fill(AnoUiBuilder *b, const AnoUiPathSeg *segs, uint32_t segCount,
                          const float color[4], uint32_t paintRef, uint32_t clipRef,
                          uint32_t flags);

// Glyph range [first, first+count) of the frame's AnoGlyphInstance array, z-ordered
// with the surrounding prims. bbox is the shaped text's conservative pixel bounds.
uint32_t ano_ui_glyphs(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                       uint32_t first, uint32_t count, const float tint[4],
                       uint32_t clipRef, uint32_t flags);

// Clip entry: rect always. Pass rrMin == NULL for a rect-only clip, else the rounded
// term's box + per-corner radii (same clamp rule as ano_ui_rrect). Returns the clip
// index for AnoUiPrim.clipRef, ANO_UI_REF_NONE when full. Nested clips are resolved
// by the CALLER (intersect rects, innermost rounded term wins). One entry per prim.
uint32_t ano_ui_clip(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                     const float rrMin[2], const float rrMax[2], const float rrRadii[4]);

// ---------------------------------------------------------------------------------------------
// Paints: push a gradient's stops + descriptor into the builder's paint/stop tables and
// return the paintRef for a fill prim (RRECT / PATH). stops are copied and sorted
// ascending by t here. Each color is premultiplied linear (use ano_ui_color_srgb).
// Return ANO_UI_REF_NONE when a table is full or stopCount is 0.

// Linear gradient: t runs 0 at p0 to 1 at p1 along the p0->p1 axis, constant across it.
uint32_t ano_ui_paint_linear(AnoUiBuilder *b, const float p0[2], const float p1[2],
                             const AnoUiStop *stops, uint32_t stopCount);

// Radial gradient: t = |pixel - center| / radius, 0 at the center, 1 on the circle.
uint32_t ano_ui_paint_radial(AnoUiBuilder *b, const float center[2], float radius,
                             const AnoUiStop *stops, uint32_t stopCount);

// Conic (angular) gradient about center: t sweeps 0..1 once around, 0 at startAngle
// (radians, y-down so positive turns clockwise on screen). A hard seam at startAngle.
uint32_t ano_ui_paint_conic(AnoUiBuilder *b, const float center[2], float startAngle,
                            const AnoUiStop *stops, uint32_t stopCount);

// Standing demo scene, the GPU self-test target (ui-render.md §7 step 4): deterministic,
// reference-evaluable prims only (no IMAGE/PATH/GLYPHS). Needs caps >= 16 prims / 4
// clips. The render-side demo compose and the offline screenshot compare both build
// exactly this. Keep it bitwise-stable.
void ano_ui_demo_scene(AnoUiBuilder *b, float originX, float originY);

// ---------------------------------------------------------------------------------------------
// Surface fold: multiplies an isotropic logical->device scale into table entries, in
// place. The renderer applies these once at compose. Everything downstream (tiles,
// evaluators, GPU) is device pixels. Pure like every verb here. s > 0. Anisotropic
// surface scales are out of contract.

// Prim: origin/half/radii scale. param[0] scales for RRECT (border width) and SHADOW
// (sigma). IMAGE lod shifts by -log2(s). PATH curve words and GLYPHS instances live
// outside the prim and fold separately (ano_ui_curves_scale; glyph instances scale
// origin by s, inv by 1/s).
void ano_ui_prim_scale(AnoUiPrim *p, float s);

// Clip: rect and rounded term scale. The rrHalf[0] < 0 "no rounded term" sentinel
// stays negative under any s > 0.
void ano_ui_clip_scale(AnoUiClip *c, float s);

// Paint: linear columns divide by s. The translation column is already gradient-space.
void ano_ui_paint_scale(AnoUiPaint *p, float s);

// Curve stream: copies count words from in to out (in == out is legal), scaling each
// packed binary16 point pair. The contour sentinel's +inf halves scale to +inf, so
// separators survive untouched.
void ano_ui_curves_scale(const uint32_t *in, uint32_t *out, uint32_t count, float s);

// ---------------------------------------------------------------------------------------------
// Reference evaluator (validation): scalar mirror of the GPU prim math, one
// implementation for tests and the GPU self-test harness (the text_raster_ref.c
// discipline). Implementation: src/ui/ui_raster_ref.c.

typedef struct AnoUiScene {
    const AnoUiPrim  *prims;  uint32_t primCount;
    const AnoUiClip  *clips;  uint32_t clipCount;
    const AnoUiPaint *paints; uint32_t paintCount;
    const AnoUiStop  *stops;  uint32_t stopCount;
    const uint32_t   *curves; uint32_t curveCount; // packed path curve words
} AnoUiScene;

// The builder's current contents as a scene view (no copy).
static inline AnoUiScene ano_ui_scene(const AnoUiBuilder *b)
{
    return (AnoUiScene){ b->prims, b->primCount, b->clips, b->clipCount,
                         b->paints, b->paintCount, b->stops, b->stopCount,
                         b->curves, b->curveCount };
}

// Exact signed distance to the prim-space rounded box (per-corner radii, y-down
// quadrant select). p is relative to the box center.
float ano_ui_ref_sd_rrect(const float p[2], const float half[2], const float radii[4]);

// Closed-form Gaussian-blurred rounded box (Wallace: erf along x, 4-sample Gaussian
// quadrature along y), evaluated at p relative to the box center. Returns [0,1].
float ano_ui_ref_shadow(const float p[2], const float half[2], float corner, float sigma);

// Resolved paint color at pixel (px,py) modulated by base, premultiplied linear.
// paintRef == ANO_UI_REF_NONE returns base unchanged. An out-of-range ref fails CLOSED
// (transparent), the clip-table policy. The GPU twin is ui_paint_eval.
void ano_ui_ref_paint(const AnoUiScene *s, uint32_t paintRef, float px, float py,
                      const float base[4], float out[4]);

// Premultiplied contribution of one prim over the unit pixel window [px,px+1)x[py,py+1),
// clip and paint applied. PATH/GLYPHS evaluate to zero here.
void ano_ui_ref_shade(const AnoUiScene *s, uint32_t prim, float px, float py, float out[4]);

// Painter's-order evaluation of the whole scene at one pixel (premultiplied linear,
// the register blend loop the GPU lane mirrors).
void ano_ui_ref_eval(const AnoUiScene *s, float px, float py, float out[4]);

// ---------------------------------------------------------------------------------------------
// Per-tile prim lists (ui-render.md §3.7 step 2-3): the CPU-coarse stage of the scaling
// ladder, built at compose cadence. The GPU walks only the prims touching each 8x8 tile.
// A tile entry is a prim index with a "solid" high bit: the prim fully covers the tile
// (coverage provably 1), letting the GPU skip the SDF and take the flat fill. Glyphs
// are NOT tiled here.

#define ANO_UI_TILE_PX          8u          // tile edge, matches the 8x8 compute workgroup
#define ANO_UI_ENTRY_SOLID      0x80000000u // tile entry: prim fully covers the tile
#define ANO_UI_ENTRY_INDEX_MASK 0x7FFFFFFFu

// Padded pixel AABB of one prim: its influence region (half extent + the 1px AA ramp, or
// 3*sigma for a shadow). Identity-inv only (v0). Matches the GPU cull and pending bounds.
void ano_ui_prim_aabb(const AnoUiPrim *p, float outMin[2], float outMax[2]);

// Builds a dense tile grid (tilesX*tilesY tiles of 8px, top-left at ox,oy in overlay px)
// from the scene's prims. Writes offsets[0..tilesX*tilesY] (tile t owns entries
// [offsets[t], offsets[t+1])) and the prim-index entry stream. cursor is tilesX*tilesY
// scratch. Returns the entry count. Sets *ok false and bails if offsetsCap (needs
// tilesX*tilesY+1) or entryCap is too small.
uint32_t ano_ui_tile_build(const AnoUiScene *s, int32_t ox, int32_t oy,
                           uint32_t tilesX, uint32_t tilesY,
                           uint32_t *offsets, uint32_t offsetsCap,
                           uint32_t *entries, uint32_t entryCap,
                           uint32_t *cursor, bool *ok);

// Painter's-order evaluation at one pixel through the tile grid: mirrors ano_ui_ref_eval
// but walks only pixel (px,py)'s tile list. Bit-identical to ano_ui_ref_eval when the
// grid covers the pixel. The GPU tiled path mirrors THIS. Glyphs are not included.
void ano_ui_ref_eval_tiled(const AnoUiScene *s, int32_t ox, int32_t oy,
                           uint32_t tilesX, uint32_t tilesY, const uint32_t *offsets,
                           const uint32_t *entries, int32_t px, int32_t py, float out[4]);

#endif
