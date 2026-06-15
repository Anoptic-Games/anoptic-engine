# Renderer Regression Report — PR #50
**Commit range:** b276928..d1f1ff4 (merge-base b276928 → merge d1f1ff4 )
**Scope:** Regressions introduced by merging module-render overhaul (GPU-driven culling, indirect-indexed drawing,
dynamic rendering, bindless materials, sub-allocated GPU memory, cgltf import), and other bugs in the renderer.

---
## 1. Executive summary

The overhaul is a large, mostly-sound rewrite, but the review surfaced **15 confirmed bugs** plus **2 items needing on-device verification** — consolidated below across regressions and other defects:

- **10 regressions** introduced by PR #50 — **3 High, 6 Medium, 1 Low** (§2)
- **5 other real defects** in the renderer — lower-priority, pre-existing, or net-new macOS enablement (§4)
- **2 items** that can only be settled on real Mac hardware (§3)

**Biggest concern:** `createLogicalDevice` force-enables a full Vulkan 1.2 / descriptor-indexing / `drawIndirectCount` / dynamic-rendering feature set with **no `vkGetPhysicalDeviceFeatures2` query** (H1) — any device missing one bit dies at startup. Two silent-corruption draw bugs (H2, H3) ride along, masked only by the single-primitive test scene. Most "MoltenVK-fatal" candidates collapsed into H1, and several "critical" claims were knocked down on verification. The branch still has **no MoltenVK scaffolding at all** (O5), so these defects are latent blockers to fix before macOS runs, not live failures today.

**Fix order (terse — full detail in §5):**

1. **H1** — query device features; enable only what's supported *(prereq; subsumes ~6 candidates)*
2. **H2** — enable/gate `drawIndirectFirstInstance` (or carry identity via `gl_DrawIDARB`)
3. **H3** — emit the real `materialIndex` to the fragment stage
4. **M3** — transition MSAA `colorImage` UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
5. **M1** — restore fail-fast in `gpu_alloc` (`UINT32_MAX` + null-handle checks)
6. **M5 / M6** — bounds-check the geometry pool and material SSBO writes
7. **M4** — recreate semaphores in `recreateSwapChain`
8. **M2** — move runtime geometry upload off the per-frame path
9. **L1** — dedupe bindless texture registration
10. **O1 / O2** — null-check `realloc`; route `components.c`/`pipeline.c` through mimalloc
11. **O3** — honor `bufferImageGranularity` (before any non-Metal backend)
12. **O4** — keep 32-bit glTF indices when needed
13. **O5** — add `VK_KHR_portability_subset` / `_enumeration` (macOS enablement)
14. **Verify** on a real Mac GPU (§3)

---

## 2. Confirmed bugs — regressions

### HIGH

#### H1. Device features force-enabled without any `vkGetPhysicalDeviceFeatures2` support query
**`src/vulkan_backend/instance/instanceInit.c:566–588`**

- **Before (`b276928`):** `createLogicalDevice` had **no `pNext` feature chain at all** — only a plain `VkPhysicalDeviceFeatures{shaderInt64, shaderFloat64, samplerAnisotropy}`, each value copied from `vkGetPhysicalDeviceFeatures(availableFeatures)`. Broadly compatible.
- **After (`d1f1ff4`):** Eight advanced bits are set to `VK_TRUE` unconditionally and chained into `vkCreateDevice` via `pNext`: `descriptorIndexing`, `shaderSampledImageArrayNonUniformIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount`, `descriptorBindingSampledImageUpdateAfterBind`, `drawIndirectCount` (all in `features12`), plus `dynamicRendering`. **No `vkGetPhysicalDeviceFeatures2` call exists anywhere in `src/`** (verified by grep — zero matches). `isDeviceSuitable` (instanceInit.c:387–397) checks none of these.
- **Why it breaks:** Per the Vulkan spec, enabling a feature the physical device reports as unsupported makes `vkCreateDevice` return `VK_ERROR_FEATURE_NOT_PRESENT`. On older MoltenVK builds, Intel-Mac MoltenVK, software ICDs, or any GPU missing a single bit, device creation hard-fails where `b276928` succeeded. `multiDrawIndirect` is gated correctly at instanceInit.c:533 — the 1.2 block is the inconsistency.
- **Fix:** During device selection, build the same chain (`VkPhysicalDeviceVulkan12Features` → `VkPhysicalDeviceDynamicRenderingFeaturesKHR`) inside a `VkPhysicalDeviceFeatures2`, call `vkGetPhysicalDeviceFeatures2`, require the bits the renderer actually uses in `isDeviceSuitable` (reject + clear diagnostic if missing), and in `createLogicalDevice` only set bits the query returned `VK_TRUE`. This restores the old "copy only what the device supports" posture.

