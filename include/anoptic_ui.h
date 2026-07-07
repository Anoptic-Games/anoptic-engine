/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic UI API
//
// Primitive ABI and pure builder verbs for the GPU UI overlay lane. Design of
// record: docs/ui/ui-render.md. Layout, styling decisions, and hit-testing live
// in caller code (the logic thread); this header only packs primitives.
//
// Threading: every function is pure over caller memory — any thread, no
// allocation, no module state. Coordinates are overlay pixels, y-down, origin
// top-left (the text-lane convention). Colors are premultiplied linear RGBA.

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
#define ANO_UI_BLEND_ADD  0x1u // rgb-additive (glow); coverage does not occlude
#define ANO_UI_BLEND_MASK 0x3u
// flag bits [2+].
#define ANO_UI_FLAG_INNER 0x4u // SHADOW: inner shadow (blur of the complement, masked inside)
// bits 3+ reserved: nine-slice, pixel-snap hints.

// paintRef/clipRef sentinel: none. Doubles as the builder's table-full error return.
#define ANO_UI_REF_NONE 0xFFFFFFFFu

// ---------------------------------------------------------------------------------------------
// The primitive GPU ABI, one std430 SSBO element per prim (offsets 0/16/24/28/32/40/
// 48/64/80/84/88/92, stride 96, GLSL twin in resources/shaders/uicoverage.glsl).
//   inv    : 2x2 pixel->prim inverse as rows, applied to (pixel - origin). Builders
//            emit identity; rotation folds here later without an ABI change.
//   origin : prim center, overlay pixels, y-down.
//   half   : half extents in prim space. SHADOW prims cull with a +3*sigma pad.
//   param  : kind-specific ([0]: border width | sigma | lod).
//   radii  : per-corner radii (tl, tr, br, bl), pre-clamped by the builder so
//            adjacent corners never overlap (the CSS rule).
//   color  : premultiplied linear RGBA; tint multiplier for IMAGE/GLYPHS; the fill
//            when paintRef == ANO_UI_REF_NONE.

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

// Gradient paint ABI, frozen now, evaluated from build step 6 on (ui-render.md §7).
// No builder verbs exist yet; prims reference ANO_UI_REF_NONE until then.
typedef struct AnoUiPaint {
    uint32_t kind;      // linear/radial/conic (constants land with step 6)
    uint32_t stopFirst;
    uint32_t stopCount;
    uint32_t flags;
    float    xform[6];  // 2x3 pixel->gradient space
    float    pad[2];
} AnoUiPaint;

typedef struct AnoUiStop {
    float color[4];     // premultiplied linear
    float t;
    float pad[3];
} AnoUiStop;

static_assert(sizeof(AnoUiPaint) == 48 && sizeof(AnoUiStop) == 32, "GPU ABI: paint tables");

// ---------------------------------------------------------------------------------------------
// Builder: packs prims and side tables into caller arrays. Never allocates; a full
// array makes the verb return ANO_UI_REF_NONE and change nothing. Emission order IS
// paint order (later prims render on top).

typedef struct AnoUiBuilder {
    AnoUiPrim  *prims;  uint32_t primCap;  uint32_t primCount;
    AnoUiClip  *clips;  uint32_t clipCap;  uint32_t clipCount;
    AnoUiPaint *paints; uint32_t paintCap; uint32_t paintCount;
    AnoUiStop  *stops;  uint32_t stopCap;  uint32_t stopCount;
} AnoUiBuilder;

// Zeroes counts and binds the caller arrays. Any table may be NULL with cap 0.
void ano_ui_builder_init(AnoUiBuilder *b,
                         AnoUiPrim *prims, uint32_t primCap,
                         AnoUiClip *clips, uint32_t clipCap,
                         AnoUiPaint *paints, uint32_t paintCap,
                         AnoUiStop *stops, uint32_t stopCap);

// Rounded rect from a min/max box. radii = per-corner (tl, tr, br, bl), clamped to
// non-negative and scaled down together if adjacent corners would overlap.
// borderWidth 0 fills; > 0 draws a ring of that width inside the boundary.
// Returns the prim index, ANO_UI_REF_NONE when full.
uint32_t ano_ui_rrect(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], const float color[4], float borderWidth,
                      uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// Gaussian shadow of the rrect (uniform cornerRadius). sigma is clamped to >= 1e-3;
