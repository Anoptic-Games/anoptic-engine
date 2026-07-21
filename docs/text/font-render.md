# FONT_RENDER.md 〜 Scanline Sweeper text

How GPU glyph rendering via the Scanline Sweeper technique plugs into this renderer
(`docs/references/scanline_sweep.md`). Short version:
the technique fits this renderer unusually well. The renderer already has every structural
prerequisite 〜 a dedicated async compute queue with four timeline lanes, a lag-0 async pass
precedent (light-cull) whose sync shape is exactly what text needs, cross-queue CONCURRENT
sharing helpers, a storage-image-then-sample precedent (Hi-Z), a dynamic-rendering composite
stage with an obvious insertion point, and a staged-upload helper for the static curve data.
No frame-graph surgery is required; the work is one new async lane plus one new core module.
Friction points exist (§6) but all have known mitigations; two of them were found by auditing
the actual font file and are worth reading before anything else is built.

## 1. The technique (what shapes the integration)

Full summary in `docs/references/scanline_sweep.md`. The properties that drive design:

- Coverage is computed analytically per pixel as a sum of signed trapezoid areas swept by
  monotonic quadratic Béziers against the pixel's em-space window. No winding numbers, no
  sample points, no SDF texture, no atlas. Anti-aliasing falls out of the math.
- All curve data is static: cubic→quadratic (not needed for TrueType), split until x- and
  y-monotonic, at which point bbox == endpoints and culling is trivial. Baked once per face,
  never invalidated by scale, perspective, or orientation. This is the future-proofing: the
  same buffers serve screen-space UI today and world-space decal/flat-geometry text later.
- Curves live in storage buffers as binary16 pairs with shared-vertex compression (first
  point implicit from the previous curve, sentinel resets). Memory bandwidth is the stated
  bottleneck; the ASCII set of Geist-Regular measures ~13 KiB (§3), so ours is negligible.
- Execution is a compute shader (recommended by the paper) or a pixel shader. The pixel
  shader variant is what a future world-space text lane would use on quads; the compute
  variant is the UI overlay lane built here.
- One documented failure mode: overlapping same-winding contours break the area sum
  (coverage denominator exceeds 1). This is live in our demo charset 〜 see §6.1.

## 2. Architecture end-to-end

Init (CPU, once per face): `src/text` loads `resources/fonts/Geist/static/Geist-Regular.ttf`
through FreeType (`FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING`, font units, UPEM=1000),
decomposes outlines via `FT_Outline_Decompose`, converts line segments to degenerate quads
(p1 = midpoint 〜 they fall into the shader's linear-fallback path naturally), splits quads at
interior derivative extrema until monotonic, normalizes winding (`FT_Outline_Get_Orientation`)
and coordinates to the em square, packs binary16 shared-vertex streams plus a glyph directory
`{curveOffset, curveCount, bboxMin, bboxMax, advance}`. The renderer uploads both blobs once
via `stagingTransfer` (`instanceInit.c:2729`) to device-local memory.

Per frame (CPU, render thread for the PoC): format the profile stats into strings, shape them
(UTF-8 → glyph ids → pen advances) into an `AnoGlyphInstance` array, bin instances into 16×16
screen tiles, write instances + tile ranges into a per-frame host-visible buffer
(`MAX_FRAMES_IN_FLIGHT`=3 slots, persistently mapped, same pattern as the uniform ring,
`instanceInit.c:1436`). Shaping re-runs only when the text changes (stat cadence, §7); the
per-frame cost is a ~16 KiB memcpy.

Per frame (GPU, async): a text command buffer on the dedicated compute queue clears the
overlay image and dispatches `textraster.comp` 〜 one workgroup per screen tile, early-out on
empty tiles, one thread per pixel. Each thread walks its tile's instance list; per instance it
maps the pixel window into em space with the instance's prebaked inverse 2×2 (this is where
scale, pan, skew, and rotation all fold in), walks the glyph's curves with monotone-bbox
rejection, accumulates signed trapezoid areas per the paper's clamped single-root scheme,
clamps coverage to [0,1] (the overlap fix, §6.1), applies the paper's gamma step, and
src-over-accumulates premultiplied color in registers. One `imageStore` per covered pixel.

Composite (GPU, graphics queue): the existing composite block (`vulkanMaster.c:1312-1370`,
dynamic rendering straight onto the swapchain) gains one draw after the PiP inset loop,
immediately before `vkCmdEndRendering` at `vulkanMaster.c:1369`: a bufferless fullscreen
triangle (reuse `tonemap.vert`) sampling the overlay with fixed-function premultiplied blend
(ONE, ONE_MINUS_SRC_ALPHA). Text therefore composites over everything including PiP views,
post-tonemap in LDR, which is what UI text wants.

