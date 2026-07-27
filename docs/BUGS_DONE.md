# Bugs, retired

Completed ledger: fixed, refuted, and wontfix retirements from `docs/BUGS.md`, plus campaign accounting, contract decisions, removed guards, and test-side corrections. Active defects, entry instructions, root-cause tags, and the reusable remediation taxonomy live in `docs/BUGS.md`.

2026-07-24 retirement text is verbatim as closed. The 19 campaign census retirements are condensed. No file:line entry appears in both `docs/BUGS.md` and this file.


## Audio

### Implementation bugs

[X] Fixed: delay.h:39: delay cap floated free of the allocation floor; tap index UB. test: anotest_dspdelayguard
- checked-arithmetic
- fix (2026-07-25): ceiling from mask; cap field deleted.

[X] Fixed: ano_audio.c:257: buffer_register frames*channels*sizeof(float) wraps past SIZE_MAX; near-empty block, huge frame count kept. test: anotest_audioguard
- checked-arithmetic
- fix (2026-07-24): frames compared against headroom quotient before the product.

[X] Fixed: audio_wav.c:34: wav_write same unchecked product; truncated WAV, fact chunk lies, call returns true. test: anotest_wavguard
- checked-arithmetic
- fix (2026-07-24): frames compared against RIFF headroom quotient before the product.

[X] Fixed: audio_linux.c:168: alsa_stop joins deviceThread before deviceState guard; siblings guard first. test: pending: linux-only, unreachable via today's call order
- odd-sibling-out
- fix (2026-07-24): alsa_stop guards deviceState before joining.

[X] Fixed: audio_fx.c:100: limiter analysis window one sample short of delay line; peak leaves window on emit; ceiling breached. test: anotest_limiterguard
- lookahead-off-by-one
- fix (2026-07-24): analysis window is lookahead + 1.

[X] Fixed: ano_audio.c:204: ano_audio_shutdown never discharges adopted sample blocks; LIVE/queued/unpolled blocks orphan. test: anotest_audioshutguard
- ownership-leak
- fix (2026-07-25): audio_discharge_blocks after mixer join and device stop; drains command ring, event ring, buffer table via ano_audio_block_free. Destroy frees outright.

[X] Fixed: audio_win64.c:589: DSBSTATUS_BUFFERLOST Restore/Play left ring and writeCursor stale; garbage audio, phase desync. test: pending: needs real dsound.dll and DSSCL_PRIORITY focus change
- recovery-desync
- fix (2026-07-25): dsound_recover restores, silences ring, re-anchors writeCursor, Play; still-lost retries.


## Filesystem

### Implementation bugs

[X] Fixed: filesystem_win64.c:137: ano_fs_write advanced on WriteFile written==0; TRUE zero-progress spun forever. test: pending: no seam for zero-progress TRUE
- unbounded-spin
- fix (2026-07-25): written == 0 folds into WriteFile failure test; returns -1.

[X] Fixed: filesystem_linux.c:65: ano_fs_resolvepath accepted mkdir EEXIST without directory check; file/symlink squat returned ready path. test: anotest_fsguard
- seam-validation
- fix (2026-07-24): fs_mkdir accepts EEXIST only for directories; all three userpath bodies route through it.


## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

[X] Fixed: log_core.c:205: deferred capture stored sourceFile as raw pointer; drain deref'd dangling path. test: anotest_logsrcguard
- dangling-capture
- fix (2026-07-24): sourceFile deep-copied into capture blob.

### Implementation bugs

[X] Fixed: log_core.c:817: g_batch sized to ring footprint; deferred render width can exceed; room underflow, heap overwrite. test: anotest_logflood
- size-mismatch
- fix (2026-07-25): flush before worst-case rendered record lacks room; ANO_LOG_BATCH_CAP + static_assert. Full width, no drops.


## Math

### Interface-level bugs and logic inefficiencies

[X] Fixed: anoptic_math.h:21: mat4/Vector4 align 4 while std430 wants 16; RenderEntity/DisplayState mat4 offsets wrong. test: pending: Static_assert pins align; no runtime CTest until types change
- alignment-contract-gap
- fix (2026-07-24): alignas(16) on mat4/Vector4 (Vector2 8); static_asserts on types and CullView/RenderEntity offsets; Vector3 stays packed.


## Memory

### Implementation bugs

[X] Fixed: memalign_linux.c:13 / memalign_macos.c:13 / memalign_win64.c:13: ano_aligned_malloc size 0 returned live block against NULL contract. test: anotest_memguard
- unguarded-delegation
- fix (2026-07-24): all three refuse size 0 or alignment 0 with NULL.


## Mesh

### Implementation bugs

[X] Fixed: ano_meshoptimizer.c:282: ano_build_meshlets skipped max_vertices<3 / max_triangles<1 reject that bound enforces; buffer sized from bound overruns. test: anotest_meshguard
- seam-validation
- fix (2026-07-24): build rejects same acceptance domain as bound.

[X] Fixed: ano_meshoptimizer.c:955: link/tetra collapse exclusion only when growth guards on; ano_simplify ran illegal collapses. test: anotest_meshsimplifyguard
- feature-gated-check
- fix (2026-07-24): exclusion runs on guards-off path too.


## Music

### Interface-level bugs and logic inefficiencies

[X] Fixed: music_host.c:193: cadence_policy override accepted any value; OOB policy table index; ROMAN NULL deref on compose thread. test: anotest_musiccadenceguard
- seam-validation
- fix (2026-07-24): cadence_policy pin and config cycle validated against AnoCadencePolicy.

[X] Fixed: music_host.c:194: mode override accepted any value; ano_mode_intervals table OOB every bar. test: anotest_musicmodeguard
- seam-validation
- fix (2026-07-24): mode pin and config mode validated against AnoMode.

[X] Fixed: music_host.c:66: cadencePolicyCount copied unvalidated; count>8 walks past cadencePolicies[8]. test: anotest_musiccadcountguard
- seam-validation
- fix (2026-07-24): cadencePolicyCount clamped to 8-slot cycle like motifLibraryCount.

### Implementation bugs

[X] Fixed: music_host.c:124: expand copied motif n verbatim; n>ANO_MOTIF_MAX overran 32-wide buffers. test: anotest_motifboundguard
- fixed-array-overflow
- fix (2026-07-25): both ingress seams clamp n to ANO_MOTIF_MAX; static_asserts weld extents.

[X] Fixed: music_arp.c:102: arp emitted past ANO_METER_MAX_SLOTS for meters past 8 quarters. test: anotest_musicarpguard
- fixed-array-overflow
- fix (2026-07-24): arp slot counts clamped to ANO_METER_MAX_SLOTS.

[X] Fixed: music_perc.c:121: perc kick/hat past ANO_METER_MAX_SLOTS; shift UB for slot>=32; emit buffers overrun. test: anotest_percmeterguard
- fixed-array-overflow
- shift-ub
- fix (2026-07-24): perc slots clamped; hit scratch and emit buffers capped.

