# Labelled unwind

House rules for `goto` in Anoptic. Part 1 is the convention. Part 2 maps what has not been converted. Part 2 authorizes nothing.

Reference: Vulkan pipeline builders as of 2026-07-25 (`flat.c`, `shadow_pipe.c`, `compute.c`, `tonemap.c`, `additive.c`, `transmission.c`, `text_raster.c`) and the audio module (`ano_audio.c`, `audio_linux.c`, `audio_macos.c`, `audio_win64.c`). Distilled from those and from Linux kernel, curl, SQLite, OpenSSL, FreeBSD style(9).


# Part 1 - the convention

## When goto is right

Three cases.

- Multi-acquisition unwind. Two or more resources in sequence; any step can refuse.
- Single-exit cleanup. Lock drop, count commit, out-param total on every exit.
- Emergency escape hatch. Abandon the body, rejoin at a common tail. `drawFrame` (`vulkanMaster.c:289`, `:315`) takes `goto discharge` when recording or submit refuses; acquired swapchain image still discharged at `:360`.

> CONVENTION: One destination below. Every arm agrees.

## When goto is banned

- Anything `break`, `continue`, `return`, early guard, or inverted condition already expresses.
- Backward jumps. Write a loop.
- Jumping into the scope of a variably-modified type. C forbids it. Hoist the VLA or size it statically.
- Out of a function. Failure return; the label is how every arm reaches one.
- Where scope-bound cleanup already runs. `LOCALHEAPATTR` releases on every exit; a label freeing the same heap is a double free.

## Naming and placement

- `fail` for an unwind. Suffix when more than one: `fail_blur`, `fail_published`, `fail_pcm`. Suffix names the phase or thing discharged, never the arm.
- `discharge`, `done`, `out` for escape hatches that are not failures. `drawFrame` says `discharge` because the frame is abandoned, not failed.
- Labels at end of function, after success `return`, column 0. Never mid-function, never inside a block or loop.
- Success path returns above the first `fail:` and never falls into one. Merged `done:`/`out:` is the deliberate exception.
- Comment above the label states what makes discharge safe (usually null-safety) and what it does not own. `flat.c:307` is the template.

> CONVENTION: The `return true;` above `fail:` is load-bearing.

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

Each arm jumps to the label matching what it holds. Labels fall through in reverse acquisition order. One label per acquisition.

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

Everything inert at declaration; one label discharges everything unconditionally; every arm jumps there.

> CONVENTION: Single discharge wherever every deallocator at the label is null-safe. Cascade where one is not.

`free(NULL)`, `mi_free(NULL)`, `ano_aligned_free(NULL)` and every `vkDestroy*`/`vkFree*` on `VK_NULL_HANDLE` are no-ops. `dlclose(NULL)`, `fclose(NULL)`, `pw_thread_loop_destroy(NULL)`, `AudioComponentInstanceDispose(NULL)` and most third-party `*_destroy` are not. No null contract means cascade. `pw_start` (`audio_linux.c:371`, five labels over dlopen, pw_init, loop, stream, thread-start) is right and stays.

Where both shapes are legal, take single discharge. Error paths are cold.

## Two variants of the single label

Guarded, when null-safety unavailable but phases not separable: one label with explicit `if`s, `audio_win64.c:456` (`if (started) ... ; if (mmcss && st->avRevert) ...`). Preferable to a cascade of two.

Arena, prefer when available: every acquisition from one scoped heap; label is one destroy. `ano_audio_init` (`ano_audio.c:93`) has nine arms; `fail_heap:` is `mi_heap_destroy(heap); return false;` (`:206`). What the arena does not own is discharged at its arm before the jump or takes its own label.

## One label per phase

> CONVENTION: One label per disjoint acquisition phase. Earlier phase fully discharged before the later begins.

`ano_vk_init_shadow` (`shadow_pipe.c:23`) is the reference. Depth phase's four blobs and five modules discharged at `:176-186`; blur phase declares its own pair and carries `blur_done:` (`:266`).

`ano_vk_init_compute` (`compute.c:24`) is the counterexample: nine stages, one label, no phase boundary. Nine blob/module pairs sit in `sh[SH_COUNT]` (`:30`) held to exit. Split when phases share no shape. When phases are repetitions of one shape, hold the resources and keep one label.

## Inert at declaration

Hoist every resource the label touches to the top of the region and give it the value its deallocator ignores: `NULL`, `VK_NULL_HANDLE`, `{0}`, `-1` for a descriptor.

> CONVENTION: Label discharges unconditionally, so an unacquired resource must already be inert.
> CONVENTION: A `goto` past a declaration does not run its initializer. Top-of-region declaration with an initializer every arm passes through is mandatory.

