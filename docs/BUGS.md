# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.

## Tags

Each entry carries one or more root-cause tags on the bullet line(s) beneath it, so the census reads by shape as well as by module. Three root causes dominate the board; the rest are smaller shared shapes or 1:1 singletons.

An entry prefixed `[X] Fixed` has been repaired; its text is kept verbatim as the record of what was wrong, and a closing `fix (date)` bullet says what was done. An entry carrying a `wontfix (date)` bullet instead is a finding whose analysis stands but whose repair the owner declined, with the reason on that bullet; it counts as unremediated. Neither bullet is a tag, and both are excluded from every tag count.

- checked-arithmetic 〜 an integer wrap (multiply, add, or subtract) slips past the guard or the allocation-sizing it should have been caught by; one shared checked-arithmetic helper retires the whole family.
- no-abort 〜 a failure that should stop the operation does not: ano_log(ANO_FATAL) logs and returns, or a callee reports unconditional success or hands back an uninitialised out-param, so the caller's error arm is dead.
- seam-validation 〜 a value crossing a documented seam (public API, bridge, config) is trusted as valid or used as an index without its domain being checked.
- ownership-leak 〜 a resource acquired earlier is never discharged on a failure return or at teardown; the discharge-side facet of the seam root cause.
- fixed-array-overflow 〜 an unbounded counter or index writes past a fixed-size stack array while a sibling bound elsewhere is guarded.
- partial-publish 〜 a batch operation publishes per-element state as it walks, then fails mid-batch with only a scalar return, leaving the prefix live and aliasable.
- copy-paste-error 〜 code cloned from a sibling with one token left unchanged, a duplicated argument or predicate.
- feature-list-drift 〜 a per-feature resource property maintained in a hand-written parallel list, present for every sibling but one.
- wrong-error-source 〜 a guard or return consults the wrong error indicator: the wrong sentinel value, or errno where the callee reports by return.
- feature-gated-check 〜 a correctness check runs only when an unrelated optional feature flag is on.
- shift-ub 〜 1 << i shifts into the sign bit of int at the top of a legal index domain.
- recovery-desync 〜 a device-loss recovery restarts playback without resetting the state the restart invalidates.
- odd-sibling-out 〜 one of several sibling implementations omits a guard or ordering the others share.
- lookahead-off-by-one 〜 a paired window and delay length differ by one at a lookahead seam, so coverage expires one step before the guarded sample is consumed.
- unbounded-spin 〜 a loop or wait with no forward-progress or termination guard spins or hangs forever.
- dangling-capture 〜 a deferred record stores a caller pointer and dereferences it after the caller may have freed it, against a one-sided lifetime contract.
- size-mismatch 〜 a buffer is provisioned by the wrong metric (stored footprint vs rendered width) and the shortfall subtraction underflows.
- unguarded-delegation 〜 a header contract is delegated straight to a third-party allocator that does not honour it.
- truncating-cast 〜 a narrowing cast drops the high bits of a duration or size, silently changing the operation.
- retry-exhaustion 〜 a bounded retry loop's exhaustion arm is indistinguishable from success and consumes an out-param the failed call left unwritten.
- clock-not-reanchored 〜 a clock source is not re-based after a power transition (sleep or hibernate) that resets it, so the monotonic contract breaks and every delta held across the transition wraps.
- noop-not-honored 〜 a documented no-op input still changes the output when it lands in a boundary position (the last element) that the code consumes unconditionally.
- shared-mutable-state 〜 a function-scope static or global is reused across contexts that should be independent (concurrent engines, reentrant calls), so their accesses race or clobber each other.
- missed-repoint 〜 a resource is recreated (grown, reallocated) but one of the descriptor sets or references pointing at it is not updated, leaving a live binding to a destroyed handle.
- table-coverage-gap 〜 two generated lookup tables that must agree on coverage diverge, so some inputs fall through to a default path and behave inconsistently with their peers.
- alignment-contract-gap 〜 a type's declared alignment is weaker than the layout contract its own header documents, so nothing enforces the guarantee its callers and the GPU mirror rely on.

One status tag rides alongside the root causes rather than replacing them:

- pending-design-decision 〜 the defect is understood and its root cause is already tagged, but the repair is blocked on a contract choice with more than one defensible answer, so writing the fix would settle that question silently. Never the only tag on an entry. Each carries a write-up 〜 the decision, the candidate answers, what each costs 〜 in docs/BUG-HUNT.md, "Open decisions". Status tags are excluded from every root-cause tag count. All five carriers were settled by 2026-07-25 〜 the tag currently marks nothing; the retired write-ups live in docs/BUGS_DONE.md, "Settled open decisions".


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

audio_win64.c:589 〜 dsound_render_loop's DSBSTATUS_BUFFERLOST recovery calls Restore then Play without rewriting or silencing the ring and without resetting writeCursor, so up to four blocks of undefined restored buffer contents play and the stale cursor keeps writing out of phase with the restarted play cursor 〜 confirmed in source by two passes; no deterministic trigger seam today (real dsound.dll, loss needs focus change under DSSCL_PRIORITY) 〜 test: pending
- recovery-desync

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 〜 no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_win64.c:137 〜 ano_fs_write's loop has no forward-progress check, so a WriteFile returning TRUE with 0 bytes written advances neither cursor nor remaining and the writer spins forever instead of returning -1; MSDN never promises written > 0 on success for synchronous file handles (network redirectors and filter drivers are the plausible producers), and the linux twin is immune by POSIX (write of nbyte > 0 on a regular file cannot return 0) 〜 test: pending 〜 no seam to make the real WriteFile return a zero-progress TRUE
- unbounded-spin

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Math

(anoptic_math.h is types-only as of 2026-07-17 〜 mat4/Vector2/3/4 PODs, no ops, no src/ module; ops live in render/vertex.h. Composition audits belong to the render_bridge and vulkan_backend interlink edges.)

### Interface-level bugs and logic inefficiencies