[X] Fixed: music_voicing.c:114: ano_voice_chord cands[256] function-scope static; concurrent engines race. test: anotest_musicguard
- shared-mutable-state
- fix (2026-07-25): cands thread_local; MODE_INTERVALS const table; no mutable file-scope state in src/music.

[X] Fixed: music_voicing.c:120: voices unbound vs MAX_VOICES; options/Cand/out overrun; drop-branch and count==0 siblings. test: anotest_voicingboundguard
- seam-validation
- fix (2026-07-25): V in [1, MAX_VOICES] or return 0; drop row fill bounded; count==0 fallback deleted.

### Interlink / Composition bugs

[X] Fixed: music_arp.c:106: arp accent +4 after clamp with no re-clamp; velocity 128..131 into synth. test: anotest_musicsynthguard
- seam-validation
- fix (2026-07-24): arp accent re-clamped to 1..127.


## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

[X] Fixed: shadow_casters.c:97: RenderLightType used as shadowTypeUsed[3] index raw; OOB aliased free-list counts. test: anotest_shadowtypeguard
- seam-validation
- fix (2026-07-25): shadow_static_budget sole decode; LIGHT_TYPE_COUNT weld; gate_light_type at drain.

[X] Fixed: render_slots.c:35: logical_reserve on UNMAPPED sentinel wraps need to 0; wild write at logicalToSlot[0xFFFFFFFF]. test: anotest_slotsentinelguard
- checked-arithmetic
- fix (2026-07-24): UNMAPPED rejected at alloc/reserve seam.

[X] Fixed: apply.c:125: RCMD_CREATE/BULK_CREATE no mapped-check; duplicate id strands old slot as undestroyable ghost. test: anotest_slotdupguard
- seam-validation
- fenced (2026-07-25): resource-management entry left open; anotest_slotdupguard stayed red on purpose.
- fix (2026-07-25): CREATE refuses live render_id; BULK validates batch-atomic; alloc_range refuses mapped mid-walk.
- amended (2026-07-26): consumer pre-screen collapsed; bulk hears alloc_range; free_owned_bulk deleted for ano_render_command_release.

### Implementation bugs

[X] Fixed: swapchain.c:428: createImageView returned indeterminate local on failure; garbage VkImageView stored. test: anotest_imageviewguard
- no-abort
- fix (2026-07-25): return out-param only on success; else VK_NULL_HANDLE.

[X] Fixed: commands.c:201: stagingTransfer copy-failure arm dead; false success; invalid CB handles on alloc fail. test: anotest_stagingcopyguard
- no-abort
- fix (2026-07-25): helpers report own VkResult; stagingTransfer single discharge; checked endSingleTimeCommands variant.

[X] Fixed: texture.c:415: createTextureImage ignored createDataBuffer bool; memcpy through NULL/indeterminate. test: anotest_texacquireguard
- no-abort
- fix (2026-07-25): callee outs total; consumption on write branch; [[nodiscard]] weld.

[X] Fixed: scene_buffers.c:35: createMaterialBuffer/Transform/Culling logged FATAL and fell through; published dead handles. test: anotest_scenebufferguard
- no-abort
- fix (2026-07-25): refused buffer returns false; creator abort into initVulkan unwind.

[X] Fixed: texture.c:437: generateMipmaps status discarded; unfilterable format published half-written wrong layout. test: anotest_texmipchainguard
- no-abort
- feature-gated-check
- fix (2026-07-25): mip count and blit capability decided together; unfilterable loads single mip + WARN.

[X] Fixed: vulkanMaster.c:505: depth/Hi-Z FATAL fall-through; initVulkan returned true with no depth. test: anotest_initdepthguard
- no-abort
- fix (2026-07-25): failed depth/Hi-Z tears down and returns false like sibling arms.

[X] Fixed: slot_upload.c:221: ensureEntityCapacity mid-chain OOM published destroyed buffers under live descriptors. test: anotest_entitygrowguard
- partial-publish
- fix (2026-07-25): commit-last; build all replacements then install; failure discharges built replacements.

[X] Fixed: descriptors.c:39: COMBINED_IMAGE_SAMPLER budget missed binding-13 Hi-Z sampler under taskCull; overlay OOM. test: anotest_descpoolguard
- feature-list-drift
- fix (2026-07-25): binding-13 via global_set_samplers(); taskCull on allocates overlay.

[X] Fixed: window.c:214: glfwCreateWindow unchecked; NULL into eleven GLFW calls. test: anotest_windowcreateguard
- no-abort
- fix (2026-07-25): refused window logs and returns NULL; init unwinds.

[X] Fixed: commands.c:82: camera UBO EXCLUSIVE while async light-cull reads on compute; no ownership transfer. test: anotest_uboshareguard
- feature-list-drift
- fix (2026-07-25): createDataBufferShared owns family list; asyncLc on → CONCURRENT.

[X] Fixed: compute.c:83: createShaderModule NULL discarded; dead module into vkCreateComputePipelines. test: anotest_shadermodguard
- no-abort
- fix (2026-07-25): ano_pipeline_stage sole .module writer; refused mint aborts compute init.

[X] Fixed: flat.c:90: twenty-one graphics shader mints discarded NULL sentinel. test: anotest_gfxshadermodguard
- no-abort
- fix (2026-07-25): same sole-decode gate; refused module aborts builder.

[X] Fixed: record.c:29: recordCommandBuffer void; failed begin still submitted never-recorded CB. test: anotest_recordbeginguard
- no-abort
- fix (2026-07-25): begin/end return bool [[nodiscard]]; submit/present behind recording status.

[X] Fixed: slot_upload.c:277: growth re-pointed UBOs but not shadowsetup binding 1; dispatch read destroyed TransformSSBO. test: anotest_vkguard
- missed-repoint
- fix (2026-07-25): updateUboDescriptorSets sole re-point for all TransformSSBO consumers including shadowsetup.

[X] Fixed: texture.c:435: createTextureImage copy used texWidth for both extents; non-square upload wrong/OOB. test: anotest_texuploadguard
- copy-paste-error
- fix (2026-07-24): copyBufferToImage handed texHeight as second extent.

[X] Fixed: device.c:663: transfer-queue fetch armed by computePresent; UINT32_MAX family into vkCreateDevice. test: anotest_transferqueueguard
- copy-paste-error
- fix (2026-07-24): armed by transferPresent; skip absent families; transfer falls back to graphics.

[X] Fixed: record_views.c:302: aux inset y in uint32 wraps negative for small H; scissor VUID breach. test: anotest_insetscissorguard
- checked-arithmetic
- fix (2026-07-24): inset that does not fit is skipped.

[X] Fixed: window.c:192: glfwGetMonitors(NULL) plus pre-init enumerateMonitors left monitorCount 0. test: anotest_windowcreateguard
- two-source-of-truth
- fix (2026-07-25): one post-glfwInit query serves bound and array.

