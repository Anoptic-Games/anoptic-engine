/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic UI API
// Primitive ABI + pure builder verbs for the GPU UI overlay lane. docs/ui/ui-render.md.
// Layout, styling, hit-testing live in the caller (logic thread).
// Pure over caller memory: any thread, no alloc, no module state. Premultiplied linear RGBA.
// Builder coords: LOGICAL UNITS of the block surface, y-down, origin top-left.
// Renderer folds logical->device once at compose via ano_ui_*_scale. Layout never sees pixels.
// AA ramps, tiles, and the reference evaluator run AFTER the fold, in device pixels.

#ifndef ANOPTICENGINE_ANOPTIC_UI_H
#define ANOPTICENGINE_ANOPTIC_UI_H

#include <stddef.h>
#include <stdint.h>


/* Primitive Kinds */

typedef enum AnoUiPrimKind {
    ANO_UI_RRECT  = 0, // rounded rect: fill (param[0]==0) or border ring (param[0]=width, inside)
    ANO_UI_SHADOW = 1, // Gaussian rrect shadow/glow: radii[0] uniform corner, param[0] sigma
    ANO_UI_IMAGE  = 2, // rrect-masked textured quad: aux0 tex index, param[0] lod, color = tint
    ANO_UI_PATH   = 3, // filled monotone-quad outline: aux0/aux1 = curveOffset / monotone-quad count
    ANO_UI_GLYPHS = 4, // AnoGlyphInstance range: aux0/aux1 = first/count, color = tint
} AnoUiPrimKind;

// flags bits [0:1]: register blend mode, painter's order.
#define ANO_UI_BLEND_OVER 0x0u // premultiplied src-over
#define ANO_UI_BLEND_ADD  0x1u // rgb-additive glow, coverage does not occlude
#define ANO_UI_BLEND_MASK 0x3u
// flag bits [2+].
#define ANO_UI_FLAG_INNER 0x4u // SHADOW: inner shadow (blur of complement, masked inside)
// bits 3+ reserved: nine-slice, pixel-snap hints.

// paintRef/clipRef sentinel: none. Also the builder's table-full error return.
#define ANO_UI_REF_NONE 0xFFFFFFFFu


/* Primitive ABI */

// One std430 SSBO element (stride 96, GLSL twin uicoverage.glsl).
// Offsets 0/16/24/28/32/40/48/64/80/84/88/92.
//   inv    : 2x2 pixel->prim inverse as rows on (pixel - origin). Builders emit identity.
//   origin : prim center, y-down. Logical at build, device pixels after compose fold.
//   half   : half extents in prim space. SHADOW culls with +3*sigma + 1px AA.
//   param  : kind-specific ([0]: border width | sigma | lod).
//   radii  : per-corner (tl, tr, br, bl), pre-clamped by builder (CSS adjacent-side rule).
//   color  : premultiplied linear RGBA. Tint for IMAGE/GLYPHS, fill when paintRef is NONE.

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

// Clip entry: AA rect (exact window clamp) + optional rounded term (coverage multiply).
// rrHalf[0] < 0: no rounded term. 48 B std430.
typedef struct AnoUiClip {
    float rect[4];      // minX, minY, maxX, maxY
    float rrCenter[2];
    float rrHalf[2];
    float rrRadii[4];   // per-corner (tl, tr, br, bl)
} AnoUiClip;

static_assert(sizeof(AnoUiClip) == 48 && offsetof(AnoUiClip, rrCenter) == 16
                  && offsetof(AnoUiClip, rrRadii) == 32,
              "GPU ABI: 48-byte clip entry");

// Paint kinds for AnoUiPaint.kind (RRECT and PATH fills). NONE uses prim color as flat fill.
// t from pixel through xform (g = xform * [px,py,1]): linear t=g.x, radial t=|g|,
// conic t=atan2(g.y,g.x)/2pi + 0.5.
#define ANO_UI_GRAD_LINEAR 0u
#define ANO_UI_GRAD_RADIAL 1u
#define ANO_UI_GRAD_CONIC  2u

// Gradient paint ABI. Stops in [stopFirst, +stopCount), ascending t, premultiplied linear.
// t clamps to end stops (CSS pad). Referenced by AnoUiPrim.paintRef.
typedef struct AnoUiPaint {
    uint32_t kind;      // ANO_UI_GRAD_*
    uint32_t stopFirst;
    uint32_t stopCount;
    uint32_t flags;
    float    xform[6];  // 2x3 pixel->gradient space: g.x = xform[0..2].(px,py,1)
    float    pad[2];
} AnoUiPaint;

typedef struct AnoUiStop {
    float color[4];     // premultiplied linear
    float t;
    float pad[3];
} AnoUiStop;

static_assert(sizeof(AnoUiPaint) == 48 && sizeof(AnoUiStop) == 32, "GPU ABI: paint tables");


/* Builder */

// Packs prims and side tables into caller arrays. Never allocates.
// Full array -> ANO_UI_REF_NONE, no mutation. Emission order IS paint order.