> **This is the root cause that several candidate findings circled.** The repeated `drawIndirectCount` / bindless / dynamic-rendering "critical, breaks MoltenVK" candidates (dimensions: lifecycle, gpu-alloc, swapchain, synchronization, descriptors, pipeline-dynrender, shader-c-interface, moltenvk) were all the **same defect** viewed from different angles. The verifiers correctly rejected the "Metal cannot do count-buffer draws at all" claim (current MoltenVK *does* support `VK_KHR_draw_indirect_count`, emulated over ICBs) and corrected the severity from critical → high. The substantive bug is **unchecked feature enablement + no fallback**, not a Metal capability wall. Fixing H1 (query-then-enable) resolves the `drawIndirectCount`, descriptor-indexing/update-after-bind, and `dynamicRendering` enable-without-check variants together.
>
> *Subsidiary action under H1 — `drawIndirectCount` fallback:* The sole draw call is `vkCmdDrawIndexedIndirectCount` (vulkanMaster.c:278), with no non-count fallback. After gating the feature, when it is unsupported fall back to `vkCmdDrawIndexedIndirect` with `drawCount = indirectBuffer.capacity` and have `cull.comp` set `instanceCount = 0` on culled/unused commands so padded entries draw nothing. Also treat `multiDrawIndirect` as **required** (not best-effort), since the single draw emits up to `capacity` (10000) commands — without it only the first command renders. Do **not** add `VK_KHR_draw_indirect_count` to `requiredExtensions[]`: the instance requests `VK_API_VERSION_1_3` (instanceInit.c:113), where it is core; adding it to the required-extension check would wrongly reject conformant devices.

#### H2. Indirect draws write non-zero `firstInstance` but `drawIndirectFirstInstance` is never enabled
**`resources/shaders/cull.comp:81`; `src/vulkan_backend/instance/instanceInit.c:532–533`; `resources/shaders/flat.vert:30`**

- **Before:** Old draw was `vkCmdDrawIndexed(..., firstInstance=0)` (b276928 vulkanMaster.c:134); `base.vert` did not use `gl_InstanceIndex`. The feature was irrelevant.
- **After:** `cull.comp:81` writes `draws[drawIdx].firstInstance = idx` (the entity index, non-zero for all but entity 0), with `instanceCount = 1`. `flat.vert:30` computes `entityIndex = pc.transformBaseOffset(0) + gl_InstanceIndex` (= `firstInstance`) and uses it to index **both** the transform SSBO and the material output. The device enables only `{shaderInt64, shaderFloat64, samplerAnisotropy, multiDrawIndirect}` — **`drawIndirectFirstInstance` is never set anywhere** (verified by grep — zero matches).
- **Why it breaks:** VUID-VkDrawIndexedIndirectCommand-firstInstance-00554 requires `firstInstance == 0` for indirect draws when `drawIndirectFirstInstance` is disabled. Violating it is a per-frame validation error and undefined behavior on conformant drivers; where the driver clamps `firstInstance` to 0 (a real, common MoltenVK/Metal base-instance limitation), **every entity collapses onto transform[0]/material[0]**. The entire instancing/identity scheme rides on this. *This is platform-agnostic* (spec violation on any conformant driver), not MoltenVK-specific — modern MoltenVK actually advertises the feature, but it is still never enabled.
- **Fix:** Enable `VkPhysicalDeviceFeatures.drawIndirectFirstInstance` (gated on `availableFeatures`, mirroring the `multiDrawIndirect` pattern) and add it to `isDeviceSuitable`. For drivers/configs lacking it, decouple identity from `firstInstance`: keep `firstInstance = 0` and carry the entity index per-draw via `gl_DrawIDARB` (GLSL `GL_ARB_shader_draw_parameters` is already in `flat.vert`) into a parallel `drawIdx → entityIndex` SSBO written by `cull.comp`.

> *Note on the related "flat.vert needs shaderDrawParameters" candidates:* Those were correctly **rejected** — the committed `flat.vert.spv` declares only `OpCapability Shader` (no `DrawParameters` capability, no `OpExtension`), because the shader uses only core `gl_InstanceIndex`. The `#extension : require` line is an inert source breadcrumb. The real feature gap is `drawIndirectFirstInstance` (H2), not `shaderDrawParameters`.

