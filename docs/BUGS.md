# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.

## Tags

Each entry carries one or more root-cause tags on the bullet line(s) beneath it, so the census reads by shape as well as by module. Three root causes dominate the board; the rest are smaller shared shapes or 1:1 singletons.

An entry prefixed `[X] Fixed` has been repaired; its text is kept verbatim as the record of what was wrong, and a closing `fix (date)` bullet says what was done. An entry carrying a `wontfix (date)` bullet instead is a finding whose analysis stands but whose repair the owner declined, with the reason on that bullet; it counts as unremediated. A `fenced (date)` bullet marks an open entry a pass deliberately worked around rather than fixed, with the reason and the guard's expected colour on that bullet; it too counts as unremediated. None of the three is a tag, and all are excluded from every tag count.

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
- partial-out-param 〜 a fallible call's failure arm writes some out-params and leaves others untouched, so the caller cannot tell what it now owns; the discharge-side twin of no-abort, named by the 2026-07-25 remediation.
- silent-drop 〜 an out-of-contract or unsatisfiable request is neither honored nor refused: it is discarded with no diagnostic and no failure channel, so the producer believes it took effect and the scene is simply wrong.

One status tag rides alongside the root causes rather than replacing them:

- pending-design-decision 〜 the defect is understood and its root cause is already tagged, but the repair is blocked on a contract choice with more than one defensible answer, so writing the fix would settle that question silently. Never the only tag on an entry. Each carries a write-up 〜 the decision, the candidate answers, what each costs 〜 in docs/BUG-HUNT.md, "Open decisions". Status tags are excluded from every root-cause tag count. All five carriers were settled by 2026-07-25 〜 the tag currently marks nothing; the retired write-ups live in docs/BUGS_DONE.md, "Settled open decisions".


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 〜 no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

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

apply.c:125 〜 the RCMD_CREATE arm forwards cmd.render_id into render_slots_alloc with no mapped-check on either side of the seam 〜 the alloc contract's one invariant "render_id unmapped" (render_slots.h:66) is enforced by nobody: alloc stores blind (render_slots.c:79-:80), and render_id is the producer's namespace arriving through a bare ring push 〜 so a duplicate CREATE of a live id mints a second physical slot, overwrites the forward map, and leaves the old slot's reverse entry holding the id: the old slot is stranded live forever (not free-listed, not quarantined, unreachable by resolve 〜 retire only ever finds the new slot via render_slots.c:125 and compact's peel loop at :181 never sees it) with its GPU entity entry still staged from the first CREATE so the cull pass draws a permanent undestroyable ghost, and render_slots_render_id_of(oldSlot) keeps answering the id (:120) so the pick readback (profiling.c:174) emits REVENT_PICK_RESULT naming a render_id the ECS may have retired and recycled for a different entity; the bulk twin overwrites identically at render_slots.c:93, and the light sibling on the same bridge guards exactly this shape (light_registry.c:89 refuses double-attach, apply.c:250 drops it) 〜 test: anotest_slotdupguard
- seam-validation
- fenced (2026-07-25) 〜 one of the seven resource-management entries the remediation campaign deliberately left open: the render-slot ledger's custody chain cannot be settled a call site at a time, and every round's RCMD_CREATE / RCMD_BULK_CREATE arm is byte-identical by fence. anotest_slotdupguard stays red on purpose.

### Implementation bugs

swapchain.c:110 〜 initSwapChain consumes the by-value SwapChainSupportDetails querySwapChainSupport returns 〜 two arrays calloc'd at :33/:38 (formats, presentModes) 〜 and returns on both arms (:157 failure, :178 success) without freeing either; the struct is a discarded local and no other backend code references the pointers (the only mentions in the tree are the alloc sites, the two choose* consumers, and the struct declaration), so the boot call (vulkanMaster.c:449) orphans both blocks and every resize recreation (recreateSwapChain, swapchain.c:353) orphans two more 〜 formatCount*8 + presentModesCount*4 bytes accrue per recreation without bound under window-drag resize storms 〜 test: anotest_swapleakguard
- ownership-leak
- fenced (2026-07-25) 〜 resource-management entry deliberately left open by the remediation campaign; querySwapChainSupport's two calloc'd arrays and initSwapChain's by-value consumption are byte-identical after four rounds of renderer work in the same file. anotest_swapleakguard stays red on purpose.