[X] Fixed: gpu_alloc.c:12: 1<<i signed shift UB at i==31 in memory-type probe (commands.c twin). test: anotest_memtypeshiftguard
- shift-ub
- fix (2026-07-24): 1u << i in both probes.

[X] Fixed: ano_GltfParser.c:30: no cgltf_validate before accessor reads; hostile asset OOB; sixteen unchecked callocs. test: anotest_gltfguard
- seam-validation
- fix (2026-07-25): cgltf_validate after load_buffers; one bump + one scratch heap.

[X] Fixed: swapchain.c:110: querySwapChainSupport calloc'd arrays never freed; leak per boot/resize. test: anotest_swapleakguard
- ownership-leak
- fenced (2026-07-25): resource-management entry left open; anotest_swapleakguard stayed red.
- fix (2026-07-25): freeSupportDetails sole discharge on both initSwapChain exits.
- amended (2026-07-26): query total into stack arrays; values-only SwapChainSupport; freeSupportDetails deleted.

[X] Fixed: render_slots.c:92: alloc_range published mid-batch then OOM; phantom mapped prefix, alias on next CREATE. test: anotest_slotrangeguard
- partial-publish
- fenced (2026-07-25): held with apply.c:125; anotest_slotrangeguard stayed red.
- fix (2026-07-25): alloc_range_rollback unpublishes prefix; mid-walk refuses already-mapped id.
- amended (2026-07-26): sentinel pre-pass deleted; reserve+mapped fused; alloc same refusal; [[nodiscard]].

[X] Fixed: flat.c:244: failure arms after shader acquire orphaned buffers/modules; shadow_pipe same shape. test: anotest_flatorphanguard
- ownership-leak
- fenced (2026-07-25): SPIR-V orphan family head; anotest_flatorphanguard stayed red.
- fix (2026-07-25): labelled unwind; inert at declaration; fail: discharges unconditionally.
- amended (2026-07-26): single-exit done:/blur_done: merges success/failure discharge.

[X] Fixed: compute.c:80: SPIR-V orphan family across compute/tonemap/additive/transmission/text_raster builders. test: pending: family needs one discharge decision
- ownership-leak
- fix (2026-07-25): labelled unwind per builder; arms before first acquisition stay plain returns.
- amended (2026-07-26): compute merged out: with sh[SH_COUNT]; text_build_blend_pipeline merges overlay/world.

[X] Fixed: pipeline.c:63: loadFile left *buffer indeterminate/dangling on failure arms; free() vs ano_aligned_malloc. test: none: boot path; callers' unwinds are the observable
- partial-out-param
- fix (2026-07-25): prologue zeroes *buffer; short-read via ano_aligned_free. Temporary until ano_res_load.

[X] Fixed: texture.c:426: createTextureImage failure after staging acquire orphaned VkBuffer; sibling FromPixels same. test: anotest_texstagingguard
- ownership-leak
- fenced (2026-07-25): custody span left open; anotest_texstagingguard stayed red.
- amended (2026-07-26): settled with texture.c:486 / glTF / components texture-custody retirement.
- fix (2026-07-26): AnoTextureResult + TexturePackage; labelled unwind; keepStaging bool. test: anotest_texunwindguard

[X] Fixed: texture.c:486: post-staging failure arms published live image/alloc with view/staging unwritten. test: anotest_texstagingguard
- partial-out-param
- ownership-leak
- fix (2026-07-26): closed with texture.c:426 labelled unwind; usage mask accept-form. test: anotest_texunwindguard

### Interlink / Composition bugs

[X] Fixed: ano_render_bridge.c:204: ui_prim_valid never checked paint stop window vs stopCount; GPU OOB. test: anotest_uistopguard
- seam-validation
- fix (2026-07-24): reject stop window outside block table without uint32 wrap.

[X] Fixed: ano_render_bridge.c:313: publish_view stored any pose; degenerate lookAt NaN-poisons frame. test: anotest_cameraposeguard
- seam-validation
- fix (2026-07-25): accept-form pose predicate at publish seam; reject holds last accepted pose.

[X] Fixed: ano_render_bridge.c:92: destroy freed ring buffer without discharging bulk_owned payloads; shutdown leak. test: anotest_bridgeguard
- ownership-leak
- fix (2026-07-25): ano_render_command_release sole decode; destroy pops and discharges enqueued commands.

[X] Fixed: shadow_casters.c:151: static shadow release never returned region entries; shape churn exhausted budget. test: anotest_shadowreregguard / tests/anotest_vk_shadow.c
- ownership-leak
- fix (2026-07-26): exact-footprint free lists; register pops fit before bump; budget-full retires held-back block.

[X] Fixed: components.c:72: ano_vk_register_texture void-drop on realloc fail; texture orphans permanently. test: anotest_texregisterguard
- ownership-leak
- fenced (2026-07-25): adoption half of parser custody; anotest_texregisterguard stayed red.
- fix (2026-07-26): [[nodiscard]] bool; parser hears refusal and discharges package.

[X] Fixed: ano_GltfParser.c:277: parseGltf heard createTextureImage false and discharged nothing; device image orphan. test: anotest_gltftexleakguard
- ownership-leak
- fenced (2026-07-25): arms unchanged through campaign texture rounds; stayed red.
- fix (2026-07-26): construct, adopt, bindless; discharge deferred past endSingleTimeCommands via pending package.

[X] Fixed: ano_GltfParser.c:435: sticky textureSrgb; shared colour/data image created SRGB only; data samples wrong. test: pending then anotest_gltftexdomainguard, anotest_vk_texdomain
- silent-drop
- amended (2026-07-26): settled by mixed-domain texture retirement.
- fix (2026-07-26): per-image usage mask; mutable-format + dual views for mixed; colorIndex[]/dataIndex[].


## Strings (including strings_utf.h)

### Implementation bugs

[X] Fixed: ano_strings_ops.c:86: anostr_join sep.len*(count-1) wraps; oversize slips guard; write streams huge. test: pending: needs a 64 GiB parts array
- checked-arithmetic
- fix (2026-07-24): size without wrapping; oversize join rejected.

[X] Fixed: ano_strings_collate.c:75: decomp redirects for U+01EF/U+0374 hit CE-trimmed targets; case pair splits collation. test: anotest_strguard
- table-coverage-gap
- fix (2026-07-25): generator drops broken decomp for direct CE; assert_trim_closure; tables from UCD 17.0.0.

[X] Fixed: gen_unicode_tables.c:481: case-record dedup memcmp'd padding; non-deterministic regenerate; headers drifted. test: anotest_strguard regeneration path
- padding-compare
- fix (2026-07-25): field-wise dedup; comment text from generator format strings.


## Synth

### Interface-level bugs and logic inefficiencies

[X] Fixed: ano_synth.c:227: score_tempo deref'd NULL anchors before begin / after end. test: anotest_synthtempoguard
- seam-validation
- fix (2026-07-24): rejects before begin and after end.

