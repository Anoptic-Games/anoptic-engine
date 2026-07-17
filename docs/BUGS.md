# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_audio.c:257 〜 buffer_register computes frames * channels * sizeof(float) in uint64 with no wrap guard, so frames ≥ 2^62 wraps bytes64 past 2^64 to a tiny value that passes the SIZE_MAX check; a near-empty block is allocated, the header keeps the huge frame count, and the call returns true instead of rejecting bad args 〜 any voice playing that buffer reads far out of bounds on the mixer thread 〜 test: anotest_audioguard

audio_wav.c:34 〜 wav_write has the same unchecked frames * channels * sizeof(float) product, so frames near 2^62 wraps dataBytes64 to a tiny value that slips under the RIFF 32-bit guard; a truncated WAV is written whose fact chunk claims the wrapped frame count and the call returns true instead of rejecting bad args 〜 a caller saving a capture gets silent success and a lying file 〜 test: anotest_wavguard

audio_win64.c:589 〜 dsound_render_loop's DSBSTATUS_BUFFERLOST recovery calls Restore then Play without rewriting or silencing the ring and without resetting writeCursor, so up to four blocks of undefined restored buffer contents play and the stale cursor keeps writing out of phase with the restarted play cursor 〜 confirmed in source by two passes; no deterministic trigger seam today (real dsound.dll, loss needs focus change under DSSCL_PRIORITY) 〜 test: pending

audio_linux.c:168 〜 alsa_stop joins mx->deviceThread before its !st deviceState guard at :170, while wasapi_stop, dsound_stop and pw_stop all guard first, two with the explicit comment that stop tolerates a failed start; latent 〜 today ano_audio.c calls stop() only on a device whose start() returned true 〜 but any caller exercising the tolerance the sibling backends document joins a never-created thread handle, which is UB 〜 test: pending 〜 linux-only, unreachable via today's call order, no trigger seam

audio_fx.c:100 〜 fx_limiter's analysis window is one sample shorter than its delay line: winmax_init gets window == lookahead while the sample emitted each step is the one written lookahead steps ago (:391), and the wedge expires stamp + win <= n (dynamics.h:73), so a peak's stamp leaves the window on the exact push that emits it 〜 the "instant attack" gain takes one release step back toward 1.0 before the peak is multiplied out, breaching the ceiling by releaseCoef * (peak - ceiling) on every transient; at the public 1 ms release floor a 10x impulse under the default 0.92 ceiling emits 1.107, past digital full scale, and the sine-driven ceiling check in anotest_audiodsp cannot see it because adjacent sine samples are near-equal 〜 test: anotest_limiterguard

ano_audio.c:204 〜 ano_audio_shutdown discharges no adopted sample block: it joins the mixer, stops the device, and destroys both bridge rings and the module heap, but never walks mx->buffers or drains a ring, and adopted blocks live on the default mi heap (plain mi_malloc at :260), not the module heap the teardown frees 〜 so LIVE owned blocks, ACMD_BUFFER_REGISTER blocks still queued in the command ring, and un-polled AEVT_BUFFER_RETIRED blocks all orphan permanently (the producer can never poll a destroyed world to get them back), breaching the lossless-block invariant (audio_internal.h:9) and the block-rides-home promise (anoptic_audio.h:326) on every world torn down with resident audio 〜 test: anotest_audioshutguard

### Interlink / Composition bugs 



## Collections

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_win64.c:137 〜 ano_fs_write's loop has no forward-progress check, so a WriteFile returning TRUE with 0 bytes written advances neither cursor nor remaining and the writer spins forever instead of returning -1; MSDN never promises written > 0 on success for synchronous file handles (network redirectors and filter drivers are the plausible producers), and the linux twin is immune by POSIX (write of nbyte > 0 on a regular file cannot return 0) 〜 test: pending 〜 no seam to make the real WriteFile return a zero-progress TRUE

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

log_core.c:817 〜 the drain batch g_batch is sized ring bytes + 16 per record on the claim that a record's rendered text fits its ring footprint, but a deferred record renders at its format width, not its stored size 〜 "%*d" width 4000 holds one 64-byte ring line yet emits ~4016 batch bytes, so one pass over a ~164-record backlog walks blen past g_batchCap (the per-record prefix memcpy and newline are unchecked), the size_t room subtraction underflows and unbounds every later record into a multi-MB heap overwrite on the draining thread 〜 test: anotest_logflood

### Interlink / Composition bugs 



## Math

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

