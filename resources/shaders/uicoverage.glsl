// UI prim ABI twin of include/anoptic_ui.h (AnoUiPrim 96 B, AnoUiClip 48 B,
// AnoUiPaint 48 B, AnoUiStop 32 B — offsets pinned by static_asserts there).
// The prim evaluator ports here from src/ui/ui_raster_ref.c statement for statement
// at build step 4 (docs/ui/ui-render.md §7); until then this include declares the
// data contract only and no shader consumes it.

// Mirrors AnoUiPrimKind.
const uint UI_RRECT  = 0u;
const uint UI_SHADOW = 1u;
const uint UI_IMAGE  = 2u;
const uint UI_PATH   = 3u;
const uint UI_GLYPHS = 4u;

// Mirrors the flag/blend constants.
const uint UI_BLEND_OVER = 0x0u;
const uint UI_BLEND_ADD  = 0x1u;
const uint UI_BLEND_MASK = 0x3u;
const uint UI_FLAG_INNER = 0x4u;
const uint UI_REF_NONE   = 0xFFFFFFFFu;

// Mirrors AnoUiPrim (96 B).
struct UiPrim {
    vec4  inv;      // 2x2 pixel->prim inverse, rows
    vec2  origin;   // prim center, overlay px, y-down
    uint  kind;
    uint  flags;
    vec2  halfExt;
    vec2  param;    // [0]: border width | sigma | lod
    vec4  radii;    // per-corner (tl, tr, br, bl)
    vec4  color;    // premultiplied linear; tint for IMAGE/GLYPHS
    uint  paintRef;
    uint  clipRef;
    uint  aux0;     // texIndex | curveOffset | glyph first
    uint  aux1;     // curveCount | glyph count
};

// Mirrors AnoUiClip (48 B). rrHalf.x < 0 = no rounded term.
struct UiClip {
    vec4 rect;      // minX, minY, maxX, maxY
    vec2 rrCenter;
    vec2 rrHalf;
    vec4 rrRadii;
};

// Mirrors AnoUiPaint (48 B) + AnoUiStop (32 B); evaluated from step 6 on.
struct UiPaint {
    uint kind;
    uint stopFirst;
    uint stopCount;
    uint flags;
    vec4 xform01;   // 2x3 pixel->gradient space, rows packed
    vec4 xform2;    // z,w padding
};

struct UiStop {
    vec4 color;     // premultiplied linear
    vec4 t;         // x = t, yzw padding
};