## 3. What is already in the tree

- Geist vendored at `resources/fonts/Geist/` 〜 18 static TrueType instances plus two
  variable-font TTFs. TrueType means quadratic outlines natively; the cubic→quad
  preprocessing stage is a no-op for this font (keep the hook for CFF/OTF later).
- Audit of Geist-Regular ASCII 32..126 (fontTools, this pass; corrected during
  step 2 〜 the first pass missed composite glyphs `: ; \` i j`, which FreeType decomposes):
  1654 monotone segments total (958 quads + 696 lines), mean 17.4 per glyph, worst `@` at
  63. Measured baked stream: 3490 points ≈ 13.6 KiB for the ASCII set; the full 974-glyph
  face lands in the low hundreds of KiB. Whole-face baking at boot is milliseconds; no
  disk cache needed.
- Geist has no legacy `kern` table. Kerning lives exclusively in GPOS PairPos subtables,
  formats 1 and 2 only (audited) 〜 consequences in §6.2.
- FreeType submodule populated at `external/freetype` (2.13.3+, `.gitmodules` entry exists),
  documented as intended for the text stack in `external/external.md:11`, but referenced by
  no CMakeLists yet. Wiring recipe in §5.
- `src/render/text/ano_RenderText.{c,h}` 〜 a disabled, uncompiled SDF/atlas-era stub. Every
  step of its rewrite plan (pixel sizes, atlas packing, bitmap staging, SDF) is obsolete
  under this technique. Delete both files when the new module lands; nothing in
  them transfers. The old `feature-render-text` salvage branch named in `docs/notes.md:290`
  no longer exists on local or origin.
- Reserved enum slots `PIPELINE_SDF_COMPOSITE` and `PIPELINE_UI` (`components.h:18-19`)
  anticipate this work. The compute raster pass takes a new `PIPELINE_COMPUTE_TEXTRASTER`
  slot; `PIPELINE_UI` stays reserved for the eventual in-world/UI draw lane (§9).
- `anostr_t` (Step 4) shipped; `docs/TODO.md:28` explicitly gates the UTF meaning layer on
  "until the text renderer forces it". This is that moment, but only a minimal UTF-8
  decoder is needed (§4), not the full layer.

## 4. New module: `src/text` + the minimal shaper

Layout per module convention: public `include/anoptic_text.h` (no FreeType types leak;
faces are opaque ids), implementation `src/text/` registered as a core module
(`target_sources(anoptic_core ...)`, mirroring `src/time/CMakeLists.txt`). Core-side rather
than render-side because shaping is exactly the thing the logic world will call when text
blocks start arriving over the bridge. FreeType allocations route through a module-owned
mi_heap via custom `FT_Memory` hooks (FreeType supports this natively), keeping the
arena discipline.

Public API, PoC scope:

    ano_text_init / ano_text_shutdown
    ano_text_font_load(path)                              -> AnoFontId
    ano_text_font_bake(font, firstCp, lastCp, heap, &out) // GPU-ready blobs in caller heap
    ano_text_shape(bake, utf8, len, sizePx, origin, color, out, cap, penOut) -> count
    ano_text_measure(bake, utf8, len, sizePx, &w, &h)
    ano_text_shape_runs(bake, utf8, runs, runCount, origin, out, cap, penOut) -> count
    ano_text_measure_runs(bake, utf8, runs, runCount, &w, &h)   // v2 style runs

Load/bake are bound to the module thread (FreeType underneath); shape/measure are pure
functions over the immutable bake 〜 no parser state, callable from ANY thread. That split
is the logic-side enabler: game code shapes into instance arrays wherever it runs, and the
caller-buffer signature writes straight into a mapped per-frame buffer with zero
allocation. penOut chains runs (color changes mid-line) bitwise-exactly.

