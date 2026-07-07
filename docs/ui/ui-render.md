# ui-render.md — vector UI layer: scouting report

Scouting pass for the general UI rendering layer (panels, buttons, menus, layering, images,
shadows/glow), tested against the current renderer and against the 2024-2026 state of the art
in GPU vector graphics. Verdict up front: the text lane should not merely inspire the UI lane —
it should become it. The Scanline Sweeper's coverage engine (`textcoverage.glsl`) is already a
general fill rasterizer over monotone quadratic outlines; nothing in `curve_area`/`window_sum`
is glyph-specific, holes and annular borders fall out of the signed-area sum natively (glyph
counters render that way today), and the per-instance clamp already survives self-overlap. The
missing pieces are runtime-submitted geometry, a typed-primitive layer for the shapes UI is
actually made of, closed-form shadows/glow, paints (gradients/textures), a clip/z model, and a
scaling story past the single-bbox dispatch. Every structural prerequisite exists: the async
overlay lane, the composite insertion point, the id-keyed block bridge protocol, bindless
textures, and the input back-channel. The field survey (§2) confirms this shape independently:
production UI renderers converged on exactly "typed analytic primitives + painter's order +
CPU-side layout", and the leading research renderer (Vello) pivoted in 2025 toward CPU-coarse /
GPU-fine — the division of labor this renderer already has.

## 1. Requirements and givens

From the stated intent: layout, dimensions, positioning, styling computed CPU-side on the game
logic thread; element specs + layering order submitted to the render thread; pushed to GPU
buffers; rendered on-device. Vector-based draws for crisp AA and flawless scaling; provisions
for transparency, texturing, shadows, glow. No hard constraints beyond the engine's standing
ones: C23 + GLSL in-house, no heavyweight deps, feature-gated fallbacks, analytic math over
brute force where it wins.

Givens worth pinning because they shape everything (all anchors verified this pass):

- The overlay lane exists and is UI-shaped already: full-res premultiplied `R8G8B8A8_UNORM`
  overlay ×3 (`text_raster.c:771`), async compute CB submitted first with no waits
  (`vulkanMaster.c:244`, `text_raster.c:979-1005`), graphics waits `textTimeline` at
  FRAGMENT (`frame/submit.c:22-102`), in-frame fallback (`frame/record.c:262`), one
  premultiplied composite draw as the last thing in the swapchain pass
  (`frame/record_views.c:317-320`). `overlay.frag:3` already calls itself the "Text/UI
  overlay composite". `PIPELINE_UI` is reserved (`components.h:19`).
- The bridge protocol for id-keyed replaceable blocks is proven: `RCMD_TEXT_SET/CLEAR`
  copy-at-submit, `bulk_owned`, registry adoption, compose-into-pending + version bump,
  per-slot post-fence refresh (`anoptic_render.h:230-231`, `bridge/apply.c:320-327`,
  `text_raster.c:127-234`). SET is full-replace, so drops are safe to skip.
- Texturing is solved: descriptor-indexed texture array, ≤4096, UPDATE_AFTER_BIND,
  PARTIALLY_BOUND (`instance/layouts.c:341-366`); UI prims carry indices.
- Input/hit-testing is solved at the boundary: cursor position lives on both threads,
  `ANO_INPUT_CHAR` exists "for typed UI" (`anoptic_render.h:463`), the logic thread drains
  events at tick rate (`main.c:258-329`). Hit-testing is rect/rrect tests on the logic
  thread against the same tree it laid out — the universal production split (Chromium,
  Slate, Flutter all keep semantic hit-testing off the renderer). No render-side work.
- Compute-queue slack: text submits with zero waits and hiz/lc run at frame edges; a UI
  raster rides the same idle window and is awaited only at composite's fragment stage.
- Frame budget context (release, 2560×1368, RTX 3090): total ≈ 2.0 ms, composite ≈
  0.09-0.14 ms; the async text raster itself is below session noise. Middleware ships
  entire game UIs at 1-1.6 ms on 2013 consoles; our target class is <0.3 ms, async-hidden.

## 2. The design space

Six schools, each with a verdict against this renderer. Sources in §9.

### 2.1 CPU tessellation (Dear ImGui, NanoVG, Qt Quick)

Layout code emits triangles (rects, glyph quads, AA fringe strips); GPU replays a dumb vertex
stream, one texture atlas, scissor per command. ImGui regenerates everything per frame and its
whole model is "1 draw cmd ≈ 1 draw call"; Qt batches opaque front-to-back + alpha
back-to-front and wants <10 batches. Strengths: trivially debuggable, predictable, the floor
every engine can reach. Weaknesses for us: AA quality is fringe-approximate and breaks under
scale (re-tessellation), vectors are faked, shadows are 9-patch textures, and the CPU re-emits
geometry every frame — the exact opposite of the bridge's retained-block economy. Verdict:
reject as the architecture, keep as the mental floor; our block registry + compose already
beats its data flow.

### 2.2 Analytic primitive instancing, fragment school (GPUI/Zed, vger)