memalign_win64.c:13 〜 ano_aligned_malloc forwards straight to mi_malloc_aligned with no zero guard, so size 0 returns a live non-NULL block against the header's "NULL if size or alignment is 0" contract at anoptic_memory.h:47 (mimalloc hands back a unique pointer for size 0); the linux/macos twins at memalign_linux.c:13 / memalign_macos.c:13 are identical, and the alignment-0 half of the contract holds only by mimalloc's own power-of-two refusal 〜 test: anotest_memguard

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_meshoptimizer.c:282 〜 ano_build_meshlets clamps max_vertices/max_triangles only from above and skips the max_vertices < 3 / max_triangles < 1 rejection its sizing twin ano_build_meshlets_bound enforces at :85, so build(indices {0,1,2}, max_vertices 2) returns 1 meshlet with vertex_count 3 where bound() returned 0 〜 a caller sizing buffers from bound() per the header contract hands build zero-length arrays and takes a heap overwrite, and the emitted meshlet breaks the max_vertices promise the meshlet_vertices layout is built on 〜 test: anotest_meshguard

ano_meshoptimizer.c:955 〜 ano_simplify_ex runs the link/tetra collapse-validity exclusion only when the growth guards are on (maxEdge2 != FLT_MAX), and ano_simplify calls it with edge_len_factor 0, so the base public API executes topologically illegal collapses its own guards-on twin rejects: one rim collapse on a 3-triangle cone fan rewrites a surviving triangle onto the remaining rim pair and the output is the same face twice 〜 two coincident opposite-wound triangles, a zero-volume sack that z-fights and rides straight into LOD chains against the header's degenerate-dropping promise 〜 test: anotest_meshsimplifyguard

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

music_host.c:193 〜 ano_music_set_override("cadence_policy", v) refuses unknown names but accepts any value, casting (int8_t)v unchecked (the config seam memcpys cadencePolicies just as blind at :65), and policy_of returns the pin verbatim ahead of every other source (music_conductor.c:132), so any value outside {-1..2} indexes the [3]-sized policy tables out of bounds behind guards that only exclude NONE 〜 CADENCE_TARGET[3] (music_harmony.c:119) reads PRE_CADENCE_FUNCTION's 'D' as cadence chord degree 68, ano_chord_symbol then derefs ROMAN[67] == NULL and the engine segfaults on the composing (audio) thread at the first phrase's cadence lookahead; DEGS (music_melody.c:517) and ARRIVE/APPROACH/PNAME (music_verify.c:752) share the one-sided guard, and the raw value is republished to gameplay in AnoMusicMeaning.cadencePolicy against its documented AnoCadencePolicy contract 〜 test: anotest_musiccadenceguard

music_host.c:194 〜 ano_music_set_override("mode", v) accepts any value, casting (int)v unchecked, and the config seam copies cfg.mode just as blind (:58); the conductor pins either verbatim 〜 the only refusal is exactly ANO_MODE_NONE (music_conductor.c:882-886 per phrase with mapper, :728-731 at init) 〜 and the (uint8_t) casts at :734/:892 launder negatives into 0..255 instead of rejecting, so every bar hands the raw mode to ano_mode_intervals whose `return table[mode]` (music_theory.c:58) indexes a static [7][7] table unchecked: mode 99 gives ano_scale_pcs a pointer ~650 bytes past the 49-byte table, -3 (laundered to 253) ~1.77 KB past, read seven-wide on the composing (audio) thread every bar, and the same raw value is republished to gameplay in AnoMusicMeaning.mode against its documented AnoMode contract (anoptic_music.h:451) and rides AEVT_MUSIC_BAR engine-wide (the HUD consumer at main.c:614 survives only because % 7 of the laundered non-negative int lands in range) 〜 test: anotest_musicmodeguard

music_host.c:66 〜 the config seam copies cadencePolicyCount unvalidated while clamping its sibling motifLibraryCount to ANO_SIG_MAX a few lines below (:101), and ano_engine_init adopts it wholesale (music_conductor.c:702), so any count > 8 sends policy_of's explicit-cycle arm past the int8_t cadencePolicies[8] array (music_conductor.c:147): count 12 makes phrase 8 read the count field's own low byte (12) as its cadence policy, an out-of-enum value that rides the :193 entry's one-sided != NONE guard chain (CADENCE_TARGET at music_harmony.c:119, DEGS at music_melody.c:517, ARRIVE/APPROACH/PNAME at music_verify.c:752) and is republished to gameplay in AnoMusicMeaning.cadencePolicy against its AnoCadencePolicy contract (anoptic_music.h:454), while counts >= 2^31 flip the (int) cast negative so phrase % count returns the raw phrase, an index growing without bound as the piece runs; in practice the count-12 walk segfaults at phrase 8's cadence lookahead (ROMAN NULL deref, music_theory.c:238 via :261) on the composing (audio) thread 〜 test: anotest_musiccadcountguard