typedef struct AnoUiBuilder {
    AnoUiPrim  *prims;   uint32_t primCap;   uint32_t primCount;
    AnoUiClip  *clips;   uint32_t clipCap;   uint32_t clipCount;
    AnoUiPaint *paints;  uint32_t paintCap;  uint32_t paintCount;
    AnoUiStop  *stops;   uint32_t stopCap;   uint32_t stopCount;
    uint32_t   *curves;  uint32_t curveCap;  uint32_t curveCount; // packed path curve words
} AnoUiBuilder;

// Zeroes counts and binds caller arrays. NULL + cap 0 legal. Curves start detached.
void ano_ui_builder_init(AnoUiBuilder *b,
                         AnoUiPrim *prims, uint32_t primCap,
                         AnoUiClip *clips, uint32_t clipCap,
                         AnoUiPaint *paints, uint32_t paintCap,
                         AnoUiStop *stops, uint32_t stopCap);

// Attaches curve-stream scratch for ano_ui_path_fill (packed binary16 words). NULL/0 detaches.
void ano_ui_builder_curves(AnoUiBuilder *b, uint32_t *curves, uint32_t curveCap);

// Premultiplied linear from sRGB-authored straight rgba.
void ano_ui_color_srgb(const float srgba[4], float out[4]);

// Rounded rect from min/max. radii = (tl,tr,br,bl), non-neg + CSS adjacent-side clamp.
// borderWidth 0 = fill, >0 = ring inside boundary. Returns prim index or ANO_UI_REF_NONE.
uint32_t ano_ui_rrect(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], const float color[4], float borderWidth,
                      uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// Gaussian shadow of the rrect (uniform cornerRadius). sigma clamps to >= 1e-3.
// ANO_UI_FLAG_INNER = inner shadow. ANO_UI_BLEND_ADD = glow.
uint32_t ano_ui_shadow(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                       float cornerRadius, float sigma, const float color[4],
                       uint32_t clipRef, uint32_t flags);

// Textured quad masked by rrect. Full texture maps to the box (uv 0..1). lod = explicit mip.
uint32_t ano_ui_image(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], uint32_t texIndex, float lod,
                      const float tint[4], uint32_t clipRef, uint32_t flags);