#### H3. Fragment shader keys materials by entity index, not by the per-entity `materialIndex`
**`resources/shaders/flat.vert:30–36`; `resources/shaders/flat.frag:25`; `src/render/gltf/ano_GltfParser.c:174–178,268`; `src/vulkan_backend/vulkanMaster.c:372–373`**

- **Before:** `base.frag` sampled a single descriptor-bound `texSampler` (set=1,binding=1) per draw — the correct texture was always used.
- **After:** Materials get an independent counter `matIdx = materialBuffer.count++` (one per primitive, baked in mesh/primitive declaration order). Entities store `entities[i].materialIndex = prim->materialIndex` in node-instantiation order. The real `materialIndex` is uploaded **only** into the cull `EntitySSBO` (`entityBuffer[i*2+1]`, vulkanMaster.c:373), which lives in the compute descriptor set. `flat.vert:36` sets `outMaterialIndex = entityIndex` and `flat.frag:25` reads `materials[inMaterialIndex]` — i.e. **the material SSBO is indexed by entity index**. The graphics set 0 (pipeline.c) exposes only GlobalUBO/TransformSSBO/MaterialSSBO; the vertex stage physically cannot read the real material index.
- **Why it breaks:** Entity index and material index coincide **only** in the trivial 1-node/1-mesh/1-primitive case (the current viking_room test — hence it looks fine). With shared materials, multiple instances of one mesh, multiple models, or non-declaration-order traversal, entities sample the wrong material/albedo. The material buffer is allocated at capacity 10000 with zero-init slots, so reading `materials[entityIndex]` beyond the populated count yields zeroed `MaterialData` (fallback texture), **not** a hard overrun — the symptom is wrong/blank materials, not a GPU fault.
- **Fix:** Deliver the true material index to the fragment stage. Smallest change: bind the `EntitySSBO` (or a per-entity material-index buffer) to the graphics set and set `outMaterialIndex = entities[entityIndex].materialIndex` in `flat.vert`. Alternative: have `cull.comp` write the resolved material index into a per-draw SSBO indexed by `gl_DrawIDARB`. Do **not** keep indexing `materials[]` by entity index.

> H2 and H3 are correctness twins masked by the same single-primitive test scene; fix both before testing any non-trivial asset.

### MEDIUM

#### M1. `findMemoryType` returns index `0` on failure (was `UINT32_MAX`), masking allocation failure
**`src/vulkan_backend/gpu_alloc.c:6–17`; callers in `instanceInit.c:978`, `texture/texture.c:189`, `vulkanMaster.c:573,600,627,655,682,694,706,718,730`, `geometry.c:41,53,99`**

- **Before:** `findMemoryType` returned `UINT32_MAX` on no-match (b276928 instanceInit.c:1599). Passing that to `vkAllocateMemory` fails (index ≥ `memoryTypeCount`), and the old code checked the `VkResult` and aborted resource creation cleanly. The overhaul **deleted those `if (vkAllocateMemory != VK_SUCCESS) return false` guards** (visible as removed lines in the texture.c/instanceInit.c diffs).
- **After:** Returns `0` — a *valid* memory type that may not satisfy `typeFilter` or `HOST_VISIBLE`. `gpu_alloc` uses it with no sentinel check (gpu_alloc.c:22), and on `vkAllocateMemory` OOM returns `GpuAllocation{0}` (memory == `VK_NULL_HANDLE`) which callers bind unconditionally. The `HOST_VISIBLE` map guard (gpu_alloc.c:73) keys on the *requested* props, not the chosen type's actual flags, so it can `vkMapMemory` non-mappable memory and leave `.mapped == NULL` for sites that immediately deref it (e.g. `createTransformBuffer`/`createCullingBuffers` → `transformBuffer.mapped[frame][i]` at vulkanMaster.c:1007).
- **Why it's Medium not High:** Both failure modes only trigger on an allocation that would already fail (no compatible type, or OOM). On conformant hardware including MoltenVK, `DEVICE_LOCAL` and `HOST_VISIBLE|HOST_COHERENT` are always satisfiable, so the bad path is essentially unreachable in normal operation. But the outcome — silent wrong-heap binding / binding `VK_NULL_HANDLE` / `NULL` deref — is UB/corruption rather than the old clean abort, and it regresses hardening the macOS branch cares about.
- **Fix:** Return `UINT32_MAX` from `findMemoryType`; in `gpu_alloc`, if `memoryType == UINT32_MAX` return empty `GpuAllocation{0}` before touching blocks. Key the `vkMapMemory` decision on the chosen type's actual `propertyFlags`. Add `allocation.memory != VK_NULL_HANDLE` checks (and propagate failure / destroy the just-created buffer/image) at all listed call sites.