Shaper v0 (PoC, built in step 4): strict UTF-8 decode (overlongs/surrogates rejected,
byte-wise resync), cmap-by-range lookup against the bake, horizontal advances, `\n`/`\r`
handling, out-of-range codepoints advance a visible ANO_TEXT_GAP_EM. That is sufficient
for profile lines. Shaper v1 (landed 2026-07-04): the in-house GPOS PairPos reader 〜
src/text/text_gpos.c, ~300 lines, FreeType-free (the bake hands it the raw table plus a
slot→glyph-id map, so the white-box test drives it with synthetic tables). Semantics:
'latn'→'DFLT' default LangSys, 'kern' feature only (Geist's mark/mkmk lookups excluded),
lookups accumulate in LookupList order, first-applying-subtable-wins per pair (specific
fmt-1 pairs short-circuit the fmt-2 class fallback; class-0 rows apply with their value),
type-9 Extension unwrapped, every read bounds-checked with malformed tables failing soft
to zero kerns. Extraction runs at bake time into a dense FUnit matrix (slotCount² scratch,
ranges past 1024 slots skip with a warning), compacted to a key-sorted AnoKernPair table
on the caller heap; ano_text_kern is a pure binary search, and shape/measure add the pair
adjustment between adjacent in-range glyphs, resetting the chain at newlines and gaps
(kerning never bridges penOut continuations 〜 split runs at non-kerning boundaries).
Oracle, fontTools-audited: ASCII×ASCII = 2891 nonzero pairs summing to −63296 FUnits
(AV −106, LT −140, To −80), pinned bitwise in anotest_text alongside a hand-assembled
synthetic table covering both coverage/ClassDef formats, accumulation, extension, and
truncation. Explicit non-goals, stated to prevent HarfBuzz-shaped creep:
GSUB ligatures, mark attachment, bidi/RTL, complex scripts, hinting (the technique is
unhinted analytic AA by design). If the game ever needs Arabic or Devanagari, that is a
separate decision about a real shaping engine, not an extension of this module.

Shaper v2, per-glyph color/style runs (landed 2026-07-04): `AnoTextRun {byteCount,
sizePx, color}` spans partition the UTF-8 buffer; `ano_text_shape_runs` /
`ano_text_measure_runs` walk ONE pen across all runs, so a style change never moves a
glyph. The four public functions are now thin wrappers over a single static `shape_core`
in text_shape.c (the old shape/measure duplication is gone; plain shape = one synthesized
run, bit-identical op order). Zero GPU-side change: size and color were already
per-instance in the 48-byte ABI (`inv` carries 1/sizePx). Semantics: pair kerning
bridges a run boundary iff the size is unchanged (the chain tracks the size that shaped
the previous glyph, so color splits inside kern pairs are position-bitwise vs the
unsplit shape and empty runs can't break a bridge); a size change resets the chain (a
kern between two sizes is ill-defined); `\n` steps by the lineHeight of its own run; a
run boundary inside a multi-byte sequence never splits the codepoint (the lead byte's
run styles it). measure_runs height = per-line steps summed at each line's ending size.
Standing demos: the world panel (mixed 72/48/60 px lines, line 3's color splits land
inside the kern pairs A|V L|T T|o W|a) and the profiling OSD via the
`ano_vk_text_set_runs` twin (white stats, the total colored by frame budget
green<4ms<amber<8<red, VRAM line dimmed).

## 5. Renderer integration map

All anchors verified against current source.

New async lane (mirrors light-cull, the lag-0 precedent 〜 not Hi-Z, which is lag-2 because
it consumes the frame's own depth; text inputs are CPU-side so it has no GPU dependency
at all):

- `textTimeline` semaphore next to `lcTimeline` (`structs.h:1267`), created in
  `createSyncObjects` (`instanceInit.c:2869-2891` block).
- Per-frame `textCommandBuffer` allocated from the existing `computeCommandPool`
  (`vulkanMaster.c:4437`), recorded each frame after the `frameFence` wait
  (`vulkanMaster.c:3317`). Reuse is fence-safe by the same argument as
  `lightcullCommandBuffer` (comment at `vulkanMaster.c:437-438`): its sole consumer is the
  fence-tracked graphics submit of the same slot.
- Submit on `ctx.computeQueue` adjacent to the lc submit (`vulkanMaster.c:3489-3507`), no
  wait semaphores (host wrote the instance buffer before submit; the overlay WAR against the
  3-frames-ago composite read is retired by the frameFence host wait), signaling
  `textTimeline = ordinal`. Copy the lc failure fallback verbatim: on failed submit,
  `vkSignalSemaphore` host-side to keep the timeline monotonic and non-deadlocking
  (`vulkanMaster.c:3499-3507`).
- Graphics submit gains one wait: `textTimeline == ordinal` at FRAGMENT_SHADER, appended to
  the single-submit arrays (`vulkanMaster.c:3407-3424`) and to batch B of the split submit
  (`vulkanMaster.c:3461-3481`). Timeline waits may be submitted before their signal 〜 the lc
  lane already relies on this (comment at `vulkanMaster.c:3440-3442`).
- Overlay layout flips inside the text CB with `VK_QUEUE_FAMILY_IGNORED` barriers, semaphores
  carrying the cross-queue memory dependency 〜 the Hi-Z pyramid pattern
  (`vulkanMaster.c:349-361, 396-408`).
- Fallback path, mandatory: when no dedicated compute family exists (`findQueueFamilies`
  fallback, `instanceInit.c:439-448`) or `ANO_FORCE_NO_ASYNC_TEXT` is set, record clear +
  dispatch into the main CB just before the composite block with a COMPUTE→FRAGMENT barrier 〜
  the lc in-frame precedent (`vulkanMaster.c:1092-1103`). Gate parses in `initVulkan` next to
  the other toggles (`vulkanMaster.c:4349-4370`).

Resources:

- Overlay image ×MAX_FRAMES_IN_FLIGHT (per frame, not per view 〜 it overlays the final
  frame): swapchain extent, `R8G8B8A8_UNORM`, STORAGE|SAMPLED, `swapchainAllocator`, created
  in `createColorResources` (`instanceInit.c:1777`), destroyed in `cleanupSwapChain`
  (`instanceInit.c:1131`), descriptors rebound next to the `updateTonemapDescriptorSets` call
  in `recreateSwapChain` (`instanceInit.c:1340`). ~40 MiB at 2560×1368 〜 see §6.6.
- Curve + directory buffers: device-local, one-shot `stagingTransfer` at init, EXCLUSIVE
  sharing (only one queue family ever reads them per boot mode, chosen at init 〜 the
  CONCURRENT helper `buffer_share_async_compute` at `vulkanMaster.c:1858` is not needed).
- Instance/tile buffer: per-frame HOST_VISIBLE|COHERENT persistently mapped via
  `createDataBuffer` (`instanceInit.c:1406`), uniform-ring pattern. No SlotUpload 〜 the
  buffer is rewritten wholesale at text-change cadence, there are no per-slot deltas.
- Compute pipeline: new `PIPELINE_COMPUTE_TEXTRASTER` enum before `PIPELINE_TYPE_COUNT`
  (`components.h`), created in `ano_vk_init_pipelines` following the lightcull recipe
  (`pipeline.c:885-935`); compute types stay out of the draw registry by convention
  (`components.h:44-50`). Descriptor set: curve SSBO, directory SSBO, instance/tile SSBO
  (per-frame), overlay storage image (per-frame); pool sizes bumped at
  `instanceInit.c:1855-1883`.
- Overlay blend pipeline: bespoke handles next to `tonemapPipeline` (`structs.h:1039-1042`),
  built like `ano_vk_init_tonemap` (`pipeline.c:1040`) plus one
  `VkPipelineColorBlendAttachmentState` with premultiplied alpha; new `overlay.frag`, reuse
  `tonemap.vert`. Recorded as one draw before `vkCmdEndRendering` at `vulkanMaster.c:1369`.
- Shaders: `resources/shaders/textraster.comp`, `overlay.frag` appended to the foreach list
  at `CMakeLists.txt:175` (glslc, vulkan1.2 target).
- New code lives in a new TU `src/vulkan_backend/text_raster.c` rather than growing
  `vulkanMaster.c` (4712 lines): record/submit/create helpers, with ~15 lines of hooks in
  `drawFrame` and ~10 in the composite block.

CMake / FreeType wiring (top level, not Vulkan-gated, since `src/text` is core):

    set(FT_DISABLE_ZLIB ON)  set(FT_DISABLE_BZIP2 ON)  set(FT_DISABLE_PNG ON)
    set(FT_DISABLE_HARFBUZZ ON)  set(FT_DISABLE_BROTLI ON)
    add_subdirectory(external/freetype EXCLUDE_FROM_ALL)
    target_link_libraries(anoptic_core PRIVATE freetype)

mirroring the mimalloc/glfw pattern (`CMakeLists.txt:55-60, 123-125`). None of the disabled
codecs are needed for plain TTF parsing.

Latency budget: the paper logs 240 µs for large-coverage 1440p text on an RTX 2060. The demo
overlay covers a few percent of the screen on a 3090; expect low tens of µs. It executes on
the compute queue concurrently with the shadow/geometry region (~2 ms of graphics work) and
is only awaited at the composite's fragment stage 〜 same-frame data, fully hidden latency,
same shape as light-cull. The async lane is nonetheless the right call: it future-proofs for
full-viewport UI at the paper's cost class without ever touching the graphics critical path.

## 6. Friction points

1. Coverage overflow from overlapping ink 〜 confirmed, measured, and broader than the
   first audit saw. The paper's coverage sum breaks when ink covers a pixel more than
   once, and Geist ships BOTH forms of that: separate same-winding contours overlapping
   (`# $ + f t`, measured unclamped coverage exactly 2.0; `%` was a bbox false positive in
   the original audit 〜 its ink never overlaps) and single contours that SELF-overlap 〜
   Geist draws `H 8 @` as overlapping strokes joined by diagonal jogs, leaving winding-2
   pockets (measured peaks 1.87 / 1.75 / 1.50) that a contour-pair audit cannot see.
   Mitigation, validated off-GPU in step 3: clamp per-glyph coverage to [0,1] at assembly.
   Against FreeType's nonzero-rule ground truth the clamped reference rasterizer measures
   worst-probe RMS 2.7/255, residual error confined to AA pixels on overlap-pocket
   boundaries. Note the paper's own per-contour-evaluation fallback handles only the first
   form 〜 self-overlap inside one contour survives it 〜 so the clamp is the more general
   fix; offline outline union remains the escalation path if artifacts ever show.
2. Kerning is GPOS-only in this font. `FT_Get_Kerning` reads the legacy `kern` table and
   returns zeros for Geist 〜 do not burn time wiring FreeType kerning APIs that cannot
   work here. RESOLVED: shaper v1's in-house PairPos reader landed (§4); the ANO_TEXT_DEMO
   pinned pipeline re-verified end-to-end with kerned layout (GPU vs reference RMS
   0.031/255).
3. Dedicated compute queue is not guaranteed (`findQueueFamilies` falls back to the graphics
   family). Every async lane in this renderer carries an in-frame fallback recording site;
   text is no exception and the pattern is mechanical (§5). Cost is a second, trivial
   recording path, not a design fork 〜 the shader and resources are identical.
4. A third async lane raises the sync-surface area. The submit-failure timeline-signal
   fallback, the fence-safety argument for CB reuse, and the wait-before-signal submission
   order are all inherited invariants that must be replicated exactly; each has a verified
   in-tree precedent cited in §5. This is copy-the-pattern work, but it is correctness-
   critical copying.
5. Multi-glyph pixel overlap forbids the naive per-glyph dispatch. Adjacent glyphs' bboxes
   overlap (kerned pairs, italics) even when ink doesn't; independent per-glyph workgroups
   doing read-modify-write on the overlay would race. The tile-gather design (§2) is
   therefore required for correctness, not an optimization: each pixel is owned by exactly one thread,
   which iterates all covering instances and blends in registers. Decided now so nobody
   "simplifies" it later into a race.
6. Overlay VRAM: full-res RGBA8 ×3 ≈ 40 MiB at 2560×1368. Acceptable against the several
   hundred MiB reclaimed by the recent atlas/MSAA/PiP work. Shrink paths when the UI layer
   gets real: dirty-
   tile persistent overlay (×1 + budgeted re-raster, the shadow-cache philosophy) or a
   half-float R8 coverage lane for monochrome text. Not PoC work.
7. Gamma placement. The paper gamma-corrects coverage to produce alpha; the composite blends
   onto an sRGB swapchain where fixed-function blending is linear-correct per spec. Apply
   the paper's gamma exactly once and eyeball against FreeType's own rasterization 〜 one
   visual tuning session, flagged so it isn't debugged twice.
8. binary16 curve precision: 2^-10 em quantum, validated by the paper on this exact font at
   text sizes. A future world-space signage lane zooming a glyph to meters may want the f32
   escape hatch (directory flag + fat buffer); noted, not built.
9. FreeType license: FTL (BSD-style with attribution clause) 〜 add the credit line to
   third-party notices when it starts shipping in builds.
10. Solver chord-fallback ghost bands 〜 found on-screen in step 6, root-caused, fixed. The
   original solve_mono fell back to chord interpolation when |a| ≤ 1e-2·|span| (meant to
   absorb f16 noise on baked lines). But genuine font curvature reaches arbitrarily far
   below any threshold: Geist's shallow stroke edges (`v w ( 2 K`, and at larger sizes even
   `"`) crossed the band boundary up to |a|/4 em off, so a stroke's accurately-solved edge
   and its chord-approximated partner stopped cancelling 〜 a constant phantom Σ(Δy)·w for
   every window to their right. Faint horizontal bands (~4/255 at 36 px) bridging concave
   nooks; invisible to RMS thresholds (outside the ink, below the noise floor) and the
   step-3 probe set didn't include the affected glyphs. Fix: the citardauq quadratic form
   t = 2c/(−b − sign(span)·√d) 〜 under monotonicity sign(b) = sign(span) (the bake clamps
   controls into the endpoint box), so the denominator is the additive |b|+√d, cancellation-
   free, and a→0 degrades exactly to the chord: one branch-free formula for real quads and
   noise-quad lines, no threshold to tune. The error was a fixed em quantity against a
   window that shrinks with 1/S, so it grew with font size: the regression net in
   anotest_text sweeps EVERY baked glyph at 64 and 200 px on a padded grid and asserts zero
   isolated coverage (≥3/255 with an all-zero FreeType 3×3 neighborhood 〜 honest AA always
   touches ink). Old solver measured against it: 562 ghost pixels at 64 px, worst 7/255 on
   `(`; fixed solver: zero at both scales, demo-canvas ghost scan 141 → 0.