// below ~0.5 px it reads as a hard edge. ANO_UI_FLAG_INNER selects an inner shadow;
// ANO_UI_BLEND_ADD turns the same math into a glow.
uint32_t ano_ui_shadow(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                       float cornerRadius, float sigma, const float color[4],
                       uint32_t clipRef, uint32_t flags);

// Textured quad masked by the rrect. The full texture maps to the box (uv 0..1);
// tint multiplies premultiplied components. lod is the explicit mip (compute lane
// has no implicit derivatives).
uint32_t ano_ui_image(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                      const float radii[4], uint32_t texIndex, float lod,
                      const float tint[4], uint32_t clipRef, uint32_t flags);

// Filled path covering curve stream entries [curveOffset, curveOffset+curveCount).
// bbox is the conservative pixel bounds used for culling. Curve baking machinery
// lands with build step 6; the verb only packs the reference.
uint32_t ano_ui_path(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                     uint32_t curveOffset, uint32_t curveCount, const float color[4],
                     uint32_t paintRef, uint32_t clipRef, uint32_t flags);

// Glyph range [first, first+count) of the frame's AnoGlyphInstance array, z-ordered
// with the surrounding prims. bbox is the shaped text's conservative pixel bounds.
uint32_t ano_ui_glyphs(AnoUiBuilder *b, const float bboxMin[2], const float bboxMax[2],
                       uint32_t first, uint32_t count, const float tint[4],
                       uint32_t clipRef, uint32_t flags);

// Clip entry: rect always; pass rrMin == NULL for a rect-only clip, else the rounded
// term's box + per-corner radii (same clamp rule as ano_ui_rrect). Returns the clip
// index for AnoUiPrim.clipRef, ANO_UI_REF_NONE when full. Nested clips are resolved
// by the CALLER (intersect rects; innermost rounded term wins) — one entry per prim.
uint32_t ano_ui_clip(AnoUiBuilder *b, const float rectMin[2], const float rectMax[2],
                     const float rrMin[2], const float rrMax[2], const float rrRadii[4]);

// Standing demo scene, the GPU self-test target (ui-render.md §7 step 4): deterministic,
// reference-evaluable prims only (no IMAGE/PATH/GLYPHS). Needs caps >= 16 prims / 4
// clips. The render-side demo compose and the offline screenshot compare both build
// exactly this; keep it bitwise-stable.
void ano_ui_demo_scene(AnoUiBuilder *b, float originX, float originY);

// ---------------------------------------------------------------------------------------------
// Reference evaluator (validation): scalar mirror of the GPU prim math, exported so
// tests and the GPU self-test harness drive one implementation (the
// text_raster_ref.c discipline). Implementation: src/ui/ui_raster_ref.c.

typedef struct AnoUiScene {
    const AnoUiPrim  *prims;  uint32_t primCount;
    const AnoUiClip  *clips;  uint32_t clipCount;
    const AnoUiPaint *paints; uint32_t paintCount;
    const AnoUiStop  *stops;  uint32_t stopCount;
} AnoUiScene;

// The builder's current contents as a scene view (no copy).
static inline AnoUiScene ano_ui_scene(const AnoUiBuilder *b)
{
    return (AnoUiScene){ b->prims, b->primCount, b->clips, b->clipCount,
                         b->paints, b->paintCount, b->stops, b->stopCount };
}

// Exact signed distance to the prim-space rounded box (per-corner radii, y-down
// quadrant select). p is relative to the box center.
float ano_ui_ref_sd_rrect(const float p[2], const float half[2], const float radii[4]);

// Closed-form Gaussian-blurred rounded box (Wallace: erf along x, 4-sample Gaussian
// quadrature along y), evaluated at p relative to the box center. Returns [0,1].
float ano_ui_ref_shadow(const float p[2], const float half[2], float corner, float sigma);

// Premultiplied contribution of one prim over the unit pixel window [px,px+1)x[py,py+1),
// clip applied. PATH/GLYPHS evaluate to zero here (they validate in their own lanes).
void ano_ui_ref_shade(const AnoUiScene *s, uint32_t prim, float px, float py, float out[4]);

// Painter's-order evaluation of the whole scene at one pixel (premultiplied linear,
// the register blend loop the GPU lane mirrors).
void ano_ui_ref_eval(const AnoUiScene *s, float px, float py, float out[4]);

#endif
