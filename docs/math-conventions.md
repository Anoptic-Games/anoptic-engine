# Renderer math and geometry conventions

The single source of truth for coordinate, matrix, depth, winding, and frustum conventions across
the renderer (CPU `vertex.c` math + every GLSL shader + `shadowsetup.comp`). Sign and storage
ambiguities here have already cost real bugs (a transposed shadow frustum, a ground plane lit on the
wrong face); when you add math, conform to this or change this doc with it.

Status: descriptive — it documents what the code does *today*. Two former inconsistencies (the
camera using OpenGL `[-1,1]` depth, and normals using `mat3(model)` instead of the inverse-transpose)
have been corrected; the historical notes below record what changed so the next person doesn't
reinstate them.

## Matrices

- Type: `typedef float mat4[4][4]` (`include/anoptic_math.h`). **Column-major**: `m[i][j]` is
  **column i, row j**. So `m[c]` is column c, and `m[3]` (CPU `m[3][0..2]`) is the translation.
  This is GLSL's native `mat4` layout, so a `mat4` uploaded byte-for-byte is read identically by a
  shader — no transpose on upload.
- Vectors are columns; transforms apply as `M * v` (pre-multiply). Composition is left-to-right in
  application order read right-to-left: `clip = proj * view * worldPos`, `worldPos = model * vLocal`.
- `multiplyMat4(out, A, B)` computes `out = A * B`. So `multiplyMat4(viewProj, proj, view)` is
  `proj * view` — the correct clip transform.
- A transform's columns: `m[0..2]` = the basis (rotation ⊗ scale), `m[3]` = translation (w=1).
  Column 2 (`m[2]`) is the local +Z axis in world space.

## Coordinate system

- World space is **right-handed**. `lookAt` (`vertex.c:125`) builds a standard RH view: forward
  `f = normalize(center - eye)`, right `s = normalize(f × up)`, up `u = s × f`, and the view looks
  down **−Z** (`m[*][2] = -f`). +Y is up, world is unflipped.
- A renderable's **local forward is −Z = `-m[2]`** (the negated third column). This is the engine
  convention for orientation; the cull/animation system and lights all assume it.

## Clip space, NDC, depth (Vulkan)

- Target is Vulkan: clip `x,y ∈ [-1,1]` with **+Y down**, and the framebuffer depth range is
  `[0,1]`. The camera `perspective` applies the Y-flip via `m[1][1] = -1/tan` (`vertex.c`);
  geometry never flips Y again.
- Depth is **Vulkan `[0,1]` (ZO)** everywhere. `perspective()` uses the ZO form
  `m[2][2]=far/(near-far)`, `m[3][2]=(far·near)/(near-far)` (RH, looking down −Z): `z_view=−near`
  maps to NDC z 0, `z_view=−far` to 1. It previously used the OpenGL `[-1,1]` form
  `m[2][2]=(far+near)/(near-far)`, `m[3][2]=2·far·near/(near-far)`, which under the `[0,1]` viewport
  over-clipped the near range (anything closer than ≈2×near) and halved depth precision — corrected.
  If you re-derive a projection, stay ZO.
- The shadow path is also ZO: `shadowsetup.comp`'s `orthoRH_ZO` maps z to `[0,1]` and does **not**
  Y-flip (`m[1][1] > 0`). The shadow render and the shadow sample share that one matrix, so the
  missing flip cancels (render and read agree). Don't add a Y-flip to the shadow ortho.
- Depth test is `LESS`, depth cleared to `1.0` (far). No reverse-Z. Shadow bias is applied in the
  depth-only pipeline (`depthBiasConstantFactor 1.5`, `slopeFactor 2.5`) plus a slope-scaled bias in
  the PCF sample.

## Frustum planes (Gribb-Hartmann)

- The 6 planes are the **rows** of the clip matrix `VP`: `left = row3 + row0`, `right = row3 − row0`,
  `bottom = row3 + row1`, `top = row3 − row1`, `far = row3 − row2`, each normalized by its xyz
  length. Normals point **inward**.
- **Near plane is Vulkan ZO**: `near = row2` alone (`clip.z ≥ 0`), NOT `row3 + row2` (that is the
  OpenGL `[-1,1]` near). This matches the ZO `perspective()` / `orthoRH_ZO`. Both
  `extractFrustumPlanes` (CPU camera) and `shadowsetup.comp` (GPU shadow) extract `row2`; a `row3 + row2`
  near on a ZO matrix is a loose plane that under-culls behind the camera.
- Test: a sphere `(center, radius)` is **outside** iff `dot(plane, vec4(center,1)) < -radius` for any
  plane. (`cull.comp` / the CPU `extractFrustumPlanes`.)
- CPU extraction (`vertex.c:214`) reads rows correctly: with column-major `m[col][row]`, the term
  `viewProj[k][3]` swept over `k=0..3` *is* row 3. 
- GOTCHA (cost us the shadow cull): in GLSL, `m[i]` is **column** i, not row i. Row i is
  `transpose(m)[i]`. Extracting `m[3] ± m[i]` directly yields the **transposed** frustum — a
  different, skewed volume that wrongly culls off-center objects. Always `transpose` first (or index
  rows explicitly): `shadowsetup.comp` does `mat4 t = transpose(viewProj); planes = t[3] ± t[k];`.