### Implementation bugs

music_arp.c:102 〜 ano_generate_arp emits one event per meter slot into AnoArpResult.events[ANO_METER_MAX_SLOTS] with no bound, but ano_meter_slots exceeds the 32-slot cap for any meter past 8 quarters (9/4 is 36, 12/4 is 48) and ano_music_create accepts such meters unvalidated, so at noteDensity > 0.65 the loop writes the tail past the stack array inside ano_engine_advance_bar 〜 the 33rd write lands on eventCount itself, recycling the tail into events[0..2] and silently truncating the bar to 3 events on top of the stack stomp 〜 the metric-weights twin at music_ir.c:63 clamps exactly this and the arp does not 〜 test: anotest_musicarpguard

music_perc.c:121 〜 ano_generate_perc's compound branch writes the grouped-kick pickup kick[slots - 2] into bool kick[ANO_METER_MAX_SLOTS] at noteDensity > 0.75, but ano_meter_slots is 36 for 9/4 (48 for 12/4), so the write lands past the stack array and the sorted-set readback then reads kick[32..35] off the frame as phantom kicks; the same 32-wide shape breaks the hat lane 〜 AnoGroove.hatDrops is a u32 slot bitmask and the shifts at :79 (1u << s) and :154 (hatDrops >> s) are UB for slot ≥ 32, wrapping on x86/arm so mask bits for slots 0..3 silently drop the hats at 32..35 〜 and the widest legal bars overrun the emit buffers outright (9/4 can push ~55 hits into AnoPercResult.events[48], 12/4 ~68 into the Hit hits[64] scratch); reached like the arp twin 〜 music_host.c:56 copies the config meter unvalidated 〜 test: anotest_percmeterguard (pins the :154 wrap deterministically; the :121 stack write executes in the same failing call)

### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

