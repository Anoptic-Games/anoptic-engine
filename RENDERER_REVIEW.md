# Renderer Review — structural limits from the Nsight frame trace

Date: 2026-07-02. Sources: `profile/` (GPU Trace advanced-mode metrics + shader profiler,
one 6.17 ms frame of the Sponza scene, Ampere/Ada-class hardware) cross-referenced against
the current `scene-sponza` code. Scope per request: not shader micro-optimization (mostly
done already), but structural limiters — synchronization, concurrency, occupancy/register
architecture, and CPU-side dispatch.

## 1. Headline

The frame is not throughput-bound anywhere. Every hardware unit is far below saturation
(top unit: VRAM at 21.9% of peak), the graphics engine reports only 40.3% active cycles,
and shader occupancy is launch-throttled (VTG by ISBE space 24.6%, pixel warps by
register allocation 18.3%). The frame alternates between two failure modes:
barrier-serialized starvation in the first third (the shadow region: ~130 micro-passes
executed strictly sequentially) and launch-throttled under-occupancy in the rest (the
color passes run at 13 of 48 warps). Four structural decisions produce most of this:

1. The shadow atlas pass issues 326 single-image pipeline barriers per frame and
   serializes 26 depth renders through one shared transient depth image.
2. Everything runs on one queue in one submit; the dedicated compute queue that
   `createLogicalDevice` creates is never used in-frame. Async overlap is zero.
3. Every raster pass, including all depth-only work (39% of measured draw time), runs the
   full-attribute `flat.mesh`, so inter-shader attribute space (ISBE) throttles warp
   launches on passes that consume none of those attributes.
4. MSAA is set to the maximum the device supports on every target in both views, and the
   picture-in-picture view renders at full swapchain resolution before being minified 3:1
   at composite.

## 2. Frame anatomy (measured)

One `vkQueueSubmit`, one command buffer, 6.11 ms on `Vulkan Graphics Q:0`. Event sums from
`profile/EventList.csv`:

| Region | Span | Contents | Event time |
|---|---|---|---|
| Compute prelude | 0.00–0.12 | update, lightsetup, shadowsetup, cull, fills, 5+ barriers | ~0.10 ms |
| Shadow atlas | 0.12–2.10 | 26 frustum depth renders (0.795 ms), 104 blur draws (0.52 ms), 326 barriers (1.63 ms attributed) | ~2.95 ms of events in a 1.98 ms span |
| View 0 | 2.10–4.04 | lightcull, prepass 0.60 ms, opaque 0.99 ms, transmission+additive 0.17 ms, pick copy | ~1.95 ms |
| Hi-Z view 0 | 4.04–4.30 | depth MAX-resolve pass, ~11 mip dispatches + per-mip barriers | ~0.25 ms |
| View 1 (PiP) | 4.30–5.90 | lightcull, prepass 0.79 ms, opaque 0.55 ms, transmission+additive 0.18 ms | ~1.60 ms |
| Hi-Z view 1 + tonemap | 5.90–6.14 | pyramid build, tonemap 0.06 ms, present barrier | ~0.24 ms |

API-call totals per frame: 369 `vkCmdPipelineBarrier`, 141 `vkCmdBeginRendering`/`End`
pairs, 168 pipeline binds, 237 descriptor-set binds, 34 indirect-count mesh draws,
107 plain draws, 29 dispatches, 1814 front-end draw/clear commands.

Key global metrics (`profile/analysis.yaml`):

| Metric | Value | Reading |
|---|---|---|
| GR Cycles Active | 40.3% | far under Nsight's 60% latency-limited threshold; engine drains at barriers and runs starved between them |
| Top unit throughputs | VRAM 21.9, CROP 20.0, PROP 15.9, ZROP 14.4, RASTER 13.6% | latency-limited everywhere, no unit near saturation |
| Warps active: pixel / mesh / compute | 20.5% / 1.5% / 0.6% | massive SM under-occupancy |
| VTG launch stalled on ISBE allocation | 24.6% of cycles | mesh-shader output space is the top occupancy limiter |
| PS launch stalled on register allocation | 18.3% of cycles | pixel warps can't launch for lack of register file |
| PS launch stalled on TRAM | 1.2% | interpolant plane storage is not the limiter |
| L1TEX / L2 hit rate | 65.9% / 71.0% | mediocre; consistent with 8×MSAA + dependent buffer chains |
| Shader-profiler "Unattributed" bucket | 20.0% of samples, 96% MISC | samples landing outside any resident shader — consistent with barrier drains |