[X] Fixed: ano_synth.c:202: tempoCount UINT32_MAX wraps anchorCap to 0; seed write OOB; begin returns true. test: anotest_synthbeginguard
- checked-arithmetic
- fix (2026-07-24): rejects tempoCount UINT32_MAX.

### Implementation bugs

[X] Fixed: ano_synth.c:246: score_event/live_bar rejected velocity==0 only; pitch/velocity upper unbound. test: anotest_synthguard
- seam-validation
- fix (2026-07-24): reject pitch>127 and velocity>127.


## Text

### Interface-level bugs and logic inefficiencies

[X] Fixed: text_gpos.c:304: GPOS offset math unchecked uint32 wrap; malformed returns success. test: anotest_gposwrapguard
- checked-arithmetic
- fix (2026-07-24): checked gadd/gmul; wrap/OOB returns documented nonzero.

[X] Fixed: text_raster_ref.c:92: ano_text_window_sum fetched pts[pointOffset] before curveCount==0 guard; blank glyph OOB/NULL. test: anotest_textblanksumguard
- seam-validation
- fix (2026-07-24): returns 0.0 for curveCount 0 before touching stream.

[X] Fixed: text_shape.c:126: final-line step from trailing byteCount-0 run; measure_runs honors no-op as height. test: anotest_textguard
- noop-not-honored
- fix (2026-07-24): final-line step from last run that styled a codepoint.

[X] Fixed: text_bake.c:507: bake failure arms left caller-heap glyphs/map/kerns stranded after zeroed-out contract. test: pending
- ownership-leak
- partial-out-param
- fix (2026-07-26): bake_kerns total on locals; single fail: discharges four caller-heap blobs; publish in one epilogue.


## Threads

### Implementation bugs

[X] Fixed: threads_macos.c:79: Darwin barrier phase/arrivals split across atomics; over-subscribed early release/deadlock. test: anotest_barriercohortguard
- odd-sibling-out
- fix (2026-07-25): arrived packed [phase:16][arrivals:16]; one CAS; generation removed from public header.

[X] Fixed: threads_macos.c:107: barrier waiter bare spin starved peers on undersubscribed CI; looked like hang. test: anoptic_threads
- unbounded-spin
- fix (2026-07-27): escalate relax → yield → 1us park; watchdog on frozen tally not wall clock.


## Time

### Implementation bugs

[X] Fixed: time_win64.c:148: TSC stamps not re-anchored across S3/S4; post-resume deltas wrap. test: pending: needs real S3/S4 resume on Windows x64
- clock-not-reanchored
- fix (2026-07-25): count = __rdtsc() + bias; large drop re-anchors via QueryUnbiasedInterruptTimePrecise.

[X] Fixed: time_linux.c:132: ano_sleep failure returned errno though clock_nanosleep never sets it. test: pending: linux-only, needs failure injection
- wrong-error-source
- fix (2026-07-24): returns clock_nanosleep's own status.

time_win64.c:310: ano_sleep us*1000 wraps for absurd us; near-instant success. test: pending: correct sleep of that length cannot be awaited
- checked-arithmetic
- wontfix (2026-07-25): guard removed; hot path; rejected domain ~585000 years.

[X] Fixed: time_win64.c:337: coarse Sleep cast to DWORD; multi-week truncates or Sleep(INFINITE). test: pending: no timer-failure seam
- truncating-cast
- fix (2026-07-24): coarse Sleep chunked.

[X] Fixed: time_win64.c:252: busywait tested !=0 sentinel while module uses UINT64_MAX; dead clock spins. test: pending: no clock-failure injection
- wrong-error-source
- fix (2026-07-24): tests UINT64_MAX sentinel.
- amended (2026-07-25): 0 disjunct removed; only UINT64_MAX remained.
- amended (2026-07-25, second): whole sentinel convention deleted on all three platforms; timebase validated at init.


## UI

### Implementation bugs

[X] Fixed: ui_raster_ref.c:334: tiled eval indexed s->prims unchecked twice; OOB on stale stream. test: anotest_uitileentryguard
- seam-validation
- fix (2026-07-25): shade_entry sole decode; fail closed to transparent OVER.

[X] Fixed: ui_path.c:99: contour counter cn unbounded; MOVE overran cstart into q[]. test: anotest_uipathguard
- fixed-array-overflow
- fix (2026-07-24): cn bounded like quad budget.

[X] Fixed: ui_build.c:236: paint_push stopCount sum wraps; copy past caller array. test: anotest_uipaintguard
- checked-arithmetic
- fix (2026-07-24): free stop slots by subtraction.

[X] Fixed: ui_tiles.c:66: tilesX*tilesY wraps; cap guard passes; offsets[] OOB. test: anotest_uitileguard
- checked-arithmetic
- fix (2026-07-24): cap checked by division before product.

[X] Fixed: ui_raster_ref.c:229: stopFirst+stopCount wraps; fail-closed paint reads OOB. test: anotest_uirefstopguard
- checked-arithmetic
- fix (2026-07-24): range check by subtraction.


## Engine

### Interlink / Composition bugs

[X] Fixed: main.c:391: music_world_start seeded transport from uninitialized telemetry after retry exhaust. test: anotest_boottelemetryguard
- retry-exhaustion
- fix (2026-07-25): haveTelem tracks acquire; exhaustion warns and returns false.

[X] Fixed: main.c:350: music_world_start failure arms discharged nothing; partial worlds leaked. test: pending
- ownership-leak
- fix (2026-07-26): single fail: via music_world_stop; drain discriminator on stop.

[X] Fixed: main.c:691: HUD submit spins forever on bool false (OOM==backpressure); close during spin hangs join. test: pending then anotest_render_bridge, anotest_rendersubmitguard
- unbounded-spin
- amended (2026-07-26): console-setup submit loop in music_world_start joined the arm list.
- amended (2026-07-26): settled by AnoRenderSubmitResult retirement.
- fix (2026-07-26): six endpoints answer AnoRenderSubmitResult; spins retry BACKPRESSURE while !g_logicShouldStop.


## Removed guards

Second census, 2026-07-25: checks deleted that cost without paying. Frivolous only when input cannot arise under reasonable API use and the prevented effect is contained. ~1650 sites across sixteen modules. Eleven clean. Thirteen findings in five, plus ano_sleep wontfix. Suite after: same five pre-existing failures, later settled under Settled open decisions.

Four shapes: arithmetic artifacts on absurd domains; invariant distrust (guard decrement without matching increment); prototype max(1,...) transliteration; redundant re-checks.

### Removed

`src/audio/audio_mixer.c:187`: remaining!=UINT64_MAX before decrement. Unbounded voice countdown now unconditional; reader cannot observe difference in real runtime.

`src/audio/dsp/delay.h:54-55`: dead delay clamps at four call sites. Index masked anyway. Fractional-read clamps kept (float→uint32 UB). Doc corrected.

`src/time/time_win64.c:251`: busywait startTime 0/UINT64_MAX pre-check. Body hits same test one line later.

