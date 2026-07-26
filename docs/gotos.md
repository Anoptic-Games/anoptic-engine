# Labelled unwind

House rules for `goto` in Anoptic. Part 1 is the convention, and it is short on purpose: read it before every error-path review. Part 2 is the map of what has not been converted. Part 2 authorizes nothing.

The idiom landed in the Vulkan pipeline builders on 2026-07-25 〜 `flat.c`, `shadow_pipe.c`, `compute.c`, `tonemap.c`, `additive.c`, `transmission.c`, `text_raster.c` 〜 and has been the audio module's shape since it was written 〜 `ano_audio.c`, `audio_linux.c`, `audio_macos.c`, `audio_win64.c`. Those files are the reference. Everything below is distilled from them and from what the Linux kernel, curl, SQLite, OpenSSL and FreeBSD style(9) each arrived at independently. Independent convergence is worth more than any one of them arguing for it.

# Part 1 〜 the convention

## When goto is right

Three cases that are really one case.

- Multi-acquisition unwind. A function acquires two or more resources in sequence and any step can refuse. Written without a label the discharge sites grow with the product of arms and resources; with one they do not grow at all.
- Single-exit cleanup. Something must happen on every exit 〜 a lock dropped, a count committed, an out-param totalled 〜 and C will not do it for you.
- Emergency escape hatch. A path abandons the rest of its body and rejoins at a common tail. `drawFrame` (`vulkanMaster.c:289`, `:315`) takes `goto discharge` when recording or submit refuses: no frame is submitted or presented, but the acquired swapchain image is still discharged at `:360`.

The shared property: the destination is one point below, every arm agrees on it, and the alternative is a repeated block or a flag threaded through nested ifs. Both alternatives drift out of agreement with each other. A label cannot.

## When goto is banned

- Anything a structured construct expresses. `break`, `continue`, `return`, an early guard clause, an inverted condition. A jump that lands one statement further down is noise.
- Backward jumps. Every label in this tree is below every jump reaching it. A backward `goto` is a hand-written loop whose termination condition no reviewer can see; write the loop.
- Jumping into the scope of an identifier of variably-modified type. C forbids it and the compiler will say so. Do not restructure to smuggle it past 〜 hoist the VLA out or size it statically.
- Out of a function. No such thing. What you want is a failure return, and the label is how every arm reaches one uniformly.
- Where a scope-bound cleanup already runs. `LOCALHEAPATTR` releases its heap on every exit including the early ones; adding a label to free what the attribute frees is a double free waiting for someone to read only one of the two.

## Naming and placement

- `fail` for an unwind. Suffix when there is more than one: `fail_blur`, `fail_published`, `fail_pcm`. The suffix names the phase or the thing discharged, never the arm that jumps to it 〜 arms move, phases do not.
- `discharge`, `done`, `out` for escape hatches that are not failures. `drawFrame` says `discharge` because the frame is abandoned, not failed.
- Labels live at the end of the function, after the success `return`, at column 0. Never mid-function, never inside a block, never inside a loop.
- The success path returns above the first `fail:` label and never falls into one. The `return true;` above `fail:` is load-bearing: delete it and every success silently unwinds. A merged `done:`/`out:` is the deliberate exception.
- A comment above the label states what makes the discharge safe 〜 usually the null-safety it relies on 〜 and what it deliberately does not own. `flat.c:307` is the template.

## The two shapes

Cascading, the kernel shape:

```c
	if (!a()) return false;
	if (!b()) goto fail_a;
	if (!c()) goto fail_b;
	return true;
fail_b:	un_b();
fail_a:	un_a();
	return false;
```

Each arm jumps to the label matching exactly what it holds. Labels fall through in reverse acquisition order. Nothing is discharged twice, nothing inert is discharged at all, and no inert initialization is needed, because control never reaches a discharge for a resource that was never acquired. It costs one label per acquisition and it is correct exactly as long as the arms point at the right labels 〜 insert an acquisition in the middle and every arm below it must be re-pointed by hand.

Single discharge:

```c
	struct Buffer code = {0};
	VkShaderModule mod = VK_NULL_HANDLE;
	...
	if (!loadFile(path, &code)) { code.data = NULL; goto fail; }
	mod = createShaderModule(dev, &code);
	if (!ano_pipeline_stage(..., mod, ...)) goto fail;
	...
	return true;
fail:
	ano_aligned_free(code.data);
	vkDestroyShaderModule(dev, mod, NULL);
	return false;
```