shadow_casters.c:97 〜 register_static_shadow indexes shadowTypeUsed[3] with the raw bridge light type ((uint32_t)cmd.light.type at apply.c:132; nothing between anoptic_render.h's documented RenderLightType {0,1,2} and here validates it, ano_render_submit is a bare ring push), so an out-of-enum type reads struct memory past the array as its budget counter and, when that alias holds 0, passes the guard and applies the += 1 at :114 out of bounds 〜 type 7 lands exactly on rtSingleFreeCount and type 10 on rtPointFreeCount (the runtime frustum free-lists sit directly after the array), so a drained pool resurrects to count 1 and the next runtime caster pops a frustum block already owned by a live light; larger types write arbitrarily far past RendererState, and the same raw type rides LightData.type to the shaders while the budget pick falls through to SPOT 〜 test: anotest_shadowtypeguard 〜 device-free, builds where anoptic_render does

### Implementation bugs

texture.c:435 〜 createTextureImage's mip-0 upload passes texture.texWidth as BOTH extents of the buffer->image copy (its sibling createTextureImageFromPixels at :377 passes width, height), so every non-square texture file uploads wrong: landscape (w > h) submits a copy region w rows tall against an h-row image and reads w*(w-h)*4 bytes past the w*h*4 staging buffer (VUID breach on both the image bound and the buffer bound, device-lost territory), portrait (w < h) uploads only w rows and leaves the rest of mip 0 undefined for generateMipmaps to smear down the whole chain; reached from every glTF texture upload (ano_GltfParser.c:270) 〜 test: anotest_texuploadguard

device.c:663 〜 createLogicalDevice's transfer-queue fetch is armed by indices->computePresent 〜 a copy-paste of the compute block directly above it 〜 instead of transferPresent, and the queue-create loop at :537 feeds uniqueQueueFamilies from transferFamily with no presence check at all, so on a device whose families advertise GRAPHICS|COMPUTE without TRANSFER_BIT (spec-legal: the transfer capability graphics/compute imply is optional to report) findQueueFamilies hands back transferPresent=false / transferFamily=UINT32_MAX and createLogicalDevice both creates a queue on family UINT32_MAX inside vkCreateDevice and calls vkGetDeviceQueue(UINT32_MAX) 〜 a VUID breach twice over at engine boot; the converse arm (compute absent, transfer present) would skip the fetch and leave ctx->transferQueue NULL for every geometry_pool_upload submit (geometry.c:339), unreachable today only because graphics implies a compute-capable family 〜 test: anotest_transferqueueguard

swapchain.c:428 〜 createImageView's failure arm only logs and falls through to `return imageView`, a local the failed vkCreateImageView leaves with undefined contents (Vulkan output params are undefined on error), so the caller receives indeterminate stack bytes as a live VkImageView and has no failure channel at all; all ten call sites store it directly (swapchain.c:439, attachments.c:86/:108/:153/:199/:210/:226/:238, text_raster.c:806, texture.c:457), createImageViews returns true unconditionally (:441) so recreateSwapChain's views==NULL guard (:365) tests only the malloc, cleanupSwapChain (:187) then hands the garbage to vkDestroyImageView 〜 an invalid-handle VUID breach 〜 and the VK_NULL_HANDLE guards on the attachments-side teardown fields compare against garbage and pass, so every teardown after a failed view create is UB on real drivers 〜 test: anotest_imageviewguard

swapchain.c:110 〜 initSwapChain consumes the by-value SwapChainSupportDetails querySwapChainSupport returns 〜 two arrays calloc'd at :33/:38 (formats, presentModes) 〜 and returns on both arms (:157 failure, :178 success) without freeing either; the struct is a discarded local and no other backend code references the pointers (the only mentions in the tree are the alloc sites, the two choose* consumers, and the struct declaration), so the boot call (vulkanMaster.c:449) orphans both blocks and every resize recreation (recreateSwapChain, swapchain.c:353) orphans two more 〜 formatCount*8 + presentModesCount*4 bytes accrue per recreation without bound under window-drag resize storms 〜 test: anotest_swapleakguard

commands.c:201 〜 stagingTransfer's copy-failure arm is dead code: copyBuffer returns true unconditionally (:267) because the single-time-command pair beneath it discards every VkResult it sees (vkAllocateCommandBuffers :222, vkBeginCommandBuffer :228, vkEndCommandBuffer :235, vkCreateFence :240, vkQueueSubmit :247, vkWaitForFences :248), so a copy that never executed 〜 device-lost or OOM at submit, or a failed command-buffer allocation whose undefined out-param handle then rides vkBeginCommandBuffer/vkCmdCopyBuffer/vkQueueSubmit as invalid-handle VUID breaches 〜 reports success to the text-bake uploads (text_raster.c:639/:644) whose ok-chain exists to hear exactly this, and the overlay then draws curve/glyph buffers the data never reached; the arm's own body is wrong for the day a fix arms it, returning at :204 without the :208 vkDestroyBuffer and leaking the transient staging buffer 〜 test: anotest_stagingcopyguard

texture.c:426 〜 createTextureImage acquires its staging buffer at :415 and discharges it only in the success epilogue 〜 :445 hands it to outStagingBuffer or destroys it 〜 so both failure returns (:426 image creation, :432 layout transition) orphan the live VkBuffer, and no caller can recover it: *outStagingBuffer is written only at :445, so the glTF loop's calloc'd slot stays VK_NULL_HANDLE while stagingCount++ has already consumed it (ano_GltfParser.c:274) and the destroy loop at :296 no-ops on the hole 〜 one buffer object bound into the shared staging arena orphans per failed texture load, the reachable arm being createImage refusing under device memory pressure (gpu_alloc's 256 MiB block grab or vkCreateImage itself), exactly the pressure a loading spree produces; the sibling createTextureImageFromPixels orphans identically on its :368/:374/:382 arms (reached with NULL out from scene_buffers.c:479), and the :432 arm additionally strands the just-created textureImage plus its texture-arena allocation since the caller ignores the out-params on failure 〜 test: anotest_texstagingguard

### Interlink / Composition bugs 

ano_render_bridge.c:204 〜 ano_render_ui_set's validator ui_prim_valid bounds every block-local reference the header enumerates 〜 clipRef, paintRef, GLYPHS windows, the full PATH curve walk 〜 but never a paint's stop window [stopFirst, +stopCount) against ui->stopCount, and no later layer recovers: apply.c:325 adopts the block blind, ui_compose rebases pa.stopFirst += ns unchecked (ui_raster.c:123), and the GPU evaluator's only guard is stopCount != 0 (uicoverage.glsl:176) though its own comment claims the out-of-range window "fails CLOSED. Mirrors ano_ui_ref_paint" 〜 the mirror's range check exists solely in the CPU ref (ui_raster_ref.c:229), so a hand-built block with stopFirst past its stop table sails through the seam whose stated purpose is that hand-built blocks cannot run the GPU walker past a stream, and ui_stop_color indexes the stop SSBO out of bounds on every painted pixel (a large stopFirst reads past the whole uiFrameBuffer binding 〜 robustness-dependent garbage or device-lost); the lone ref guard itself wraps in uint32, so stopFirst near UINT32_MAX slips even it 〜 test: anotest_uistopguard



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_ops.c:86 〜 anostr_join sizes its result as sep.len * (count-1) plus part lengths in uint64, so sep.len near UINT32_MAX with count ≈ 2^32+3 wraps the product to ≤ UINT32_MAX and slips the :89 oversize guard that exists to reject exactly this; the write loop then streams ~2^64 bytes into the ~4 GiB allocation 〜 the cheapest trigger needs a 64 GiB parts array (2^32+3 16-byte anostr_t entries), far past any engine caller but constructible on a large-memory host 〜 test: pending 〜 needs a 64 GiB parts array

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

ano_synth.c:227 〜 ano_synth_score_tempo validates only bpm and hands straight to clock_add, which unconditionally dereferences the last anchor (:157), but the anchor array exists only after score_begin/live_begin 〜 on a fresh synth anchors is NULL and anchorMask 0, so the documented-order misuse (anoptic_synth.h:80) that every sibling entry point reports with false (score_bar/score_event via their zero-cap checks, score_end via barCount, live_bar via !live, score_frames/time_at via scoreReady) is a deterministic NULL deref on the logic thread; the same unguarded path called after score_end silently mutates the tempo map the notes were already frame-stamped against 〜 test: anotest_synthtempoguard

ano_synth.c:202 〜 score_begin sizes the anchor array as tempoCount + 1u for the implicit beat-0 seed, so tempoCount UINT32_MAX wraps anchorCap to 0 before the sizing 〜 no huge allocation needed 〜 and the zero-count mi_heap_calloc returns a non-NULL minimal block (malloc(0) semantics) that passes the :211 NULL guard, so :213 writes the 24-byte seed anchor s->anchors[0] out of bounds and begin returns true against the header's begin-counts-size-allocations contract (anoptic_synth.h:81); the surviving anchorCap 0 then makes clock_add's :164 fullness check reject the very first tempo point the begin just promised room for, a deterministic functional proxy even where the heap overwrite lands silently 〜 test: anotest_synthbeginguard

### Implementation bugs

ano_synth.c:246 〜 the score_event guard rejects velocity == 0 but never the documented upper bounds (velocity 1..127, pitch 0..127 per AnoNoteEvent), so an event with velocity 200 or pitch 130 returns true and enters the schedule; the voice then renders at powf(v/127, 1.5) ≈ 2x the contract's amplitude ceiling, and merge_ties keys chains on pitch & 0x7F so an out-of-range pitch aliases a different in-range pitch's tie chain and silently merges two different-pitch notes into one 〜 the live twin at :429/:431 has the same one-sided filter 〜 test: anotest_synthguard

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

time_linux.c:132 〜 ano_sleep's failure path perrors and returns errno, but clock_nanosleep reports errors in its return value without setting errno, so a real failure returns stale errno 〜 possibly 0, i.e. success 〜 instead of the status the loop already holds in sleepStatus; latent, the non-EINTR path needs a kernel-level failure today's argument conversion cannot produce 〜 test: pending 〜 linux-only and needs a clock_nanosleep failure injection seam

time_win64.c:310 〜 ano_sleep computes target_ns = us * 1000 in uint64, so us > UINT64_MAX/1000 (a ~585-year request) wraps target_ns near zero and the call returns success almost immediately against the header's "sleep for us microseconds"; the macos twin has the same wrap at time_macos.c:168, while time_linux.c splits us into tv_sec/tv_nsec and is immune 〜 test: pending 〜 a correct ~585-year sleep cannot be awaited by a test

time_win64.c:337 〜 the no-timer fallback casts coarse_ns/1e6 to DWORD, so a ≥49.7-day sleep truncates its coarse stage and the spin tail then busywaits the missing weeks at 100% core instead of yielding, and a request landing exactly on 4294967295 ms becomes Sleep(INFINITE), a permanent hang; needs the hi-res waitable timer unavailable or SetWaitableTimer failing plus a multi-week argument 〜 test: pending 〜 no timer-failure seam and the correct behavior is a weeks-long wait

time_win64.c:252 〜 ano_busywait's loop guard tests startTime != 0 && endTime != 0 while the module's clock error sentinel is UINT64_MAX (ano_sleep at :312 checks the right one), so the guard is dead: a persistently failing clock returns UINT64_MAX twice, the delta stays 0 and busywait spins forever instead of erroring, and a legitimate 0 timestamp ends the wait early as success; latent 〜 QPC cannot fail on XP+ per the comment at :40 〜 and the linux/macos busywaits carry no sentinel check at all 〜 test: pending 〜 no clock-failure injection seam

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ui_path.c:99 〜 ano_ui_path_fill guards its quad budget (qn at :95/:122/:139) but never the contour counter (cn), so every MOVE writes cstart[cn++] into the fixed 513-entry stack array cstart[UI_PATH_MAX_QUADS + 1] unchecked and the :151 seal adds one more; a path with more than 512 empty contours 〜 legal input the contract promises ANO_UI_REF_NONE for ("the path is empty") 〜 sprays cstart past its end over the live quad buffer q[] sitting just above it, and the emit pass reads the corrupted geometry back out and faults 〜 test: anotest_uipathguard

ui_build.c:236 〜 paint_push's fullness guard adds b->stopCount + stopCount in uint32, so once any stops are resident a stopCount near UINT32_MAX wraps the sum under stopCap, the guard passes, and the :239 copy loop writes ~2^32 32-byte stops past the caller's array 〜 reached from the public ano_ui_paint_linear/radial/conic and a direct breach of the header's "full array -> ANO_UI_REF_NONE, no mutation" promise at anoptic_ui.h:121; absurd-argument territory, but the guard is defeated rather than the input rejected 〜 test: anotest_uipaintguard

ui_tiles.c:66 〜 ano_ui_tile_build computes nTiles = tilesX * tilesY and guards nTiles + 1 > offsetsCap in uint32, so 65536 x 65536 wraps nTiles to 0, the cap guard passes, *ok stays true for a 2^32-tile grid the buffers cannot hold, and any prim in the scene scatters pass-1 counts into offsets[] far past offsetsCap; tilesX = UINT32_MAX with tilesY = 1 wraps nTiles+1 to 0 instead and the :73 zero-fill writes 2^32 offsets out of bounds 〜 absurd tile counts, but "*ok false if a cap is too small" is exactly the promise the wrap defeats 〜 test: anotest_uitileguard

ui_raster_ref.c:229 〜 ano_ui_ref_paint's stop-window reject arm computes pa->stopFirst + pa->stopCount in uint32, so stopFirst near UINT32_MAX wraps the sum under s->stopCount, the guard passes, and ui_stop_color reads s->stops[stopFirst] ~137 GB past the table on its first comparison 〜 a direct breach of the header's "Out-of-range ref fails CLOSED (transparent)" promise (anoptic_ui.h:275) in the CPU mirror the GPU evaluator cites as its fail-closed reference, and the exact wrap shape the bridge entry (ano_render_bridge.c:204) shows no upstream layer catches, so the last line of defense crashes on the block it exists to reject; the plain non-wrapping out-of-range window fails closed correctly 〜 test: anotest_uirefstopguard

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 

main.c:391 〜 music_world_start's boot handshake spins ano_audio_acquire_telemetry at most 200 times over ~1 s (:389) and then seeds ano_synth_transport_start((t.blockIndex + 8u) * t.blockFrames) unconditionally, but the acquire contract returns false with *out untouched until the mixer's first publish (anoptic_audio.h:375; audio_bridge.h:144 bails before the copy while the version is 0), so exhausting the retries is indistinguishable from success and the AnoAudioTelemetry t declared at :388 is consumed uninitialized 〜 the transport start frame is stack garbage: a huge value parks the "few blocks ahead of playhead" start effectively forever and the session runs silent, a small one starts the score behind the playhead as a genLate storm 〜 the telemetry readout at :917 and anotest_audiotone's wait_telemetry both check this same call's return, only the boot path drops the false arm; reached when the mixer thread gets no cycles for >1 s after a successful ano_audio_init (starvation on that scale is documented reality on the 2-core sanitizer CI runners) 〜 test: anotest_boottelemetryguard (tentative) 〜 compiles the real main.c TU and forces the false arm with a contract-faithful acquire shim at the seam; the field trigger itself is OS starvation of a live mixer, not reachable deterministically