`src/time/time_win64.c:319`: sleep start 0/UINT64_MAX pre-check. Spin stage already gates EIO; coarse stage clock-independent.

`src/time/time_win64.c:257` and `:357`: ==0 disjuncts of endTime/now tests. Correctness: legitimate 0 ns stamp must not early-exit. See time_win64.c:252 entry.

`src/vulkan_backend/frame/frame.h:94`: dt>UINT32_MAX clamp on cosmetic frametime_pct_ms only.

`src/vulkan_backend/shadow/shadow_cache.c:150` and `:239`: counter-- guarded while matching ++ unguarded. Flag selects which counter; underflow guards deleted.

`src/vulkan_backend/instance/window.c:190,195`: monitorIndex>=0 tautology on uint32_t. else if → else.

`src/vulkan_backend/vulkanConfig.c:111`: same monitorIndex>=0 tautology.

`src/text/text_shape.c:33`: kern slot>=glyphCount guard. Function never indexes glyphs[]; search already returns 0. Behaviour above 65536: truncated key may hit real pair.

`src/music/music_melody.c:304` and `src/music/music_imitation.c:22`: k<1 max(1,...) on uint32_t (n+1)/2. Inert at n==0.

`src/mesh/ano_meshoptimizer.c:966`: bestnb!=v conjunct; written only when both hold. Golden hash holds.

### Found while removing, not yet verified

Reader notes (2026-07-25). All but music_motif.c:472 chased same day; dispositions under Remediation determinations.

- log_core.c: g_batch overflows from ordinary wide-format use.
- time_win64.c: UINT64_MAX sentinel does not survive ano_ticks_to_ns.
- window.c:192: glfwGetMonitors(NULL).
- music_host.c:124: motif n unclamped.
- ano_GltfParser.c: sixteen unchecked callocs; no cgltf_validate.
- delay.h:63: (float)(cap-1u) wrap at maxDelay==0.
- time_linux.c / time_macos.c: busywait no clock-fail exit.
- main.c:614 / :389, ui_raster_ref.c:334,362, vertex.c:104, music_motif.c:472 leads.


## Remediation determinations

Third pass, 2026-07-25, over the notes above. Eleven determinations: five tier 1, six tier 4, zero tier 5. music_motif.c:472 stayed out of scope.

`src/time/`: delete sentinel convention on all three platforms; init abort on impossible branches; three busywait loops identical.

`log_core.c` g_batch: flush before worst-case room shortfall; Static_assert on batchCap. Full width, no drops.

`music_host.c` motif n: clamp to ANO_MOTIF_MAX in expand() (and ano_director_init).

`ano_GltfParser.c`: cgltf_validate after load_buffers; collapse callocs into bump + scratch.

`window.c:192`: one post-init glfwGetMonitors(&monitorCount).

`ui_raster_ref.c:334,362`: shade_entry sole decode, fail closed.

`vertex.c:104` lookAt: accept-form pose at ano_render_publish_view.

`main.c:614` PC_NAMES: refuted as OOB (uint8_t tonic). Residual: normalize at music_host.c:78 + static_assert.

`main.c:389` telemetry: read only on acquire success; timeout returns false.

`delay.h:61-62`: ceiling from mask; delete cap.

### Implemented

All eleven landed 2026-07-25. Five structural, six seam invariants, zero fault-site guards. Suite after: headless debug four failures (remaining open decisions); four new guards green.


## Settled open decisions

Five contract forks settled and implemented on 2026-07-25. Final dispositions under Campaign closeout.

### log_core.c:817: size-mismatch (bucket 2, anotest_logflood)

Deferred record renders at format width, not ring footprint. Settled: full width, no drops, flat memory; mid-pass flush; flood pays file writes. anotest_logflood green.

### ano_render_bridge.c:92 and ano_audio.c:204: ownership-leak (bucket 4, anotest_bridgeguard, anotest_audioshutguard)

Both destroy paths tore down rings without discharging payloads. Settled: owner drains; transport payload-agnostic; destroy frees adopted audio blocks; rides-home gains teardown clause.

### music_voicing.c:114: shared-mutable-state (bucket 6, anotest_musicguard)

Function-scope static cands; concurrent engines. Settled: thread_local; const MODE_INTERVALS; no mutable file-scope state in src/music. TSan clean.

### ano_strings_collate.c:75: table-coverage-gap (bucket 5, anotest_strguard)

CE/decomp coverage disagree. Settled: assert_trim_closure in generator; smaller closure; 26 case round trips fixed; padding-memcmp surfaced separately.

### Implemented

All four landed 2026-07-25. Headless debug green 55/55 (56/56 with voicingboundguard). Render suite 26 → 22 failures. TSan clean on touched modules.


## The 2026-07-25 remediation: campaign chronology

Condensed campaign narrative. Fix bodies live in module sections and stray-fix section below.

Starting board: 50 fixed of 77 tallied (26 open + 1 wontfix). Round 1: 15 renderer census + four platform singletons (audio_win64.c:589, filesystem_win64.c:137, threads_macos.c:79, time_win64.c:148). Seven custody entries fenced; four closed in trivial-fix wave (apply.c:125, render_slots.c:92, swapchain.c:110, flat.c:244); three plus texture.c:426/:486, components.c:72, ano_GltfParser.c:277 retired later.

Rounds 2-4: 44 stray fixes (41 source, 3 test-side) → 110 of 128. Rounds 5-6: six in-scope residuals + four strays → 120 of 132. Trivial-fix wave: five decision-free opens → 126 of 133. Later 2026-07-26 waves tallied in then-active docs/BUGS.md; retirements under module headings and campaign closeout here.

Method: five-tier hierarchy; contract forks adjudicated below. Platform singletons without host runner are inspection-grade under Platform verification posture. loadFile (pipeline.c:63) made total as temporary workaround; resource manager retires via ano_res_load.


## The 2026-07-25 remediation: stray fixes

Rounds 2-4: strays beside round-1 fixes. Forty-four landed (41 source, 3 test-side). Rounds 5-6: eleven more. Ten contract questions adjudicated below.

### Render / Vulkan backend

[X] Fixed: vulkanMaster.c:220 (with :253, :279, :282, :303, :170-182, :323): unsubmitted acquire left semaphore signalled; present booked submitted frame idle. test: anotest_acquirerecycleguard
- fix (2026-07-25, round 2): one discharge door for unsubmitted exits; in-flight ledger rides submit.

[X] Fixed: attachments.c:207 (with instanceInit.h:76-81, vulkanMaster.c:532, swapchain.c:371): createColorResources void; refused images/views published live. test: anotest_attachviewguard (sibling shape)
- fix (2026-07-25, round 2): createColorResourcesChecked [[nodiscard]] + void face.

[X] Fixed: vulkanMaster.c:354 (with :103-108): vulkanGarbage.window never assigned; shutdown leaked GLFW window. test: anotest_acquirerecycleguard teardown tail
- fix (2026-07-25, round 2): ownership at mint; teardown destroys-then-clears; unconditional glfwTerminate.