Everything inert at declaration, one label discharging everything unconditionally, every arm jumping to the same place.

House choice: single discharge wherever every deallocator named at the label is null-safe. Cascade where one is not.

That rule is not taste. `free(NULL)`, `mi_free(NULL)`, `ano_aligned_free(NULL)` and every `vkDestroy*`/`vkFree*` given `VK_NULL_HANDLE` are defined no-ops, so discharging an unacquired resource is a no-op the compiler nearly writes itself. `dlclose(NULL)`, `fclose(NULL)`, `pw_thread_loop_destroy(NULL)`, `AudioComponentInstanceDispose(NULL)` and most third-party `*_destroy` entry points are not. Where the deallocator has no null contract the cascade is not an alternative, it is the only shape correct without wrapping every discharge in an `if` 〜 and wrapping every discharge in an `if` is the cascade with worse names. `pw_start` (`audio_linux.c:371`, five labels over dlopen, pw_init, loop, stream, thread-start) is right for that reason and stays as it is.

Where both shapes are legal, the honest tradeoff: the house shape runs a handful of no-op frees on the error path that the cascade would skip, and buys arm-simplicity 〜 every arm is the same three characters 〜 plus immunity to arm-reordering and arm-insertion mistakes, which are the failure mode the cascade actually has in the field. Error paths are cold: a boot-time driver refusal, host OOM, a missing `.spv`. The trade is right. Take it.

## Two variants of the single label

Guarded, when null-safety is unavailable but the phases are not separable: one label with explicit `if`s, `audio_win64.c:456` (`if (started) ... ; if (mmcss && st->avRevert) ...`). Acceptable, and preferable to a cascade of two.