#### M2. Runtime `geometry_pool_upload` mutates shared device-local buffers with no sync against in-flight draws
**`src/vulkan_backend/geometry.c:158–194`; `src/vulkan_backend/vulkanMaster.c:431,457`**

- **Before:** Geometry was uploaded once at init into per-entity buffers, before any frames were submitted. No runtime upload path existed.
- **After:** `drawFrame` calls `testAssetUnloadReload()` at the top (vulkanMaster.c:457), **before** the current-frame fence wait. At phase 1 it calls `geometry_pool_upload`, which records `vkCmdCopyBuffer` into the shared EXCLUSIVE-mode device-local `pool->vertexBuffer`/`indexBuffer` (bound and drawn every frame at vulkanMaster.c:265–266, 278), submits on `transferQueue`, and waits **only on its own transfer fence** — no semaphore, no buffer memory barrier, no queue-family-ownership transfer against the graphics queue's in-flight reads. If `transferFamily != graphicsFamily`, cross-queue access to an EXCLUSIVE buffer is undefined.
- **Why it's Medium not High:** This is a phase-guarded dev/test harness (`static int phase` 0→1→2) that runs the copy **exactly once per program lifetime** into a deferred-freed block, so practical corruption is largely avoided today. The candidate's command-pool external-sync claim was **rejected** (`drawFrame` is single-threaded from main.c; the loop only resets its own pre-allocated per-frame buffers). The host-side `meshes[]` realloc claim was also rejected (read single-threaded later in the same `drawFrame`; cull reads a per-frame device SSBO copy). The remaining real defect is the unsynchronized GPU buffer write.
- **Fix:** Use a dedicated transient transfer command pool (not `state->commandPool`); before mutating the shared buffers, wait on **all** `MAX_FRAMES_IN_FLIGHT` fences (or `vkDeviceWaitIdle` for this rare event); if `transferFamily != graphicsFamily`, do a queue-family-ownership transfer or make the buffers `VK_SHARING_MODE_CONCURRENT`, and add a `TRANSFER_WRITE → VERTEX_ATTRIBUTE_READ|INDEX_READ` barrier. Keep it gated behind the deletion-queue lifetime guarantee. This is dev scaffolding and should not be on the per-frame hot path in shipped builds.

#### M3. MSAA color image used as a color attachment while still in `VK_IMAGE_LAYOUT_UNDEFINED`
**`src/vulkan_backend/vulkanMaster.c:213–216,245`; `src/vulkan_backend/instance/instanceInit.c:1160–1167`; `src/vulkan_backend/texture/texture.c:174`**

- **Before:** The old `VkRenderPass` color attachment had `initialLayout=UNDEFINED → finalLayout=COLOR_ATTACHMENT_OPTIMAL`; the render pass performed the layout transition automatically every pass.
- **After:** `createColorResources` creates `colorImage` with `initialLayout=UNDEFINED` (texture.c:174) and **never transitions it** (verified — `colorImage`/`colorView` appear only at create/destroy/attachment-bind, never in a `vkCmdPipelineBarrier`/`transitionImageLayout`). The depth image *is* transitioned (instanceInit.c:1150) and the swapchain/resolve image *is* barriered (vulkanMaster.c:145–167), but the MSAA color image was missed in the render-pass → dynamic-rendering conversion. `recordCommandBuffer` sets `colorAttachment.imageLayout = COLOR_ATTACHMENT_OPTIMAL` and calls `vkCmdBeginRendering`, which performs **no** automatic transition.
- **Why it's High-corrected-from-Critical:** It's a genuine spec violation (image used as attachment while UNDEFINED; `LOAD_OP_CLEAR` overwrites contents but does not relax the layout requirement) that fires validation errors and is technically UB. But because `loadOp=CLEAR` discards prior contents, most implementations (including MoltenVK, where Metal abstracts layouts) still produce correct output in practice, so it is unlikely to crash or visibly corrupt.
- **Fix:** In `createColorResources` (after creating `colorImage`/`colorView`), add a one-time transition mirroring the depth path: `transitionImageLayout(..., colorImage, colorFormat, UNDEFINED → COLOR_ATTACHMENT_OPTIMAL, 1)`. Since `createColorResources` re-runs on swapchain recreation, this stays correct. Confirm `transitionImageLayout` derives a color aspect mask (the depth path passes a depth format) or add a color path.

