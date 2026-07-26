# Renderer math and geometry conventions

Source of truth for coordinate, matrix, depth, winding, and frustum conventions across the renderer (CPU `vertex.c` math + every GLSL shader + `shadowsetup.comp`). Add math that matches this, or update this with the change.

Status: descriptive. Documents what the code does *today*. Corrected: camera depth is Vulkan ZO `[0,1]` (was OpenGL `[-1,1]`); normals use inverse-transpose (was `mat3(model)`). Historical notes below record the old forms so they are not reinstated.

## Matrices

- Type: `typedef float mat4[4][4]` (`include/anoptic_math.h`). **Column-major**: `m[i][j]` is **column i, row j**. So `m[c]` is column c, and `m[3]` (CPU `m[3][0..2]`) is the translation. Matches GLSL `mat4`; upload byte-for-byte, no transpose.
- Vectors are columns; transforms apply as `M * v` (pre-multiply). Composition left-to-right in application order, read right-to-left: `clip = proj * view * worldPos`, `worldPos = model * vLocal`.
- `multiplyMat4(out, A, B)` computes `out = A * B`. So `multiplyMat4(viewProj, proj, view)` is `proj * view`.
- A transform's columns: `m[0..2]` = the basis (rotation ⊗ scale), `m[3]` = translation (w=1). Column 2 (`m[2]`) is the local +Z axis in world space.

## Coordinate system

- World space is **right-handed**. `lookAt` (`vertex.c:125`) builds a standard RH view: forward `f = normalize(center - eye)`, right `s = normalize(f × up)`, up `u = s × f`, and the view looks down **−Z** (`m[*][2] = -f`). +Y is up, world is unflipped.
- A renderable's **local forward is −Z = `-m[2]`** (the negated third column). Cull/animation and lights assume this.

## Clip space, NDC, depth (Vulkan)

- Target is Vulkan: clip `x,y ∈ [-1,1]` with **+Y down**, framebuffer depth range `[0,1]`. Camera `perspective` applies the Y-flip via `m[1][1] = -1/tan` (`vertex.c`); geometry never flips Y again.
- Depth is **Vulkan `[0,1]` (ZO)** everywhere. `perspective()` uses the ZO form `m[2][2]=far/(near-far)`, `m[3][2]=(far·near)/(near-far)` (RH, looking down −Z): `z_view=−near` maps to NDC z 0, `z_view=−far` to 1. Old OpenGL `[-1,1]` form was `m[2][2]=(far+near)/(near-far)`, `m[3][2]=2·far·near/(near-far)`. Re-derive projections as ZO.
- Shadow path is also ZO: `shadowsetup.comp`'s `orthoRH_ZO` maps z to `[0,1]` and does **not** Y-flip (`m[1][1] > 0`). Shadow render and sample share that matrix. Don't add a Y-flip to the shadow ortho.
- Depth test is `LESS`, depth cleared to `1.0` (far). No reverse-Z. Shadow bias: depth-only pipeline (`depthBiasConstantFactor 1.5`, `slopeFactor 2.5`) plus slope-scaled bias in the PCF sample.

## Frustum planes (Gribb-Hartmann)

- The 6 planes are the **rows** of the clip matrix `VP`: `left = row3 + row0`, `right = row3 − row0`, `bottom = row3 + row1`, `top = row3 − row1`, `far = row3 − row2`, each normalized by its xyz length. Normals point **inward**.
- **Near plane is Vulkan ZO**: `near = row2` alone (`clip.z ≥ 0`), NOT `row3 + row2` (OpenGL `[-1,1]` near). Matches ZO `perspective()` / `orthoRH_ZO`. Both `extractFrustumPlanes` (CPU camera) and `shadowsetup.comp` (GPU shadow) extract `row2`; a `row3 + row2` near on a ZO matrix under-culls behind the camera.
- Test: a sphere `(center, radius)` is **outside** iff `dot(plane, vec4(center,1)) < -radius` for any plane. (`cull.comp` / the CPU `extractFrustumPlanes`.)
- CPU extraction (`vertex.c:214`) reads rows correctly: with column-major `m[col][row]`, the term `viewProj[k][3]` swept over `k=0..3` *is* row 3.
- GOTCHA (cost us the shadow cull): in GLSL, `m[i]` is **column** i, not row i. Row i is `transpose(m)[i]`. Extracting `m[3] ± m[i]` directly yields the **transposed** frustum. Always `transpose` first (or index rows explicitly): `shadowsetup.comp` does `mat4 t = transpose(viewProj); planes = t[3] ± t[k];`.
- Parity: GPU-derived frustum (shadow maps) and CPU-derived camera frustum both feed the same `cull.comp` comparison; both must use this exact plane convention.