Arena, and prefer it whenever it is available: where every acquisition came from one scoped heap, the label is one destroy. `ano_audio_init` (`ano_audio.c:93`) has nine arms and the whole of `fail_heap:` is `mi_heap_destroy(heap); return false;` (`:206`). This is the CLAUDE.md allocation rule paying for itself 〜 the arena is the unwind and the label only carries you to it. What the arena does not own is discharged at its own arm before the jump (the mixer-thread arm's `mx->device->stop(mx)`) or takes its own label.

## One label per phase

One label per disjoint acquisition phase, where disjoint means the earlier phase is fully discharged before the later begins.

`ano_vk_init_shadow` (`shadow_pipe.c:23`) is the reference. The depth phase's four blobs and five modules are discharged at `:176-186`; the blur phase then declares its own pair and carries `blur_done:` (`:266`). One label spanning both would have to discharge depth-phase locals that are already gone 〜 a double free, or a re-inert at every tail, which is bookkeeping bought back to pay for a label you did not split.

`ano_vk_init_compute` (`compute.c:24`) is the counterexample that fixes the rule: nine stages, one label, and no phase boundary anywhere. Nothing is discharged mid-function 〜 the nine blob/module pairs sit in `sh[SH_COUNT]` (`:30`) and are held to exit 〜 so there is no earlier phase for a later label to double-free. Split labels when the phases share no shape, as a depth pipeline and a blur pipeline do not. When the phases are repetitions of one shape, as nine compute stages are, do not make them phases at all: hold the resources and keep one label.

## Inert at declaration

Hoist every resource the label touches to the top of the region it unwinds and give it the value its deallocator ignores: `NULL`, `VK_NULL_HANDLE`, `{0}`, `-1` for a descriptor.

Two reasons. The second is the one that bites.

1. The label discharges unconditionally, so an unacquired resource must already be inert.
2. Jumped-over initializers. A `goto` past the declaration of an automatic object does not run its initializer; the object's lifetime has begun and its value is indeterminate. Reading it at the label is undefined and no diagnostic is required. Declaring at the top, with an initializer every arm passes through, is the only way to make the label's read defined. Hoisting is therefore mandatory, not stylistic.

Corollary: a callee that is not total on failure breaks the rule from outside. `loadFile` once left `buffer->data` dangling on a short read, so every builder in tree re-inerts a refused buffer at the arm 〜 `{ code.data = NULL; goto fail; }`. That is defensive now the callee is total (`pipeline.c:63`) and it is kept, and it is deleted with `loadFile`. New code should not need it: a fallible callee writes total out-params or it is the bug.

## Discharge idempotence

Every deallocator named at a label must be null-safe, or the pointer must be re-inerted after its discharge. There is no third option.

- Null-safe by contract: `free`, `mi_free`, `ano_aligned_free` (mi_free by delegation), every `vkDestroy*`/`vkFree*` on `VK_NULL_HANDLE`.
- Not null-safe: `dlclose`, `fclose`, third-party destroy entry points, anything taking a handle by value with no documented null case. Read the contract. Do not assume it.
- Re-inert after discharge when a path below still names what was released 〜 or restructure so nothing is released twice. `ano_vk_init_compute` took the second option: it stopped discharging each stage as it completed, and one `out:` (`compute.c:486`) now discharges all nine pairs on a path that runs at most once. Where the mid-function discharge has to stay, split the label instead 〜 `shadow_pipe.c:187` is the comment standing guard over exactly that hazard.

The discharge set is what has been acquired and not yet published. A resource that escapes through an out-param on success is not the label's 〜 `createTextureImage` hands its staging buffer out at `texture.c:526`, so a label there owns that buffer only above that line. Getting this wrong is not a leak, it is a use-after-free in the caller.

The success epilogue and the label body are frequently identical (`tonemap.c:141-144` against `:154-157`; both `text_raster.c` builders). That duplication is accepted under roughly six lines: a shared `out:` with a status variable saves them and costs every reader a status branch to trace, on the path where tracing is hardest.

The trade flips when what is duplicated is not an epilogue but a per-stage tail whose only job is keeping a null-after-discharge invariant true. `ano_vk_init_compute` deleted its tails for a merged `out:` behind a hoisted `bool ok` (`compute.c:29`, `:486`) discharging all nine pairs in one loop; `flat.c:309` is the same shape at one stage. One discharge site that runs at most once has no invariant to maintain, and deleting an invariant is worth more than the branch it costs. The price it does pay is that the resources live until exit 〜 here nine SPIR-V blobs and modules, under a megabyte, once 〜 affordable on a one-shot init path and not in the frame loop.

## What a label must never do

- Allocate. A cleanup path that can itself fail has no cleanup path.
- Log at more than one site. One diagnostic per failure. Arms that need distinct messages log at the arm; the label stays silent, which is what every pipeline builder does.
- Return anything but the failure value. No partial success, no mostly-initialized.
- Discharge what the caller owns, or what a teardown function will discharge later. `flat_init_with_cull` frees blobs and modules and deliberately leaves the pipelines, layout and cache to `ano_pipeline_flat_cleanup`, which the prototype's published state reaches. Freeing them at the label double-frees at teardown.
- Run on the success path, if it is named `fail`. A merged `done:`/`out:` runs on both by design, and its name is the notice.

## Relationship to the rest

- Bugs go in `docs/BUGS.md`, not in a comment beside the label. A known-wrong unwind is a census entry with a named test, never a `// TODO: leaks here`.
- Contract forks get adjudicated, not hidden in error paths. Where what an arm should discharge depends on an unsettled ownership question, the question is lifted out and ruled on (`docs/BUGS_DONE.md`, "Adjudicated contracts"). An unwind written over an open fork settles it silently, which is exactly what the fence in Part 2 exists to prevent.
- The five-tier hierarchy (`.claude/skills/invariants/SKILL.md`) ranks the repair. A labelled unwind is a tier-1 answer: it makes the leak unconstructible in that function rather than guarding each arm.

# Part 2 〜 the map

Survey of where the idiom would land next. Nothing here is authorization to convert. No code changes now: this is the map, not the march.

Two things it is not. A labelled unwind is not a fix for a custody question 〜 it changes where a discharge is written, never who owns the resource, so an entry blocked on ownership stays blocked after conversion, and converting it early settles the ownership question silently. And it is not warranted below about three arms: `render_slots_alloc_range` was closed with an `alloc_range_rollback` helper at each arm and is right as it is; `initSwapChain` (`swapchain.c:149`) took a null-safe `freeSupportDetails` helper in the trivial-fix wave, and that helper was deleted on 2026-07-26 when the query was restructured to return values only 〜 no owned pointer, nothing to discharge, at either arm. Roughly: one or two arms take a helper, three or more take a label.

## Blocked on a ruling 〜 do not convert

`shadow_resources.c:22` 〜 `createShadowResources`. The flagship, and the largest single unwind in the tree. Nineteen `return false` arms after the first acquisition (docs/BUGS.md says sixteen; the file has gained arms since that entry was written) and not one discharges anything. The unwind would own: per frame a `frustumBuffer`+`frustumAlloc` and a `sampleVPBuffer`+`sampleVPAlloc`, times `MAX_FRAMES_IN_FLIGHT`; the atlas and temp images with their allocations, array views and `ANO_SHADOW_ATLAS_LAYERS` layer views each; the transient seed command buffer; `shadowDepthImage` with its allocation and `ANO_SHADOW_FRUSTUM_COUNT` slice views; two `SlotUpload`s; the `calloc`'d `shadowCfgMirror`. Blocker: refusal semantics, per the docs/BUGS.md entry 〜 what the transient CB and the two SlotUploads do on refusal is undecided, and the label cannot be written without deciding it. Adjudicate first. Secondary obligation once unblocked: the label must re-inert every handle it destroys, because `cleanupVulkan` walks the same published fields.

`texture.c:450` 〜 `createTextureImage` and `texture.c:381` 〜 `createTextureImageFromPixels`. Converted 2026-07-26 〜 both carry one `fail:` label; the ruling that unblocked them is in `docs/BUGS_DONE.md` under the retired custody chain. The hazard this entry recorded 〜 the staging buffer conditionally escaping through `*outStagingBuffer` above the last arm, so a naive label double-frees 〜 was dissolved rather than guarded: the out-param collapsed to a `bool keepStaging`, and the buffer is published into the returned package only in the success epilogue, so it never escapes above a failure arm and the label owns it unconditionally. Two shape notes. The decoded pixels are freed and the pointer re-inerted the moment they are staged rather than held to the label, because holding multi-MB through mip generation is exactly wrong on the memory-pressure path these arms fire on 〜 Discharge idempotence, applied. Every acquisition was hoisted above the first `vkCmd*`, so no arm destroys an image a borrowed command buffer already references; a label that discharges into a recorded-but-unsubmitted CB is a use-after-free bookkeeping cannot fix.

## Waiting on the resource manager

`loadFile` retirement (`pipeline.c:63`, ~20 call sites). Every builder's `{ code.data = NULL; goto fail; }` re-inert exists because `loadFile` was once non-total; the totality patch and every caller-side re-inert are a temporary workaround that is deleted with `loadFile` when shader bytes arrive through `ano_res_load` (docs/BUG-HUNT.md, the trivial-fix wave note; docs/resourcemgr/resource-manager-plan.md). Do not build new unwinds on `loadFile`, and do not clean up the re-inerts before it goes.

Any cross-file discharge. Where the acquisition and the only correct discharge sit in different translation units 〜 the parser/registrar chain above 〜 a label in either file is half an answer. These wait.

## Open candidates, strongest first

Ranked by what a failure strands, not by arm count. The first two strand live resources today; the rest are repetition, asymmetry risk, or tightening.

1. `text_bake.c:507` 〜 `ano_text_font_bake_ranges`. Nine arms after the first acquisition, and the leak is real: `glyphs` and `map` come from the caller's heap, no arm frees them, and `*out` was zeroed at `:523` so the caller holds no handle to them either. The last two arms (`:636`, `:643`) additionally strand the `pairs` block `bake_kerns` already published into `out->kerns`. The label discharges `glyphs`, `map`, `out->kerns` and re-totals `*out`; the `LOCALHEAPATTR` scratch needs nothing. Settle it together with its callee `text_bake.c:440` 〜 `bake_kerns` (four arms), which shares the same caller-heap ownership ambiguity: who frees a block the callee allocated on the caller's heap and published into the caller's struct is the question both are really asking.
   Converted 2026-07-26 〜 single `fail:` label, kern publish moved commit-last (`bake_kerns` made total on out-params, publishing only in the caller's one epilogue); the record is in docs/BUGS_DONE.md.
2. `main.c:350` 〜 `music_world_start`. Four arms, none discharging anything, and the damage lasts the process: the arm at `:396` fires after `ano_audio_init` succeeded, so a live mixer thread and an open device backend survive a start that reported failure. The caller (`:1044`) logs and continues, and `music_world_stop` does not run until `:1069`. The label discharges `g_synth`, `g_music` and 〜 on that last arm only 〜 `ano_audio_shutdown`. Note the shape: this is a cascade, because the discharge set genuinely differs per arm and `ano_audio_shutdown` must not run on an arm that never brought audio up.
   Converted 2026-07-26 〜 single `fail:` label through `music_world_stop(false)`; the cascade premise above was false at source 〜 `ano_audio_shutdown` is handle-guarded (`ano_audio.c:270-273`), a no-op when audio never came up 〜 so single discharge is the correct shape. The record is in docs/BUGS_DONE.md.
3. `geometry.c:107` 〜 `geometry_pool_emit_level`. The cleanest conversion in the tree and the one to do first if any is done. Six arms, each a verbatim paste of the same free block growing by one destroy: `meshlets`, `meshlet_vertices`, `meshlet_triangles`, `bounds`, then `stagingBuffer`, then `transientPool`. The success tail at `:348` is a seventh copy. Nothing escapes and the pool reservations commit after the last failure arm, so the label has nothing to roll back.
4. `vulkanMaster.c:365` 〜 `initVulkan`. Thirty-odd arms, and the reason it belongs here is not the count: every arm but one spells the same `unInitVulkan(); return false;`, and the one that does not is `:674` (`ano_render_load_scene_assets`), which strands the whole initialized renderer. That is the standing docs/BUGS.md lead `vulkanMaster.c:593`, and it is exactly the defect one label makes unconstructible. Latent today only because the callee always answers true. The label body already exists as `unInitVulkan`.
5. `commands.c:343` 〜 `createSyncObjects`. Four arms, but the interesting failure is inside them: the short-circuit `||` chains at `:354-356`, `:369-370`, `:377-378` strand the earlier semaphores of their own statement, which no per-arm cleanup reaches without splitting the chain. Discharges per-frame `imageAvailable`/`renderFinished`/`frameFence`, four timelines, timestamp query pools, per-frame `pickReadback` buffer and allocation.
6. `descriptors.c:112` 〜 `createDescriptorSets`. Ten stranding arms over eleven batches of sets published into `rendererState` as they are allocated. Constraint that shapes the label: the pool is created without `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` (`descriptors.c:56`), so the label cannot free sets 〜 it re-zeroes the published fields so teardown never sees a set handle from a pool about to die. An unwind that frees here is wrong; an unwind that unpublishes is right.
7. `scene_buffers.c:284` 〜 `createCullingBuffers`. Six bare arms over a `SlotUpload` plus six buffer/allocation/mapped-pointer triples times `MAX_FRAMES_IN_FLIGHT`. Nothing in the function discharges anything; it survives only because the handles land in `state->culling` and the global teardown is handle-guarded.
8. `attachments.c:207` 〜 `createColorResourcesChecked` (twelve arms), `attachments.c:60` 〜 `createDepthResources` (six), `attachments.c:134` 〜 `createHiZResources` (four). All three publish per-frame-per-view images, allocations and views, and all three delegate discharge to `cleanupSwapChain` by documented contract, so these are tightenings, not leaks. `createHiZResources` has one wrinkle that earns a label on its own: `hizMipCount` is published at `:150` before the views exist, so a refused mip view leaves teardown walking dead handles unless the label re-zeroes the count 〜 the commit-last shape the pipeline prototypes already hold.
9. `pipeline.c:127` 〜 `ano_vk_init_pipelines`. Five arms, each abandoning the prototypes the arms above built. `ano_vk_cleanup_pipelines` (`:161`) is the label body, already written.
10. `audio_wav.c:122` 〜 `ano_audio_wav_load`. Eight arms, no leak today, pure arm-simplicity: `fclose(f)` written twice, `mi_free(raw)` five times, across three live-set shapes. It is the best teaching case in tree for two rules at once 〜 `fclose(NULL)` is undefined, so the file phase cannot join a null-safe label, and the phases are genuinely disjoint (`:137` closes the file before the first `raw` arm). Two labels, `shadow_pipe.c`'s split exactly.
11. `layouts.c:161` 〜 `ano_vk_init_cull_layout`. Three arms over four descriptor set layouts; two of them (`:288`, `:316`) do not even log.
12. `slot_upload.c:78` 〜 `slot_upload_create`. Six arms over the device buffer, per-frame staging buffers and per-frame `malloc`'d region arrays. Its header makes "the caller owns the partial `*b`" the contract, so a label here is a contract change 〜 it makes the function total 〜 and must be proposed as one, not slipped in as cleanup. Sibling `slot_upload.c:125` `slot_upload_grow_staging` has the file's purest copy-paste shape at three arms.
13. `scene_buffers.c:401` 〜 `createFallbackResources`. Three arms over a granted geometry-pool slot, the fallback image trio and a bindless texture slot. Half-blocked: `bindless_register_texture` only ever increments, so the bindless grant is not reversible 〜 a truthful label discharges the mesh slot and the image and says out loud that the texture slot is spent. Settle that before writing it.
14. `ano_GltfParser.c:131` 〜 `parseGltf`, prologue only. Four arms repeating `cgltf_free(data)`, one adding `free(assetBase)`; the scratch heap is `LOCALHEAPATTR` and needs nothing. The function's own comment at `:161` claims one failure arm and there are four. Stated separately because the rest of the function is blocked: the texture loop's `continue` arms are the fenced `ano_GltfParser.c:277` custody entry, and converting the prologue must never be reported as having touched it.
15. `audio_mixer.c:24` 〜 `ano_audio_graph_init` (three arms, half-built bus graph) and `audio_fx.c:36` 〜 `ano_audio_fx_init` (three arms in the REVERB case over `pre`, `ap[0..1]`, `line[0..i-1]`). Both leave partial state on the caller's heap and both are discharged wholesale by the caller's `mi_heap_destroy`, so there is no live leak 〜 arena unwind already covers them. Lowest priority; convert only if the heap ownership is being revisited anyway.

## Checked and not candidates

Already converted, leave alone: all three audio platform backends 〜 `alsa_start` (`audio_linux.c:93`), `pw_start` (`:371`), `coreaudio_start` (`audio_macos.c:56`), `wasapi_start`/`wasapi_main`/`dsound_start`/`dsound_main` (`audio_win64.c:472`/`:313`/`:808`/`:691`) 〜 all cascading labels, all correct for their non-null-safe deallocators.

Already discharged by scope, and converting them would be a regression: `ano_meshoptimizer.c` `ano_optimize_vertex_cache` (`:119`) and `ano_simplify_ex` (`:662`), and `audio_mixer.c:685` `ano_audio_render_offline`, all `LOCALHEAPATTR`. Their bare returns are correct by construction. Recorded here so a later sweep does not re-flag them on arm count.

Below the bar or holding nothing: `getRequiredExtensions` (`instance.c:171`, no arms at all 〜 its defects are an unchecked `calloc` and the per-string `strdup` leak, neither of which a label addresses), `createImageViews` (`swapchain.c:455`, already unwinds its own prefix), `recordCommandBuffer` (`frame/record.c:20`, three arms but no resource), `ano_vk_ui_init` (`ui_raster.c:186`, degrades rather than refusing), `ano_text_font_load` (`text.c:124`, two arms, the cheapest possible conversion and still under the bar), plus `createInstance`, `initWindow`, `ano_vk_init_geometry_pool`, `createDescriptorPool`, `createBindlessTextureArray`, `ano_synth_create`, `ano_synth_score_begin`, `ano_synth_live_begin`, `ano_music_create`, `ano_render_ui_set`.

## Hazards and adjacent findings

Variable-length arrays sit in three functions in `src/vulkan_backend/instance/`: `device.c:43`, `commands.c:393`, `instance.c:225`. A label added below one of those with any arm above it is the jump-into-VM-scope the standard forbids. Hoist or fix the bound first.

Every fail label in tree rests on `ano_aligned_free(NULL)` being a no-op, and that holds only by delegation to `mi_free`. `anoptic_memory.h:51` does not say so. It should.

Two defects this sweep surfaced were not goto matters; both disposed 2026-07-26, the analyses recorded in docs/BUGS.md: the pickPhysicalDevice claim was rejected on verification 〜 the `:481` arm frees the enumeration array itself, and `ctx->availableDevices` is ctx-owned custody reclaimed by `cleanupVulkan` on every failure arm, since the `vulkanGarbage.ctx` registration at `vulkanMaster.c:416` precedes the call. The createTextureImage claim was already tallied 〜 the open `texture.c:486` entry subsumes those arms.