A non-total-on-failure callee breaks the rule from outside. `loadFile` once left `buffer->data` dangling on a short read, so every builder re-inerts: `{ code.data = NULL; goto fail; }`. Defensive now the callee is total (`pipeline.c:63`); kept; deleted with `loadFile`. New code: fallible callee writes total out-params or it is the bug.

## Discharge idempotence

> CONVENTION: Every deallocator at a label must be null-safe, or the pointer re-inerted after discharge. No third option.

- Null-safe by contract: `free`, `mi_free`, `ano_aligned_free` (mi_free by delegation), every `vkDestroy*`/`vkFree*` on `VK_NULL_HANDLE`.
- Not null-safe: `dlclose`, `fclose`, third-party destroy, anything taking a handle by value with no documented null case. Read the contract.
- Re-inert after discharge when a path below still names what was released, or restructure so nothing is released twice. `ano_vk_init_compute` took the second: one `out:` (`compute.c:486`) discharges all nine pairs at most once. Where mid-function discharge must stay, split the label: `shadow_pipe.c:187`.

Discharge set: acquired and not yet published. A resource that escapes through an out-param on success is not the label's. `createTextureImage` hands its staging buffer out at `texture.c:526`; a label there owns that buffer only above that line.

Success epilogue and label body are frequently identical (`tonemap.c:141-144` against `:154-157`; both `text_raster.c` builders). Duplication accepted under roughly six lines. Trade flips when the duplicate is a per-stage tail keeping null-after-discharge. `ano_vk_init_compute` deleted its tails for a merged `out:` behind hoisted `bool ok` (`compute.c:29`, `:486`); `flat.c:309` same at one stage. Resources live until exit (nine SPIR-V blobs/modules, under a megabyte, once): fine on one-shot init, not in the frame loop.

## What a label must never do

- Allocate. A cleanup path that can fail has no cleanup path.
- Log at more than one site. One diagnostic per failure. Distinct messages log at the arm; label stays silent.
- Return anything but the failure value. No partial success.
- Discharge what the caller owns, or what teardown will discharge later. `flat_init_with_cull` frees blobs and modules; leaves pipelines, layout and cache to `ano_pipeline_flat_cleanup`.
- Run on the success path, if named `fail`. Merged `done:`/`out:` runs on both by design.

## Relationship to the rest

- Bugs go in `docs/BUGS.md`, not beside the label. Known-wrong unwind = census entry with named test, never `// TODO: leaks here`.
- Contract forks get adjudicated, not hidden in error paths. Unsettled ownership lifted out and ruled on (`docs/BUGS_DONE.md`, "Adjudicated contracts").
- Five-tier hierarchy (`.claude/skills/invariants/SKILL.md`) ranks the repair. Labelled unwind is tier-1: makes the leak unconstructible.


# Part 2 - the map

Survey of where the idiom would land next. Nothing here authorizes conversion. No code changes now.

Not a custody fix: changes where discharge is written, never who owns. Entry blocked on ownership stays blocked.

Not warranted below about three arms: `render_slots_alloc_range` closed with `alloc_range_rollback` per arm and is right; `initSwapChain` (`swapchain.c:149`) took null-safe `freeSupportDetails` in the trivial-fix wave, deleted 2026-07-26 when the query returned values only. Roughly: one or two arms take a helper, three or more take a label.

## Blocked on a ruling - do not convert

`shadow_resources.c:22` - `createShadowResources`. Largest unwind in tree. Nineteen `return false` arms after first acquisition (docs/BUGS.md says sixteen; file has gained arms) and none discharges anything. Unwind would own: per frame `frustumBuffer`+`frustumAlloc` and `sampleVPBuffer`+`sampleVPAlloc`, times `MAX_FRAMES_IN_FLIGHT`; atlas and temp images with allocations, array views and `ANO_SHADOW_ATLAS_LAYERS` layer views each; transient seed command buffer; `shadowDepthImage` with allocation and `ANO_SHADOW_FRUSTUM_COUNT` slice views; two `SlotUpload`s; `calloc`'d `shadowCfgMirror`. Blocker: refusal semantics per docs/BUGS.md; transient CB and two SlotUploads on refusal undecided. Adjudicate first. Secondary once unblocked: label must re-inert every handle it destroys; `cleanupVulkan` walks the same published fields.

`texture.c:450` - `createTextureImage` and `texture.c:381` - `createTextureImageFromPixels`. Converted 2026-07-26: both carry one `fail:`; ruling in `docs/BUGS_DONE.md` under retired custody chain. Staging buffer hazard dissolved: out-param collapsed to `bool keepStaging`; buffer published into returned package only in success epilogue; label owns it unconditionally. Decoded pixels freed and re-inerted the moment staged (Discharge idempotence). Every acquisition hoisted above first `vkCmd*`; no arm destroys an image a borrowed CB already references.