texture.c:426 〜 createTextureImage acquires its staging buffer at :415 and discharges it only in the success epilogue 〜 :445 hands it to outStagingBuffer or destroys it 〜 so both failure returns (:426 image creation, :432 layout transition) orphan the live VkBuffer, and no caller can recover it: *outStagingBuffer is written only at :445, so the glTF loop's calloc'd slot stays VK_NULL_HANDLE while stagingCount++ has already consumed it (ano_GltfParser.c:274) and the destroy loop at :296 no-ops on the hole 〜 one buffer object bound into the shared staging arena orphans per failed texture load, the reachable arm being createImage refusing under device memory pressure (gpu_alloc's 256 MiB block grab or vkCreateImage itself), exactly the pressure a loading spree produces; the sibling createTextureImageFromPixels orphans identically on its :368/:374/:382 arms (reached with NULL out from scene_buffers.c:479), and the :432 arm additionally strands the just-created textureImage plus its texture-arena allocation since the caller ignores the out-params on failure 〜 test: anotest_texstagingguard
- ownership-leak
- fenced (2026-07-25) 〜 resource-management entry deliberately left open. The campaign fixed the acquisition side of this seam twice around it (texture.c:415's unnoticed refusal at round 1, the staging-refusal arms' out-param totality at round 4) and each time stopped strictly before the acquisition, so the custody span is unchanged and unforeclosed. anotest_texstagingguard stays red on purpose.

render_slots.c:92 〜 render_slots_alloc_range publishes each element's mapping as it walks the batch (logicalToSlot[id] = base+i at :93, slotToLogical[base+i] = id at :94) but advances slotHighWater only in the epilogue (:96), so the mid-batch logical_reserve OOM return at :92 reports UNMAPPED while the already-walked prefix stays mapped into the un-owned high-water region 〜 the one OOM arm in a module that otherwise preserves its invariants explicitly ("Leaves *arr/*cap untouched on OOM" :16, "Quarantine OOM: leak the slot" :132, free-list OOM keeps quarantined :154); the caller cannot hear it (apply.c:167 discards the return and the RCMD_BULK_CREATE resolve loop at :170 stages GPU uploads, base poses and shadow tracking for the phantom prefix slots, all at or past slotHighWater so culling and animation never dispatch them), the next RCMD_CREATE re-hands slot base to a different render_id (:75 highWater++) so two live ids alias one physical slot, and a destroy of the stale id retires the live owner's slot through quarantine into the free-list (:128-:138, :156) while stripping its reverse mapping 〜 the slot is handed out a third time while still owned, exactly the double allocation the frame-gated quarantine exists to prevent 〜 test: anotest_slotrangeguard
- partial-publish
- fenced (2026-07-25) 〜 resource-management entry deliberately left open; the render-slot ledger is one custody chain with apply.c:125 and both are held together. anotest_slotrangeguard stays red on purpose.

flat.c:244 〜 flat_init_with_cull discharges its three ano_aligned_malloc'd shader-code buffers (loadFile mints, pipeline.c:55) and minted VkShaderModules only in the success epilogue (:301-:309), so every failure return after the first acquisition orphans whatever is live 〜 :79 strands geomShaderCode, :88 both geometry buffers, :101 all three buffers plus the three modules minted at :90-:92, and the pipeline-create arms :244/:261/:296 strand three buffers plus three-to-four modules (taskModule rides on task lanes) 〜 all stack locals no code outside the function can reach: ano_pipeline_flat_cleanup never sees them and the boot caller returns false without even calling it (pipeline.c:128-:141), the author's own TODO names the missing "garbo removers for the shader buffers and modules" (pipeline.c:122), shadow_pipe.c:160-:161 shares the shape (returns false leaving geomCode/fragCode and geomModule/fragModule/taskModule live), and the :52 unchecked implementations calloc deref'd blind at :244 rides beside the family (text_raster.c:338 checks its identical calloc, proving intent) 〜 reached on all three boot lanes flat/twosided/masked by exactly the host-OOM / missing-or-corrupt .spv arms the returns exist to handle, orphaning memory under the very pressure that made them fire 〜 test: anotest_flatorphanguard
- ownership-leak
- fenced (2026-07-25) 〜 resource-management entry deliberately left open, and the head of the SPIR-V orphan family now tallied at compute.c:80. The campaign twice declined a tier-1 load+mint+free acquisition helper in this builder precisely because it would have implemented half of this entry inside a shader-module fix; the gate it landed instead sits on neither loadFile nor vkCreateGraphicsPipelines, so nothing here is foreclosed. anotest_flatorphanguard stays red on purpose.

compute.c:80 〜 the SPIR-V orphan family flat.c:244 names in passing, tallied in its own right: every pipeline builder acquires ano_aligned_malloc'd shader code and VkShaderModules early and discharges them only on the success tail, so each early return between an acquisition and its tail abandons whatever is live 〜 ano_vk_init_compute holds eight code buffers and eight modules across seven later loadFile refusals, eight pipeline-create refusals and (since 2026-07-25) eight module gates, discharging only at :101-102/:170-171/:226-227/:296-302/:351-352/:403-404/:460-461/:495-496; shadow_pipe.c:157-163 vs :172-175 and :209-215 vs :257-260, tonemap.c:52-56, additive.c:63-80, transmission.c:67-87 and text_raster.c:341-344/:371-380/:481-490 all repeat the acquire-early / discharge-on-success-only layout 〜 the round-2 and round-3 calloc arms were deliberately placed BEFORE the first acquisition so they could not join this family, which is the measure of how carefully it has been left alone 〜 logged 2026-07-25 (rounds 1 and 3) 〜 test: pending 〜 anotest_flatorphanguard pins the flat.c head only; the family needs one discharge decision, not seven
- ownership-leak

shadow_resources.c:22 〜 createShadowResources has sixteen `return false` arms and not one discharges anything acquired above it: an arm at :43/:46/:47 leaves frame i's frustumBuffer created and bound plus every earlier frame's pair live; :69-:88 leaves the m==0 moment image, its GpuAllocation, its array view and up to ANO_SHADOW_ATLAS_LAYERS layer views live on top of all MAX_FRAMES_IN_FLIGHT frustum/sampleVP pairs; :148-:159 leaves both moment images and every view live; :165-:168 leaves the whole shadow rig plus the first slot_upload_create's device allocation live 〜 the caller treats false as "init failed" and unwinds through the module cleanup path, but only for state it can see, and the half-written frames[i].shadow and state->shadow* fields hold created-but-unregistered handles on every arm above their own assignment; boot-only, every arm a driver or host refusal, so nothing renders wrong 〜 it matters because ANO_FATAL/ANO_ERROR are diagnostic-only and an init failure is supposed to be a full unwind, which this function does not perform for its own acquisitions 〜 logged 2026-07-25 (rounds 2 and 4), left unforeclosed: the round-4 status fix at :102-104 touched a distinct defect and added no discharge 〜 test: pending 〜 the complete fix is one labelled unwind or acquisition ledger covering all sixteen arms, decided together with what the transient CB and the SlotUploads do on refusal
- ownership-leak

texture.c:486 〜 every createTextureImage arm past the staging acquisition 〜 createImage (:486-490), transitionImageLayout (:493-497), copyBufferToImage (:499-503), generateMipmaps (:506-510), createTextureImageView (:513-517) 〜 returns false with *textureImage and *textureImageAlloc published and live but *textureImageView never written, and every arm above :512 additionally leaves the staging VkBuffer live with *outStagingBuffer unwritten, so the parser's calloc'd stagingBuffers slot stays VK_NULL_HANDLE while stagingCount++ has already consumed it and the discharge loop no-ops on the hole; createTextureImageFromPixels carries the twin at :411-427 〜 totalling these arms means deciding the staging discharge and the image's fate on the same lines, which is the texture.c:426 entry plus ano_GltfParser.c:277, so the campaign added no discharge, no zeroing and no comment on any of them 〜 logged 2026-07-25 (round 4) 〜 test: anotest_texstagingguard (the fenced red covers the staging half)
- partial-out-param
- ownership-leak

shadow_casters.c:151 〜 static shadow release deactivates a block but never returns its region entries: shadowFrustumNext is monotonic and there is no static free-list, accepted by ruling for the revoke and same-footprint rebuild paths, which consume zero region 〜 the residual is footprint churn on one row (point -> spot -> point ...), where each shape change strands the previous block and bumps a fresh one, so roughly 26 shape changes across a scene exhaust ANO_SHADOW_STATIC_FRUSTUM_COUNT and every later static caster degrades silently to shadowless; bounded and non-corrupting (the region check at :154 holds and stranded blocks are active=0, costing nothing per frame) but permanent for the process 〜 logged 2026-07-25 (round 2) 〜 test: anotest_shadowreregguard pins the release and the same-footprint reuse, not the churn
- ownership-leak

### Interlink / Composition bugs 

components.c:72 〜 ano_vk_register_texture is the only route a loaded texture's ownership takes into the teardown registry 〜 cleanupVulkan walks primitives.textureBuffers and nothing else destroys the loaded views/images (cleanup.c:64-:71) 〜 yet it returns void and its realloc-failure arm logs ANO_ERROR and drops the TextureData record (:76-:78), so the glTF caller cannot hear the refusal and proceeds to bindless-register and draw the view (ano_GltfParser.c:282-:288) whose VkImage/VkImageView/GpuAllocation now orphan permanently: still resident, still sampled every frame, unreachable at shutdown, one whole texture per refused growth under exactly the loading-spree memory pressure that makes realloc refuse; the mesh twin ano_vk_register_mesh (:44) shares the void-drop shape and has no production caller at all 〜 dead code 〜 test: anotest_texregisterguard
- ownership-leak
- fenced (2026-07-25) 〜 resource-management entry deliberately left open: the registration channel is the adoption half of the parser's custody chain and moves with ano_GltfParser.c:277. anotest_texregisterguard stays red on purpose.

ano_GltfParser.c:277 〜 parseGltf hears createTextureImage's false and discharges nothing: the callee's post-create failure arms return false with the just-created VkImage and texture-arena allocation already written through the out-params into loadedImages[t]/loadedAllocs[t] (texture.c:429-:433 transition arm 〜 its strand half noted under texture.c:426 〜 plus the :446-:450 view arm that today cannot fire only because createTextureImageView swallows its own failure, the tallied swapchain.c:428 family, and the arm the texture.c:437 fix adds), yet the parser's failure route is textureLoaded[t] = false and continue (:276) 〜 adoption into the teardown registry is success-only (ano_vk_register_texture at :282, the sole route cleanupVulkan ever walks), no failure arm destroys the image or frees the allocation, and :619-:621 free the host arrays holding the only copies of the handles 〜 one whole device texture image plus arena allocation orphans permanently per failed load, armed by exactly the fixes the tallied callee entries demand, so repairing texture.c's arms without a parser-side discharge converts today's staging leak into a full texture leak 〜 test: anotest_gltftexleakguard
- ownership-leak
- fenced (2026-07-25) 〜 resource-management entry deliberately left open, and the reason the campaign's four texture rounds all stopped before the acquisition: the entry's own warning held, so the arms it names are unchanged and the failure route still adopts nothing. The round-4 bindless-full latch landed in the same function without touching them (anotest_gltflatchguard is green beside the red). anotest_gltftexleakguard stays red on purpose.



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

### Interlink / Composition bugs 



## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

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
- scratch_process.c (src/render/gltf/) 〜 dead code: not in any CMakeLists, no includer. Amended 2026-07-25 (round 6): it is also the one caller left on the pre-sentinel contract 〜 :35 assigns geometry_pool_upload's answer straight into primitive->meshIndex unchecked, which now means an ANO_MESH_NONE refusal is written as a mesh index instead of aliasing slot 0. Harmless while nothing compiles it, and it is the reason the sentinel sweep found no third call site; delete the file or port it, do not leave it as a template.
- memalign_win64.c:1 〜 guards _WIN64 while CMake selects on WIN32; a 32-bit Windows build gets an empty TU and a link failure (marginal, engine is 64-bit-only).
- threads_macos.h:15 〜 PTHREAD_BARRIER_SERIAL_THREAD lives only in the private header ("Never include outside src/threads"); on Linux pthread.h supplies it, on Darwin nothing public does 〜 portable caller code testing ano_thread_barrier_wait's documented serial return compiles on linux/win64 and fails to compile on macOS.
- threads↔log_crash seam 〜 the spawn trampoline's cleanup handler (threads.c:39) disarms the altstack before pthread TSD destructors run, so a stack-overflow crash inside any TSD destructor (mimalloc thread teardown included) faults with no alternate stack and the blackbox Stage-1 handler recurses instead of reporting.
- music_theory.c:48 〜 ano_mode_intervals lazy-bake static sets the baked flag after the table writes with no fence 〜 same cross-thread shape as the voicing race, benign on x86 today.
- music_host.c cadence_policy 〜 ano_music_set_override("cadence_policy", -1) stores ANO_CADENCE_NONE which ano_next_chord indexes into CADENCE_TARGET[]/PRE_CADENCE_FUNCTION[] 〜 OOB read for an enum-defined value.
- music_host.c override_apply 〜 has-flag written before value with plain stores; safe only while commands apply on the engine-owning thread 〜 any future cross-thread ano_music_set_* re-opens it.
- ano_GltfParser.c:71 〜 parseGltf ignores geometry_pool_upload_chain's failure return. The pool-exhaustion half is discharged as of 2026-07-25 (round 6) with the parser untouched: the chain writes ANO_MESH_NONE into *out_lodBase on every refusal arm and that word IS ANO_RENDER_NO_MESH, so `geometryPoolIndex = lodBase` now lands on the documented absent-mesh lane the cull pass skips instead of silently drawing the fallback cube. What remains is the skipped-primitive half 〜 the `continue` at :237 leaves geometryPoolIndex at the carved block's zero, i.e. FALLBACK_MESH_INDEX 〜 and the unread return itself, which still cannot tell a partial chain from a full one.
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
- time suspend semantics 〜 struck 2026-07-25: the campaign adjudicated the contract (the engine's monotonic clock excludes suspended time on every platform), the win64 re-anchor measures its resume gap against QueryUnbiasedInterruptTimePrecise rather than QPC to implement it, and anoptic_time.h:20 now says so. The apple-silicon mach_absolute_time question folds into that sentence.

Logged by the 2026-07-25 remediation and left as leads rather than entries, being warning hygiene, documentation lag, or arguments recorded so they are not rediscovered:

- window.c:230 〜 `if (monitorIndex == -1)` on a uint32_t: -Wsign-compare, behaviourally correct (-1 converts to UINT32_MAX and anotest_windowcreateguard exercises exactly that sentinel), fixed by spelling it `== UINT32_MAX`.
- window.c:59 〜 `static uint32_t count` in framebufferResizeCallback is consumed only by ano_debug_log, so -Wunused-but-set-variable fires in Release and not in Debug.
- instanceInit.h:28-29 〜 enumerateMonitors gained two refusal arms, a mint-not-free precondition, a post-glfwInit ordering precondition and a borrowed-storage postcondition, all documented at the definition (window.c:26-30) and none at the declaration callers read.
- pipeline.c:330 〜 the count-first dissolution landed inside `if (implementations != NULL)`, so a prototype holding implementationCount > 0 beside a NULL pointer would keep its count through teardown; unreachable under the commit-last contract the twelve builders now hold, so the gate was deliberately not widened. One-line remedy if it is ever revisited: hoist the `implementationCount = 0` above the NULL test.
- vulkanMaster.c latchRenderUnrecoverable 〜 calls glfwSetWindowShouldClose(window, GLFW_TRUE) unguarded, and real GLFW asserts window != NULL. Unreachable today (every path into the latch is below drawFrame's acquire), but `window` became a pointer that can legitimately be NULL when teardown started clearing it, so "the latch only ever runs with a live window" is now load-bearing rather than incidental.
- ano_GltfParser.c:409 〜 the bindless-full latch is parse-local, so an asset loaded after the array is already full still decodes, stages, uploads and adopts exactly one texture before the refusal re-establishes it. One wasted decode per parseGltf call, nothing orphans; the deliberate price of not exporting a fullness predicate through texture.h.
- anotest_protopairguard 〜 pins commit-last publication and count-first dissolution against flat.c, compute.c, additive.c and transmission.c, but the same count-first fix now also lives in pipeline.c:330-344, which that target does not compile 〜 the loop owning teardown for additive, transmission, tonemap, shadow and all eight compute prototypes is fixed but unpinned.



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

A fifth pass, the remediation campaign of 2026-07-25, took the 27 that were left and ran four fix rounds against them. Round 1 fixed 19 of the 27 outright 〜 the 15 renderer entries plus the four platform singletons (`audio_win64.c:589`, `filesystem_win64.c:137`, `threads_macos.c:79`, `time_win64.c:148`) 〜 and deliberately fenced the seven resource-management entries, which stay open above with their guards red on purpose and their custody spans byte-identical after four rounds of work in the same files. The one `wontfix` was not touched. Rounds 2 to 4 then worked the strays the fixes surfaced, 44 items in all, of which 41 are source defects entering this tally already fixed and three were test-side (two new guards authored to pin invariants nothing read, one guard re-authored against an adjudicated ruling). Ten findings the campaign logged and did not fix are new open entries above. Seven contract questions were adjudicated rather than settled inside a fix; the record is in `docs/BUGS_DONE.md`, "Adjudicated contracts (2026-07-25)", and the arithmetic behind the table below is in `docs/BUG-HUNT.md`, "The 2026-07-25 remediation".

| Section | Fixed | Tallied |
|---|---:|---:|
| Audio | 12 | 12 |
| Filesystem | 2 | 2 |
| Log | 2 | 2 |
| Math | 1 | 1 |
| Memory | 1 | 1 |
| Mesh | 2 | 2 |
| Music | 9 | 9 |
| Render / Vulkan | 71 | 82 |
| Strings | 3 | 3 |
| Synth | 3 | 3 |
| Text | 3 | 3 |
| Threads | 1 | 1 |
| Time | 4 | 5 |
| UI | 5 | 5 |
| Engine | 1 | 1 |
| **Total** | **120** | **132** |

Rounds 5 and 6, later on 2026-07-25, reversed round 4's disposition on the six entries it had ledgered rather than fixed. The user's standing order is that an in-scope defect found during the campaign gets fixed, and those six were in scope: `audio_mixer.c:616`, `audio_win64.c:389`, `scene_buffers.c:43`, `texture.c:381`, `apply.c:66` and `apply.c:57`. Round 5 fixed all six plus the `backend.h` contract the light work had outrun; round 6 took the four strays round 5 left and fixed every one inside the round, so the stray flow that ran 21, 16, 11, 17 finally ran dry. Eleven fixes in all. Six were already tallied above as open entries, so they move Fixed only. Four enter the census already fixed and add a line each 〜 the `ANO_MESH_NONE` refusal sentinel with its fallback-mesh unwind, the `backend.h` static-region contract, the `anoptic_render.h` `castsShadow` static-lane documentation, and `refresh_static_shadow`'s declaration home. The eleventh is test-side (a stale comment in `anotest_staticrowdecodeguard` claiming an invariant stronger than any of its own CHECKs) and carries no tally line, the same rule that made 44 stray fixes move the board by 41. So Fixed rises 110 + 6 + 4 = 120, Tallied 128 + 4 = 132, and open falls 18 − 6 = 12. Per module: Audio 10|12 to 12|12, now fully remediated; Render / Vulkan 63|78 to 71|82. Every other module is unchanged.

What is left is no longer the systemic mass the census named, because the swoops were taken: Vulkan Result discipline and the ownership/rebind work retired the renderer block wholesale rather than file by file. Twelve entries stand open. Seven are the resource-management fence 〜 one custody question wearing seven anchors, and the campaign's own evidence for why it was fenced is that six independent rounds each stopped exactly at its boundary. Four are what the fixing turned up and could not close inside its own surface: the SPIR-V orphan family, the shadow-resources unwind and the texture post-acquisition arms (the same partial-construction shape as the fence), and the static frustum region's monotonic non-reclamation. The twelfth is the declined repair. No open entry is blocked on a contract choice any more.

Guard-test movement: 22 new guards authored and registered (3 in round 1, 14 across rounds 2 to 4, 5 across rounds 5 and 6), and all 15 renderer census guards flipped red to green with no assertion touched 〜 the fixes met the tests the census had already written. Two guard files needed link-stub retypes only, where a fix changed a stubbed signature (`recordCommandBuffer` becoming `[[nodiscard]] bool` broke `anotest_initdepthguard`'s void stub, and left `anotest_recordbeginguard`'s own call discarding a result); both are stubs and discards, no CHECK. Rounds 5 and 6 add `anotest_audiopaceguard` to the headless suite and `anotest_scenebindguard`, `anotest_texpixeldomainguard`, `anotest_staticcastdropguard` and `anotest_fallbackmeshguard` to the render one, each observed red against its own pre-fix TU and green after by direct link rather than by a suite run, so the totals move from 87 of 94 to 91 of 98 with the same seven fenced reds, and from 57 of 57 to 58 of 58, on the next build. Entries fixed without a runnable gate here are the four platform singletons (Win32-only, or Darwin-only for the barrier, which is TSan-verified on-host instead), `audio_win64.c:389`, and the ones the census itself marks `test: pending`.

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