## 3. Finding 1 — shadow pass synchronization architecture (largest single win) - ADDRESSED

`recordCommandBuffer` (src/vulkan_backend/vulkanMaster.c:510–708) drives the layered
Power-CDF shadow pass as: per frustum s in 0..41 — transition that frustum's two atlas
sublayers UNDEFINED→COLOR (one barrier), serialize the shared transient depth with a
LATE→EARLY barrier, begin rendering, draw, end, transition sublayers COLOR→SHADER_READ
(one barrier). Then two blur phases loop per frustum per sublayer: one barrier in, one
fullscreen 512² triangle, one barrier out — 104 blur draws, 208 blur barriers.

Barrier census for the region (from EventList descriptions): 132× COLOR→FRAGMENT, 81×
TOP→COLOR, 52× FRAGMENT→COLOR, 32× TOP→FRAGMENT (inactive-frustum layout parking), 28×
LATE→EARLY. All are single-subresource image barriers issued as individual
`vkCmdPipelineBarrier` calls. The region does ~1.3 ms of real draw work spread over a
~2.0 ms wall span, with 1.63 ms of barrier-attributed time interleaved; the SM occupancy
plot collapses to near zero between each 5–30 µs draw. This is the sawtooth region in the
trace and the bulk of the GPU's 60% idle time.

The key mechanism: a pipeline barrier's execution dependency is scoped by stage masks,
not by the image named in it. Every per-layer COLOR→FRAGMENT barrier therefore orders ALL
prior color-attachment writes against ALL subsequent fragment work — so the 104 blur
draws and 26 depth renders execute strictly one-after-another even though their data is
fully disjoint. ~130 passes of 5–30 µs each run sequentially on hardware that needs
thousands of concurrent warps to hide latency; the occupancy trace collapses to near zero
between every one. Three compounding serializers:

- Barrier granularity. Barriers are per-frustum/per-sublayer instead of batched. All 26
  depth renders write disjoint atlas layers; all 52 blur-X draws read the atlas and write
  disjoint temp layers; all blur-Y draws do the reverse. Each phase needs exactly one
  barrier covering the whole array (or one `vkCmdPipelineBarrier` with all subresource
  ranges), not 100+.
- The shared transient depth (`sh->depthImage`, single layer, structs.h:757) forces every
  frustum's rasterization to fully drain before the next may start (the `dWaw` barrier at
  vulkanMaster.c:564–571). A depth image with one slice per frustum (or depth array +
  layered rendering) removes the cross-frustum dependency entirely — 26 independent
  renders could then overlap on the SMs, which is exactly what a 512² render needs to
  fill a large GPU.
- One render pass per sublayer for the blur. 104 `BeginRendering` instances to blur 52
  layers twice. With the atlas layers as one attachment array (layerCount = active
  sublayers, `gl_Layer` from instance index, or one pass per direction with multiview),
  each direction is a single render pass with a single draw: 2 passes, 2 draws, ~4
  barriers, and the blur becomes one wide dispatch-shaped workload instead of 104 tiny
  serialized ones.

Restructure target: render all active frustum layers (still 26 draws, but no interleaved
barriers and no shared-depth serialization) → 1 barrier → blur-X all layers in one pass →
1 barrier → blur-Y all layers → 1 barrier. Expected effect: the drain/idle gap (region
span 1.98 ms vs 1.31 ms of measured work) disappears, and the work itself packs — 26
overlapping 512² depth renders and one wide TEX-bound blur wave use the machine far
better than 130 isolated micro-passes. Estimated 0.7–1.2 ms off the frame; the residual
floor is the ISBE-bound caster geometry (finding 3) plus the genuinely TEX-bound blur.