## Waiting on the resource manager

`loadFile` retirement (`pipeline.c:63`, ~20 call sites). Every builder's `{ code.data = NULL; goto fail; }` exists because `loadFile` was once non-total; deleted with `loadFile` when shader bytes arrive through `ano_res_load` (docs/BUGS_DONE.md, "The 2026-07-25 remediation"; docs/resourcemgr/resource-manager-plan.md). Do not build new unwinds on `loadFile`. Do not clean re-inerts before it goes.

Any cross-file discharge. Acquisition and only correct discharge in different TUs: label in either file is half an answer. These wait.

## Open candidates, strongest first

Ranked by what a failure strands, not arm count. First two strand live resources today; rest are repetition, asymmetry risk, or tightening.

1. `text_bake.c:507` - `ano_text_font_bake_ranges`. Nine arms after first acquisition. Leak is real: `glyphs` and `map` from caller's heap, no arm frees them, `*out` zeroed at `:523` so caller holds no handle. Last two arms (`:636`, `:643`) also strand `pairs` that `bake_kerns` published into `out->kerns`. Label discharges `glyphs`, `map`, `out->kerns` and re-totals `*out`; `LOCALHEAPATTR` scratch needs nothing. Settle with callee `text_bake.c:440` - `bake_kerns` (four arms); shared caller-heap ownership ambiguity.
   Converted 2026-07-26 - single `fail:`, kern publish moved commit-last; record in docs/BUGS_DONE.md.
2. `main.c:350` - `music_world_start`. Four arms, none discharging. Arm at `:396` fires after `ano_audio_init` succeeded: live mixer thread and open device survive a reported failure. Caller (`:1044`) logs and continues; `music_world_stop` does not run until `:1069`. Label discharges `g_synth`, `g_music` and on that last arm only `ano_audio_shutdown`. Cascade shape: discharge set differs per arm.
   Converted 2026-07-26 - single `fail:` through `music_world_stop(false)`; cascade premise was false - `ano_audio_shutdown` is handle-guarded (`ano_audio.c:270-273`), no-op when audio never came up. Record in docs/BUGS_DONE.md.
3. `geometry.c:107` - `geometry_pool_emit_level`. Cleanest conversion in tree. Six arms, each a paste of the same free block growing by one destroy: `meshlets`, `meshlet_vertices`, `meshlet_triangles`, `bounds`, then `stagingBuffer`, then `transientPool`. Success tail at `:348` is a seventh copy. Nothing escapes; pool reservations commit after the last failure arm; label has nothing to roll back.
4. `vulkanMaster.c:365` - `initVulkan`. Thirty-odd arms. Every arm but one spells `unInitVulkan(); return false;`; the one that does not is `:674` (`ano_render_load_scene_assets`), which strands the whole initialized renderer. Standing docs/BUGS.md lead `vulkanMaster.c:593`. Latent today only because the callee always answers true. Label body already exists as `unInitVulkan`.
5. `commands.c:343` - `createSyncObjects`. Four arms. Short-circuit `||` chains at `:354-356`, `:369-370`, `:377-378` strand earlier semaphores of their own statement. Discharges per-frame `imageAvailable`/`renderFinished`/`frameFence`, four timelines, timestamp query pools, per-frame `pickReadback` buffer and allocation.
6. `descriptors.c:112` - `createDescriptorSets`. Ten stranding arms over eleven batches published into `rendererState` as allocated. Pool created without `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` (`descriptors.c:56`): label cannot free sets; it re-zeroes published fields so teardown never sees a set handle from a dying pool. Free here is wrong; unpublish is right.
7. `scene_buffers.c:284` - `createCullingBuffers`. Six bare arms over a `SlotUpload` plus six buffer/allocation/mapped-pointer triples times `MAX_FRAMES_IN_FLIGHT`. Nothing discharges; survives because handles land in `state->culling` and global teardown is handle-guarded.
8. `attachments.c:207` - `createColorResourcesChecked` (twelve arms), `attachments.c:60` - `createDepthResources` (six), `attachments.c:134` - `createHiZResources` (four). All three publish per-frame-per-view images, allocations and views; all three delegate discharge to `cleanupSwapChain` by contract. Tightenings, not leaks. `createHiZResources` wrinkle: `hizMipCount` published at `:150` before views exist; refused mip view leaves teardown walking dead handles unless the label re-zeroes the count.
9. `pipeline.c:127` - `ano_vk_init_pipelines`. Five arms, each abandoning prototypes the arms above built. `ano_vk_cleanup_pipelines` (`:161`) is the label body, already written.
10. `audio_wav.c:122` - `ano_audio_wav_load`. Eight arms, no leak today. `fclose(f)` written twice, `mi_free(raw)` five times, across three live-set shapes. Teaching case: `fclose(NULL)` is undefined, so the file phase cannot join a null-safe label; phases are disjoint (`:137` closes the file before the first `raw` arm). Two labels, `shadow_pipe.c`'s split exactly.
11. `layouts.c:161` - `ano_vk_init_cull_layout`. Three arms over four descriptor set layouts; two (`:288`, `:316`) do not even log.
12. `slot_upload.c:78` - `slot_upload_create`. Six arms over device buffer, per-frame staging buffers and per-frame `malloc`'d region arrays. Header makes "caller owns the partial `*b`" the contract; a label here is a contract change (makes the function total) and must be proposed as one. Sibling `slot_upload.c:125` `slot_upload_grow_staging` has the purest copy-paste shape at three arms.
13. `scene_buffers.c:401` - `createFallbackResources`. Three arms over a granted geometry-pool slot, the fallback image trio and a bindless texture slot. Half-blocked: `bindless_register_texture` only ever increments, so the bindless grant is not reversible. Truthful label discharges mesh slot and image and says the texture slot is spent. Settle that before writing it.
14. `ano_GltfParser.c:131` - `parseGltf`, prologue only. Four arms repeating `cgltf_free(data)`, one adding `free(assetBase)`; scratch heap is `LOCALHEAPATTR`. Comment at `:161` claims one failure arm; there are four. Rest of function blocked: texture loop's `continue` arms are the fenced `ano_GltfParser.c:277` custody entry; converting the prologue must never be reported as having touched it.
15. `audio_mixer.c:24` - `ano_audio_graph_init` (three arms, half-built bus graph) and `audio_fx.c:36` - `ano_audio_fx_init` (three arms in the REVERB case over `pre`, `ap[0..1]`, `line[0..i-1]`). Both leave partial state on the caller's heap; both discharged wholesale by caller's `mi_heap_destroy`. No live leak. Lowest priority; convert only if heap ownership is revisited.