#### M4. `recreateSwapChain` dropped `clearSemaphores()`; reused `renderFinished` can be left signaled after present `OUT_OF_DATE`
**`src/vulkan_backend/vulkanMaster.c:530–533` (present path); `src/vulkan_backend/instance/instanceInit.c:847–907`**

- **Before:** Every resize path called `clearSemaphores()` (b276928 vulkanMaster.c:146–167), destroying + recreating all per-frame `imageAvailable`/`renderFinished` semaphores before recreate — a clean-state guarantee.
- **After:** `clearSemaphores()` is deleted entirely. `recreateSwapChain` does `vkDeviceWaitIdle` + reset fences + `frameSubmitted=false`, reusing the same semaphores.
- **Why it's a (medium) regression:** When `vkQueuePresentKHR` returns `VK_ERROR_OUT_OF_DATE_KHR`, the spec leaves it unspecified whether the present's wait on `renderFinished[frameIndex]` executed, so that binary semaphore can stay signaled. `vkDeviceWaitIdle` drains queued work but does **not** unsignal a binary semaphore whose wait was never queued; the next cycle re-signals it via `vkQueueSubmit`, violating VUID-vkQueueSubmit-pSignalSemaphores and risking a hang.
- **Caveats from verification:** This is **not** branch-specific macOS hardening — the commits that added `clearSemaphores`/`skipCheck` (`fe9cf8f`, etc.) are in shared history; the overhaul removed them for everyone. The `skipCheck` removal is immaterial (it only skipped a redundant fence loop, never guarded semaphores, and was never decremented). On the macOS target, resize usually yields `VK_SUBOPTIMAL_KHR` (a success code where the present wait *is* executed and `vkDeviceWaitIdle` makes reuse safe), so the dangerous `OUT_OF_DATE`-on-present path is intermittent.
- **Fix:** Inside `recreateSwapChain`, after `vkDeviceWaitIdle` and before resetting fences, destroy + recreate each frame's `imageAvailable`/`renderFinished` (the old `clearSemaphores()` equivalent). Do **not** restore `skipCheck`.

> The two **acquire-path** variants of the semaphore finding (the lifecycle-dimension and swapchain-dimension "acquire OUT_OF_DATE reuses half-signaled semaphore" candidates) were correctly **rejected**: on the acquire `OUT_OF_DATE` path the spec does not signal the semaphore, and on the success/`SUBOPTIMAL` path the signal is always consumed by the subsequent `vkQueueSubmit`. The only real exposure is the **present** `OUT_OF_DATE` path above.

#### M5. Geometry mega-buffer bump allocator has no capacity bounds check
**`src/vulkan_backend/geometry.c:29–30,136–156`**

- **Before:** Each mesh got a dedicated, exactly-sized device-local buffer (b276928 memory.c `createVertexBuffer`/`createIndexBuffer`) — no shared pool to overflow.
- **After:** Fixed 64 MiB vertex / 16 MiB index pools with bump heads. On the no-free-block path: `finalVertexOffset = vertexWriteOffset; vertexWriteOffset += vertexSize` with **no check** that the result fits the pool (same for indices). The pool sizes are local variables in the init function, not even stored in the struct.
- **Why it breaks:** Once cumulative uploads exceed the pool size, `vkCmdCopyBuffer` records a `dstOffset` past the buffer (VU violation / GPU OOB write) and subsequent indirect draws read garbage. The free-block reuse masks this for the recycling demo, but any real asset stream or long session silently corrupts geometry. (Verifier corrected the label from "off-by-one" to "missing bounds check.")
- **Fix:** Store `vertexPoolSize`/`indexPoolSize` in `struct GeometryPool`. Before each bump commit, check `writeOffset + size <= poolSize`; on overflow, destroy the staging buffer/cmd and return the fallback mesh index (not `(uint32_t)-1`, to avoid downstream OOB mesh-index use). Keep the comparison in 64-bit (`writeOffset` is `uint32_t`, pool sizes are `VkDeviceSize`). Longer term, grow the pool with an additional block.

#### M6. Material SSBO write has no capacity bound; `materialBuffer.count++` can overrun the mapped buffer
**`src/render/gltf/ano_GltfParser.c:174–187`**