## 7. Demo target: profile lines on-screen

Source data: `ano_print_profiling` (`vulkanMaster.c:3231`) already averages GPU region times
and VRAM per allocator, printing every `ANO_PERF_WINDOW_FRAMES` = 128 frames
(now in `src/vulkan_backend/frame/profiling.c`):

    [profile mode=%s] GPU ms: upload=... compute=... shadow=... (frusta %.1f/%u)
    lighting=... composite=... total=... | VRAM MiB: gpu=... tex=... swap=... staging=... | ...

The demo mirrors exactly these fields on-screen: at each print tick, format the same values
into 3-4 lines, shape at ~16 px into the instance buffer, top-left origin. Everything stays
inside the render thread 〜 the stats live there, so the PoC needs zero bridge traffic. This
also means the demo exercises shaping, baking, raster, async sync, and composite blend with
content that changes at a realistic cadence (numbers tick every 2 s) while the instance
buffer re-uploads per frame, matching the eventual UI workload shape. (120 frames is a
frame count, not wall time 〜 a fraction of a second at demo framerates; make it a knob if the
flicker rate annoys.) Toggles:
`ANO_TEXT_OVERLAY` (demo on/off), `ANO_FORCE_NO_ASYNC_TEXT` (fallback lane A/B). Note the
async lanes (hiz/lc/text) sit outside the graphics timestamp chain; text pass timing uses
the established freeze/A-B methodology rather than a new `ANO_TS_*` slot for now.