- Parity requirement: any GPU-derived frustum (shadow maps) and the CPU-derived camera frustum are
  tested by the *same* `cull.comp` comparison, so both must produce planes in this exact convention.

## Lights

- A light carries no stored position/direction; both are derived from its driving entity's live
  transform (`transforms[light.transformIndex]`), so GPU animation applies.
- World position = `m[3].xyz`. **Forward (travel direction) = `normalize(-m[2].xyz)`** (local −Z).
  For a directional or spot light, set column 2 to the **negated** travel direction; translation in
  column 3. In the shader, the surface→light vector is `L = -forward` (directional) or
  `normalize(lightPos - fragWorldPos)` (point/spot).

## Winding, faces, and normals — the scale-sign footgun

- Front face is **counter-clockwise** (`VK_FRONT_FACE_COUNTER_CLOCKWISE`). The geometry pipelines
  (flat, transmission, shadow depth) use `cullMode = NONE` (double-sided), so winding does not cull —
  but it still determines the **geometric** front for anything that reads `gl_FrontFacing`, and it
  interacts with scale sign below.
- The projection Y-flips (`m[1][1] < 0`) while the viewport has positive height, so screen-space
  winding is reversed relative to OpenGL. The convention is **self-consistent**: `frontFace = CCW`
  plus a glTF asset (CCW front faces, outward normals) gives a correct `gl_FrontFacing` on the visible
  surface, which is why the loaded meshes are right as-is. Do NOT flip `frontFace` to CLOCKWISE to
  "fix" one mesh — it inverts `gl_FrontFacing` for *every* mesh and regresses the ones that already
  work. A mesh that needs a mirror is itself wound backward; fix the mesh, not the pipeline.
- The procedural fallback cube (mesh 0, also the failed-load fallback) was originally wound the
  *opposite* way and so disagreed with the glTF assets — visible only on the flat ground box, whose
  top face came out back-facing. Fixed by reversing the cube's triangle winding to match glTF
  (`vulkanMaster.c` `createFallbackResources`); the ground now uses all-positive scale with no mirror.
- Normals are transformed by the **inverse-transpose normal matrix**
  `transpose(inverse(mat3(model)))` (`flat.vert`, `flat.mesh`) — correct under non-uniform, sheared,
  and negative scale. It equals `mat3(model)` up to scale for rotation + uniform scale (the common
  case), and for axis-aligned normals under a *diagonal* scale even the direction is unchanged; the
  fix only moves normals under rotation combined with non-uniform scale. (Previously the code used
  `mat3(model)` directly, which skewed those.) In `flat.mesh` the matrix is hoisted out of the vertex
  loop — it is per-entity, not per-vertex.
- HAZARD: **winding** flips under a negative-determinant (mirrored) basis (any odd number of negated
  scale axes). The geometry pipelines use `cullMode = NONE`, so a flipped winding does not drop the
  triangle, but it flips `gl_FrontFacing`, and `flat.frag` does `if (!gl_FrontFacing) N = -N` for
  double-sided surfaces — so a mirrored instance can read an inverted normal there. A single negated
  scale axis on an entity transform is therefore a *mirror*, not a scale, and will invert its facing;
  use it only deliberately. When a flat surface renders unlit/inside-out, suspect winding
  (`gl_FrontFacing`) and the source mesh's authored winding before the lighting or normal math.
- The fallback cube carries a constant `(1,1,1)` vertex normal on all 8 corners (no per-face normals).
  Under the inverse-transpose, a thin box scales that toward its thinnest axis, so the ground reads
  ~`+Y` — convenient, but a non-thin fallback cube shades with a single diagonal normal. Real per-face
  normals (24 verts) would be the robust fix if the fallback ever needs to look right at any scale.

## Vertex format

- `PackedVertex` (32 bytes, std430): `float px,py,pz; float nx,ny,nz; float u,v;` — position,
  normal, UV. The geometry stages pull vertices programmatically by `gl_VertexIndex` (vertex path)
  or meshlet indices (mesh path) from one shared mega-buffer; `vertexOffset` is in 32-byte units.

## Screen / froxel space

- Fragment screen position is `gl_FragCoord.xy` in physical pixels (`imageExtent`), origin top-left.
- Clustered-forward froxels: screen tiles `X×Y` from `gl_FragCoord / screenSize`, depth slice
  logarithmic between `cameraNear..cameraFar` (`slice = log(z/near)/log(far/near)`), `Z` slices.
  View-space depth for the slice is `-(view * worldPos).z` (view looks down −Z, so negate).

## Stage-interface rule (shaders)

- A fragment shader's `in` interface must match the bound geometry stage's `out` interface, even for
  a depth-only pass that ignores the values. The shared `flat.vert`/`flat.mesh` output locations 0–4
  unconditionally; `shadow_depth.frag` therefore declares the same 0–4 inputs (ignored), or a driver
  may drop the geometry stage's rasterizer output on the mismatch (it did, on the vertex path).

## Standing recommendation

The recurring failure mode is the same: a convention re-implemented in a second place (GLSL vs CPU,
shadow vs camera) with a silent sign/transpose/range difference. The durable fix is **shared, tested
helpers** — one `lookAt`/`ortho`/`perspective`/`extractPlanes` each, used by both the CPU and any
shader codegen, with a unit test asserting CPU and GPU agree on a known frustum. Until that exists,
this doc is the contract; cite it in review when math is added.