// Filled path over pre-baked curve stream at curveOffset for curveCount monotone quads.
// bbox = conservative cull bounds (logical) + prim-local origin. Prefer ano_ui_path_fill.
uint32_t ano_ui_path(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                     uint32_t curveOffset, uint32_t curveCount, const float color[4],
                     uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// Path segment (logical units, y-down). MOVE opens a contour; LINE/QUAD extend from prior point.
// Each contour auto-closes to its opening point.
#define ANO_UI_SEG_MOVE 0u // start a new contour at (p[0], p[1])
#define ANO_UI_SEG_LINE 1u // straight edge to (p[0], p[1])
#define ANO_UI_SEG_QUAD 2u // quadratic to (p[2], p[3]) via control (p[0], p[1])

// Contour separator in the packed curve stream (both binary16 halves +inf). Public ABI.
#define ANO_UI_CURVE_SENTINEL 0x7C007C00u
typedef struct AnoUiPathSeg {
    uint32_t kind;
    float    p[4];
} AnoUiPathSeg;

// Fills a path (lines/quads, auto-closed) into the curve buffer as monotone quads.
// Nonzero-winding, winding-independent. Opposite-wound inners punch holes.
// Returns prim index, or ANO_UI_REF_NONE if a table/curve buffer is full or path empty.
uint32_t ano_ui_path_fill(AnoUiBuilder *b, const AnoUiPathSeg *segs, uint32_t segCount,
                          const float color[4], uint32_t paintRef, uint32_t clipRef,
                          uint32_t flags);

// Glyph range [first, first+count) of the frame's AnoGlyphInstance array.
// bbox = shaped text's conservative bounds (logical).
uint32_t ano_ui_glyphs(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                       uint32_t first, uint32_t count, const float tint[4],
                       uint32_t clipRef, uint32_t flags);

// Clip entry: rect always. rrMin NULL = rect-only, else rounded term + radii (rrect clamp).
// Nested clips: CALLER intersects rects, innermost rounded term wins. One entry per prim.
uint32_t ano_ui_clip(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                     const float rrMin[2], const float rrMax[2], const float rrRadii[4]);


/* Paints */

// Push gradient stops + descriptor, return paintRef for RRECT/PATH fills.
// Stops copied and sorted ascending by t. Colors are premultiplied linear.
// ANO_UI_REF_NONE when a table is full or stopCount is 0.

// Linear: t runs 0 at p0 to 1 at p1 along p0->p1, constant across it.
uint32_t ano_ui_paint_linear(AnoUiBuilder *b, const float p0[2], const float p1[2],
                             const AnoUiStop *stops, uint32_t stopCount);

// Radial: t = |pixel - center| / radius, 0 at center, 1 on the circle.
uint32_t ano_ui_paint_radial(AnoUiBuilder *b, const float center[2], float radius,
                             const AnoUiStop *stops, uint32_t stopCount);

// Conic: t sweeps 0..1 about center, 0 at startAngle (radians, y-down = clockwise on screen).
uint32_t ano_ui_paint_conic(AnoUiBuilder *b, const float center[2], float startAngle,
                            const AnoUiStop *stops, uint32_t stopCount);

// Standing demo scene (ui-render.md §7): deterministic, reference-evaluable (no IMAGE/GLYPHS).
// Caps >= 16 prims / 4 clips / 1 paint / 2 stops / 16 curve words. Keep bitwise-stable.
void ano_ui_demo_scene(AnoUiBuilder *b, float originX, float originY);


/* Surface Fold */

// Isotropic logical->device scale into table entries in place, once at compose.
// Downstream is device pixels. Pure. s > 0. Anisotropic scales are out of contract.

// Prim: origin/half/radii scale. param[0] scales for RRECT border and SHADOW sigma.
// IMAGE lod shifts by -log2(s). PATH curves and GLYPHS fold separately.
void ano_ui_prim_scale(AnoUiPrim *p, float s);

// Clip: rect and rounded term scale. rrHalf[0] < 0 sentinel stays negative for any s > 0.
void ano_ui_clip_scale(AnoUiClip *c, float s);

// Paint: linear columns divide by s. Translation column is already gradient-space.
void ano_ui_paint_scale(AnoUiPaint *p, float s);

// Curve stream: copy count words (in == out legal), scale each packed binary16 point pair.
// Contour sentinel +inf halves stay +inf.
void ano_ui_curves_scale(const uint32_t *in, uint32_t *out, uint32_t count, float s);


/* Reference Evaluator */

// Scalar mirror of the GPU prim math. src/ui/ui_raster_ref.c.

typedef struct AnoUiScene {
    const AnoUiPrim  *prims;  uint32_t primCount;
    const AnoUiClip  *clips;  uint32_t clipCount;
    const AnoUiPaint *paints; uint32_t paintCount;
    const AnoUiStop  *stops;  uint32_t stopCount;
    const uint32_t   *curves; uint32_t curveCount; // packed path curve words
} AnoUiScene;

// Builder contents as a scene view (no copy).
static inline AnoUiScene ano_ui_scene(const AnoUiBuilder *b)
{
    return (AnoUiScene){ b->prims, b->primCount, b->clips, b->clipCount,
                         b->paints, b->paintCount, b->stops, b->stopCount,
                         b->curves, b->curveCount };
}

// Exact signed distance to the prim-space rounded box. p relative to box center.
float ano_ui_ref_sd_rrect(const float p[2], const float half[2], const float radii[4]);

// Closed-form Gaussian-blurred rounded box (Wallace), p relative to center. Returns [0,1].
float ano_ui_ref_shadow(const float p[2], const float half[2], float corner, float sigma);

// Resolved paint color at (px,py) modulated by base. NONE returns base.
// Out-of-range ref fails CLOSED (transparent). GPU twin: ui_paint_eval.
void ano_ui_ref_paint(const AnoUiScene *s, uint32_t paintRef, float px, float py,
                      const float base[4], float out[4]);

// Premultiplied contribution of one prim over [px,px+1)x[py,py+1), clip + paint applied.
// IMAGE/GLYPHS evaluate to zero here.
void ano_ui_ref_shade(const AnoUiScene *s, uint32_t prim, float px, float py, float out[4]);

// Painter's-order evaluation of the whole scene at one pixel (premultiplied linear).
void ano_ui_ref_eval(const AnoUiScene *s, float px, float py, float out[4]);


/* Tile Lists */

// Per-tile prim lists (ui-render.md §3.7): CPU-coarse stage, compose cadence.
// GPU walks only prims touching each 8x8 tile. Solid high bit = full coverage (skip SDF).
// Glyphs are NOT tiled here.

#define ANO_UI_TILE_PX          8u          // tile edge, matches the 8x8 compute workgroup
#define ANO_UI_ENTRY_SOLID      0x80000000u // tile entry: prim fully covers the tile
#define ANO_UI_ENTRY_INDEX_MASK 0x7FFFFFFFu

// Padded pixel AABB of one prim (half + 1px AA; SHADOW +3*sigma + 1px). Identity-inv only (v0).
void ano_ui_prim_aabb(const AnoUiPrim *p, float outMin[2], float outMax[2]);

// Dense tile grid (8px tiles, top-left at ox,oy). offsets[t]..offsets[t+1] owns tile t.
// cursor = tilesX*tilesY scratch. Returns entry count. *ok false if offsetsCap/entryCap too small.
uint32_t ano_ui_tile_build(const AnoUiScene *s, int32_t ox, int32_t oy,
                           uint32_t tilesX, uint32_t tilesY,
                           uint32_t *offsets, uint32_t offsetsCap,
                           uint32_t *entries, uint32_t entryCap,
                           uint32_t *cursor, bool *ok);

// Painter's-order eval through the tile grid. Matches ano_ui_ref_eval inside the grid
// (shadow tails outside the AABB cull differ). Glyphs not included.
void ano_ui_ref_eval_tiled(const AnoUiScene *s, int32_t ox, int32_t oy,
                           uint32_t tilesX, uint32_t tilesY, const uint32_t *offsets,
                           const uint32_t *entries, int32_t px, int32_t py, float out[4]);

#endif