Note on a prior conclusion: the earlier compute-shader blur experiment regressed ~20% and
was correctly reverted — but it tested a different hypothesis (faster blur kernel,
raster↔compute transitions per layer, same per-layer structure). It says nothing about
batching the barriers or de-serializing the layers within the raster path, neither of
which has been tried. The blur kernel itself is near-optimal and should not be touched;
the choreography around it is the problem. Likewise the depth-render serialization
through the shared transient depth has never been on the table before.

## 4. Finding 2 — zero asynchronous execution - PARTIALLY ADDRESSED (Hi-Z build)

Measured outcome (2026-07-03, Hi-Z portion): the pyramid build now records into a per-frame
compute CB and runs on a dedicated compute-family queue. findQueueFamilies previously
first-fit the graphics family for compute, so `ctx.computeQueue` aliased the graphics
queue — it now prefers a compute-only family (NVIDIA exposes one) and the gate detects
aliasing. Ordering is two timeline semaphores: the compute build waits gfxTimeline ==
its frame's submit ordinal (depth resolves done), the ordinal+2 graphics submit waits
hizTimeline at COMPUTE|EARLY_FRAGMENT_TESTS before its cull — so the build overlaps the
NEXT frame's graphics and the cull consumes a lag-2 pyramid (descriptor slot, reprojection
history ring, and a post-recreate warmup gate all follow the lag). The pyramid and
depth-resolve images are CONCURRENT-shared between the two families; graphics keeps the
fixed-function MAX depth resolve and the resolve target's layout flips (its rest state
becomes SHADER_READ), so the compute CB needs no graphics-only stages. Gate:
timelineSemaphore + depthMaxResolve + distinct compute family; ANO_FORCE_NO_ASYNC_HIZ
pins the in-frame build, ANO_HIZ_ON enables view 0's occlusion test from startup (the H
key, headlessly). Measured (debug, phase-matched over 72 print windows, SHADOWMAP):
lighting −0.065 ms, total −0.054 ms. The chain is far cheaper than the trace's 0.4–0.5 ms
estimate because findings 5/6 already shrank it (half-res pyramid, inset view 1,
single-sample MAX-resolve reduce, 4×MSAA); the structural win is that enabling the
occlusion consumer now costs ~nothing on the graphics timeline, and the compute-queue +
timeline infrastructure exists for the lightcull overlap below. Validation-clean in all
six configurations (async/sync × occlusion on/off, forced fallback reduce, release).
Remaining from this finding: lightcull overlap (item 2), split submits (item 3), prelude
barrier merging.

`createLogicalDevice` requests graphics, compute, transfer, and present queues
(instanceInit.c:751–916), but the frame uses only `ctx.graphicsQueue`
(vulkanMaster.c:2690); `ctx.computeQueue` is referenced nowhere in the frame path. The
trace's "Compute In Flight" row is empty except at frame start. Consequences:

- All 29–30 dispatches interleave with raster on the graphics queue, each fenced by
  compute↔raster barriers on the same timeline (21× COMPUTE→COMPUTE barriers alone for
  the Hi-Z mip chains).
- The Hi-Z pyramid build runs on the critical path between view 0 and view 1's geometry
  (vulkanMaster.c:995–1167) — per view: a depth MAX-resolve render pass, ~11 mip
  dispatches with a memory barrier between each, plus 4 layout transitions. Its only
  consumer is next frame's cull (binding 11, previous-slot pyramids), and the occlusion
  test it feeds is default-off (`isOccluded` shows a single sample in the whole profile).
  Today this is ~0.4–0.5 ms of pure critical-path overhead for zero benefit until the H
  toggle ships enabled.
- The compute prelude (update → scatter → lightsetup → shadowsetup → cull, each followed
  by its own full memory barrier, vulkanMaster.c:383–504) is fully serial. lightsetup and
  shadowsetup are mutually independent — they could share one barrier — and the whole
  prelude for frame N+1 is independent of frame N's raster.

Because everything shares one queue, none of the natural overlaps can happen: shadow
raster (raster-unit heavy, SM light) never overlaps Hi-Z/light-cull compute (SM heavy,
raster idle); view 1's geometry never overlaps view 0's Hi-Z; frame N+1's prelude never
overlaps frame N's tail. Recommended order of adoption:

1. Move the Hi-Z build (both views) to the end of the frame or, better, onto
   `ctx.computeQueue`, synchronized with a timeline semaphore signaled after the last
   depth-writing pass and waited by next frame's cull. It leaves the critical path
   entirely; the per-mip barrier chain stops blocking raster. (LANDED — see measured
   outcome above; end-of-submit signal + lag-2 pyramid instead of a mid-frame split.)
2. Run each view's lightcull on the compute queue during the shadow region (its inputs —
   lightsetup output + camera UBO — are ready before shadows start; its consumer is that
   view's opaque pass).
3. Longer term: split the mega-submit into 2–3 submits with semaphores (prelude+shadow /
   views / post) so the CPU can also pipeline recording against GPU execution and an
   async queue has real submission-level concurrency to work with.

The MSAA color/id attachments shared between views (vulkanMaster.c:779–799) are a further
sequential coupling: view 1's entire geometry chain waits on view 0's resolve via the
COLOR→COLOR barrier. Per-view MSAA attachments (or rendering both views into layers of
one MSAA array image in one pass set) would let the two views' raster overlap — relevant
once view 1 is cheap (finding 6).

## 5. Finding 3 — full-fat mesh shader on depth-only passes (top occupancy limiter) - ADDRESSED

Measured outcome (2026-07-03, after finding 1 landed): ANO_DEPTH_ONLY compiles of
flat.mesh/flat.vert (invariant gl_Position, no user outputs) wired into the prepass and
shadow pipelines; shadow_depth.frag inputs slimmed to match. Validation-clean on both
geometry paths, no EQUAL-test speckle. Gain: lighting region −0.09 ms median (phase-
aligned A/B), shadow region unchanged — far below the estimate. Interpretation: the
24.6% ISBE launch stall was a symptom of the barrier-serialized regime (each isolated
micro-pass ramped occupancy from zero, so launch-side stalls dominated); with the
serialization gone, the depth passes are bounded by the mesh shader's input-side loads
(the LGSB-bound getLocalIndex byte-decode and AoS vertex pulls below) and fixed-function
raster, not by output allocation. The change is kept — strictly less ISBE/register/
attribute traffic, and the prerequisite for task-shader culling — but the remaining
levers re-rank: PiP resolution (finding 6) and MSAA policy (finding 5) are now the
largest wall-clock items, then async overlap (finding 2), then the mesh input loads.

`flat.mesh` writes five user attribute streams per vertex — `fragNormal` (vec3),
`fragTexCoord` (vec2), `outMaterialIndex` (flat uint), `fragWorldPos` (vec3),
`outEntityIndex` (flat uint) — plus `gl_Position`, for 64 vertices / 126 primitives per
workgroup (resources/shaders/flat.mesh:148–152, 271–299). The `shadowPass` specialization
constant switches only the projection matrix; the attribute writes and their ISBE
footprint are identical in every variant. The depth pre-pass pipeline (implementation 2,
instance/pipelines/flat.c:238–266) strips the fragment stage but reuses the same mesh
module, and the shadow pipeline does the same (pipeline.c:1125–1253).

Measured cost: VTG warp launches are stalled on ISBE allocation 24.6% of all SM cycles —
the single largest occupancy limiter in the frame — while mesh warps average only 1.5%
active. Depth-only draws (camera prepasses 1.39 ms + shadow casters 0.80 ms = 2.19 ms,
35.5% of all draw time) pay that full attribute cost with no consumer: the prepass has no
fragment shader at all, and `shadow_depth.frag` reads no vertex attributes. The
`normalMatrix = transpose(inverse(mat3(model)))` at flat.mesh:266 is likewise computed
per meshlet on depth passes that never use it.

There are already-compiled `flat_depth.mesh.spv` / `flat_depth.vert.spv` artifacts
sitting untracked in resources/shaders/, but no `flat_depth.*` source exists and nothing
in `src/` references them — the slim depth path was started and never wired. Because the
opaque pass depth-EQUAL-tests against prepass depth, the depth-only module must produce
bit-identical `gl_Position` arithmetic (same matrix order and precision), which a
position-only copy of flat.mesh satisfies.