[X] Fixed: vulkanMaster.c:667-673: init zero-fill ignored beginSingleTimeCommands NULL; filled into nothing. test: anotest_acquirerecycleguard init tail
- fix (2026-07-25, round 2): test sentinel at ingress; FATAL + unInitVulkan + return false.

[X] Fixed: swapchain.c:442-468: createImageViews published viewCount before array; OOM left NULL+live count. test: anotest_imageviewguard
- fix (2026-07-25, round 2): commit-last; publish only when every slot live.

[X] Fixed: vulkanMaster.c:489 (with instanceInit.h:54-55): createImageViews bool dropped; failure sniffed from views==NULL. test: anotest_imageviewguard
- fix (2026-07-25, round 2): consume bool; [[nodiscard]] on declaration.

[X] Fixed: beginSingleTimeCommands sentinel family: texture.c:57/:150/:258, slot_upload.c:207, shadow_resources.c:92, ano_GltfParser.c:404: six sites used handle without testing NULL. test: anotest_texuploadguard, anotest_texacquireguard, anotest_texmipchainguard
- fix (2026-07-25, round 2): clear at each mint; copyBufferToImage [[nodiscard]] bool; parseGltf brackets by ownership.

[X] Fixed: slot_upload.c:212: growth preserve copy used void endSingleTimeCommands; refused submit reported success with garbage prefix. test: anotest_growrollbackguard
- fix (2026-07-25, round 2): endSingleTimeCommandsChecked; commit-last makes refusal total.

[X] Fixed: compute.c:77/:144/:187/:242/:314/:371/:426/:459 and flat.c:52: nine calloc results unchecked into PipelinePrototype.implementations. test: anotest_pipelineallocguard
- fix (2026-07-25, round 2): runtime seam at mint ahead of SPIR-V acquisition.

[X] Fixed: scene_buffers.c:34 and every creator: refused create left undefined/dead handles in RendererState slots. test: anotest_scenebufferslotguard
- fix (2026-07-25, round 2): one total-out-param helper; commit-last; [[nodiscard]].

[X] Fixed: shadow_casters.c:103-127 (call site apply.c:160-162): re-register static light double-booked budget/region. test: anotest_shadowreregguard
- fix (2026-07-25, round 2): acquisition releases row first; matching footprints rewrite in place.

[X] Fixed: shadow_casters.c:77-87 (call site apply.c:177-187): enabled row chased recycled slot when rewrite omitted revoke. test: anotest_shadowreregguard
- fix (2026-07-25, round 2): stage_command_fields revokes static shadow when payload does not cast; backend.h states contract.

[X] Fixed: swapchain.c:378-384: recreateSwapChain sniffed depthView==NULL after createDepthResources. test: compile-time [[nodiscard]]
- fix (2026-07-25, round 3): createDepthResources [[nodiscard]] bool; both sites consume.

[X] Fixed: vulkanMaster.c:680: init light/shadow zero-fill used void endSingleTimeCommands face. test: anotest_initdepthguard
- fix (2026-07-25, round 3): checked face selected by failure policy.

[X] Fixed: vulkanMaster.c:351 vs window.c:173: enumerateMonitors before sole glfwInit; permanent empty monitor list. test: none honest
- fix (2026-07-25, round 3): reorder; dependency stated at call site.

[X] Fixed: vulkanMaster.c:176-181, :225-228, :314-317: unrecoverable frame faults logged and continued; next acquire ate pending signal. test: anotest_renderlatchguard
- fix (2026-07-25, round 3): file-scope terminal latch; three FATAL arms close window; early-out at drawFrame top.

[X] Fixed: texture.c:25-31 (consumed at ano_GltfParser.c:433-436 and scene_buffers.c:437): bindless_register_texture answered 0 for full and for first slot. test: anotest_texcontractguard
- fix (2026-07-25, round 3): ANO_BINDLESS_NONE out of index domain.

[X] Fixed: texture.c:431 and :369: staging size computed narrow then widened; wrap undersized staging vs true extent. test: anotest_texcontractguard
- fix (2026-07-25, round 3): widen leftmost operand.

[X] Fixed: texture.c:212-216: createImageShared failure left *image indeterminate, *imageAlloc unassigned. test: anotest_texcontractguard
- fix (2026-07-25, round 3): total both outs on arm with (GpuAllocation){0}.

[X] Fixed: texture.c:227: vkBindImageMemory VkResult discarded; unbacked image returned true. test: anotest_texcontractguard
- fix (2026-07-25, round 3): check bind; destroy minted image; total outs; return false.

[X] Fixed: texture.c:147 and :250: copyBufferToImage/generateMipmaps external with no header. test: none (toolchain)
- fix (2026-07-25, round 3): internal linkage.

[X] Fixed: shadow_resources.c:34/:46/:72/:149: four dropped vkBind*Memory results; unbacked objects, init success. test: none (RM fence)
- fix (2026-07-25, round 3): hear status; return false; no discharge added.

[X] Fixed: slot_upload.c:44/:97/:112/:142/:205: five dropped vkBindBufferMemory; unbacked buffer installed. test: none (stubs always SUCCESS)
- fix (2026-07-25, round 3): bind refusal into existing failure channel.

[X] Fixed: compute.c:95/:164/:176/:231/:308/:398/:455/:490, flat.c:19, additive.c:18, transmission.c:17, shadow_pipe.c:39, tonemap.c:50, text_raster.c:348: fourteen vkCreatePipelineCache results discarded. test: anotest_protopairguard
- fix (2026-07-25, round 3): refused cache zeroed; init continues; NULL valid pipelineCache.

[X] Fixed: additive.c:48 and transmission.c:47: last unchecked-calloc sites into implementations. test: anotest_protopairguard
- fix (2026-07-25, round 3): one-line arm before first SPIR-V acquisition.

[X] Fixed: components.c:127 (writers at compute.c ×8, flat.c:51, additive.c:47, transmission.c:46, text_raster.c:335): implementationCount published before array mint. test: anotest_protopairguard
- fix (2026-07-25, round 3): writers commit-last; flat destructor dissolves count-first.

[X] Fixed: text_raster.c:280 and :291: ano_vk_text_create_buffer partial outs + dropped bind. test: none
- fix (2026-07-25, round 3): total-out-param idiom both arms.

[X] Fixed: apply.c:34 vs :56 vs :165: RFIELD_LIGHT from light_index!=NO_LIGHT while consumers gate <STATIC_COUNT; runtime index silent-lost light. test: anotest_lightrowguard
- fix (2026-07-25, round 3): domain once at drain; refuse to absent-light + ANO_ERROR; weld in backend.h.

[X] Fixed: window.c:25-32: enumerateMonitors mi_malloc unchecked / count 0 after reorder made path live. test: anotest_monitorledgerguard
- fix (2026-07-25, round 4): total on both refusal arms; empty (NULL, 0) ledger.