## 8. GlyphInstance ABI draft

The user-sketched fields (glyphID, position, scale, color) extended to carry pan/skew/rotation
without per-pixel transform cost 〜 the shader gets the pixel→em inverse prebaked. Final ABI
(shipped in `anoptic_text.h`, step 4); field order differs from the original sketch so a GLSL
`vec4 inv; vec4 color; vec2 origin; uint glyphID; uint flags;` declaration lands on identical
std430 offsets (0/16/32/40/44, stride 48 〜 a vec4 at offset 24 would have misaligned):

    // std430, 48 B; static_asserts pin the offsets
    typedef struct AnoGlyphInstance {
        float    inv[4];      // 2x2 pixel->em inverse, rows; scale, the screen-y-down vs
                              // em-y-up flip, and future skew/rotation fold here
        float    color[4];    // premultiplied linear RGBA
        float    origin[2];   // baseline pen position, screen pixels, y-down
        uint32_t glyphID;     // glyph directory index
        uint32_t flags;       // reserved (f32-curve escape, effects)
    } AnoGlyphInstance;

The v0 shaper emits inv = (1/size, 0, 0, −1/size); em = inv · (pixel − origin).

The forward transform exists only CPU-side, used for tile binning. Text-layout sub-buffers:
the per-frame buffer is instance array + tile ranges; a block is a contiguous instance range
`{first, count}` (PoC: one block). When the logic side starts producing text, blocks arrive
over the bridge either as copy-at-submit bulk payloads (the `RCMD_BULK`/`bulk_owned` lifetime
rules, `anoptic_render.h:334`) or through the zero-copy stream region
(`ano_render_stream_begin/commit`) 〜 transport choice deferred until the UI layer defines
update rates; the ABI above is public in `anoptic_text.h` from day one so that path never
re-ABIs.