Expected effect: ISBE allocation per depth workgroup drops several-fold (gl_Position only
versus gl_Position plus five user streams), directly attacking the 24.6% launch stall on
35% of the frame's draw time; it also shrinks the mesh stage's register footprint on
those passes (fewer live values per lane). One coupling to respect: shadow_depth.frag
deliberately declares the full location 0–4 input interface as a fallback-path driver
workaround (see its header comment) — the slim mesh module needs a matching slim fragment
variant so the interfaces stay linked on both geometry paths. This was the top lever
identified in the previous capture and remains so; it is the prerequisite for the
shadow-region compression in finding 1 to reach its floor, since after de-serialization
the shadow region becomes VTG-launch-bound.

## 6. Finding 4 — pixel-shader occupancy is register-launch-limited

`flat.frag` is 42.6% of all shader-profiler samples and runs at ~13 resident warps out of
48 (shader_pipelines.csv: 48 registers allocated, 43 live; avg warp latency 13,430
cycles; top stalls NOTSEL 27.9%, WAIT 19.1%, then LGSB). `transmission.frag` shows the
same shape (58 regs, 13 warps). Meanwhile PS warp launch is stalled on register
allocation 18.3% of all cycles. 48 regs/thread alone would allow ~42 warps, so the
register file is being shared: during every color pass the SM co-resides `flat.mesh`
warps at 55–63 registers × 64-lane workgroups with the PS warps; the two stages compete
for one register file, and the fat mesh stage (finding 3) is what makes that competition
lethal. The observable result: 20.5% pixel occupancy on a shader whose stall profile
(NOTSEL + WAIT + LGSB on dependent loads cluster→light→shadowInfo→frustum→atlas) is
exactly the kind that more resident warps would hide.

Levers, in order of leverage:

- Fix finding 3 first: a slim depth mesh module plus (for color passes) packed outputs
  shrinks VTG's register/ISBE share and directly raises the PS launch ceiling.
- Shrink the PS input interface: 5 attribute locations → 3 (octahedral-encode normal into
  2×f16, pack uv as 2×f16, merge materialIndex+entityIndex into one uint — both are flat
  and ≤16M). Fewer interpolants also cuts IPA work and TRAM.