The modern production answer for editor-grade UI. GPUI renders exactly five primitive classes —
shadows, rectangles, glyphs, icons, images — as one instanced draw per class in painter's
order; rounded rects are per-pixel SDFs; drop shadows are Evan Wallace's closed-form erf
technique verbatim; arbitrary paths are simply not supported ("most 2D graphical interfaces
break down into a few basic elements"). vger is the same idea with more prim kinds. Strengths:
tiny CPU cost, crisp analytic AA at any scale, hardware blending gives painter's order for
free, ~2 pipelines. Weaknesses: no vector paths (icons become textures), overdraw is real
fill-rate (each translucent quad reads/writes the target), fragment-lane only, and it would
live beside our compute text lane as a second, differently-flavored system with its own
z-interleaving problem against text. Verdict: the primitive taxonomy and the shadow math are
exactly right — steal both — but implement them inside the compute lane we already have rather
than as a parallel fragment system.

### 2.3 Fully-GPU compute pipelines (Vello classic; RAVG → MPVG → Li lineage)

Vello classic: scene encoding → GPU flattening (Euler-spiral stroke expansion, one segment per
thread) → binning → coarse raster into 16×16-tile per-tile command lists (PTCL) with backdrop
winding and solid-tile shortcuts → fine raster blending in registers. The academic lineage
established the key invariants: per-cell self-contained winding (RAVG), GPU-built accelerators
+ local-memory blending + front-to-back early termination worth ~33% (MPVG), boundary-vs-span
processing (Li 2016). Costs, from the authors themselves: unpredictable intermediate memory
(the tile/segment allocation problem, retry machinery), a long compute pipeline of a dozen
dispatches, conflation artifacts from coverage AA (still on their future-work list in 2026),
and complexity far past what a UI workload needs. Verdict: do not build a general scene
compiler. Steal three ideas: per-tile command lists, per-tile backdrop/solid classification
(the interior fast path), and front-to-back early-out. Our workload (hundreds-to-thousands of
prims, mostly axis-aligned) lets the CPU build the tile lists at UI-change cadence, skipping
Vello's entire GPU binning half.

### 2.4 CPU-coarse / GPU-fine hybrid (Vello sparse strips, 2025-2026)

Linebender's own pivot, motivated by classic Vello's memory unpredictability and downlevel-GPU
exclusion: CPU does flatten/stroke/tile/sort/coarse into height-4 "sparse strips" (u8 coverage
columns for boundaries + implicit winding-derived spans for interiors); GPU is plain
vertex/fragment draws consuming strips. vello_cpu beats Skia/Cairo at scale; vello_hybrid is
beta as of 2026 Q1. Verdict: this is the strongest external validation of the recommended
shape. Our renderer already holds the two ends (CPU logic thread doing layout; GPU per-pixel
analytic coverage); the strip/coarse stage maps onto "CPU composes per-tile prim lists at
UI-change cadence". We keep one advantage over sparse strips: our fine stage evaluates
coverage analytically per pixel from curves, so we never bake or upload coverage textures and
re-rasterize nothing on scroll/scale of the same block content.

### 2.5 Single-pass interlocked path rendering (Rive)

Pixel-local storage / rasterization-order attachment access / fragment interlock / atomics —
five modes plus vendor workarounds — to get order-correct src-over of thousands of overlapping
animated paths in one pass with no offscreens. Verdict: solves a problem we do not have. A
z-sorted UI prim list blended in registers inside one compute thread has perfect ordering by
construction, zero extensions, zero modes. Revisit only if Lottie-class vector animation
becomes UI content. Their 2025 "feathering" is worth remembering: Gaussian-integral coverage
shaping as a vector property (a 1D erf LUT + edge distances) rather than blur passes — the
same closed-form-blur philosophy as §3.4.

### 2.6 Stencil-then-cover + MSAA (NV_path_rendering, Impeller, Skia Graphite MSAA path)

Fixed-function winding accumulation in stencil, then cover; AA via MSAA. Impeller moved to
this in 2024 and its real lesson is elsewhere: a fixed, build-time-enumerated pipeline set
(<50 shaders) and full per-frame redraw ship 60-120 Hz UI on phones; runtime shader growth is
how UI renderers die (Skia's jank decade). Graphite's lessons likewise: painter's-index-as-
depth for opaque reordering, and clip-as-data so pipeline state never depends on the clip
stack. Verdict: reject the raster technique (serial per path, MSAA memory against our ×1
overlay, no analytic AA), adopt the two architectural rules. The Slug patent (US 10,373,352,
banded winding-number glyph eval) was disclaimed into the public domain 2026-03-17, so that
whole family is now legally open — noted for completeness; our signed-area lineage
(Manson-Schaefer exact-coverage school via the sweeper paper) is different math anyway and
strictly better suited to windowed box-filter coverage.

## 3. Recommended architecture: one coverage lane, typed prims

One prim stream, painter's order, evaluated per pixel in the existing tile-gather compute
dispatch, into the existing overlay, composited by the existing draw. Text becomes one prim
kind among several. Everything below is a design sketch to be hardened by the build sequence
(§7), not a spec.

### 3.1 Layering and z model

Painter index = (block layer u8, block creation seq, prim index within block), exactly the
Graphite/Slate model flattened CPU-side. Blocks arrive over the bridge with an explicit layer
byte; the render-side compose sorts blocks by (layer, seq) and concatenates prim arrays; the
GPU walks each pixel's prims in stream order and src-over-blends in registers
(`acc = src + acc*(1-src.a)` — later prim lands on top, the ordering `textraster.comp:62-64`
already implements). No depth buffer, no sort keys on GPU, no interlock. Per-prim blend mode
(over, additive for glow, multiply for tint) is a flags nibble switching the register blend —
free in compute, impossible to do this cheaply with fixed-function.

### 3.2 Primitive taxonomy and ABI draft

Kinds, chosen against GPUI's five plus what their model can't do (true vectors, gradients):

- UI_RRECT: axis-aligned rounded rect, per-corner radii, solid or paint fill; border via
  width param (annulus = |d+w/2| style band, or two-contour outline — see §3.3).
- UI_SHADOW: rrect + sigma + color, Wallace closed form (§3.4); flags: inner, glow(additive).
- UI_IMAGE: rrect-clipped textured quad, bindless index, per-prim analytic LOD, optional
  9-slice (aux-packed insets).
- UI_PATH: arbitrary filled outline, monotone quads in the shared curve stream (§3.3) —
  icons, decorative curves, plots.
- UI_GLYPHS: a {first,count} range of AnoGlyphInstance — text runs z-interleaved with panels;
  evaluation is the existing `shade_window`.

Draft ABI, 96 B std430 (six vec4 rows; offsets pinned by static_asserts like the 48 B glyph
ABI, GLSL twin in a shared include):

    // std430, 96 B
    typedef struct AnoUiPrim {
        float    inv[4];      // 2x2 pixel->prim inverse (identity for UI-space rects)
        float    origin[2];   // prim origin, overlay pixels
        uint32_t kind;        // UI_RRECT | UI_SHADOW | UI_IMAGE | UI_PATH | UI_GLYPHS
        uint32_t flags;       // blend mode, inner/glow, 9-slice, pixel-snap hints
        float    half[2];     // half-extents, prim space
        float    param[2];    // sigma | border width | lod | (kind-specific)
        float    radii[4];    // per-corner radii (tl, tr, br, bl)
        float    color[4];    // premultiplied; base factor when a paint is referenced
        uint32_t paintRef;    // paint table index | UI_PAINT_SOLID
        uint32_t clipRef;     // clip table index | UI_CLIP_NONE
        uint32_t aux0;        // texIndex | curveOffset | glyph first
        uint32_t aux1;        // 9-slice pack | curveCount | glyph count
    } AnoUiPrim;

Side tables composed per frame slot with the prims: paint table (gradient kind, 2×3 transform,
stop offset/count into a stop array), clip table (§3.6), curve stream region for UI_PATH
outlines, and the existing glyph instance region. v0 transport: everything memcpy'd into the
per-frame mapped buffer on version bump — the text refresh protocol verbatim.

### 3.3 Coverage evaluation: two evaluators, one standard

The design thesis stays the sweeper's: the pixel is an integral, not a sample point.

- UI_PATH (and optionally rrect boundaries): the existing `curve_area` walk over window-local
  monotone quads. Generalizes for free: contours with opposite winding are holes (signed
  areas cancel — glyph counters prove it in production); a border ring is outer contour +
  reversed inner contour, one prim; the per-prim clamp to [0,1] survives self-overlap (the
  Geist lesson, `font-render.md` §6.1). Strokes become fills CPU-side at prim build — for UI
  this is almost always trivial (rrect borders are exact ring contours; polyline strokes are
  quad-per-segment expansion; genuinely curved strokes can adopt the closed-form arc/offset
  machinery of Levien-Uguray 2024 later if ever needed). Even-odd assets are normalized to
  nonzero at import (orientation fix), the standard bake step.
- UI_RRECT/UI_IMAGE/UI_SHADOW: closed-form per-pixel evaluation. For an axis-aligned rrect the
  per-axis box-filter coverage of each straight edge is exact (clamp of the signed offset over
  the window — the same trapezoid math `curve_area` uses), and only the corner-arc quadrant
  falls back to the SDF-with-linear-ramp approximation (IQ's 4-radius rounded-box distance),
  where curvature visually masks the ~half-pixel error. Long straight panel edges therefore AA
  bit-consistently with text — the place where two different AA families would visibly clash.

If exactness ever matters at corners too, rrects can be lowered to outlines and routed through
`curve_area`: circular quarter-arcs as monotone quadratics cost, per 90° corner (max radial
error as a fraction of r, derived this pass): 1 segment 6.07%, 2 segments 0.31%, 3 segments
0.056%, 4 segments 0.025%. Two segments keeps the error under 0.1 px up to r≈32 px, three up
to r≈180 px. An rrect outline is then 4 lines + 8-12 quads ≈ one average glyph (17.4 segments)
per pixel — fine for boundary tiles, ruinous for interior pixels, which is why §3.7's interior
classification is load-bearing if this path is taken. Default: closed-form rrect; outline
lowering as a quality escape hatch.

A property worth stating because it is unusual: two shapes sharing an exact edge produce
coverage that sums to exactly 1 in every window (signed areas are additive), so same-color
abutting panels are seamless by construction. Differing colors still show the standard
conflation seam (§5.3).

### 3.4 Shadows and glow, closed form

The centerpiece of "mathematical elegance meets art direction", and the field agrees on the
math (GPUI ships exactly this; CC0 source):

- 1D: the Gaussian-blurred box edge is an erf difference. Wallace's shader form:
  `integral = 0.5 + 0.5*erf(q * (sqrt(0.5)/sigma))`; a blurred axis-aligned box is the product
  of two such 1D factors — a plain rect shadow is ~4 erf evaluations, closed form, exact.
- Rounded: closed form along x (erf across the corner-modulated width), numeric along y —
  4 Gaussian-weighted samples clamped to ±3σ. Total ≈ a dozen erf/exp evaluations per pixel
  inside the padded bbox. Wallace's rational erf approximation is 4 ALU ops.
- Glow = the same evaluator with an additive blend flag (soft alpha shape, add instead of
  over); inner shadow = sign flip on the box query. Prim bbox = rrect bbox + 3σ padding, so
  the tile cull contains the cost automatically.
- Large σ relative to the rect: Wallace's separation slightly squares off; Levien's 2020
  refinement (distance-field formulation: corner radius inflated to sqrt(r_c² + 1.25·r_b²),
  superellipse exponent growth, erf7 sigmoid) fixes exactly that regime and is the same cost
  class. Adopt Wallace v0, keep Levien's form as the large-σ upgrade.
- Scope limit, stated honestly: this is axis-aligned rrect-family only. Shadows of rotated
  prims or arbitrary UI_PATH shapes have no closed form (Gaussian blur of an SDF is not a
  function of the SDF — the sigmoid-of-distance shortcut is visibly wrong near concavities).
  Escalation path if art direction demands it: cached mini-raster + separable blur as a
  parameter-keyed render task (the WebRender box-shadow model), or Rive-style per-edge
  Gaussian-integral feathering. Deferred; rrect shadows cover the overwhelming majority of
  UI art direction (GPUI ships nothing else).

### 3.5 Paints: gradients, images, dither

- Gradients: linear/radial/conic evaluated analytically from the paint table (conic = atan2
  with a half-texel seam guard), stops interpolated in-shader over a small sorted stop array
  (≤8 typical; a ramp LUT texture only if stop counts grow). Interpolation space: premultiplied
  linear to match the lane (§3.8), optional OKLab flag later for designer ramps.
- Banding: the overlay is 8-bit; dark large-radius gradients and shadow falloffs will band.
  Mitigation is one line: interleaved-gradient-noise (or blue-noise) dither at the final
  register→imageStore quantization, amplitude 1 LSB. Keep an `R16G16B16A16_SFLOAT` overlay
  variant behind a knob if dither ever proves insufficient (doubles overlay VRAM 42→84 MiB).
- Images: bindless index + analytic LOD (UI transforms are known CPU-side; compute lane has
  no implicit derivatives — `textureLod` with the prim's precomputed level; UI is mostly
  1:1 or gentle scaling). Premultiplied-alpha decode at texture bake for correct filtering
  (the classic atlas-bleed rule). 9-slice = uv remap from aux insets, trivial in the
  evaluator. Rounded-corner image masking = rrect coverage × sample, free by construction.

### 3.6 Clipping: data, never state

Per-prim `clipRef` into a small clip table; each entry is one axis-aligned rect + at most one
rrect (center/half/radii). The CPU compose flattens the block's clip stack: intersecting
axis-aligned rects collapse into one rect; the innermost rounded clip survives as the rrect
term; deeper rounded nesting falls back to the outermost rrect + rects (WebRender's ClipNode
model, plus its elimination rule — drop clips that provably contain the prim). GPU applies
clips as coverage multiplies; the rect term can instead clamp the integration window exactly.
This keeps clips out of pipeline state entirely (the Graphite rule; the thing that fragments
batching in Qt/Slate/Unity), costs ~10 ALU when present, and nests to any practical UI depth.

### 3.7 Tiles: the scaling ladder

The economics: a full-screen 4-deep translucent stack at 2560×1368 is ~14 M prim-evaluations
of ~20-40 ALU each — trivial for the async queue. The danger cases are (a) many prims × many
tiles in the brute gather (today's chunk scan touches every prim per tile), and (b) interior
pixels of outline-evaluated shapes. The ladder, each step gated on measurement:

1. v0 (build sequence steps 1-5): today's machinery unchanged — union-bbox bounded dispatch
   (`text_raster.c:919-947`), CHUNK-64 shared-memory cull, brute per-tile scan. Correct at
   demo scale; the text lane proved the cost class.
2. Per-tile prim lists, CPU-built at compose cadence (the load-bearing step): when a block
   changes, the compose walks prims, appends (kind,index) handles to the 8×8-tile grid
   (54,720 tiles at 2560×1368; axis-aligned prims rasterize to tile ranges in closed form,
   paths via segment-bbox conservative rasterization). Because composition happens only on
   UI change — never per frame — this is the sparse-strips CPU-coarse stage at retained-mode
   cost: zero CPU when the UI is idle. The dispatch goes indirect over non-empty tiles.
   This step also unifies text and UI z-order for real (glyph runs interleave in the lists).
3. Same pass, two Vello-school refinements: per-tile interior classification (a prim whose
   coverage over the tile is provably 1 becomes a "solid" list entry — no curve walk, no SDF;
   the backdrop idea at tile granularity) and topmost-opaque truncation (drop everything
   beneath an opaque tile-covering prim at list-build time — MPVG's early termination, done
   free on the CPU). Optional GPU-side polish: walk lists front-to-back with under-blending
   and saturate-exit at α≈1.
4. Only if profiling ever demands it (dense dynamic vector content): move binning to a GPU
   pass. Nothing in the ABI changes; the lists just get built by a shader.

### 3.8 Lane integration and color policy

- One dispatch, one overlay: extend the raster CB to evaluate prims and glyphs into the same
  overlay image (v0: prims loop before the OSD glyph loop inside the same shader — UI under
  all overlay text until step-2 lists interleave them properly; the OSD is top-most anyway).
  Same timeline, same submit slot, same in-frame fallback, same composite draw — zero new
  sync objects, zero new composite work. Gates: `ANO_FORCE_NO_UI`, plus the established
  non-fatal downgrade shape (init returns true, failures clear the gate and log).
- Color: the lane stays premultiplied linear (the glyph ABI is already "premultiplied linear
  RGBA", the composite blend is linear-correct onto the sRGB swapchain). UI blending in
  linear reads slightly heavier/thinner than design-tool (sRGB-space) blending — the browser
  world blends in device space; ours is a from-scratch aesthetic, text already tuned its
  gamma step once (`font-render.md` §6.7), and consistency inside one overlay beats matching
  Figma. Decide once, document, expose a single tuning session like text did. Dither (§3.5)
  covers the 8-bit-linear banding cost.
- HDR future: the overlay is exactly the "SDR UI in its own target" pattern HDR and
  frame-generation pipelines want (FSR3 UI composition, console paper-white guidance).
  When an HDR swapchain lands, the composite draw gains a paper-white multiply (~200 nits
  default, user-calibrated); nothing else changes. Never blend in PQ space.

### 3.9 Bridge protocol and API boundary

Clone the text verbs: `RCMD_UI_SET`/`RCMD_UI_CLEAR`, `ano_render_ui_set(bridge, ui_id, blob)`
packing header + prim array + side tables in one allocation, `bulk_owned`, registry adoption
(`ANO_UI_MAX_BLOCKS` ≈ 64), full-replace semantics, compose + version bump + per-slot refresh.
Block header carries the layer byte and a scroll offset (applied CPU-side at compose v0; a
GPU-side per-block offset later makes scrolling re-compose-free). Logic side gets a thin pure
prim-builder API in `include/anoptic_ui.h` (push_rrect/push_shadow/push_image/push_text/
push_path into a caller buffer, mirroring `ano_text_shape`'s purity — callable any thread,
zero allocation); widget/layout/hit-test logic lives above it in game code and is explicitly
not the renderer's business. Text-on-UI: the builder calls the existing shaper and emits a
UI_GLYPHS prim referencing the shaped range, so labels ride the same block and the same z.

### 3.10 The world-space dividend

The textworld precedent generalizes: the prim evaluator lives in a shared include (the
`textcoverage.glsl` pattern), so a fragment twin on a quad in the additive pass renders the
same prim stream as a diegetic panel in the 3D scene — derivative-window AA, perspective-
correct, holograms/cockpit screens for free. Same data, two consumers, zero drift by
construction. Out of v0 scope; the include split is designed in from step 1.

## 4. Reuse inventory

What the UI lane inherits without modification (anchors current as of this pass):

- Async lane skeleton: submit-first-no-waits CB + timeline + FRAGMENT wait + host-signal
  failure fallback + in-frame fallback recording site (`text_raster.c:890-1005`,
  `frame/submit.c`, `frame/record.c:262`).
- Overlay lifecycle incl. resize: `instance/attachments.c:247-248`, `swapchain.c:307-308,
  394-395`.
- Composite insertion: `frame/record_views.c:317-320` — unchanged.
- Bounded dispatch + pending/version/refresh protocol: `text_raster.c:94-144, 222-234`.
- Tile-gather shader shape incl. shared-memory cull and register blending:
  `textraster.comp` — becomes the prim uber-loop.
- Block registry + bridge verbs: `text_raster.c:170-220`, `bridge/apply.c:320-327`,
  `render_bridge/ano_render_bridge.c:130-154`.
- Bindless textures (`instance/layouts.c:341-366`), staged one-shot uploads
  (`instance/commands.c:184-211`), persistently-mapped frame rings (`commands.c:42-68`),
  descriptor pool sizing site (`instance/descriptors.c:25-63`), feature-gate parsing site
  (`vulkanMaster.c:352-382`).
- CPU reference-rasterizer culture: `src/text/text_raster_ref.c` mirrored statement-for-
  statement by the shader — the UI evaluator gets the same twin (§7 step 2).

## 5. Friction points

1. Interior-pixel economics. Outline evaluation of big filled shapes costs ~an average glyph
   per pixel; closed-form rrects mostly dodge it, UI_PATH does not. Mitigation: §3.7 step 2-3
   (interior classification + opaque truncation) before any path-heavy art ships. Decided now
   so nobody ships a full-screen path fill on the brute scan.
2. 8-bit overlay banding and low-alpha premultiplied precision. Mitigation: 1-LSB dither at
   store; 16F overlay knob (2× VRAM) as escape hatch. Cheap on day one, miserable later.
3. Conflation seams. Abutting different-colored shapes leak backdrop ∝ cov·(1−cov) along the
   shared edge (max ~25% at the midpoint) — inherent to coverage-based AA everywhere (Vello
   still lists it as future work). Mitigations, in order: layout snaps shared edges to pixel
   boundaries (coverage ∈ {0,1}, seam vanishes); panels extend behind children; same-color
   abutment is exact already (§3.3). Not engine-fixable in general; write the layout guidance.
4. Even-odd and self-intersecting path imports. The engine renders nonzero-normalized fills;
   the importer must orientation-fix (and optionally planarize) SVG-origin icons. The clamp
   handles residual self-overlap the way it handles Geist's `H 8 @`.
5. Compute-lane texture sampling has no implicit derivatives or aniso. Analytic LOD covers
   flat UI; heavily minified or perspective UI imagery would need the fragment twin (§3.10).
   Accept and document.
6. Blend-space taste. Linear-premultiplied accumulation reads different from design-tool sRGB
   blending (thin-stroke weight, gradient midpoints). One deliberate tuning session against
   reference mocks, like text's gamma pass; expose nothing per-prim until forced.
7. Shadow generality. Closed form is axis-aligned rrect only (§3.4). Rotated/path shadows are
   a deliberate v0 gap with a named escalation path (cached blur tasks). Say no early.
8. Registry and buffer caps. ANO_UI_MAX_BLOCKS, prim count per block, curve-stream bytes, and
   the 1 MiB-class frame ring all need caps chosen and asserted at the ABI boundary
   (`ANO_RENDER_TEXT_MAX` precedent); full-replace semantics make overflow behavior benign
   (drop + warn) but the caps must exist before the first consumer.
9. Sync-surface copying. The async lane invariants (submit order, host-signal fallback,
   fence-safe CB reuse, wait-before-signal) are inherited verbatim; each has an in-tree
   precedent but the copying is correctness-critical, as font-render.md §6.4 already warned.
10. Scope discipline. The taxonomy is complete for shipped-game UI (GPUI ships less). The
    predictable creep vectors — arbitrary masks, backdrop blur (needs a scene color pyramid),
    per-prim filters, Lottie-class animation — are all explicit non-goals of v0; each has a
    named future home (§8) so "no" has somewhere to point.

## 6. Performance and memory envelope

Estimates, calibrated against the measured text lane and published numbers; to be replaced by
step-8 measurements.

- Raster cost: text lane in-frame measured +0.15 ms at torture-text scale, async-hidden to
  composite-noise (`font-render.md` §10 steps 6-7); the sweeper paper logs 240 µs for large-
  coverage 1440p text on an RTX 2060. A menu screen (full-screen 3-5 layer stack + text) is
  the same order: tens of M prim-window evaluations, expected well under 0.5 ms on the 3090
  class, fully hidden on the async queue against ~2 ms of graphics. Coherent middleware ships
  entire game UIs in ≤1-1.6 ms on 2013 consoles — our structural budget is comfortable.
- CPU: compose (sort blocks, flatten clips, build tile lists) runs at UI-change cadence only;
  thousands of prims ≈ tens of µs class. Idle UI = one version compare per frame.
- Memory: overlay already paid (≈42 MiB ×3 at 2560×1368). New: per-frame prim ring (4096
  prims × 96 B × 3 ≈ 1.2 MiB), side tables (≤256 KiB ×3), tile lists (54,720 tiles × 8 B
  header + spans, ≈1-2 MiB ×3 when step-2 lands), UI curve stream (tens of KiB). Everything
  host-visible v0; device-local promotion only if profiling asks.
- Latency: logic tick → RCMD_UI_SET → next frame compose + async raster → same-frame
  composite. Identical to text: one logic tick + one frame, no added stages.

## 7. Build sequence

Mirrors the text PoC discipline: offline reference first, GPU inert plumbing, in-frame before
async, self-test gates at every step.

1. ABI + module skeleton: `include/anoptic_ui.h` (AnoUiPrim + builder verbs, static_asserts),
   `src/vulkan_backend/ui_raster.c` stub, `uicoverage.glsl` include split (prim evaluator
   headerless of glyph specifics), gates parsed. Compiles everywhere, does nothing.
   (Done 2026-07-07: 96 B AnoUiPrim / 48 B clip / 48+32 B paint tables, offsets static_
   asserted; builder verbs rrect/shadow/image/path/glyphs/clip in src/ui/ui_build.c —
   pure over caller arrays, full arrays refuse via ANO_UI_REF_NONE with no mutation,
   radii clamped to min(half) per corner (tighter than CSS: the sum rule is then
   implied), sigma floored 1e-3; uicoverage.glsl carries the GLSL struct twins, no
   shader consumes it yet; render side = uiOverlay gate riding textOverlay +
   ANO_FORCE_NO_UI, init/destroy stubs wired in initVulkan/cleanupVulkan. Debug build
   clean. Incidental unblock: ANOSTR_SID expansion exceeds clang's default 256 bracket
   depth — global -fbracket-depth=1024 for clang in the root lists file, surfaced by
   this host's first test build since sidbench landed.)
2. CPU reference evaluator: `ui_raster_ref.c` mirroring the shader statement-for-statement
   (rrect coverage, erf shadow, clip multiply, paints). Oracle: 64×-supersampled CPU
   rasterization of the same prims; RMS gates per kind (the text culture's FreeType-compare,
   self-oracled). This validates the closed forms — especially corner-arc AA error and the
   Wallace 4-sample shadow — before any Vulkan.
   (Done 2026-07-07: src/ui/ui_raster_ref.c — exact 4-radius rrect SDF (y-down quadrant
   select), ramp coverage + ring erosion, Wallace shadow (rational erf, 4-sample
   quadrature, 3-sigma clamp), inner = (1−blur)·insideCov, overlay-space clip multiply
   failing CLOSED on bad refs, painter's src-over + rgb-only ADD. anotest_ui oracles all
   in double: 64×64-supersampled predicates plus closed-form rect truth where fractions
   defeat the 1/64 quantum, and separable-Gaussian-blurred 4× masks for shadows.
   Measured and pinned: straight-edge windows EXACT (0.0 vs analytic oracle; clip rows
   worst 8e-7 — the ramp is the half-plane box filter, as designed); corner-zone error
   confined and bounded — sharp-corner tip 0.120, r=8 arc 0.0375, mixed 0.125, ring
   0.125 at the erosion's inner corners (zone classifier = corner radius + border +
   3.5 px); shadows vs blur truth max 3.4/2.7/4.7 per 255, rms ≤ 1/255 across
   rect σ=2/8 and rrect r=8 σ=4; SDF edge/arc rays d==t to 1e-3, 15213-probe sign sweep
   clean. Suite 19/19 on build.sh 3.)
3. GPU plumbing, visually inert: buffers, descriptor writes, pipeline, empty dispatch wired
   into the text CB path behind `ANO_FORCE_NO_UI`. Validation-clean, frame totals unchanged.
   (Done 2026-07-07: no second pipeline — per §3.8 the prims ride the text raster dispatch,
   so plumbing = per-slot uiFrameBuffer (prim 384 + clip 12 + paint 12 + stop 32 KiB regions,
   256-aligned offsets, host-visible persistently mapped, 440 KiB ×3 ≈ +1.2 MiB observed on
   the gpu allocator), textRasterSetLayout grown 4→8 bindings (4-7 = table regions,
   COMPUTE-only stages), TextRasterPush 24→32 B carrying uiPrimCount/uiClipCount (zero until
   step 5; the shader still declares the 24 B prefix), pool sizing +4 SSBO/frame,
   ano_vk_ui_write_sets hooked onto ano_vk_text_update_sets so resize rebinds ride free.
   Robustness choices: tables are created whenever textOverlay is up — ANO_FORCE_NO_UI pins
   the counts to 0 instead of leaving bindings unwritten, so the step-4 shader's static use
   of bindings 4-7 can never dangle; a failed table creation clears uiOverlay and falls the
   bindings back to the slot's textFrameBuffer (valid SSBO, never read at count 0).
   Hardware-verified on the desktop: debug run validation-clean (0 hits) with the text lane
   fully live, "tables resident (440 KiB x3 slots)" boot line, suite 19/19, release+debug
   builds clean. Frame totals are unchanged by construction (zero new commands recorded;
   cross-session ms vary with desktop load per the standing A/B-within-session rule). Live
   window-resize recheck deferred to the step-4 on-screen pass, where an actual consumer
   makes it observable.)
4. Prim raster in-frame: extend the raster shader with the prim loop (before the glyph loop),
   `ANO_UI_OPAQUE`-style self-test canvas vs the reference evaluator (RMS gate), resize +
   teardown clean. Rrect/shadow/image/solid paints land here.
   (Done 2026-07-07: uicoverage.glsl carries the evaluator, a statement-for-statement port
   of ui_raster_ref.c (sd_rrect, rational erf, 4-sample shadow quadrature, fail-closed clip
   multiply) plus the two GPU-only pieces — UI_IMAGE sampling the bindless array (set 1 on
   the raster pipeline layout; the array's stage flags gained COMPUTE; index is dynamically
   uniform through the shared-mask loop, nonuniformEXT-wrapped anyway) and ui_box_hits tile
   culling (shadow prims pad by 3 sigma). textraster.comp runs the prim chunk-cull loop
   before the glyph loop — same sHit pattern, per-prim ADD/OVER register blend — and the
   dispatch bounds are the union of text and UI AABBs. Standing demo scene
   (src/ui/ui_demo.c, ANO_UI_DEMO, 13 prims + 2 clips + a live-only image prim composed
   once into all three slots): drop shadow, plate, ring, rect-clipped overflow bar, inner
   shadow, capsules, additive button glow, footer wash clipped by the panel's rounded
   silhouette. Self-test: ANO_UI_OPAQUE pins opaque backdrop + full-canvas dispatch + glyph
   skip (flags bit 1); anotest_ui gained --dump/--compare (P6, both quantizers mimicked:
   UNORM8 linear overlay then sRGB encode). Hardware-verified: screenshot vs reference over
   the full 2560×1368 canvas RMS 0.0384/255, max 13/255 on one plate-edge AA pixel — 13 ≈
   12.92 is exactly one linear LSB through the sRGB dark-end slope, i.e. the documented
   UNORM8-overlay envelope (text precedent 0.034/255); live demo composites correctly under
   the OSD/HUD glyphs; two real windowed resize cycles (1600×900, 2100×1150) validation-
   clean with the demo intact — the step-3 deferral closed. Suite 19/19. Observation for
   step 5: ABI colors are linear, so designer-authored sRGB values read brighter on screen —
   add an sRGB-authoring helper alongside the bridge verbs.)
5. Bridge verbs + logic demo: RCMD_UI_SET/CLEAR, registry, compose/version/refresh; main.c
   demo — a menu panel with hover/click buttons (logic-side hit-testing over the input
   back-channel), a HUD bar with glyph labels via UI_GLYPHS, layered over the world demo.
   This is the milestone that exercises the full stated intent: logic-side layout, bridge
   submission, on-device render, layering.
   (Done 2026-07-07 — INTENT-COMPLETE, HW-verified interactively. Protocol: RenderUiBlock
   (layer, scroll, five counted tables packed in ONE adopted allocation, block-LOCAL
   clip/paint/glyph refs), ano_render_ui_set validates producer-side and drops invalid
   blocks as a warned no-op returning true — backpressure retry loops can never spin on
   bad input; ring-full stays the retryable false. Render side: 64-entry id-keyed
   registry (text semantics verbatim), compose = stable layer sort + whole-block skip on
   table-budget overflow (truncation would corrupt refs) + ref rebase (clipRef/paintRef/
   stopFirst/UI_GLYPHS aux0) + scroll fold + bounds; per-slot refresh copies pending
   tables and publishes slot-current counts (the text protocol). UI glyph labels live in
   their own frame-buffer region [ANO_UI_GLYPH_FIRST=16384, +5120) so the plain glyph
   loop never double-draws them; the world region is clamped to end there; UI_GLYPHS
   evaluates in-shader as a per-pixel range walk of shade_window, tinted and clipped.
   ANO_UI_DEMO/_OPAQUE now PIN composition (registry still adopts) so the self-test
   canvas is bridge-proof. Logic demo (main.c): menu_layout/menu_hit are the single
   source of truth for both rendering and hit-testing; hover tracks the cursor and
   resubmits ON CHANGE only; M toggles, RESUME closes, OPTIONS increments its own label,
   QUIT closes (demo no-op); persistent status bar once the first snapshot publishes the
   viewport. Driven end-to-end on hardware via XTEST synthetic input: bar, open, hover
   highlight + glow, OPTIONS (2) after two clicks, RESUME close — all screenshot-
   verified, validation-clean; the pinned self-test re-measured bit-identical
   (RMS 0.0384/255, max 13/255, same pixel). Found + fixed along the way: SHADER_INCLUDES
   in the root lists file was missing textcoverage.glsl/uicoverage.glsl, so include-only
   shader edits never recompiled their .spv — a latent build bug the text lane had been
   masking by co-editing textraster.comp. sRGB authoring helper ano_ui_color_srgb landed
   with the demo. Suite 19/19, release+debug clean.)
6. Gradients + clip table + dither + UI_PATH (runtime monotone-quad bake through the text
   bake's split machinery, importer orientation-fix). Reference gates extend per feature.
7. Per-tile lists + interior/opaque classification at compose, indirect dispatch, glyph
   interleave (true unified z). A/B against the brute scan; keep both paths a build flag
   until the win is measured.
8. Async lane switch-on (the CB is already in the text submit slot), A/B in-frame vs async,
   freeze-methodology numbers recorded here, `docs/ui/ui-render.md` updated with measured
   costs. PoC complete.

Rough scope by the text precedent (2.5k lines total there): ABI+builder ≈ 300 lines C,
reference evaluator + oracle tests ≈ 500, ui_raster.c ≈ 500, shader additions ≈ 300 GLSL,
bridge/registry ≈ 200, demo ≈ 150. ~2k lines to the step-5 milestone.

## 8. Future paths (explicitly out of PoC scope)

- Opaque front-to-back pre-pass economics via list truncation (§3.7.3) — measure first.
- Retainer blocks: rarely-dirty expensive blocks rendered to cached textures with budgeted
  oldest-first re-raster — the shadow-cache policy verbatim, only if a block measures hot.
- Per-block damage rects on a persistent ×1 overlay (clear region + redraw dirty blocks) —
  the WebRender partial-present analog; pairs with the ×1 overlay VRAM reclaim.
- Backdrop blur / frosted glass: requires a tonemapped scene color pyramid; a composite-pass
  effect, not a UI-lane one. Priced separately when art direction asks.
- Squircle/superellipse corners (Levien's exponent machinery §3.4 doubles as the shape).
- Rotated prims (the inv 2×2 already carries it; clip/shadow closed forms are the blockers).
- Animation params: per-block scalar channel (opacity/offset) updated without re-submit.
- World-space diegetic panels via the fragment twin (§3.10).
- HDR composite paper-white multiply (§3.8) when an HDR swapchain lands.

## 9. Sources

Compute-school lineage: RAVG (hhoppe.com/proj/ravg), MPVG (w3.impa.br/~diego/projects/GanEtAl14),
Li et al. 2016 scanline (kunzhou.net, archived; github.com/Mochimazui/gpu-scanline-path-rendering),
Manson & Schaefer wavelet rasterization + analytic curve rasterization (josiahmanson.com;
people.engr.tamu.edu/schaefer/research/scanline.pdf), Loop & Blinn 2005, Kilgard & Bolz
NV_path_rendering (arxiv.org/abs/1210.0396), Levien's lineage commentary
(raphlinus.github.io: modern-2d 2019, piet-gpu-progress 2020).

Vello current: stroke expansion paper (arxiv.org/abs/2405.00127, HPG 2024;
linebender.org/gpu-stroke-expansion-paper), flatten.wgsl (github.com/linebender/vello),
sparse-strips pivot (linebender.org/blog/tmil-13, tmil-24, tmil-25; the Potato design doc;
Stampfl's 2025 ETH thesis on vello_cpu/hybrid; vello issue #670).

Analytic school: Wallace fast rounded-rectangle shadows (madebyevan.com/shaders/
fast-rounded-rectangle-shadows, CC0), Levien blurred rounded rects (raphlinus.github.io/
graphics/2020/04/21/blurred-rounded-rects.html), GPUI (zed.dev/blog/videogame), Rive
feathering (rive.app/blog/how-rive-reinvented-feathering-for-the-vectorian-era), IQ 2D SDFs
(iquilezles.org/articles/distfunctions2d).

Production compositors: WebRender (firefox-source-docs RenderingOverview; servo/webrender
CLIPPING_AND_POSITIONING.md, text-rendering.md, picture module docs), Flutter Impeller (FAQ,
README, flutter/engine PR #50856, perf best practices), Skia Graphite (Chromium blog
announcement), Rive runtime interlock modes (rive-app/rive-runtime
render_context_vulkan_impl.cpp; Khronos ROAA proposal), Qt scene graph renderer docs, Unreal
Slate/UMG invalidation docs, Unity UGUI/UI Toolkit docs, Dear ImGui draw system.

Color/HDR/composition: Microsoft Advanced Color (learn.microsoft.com), Xbox ATG SimpleHDR,
Unity URP HDR output, Frostbite HDR GDC 2017, AMD FSR3 UI composition (gpuopen.com),
premultiplication (realtimerendering.com/blog/gpus-prefer-premultiplication), gamma/blending
(blog.johnnovak.net what-every-coder-should-know-about-gamma; almarklein.org/gamma.html).

Slug patent disclaimer (2026-03-17, US 10,373,352 into public domain): terathon.com/blog/
decade-slug.html; sluglibrary.com; hackaday.com 2026-03-20 coverage.

Engine-internal: docs/text/font-render.md (design of record for the lane being generalized),
docs/references/scanline_sweep.md (the coverage technique), and the integration anchors
verified this pass throughout §1/§4.