The v0 bridge path LANDED 2026-07-04, taking the copy-at-submit option (text updates are
discrete label changes at UI rates, not per-tick streams 〜 the events-vs-state standing
rule picks the command ring). Public surface: `anoRenderTextBake()` hands logic the
render-side bake (immutable plain data, published before the logic thread spawns; NULL =
text stack down, and shaping over NULL yields 0 so producers degrade for free); logic
shapes on its own thread and ships `RenderTextBlock {count, instances}` via
`ano_render_text_set(bridge, text_id, instances, count)` 〜 one mi allocation packs header
+ copy, the command carries it `bulk_owned`, and the render-side registry ADOPTS the
allocation on RCMD_TEXT_SET (no second copy; freed on replace/clear/teardown).
`ano_render_text_clear` / count 0 removes a block; text_id is the producer's namespace
(the light_id convention). SET is a full replace, so unlike CREATE/DESTROY a dropped
submit is safe to skip 〜 the block is stale one tick; one-shot sets and clears retry.
Registry: ANO_TEXT_MAX_BLOCKS (64) entries, render thread only; compose = OSD region
[0, textOsdCount) + blocks in creation order, truncated at ANO_RENDER_TEXT_MAX == the
region cap (static-asserted against ANO_TEXT_WORLD_FIRST); every change recomposes
textPending and bumps textVersion 〜 the frame-refresh protocol and the GPU dispatch are
untouched. ANO_TEXT_DEMO's pin suppresses block COMPOSITION (registry still adopts), so
the offline pixel-compare canvas stays exactly the pinned demo text. Demo (main.c logic
master): a styled title (runs path), a transient notice cleared 15 s after frames start
(REVENT-free one-shot via the armed deadline), and a 1 Hz camera readout; all three
verbs hardware-verified (notice visible then gone, validation 0 both normal + pinned
runs).