- Register diet in flat.frag: the full `MaterialData mat = materialBuf.materials[i]`
  struct pull (a ~400-byte struct, flat.frag:224) invites the compiler to keep many dead
  fields live across the light loop; loading the four used fields into locals (or
  splitting a hot 32-byte material block) reliably drops allocation. Similarly `LightRuntime`
  is well-packed already; the shadow path's `shadowFrustumBuf.shadowFrustums[f].viewProj`
  (64 B dependent load per lit fragment, shadow_sample.glsl:24, LGSB 43% of that
  function's stalls) could come from a small UBO indexed by frustum since
  ANO_SHADOW_FRUSTUM_COUNT×64 B = 2.7 KB fits uniform space, converting an L1TEX
  dependent chain into constant-bank reads.
- If allocation still pins occupancy, `-maxrregcount`-equivalent via
  `VK_KHR_pipeline_executable_properties` inspection + spilling experiment is the blunt
  instrument; measure, since spills trade LGSB for occupancy.

## 7. Finding 5 — MSAA policy: maximum supported samples, everywhere - ADDRESSED

Measured outcome (2026-07-03): preferredMsaa config (default 4×, requestMsaaSamples /
getChosenMsaaSamples, ANO_MSAA env override for A/B, <2 clamps to 2) applied in
getMaxUsableSampleCount; color resolves collapsed to one per view (opaque/transmission
resolveMode NONE, additive AVERAGE — the MSAA surface already persisted across the three
passes). Mode-matched A/B at 2560×1368 SHADOWMAP: lighting 1.72→1.28 ms, total 2.82→2.28
ms (−19%); swapchain-class VRAM another −370 MiB. Validation-clean; 4× visually clean.
NOT taken here: per-view sample counts (rasterizationSamples is baked pipeline state —
needs duplicate pipeline sets) and the pick-id restructure (at 4× with one resolve its
remaining cost didn't justify the plumbing; revisit if a capture shows the id resolve).

`getMaxUsableSampleCount` picks the highest common sample count and `initVulkan` uses it
unconditionally (instanceInit.c:629–641, 742) — 8× on this class of hardware. Every
geometry pass in both views rasterizes at 8×: prepass depth, opaque HDR (RGBA16F) + pick
id (an 8× full-screen R32_UINT attachment, cleared and written in both views' opaque
passes), transmission, additive. Each of the three color passes per view ends with an AVERAGE resolve of the
shared MSAA color into the view's HDR target (g_framePasses resolveMode,
vulkanMaster.c:249–275), so the MSAA surface is resolved three times per view per frame;
the id target resolves SAMPLE_ZERO for view 0. Downstream, the Hi-Z reduce had to grow a
MAX depth-resolve path because the source depth is 8× (the earlier hiz.comp capture
showed per-sample fetches as its top stall), and the CROP/ZROP/VRAM percentages carry a
permanent ~4–8× write-amplification tax. This is a large fixed multiplier on exactly the
passes findings 1 and 3 make cheap.

Recommendations: make sample count a setting with a sane default (4× is the visual
sweet spot; 2× for the PiP view), and special-case the pick id attachment — a 1-texel
readback does not need an 8×MSAA full-screen R32_UINT attachment + clear + resolve in
both views. Options: render ids in a 1× thin pass over the prepass depth, or scissor the
id attachment write to a small rect around the cursor. Resolving color once at the end of
the additive pass (loadOp LOAD on the MSAA surface between passes, resolve only in the
last) would also drop two of the three per-view resolves; transmission/additive draw so
few pixels that per-pass resolve is nearly pure overhead.

## 8. Finding 6 — the PiP view renders at full resolution - ADDRESSED

Measured outcome (2026-07-03): viewExtent[v] plumbed through image creation (per-view
depth/HDR/Hi-Z/depth-resolve AND per-view MSAA color+id — the shared-attachment coupling
barrier between views is gone), viewports/renderAreas, per-view UBO screen dims + aspect,
per-view Hi-Z dims in the cull UBO, and the resize path. Phase-aligned A/B at 2560×1368:
lighting 2.80→1.78 ms median (−36%), composite 0.060→0.040 ms, frame total 3.90→2.83 ms
(−27%); swapchain-class VRAM −372 MiB net. Bonus effects: view 1's screen-area cull and
LOD thresholds now operate in inset pixels (coarser LODs, fewer triangles), and the inset
composites 1:1 instead of 3:1-minified (sharper). Validation-clean including a live
resize; shadows and inset visually correct.

All ViewResources — depth, HDR color, Hi-Z pyramid, pick-resolve — are created at
swapchain extent for every view (instanceInit.c:1596–1763), every per-view pass sets
renderArea/viewport to the full extent (vulkanMaster.c:866–890), and the composite then
draws view 1 into a W/3 × H/3 inset (vulkanMaster.c:1197–1219). View 1 costs 1.52 ms of
draw time (prepass 0.79 + opaque 0.55 + transparency 0.18) plus its own full-res Hi-Z
build — ~25% of the frame — to produce 1/9th of the screen's pixels, minified.

Rendering view 1 at inset resolution cuts its pixel-dependent cost ~9× (opaque PS, ZROP,
resolve, lightcull froxel volume, Hi-Z pyramid) and improves the inset's image quality
(proper 1:1 sampling instead of 3:1 minification through a bilinear tap). Its
geometry-side cost (mesh/ISBE) does not shrink with resolution — which is finding 3's
territory and another reason the slim mesh path matters. Estimated recovery: 0.8–1.1 ms.
Per-view extents also unlock per-view MSAA (finding 5) and de-coupling the shared MSAA
attachments (finding 2).

## 9. Finding 7 — no backface or cluster culling in the raster path

`rasterizer.cullMode = VK_CULL_MODE_NONE` in both the flat family (flat.c:113) and the
shadow pipeline (pipeline.c:1189 region), so every pass rasterizes both faces of every
triangle. The per-meshlet backface cone cull in flat.mesh is commented out ("STOPGAP…
still broken, skip until task shaders", flat.mesh:186–250), and there is no task stage —
cull.comp emits `groupCountX = meshletCount` per surviving entity (cull.comp:219), so
every meshlet of every visible entity launches a mesh workgroup in every pass it appears
in (2 camera partitions + up to 26 shadow partitions). Sponza is authored single-sided;
`MaterialData.doubleSided` exists but never reaches pipeline state.

For the depth-only passes this roughly doubles rasterized area and Z traffic, and for the
mesh stage it doubles post-VTG raster feed. Given depth-only work is 35% of draw time and
ISBE-starved, the cheap half of this fix (cullMode BACK for opaque/prepass/shadow, keep
NONE for the blended lanes; two rasterization variants selected by material class, or
`VK_EXT_extended_dynamic_state`'s vkCmdSetCullMode) is worth taking before the expensive
half (task-shader cluster cull with per-meshlet cone + frustum + Hi-Z tests, which
remains the canonical design and also fixes the launch-per-culled-meshlet waste).

## 10. Finding 8 — shadow atlas lifetime: triple-buffered, rebuilt from scratch

ShadowResources lives inside FrameResources (structs.h:746–764, 933), so the 512² ×
84-layer RGBA16 atlas and its blur temp exist once per frame-in-flight: 176 MB × 2 images
× 3 slots ≈ 1.05 GB of VRAM for shadow storage, of which two-thirds is stale copies. Every
frame re-renders and re-blurs all 26 active frustums into its slot from UNDEFINED, even
when neither lights nor casters moved (this trace is a static scene: the entire 0–2.1 ms
region is redundant work in steady state).

Because each slot starts UNDEFINED, temporal reuse is structurally impossible as built.
Moving to a single shared atlas (safe on one in-order queue; the same cross-frame WAR
technique already used for the Hi-Z pyramids applies) enables:

- dirty-frustum tracking: re-render a frustum only when its light transform, caster set,
  or LOD selection changed (the render already knows all three on the CPU side);
- caster-motion budgets: cap re-rendered frustums per frame, amortizing dynamic scenes;
- ~700 MB VRAM back, which also relieves L2 working-set pressure (L2 hit rate is 71%).

This was flagged as the second lever in the previous capture ("temporal shadow cache");
the per-frame-in-flight allocation is the structural blocker to land it.

## 11. Finding 9 — CPU-side dispatch and smaller items

- Recording is single-threaded on the main thread (main.c:397–401) and re-records ~1,230
  commands per frame into one command buffer. At current scale this is not the frame
  limiter (GPU 6.17 ms), but nearly half those commands are the shadow region's barrier
  choreography — finding 1 removes ~300 barriers, ~100 render-pass begins/ends, and ~100
  descriptor/pipeline binds per frame as a side effect. The threads module exists if
  multi-CB parallel recording is ever needed; split submits (finding 2) are the
  prerequisite.
- `vkCmdFillBuffer` zeroes the entire indirect buffer every frame — stride × capacity ×
  partition count (vulkanMaster.c:397–398), several MB against maxEntities=10000 × 46
  partitions — where only the drawCount buffer (184 B) actually needs zeroing on the
  indirect-count path; unwritten commands past the count are never read. Keep the full
  fill only for the non-indirect-count fallback.
- lightcull.comp re-derives each light's world position per froxel per light via a
  transforms[] mat4 load and multiply (lightcull.comp:117–127) even though
  lightsetup.comp precomputed exactly this into LightRuntime for the fragment passes. It
  shows the worst warp latency in the profile (avg 26.7k cycles, LGSB 58%). Small in
  absolute terms (2 × ~0.03 ms) but a free 2× on a pass that will multiply with froxel
  count and view count; read the precomputed pose instead.
- The compute prelude issues one full memory barrier per dispatch (5 serial stages);
  lightsetup+shadowsetup are independent and can share one barrier, and the prelude's
  barriers can name only the stages that actually consume each output (they mostly do
  already).
- `vulkanSettings.preferredMode` defaults to `0b111111111` (vulkanConfig.c:12), which
  matches no `VkPresentModeKHR`, so `chooseSwapPresentMode` silently falls back to FIFO.
  Works, but it means present mode is vsync-by-accident rather than by choice; pick an
  explicit default (FIFO for correctness, MAILBOX for latency when available).
- Pick readback is fence-gated two frames later (ano_collect_pick) — no CPU stall; fine.
  The mid-frame TRANSFER round-trip barriers on the pick image (vulkanMaster.c:947–974)
  would ride along to the end of the view loop naturally if the id target becomes 1×
  (finding 5).
- `vkAcquireNextImageKHR` runs before recording with UINT64_MAX (vulkanMaster.c:2646);
  under FIFO with 3 frames in flight this is where back-pressure lands. Harmless today;
  worth revisiting only when submit splitting changes the frame's shape.

## 12. Priorities

Gains are estimates against the 6.17 ms frame and are not additive — several findings
gate each other (1 exposes 3; 5 and 6 multiply; 2 hides whatever remains).

| # | Change | Complexity | Est. gain |
|---|---|---|---|
| 1 | Batch shadow barriers, per-frustum depth slices, layered blur passes | LANDED 2026-07-02 | measured: shadow region 1.98 → 0.83–0.95 ms (see finding 1 note) |
| 2 | Slim depth-only mesh module (wire flat_depth) for prepass + shadow | LANDED 2026-07-03 | measured ~0.1 ms (ISBE premise died with #1; see finding 3 note) |
| 3 | Render PiP view at inset resolution | LANDED 2026-07-03 | measured 1.07 ms + 372 MiB VRAM (see finding 6 note) |
| 4 | MSAA: setting + 4× default; resolve once (pick-id restructure deferred) | LANDED 2026-07-03 | measured 0.53 ms + 370 MiB VRAM (see finding 5 note) |
| 5 | Hi-Z build off critical path (end of frame or async queue) | LANDED 2026-07-03 (async queue + timelines) | measured ~0.06 ms — chain already shrunk by #3/#4; occlusion consumer now ~free (see finding 2 note) |
| 6 | Backface culling on opaque/prepass/shadow | Low (pipeline state; verify Sponza winding) | 0.2–0.5 ms of depth-only raster |
| 7 | Single shared shadow atlas + dirty-frustum reuse | Medium-high (lifetime + invalidation rules) | up to ~1.5 ms steady-state static; ~700 MB VRAM |
| 8 | Async lightcull, merged prelude barriers, drawCount-only fill, lightcull pose reuse | Low each | 0.1–0.2 ms + scalability |
| 9 | flat.frag register diet + packed interpolants | Medium (measure per step) | raises PS occupancy; frame gain realized with #1/#2 |
| 10 | Task-shader cluster cull (cone + frustum + Hi-Z per meshlet) | High | large at scale; the million-entity end-state |

Sequencing note: 1 → 2 → 6 all attack the same shadow+prepass block and are independent
of each other's code paths; 3 and 4 are independent and can land any time; 5 and 8 are
the first steps toward the async-compute shape that 10 eventually needs. 7 changes
steady-state cost rather than worst-case cost and is best landed after 1 so the dirty
path reuses the batched-barrier structure.

## Appendix — how the numbers were derived

- Region/event sums: `profile/EventList.csv` (GPU start/end/duration per API call),
  aggregated by call type and time window; `<0.01ms` durations counted as 5 µs.
- Barrier census: stage-mask signatures parsed from each `vkCmdPipelineBarrier`
  description; counts match the per-frustum/per-sublayer loop structure in
  recordCommandBuffer exactly (26 active + 16 inactive frustums, 2 sublayers, 2 blur
  passes).
- Occupancy/stall metrics: `profile/analysis.yaml` (GPU Trace advanced metrics) and
  `profile/shader_pipelines.csv` (per-pipeline register/warp/stall profile).
- Hotspots: `profile/hotspots.csv`, `top_down.csv`/`bottom_up.csv`; per-line SASS-sample
  CSVs under `profile/shaders/` corroborate the per-shader stall mix.
- Draw-time split: 34 indirect mesh draws = 4.075 ms total → shadow 0.795, prepasses
  1.39 (0.60 + 0.79), opaques 1.54 (0.99 + 0.55), transparency 0.35; depth-only share
  2.19 ms = 35.5% of draw time.