[X] Fixed: vulkanMaster.c:108-113: glfwTerminate before cleanupMonitors; modes pointers invalidated; incomplete ledger clear. test: anotest_acquirerecycleguard
- fix (2026-07-25, round 4): cleanupMonitors before glfwTerminate; ledger clears completely.

[X] Fixed: texture.c:386-395 and :467-479: staging-refusal arms left image/alloc/view outs unwritten. test: anotest_texfaceguard
- fix (2026-07-25, round 4): arms totalize outs; pre-acquisition only.

[X] Fixed: scene_buffers.c:437-442: fallback-texture register mismatch warned and returned true. test: anotest_fallbackinitguard
- fix (2026-07-25, round 4): arm unwinds.

[X] Fixed: texture.c:438-448: mip log2(max(w,h))+1 ungated; log2(0)/negative → cast UB. test: anotest_texfaceguard
- fix (2026-07-25, round 4): accept-form gate at decode seam.

[X] Fixed: ano_GltfParser.c:409-465: after first ANO_BINDLESS_NONE still decoded/uploaded further textures. test: anotest_gltflatchguard
- fix (2026-07-25, round 4): parse-local bindless-full latch at pre-decode skip.

[X] Fixed: apply.c:57-71: static LightData hand-rolled without light_set_dir; spots/dirs aimed parent -Z. test: anotest_staticrowdecodeguard
- fix (2026-07-25, round 4): one shared decode for static and runtime.

[X] Fixed: apply.c:56-74 with shadow_cache.c:200-213: ShadowCasterVolume.range only on CREATE; UPDATE left influence sphere stale. test: anotest_staticrowdecodeguard
- fix (2026-07-25, round 4): refresh_static_shadow no-grant update path.

[X] Fixed: shadow_resources.c:101: moment-atlas seed used void endSingleTimeCommands; wrong layout on first sample. test: none
- fix (2026-07-25, round 4): consume checked face; return false.

[X] Fixed: pipeline.c:328-344: cleanup_pipelines freed implementations before zeroing count. test: unpinned (protopairguard does not compile pipeline.c)
- fix (2026-07-25, round 4): count-first dissolution.

[X] Fixed: scene_buffers.c:43 (mintSceneBuffer): last unchecked vkBindBufferMemory on scene mint path. test: anotest_scenebindguard
- fix (2026-07-25, round 5): check bind; destroy minted buffer; outs zeroed at entry.

[X] Fixed: texture.c:381 (createTextureImageFromPixels): caller extent ungated; 0/overflow/NULL pixels. test: anotest_texpixeldomainguard
- fix (2026-07-25, round 5): accept-form gate above acquisition; outs totalled.

[X] Fixed: apply.c:66 (static UPDATE castsShadow): castsShadow 0→1 silent drop on whole-row contract. test: anotest_staticcastdropguard
- fix (2026-07-25, round 5): diagnostic ANO_ERROR naming CREATE-only rule; behaviour unchanged.

[X] Fixed: apply.c:57 with shadow_casters.c: LightData.type vs ShadowFrustumConfig.lightType diverged on type-changing UPDATE. test: anotest_staticcastdropguard, anotest_staticrowdecodeguard
- fix (2026-07-25, round 5): next_owned_block sole ownership decode; ANO_ERROR on type mismatch; refresh on block footprint.

[X] Fixed: backend.h:21-53: static-region contract doc lagged CREATE/UPDATE/DESTROY lifecycle. test: none (comment)
- fix (2026-07-25, round 5): comment states full lifecycle and dropped-edge reports.

[X] Fixed: geometry.c:410/:420/:487 with scene_buffers.c: mesh refusal sentinel 0 == FALLBACK_MESH_INDEX; fallback upload refusal invisible. test: anotest_fallbackmeshguard
- fix (2026-07-25, round 6): ANO_MESH_NONE = 0xFFFFFFFFu; fallback-mesh arm unwinds.

[X] Fixed: anoptic_render.h:150-153: castsShadow comment documented only LIGHT_ATTACH lane. test: none (comment)
- fix (2026-07-25, round 6): public header documents both lanes per-command.

[X] Fixed: light_registry.h:29-33: refresh_static_shadow declared away from siblings/contract. test: none (header layout)
- fix (2026-07-25, round 6): declaration into backend.h beside unregister/static_shadow_row_casts.

### Audio

[X] Fixed: audio_win64.c:535: dsound_recover Restore success then Lock/Play fail left BUFFERLOST clear; recovery never re-fired; permanent silence. test: pending: hardware checklist
- fix (2026-07-25, round 2): Stop on failure exit; writeCursor commit-last; gate on not-usably-playing.

[X] Fixed: audio_win64.c:290-291 and :319-320: GetBuffer S_OK-with-NULL took no-release path; OUT_OF_ORDER forever. test: pending: hardware checklist
- fix (2026-07-25, round 3): wasapi_write sole acquire/release seam; NULL arm releases 0 frames.

[X] Fixed: audio_win64.c:370-391: GetCurrentPadding failure slept/continued; DEVICE_INVALIDATED spun ~100 Hz forever. test: pending: hardware checklist
- fix (2026-07-25, round 4): wasapi_terminal classifies permanent codes; one ANO_ERROR and return.

[X] Fixed: audio_win64.c:389: write-arm refusals unclassified; OUT_OF_ORDER/INVALID_SIZE wedged stream silent. test: pending: hardware; portable half anotest_audiopaceguard
- fix (2026-07-25, round 5): wasapi_write_checked; wasapi_packet_terminal; terminal or 50 consecutive undocumented refusals breaks.

[X] Fixed: audio_mixer.c:616: ring-full continue above telemetry publish; dead device → mixer silent forever. test: anotest_audiopaceguard
- fix (2026-07-25, round 5): ring-full with head unmoved → half-drain idle that still publishes.

### Test-side

Four guard-side repairs (three rounds 2-4, one round 6). No census line.

[X] Fixed: tests/anotest_growrollbackguard.c (new): commit-last growth rollback unobserved. test: anotest_growrollbackguard
- fix (2026-07-25, round 2): pins nine capacity faces across mid-chain refusals.

[X] Fixed: tests/anotest_recordbeginguard.c:232: discarded [[nodiscard]] recordCommandBuffer bool. test: same
- fix (2026-07-25, round 2): (void) discard idiom; return deliberately unpinned.

[X] Fixed: tests/anotest_acquirerecycleguard.c presentation-failure tail: expected continue past any present failure. test: same
- fix (2026-07-25, round 4): phase splits at recoverable line; asserts both halves.

[X] Fixed: tests/anotest_staticrowdecodeguard.c:181: comment claimed any ANO_ERROR is failure; stronger than CHECKs. test: same
- fix (2026-07-25, round 6): comment matches CHECK scope.

### Adjudicated contracts (2026-07-25)

Ten rulings written into the tree. First seven rounds 1-4. Last three rounds 5-6.