## 9. Future paths (explicitly out of PoC scope)

- World-space text (LANDED 2026-07-04, ahead of schedule): the paper's pixel-shader
  variant, built as a bespoke lane rather than a material type 〜 textworld.vert/.frag on a
  bufferless spinning quad, recorded inside each view's additive pass (the last MSAA color
  pass, so the panel resolves with the scene; depth-tested LESS/no-write, premultiplied
  src-over onto lit HDR, cull NONE so the back reads as a mirrored sign). The coverage math
  moved to a shared include (textcoverage.glsl) consumed verbatim by both lanes, so the
  fragment variant can never drift from the compute lane or the CPU reference. The em
  window comes from screen derivatives of the interpolated panel coordinate
  (abs(dFdx)+abs(dFdy)), giving perspective-adaptive AA 〜 grazing views widen the window
  and the text self-filters instead of shimmering. Text shapes once at init into the upper
  region of the per-frame instance buffers (index ANO_TEXT_WORLD_FIRST on; the OSD pending
  path owns the region below), through the same shaper 〜 kerning included 〜 and the same
  48 B ABI; the raster descriptor set is shared (bindings 0–2 gained FRAGMENT visibility;
  the storage-image binding stays compute-only and is never statically used by the
  fragment stage). The MVP composes CPU-side per view from the uboData mirrors
  (proj·view·model), yaw driven by the UBO clock. Gate: ANO_FORCE_NO_TEXT_WORLD; both
  states hardware-verified validation-clean, suite 15/15; the panel is visible in main and
  PiP views (world content in every camera), readable front-on and correctly mirrored from
  behind. Panel: 768×256 virtual px on a 6×2-unit quad at the atrium center, 64 px text.
- Dense-text scaling: the paper's scanline-partition/stripe acceleration with cooperative
  shared-memory curve loads replaces the per-tile gather when full-page text appears.
  Indirect dispatch over non-empty tiles is the intermediate step.
- Variable fonts: Geist ships variable TTFs; `FT_Set_Var_Design_Coordinates` + re-bake per
  named instance. Static instances suffice indefinitely for engine UI.
- OTF/CFF faces: enable the cubic→quad stage (the hook exists in the bake path).
- Glyph directory on demand: bake full ASCII eagerly, other codepoints lazily at shape time
  (directory grows, curve buffer appends via the staged-upload path).

## 10. Build sequence

1. Wiring: FreeType into CMake (§5 recipe), delete `src/render/text/ano_RenderText.{c,h}`,
   skeleton `src/text` module + `include/anoptic_text.h` compiling on all platforms.
2. Bake path: decompose → monotone quads → winding/em normalize → f16 shared-vertex stream +
   directory. Unit tests: monotonicity and bbox==endpoints invariants, segment counts against
   this report's audit numbers (1654 / ASCII), closed-contour area sanity.
3. CPU reference rasterizer: scalar mirror of the shader math; RMS-compare coverage against
   `FT_Render_Glyph` bitmaps for a probe set including the overlap glyphs. This validates the
   coverage math and the clamp fix off-GPU before any Vulkan work. (Done: 13 probes at
   64 px/em, worst RMS 2.71/255, worst per-pixel 53/255 on winding-pocket AA boundaries;
   surfaced the self-overlap form now recorded in §6.1.)
4. Shaper v0 (UTF-8, cmap, advances, newline) + golden layout tests. (Done: pure over the
   bake, any-thread; AnoGlyphInstance ABI finalized with GLSL-aligned field order; goldens
   cover exact advances, blanks, CRLF, gap, cap-vs-count, bitwise penOut continuation,
   measure.)
5. GPU plumbing, visually inert: buffers, overlay ×3 + resize path, descriptor sets, compute
   pipeline, composite blend draw sampling a cleared overlay. Validation-clean. (Done:
   hardware-verified on the desktop 〜 overlay-on, ANO_FORCE_NO_TEXT, two live resize cycles,
   and WM-close teardown all validation-clean; init bakes 95 glyphs / 3490 points / 16.6 KiB
   static; frame totals unchanged. New TU src/vulkan_backend/text_raster.c; stub
   textraster.comp gates on instanceCount==0 with the full interface live.)