- **Before:** Materials were parsed into a local `malloc`'d array sized exactly to the file's `materialCount`; no global fixed-capacity mapped SSBO existed.
- **After:** `parseGltf` does `matIdx = rendererState.materialBuffer.count++` per primitive, then writes `materialBuffer.mapped[frame][matIdx] = matData` for all `MAX_FRAMES_IN_FLIGHT` with **no check against `capacity`**. Capacity is fixed at `maxEntities = 10000` (vulkanMaster.c:554), `count` is set to 0 only at creation and never reset (monotonic across `parseGltf` calls). The sibling `DeletionQueue` in the same file *does* guard+grow, confirming the omission is a real defect.
- **Why it's Medium not High:** Definite OOB host write into persistently-mapped device memory once cumulative primitive count exceeds 10000 — but currently latent (the only shipped asset has one primitive; `maxEntities=10000` is a documented placeholder). Concrete hazard against the engine's stated million-entity goal. (Verifier corrected the label from "descriptor-mismatch" to "missing capacity check / OOB write.")
- **Fix:** Before incrementing, guard `if (materialBuffer.count >= materialBuffer.capacity) { log + bail/reuse-default; }`. Better: pre-sum primitive counts across `data->meshes` and validate against remaining capacity before the bake loop. Long term, give `MaterialBuffer` a grow path like the `DeletionQueue`.

### LOW

#### L1. Per-primitive bindless texture registration duplicates slots (append-only, never reclaimed)
**`src/render/gltf/ano_GltfParser.c:155–170`; `src/vulkan_backend/texture/texture.c:22–47`**

- **Before:** The old parser deduplicated texture uploads via a `texture->processed` guard — each unique texture uploaded once.
- **After:** Step 2 loads each unique texture once, but step 3 calls `bindless_register_texture(...)` **per primitive**. `bindless_register_texture` is strictly append-only (`index = bta->textureCount++`, no dedup). A texture shared by K primitives consumes K bindless slots all pointing at the same view, and every `parseGltf`/reload appends more, never reclaimed (cap 4096).
- **Why it's Low (corrected from High):** Does not crash or visually break a typical single load (duplicate slots point to the identical view; cap is 4096). The harm is wasted slots, divergent `albedoIndex` for identical images, and unbounded append-only growth across reloads.
- **Fix:** Register each unique texture once in step 2, cache `texIdx → bindless index`, and look it up in step 3. Optionally reset `bta->textureCount` on model unload.

> A further **Low** allocator candidate — the `GpuAllocator` block-array `realloc`-NULL-check — is real but minor and is tracked as **O2** (§4). (The staging-arena "unbounded leak" candidate was investigated and confirmed *not* a bug — residual is bounded and freed at teardown — so it is not listed.)

---

## 3. Needs human verification

These were rated by the verifier as real-but-conditional, with the failure mode dependent on the actual target hardware/driver. They cannot be settled from source alone:

1. **Bindless update-after-bind array vs. MoltenVK argument-buffer limits** — `pipeline.c:176–214` hardcodes a 4096-entry `COMBINED_IMAGE_SAMPLER` array with `PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT | UPDATE_AFTER_BIND`, indexed via `nonuniformEXT` in `flat.frag`. **No limit query exists** (`maxPerStageDescriptorUpdateAfterBindSampledImages` / `maxDescriptorSetUpdateAfterBindSampledImages` are never read; no `vkGetPhysicalDeviceProperties2`). The robustness gap (force-enabled UAB features + unchecked 4096 count) is folded into **H1's fix**. **To check on real hardware:** confirm the target Metal GPU/MoltenVK config (`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS`, argument-buffer tier) supports update-after-bind sampled-image arrays of 4096, and clamp `maxTextures = min(4096, reported limit)`. Likely fine on Metal-3 Apple Silicon; risk is older/Intel Macs.

2. **`drawIndirectCount` / `multiDrawIndirect` behavior under the chosen MoltenVK build** — the H1 fix gates and falls back, but **whether the count-buffer fast path actually works** (vs. needing the `vkCmdDrawIndexedIndirect` fallback) must be confirmed on the target MoltenVK version once a device is created. Also verify `VkPhysicalDeviceProperties.limits.maxDrawIndirectCount >= 10000` under Metal emulation. **What to check:** run with validation layers on the actual macOS GPU after H1/H2 land; confirm device creation succeeds and the indirect-count draw renders all meshes.

---

## 4. Confirmed bugs — other defects (non-regression)