backend.h, static-region: STATIC rows/shadows scene-lifetime, caller-owned; no entity-follow on DESTROY; re-registration is replace; RFIELD_LIGHT no field mask; castsShadow 0 is revoke.

anoptic_time.h:20, exclude-suspend: monotonic clock excludes suspended time; TSC re-anchor via QueryUnbiasedInterruptTimePrecise; mincore linked.

texture.h, VK_NULL_HANDLE borrow: no-borrow and failed mint both take per-op path; refused per-op mint answers false with nothing recorded.

PipelinePrototype, commit-last: writers own (count, implementations) atomicity; teardown dissolves count-first.

vulkanMaster.c, terminal latch: unrecoverable frame faults latch through close door; recreate arms untouched.

ano_GltfParser.c, bindless-full latch: after first ANO_BINDLESS_NONE skip further decode/upload in that parse.

apply.c, shared static light decode: one decode for static and runtime; casting UPDATE refreshes owned volumes via refresh_static_shadow.

bridge/apply.c, static-UPDATE report rate: unbounded per offending command, matching gate_light_domain.

geometry.h, mesh refusal sentinel: ANO_MESH_NONE = 0xFFFFFFFFu, welded against mesh absents.

include/anoptic_render.h, castsShadow static lane: public header documents both lanes per-command.

### Platform verification posture

Four platform defects plus four Windows strays: inspection-grade with checklists, not host-verified. Darwin barrier fix verified on-host.

Darwin barrier: negative control (9/count-3 → 611 early releases, 4662 unclosed/20000; 16/count-5 deadlocked) vs fix (exact cohorts, TSan clean). anotest_barriercohortguard green 57/57; passes on Linux/glibc.

Win64 census (inspection): filesystem_win64.c:137 needs redirector/filter TRUE+0 written. audio_win64.c:589 needs DSound + DSSCL_PRIORITY focus loss. time_win64.c:148 needs TSC election + real S3/S4; mincore link landed.

Windows strays: WASAPI packet-arm latch; DSound recovery; WASAPI packet pairing; WASAPI terminal latch. Win32-gated guards left open until a Windows runner exists.


## Campaign closeout (2026-07-27)

The 2026-07 bug hunt is closed. Final board 151 of 151 tallied findings retired: 149 fixed, one refuted, one wontfix, zero open. Post-census test failures recorded separately; do not alter source tally.

### Final accounting

| Disposition | Count |
|---|---:|
| Fixed | 149 |
| Refuted | 1 |
| Wontfix | 1 |
| Open | 0 |
| Tallied total | 151 |

Source census began with 70 verified findings and 41 lead records; four leads duplicated verified findings, yielding 107 distinct initial concerns. Later passes promoted new verified defects. Root-cause tags were assignments, not defect counts; every finding had one primary remediation bucket.

### Final source retirements

[X] Fixed: filesystem_win64.c:33: Windows game-path rejected whole exe path against smaller engine capacity before filename trim. test: pending / sibling parity
- odd-sibling-out
- fix (2026-07-27): reject only failed/truncated GetModuleFileNameA; trim; then validate directory against MAXPATH.

[X] Fixed: memory.c:9: ano_heap_release forwarded NULL heap to mi_heap_destroy; mimalloc debug asserts. test: pending / cleanup-attr paths
- unguarded-delegation
- seam-validation
- fix (2026-07-27): destroy only non-NULL heap.

[X] Fixed: music_host.c:45: cadence validation admitted ANO_CADENCE_NONE; sentinel became negative table index. test: pending / cadence ingress
- seam-validation
- odd-sibling-out
- fix (2026-07-27): cadence_ok accepts AUTHENTIC..DECEPTIVE; invalid config falls back to AUTHENTIC.

[X] Fixed: music_host.c:232: register-center override cast unchecked double into MIDI domain. test: pending / override seam
- seam-validation
- odd-sibling-out
- fix (2026-07-27): active only for MIDI 0..127; else clear override.

[X] Fixed: music_host.c:226: tempo override accepted 0/negative/NaN/inf; clock div0 / float→int UB. test: pending / override seam
- seam-validation
- odd-sibling-out
- fix (2026-07-27): active only for finite positive; else clear pin.

[~] Refuted: shadow_resources.c:22: proposed local partial-build unwind. Handles publish into zeroed RendererState; refusal propagates to initVulkan → unInitVulkan; cleanupVulkan destroys published shadow resources; arena spans retire with allocator teardown. No unreachable acquisition. test: n/a
- ownership-leak
- refuted (2026-07-27): end-to-end acquisition/propagation/teardown/allocator trace; no source change.

[X] Fixed: instance.c:192: getRequiredExtensions duplicated extension name strings then freed only pointer array. test: pending / boot
- ownership-leak
- fix (2026-07-27): one pointer array of borrowed GLFW/static literals.

[X] Fixed: ano_GltfParser.c:236: skipped primitive kept geometryPoolIndex 0; aliased fallback mesh. test: pending / parse skip
- silent-drop
- fix (2026-07-27): initialize rows to ANO_MESH_NONE; only successful upload overwrites.

[X] Fixed: ano_strings_collate.c:504: allocation-refusal fallbacks used qsort; broke equal-key stability. test: pending / collate fallback
- odd-sibling-out
- fix (2026-07-27): stable in-place insertion on both no-scratch arms.

[X] Fixed: text_gpos.c:294: fixed arrays truncated kern inventories; non-PairPos lookups could exhaust budget first. test: pending / GPOS extract
- silent-drop
- fix (2026-07-27): size inventories from parsed counts on local heap; grow subtables; EIO/ENOMEM on failure.

[X] Fixed: time_win64.c:402: ano_sleep ≤1 ms bypassed scheduler path into pure busy-wait. test: pending / sleep contract
- odd-sibling-out
- fix (2026-07-27): every nonzero request enters coarse stage; do-while Sleep(0) before precision spin.

### Post-census test-side corrections

[X] Fixed: threads_macos.c barrier waiter and tests/anotest_threads.c watchdog: bare spin starved CI; 30s wall watchdog mislabeled progress as deadlock. test: anoptic_threads
- unbounded-spin
- fix (2026-07-27): relax → yield → park; watchdog on 10s frozen tally. macOS headless 0.23s; TSan 1.64s.

[X] Fixed: tests/anotest_time.c granularity test: min elapsed treated as resolution; TSan call overhead failed sub-100 ns threshold. test: tests-tsan Nix package
- test false-positive
- fix (2026-07-27): GCD of observed tick deltas for quantization; keep advance/monotonicity checks.

### Non-bug follow-ups

Deliberately excluded from the bug tally.

- Monitors ledger: enumerateMonitors hardened; initWindow still re-queries GLFW.
- Bindless capacity: consumers latch refusal word; future checked face could return slot+capacity if release lands.
- PipelinePrototype: inline PipelineImplementation[3] would delete count/pointer pair; layout refactor, not a bug fix.