## Lights

- A light carries no stored position/direction; both derive from its driving entity's live transform (`transforms[light.transformIndex]`).
- World position = `m[3].xyz`. **Forward (travel direction) = `normalize(-m[2].xyz)`** (local −Z). For directional or spot: set column 2 to the **negated** travel direction; translation in column 3. In the shader, surface→light is `L = -forward` (directional) or `normalize(lightPos - fragWorldPos)` (point/spot).

## Winding, faces, and normals (scale-sign footgun)

- Front face is **counter-clockwise** (`VK_FRONT_FACE_COUNTER_CLOCKWISE`). Geometry pipelines (flat, transmission, shadow depth) use `cullMode = NONE` (double-sided). Winding still sets the **geometric** front for `gl_FrontFacing`, and interacts with scale sign below.
- Projection Y-flips (`m[1][1] < 0`) while viewport height is positive, so screen-space winding is reversed relative to OpenGL. Convention is **self-consistent**: `frontFace = CCW` plus a glTF asset (CCW front faces, outward normals) gives correct `gl_FrontFacing`. Do NOT flip `frontFace` to CLOCKWISE. A mesh that needs a mirror is wound backward; fix the mesh, not the pipeline.
- Procedural fallback cube (mesh 0 / failed-load fallback) was wound opposite glTF; fixed by reversing its triangle winding (`vulkanMaster.c` `createFallbackResources`). Ground uses all-positive scale, no mirror.
- Normals use the **inverse-transpose normal matrix** `transpose(inverse(mat3(model)))` (`flat.vert`, `flat.mesh`). Equals `mat3(model)` up to scale for rotation + uniform scale; old code used `mat3(model)` directly. In `flat.mesh` the matrix is hoisted out of the vertex loop (per-entity, not per-vertex).
- HAZARD: **winding** flips under a negative-determinant (mirrored) basis (any odd number of negated scale axes). With `cullMode = NONE`, a flipped winding does not drop the triangle, but flips `gl_FrontFacing`, and `flat.frag` does `if (!gl_FrontFacing) N = -N`. A single negated scale axis is a *mirror*, not a scale. Unlit/inside-out flat surfaces: check winding (`gl_FrontFacing`) and the mesh's authored winding before lighting/normal math.
- Fallback cube carries a constant `(1,1,1)` vertex normal on all 8 corners (no per-face normals). Under inverse-transpose, a thin box scales that toward its thinnest axis (~`+Y` for the ground). Non-thin fallback cubes shade with one diagonal normal; real per-face normals (24 verts) would fix any-scale shading.

## Vertex format

- `PackedVertex` (32 bytes, std430): `float px,py,pz; float nx,ny,nz; float u,v;` (position, normal, UV). Geometry stages pull by `gl_VertexIndex` (vertex path) or meshlet indices (mesh path) from one shared mega-buffer; `vertexOffset` is in 32-byte units.

## Screen / froxel space

- Fragment screen position is `gl_FragCoord.xy` in physical pixels (`imageExtent`), origin top-left.
- Clustered-forward froxels: screen tiles `X×Y` from `gl_FragCoord / screenSize`, depth slice logarithmic between `cameraNear..cameraFar` (`slice = log(z/near)/log(far/near)`), `Z` slices. View-space depth for the slice is `-(view * worldPos).z` (view looks down −Z, so negate).

## Stage-interface rule (shaders)

- A fragment shader's `in` interface must match the bound geometry stage's `out` interface, even for a depth-only pass that ignores the values. Shared `flat.vert`/`flat.mesh` output locations 0–4 unconditionally; `shadow_depth.frag` declares the same 0–4 inputs (ignored), or a driver may drop the geometry stage's rasterizer output on mismatch.

## Standing recommendation

Re-implementing a convention in a second place (GLSL vs CPU, shadow vs camera) with a silent sign/transpose/range difference is the recurring failure. Prefer **shared, tested helpers**: one `lookAt`/`ortho`/`perspective`/`extractPlanes` each, used by CPU and any shader codegen, with a unit test that CPU and GPU agree on a known frustum. Until then, this doc is the contract; cite it in review when math is added.