6. `textraster.comp` via the in-frame fallback path first (sync-trivial): static string on
   screen, screenshot-compare against the reference rasterizer. (Done: coverage math is a
   statement-for-statement GLSL port of text_raster_ref.c; workgroup = 8×8 tile, 64 lanes
   cooperatively cull instances in 64-chunks into a shared verdict mask 〜 order-preserving
   for the premultiplied over-blend 〜 with an any-hit flag so empty tiles skip the scan,
   and untouched pixels skip the imageStore (the clear already wrote transparent). Demo:
   126 instances, 3 stress lines covering all 95 glyphs at 36 px, shaped once into every
   frame slot. Screenshot-compare via ANO_TEXT_OPAQUE (flags bit 0 = opaque backdrop) vs an
   offline harness that mirrors the shader loop over the exported ano_text_window_sum: RMS
   0.034/255 over the full 2560×1368 canvas, max 2.11/255, zero pixels past 4/255 〜 inside
   the sRGB-roundtrip + UNORM8 envelope. Validation-clean; suite 15/15. Cost in-frame,
   release, busy desktop: composite 0.09→0.25 ms, total ≈ +0.15 ms; before the empty-tile
   skips it was +0.5 ms 〜 the remaining cost is the degenerate one-big-list gather, and
   real per-tile binning stays the step-8 lever if the stat screen needs it. Post-landing,
   on-screen inspection caught faint ghost bands on `v w ( 2` 〜 present in the CPU
   reference too, root-caused to the solve_mono chord-fallback threshold and fixed with the
   citardauq form; full story in friction §6.10, regression net (all-glyph two-scale
   ghost-pixel sweep) in anotest_text.)
7. Async lane: `textTimeline`, text CB, submit + graphics wait, `ANO_FORCE_NO_ASYNC_TEXT`
   A/B, validation-clean in both modes, freeze-methodology timing. (Done: lag-0 lane on
   asyncHiz's infrastructure, gated by asyncText 〜 decided at the toggle site so the
   overlay images and curve/directory buffers pick CONCURRENT graphics+compute sharing,
   downgraded non-fatally if the lane's objects fail. The per-frame raster CB submits to
   the compute queue FIRST, with no waits (inputs are CPU-written and slot reuse is
   frame-fence ordered 〜 same argument as the light-cull CB), signals textTimeline ==
   ordinal; the main submit waits it at FRAGMENT_SHADER on both the split and non-split
   paths; failed submits host-signal the ordinal; unInitVulkan drains the timeline next
   to hiz/lc. The shared record body parameterizes only the final barrier 〜 a compute-
   only family has no FRAGMENT stage, so the async CB releases at BOTTOM_OF_PIPE/0 and
   the semaphore carries the cross-queue dependency. Hardware-verified: async + forced-
   in-frame both validation-clean incl. two resize cycles and WM-close teardown; async
   overlay pixel-identical to in-frame (compare RMS 0.032/255, same max pixel). A/B,
   release, busy desktop: composite no-text 0.093 / in-frame 0.255 / async 0.125 ms 〜
   the raster is off the graphics timeline; the residual +0.03 ms is the composite blend
   draw itself. Totals within session noise, as with the light-cull split; the lever
   grows with text volume.)
8. Demo: profile-line mirror at print cadence, record before/after frame numbers here.
   (Done: ano_print_profiling shapes the same readout into a three-line 22 px overlay at
   the top-left via ano_vk_text_set 〜 render-thread-internal, zero bridge traffic. Update
   machinery: set() shapes into a pending canonical array (textHeap) and bumps
   textVersion; each frame slot copies it into its own mapped frame buffer right after
   its fence wait (ano_vk_text_frame_refresh, called before record/submit), so in-flight
   GPU readers are never overwritten and the push-constant count always matches the
   slot's contents. Boot shows a title line until the first 120-frame print; ANO_TEXT_DEMO
   pins the step-6 stress text so the offline pixel-compare harness keeps a stable
   target 〜 verified post-pin against two elapsed print intervals, still RMS 0.032/255.
   Hardware-verified: live on-screen updates across print intervals, validation-clean,
   suite 15/15. Numbers, release, busy desktop: composite with the stats overlay ≈ 0.11 ms
   vs no-text ≈ 0.09–0.14 ms 〜 the on-timeline cost of the live readout is inside session
   noise; the raster itself runs on the async lane (step 7). The PoC goal 〜 the
   profile lines on-screen through the Scanline Sweeper 〜 is delivered.)

Rough scope: `src/text` ≈ 1000-1200 lines C (bake 400, shaper 300, tests 400),
`textraster.comp` ≈ 300 lines GLSL, `text_raster.c` + hooks ≈ 600 lines. PoC total ≈ 2.5k
lines, no new heavyweight dependencies (FreeType is parse-only at init; an in-house
glyf/cmap/hmtx reader could even retire it later if the dependency ever chafes).