## Checked and not candidates

Already converted, leave alone: all three audio platform backends - `alsa_start` (`audio_linux.c:93`), `pw_start` (`:371`), `coreaudio_start` (`audio_macos.c:56`), `wasapi_start`/`wasapi_main`/`dsound_start`/`dsound_main` (`audio_win64.c:472`/`:313`/`:808`/`:691`) - all cascading labels, all correct for their non-null-safe deallocators.

Already discharged by scope; converting would be a regression: `ano_meshoptimizer.c` `ano_optimize_vertex_cache` (`:119`) and `ano_simplify_ex` (`:662`), and `audio_mixer.c:685` `ano_audio_render_offline`, all `LOCALHEAPATTR`. Bare returns correct by construction.

Below the bar or holding nothing: `getRequiredExtensions` (`instance.c:171`, no arms; defects are unchecked `calloc` and per-string `strdup` leak, neither a label addresses), `createImageViews` (`swapchain.c:455`, already unwinds its own prefix), `recordCommandBuffer` (`frame/record.c:20`, three arms but no resource), `ano_vk_ui_init` (`ui_raster.c:186`, degrades rather than refusing), `ano_text_font_load` (`text.c:124`, two arms, under the bar), plus `createInstance`, `initWindow`, `ano_vk_init_geometry_pool`, `createDescriptorPool`, `createBindlessTextureArray`, `ano_synth_create`, `ano_synth_score_begin`, `ano_synth_live_begin`, `ano_music_create`, `ano_render_ui_set`.

## Hazards and adjacent findings

VLAs in three functions in `src/vulkan_backend/instance/`: `device.c:43`, `commands.c:393`, `instance.c:225`. A label below one with any arm above it is the jump-into-VM-scope the standard forbids. Hoist or fix the bound first.

Every fail label in tree rests on `ano_aligned_free(NULL)` being a no-op, and that holds only by delegation to `mi_free`. `anoptic_memory.h:51` does not say so. It should.

Two defects this sweep surfaced were not goto matters; both disposed 2026-07-26, analyses in docs/BUGS.md: pickPhysicalDevice claim rejected on verification - `:481` arm frees the enumeration array itself, and `ctx->availableDevices` is ctx-owned custody reclaimed by `cleanupVulkan` on every failure arm, since `vulkanGarbage.ctx` registration at `vulkanMaster.c:416` precedes the call. createTextureImage claim already tallied - open `texture.c:486` entry subsumes those arms.
