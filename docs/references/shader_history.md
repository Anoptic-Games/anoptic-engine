# GPU Shader Stages 〜 Reference & History

A map of every programmable shader stage: what it does, which stages can coexist
in a single pipeline, which are strictly mutually exclusive, when each first shipped
on NVIDIA, and when each became usable on Apple Silicon.

> Scope note: the "Apple Silicon usable" column is the native Metal capability.
> Because the Anoptic Engine talks Vulkan, the practical gate is the translation
> layer (MoltenVK today, KosmicKrisp later) 〜 see [Notes for the Anoptic Engine](#notes-for-the-anoptic-engine).

---

## The pipeline topologies

Parentheses `( )` mark optional stages.

### ① Classic graphics pipeline

<pre style="font-family:Menlo,'DejaVu Sans Mono',monospace;font-size:10pt;line-height:1.2;white-space:pre;background:#fff;border:1px solid #e2e2e2;border-radius:6px;padding:14pt 16pt;margin:12pt 0;overflow-x:auto;color:#111;">
               ┌─────────────────┐
               │ Input Assembler │
               └─────────────────┘
                        ↓
                 ┌────────────┐
                 │   VERTEX   │   required
                 └────────────┘
                        ↓
  ┌────────────────────────────────────────────┐
  │ ( Tess Control → Tessellator → Tess Eval ) │   optional pair
  └────────────────────────────────────────────┘
                        ↓
                 ┌──────────────┐
                 │  (Geometry)  │   optional
                 └──────────────┘
                        ↓
                 ┌──────────────┐
                 │  Rasterizer  │
                 └──────────────┘
                        ↓
                 ┌────────────┐
                 │  FRAGMENT  │
                 └────────────┘
                        ↓
                  ┌──────────┐
                  │  Output  │
                  └──────────┘
</pre>

### ② Mesh graphics pipeline 〜 the modern geometry front-end

<pre style="font-family:Menlo,'DejaVu Sans Mono',monospace;font-size:10pt;line-height:1.2;white-space:pre;background:#fff;border:1px solid #e2e2e2;border-radius:6px;padding:14pt 16pt;margin:12pt 0;overflow-x:auto;color:#111;">
 ┌────────┐   ┌──────┐   ┌────────────┐   ┌──────────┐
 │ (Task) │ ▶ │ MESH │ ▶ │ Rasterizer │ ▶ │ FRAGMENT │
 └────────┘   └──────┘   └────────────┘   └──────────┘
</pre>

Task is the amplification stage 〜 optional; Mesh is required.

### ③ Compute pipeline 〜 standalone, no rasterizer

<pre style="font-family:Menlo,'DejaVu Sans Mono',monospace;font-size:10pt;line-height:1.2;white-space:pre;background:#fff;border:1px solid #e2e2e2;border-radius:6px;padding:14pt 16pt;margin:12pt 0;overflow-x:auto;color:#111;">
 ┌────────────────────────────────────────────────┐
 │  COMPUTE  〜  dispatch X × Y × Z workgroups     │
 └────────────────────────────────────────────────┘
</pre>

### ④ Ray-tracing pipeline 〜 standalone

<pre style="font-family:Menlo,'DejaVu Sans Mono',monospace;font-size:10pt;line-height:1.2;white-space:pre;background:#fff;border:1px solid #e2e2e2;border-radius:6px;padding:14pt 16pt;margin:12pt 0;overflow-x:auto;color:#111;">
 ┌────────────────┐
 │ RAY GENERATION │   traceRay()
 └────────┬───────┘
          ↓
   traverse acceleration structure
          │
          ├──▶ hit  ──▶ (Intersection) ──▶ (Any-Hit) ──▶ Closest-Hit
          │
          └──▶ miss ──▶ Miss

 Callable 〜 invoked on demand by Ray Gen / Closest-Hit / Miss
            (a subroutine)
</pre>

---

## Coexistence vs. mutual exclusion

A single graphics pipeline picks one geometry front-end. The classic group and the
mesh group are **strictly mutually exclusive**; the fragment stage is shared.

<pre style="font-family:Menlo,'DejaVu Sans Mono',monospace;font-size:10pt;line-height:1.2;white-space:pre;background:#fff;border:1px solid #e2e2e2;border-radius:6px;padding:14pt 16pt;margin:12pt 0;overflow-x:auto;color:#111;">
   CLASSIC front-end     ⇔ strict mutex ⇔     MESH front-end
   ─────────────────                          ──────────────
   Vertex      (required)                     Mesh   (required)
   + Tess Control ┐                           + Task (optional)
   + Tess Eval    ┘ both/neither
   + Geometry  (optional)
            │                                   │
            └─────────────────┬─────────────────┘
                              ↓
                         Rasterizer
                              ↓
                 FRAGMENT  (shared by both)
</pre>

Rules:

- Within the classic path, all of these can be live at once as separate stages:
  Vertex + Tess Control + Tess Eval + Geometry + Fragment (up to 5).
- Tess Control + Tess Eval are all-or-nothing 〜 include both or neither.
- Classic and Mesh front-ends are strictly mutex. A pipeline is either
  {Vertex, Tess, Geometry} or {Task, Mesh} 〜 never both. (That's the crux of the
  lighting-branch problem.)
- Fragment is shared 〜 it sits after the rasterizer regardless of front-end.
- Compute is its own pipeline; it can never be a stage beside graphics stages.
- Ray-tracing stages coexist with each other in an RT pipeline, but not with
  raster / mesh / compute.

---

## What each stage means + history

| Shader | What it does (briefly) | NVIDIA first (year, GPU) | Apple Silicon usable (year, OS) |
|---|---|---|---|
| Vertex | Transforms one input vertex (position → clip space), passes varyings down. The classic geometry entry point. | 2001, GeForce 3 (NV20) | 2020, macOS 11 (M1) 〜 native Metal |
| Fragment (Pixel) | Shades one rasterized fragment → color/depth. The classic pixel output. | 2001, GeForce 3 (PS 1.1) | 2020, macOS 11 (M1) 〜 native Metal |
| Geometry | Runs per primitive; can discard / duplicate / amplify primitives (e.g. point → quad). Powerful but slow; widely deprecated. | 2006, GeForce 8800 GTX (G80) | **Never** 〜 Metal has no GS stage; unsupported on MoltenVK and KosmicKrisp |
| Tess Control (Hull) | Runs per patch control-point; outputs tessellation factors (how finely to subdivide). | 2010, GeForce GTX 480 (Fermi) | 2020, macOS 11 (M1) 〜 Metal uses a compute-based model; MoltenVK emulates via compute |
| Tess Eval (Domain) | Runs per tessellator-generated vertex; positions the new vertices on the patch. | 2010, GeForce GTX 480 (Fermi) | 2020, macOS 11 (M1) 〜 same compute-emulated caveat |
| Compute | General-purpose data-parallel kernel; no fixed-function graphics. Culling, simulation, image processing. | 2006–07, GeForce 8800 (G80) + CUDA | 2020, macOS 11 (M1) 〜 native Metal |
| Task (Amplification) | Optional pre-stage to Mesh; decides how many mesh workgroups to launch (LOD, cluster culling). | 2018, RTX 2080 (Turing, `NV_mesh_shader`) | Native Metal 3: 2022, macOS 13 (M1+); via Vulkan not yet (no MoltenVK support; KosmicKrisp / macOS 26 pending) |
| Mesh | Replaces Vertex + Tess + Geometry. A compute-like workgroup that emits a meshlet (≤ ~64 verts / 126 prims) directly, with on-GPU per-meshlet culling. | 2018, RTX 2080 (Turing) | Native Metal 3: 2022, macOS 13 (M1+); via Vulkan not yet |
| Ray Generation | Entry point of an RT dispatch; casts primary rays, writes the image. | 2018, RTX 2080 (Turing, RT cores) | HW-accelerated 2023, macOS 14 (M3); not via MoltenVK |
| Intersection | Custom ray ↔ primitive hit test (for non-triangle geometry). | 2018, RTX 2080 | 2023, macOS 14 (M3) |
| Any-Hit | Runs at every potential hit along a ray (e.g. alpha-test transparency). | 2018, RTX 2080 | 2023, macOS 14 (M3) |
| Closest-Hit | Runs at the nearest confirmed hit; does the actual shading / lighting. | 2018, RTX 2080 | 2023, macOS 14 (M3) |
| Miss | Runs when a ray hits nothing (sky / environment). | 2018, RTX 2080 | 2023, macOS 14 (M3) |
| Callable | A shader invoked on-demand by other RT shaders (like a GPU subroutine). | 2018, RTX 2080 | 2023, macOS 14 (M3) |

---

## Notes for the Anoptic Engine

1. "Apple Silicon usable" = native Metal capability. Since the engine talks Vulkan,
   the real gate is the translation layer. Native Metal has mesh shaders since
   Ventura (2022), but MoltenVK does not expose `VK_EXT_mesh_shader` 〜 so a
   mesh-shader-based renderer cannot run on macOS Sonoma despite the hardware being
   capable. This is the KosmicKrisp / macOS 26 dependency.
2. Geometry shaders are a dead end on Apple 〜 no Metal stage, no MoltenVK, no
   KosmicKrisp. Any `geometryShader` device-feature requirement is therefore both
   cruft and actively hostile to the macOS target if nothing actually uses the stage.