(The former lead "anoptic_math.h:16 vs docs/math-conventions.md 〜 one of the two contracts is lying" is struck as of 2026-07-24: the doc was right and the header was wrong. `translate` writes the translation to `mat[3][0..2]`, `multiplyMat4` documents `temp[col][row] += a[k][row] * b[col][k]`, and `perspective` sets `matrix[2][3] = -1` 〜 all column-major, matching GLSL's mat4 so uploads need no transpose. The header comment now says so. No code changed.)

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs


### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

shadow_casters.c:97 〜 register_static_shadow indexes shadowTypeUsed[3] with the raw bridge light type ((uint32_t)cmd.light.type at apply.c:132; nothing between anoptic_render.h's documented RenderLightType {0,1,2} and here validates it, ano_render_submit is a bare ring push), so an out-of-enum type reads struct memory past the array as its budget counter and, when that alias holds 0, passes the guard and applies the += 1 at :114 out of bounds 〜 type 7 lands exactly on rtSingleFreeCount and type 10 on rtPointFreeCount (the runtime frustum free-lists sit directly after the array), so a drained pool resurrects to count 1 and the next runtime caster pops a frustum block already owned by a live light; larger types write arbitrarily far past RendererState, and the same raw type rides LightData.type to the shaders while the budget pick falls through to SPOT 〜 test: anotest_shadowtypeguard 〜 device-free, builds where anoptic_render does
- seam-validation

apply.c:125 〜 the RCMD_CREATE arm forwards cmd.render_id into render_slots_alloc with no mapped-check on either side of the seam 〜 the alloc contract's one invariant "render_id unmapped" (render_slots.h:66) is enforced by nobody: alloc stores blind (render_slots.c:79-:80), and render_id is the producer's namespace arriving through a bare ring push 〜 so a duplicate CREATE of a live id mints a second physical slot, overwrites the forward map, and leaves the old slot's reverse entry holding the id: the old slot is stranded live forever (not free-listed, not quarantined, unreachable by resolve 〜 retire only ever finds the new slot via render_slots.c:125 and compact's peel loop at :181 never sees it) with its GPU entity entry still staged from the first CREATE so the cull pass draws a permanent undestroyable ghost, and render_slots_render_id_of(oldSlot) keeps answering the id (:120) so the pick readback (profiling.c:174) emits REVENT_PICK_RESULT naming a render_id the ECS may have retired and recycled for a different entity; the bulk twin overwrites identically at render_slots.c:93, and the light sibling on the same bridge guards exactly this shape (light_registry.c:89 refuses double-attach, apply.c:250 drops it) 〜 test: anotest_slotdupguard
- seam-validation

### Implementation bugs

swapchain.c:428 〜 createImageView's failure arm only logs and falls through to `return imageView`, a local the failed vkCreateImageView leaves with undefined contents (Vulkan output params are undefined on error), so the caller receives indeterminate stack bytes as a live VkImageView and has no failure channel at all; all ten call sites store it directly (swapchain.c:439, attachments.c:86/:108/:153/:199/:210/:226/:238, text_raster.c:806, texture.c:457), createImageViews returns true unconditionally (:441) so recreateSwapChain's views==NULL guard (:365) tests only the malloc, cleanupSwapChain (:187) then hands the garbage to vkDestroyImageView 〜 an invalid-handle VUID breach 〜 and the VK_NULL_HANDLE guards on the attachments-side teardown fields compare against garbage and pass, so every teardown after a failed view create is UB on real drivers 〜 test: anotest_imageviewguard
- no-abort

swapchain.c:110 〜 initSwapChain consumes the by-value SwapChainSupportDetails querySwapChainSupport returns 〜 two arrays calloc'd at :33/:38 (formats, presentModes) 〜 and returns on both arms (:157 failure, :178 success) without freeing either; the struct is a discarded local and no other backend code references the pointers (the only mentions in the tree are the alloc sites, the two choose* consumers, and the struct declaration), so the boot call (vulkanMaster.c:449) orphans both blocks and every resize recreation (recreateSwapChain, swapchain.c:353) orphans two more 〜 formatCount*8 + presentModesCount*4 bytes accrue per recreation without bound under window-drag resize storms 〜 test: anotest_swapleakguard
- ownership-leak

commands.c:201 〜 stagingTransfer's copy-failure arm is dead code: copyBuffer returns true unconditionally (:267) because the single-time-command pair beneath it discards every VkResult it sees (vkAllocateCommandBuffers :222, vkBeginCommandBuffer :228, vkEndCommandBuffer :235, vkCreateFence :240, vkQueueSubmit :247, vkWaitForFences :248), so a copy that never executed 〜 device-lost or OOM at submit, or a failed command-buffer allocation whose undefined out-param handle then rides vkBeginCommandBuffer/vkCmdCopyBuffer/vkQueueSubmit as invalid-handle VUID breaches 〜 reports success to the text-bake uploads (text_raster.c:639/:644) whose ok-chain exists to hear exactly this, and the overlay then draws curve/glyph buffers the data never reached; the arm's own body is wrong for the day a fix arms it, returning at :204 without the :208 vkDestroyBuffer and leaking the transient staging buffer 〜 test: anotest_stagingcopyguard
- no-abort

texture.c:426 〜 createTextureImage acquires its staging buffer at :415 and discharges it only in the success epilogue 〜 :445 hands it to outStagingBuffer or destroys it 〜 so both failure returns (:426 image creation, :432 layout transition) orphan the live VkBuffer, and no caller can recover it: *outStagingBuffer is written only at :445, so the glTF loop's calloc'd slot stays VK_NULL_HANDLE while stagingCount++ has already consumed it (ano_GltfParser.c:274) and the destroy loop at :296 no-ops on the hole 〜 one buffer object bound into the shared staging arena orphans per failed texture load, the reachable arm being createImage refusing under device memory pressure (gpu_alloc's 256 MiB block grab or vkCreateImage itself), exactly the pressure a loading spree produces; the sibling createTextureImageFromPixels orphans identically on its :368/:374/:382 arms (reached with NULL out from scene_buffers.c:479), and the :432 arm additionally strands the just-created textureImage plus its texture-arena allocation since the caller ignores the out-params on failure 〜 test: anotest_texstagingguard
- ownership-leak

texture.c:415 〜 createTextureImage discards createDataBuffer's bool status and consumes the out-params regardless: :417-418 memcpy the whole decoded image through stagingAlloc.mapped, which the callee's arena-exhaustion arm hands back as NULL (*allocation zeroed, commands.c:59-64) and its vkCreateBuffer arm never writes at all (commands.c:50-54, stagingAlloc is an uninitialized local at :414) 〜 a deterministic NULL write of texWidth*texHeight*4 bytes or a wild stack-pointer write on the loading thread, after which the run-on records copyBufferToImage from the VK_NULL_HANDLE or garbage stagingBuffer; the sibling createTextureImageFromPixels repeats the shape at :360-363, and the reachable arm is exactly the staging-arena pressure the :426 entry shows a loading spree produces 〜 where :426 orphans the buffer a successful acquire minted, this is the failed acquire never being noticed 〜 test: anotest_texacquireguard
- no-abort

render_slots.c:92 〜 render_slots_alloc_range publishes each element's mapping as it walks the batch (logicalToSlot[id] = base+i at :93, slotToLogical[base+i] = id at :94) but advances slotHighWater only in the epilogue (:96), so the mid-batch logical_reserve OOM return at :92 reports UNMAPPED while the already-walked prefix stays mapped into the un-owned high-water region 〜 the one OOM arm in a module that otherwise preserves its invariants explicitly ("Leaves *arr/*cap untouched on OOM" :16, "Quarantine OOM: leak the slot" :132, free-list OOM keeps quarantined :154); the caller cannot hear it (apply.c:167 discards the return and the RCMD_BULK_CREATE resolve loop at :170 stages GPU uploads, base poses and shadow tracking for the phantom prefix slots, all at or past slotHighWater so culling and animation never dispatch them), the next RCMD_CREATE re-hands slot base to a different render_id (:75 highWater++) so two live ids alias one physical slot, and a destroy of the stale id retires the live owner's slot through quarantine into the free-list (:128-:138, :156) while stripping its reverse mapping 〜 the slot is handed out a third time while still owned, exactly the double allocation the frame-gated quarantine exists to prevent 〜 test: anotest_slotrangeguard
- partial-publish

scene_buffers.c:35 〜 createMaterialBuffer's vkCreateBuffer failure arm logs ANO_FATAL and falls through 〜 ano_log(ANO_FATAL) is plain ano_log_write, which formats a record and returns 0, nothing in src/log aborts 〜 so :39 feeds vkGetBufferMemoryRequirements the failed call's out-param handle (undefined contents on error per spec, an invalid-handle VUID breach), gpu_alloc then sizes an allocation from garbage requirements, :46 binds the garbage handle, and the creator returns true publishing a dead per-frame buffer set into every material descriptor write; createTransformBuffer repeats the shape at :174-176/:179/:186 on the engine's transform lane, createCullingBuffers drops all six of its vkCreateBuffer results with no guard at all (:343/:359/:375/:392/:409/:424), and the same file's own siblings prove the intended contract by returning false on the identical check (createLightRuntimeBuffer :212, createIndirectDrawBuffer :249, createClusterBuffers :288/:295) 〜 reached at engine boot from ano_vk_create_scene_resources (:499/:505/:510) under exactly the device-memory pressure that makes vkCreateBuffer refuse 〜 test: anotest_scenebufferguard
- no-abort

texture.c:437 〜 createTextureImage discards generateMipmaps' bool while the fallback whole-chain TRANSFER_DST→SHADER_READ transition beneath it sits commented out (:439-443), and the generator's only failure arm 〜 the driver reporting no VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, checked at :239-243 〜 returns before recording a single barrier or blit, so the upload returns true with the image parked in TRANSFER_DST_OPTIMAL and mips 1..N-1 never written: the glTF caller then binds the full-chain view into the bindless array on that very success flag (ano_GltfParser.c:270/:284) with the descriptor pinning SHADER_READ_ONLY_OPTIMAL (texture.c:36), a wrong-layout sample on every draw 〜 VUID breach, garbage or device-lost 〜 plus undefined mip-tail content wherever the shared sampler's maxLod 20 (:487) descends; both formats the function uses today mandate FILTER_LINEAR on conformant drivers, so the live arms are non-conformant/layered drivers and the flag16 TODO's 16-bit formats (:397) where the feature is genuinely spec-optional, while the sibling createTextureImageFromPixels proves the intended shape with its explicit checked transition at :379 〜 test: anotest_texmipchainguard
- no-abort
- feature-gated-check

vulkanMaster.c:505 〜 initVulkan's depth and Hi-Z arms log ANO_FATAL "Quitting init" and fall through 〜 ano_log(ANO_FATAL) is plain ano_log_write, nothing aborts 〜 with no unInitVulkan and no return, while every sibling arm in the same function proves the intended contract with unInitVulkan(); return false; (:444-:446, :452-:454, :518-:520, :524-:526, ...), so a failed createDepthResources (:503-:506) or createHiZResources (:509-:512) keeps initializing: the layouts and pipelines build against a depthFormat never set, updateHiZDescriptorSets (:664) writes the never-created hizSampledView/hizMipViews into live per-mip sets 〜 an invalid-handle VUID breach at init 〜 the hizValidOrdinal warmup gate stays 0 because attachments.c:185 runs only on success so updateCullingBuffers trusts a pyramid that does not exist from the first frame, and initVulkan returns true (:687) handing main a "healthy" renderer that records every frame with no depth attachment; the depth arm is real hardware reality (findDepthFormat VK_FORMAT_UNDEFINED at attachments.c:63, or device-memory pressure on the per-view MSAA depth images), and createColorResources directly above (:501) cannot even report failure 〜 void return, the author's own "// TODO: make bool + check" 〜 test: anotest_initdepthguard
- no-abort

slot_upload.c:221 〜 ensureEntityCapacity's && growth chain publishes per arm as it walks: slot_upload_grow_device destroys the old device buffer and swaps in the new one before returning (:191-:193), but updateUboDescriptorSets runs only on full success (:276-:277), so a later arm's OOM 〜 vkCreateBuffer or gpu_alloc refusing at :179/:183, growBufferSet at :227-:234, or the mover reallocs at :238-:245 〜 returns false at :258 with the already-swapped prefix arms' old VkBuffers destroyed while the live descriptor sets still reference them by handle (descriptors.c:550/:743/:748 bind SlotUpload.device); the ANO_FATAL at :257 is plain ano_log_write and never aborts, both bridge callers drop the spawn and keep rendering (apply.c:123-:124, :164-:165), so the very next recorded frame binds destroyed buffers 〜 a GPU use-after-free / device-lost under exactly the memory pressure that made growth fail, and the torn prefix (SlotUpload.capacity already newCap, slots ceiling still oldCap since :270 is success-only) stays live and aliasable for any retry 〜 test: anotest_entitygrowguard
- partial-publish

descriptors.c:39 〜 createDescriptorPool's combined-image-sampler budget enumerates every consumer by name 〜 tonemap/view + 4 shadow + Hi-Z (pyramid+depth)/mip + cull bind11 pyramids/view + 1 text overlay 〜 but has no term for the task-cull Hi-Z sampler the global layout gains at binding 13 when taskCull is on (layouts.c:123-:128, bindingCount 14 at :149, written per view per frame at descriptors.c:483-:496), and with the shipped constants (3 frames, 2 views, 16 hiz mips) createDescriptorSets' own allocations then consume the budget exactly 〜 219 of 219 〜 so the pool's LAST consumer, the text overlay sets ano_vk_text_create_sets allocates from the same pool with tonemapSetLayout (text_raster.c:846/:852), is refused with VK_ERROR_OUT_OF_POOL_MEMORY and the graceful-degradation arm converts the sizing bug into silent feature loss: textOverlay is cleared with one WARN (:862-:864) and the UI overlay riding those sets dies with it, on exactly the mesh+task-shader hardware where taskCull defaults on (vulkanMaster.c:390); taskCull off leaves the 3-sampler slack that shows the +1u text term was meant to cover the overlay 〜 test: anotest_descpoolguard
- feature-list-drift

window.c:214 〜 initWindow guards glfwInit's failure with FATAL + return NULL (:173-:179) but never checks glfwCreateWindow, whose NULL return (headless or dead display: GLFW_PLATFORM_ERROR; a 0x0 configured resolution: GLFW_INVALID_VALUE) flows unguarded into glfwSetWindowUserPointer (:226), glfwSetFramebufferSizeCallback (:227), glfwGetWindowContentScale (:231) and six more callback registrations (:234-:241) 〜 every one requires a valid handle (assert in a debug GLFW, a straight NULL deref in release), so boot crashes inside GLFW before the function can honor its own header contract "returns a window pointer or NULL on failure" (instanceInit.h:26), and the caller's clean FATAL-and-unInitVulkan arm that exists to hear exactly this (vulkanMaster.c:324-:327) never runs; with ANO_POS set the crash moves earlier still to glfwSetWindowPos (:221) 〜 test: anotest_windowcreateguard
- no-abort

commands.c:82 〜 createUniformBuffers mints each view's per-frame camera UBO EXCLUSIVE with no asyncLc arm, while updateClusterDescriptorSets binds that buffer into the light-cull set at binding 0 (descriptors.c:308/:330) and the light-cull dispatch runs on the dedicated compute queue family when async light-cull is on (hiz.c:93-:111 records vr->lightcullSet, submit.c:113 submits to computeQueue), so lightcull.comp's per-froxel reads of view/proj/near/far/clusterDims (lightcull.comp:78-:118) cross queue families on an EXCLUSIVE resource with no ownership transfer anywhere in the tree (all 29 barriers pass VK_QUEUE_FAMILY_IGNORED) 〜 per the sharing-mode contract the compute family's reads are undefined and every froxel light list is built from spec-undefined camera state, on exactly the discrete-GPU hardware where asyncLc defaults on (vulkanMaster.c:385), surviving today only by desktop-driver coherency; the module's own rule exists for precisely this 〜 buffer_share_async_compute (slot_upload.c:46, "a buffer the async light-cull touches across queue families") treats every sibling binding in the same set, lightRuntimeBuffer (scene_buffers.c:208), both cluster buffers (:284), the light SSBO via computeShared (:56) 〜 and the binding-0 UBO is the one consumed buffer that misses it 〜 test: anotest_uboshareguard
- feature-list-drift

compute.c:83 〜 ano_vk_init_compute discards createShaderModule's documented NULL failure sentinel at all nine consumption sites (:83/:152/:197/:254/:262/:326/:386/:443/:478) and hands the dead handle to vkCreateComputePipelines as stage.module (:97/:166/:222/:292/:348/:400/:457/:492) with no maintenance5 shader-module-create-info chained anywhere 〜 an invalid-usage pipeline create (VUID-VkPipelineShaderStageCreateInfo-module), UB on real drivers, reached by host/device OOM at boot or a corrupt/truncated shipped .spv that loadFile reads whole without validating (pipeline.c:41 checks readability only), instead of the clean boot refusal the same function proves intended one line above every site (`if (!loadFile(...)) return false;`); the producer's own module documents the skipped check 〜 pipeline.c:89 mints the NULL and pipeline.c:104 guards the identical mint in ano_pipeline_task_stage 〜 while the eight unchecked implementations callocs beside the sites (:77/:146/:191/:249/:321/:381/:438/:473) and every graphics-side builder (flat.c:90-:92, shadow_pipe.c:52/:162/:214, tonemap.c:55, additive.c:68, transmission.c:75, text_raster.c:344/:379/:489) repeat the consumption shape 〜 test: anotest_shadermodguard
- no-abort

flat.c:90 〜 every graphics-side pipeline builder discards createShaderModule's documented NULL failure sentinel (pipeline.c:89) and bakes the dead handle into VkPipelineShaderStageCreateInfo.module for vkCreateGraphicsPipelines: the family head flat_init_with_cull mints three unchecked (:90-:92, consumed at :106/:112/:269 into the creates at :244/:261/:296) on all three boot lanes flat/twosided/masked, and the shape repeats across the family 〜 shadow_pipe.c:52-:53/:162-:163/:214-:215, tonemap.c:55-:56, additive.c:68-:69, transmission.c:75-:76, text_raster.c:344/:379-:380/:489-:490 〜 twenty consumption sites with pipeline.c:104 (ano_pipeline_task_stage) the lone guarded mint; an invalid-usage pipeline create (VUID-VkPipelineShaderStageCreateInfo-module, no maintenance5 fallback chained), UB on real drivers, reached by host/device OOM at boot or a corrupt/truncated shipped .spv that loadFile reads whole without validating, instead of the clean refusal every loadFile arm one line above proves intended, and a permissive driver returns success so init reports healthy with boot pipelines minted from a failed module; the compute-side twin is tallied at compute.c:83 〜 this is the graphics family it names in passing 〜 test: anotest_gfxshadermodguard
- no-abort

flat.c:244 〜 flat_init_with_cull discharges its three ano_aligned_malloc'd shader-code buffers (loadFile mints, pipeline.c:55) and minted VkShaderModules only in the success epilogue (:301-:309), so every failure return after the first acquisition orphans whatever is live 〜 :79 strands geomShaderCode, :88 both geometry buffers, :101 all three buffers plus the three modules minted at :90-:92, and the pipeline-create arms :244/:261/:296 strand three buffers plus three-to-four modules (taskModule rides on task lanes) 〜 all stack locals no code outside the function can reach: ano_pipeline_flat_cleanup never sees them and the boot caller returns false without even calling it (pipeline.c:128-:141), the author's own TODO names the missing "garbo removers for the shader buffers and modules" (pipeline.c:122), shadow_pipe.c:160-:161 shares the shape (returns false leaving geomCode/fragCode and geomModule/fragModule/taskModule live), and the :52 unchecked implementations calloc deref'd blind at :244 rides beside the family (text_raster.c:338 checks its identical calloc, proving intent) 〜 reached on all three boot lanes flat/twosided/masked by exactly the host-OOM / missing-or-corrupt .spv arms the returns exist to handle, orphaning memory under the very pressure that made them fire 〜 test: anotest_flatorphanguard
- ownership-leak

record.c:29 〜 recordCommandBuffer checks vkBeginCommandBuffer and only logs on failure 〜 the function is void so drawFrame has no failure channel (vulkanMaster.c:228) 〜 and the asyncLc split repeats the shape at :202 (prelude vkEndCommandBuffer logged, prelude submitted anyway) and :205 (main begin logged), so a begin refused under host/device memory pressure keeps recording: every vkCmd* from the :36 query-pool reset to the :287 present barrier lands on a command buffer never put in RECORDING state (VUID-vkCmd*-commandBuffer-recording, UB on real drivers), the :297 vkEndCommandBuffer on it is a second state breach, and ano_frame_submit consumes the never-recorded buffer as if executable (submit.c:44/:72/:94, a pCommandBuffers state VUID at the queue) 〜 device-lost territory every frame the pressure persists, while the frame module's own submit twin proves the intended contract by returning false on its failing call (submit.c:47-:51) and every other begin site in the tree (geometry.c:241, hiz.c:84/:98, text_raster.c:1054, commands.c:228) discards the result outright 〜 test: anotest_recordbeginguard
- no-abort

slot_upload.c:277 〜 ensureEntityCapacity recreates every entity-scaled buffer 〜 growBufferSet (:36) vkDestroyBuffer's each old per-frame live-transform SSBO after binding its replacement 〜 then re-points descriptors through updateUboDescriptorSets alone, but the shadowsetup compute set's binding 1 is that same transformBuffer.buffer[i] (descriptors.c:357/:376) and its only writer, updateShadowDescriptorSets, runs exactly once in initVulkan (vulkanMaster.c:666); nothing re-runs it on growth, and swapchain recreation re-runs only the Hi-Z and tonemap sets 〜 the first CREATE/BULK_CREATE that pushes slotHighWater past the current capacity (INITIAL_ENTITY_CAPACITY 10000) leaves frames[i].shadow.setupSet binding 1 referencing a destroyed VkBuffer, and PIPELINE_COMPUTE_SHADOWSETUP binds that set every frame (record.c:133, passes.c pass 2), so validation flags every subsequent dispatch and on a live device shadowsetup.comp reads whatever the bump allocator kept behind the freed handle: every shadow-frustum viewProj and the fragment-stage sampling-viewProj UBO derive from stale or garbage parent transforms, shadows permanently detach from their lights (or the device faults) after the first entity growth 〜 a boundary the demo scene never crosses 〜 test: anotest_vkguard
- missed-repoint

### Interlink / Composition bugs 

components.c:72 〜 ano_vk_register_texture is the only route a loaded texture's ownership takes into the teardown registry 〜 cleanupVulkan walks primitives.textureBuffers and nothing else destroys the loaded views/images (cleanup.c:64-:71) 〜 yet it returns void and its realloc-failure arm logs ANO_ERROR and drops the TextureData record (:76-:78), so the glTF caller cannot hear the refusal and proceeds to bindless-register and draw the view (ano_GltfParser.c:282-:288) whose VkImage/VkImageView/GpuAllocation now orphan permanently: still resident, still sampled every frame, unreachable at shutdown, one whole texture per refused growth under exactly the loading-spree memory pressure that makes realloc refuse; the mesh twin ano_vk_register_mesh (:44) shares the void-drop shape and has no production caller at all 〜 dead code 〜 test: anotest_texregisterguard
- ownership-leak

ano_GltfParser.c:277 〜 parseGltf hears createTextureImage's false and discharges nothing: the callee's post-create failure arms return false with the just-created VkImage and texture-arena allocation already written through the out-params into loadedImages[t]/loadedAllocs[t] (texture.c:429-:433 transition arm 〜 its strand half noted under texture.c:426 〜 plus the :446-:450 view arm that today cannot fire only because createTextureImageView swallows its own failure, the tallied swapchain.c:428 family, and the arm the texture.c:437 fix adds), yet the parser's failure route is textureLoaded[t] = false and continue (:276) 〜 adoption into the teardown registry is success-only (ano_vk_register_texture at :282, the sole route cleanupVulkan ever walks), no failure arm destroys the image or frees the allocation, and :619-:621 free the host arrays holding the only copies of the handles 〜 one whole device texture image plus arena allocation orphans permanently per failed load, armed by exactly the fixes the tallied callee entries demand, so repairing texture.c's arms without a parser-side discharge converts today's staging leak into a full texture leak 〜 test: anotest_gltftexleakguard
- ownership-leak



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Text

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Threads

### Interface-level bugs and logic inefficiencies

### Implementation bugs

threads_macos.c:79 〜 the Darwin barrier gap-fill samples generation before the arrived fetch_add (:81), and the completing thread's arrived reset (:84) has no guard against increments landing between the count-th arrival and the reset, so in the over-subscribed reuse POSIX defines (more threads than count sharing the barrier, released cohort by cohort 〜 glibc behind the Linux build handles it) a thread preempted between :79 and :81 while another cohort completes finds its :89 spin predicate already false and returns 0 as the sole arrival of a fresh round, and an increment racing the :84 reset is silently erased; exactly-count usage is provably correct, the divergence only bites shared-cohort usage 〜 an over-subscribed ano_thread_barrier_wait rendezvous (pairwise handoff, cohort batching) releases threads on macOS before count peers have arrived, so a waiter consumes partner data that was never written while the identical caller code is correct on linux/win64 〜 confirmed in source by three passes; no deterministic trigger seam from here (Darwin-only TU 〜 forcing __APPLE__ on glibc collides the pthread_spinlock_t/pthread_barrier_t typedefs 〜 and the window sits between two adjacent atomics, stress-only even on target) 〜 test: pending
- odd-sibling-out

### Interlink / Composition bugs 



## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

time_win64.c:148 〜 ano_timestamp_ticks in TSC mode returns raw __rdtsc with no re-anchor across power transitions: the TSC does not survive S3 sleep or S4 hibernate (the core power domain drops and firmware restarts the counter near zero 〜 the reason both Windows and Linux re-base their own TSC-derived clocks on resume), the invariant-TSC check at :55 only covers P/C/T states, the election at :130 is frozen for the process, and ano_ticks_to_ns is a pure function of the frozen cachedTscHz, so the first post-resume stamp lands below every pre-sleep stamp and ano_timestamp_raw/us/ms follow it backward; the QPC path the same function uses on non-invariant-TSC CPUs is immune (the kernel re-biases QPC at resume), as are linux CLOCK_MONOTONIC and darwin mach_absolute_time 〜 the header's monotonic promise (anoptic_time.h:20) breaks exactly on the machines that elected the fast path 〜 every u64 now-start delta held across a lid-close or hibernate wraps toward 2^64: log_core.c:395's anchor math stamps records ~146 years into the future at 4 GHz, audio watchdog and frame-pacing deltas explode, and ano_sleep's own elapsed check (:345) sees ~2^64 and cuts a mid-suspend sleep short 〜 confirmed in source by two passes (no power-broadcast hook, no re-election, no bias term anywhere in the tree); no deterministic trigger seam (needs a real S3/S4 resume on Windows x64, and the clock mode is a TU-private static with no injection hook) 〜 test: pending
- clock-not-reanchored

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Leads for future passes

(unverified suspicions surfaced during census iterations 〜 chase, confirm, then move up into a tally or strike)

- filesystem_win64.c:33 〜 len >= MAXPATH checked on the full exe path before trimming the filename; linux/macos check after trimming 〜 256-259 char full paths whose dir fits fail only on win64.
- filesystem_win64.c:23 〜 the ANSI-API debt comment covers GetModuleFileNameA only; CreateFileA and getenv("APPDATA") share the non-ASCII mangling but are not acknowledged.
- log↔filesystem seam 〜 log_core.c:834 and log_crash.c:34 consume ano_fs_logpath; behavior when it legitimately returns length 0 unaudited.
- memory↔every-module seam 〜 MI_OVERRIDE=OFF (CMakeLists.txt:105) + macro-only override (anoptic_memory.h:15) means two live allocators partitioned by "TU includes anoptic_memory.h or not"; any internally-allocating libc call (getline/asprintf/scandir) in a macro'd TU frees a glibc pointer with mi_free 〜 deserves a dedicated composition pass. Partially swept 2026-07-24: `ano_meshoptimizer.c` (24 sites), `geometry.c` (50) and `gpu_alloc.c` (2) were outside the domain entirely and now include the header 〜 verified for mesh by `nm -u` on the built object, which references only mi_malloc/mi_free. Each was self-contained (nothing they allocate is freed by another TU), so this was domain hygiene, not a live cross-allocator free. Still open: `light_registry.c`, `text_raster.c`, `shadow_resources.c` and `shadow_cache.c` reach the header only transitively (via structs.h -> render_bridge.h, and via pipeline.h), so their allocator is correct by an unrelated header's include chain and would flip silently if that chain were tidied 〜 the sweep should make every allocating TU include it directly. `src/render/gltf/scratch_process.c` (4 sites) is left alone as the dead-code lead above.
- instance.c:192 〜 getRequiredExtensions strdups extension strings, createInstance frees only the array 〜 per-string leak (vulkan_backend iteration).
- memory↔scoped-heap seam 〜 a LOCALHEAPATTR scratch heap silently gives up mimalloc's MI_DEBUG=2 overflow canary: the canary is only consulted by mi_free, and both mi_heap_destroy (what ano_heap_release calls) and mi_heap_delete reclaim pages wholesale without checking it. Measured 2026-07-24 on this tree's mimalloc-debug: mi_malloc + 16-byte overrun + mi_free reports "buffer overflow in heap block", while the same overrun through mi_heap_malloc + mi_heap_destroy and + mi_heap_delete both exit silent. ASan does not cover the gap either 〜 MI_OVERRIDE and MI_TRACK_ASAN are both OFF, so ASan never sees a mimalloc block at all. Every scoped-heap site is therefore un-instrumented for overruns: `text_bake.c:530`, `audio_mixer.c:664`, `ano_meshoptimizer.c`. The lever worth trying first is MI_TRACK_ASAN=ON for sanitizer builds, which is a CMake change rather than a source one and gives ASan every mimalloc block engine-wide; for the two text/audio sites, whose scratch is not sub-partitioned, that alone restores full per-allocation detection. It does NOT cover arrays carved out of one shared block (`ano_meshoptimizer.c` `ano_simplify_ex`, `ano_optimize_vertex_cache`), where an overrun of one sub-array into the next is invisible to any allocator-level tool by construction. A manual ASan-poisoned gap between sub-arrays was built and measured there on 2026-07-24 and then removed: it catches only overruns that stop inside the gap (verified 4 B and 12 B caught, 48 B and 256 B silent), which is a narrow slice of the failure it appears to guard, at the cost of a poison/unpoison protocol in the file's hottest function. Carved-arena opacity is a property of the layout, not a defect to instrument around 〜 the checks that actually pin this code are the byte-exact simplifier goldens.
- scratch_process.c (src/render/gltf/) 〜 dead code: not in any CMakeLists, no includer.
- memalign_win64.c:1 〜 guards _WIN64 while CMake selects on WIN32; a 32-bit Windows build gets an empty TU and a link failure (marginal, engine is 64-bit-only).
- threads_macos.h:15 〜 PTHREAD_BARRIER_SERIAL_THREAD lives only in the private header ("Never include outside src/threads"); on Linux pthread.h supplies it, on Darwin nothing public does 〜 portable caller code testing ano_thread_barrier_wait's documented serial return compiles on linux/win64 and fails to compile on macOS.
- threads↔log_crash seam 〜 the spawn trampoline's cleanup handler (threads.c:39) disarms the altstack before pthread TSD destructors run, so a stack-overflow crash inside any TSD destructor (mimalloc thread teardown included) faults with no alternate stack and the blackbox Stage-1 handler recurses instead of reporting.
- music_theory.c:48 〜 ano_mode_intervals lazy-bake static sets the baked flag after the table writes with no fence 〜 same cross-thread shape as the voicing race, benign on x86 today.
- music_host.c cadence_policy 〜 ano_music_set_override("cadence_policy", -1) stores ANO_CADENCE_NONE which ano_next_chord indexes into CADENCE_TARGET[]/PRE_CADENCE_FUNCTION[] 〜 OOB read for an enum-defined value.
- music_host.c override_apply 〜 has-flag written before value with plain stores; safe only while commands apply on the engine-owning thread 〜 any future cross-thread ano_music_set_* re-opens it.
- ano_GltfParser.c:71 〜 parseGltf ignores geometry_pool_upload_chain's failure return; on pool exhaustion lodBase stays 0 and the primitive silently renders the fallback cube (FALLBACK_MESH_INDEX 0), same silent-0 for skipped primitives.
- render texture usage 〜 textureSrgb[] is set true per-usage; a texture shared between a color slot (baseColor/emissive) and a data slot (metallicRoughness/normal) uploads once as sRGB, silently corrupting the data usage.
- ano_GltfParser.c:76 〜 posAccessor->count / indices->count size_t→u32 casts unguarded; >4G-element accessors truncate even after a validate gate lands (marginal).
- cgltf↔memory seam 〜 cgltf's implementation TU compiles under the malloc-macro override, so its internal alloc/free stay mimalloc-consistent only within that TU; any future cgltf call from a non-macro'd TU sharing cgltf_data cross-frees allocators (instance of the MI_OVERRIDE=OFF lead).
- ano_unicode_tables.h case trim 〜 anorune_to_upper(0x0292)==0x0292 while to_lower(0x01B7)==0x0292 〜 the trim kept one direction of the Ʒ/ʒ pair; same trim-boundary disease as the tallied collation bug, a coverage-consistency pass over tools/gen_unicode_tables.c would catch the whole class.
- ano_strings_collate.c:504 〜 mi_malloc-failure fallbacks (qsort + fb_sym_cmp_) break documented sort stability for byte-equal values with distinct backings; not deterministically testable in-process.
- strings↔log seam 〜 log renders %.*s of anostr_fmt output raw on malformed UTF-8 〜 unaudited.
- anostr_sort_idx 〜 count > UINT32_MAX silently leaves the identity permutation while anostr_sort sorts; unrepresentable-permutation edge the header is silent on.
- text↔ui seam 〜 ano_quad_split_monotone/ano_half_pack/unpack defined in text_bake.c but re-declared with a duplicated AnoQuad type in ui_path.h; the two definitions must stay bit-identical and only a size static_assert guards them 〜 ODR/ABI seam.
- render↔text seam 〜 textcoverage.glsl mirrors text_raster_ref.c statement-for-statement (the C ref is the GPU's validation oracle) with no enforcement coupling; drift in one silently corrupts the RMS verification.
- text_bake.c ano_text_window_sum 〜 reads pts[g->pointOffset] unconditionally when curveCount==0 〜 OOB by one uint32 for a blank last glyph with pointOffset==pointCount; every in-tree caller gates on curveCount>0, latent precondition gap.
- text GPOS caps 〜 GPOS_MAX_LOOKUPS=16 / GPOS_MAX_SUBS=32 truncate silently; a font spreading kern across more lookups/subtables loses pairs with no contract mention.
- engine↔render_bridge seam 〜 the blocking retry spins (main.c:48/162/683) never check g_logicShouldStop and main stops draining the command ring before joining logicThread (main.c:1057) 〜 unreachable today only because the 4096-slot ring exceeds startup command volume; scene growth past ring capacity turns close-during-spawn into a shutdown hang.
- vulkanMaster.c:593 〜 ano_render_load_scene_assets failure returns false without unInitVulkan(), unlike every sibling failure arm 〜 leak/asymmetry.
- engine↔music panel 〜 main.c:613 guards chordDegree but not keyTonic/mode 〜 the OOB half refuted 2026-07-25: the delivered tonic funnels through AnoScale.tonic, a uint8_t (music_theory.h:28, both writers cast), so PC_NAMES[keyTonic % 12] is always in bounds, and mode is mode_ok-validated upstream. What remained was a labelling defect 〜 an out-of-contract config tonic truncates and the panel names a wrong key 〜 fixed the same day by ((keyTonic % 12) + 12) % 12 at expand()'s ingress beside its mode/cadence siblings, plus a static_assert on PC_NAMES' extent. Unsigning was verified wrong: wander_target's keyTonic + 7*step is sign-dependent.
- log_crash↔log teardown 〜 process-lifetime crash hooks outlive ANO_LOG_SCOPE_ATTR cleanup at main exit; POSIX path audited safe, log_crash_win64.c not audited.
- render_slots.c:84 〜 render_slots_alloc_range mid-loop logical_reserve OOM returns UNMAPPED leaving earlier mappings pointed into un-reserved high-water slots with no rollback 〜 a later alloc double-maps them; OOM-only path.
- slot_upload.c growth VRAM 〜 growBufferSet/slot_upload_grow_device never return the old GpuAllocation (bump allocator, no free) 〜 every entity growth leaks the prior span; "handle only" comment may mean accepted design.
- swapchain recreate↔descriptor ownership seam 〜 recreate re-runs only Hi-Z/tonemap descriptor sets (swapchain.c:389) 〜 any per-frame resource recreated there but bound elsewhere repeats the shadowsetup-dangling-descriptor shape.
- vulkan_backend↔render_bridge adopted blocks 〜 RCMD_TEXT_SET/RCMD_UI_SET use a different free contract than free_owned_bulk 〜 ownership seam unaudited.
- apply.c:346 〜 REVENT_SLOT_RETIRED emitted fire-and-forget while anoptic_render.h promises lifetime facts are lossless; a retirement burst past the 256-slot input reserve with slow logic drain silently strands render_ids (latent: main.c ignores the event today).
- bridge dead protocol 〜 REVENT_BATCH_CONSUMED has no emitter and RCMD_BULK_CREATE no submit helper; the header-documented borrowed-batch producer-frees-on-ack lifetime is unimplemented 〜 a conforming producer waits forever. Near-certain future tally.
- ano_render_ui_set validation gap 〜 paints' stopFirst/stopCount never checked against the block's stopCount; after compose rebase a hand-built block samples other blocks' gradient stops (GPU bounded, CPU ref evaluator could read OOB).
- transformStream reclaimSeq 〜 produceSeq/curSeq cross threads via ring ordering (audited sound) but the reclaimSeq writer side is unaudited.
- music inward/outward clamp asymmetry (generic) 〜 music_ir.c:112/:115 clamp accentDepth/registerCenter outward while override_apply casts raw doubles inward (music_host.c:187/:188) 〜 same shape as the tallied velocity seam; other lanes unaudited for overflow from those.
- tempo_bpm override 〜 raw double into mapped_params with no validation; a 0/negative pin crossing into ano_synth_live_bar's barSeconds = barQ*60/tempoBpm division is unaudited.
- time_win64.c:316 〜 ano_sleep with us <= 1000 skips the coarse stage entirely and is a pure busywait, while the header (anoptic_time.h:62) promises "Yields to the scheduler"; anotest_time.c:173 already calls it "spin-only on Windows", so decide whether the header or the implementation is the contract.
- time_linux.c:132 〜 clock_nanosleep returns its error directly and does not set errno, so the non-EINTR failure path perrors stale state and returns a stale errno (possibly 0 = success); unreachable with the tv_sec/tv_nsec this wrapper builds, but the convention is wrong.
- time suspend semantics 〜 even after the win64 TSC re-anchor lands, the three platforms disagree on what a monotonic delta held across a system sleep means: linux CLOCK_MONOTONIC and intel-mac mach_absolute_time exclude the sleep, win QPC and apple-silicon mach_absolute_time include it 〜 the header is silent and the audio/music schedulers consume these deltas cross-platform.



## Census (2026-07-18)

Post-merge tally of this file after splicing the attached census pass with the in-repo BUGS.md. Severity is inferred from the writeups (entries carry no severity tags). Leads are unverified and excluded from the tallied counts unless noted.

Amended 2026-07-24: the math-conventions lead was chased and split 〜 the row-major/column-major half was resolved and struck (the header comment was wrong, the doc right, no code change), and the alignment half was promoted to the tallied entry anoptic_math.h:21 (Latent). Net: tallied 69 -> 70, leads 42 -> 41, file items unchanged at 111.

### Rubric

| Level | Meaning |
|---|---|
| Critical | Memory corruption, crash/segfault, GPU device-lost / invalid-handle UB, or uninitialized use |
| Major | Wrong results, contract break, leak, race, or feature loss under plausible use |
| Latent | Explicitly latent / unreachable today / absurd-arg-only / exotic seam |

### Tally

| Severity | Count |
|---|---:|
| Critical | 32 |
| Major | 26 |
| Latent | 12 |
| **Tallied total** | **70** |
| Leads (unverified) | 41 |
| File items (tallied + leads) | 111 |

### Severity by section

| Section | Critical | Major | Latent | Total |
|---|---:|---:|---:|---:|
| Audio | 1 | 4 | 1 | 6 |
| Collections | 0 | 0 | 0 | 0 |
| Filesystem | 0 | 2 | 0 | 2 |
| Log | 2 | 0 | 0 | 2 |
| Math | 0 | 0 | 1 | 1 |
| Memory | 0 | 1 | 0 | 1 |
| Mesh | 1 | 1 | 0 | 2 |
| Music | 5 | 2 | 0 | 7 |
| Render / Vulkan | 18 | 10 | 2 | 30 |
| Strings | 0 | 1 | 1 | 2 |
| Synth | 2 | 1 | 0 | 3 |
| Text | 0 | 2 | 1 | 3 |
| Threads | 0 | 1 | 0 | 1 |
| Time | 0 | 1 | 4 | 5 |
| UI | 2 | 0 | 2 | 4 |
| Engine | 1 | 0 | 0 | 1 |
| **Total** | **32** | **26** | **12** | **70** |

Render holds 18/32 Criticals. Music is next (5). Time is almost all Latent.

### Remediation status (2026-07-24)

The `[X] Fixed` entries this section counts, and the one `wontfix`, are no longer in this file: they moved verbatim to `docs/BUGS_DONE.md` on 2026-07-25, grouped under the module headings they came from.

The one-off pass: entries whose fix is local to one function or one platform-sibling set, needing no API change and no new mechanism. 36 of the 70 tallied entries were `[X] Fixed` by it, and one more (`time_win64.c:310`) carries a `wontfix`. The severity tables above describe the census as found and are deliberately not rewritten.

A second pass on 2026-07-25 ran the other way, deleting thirteen guards that rejected unreachable inputs to prevent contained effects. Two of them, in `time_win64.c`, amend fixes this pass had landed the day before. The record is in BUGS_DONE.md, "Removed guards", together with the incidental findings the sweep turned up 〜 unverified, and not counted in the census above.

A third pass, later on 2026-07-25, verified those incidental findings and implemented the eleven determinations recorded in BUGS_DONE.md, "Remediation determinations": five landed structural (tier 1), six as single-seam invariants (tier 4), none as fault-site guards, each with type-level welds 〜 static_asserts binding table extents, cap relations, ABI masks and field types to the invariants the code now assumes. One census claim was refuted outright (the PC_NAMES index, under Engine seams), and five entries entered the census already fixed: delay.h, music_host.c motif n, ui_raster_ref.c tile entries, window.c monitor query, ano_render_bridge.c pose seam. It also settled the log_core.c:817 open decision and deleted the time module's entire clock-failure sentinel convention. Guard movement this pass: anoptic_logflood, anoptic_gltfguard and anoptic_boottelemetryguard closed; four new guards landed green (motifboundguard, dspdelayguard, uitileentryguard, cameraposeguard). The headless suite stood at four failures, all of them the remaining open decisions.

A fourth pass, later on 2026-07-25, settled the open decisions themselves and implemented all four the same day. The tier policy ranked answers the write-ups had treated as peers: the destroy-time ring drain landed owner-side in both render_bridge and audio (tier 1 completes the one asymmetric sibling among the drop paths; tier 2 rejects a type-erased release callback in the transport), the audio rides-home promise took its teardown clause outright since a two-phase drain exports a liveness obligation no tier can home, and the music ownership question dissolved at the verify gate 〜 the candidate table is call-transient, so thread_local scratch changes nothing observable, and music_theory.c's lazy bake died with it, replaced by rodata. The strings closure direction fell to measurement: the divergence set is empty, so the decomp trim drops dangling redirects, the case trim closes the same way (26 broken round-trip mappings now return identity), and assert_trim_closure fails generation before a file is written if either table reopens. One incidental entered the census already fixed 〜 the generator's padding-memcmp dedup, which made regeneration nondeterministic 〜 and one new open entry was filed (music_voicing.c:120, the voices ingress), then closed the same day: verifying that seam widened it to two sibling holes (a pcCount row overrun in the drop branch, a zero-pc fallback read), all three retired together. Guard movement: anotest_strguard, anotest_bridgeguard, anotest_audioshutguard and anotest_musicguard closed, anotest_voicingboundguard landed green; the headless suite is green for the first time, and TSan runs the music guard clean where it confirmed the race before.

| Section | Fixed | Tallied |
|---|---:|---:|
| Audio | 6 | 7 |
| Filesystem | 1 | 2 |
| Log | 2 | 2 |
| Math | 1 | 1 |
| Memory | 1 | 1 |
| Mesh | 2 | 2 |
| Music | 9 | 9 |
| Render / Vulkan | 10 | 32 |
| Strings | 3 | 3 |
| Synth | 3 | 3 |
| Text | 3 | 3 |
| Threads | 0 | 1 |
| Time | 3 | 5 |
| UI | 5 | 5 |
| Engine | 1 | 1 |
| **Total** | **50** | **77** |

What is left is not leftovers, it is the systemic mass the census exists to name. Render still holds 22 open entries, nearly all of them the Vulkan Result + abort/unwind swoop and the GPU bind/rebind registry; those cannot be retired one file at a time and doing so would be the whack-a-mole the tally was written to prevent. Four open entries were a different case again 〜 blocked on contract decisions with more than one defensible answer that a bug fix could not settle quietly. On 2026-07-25 the tier policy settled three and a measurement dissolved the fourth; all four landed the same day and are retired to `docs/BUGS_DONE.md`, "Settled open decisions".

Guard-test movement: 26 failing to 0 〜 the fourth pass closed the last four, and no red test remains in the headless suite. Entries fixed without a runnable gate here are the Vulkan-gated ones (`texture.c:435`, `device.c:663`, `gpu_alloc.c:12`, `record_views.c:302`, `render_slots.c:35`) and the ones the census itself marks `test: pending` (`audio_linux.c:168`, the Time set, `ano_strings_ops.c:86`, `anoptic_math.h:21`).

### Context

For a systematic audit census of a C23 + Vulkan + lock-free engine, ~70 tallied findings is in band; many Criticals are bad-input / OOM / rare-device paths the demo never crosses, not daily boot crashes. The point of tallying before whack-a-mole was to expose systemic gaps.

### Systemic gaps (fell swoops)

Local one-offs will remain (limiter window, mesh simplify, collation CE/decomp, Darwin barrier, TSC resume, measure_runs, voicing race). Most Critical mass 〜 especially Render + Music 〜 clusters into a few missing disciplines:

| Swoop | Rough blast radius |
|---|---|
| Vulkan Result + abort/unwind (FATAL aborts or tears down; no publish-on-failure; every fallible create checked) | ~12–15 Render Criticals |
| GPU resource → descriptor bind registry (recreate invalidates + rewrites all dependents before old handle destroy) | ~4–6 growth/UAF/rebind |
| Music/synth validate-at-ingress (create / set_override / config adopt + synth event upper bounds) | ~6–8 Music+Synth |
| Checked size arithmetic (`ano_mul_u32` / `ano_add_u32` or equivalent as the only cap/size path) | ~6–8 wrap guards |
| Ownership discharge on destroy/failure (adopted blocks, staging, pipeline temps) | ~6–8 leaks |
| Asset validate gate (`cgltf_validate` before accessor reads) | 1 + class |

Stacked: plausibly ~70–80% of Criticals and a large Major chunk 〜 not 90% of the whole file. Highest leverage order: Result discipline, bind registry, music/synth ingress, then arithmetic + ownership as house rules. Do not chase folder reshuffles or MI_OVERRIDE first; the gaps are seams and obligations.

### Three layers (do not conflate)

`backup-resource-manager` and the Vulkan Result swoop hit different layers. Conflating them leaves Render Criticals standing after a large merge.

| Layer | What backup-resource-manager aimed at | What the Criticals need |
|---|---|---|
| Asset / CPU resource mgr | Logical paths, rid registry, load-to-caller-heap, durable writes, bytes vs meaning | Hostile/truncated file loads, path chaos, size-then-read 〜 not VkBuffer UAF |
| Thread interconnects | `anoring_*` / `anoseqpub` / tickets in `anoptic_collections.h`; bridges migrate onto them | One ownership contract for adopted payloads; stop private twin rings |
| Vulkan Result + GPU bind graph | Essentially absent (GPU allocations stay renderer-owned per resource-manager-ownership.md) | FATAL fallthrough, discarded VkResult, growth/recreate without rebind |

Facts:

- Vulkan Result + abort/unwind does not depend on resourcemg. It is local `vulkan_backend` discipline. Resourcemg helps the load side (shaders via `ano_res_load`, kill `loadFile` antipatterns) but will not stop NULL shader modules riding into pipeline creates or depth init logging FATAL and continuing.
- The resource manager moves handles; the renderer owns GPU allocations. Resourcemg is not the descriptor/rebind swoop. That is a thin renderer-side table: device object live at (set, binding, frame); destroy ⇒ rebind or retire dependents.
- Rings: standardize, do not remake. `docs/URGENT-audiorace.md` already closed this 〜 `anoring_spsc` matches the audio/render bridge design; migration value is dedup + one destroy/drain contract. Land collections with the word-lane seqpub fix (branch `anoseqpub` still had the plain-memcpy race), migrate audio/render/log onto it, encode once: push = ownership transfer; destroy = drain + free `bulk_owned` / retired blocks.
- Practical order: (1) Vulkan Result + unwind and a small GPU bind/rebind registry; (2) promote collections rings with seqpub fixed and existing bridge ownership rules honored; (3) port resource manager (pools + `ano_res_load` + registry) for the asset/path class, with parsers still validating meaning.