These are **real defects** surfaced by the same review, kept separate from §2 only because each is lower-severity, neutralized on the current macOS/Metal target, or pre-existing (not introduced by PR #50). They are counted in §1 and scheduled in the consolidated remediation (§5).

- **O1. `components.c` / `pipeline.c` allocate through system libc instead of mimalloc; unchecked `realloc` in `ano_vk_register_mesh`/`ano_vk_register_texture`** — Two real defects. (1) Every other allocating TU includes `<mimalloc-override.h>`; these two omit it, so their `malloc`/`calloc`/`realloc`/`free` bind to system libc rather than the mimalloc override, violating the "all allocations go through arenas / thread-local heaps" invariant (the override *is* the allocator abstraction here — there is no `ano_malloc`). (2) `ano_vk_register_mesh`/`ano_vk_register_texture` grow their arrays with `self = realloc(self, …)` (+1 linear growth) and dereference the result with **no NULL check** — a host-OOM NULL deref. **Fix:** add `#include <mimalloc-override.h>` to both TUs so their allocations route through mimalloc; realloc into a temp pointer, check for NULL before assigning (free/return failure on NULL), and grow geometrically (×1.5/×2) instead of +1 to avoid O(n²) recopy. *(A convention slip, not a regression — the old backend used raw libc pervasively too — but a real allocator-routing + OOM-safety defect.)*
- **O2. `GpuAllocator` block-array `realloc` with no NULL check** (gpu_alloc.c:50–58) — Low. `self->blocks` is grown via `realloc` and immediately dereferenced; on host-OOM this is a NULL deref. Only triggers when spawning a new 256 MiB block, so rare in practice. **Fix:** realloc into a temp pointer; on NULL, return failure and leave the old array intact; assign and increment `blockCount` only *after* a successful grow. Applies to the same `realloc(self)` pattern everywhere it appears.
- **O3. `bufferImageGranularity` ignored when linear buffers and optimal images share a block** (gpu_alloc.c:31) — Medium in general, **currently inert on the macOS target only**. The sub-allocator honors only `reqs.alignment`, so linear DEVICE_LOCAL geometry buffers and OPTIMAL DEVICE_LOCAL textures can be packed closer than `bufferImageGranularity` — aliasing/corruption on any tiler reporting granularity > 1. MoltenVK/Metal reports `bufferImageGranularity == 1`, so it cannot fire on Metal, but it is a live hazard the moment a desktop GPU backend is targeted (granularity is commonly 256 B–1 KiB there). **Fix:** track each block's tiling class (linear vs optimal) and, when a sub-allocation would straddle a class boundary, round the offset up to `bufferImageGranularity`; or route optimal images through a dedicated allocator that never shares blocks with linear resources. *(Promote to Medium in §2 if/when a non-Metal backend is targeted.)*
- **O4. glTF indices truncated to `uint16`** (ano_GltfParser.c:86–89) — latent correctness bug, **pre-existing (not a regression)**. `UNSIGNED_INT` accessor indices are narrowed to `uint16` and the buffer is bound as `VK_INDEX_TYPE_UINT16`. Any mesh with > 65 535 vertices silently renders corrupted geometry (index values wrap). The old parser did the same, so it is byte-for-byte unchanged by PR #50 — but still a real defect, not merely a backlog enhancement. **Fix:** read the accessor component type; when it is `UNSIGNED_INT` (or the primitive's vertex count exceeds 65 535), keep 32-bit indices and bind `VK_INDEX_TYPE_UINT32`, sizing the index pool/staging to the chosen index width.
- **O5. Missing `VK_KHR_portability_enumeration` / `VK_KHR_portability_subset`** — **hard blocker for the macOS target**, but net-new enablement rather than a PR #50 regression (`b276928` already lacked all portability handling; the branch has no `__APPLE__`/Metal-surface code). Without it the engine cannot initialize on MoltenVK: `vkCreateInstance` returns `VK_ERROR_INCOMPATIBLE_DRIVER` unless the instance requests `VK_KHR_portability_enumeration` and passes `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`, and the spec **requires** enabling `VK_KHR_portability_subset` as a device extension on any physical device that advertises it. **Fix:** (1) add `VK_KHR_portability_enumeration` to the instance extensions and set the `…ENUMERATE_PORTABILITY_BIT_KHR` create flag (guarded on `__APPLE__` or on the extension being reported); (2) when enabling device extensions, if the device advertises `VK_KHR_portability_subset`, add it to the enabled list. Track under macOS enablement, but it is a prerequisite for the branch running at all.

---

## 5. Consolidated remediation — sequential fix order

Ordered by "fixes the most, soonest," covering **both regressions (§2) and other defects (§4)**. Items 1–3 unblock a working macOS render path; 4–9 are regression hardening; 10–13 clear the remaining real defects; 14 is the on-device pass.

1. **[H1] Add a `vkGetPhysicalDeviceFeatures2` query and gate every advanced device feature on actual support.** Build the `VkPhysicalDeviceVulkan12Features` + `VkPhysicalDeviceDynamicRenderingFeaturesKHR` chain, require the used bits in `isDeviceSuitable`, and only set bits the query reported `VK_TRUE`. *This single change resolves the `drawIndirectCount`, descriptor-indexing/update-after-bind, and `dynamicRendering` enable-without-check defects together and is the prerequisite for the engine ever creating a device on MoltenVK.* Add the `drawIndirectCount` → `vkCmdDrawIndexedIndirect` fallback and make `multiDrawIndirect` required.

2. **[H2] Enable & gate `drawIndirectFirstInstance`** (or stop encoding entity identity via `firstInstance`, using `gl_DrawIDARB` + a per-draw entity-index SSBO). Without this, all entities collapse onto index 0 on any conformant driver.

3. **[H3] Fix material indexing:** make `flat.vert` emit the real `materialIndex` (bind `EntitySSBO` to the graphics set, or write material index per-draw in `cull.comp`). Pairs with H2 — both are masked by the single-primitive test scene.

4. **[M3] Add the one-time MSAA `colorImage` UNDEFINED → COLOR_ATTACHMENT_OPTIMAL transition** in `createColorResources` (re-runs on resize).

5. **[M1] Restore fail-fast in `gpu_alloc`:** `findMemoryType` → `UINT32_MAX`, empty allocation on no-match/OOM, map-guard on actual type flags, and `VK_NULL_HANDLE` checks at all bind sites.

6. **[M6] Bounds-check the material SSBO write; [M5] bounds-check the geometry pool bump allocator.** Both are latent OOB writes that surface with real/large assets.

7. **[M4] Re-add the semaphore destroy/recreate inside `recreateSwapChain`** (present-`OUT_OF_DATE` path).

8. **[M2] Move runtime `geometry_pool_upload` off the per-frame hot path:** dedicated transfer pool, wait on all frame fences (or `vkDeviceWaitIdle`), add the transfer→graphics barrier / QFOT. Treat the test harness as dev-only.

9. **[L1] Deduplicate bindless texture registration** (register once per unique texture, cache `texIdx → slot`).

10. **[O1/O2] Make every host `realloc` OOM-safe and route allocations through mimalloc:** add `#include <mimalloc-override.h>` to `components.c`/`pipeline.c`; realloc into a temp pointer with a NULL check before assigning (in `ano_vk_register_mesh`/`ano_vk_register_texture`, the `GpuAllocator` block array, and the same `realloc(self)` pattern elsewhere); grow geometrically rather than +1.

11. **[O3] Honor `bufferImageGranularity`** when linear buffers and optimal images share a block — track per-block tiling class and round straddling offsets up (or give optimal images a dedicated allocator). Inert on Metal today; do it before any non-Metal backend.

12. **[O4] Keep 32-bit glTF indices** when the accessor is `UNSIGNED_INT` or the primitive exceeds 65 535 vertices — bind `VK_INDEX_TYPE_UINT32` and size the index pool/staging to the chosen width.

13. **[O5] Add `VK_KHR_portability_enumeration` (instance, with the enumerate-portability create flag) and `VK_KHR_portability_subset` (device, when advertised).** Net-new macOS enablement, but a prerequisite for the branch running on MoltenVK at all.

14. **[Verify — §3] On a real Mac GPU with validation layers** after items 1–3: confirm device creation succeeds, the indirect-count draw renders all meshes, clamp bindless `maxTextures` to the reported update-after-bind limit, and confirm `maxDrawIndirectCount >= 10000`.

---

**Coverage note (honesty):** This report is source-level only — no build was run and no MoltenVK device was exercised, so the two §3 items (UAB limits, indirect-count emulation behavior) are genuinely unresolved and require on-device testing. All confirmed findings cite verified file:line against the working tree (== `d1f1ff4` for the referenced files) and the `b276928` baseline; I independently re-checked the load-bearing facts for H1, H2, H3, M3, and M4 (no `Features2` call, no `drawIndirectFirstInstance`, no `colorImage` transition, `firstInstance = idx`, `outMaterialIndex = entityIndex`, deleted `clearSemaphores`). Where verifier verdicts disagreed with candidates, I used the verifier's corrected severity and merged the ~6 duplicate `drawIndirectCount`/feature-enablement candidates into root cause **H1**.
