# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.

## Tags

Each entry carries one or more root-cause tags on the bullet line(s) beneath it, so the census reads by shape as well as by module. Three root causes dominate the board; the rest are smaller shared shapes or 1:1 singletons.

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


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_audio.c:257 〜 buffer_register computes frames * channels * sizeof(float) in uint64 with no wrap guard, so frames ≥ 2^62 wraps bytes64 past 2^64 to a tiny value that passes the SIZE_MAX check; a near-empty block is allocated, the header keeps the huge frame count, and the call returns true instead of rejecting bad args 〜 any voice playing that buffer reads far out of bounds on the mixer thread 〜 test: anotest_audioguard
- checked-arithmetic

audio_wav.c:34 〜 wav_write has the same unchecked frames * channels * sizeof(float) product, so frames near 2^62 wraps dataBytes64 to a tiny value that slips under the RIFF 32-bit guard; a truncated WAV is written whose fact chunk claims the wrapped frame count and the call returns true instead of rejecting bad args 〜 a caller saving a capture gets silent success and a lying file 〜 test: anotest_wavguard
- checked-arithmetic

audio_win64.c:589 〜 dsound_render_loop's DSBSTATUS_BUFFERLOST recovery calls Restore then Play without rewriting or silencing the ring and without resetting writeCursor, so up to four blocks of undefined restored buffer contents play and the stale cursor keeps writing out of phase with the restarted play cursor 〜 confirmed in source by two passes; no deterministic trigger seam today (real dsound.dll, loss needs focus change under DSSCL_PRIORITY) 〜 test: pending
- recovery-desync

audio_linux.c:168 〜 alsa_stop joins mx->deviceThread before its !st deviceState guard at :170, while wasapi_stop, dsound_stop and pw_stop all guard first, two with the explicit comment that stop tolerates a failed start; latent 〜 today ano_audio.c calls stop() only on a device whose start() returned true 〜 but any caller exercising the tolerance the sibling backends document joins a never-created thread handle, which is UB 〜 test: pending 〜 linux-only, unreachable via today's call order, no trigger seam
- odd-sibling-out

audio_fx.c:100 〜 fx_limiter's analysis window is one sample shorter than its delay line: winmax_init gets window == lookahead while the sample emitted each step is the one written lookahead steps ago (:391), and the wedge expires stamp + win <= n (dynamics.h:73), so a peak's stamp leaves the window on the exact push that emits it 〜 the "instant attack" gain takes one release step back toward 1.0 before the peak is multiplied out, breaching the ceiling by releaseCoef * (peak - ceiling) on every transient; at the public 1 ms release floor a 10x impulse under the default 0.92 ceiling emits 1.107, past digital full scale, and the sine-driven ceiling check in anotest_audiodsp cannot see it because adjacent sine samples are near-equal 〜 test: anotest_limiterguard
- lookahead-off-by-one

ano_audio.c:204 〜 ano_audio_shutdown discharges no adopted sample block: it joins the mixer, stops the device, and destroys both bridge rings and the module heap, but never walks mx->buffers or drains a ring, and adopted blocks live on the default mi heap (plain mi_malloc at :260), not the module heap the teardown frees 〜 so LIVE owned blocks, ACMD_BUFFER_REGISTER blocks still queued in the command ring, and un-polled AEVT_BUFFER_RETIRED blocks all orphan permanently (the producer can never poll a destroyed world to get them back), breaching the lossless-block invariant (audio_internal.h:9) and the block-rides-home promise (anoptic_audio.h:326) on every world torn down with resident audio 〜 test: anotest_audioshutguard
- ownership-leak

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 〜 no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_linux.c:65 〜 ano_fs_resolvepath accepts mkdir's EEXIST as success without checking the existing entry is a directory, so a regular file or dangling symlink squatting ~/.anoptic returns a non-empty path nothing can be created under; the macos (:69) and win64 (:61) twins and the fs_mkdir primitive (:82) behind ano_fs_logpath share the one-sided filter 〜 the header's "Non-empty result is ready to write into" promise breaks: every save/config open under the returned path fails ENOTDIR while the resolver reports success, and a file named logs beside the exe kills the log and crash-log directory the same silent way 〜 test: anotest_fsguard
- seam-validation

filesystem_win64.c:137 〜 ano_fs_write's loop has no forward-progress check, so a WriteFile returning TRUE with 0 bytes written advances neither cursor nor remaining and the writer spins forever instead of returning -1; MSDN never promises written > 0 on success for synchronous file handles (network redirectors and filter drivers are the plausible producers), and the linux twin is immune by POSIX (write of nbyte > 0 on a regular file cannot return 0) 〜 test: pending 〜 no seam to make the real WriteFile return a zero-progress TRUE
- unbounded-spin

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

log_core.c:205 〜 the deferred capture stores the caller's sourceFile as a raw pointer in the ring blob and format_deferred dereferences it at drain time (:268/:277, batch drain :555), but the header's lifetime contract is one-sided: anoptic_log.h:53 demands "printFormat MUST be a string literal" while sourceFile 〜 the parameter beside it, same trust level 〜 carries no lifetime requirement at all, so a caller passing a stack or heap path through the documented entry points ano_log_write/ano_log_vwrite (whose own comment invites wrappers, exactly where dynamic names arise) gets a dangling deref on the drain thread 〜 strnlen then memcpy of up to 256 bytes from freed or reused memory into the log 〜 or silently logs whatever the buffer holds at drain instead of at call; the implementation's own %s arm proves the intended rule by deep-copying every caller-owned string at capture (:247-:256), and sourceFile is the one string that misses it (the eager fallback at :180 copies it correctly) 〜 test: anotest_logsrcguard
- dangling-capture

### Implementation bugs

log_core.c:817 〜 the drain batch g_batch is sized ring bytes + 16 per record on the claim that a record's rendered text fits its ring footprint, but a deferred record renders at its format width, not its stored size 〜 "%*d" width 4000 holds one 64-byte ring line yet emits ~4016 batch bytes, so one pass over a ~164-record backlog walks blen past g_batchCap (the per-record prefix memcpy and newline are unchecked), the size_t room subtraction underflows and unbounds every later record into a multi-MB heap overwrite on the draining thread 〜 test: anotest_logflood
- size-mismatch

### Interlink / Composition bugs 



## Math

(anoptic_math.h is types-only as of 2026-07-17 〜 mat4/Vector2/3/4 PODs, no ops, no src/ module; ops live in render/vertex.h. Composition audits belong to the render_bridge and vulkan_backend interlink edges.)

### Interface-level bugs and logic inefficiencies

anoptic_math.h:21 〜 the header declares mat4/Vector2/3/4 the canonical std430 types "across render, ECS, and the logic<->render bridge", but `typedef float mat4[4][4]` and the `struct { float v[N]; }` vectors all carry `_Alignof == 4` (measured, clang 22 -std=c23: mat4 size 64 align 4, Vector4 size 16 align 4), while std430/std140 require a 16-byte alignment for both 〜 so the type system enforces nothing and every C struct mirroring a GLSL block agrees with it only by accident of member order; two already disagree: RenderEntity (structs.h:154) puts three uint32_t ahead of its mat4, landing `transform` at offset 12 (sizeof 76) where std430 demands 16 (sizeof 80), and DisplayState (render_bridge.h:47) lands its mat4 at offset 4 〜 latent only because neither is uploaded as a struct today (the GPU reads a separate 8-byte EntityInfo plus a mat4 array at offset 0, and the CullView instances that ARE uploaded put their mat4 first inside mapped device memory), so the day either one is memcpy'd into an SSBO the shader reads every field 4 bytes off; the same missing alignment is also a per-frame cost on the hottest stream 〜 a 64-byte record that may legally start off a cache line makes every element of a CPU-side transform array straddle two lines, doubling line traffic on the million-entity sweep 〜 the fix is `alignas(16)` on the types (an ABI change: it repacks RenderEntity and DisplayState), not a comment 〜 test: pending 〜 a `_Static_assert(_Alignof(mat4) >= 16)` guard TU pins it at compile time, but it cannot fail at runtime and so cannot be a CTest failure until the types change
- alignment-contract-gap

(The former lead "anoptic_math.h:16 vs docs/math-conventions.md 〜 one of the two contracts is lying" is struck as of 2026-07-24: the doc was right and the header was wrong. `translate` writes the translation to `mat[3][0..2]`, `multiplyMat4` documents `temp[col][row] += a[k][row] * b[col][k]`, and `perspective` sets `matrix[2][3] = -1` 〜 all column-major, matching GLSL's mat4 so uploads need no transpose. The header comment now says so. No code changed.)

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

memalign_linux.c:13 / memalign_macos.c:13 / memalign_win64.c:13 〜 ano_aligned_malloc is a bare one-line forward to mi_malloc_aligned with no zero guard, so size 0 follows malloc's zero-size convention and returns a live non-NULL minimum-size block (mimalloc hands back a unique pointer) against the header's "NULL if size or alignment is 0" contract at anoptic_memory.h:47; only the alignment-0 half holds, via mimalloc's own power-of-two refusal, so the contract is half-implemented and the documented zero-size sentinel never fires 〜 a caller branching on NULL to reject a degenerate count*stride==0 request instead gets success plus a block it believes cannot exist, so its reject path is unreachable and each such call leaks one block under "NULL means nothing was allocated" 〜 test: anotest_memguard
- unguarded-delegation

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_meshoptimizer.c:282 〜 ano_build_meshlets clamps max_vertices/max_triangles only from above and skips the max_vertices < 3 / max_triangles < 1 rejection its sizing twin ano_build_meshlets_bound enforces at :85, so build(indices {0,1,2}, max_vertices 2) returns 1 meshlet with vertex_count 3 where bound() returned 0 〜 a caller sizing buffers from bound() per the header contract hands build zero-length arrays and takes a heap overwrite, and the emitted meshlet breaks the max_vertices promise the meshlet_vertices layout is built on 〜 test: anotest_meshguard
- seam-validation

ano_meshoptimizer.c:955 〜 ano_simplify_ex runs the link/tetra collapse-validity exclusion only when the growth guards are on (maxEdge2 != FLT_MAX), and ano_simplify calls it with edge_len_factor 0, so the base public API executes topologically illegal collapses its own guards-on twin rejects: one rim collapse on a 3-triangle cone fan rewrites a surviving triangle onto the remaining rim pair and the output is the same face twice 〜 two coincident opposite-wound triangles, a zero-volume sack that z-fights and rides straight into LOD chains against the header's degenerate-dropping promise 〜 test: anotest_meshsimplifyguard
- feature-gated-check

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

music_host.c:193 〜 ano_music_set_override("cadence_policy", v) refuses unknown names but accepts any value, casting (int8_t)v unchecked (the config seam memcpys cadencePolicies just as blind at :65), and policy_of returns the pin verbatim ahead of every other source (music_conductor.c:132), so any value outside {-1..2} indexes the [3]-sized policy tables out of bounds behind guards that only exclude NONE 〜 CADENCE_TARGET[3] (music_harmony.c:119) reads PRE_CADENCE_FUNCTION's 'D' as cadence chord degree 68, ano_chord_symbol then derefs ROMAN[67] == NULL and the engine segfaults on the composing (audio) thread at the first phrase's cadence lookahead; DEGS (music_melody.c:517) and ARRIVE/APPROACH/PNAME (music_verify.c:752) share the one-sided guard, and the raw value is republished to gameplay in AnoMusicMeaning.cadencePolicy against its documented AnoCadencePolicy contract 〜 test: anotest_musiccadenceguard
- seam-validation

music_host.c:194 〜 ano_music_set_override("mode", v) accepts any value, casting (int)v unchecked, and the config seam copies cfg.mode just as blind (:58); the conductor pins either verbatim 〜 the only refusal is exactly ANO_MODE_NONE (music_conductor.c:882-886 per phrase with mapper, :728-731 at init) 〜 and the (uint8_t) casts at :734/:892 launder negatives into 0..255 instead of rejecting, so every bar hands the raw mode to ano_mode_intervals whose `return table[mode]` (music_theory.c:58) indexes a static [7][7] table unchecked: mode 99 gives ano_scale_pcs a pointer ~650 bytes past the 49-byte table, -3 (laundered to 253) ~1.77 KB past, read seven-wide on the composing (audio) thread every bar, and the same raw value is republished to gameplay in AnoMusicMeaning.mode against its documented AnoMode contract (anoptic_music.h:451) and rides AEVT_MUSIC_BAR engine-wide (the HUD consumer at main.c:614 survives only because % 7 of the laundered non-negative int lands in range) 〜 test: anotest_musicmodeguard
- seam-validation

music_host.c:66 〜 the config seam copies cadencePolicyCount unvalidated while clamping its sibling motifLibraryCount to ANO_SIG_MAX a few lines below (:101), and ano_engine_init adopts it wholesale (music_conductor.c:702), so any count > 8 sends policy_of's explicit-cycle arm past the int8_t cadencePolicies[8] array (music_conductor.c:147): count 12 makes phrase 8 read the count field's own low byte (12) as its cadence policy, an out-of-enum value that rides the :193 entry's one-sided != NONE guard chain (CADENCE_TARGET at music_harmony.c:119, DEGS at music_melody.c:517, ARRIVE/APPROACH/PNAME at music_verify.c:752) and is republished to gameplay in AnoMusicMeaning.cadencePolicy against its AnoCadencePolicy contract (anoptic_music.h:454), while counts >= 2^31 flip the (int) cast negative so phrase % count returns the raw phrase, an index growing without bound as the piece runs; in practice the count-12 walk segfaults at phrase 8's cadence lookahead (ROMAN NULL deref, music_theory.c:238 via :261) on the composing (audio) thread 〜 test: anotest_musiccadcountguard
- seam-validation

### Implementation bugs

music_arp.c:102 〜 ano_generate_arp emits one event per meter slot into AnoArpResult.events[ANO_METER_MAX_SLOTS] with no bound, but ano_meter_slots exceeds the 32-slot cap for any meter past 8 quarters (9/4 is 36, 12/4 is 48) and ano_music_create accepts such meters unvalidated, so at noteDensity > 0.65 the loop writes the tail past the stack array inside ano_engine_advance_bar 〜 the 33rd write lands on eventCount itself, recycling the tail into events[0..2] and silently truncating the bar to 3 events on top of the stack stomp 〜 the metric-weights twin at music_ir.c:63 clamps exactly this and the arp does not 〜 test: anotest_musicarpguard
- fixed-array-overflow

music_perc.c:121 〜 ano_generate_perc's compound branch writes the grouped-kick pickup kick[slots - 2] into bool kick[ANO_METER_MAX_SLOTS] at noteDensity > 0.75, but ano_meter_slots is 36 for 9/4 (48 for 12/4), so the write lands past the stack array and the sorted-set readback then reads kick[32..35] off the frame as phantom kicks; the same 32-wide shape breaks the hat lane 〜 AnoGroove.hatDrops is a u32 slot bitmask and the shifts at :79 (1u << s) and :154 (hatDrops >> s) are UB for slot ≥ 32, wrapping on x86/arm so mask bits for slots 0..3 silently drop the hats at 32..35 〜 and the widest legal bars overrun the emit buffers outright (9/4 can push ~55 hits into AnoPercResult.events[48], 12/4 ~68 into the Hit hits[64] scratch); reached like the arp twin 〜 music_host.c:56 copies the config meter unvalidated 〜 test: anotest_percmeterguard (pins the :154 wrap deterministically; the :121 stack write executes in the same failing call)
- fixed-array-overflow
- shift-ub

music_voicing.c:114 〜 ano_voice_chord builds its candidate table in a plain function-scope static (static Cand cands[256], 6 KiB, comment "single-threaded conductor context"), shared across every engine in the process, while the module's own hosting design (ANOPTIC_MUSICGEN.md seek: rebuild a second engine off-thread while the callback-hosted composer keeps advancing the live one) runs two engines through advance_bar -> generate_pad -> ano_voice_chord concurrently; the dedupe scan, cost pass, and final out[] copy read entries the other thread is rewriting, so a wrong pad voicing is selected and enters st->prevVoicing 〜 the anoptic_music.h:481 contract "Same config+seed+bar => byte-identical" breaks the moment any second engine composes concurrently: the off-thread seek snapshot is not the piece linear play would have produced (ACMD_MUSIC_SEEK adopts audibly different music, musicdrive's sample-identical-seek invariant fails), the live bar can sound a foreign chord, and TSan confirms the write-write race on ano_voice_chord.cands from both threads 〜 test: anotest_musicguard
- shared-mutable-state

### Interlink / Composition bugs 

music_arp.c:106 〜 the arp lane clamps velocity to 1..127 (:87) and then adds its +4 slot accent with no re-clamp (its own header admits "Accented slots add 4 with no re-clamp"), and both inward parameter paths hand it an unclamped velocityCenter: expand() copies the public uint8_t field raw into the internal int (music_host.c:81) and the "velocity_center" override stores and uses the raw double (music_host.c:183, music_conductor.c:599/607 〜 ACMD_MUSIC_OVERRIDE forwards game-supplied values verbatim at ano_synth.c:703), even though the outward bridge clamps velocityCenter to 0..127 (music_ir.c:109), declaring the very domain the inward side never enforces; every other lane clamps its final velocity (bass :62, pad :198, melody velocity_of, counter :253, perc :216, imitation :141) and the arp's default modifier chain is echo-only, which clamps echo copies but not originals, so any velocityCenter >= 140 makes every accented arp slot 〜 slot 0 of every bar, which the skip logic never masks 〜 emit velocity 128..131, violating AnoNoteEvent's documented 1..127 〜 the host copies event cores verbatim into AnoMusicBar (music_host.c:233), music_pump feeds them to ano_synth_live_bar whose one-sided guard (ano_synth.c:429, batch twin :246 〜 tallied Synth-side) stages them, and the voice renders amp = powf(131/127, 1.5) ≈ 1.048 (synth_voices.c:353), above the ceiling any contract-legal velocity can produce, on the downbeat of every bar the arp sounds 〜 in-tree traffic through a plain console override crosses the broken guard, so the music emitter defect and the synth acceptor defect compose end-to-end 〜 test: anotest_musicsynthguard
- seam-validation



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

shadow_casters.c:97 〜 register_static_shadow indexes shadowTypeUsed[3] with the raw bridge light type ((uint32_t)cmd.light.type at apply.c:132; nothing between anoptic_render.h's documented RenderLightType {0,1,2} and here validates it, ano_render_submit is a bare ring push), so an out-of-enum type reads struct memory past the array as its budget counter and, when that alias holds 0, passes the guard and applies the += 1 at :114 out of bounds 〜 type 7 lands exactly on rtSingleFreeCount and type 10 on rtPointFreeCount (the runtime frustum free-lists sit directly after the array), so a drained pool resurrects to count 1 and the next runtime caster pops a frustum block already owned by a live light; larger types write arbitrarily far past RendererState, and the same raw type rides LightData.type to the shaders while the budget pick falls through to SPOT 〜 test: anotest_shadowtypeguard 〜 device-free, builds where anoptic_render does
- seam-validation

render_slots.c:35 〜 logical_reserve covers render_id by growing to need = render_id + 1u, so the module's own sentinel 0xFFFFFFFF (ANO_RENDER_SLOT_UNMAPPED, render_slots.h:16) wraps need to 0, ensure_cap's need <= *cap arm reports success with nothing allocated (:19), the UNMAPPED-init loop runs zero times, and render_slots_alloc consumes a physical slot then stores it at logicalToSlot[0xFFFFFFFF] (:79) 〜 a wild write ~16 GiB past the map, or from a NULL map on a fresh table, on the render master thread, with the reverse map left holding UNMAPPED so the consumed slot is invisible to picking and can never be retired; nothing on the way in excludes the sentinel: render_id is the producer's namespace ("logical name", anoptic_render.h:399), ano_render_submit is a bare ring push (ano_render_bridge.c:102), apply.c forwards cmd.render_id and the RCMD_BULK_CREATE id array raw (:125/:167 〜 the bulk twin hits the same wrap at render_slots.c:92-:93), and the alloc contract's one invariant "render_id unmapped" (render_slots.h:66) is vacuously true of 0xFFFFFFFF, while the resolve twin guards this exact domain edge (:102) 〜 test: anotest_slotsentinelguard
- checked-arithmetic

apply.c:125 〜 the RCMD_CREATE arm forwards cmd.render_id into render_slots_alloc with no mapped-check on either side of the seam 〜 the alloc contract's one invariant "render_id unmapped" (render_slots.h:66) is enforced by nobody: alloc stores blind (render_slots.c:79-:80), and render_id is the producer's namespace arriving through a bare ring push 〜 so a duplicate CREATE of a live id mints a second physical slot, overwrites the forward map, and leaves the old slot's reverse entry holding the id: the old slot is stranded live forever (not free-listed, not quarantined, unreachable by resolve 〜 retire only ever finds the new slot via render_slots.c:125 and compact's peel loop at :181 never sees it) with its GPU entity entry still staged from the first CREATE so the cull pass draws a permanent undestroyable ghost, and render_slots_render_id_of(oldSlot) keeps answering the id (:120) so the pick readback (profiling.c:174) emits REVENT_PICK_RESULT naming a render_id the ECS may have retired and recycled for a different entity; the bulk twin overwrites identically at render_slots.c:93, and the light sibling on the same bridge guards exactly this shape (light_registry.c:89 refuses double-attach, apply.c:250 drops it) 〜 test: anotest_slotdupguard
- seam-validation

### Implementation bugs

texture.c:435 〜 createTextureImage's mip-0 upload passes texture.texWidth as BOTH extents of the buffer->image copy (its sibling createTextureImageFromPixels at :377 passes width, height), so every non-square texture file uploads wrong: landscape (w > h) submits a copy region w rows tall against an h-row image and reads w*(w-h)*4 bytes past the w*h*4 staging buffer (VUID breach on both the image bound and the buffer bound, device-lost territory), portrait (w < h) uploads only w rows and leaves the rest of mip 0 undefined for generateMipmaps to smear down the whole chain; reached from every glTF texture upload (ano_GltfParser.c:270) 〜 test: anotest_texuploadguard
- copy-paste-error

device.c:663 〜 createLogicalDevice's transfer-queue fetch is armed by indices->computePresent 〜 a copy-paste of the compute block directly above it 〜 instead of transferPresent, and the queue-create loop at :537 feeds uniqueQueueFamilies from transferFamily with no presence check at all, so on a device whose families advertise GRAPHICS|COMPUTE without TRANSFER_BIT (spec-legal: the transfer capability graphics/compute imply is optional to report) findQueueFamilies hands back transferPresent=false / transferFamily=UINT32_MAX and createLogicalDevice both creates a queue on family UINT32_MAX inside vkCreateDevice and calls vkGetDeviceQueue(UINT32_MAX) 〜 a VUID breach twice over at engine boot; the converse arm (compute absent, transfer present) would skip the fetch and leave ctx->transferQueue NULL for every geometry_pool_upload submit (geometry.c:339), unreachable today only because graphics implies a compute-capable family 〜 test: anotest_transferqueueguard
- copy-paste-error

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

record_views.c:302 〜 ano_record_composite places each aux-view inset at y = H - margin - (insetH + margin)*(idx + 1) + margin in uint32 arithmetic before the int32 cast, so swapchain height H ≤ 22 wraps y negative (H=22 gives -1, H=2 gives -14) and the value rides verbatim into vkCmdSetScissor offset.y 〜 a VUID-vkCmdSetScissor-x-00595 breach (scissor offsets must be ≥ 0, UB on real drivers) on every composited frame, with H ≤ 2 additionally emitting a zero-height inset viewport against VUID-VkViewport-height-01772; reachable because the composite runs unconditionally for ANO_VIEW_COUNT 2, imageExtent adopts the surface currentExtent 〜 the raw Win32 client size (swapchain.c:73/:167) 〜 and recreateSwapChain's only floor is the 0x0 wait loop (swapchain.c:340-:345), so while Win32's minimum tracking size protects the sibling x at :301 the client height drags freely into [1,22] 〜 test: anotest_insetscissorguard
- checked-arithmetic

gpu_alloc.c:12 〜 findMemoryType probes typeFilter & (1 << i) with i walking to memoryTypeCount - 1, and the legal domain is VK_MAX_MEMORY_TYPES == 32 with the count adopted raw from vkGetPhysicalDeviceMemoryProperties, so a device exposing all 32 memory types evaluates 1 << 31 on the probe of the last type 〜 a signed int shifted into its sign bit, UB under the C23 the build mandates (CMakeLists.txt:26; 6.5.7 kept the C17 "representable in the result type" wording, and this tree's own clang in -std=c23 traps "left shift of 1 by 31 places cannot be represented in type 'int'") 〜 device-gated in consequence: common codegen wraps to INT_MIN and the & happens to select bit 31 correctly, so the live arms are any sanitized build and an optimizer entitled to treat the i == 31 iteration as unreachable 〜 dropping exactly the probe of a device whose only matching type is index 31 into the UINT32_MAX fall-through; the twin at commands.c:173 duplicates the loop verbatim on every createDataBuffer/stagingTransfer type probe, so one fix must land twice 〜 test: anotest_memtypeshiftguard (real gpu_alloc.c TU compiled -fsanitize=shift so the abstract-machine UB is a deterministic halt; an uninstrumented build passes the functional proxy by the very wrap that keeps the bug latent)
- shift-ub

ano_GltfParser.c:30 〜 parseGltf goes from cgltf_parse_file/cgltf_load_buffers straight to per-element cgltf_accessor_read_float (:80) and cgltf_accessor_read_index (:96) without ever calling cgltf_validate, the library's only accessor-vs-bufferView and bufferView-vs-buffer byte-range gate; those helpers compute buffer->data + view->offset + accessor->offset + stride*index with no bounds check, and load_buffers only verifies the .bin/base64 payload reaches the declared byteLength, never the accessor math, so a file whose accessor count*stride (or view offset+size) overruns the loaded buffer walks off the end of the heap block 〜 a truncated or hostile asset is read out of bounds during init (a large claimed count reads gigabytes past the block and faults the loader), garbage vertices and out-of-range index values flow into geometry_pool_upload_chain and the GPU index buffer, and parseGltf returns a non-NULL "successfully parsed" asset instead of the NULL its own :25/:31 error paths establish for bad files 〜 test: anotest_gltfguard
- seam-validation

slot_upload.c:277 〜 ensureEntityCapacity recreates every entity-scaled buffer 〜 growBufferSet (:36) vkDestroyBuffer's each old per-frame live-transform SSBO after binding its replacement 〜 then re-points descriptors through updateUboDescriptorSets alone, but the shadowsetup compute set's binding 1 is that same transformBuffer.buffer[i] (descriptors.c:357/:376) and its only writer, updateShadowDescriptorSets, runs exactly once in initVulkan (vulkanMaster.c:666); nothing re-runs it on growth, and swapchain recreation re-runs only the Hi-Z and tonemap sets 〜 the first CREATE/BULK_CREATE that pushes slotHighWater past the current capacity (INITIAL_ENTITY_CAPACITY 10000) leaves frames[i].shadow.setupSet binding 1 referencing a destroyed VkBuffer, and PIPELINE_COMPUTE_SHADOWSETUP binds that set every frame (record.c:133, passes.c pass 2), so validation flags every subsequent dispatch and on a live device shadowsetup.comp reads whatever the bump allocator kept behind the freed handle: every shadow-frustum viewProj and the fragment-stage sampling-viewProj UBO derive from stale or garbage parent transforms, shadows permanently detach from their lights (or the device faults) after the first entity growth 〜 a boundary the demo scene never crosses 〜 test: anotest_vkguard
- missed-repoint

### Interlink / Composition bugs 

ano_render_bridge.c:204 〜 ano_render_ui_set's validator ui_prim_valid bounds every block-local reference the header enumerates 〜 clipRef, paintRef, GLYPHS windows, the full PATH curve walk 〜 but never a paint's stop window [stopFirst, +stopCount) against ui->stopCount, and no later layer recovers: apply.c:325 adopts the block blind, ui_compose rebases pa.stopFirst += ns unchecked (ui_raster.c:123), and the GPU evaluator's only guard is stopCount != 0 (uicoverage.glsl:176) though its own comment claims the out-of-range window "fails CLOSED. Mirrors ano_ui_ref_paint" 〜 the mirror's range check exists solely in the CPU ref (ui_raster_ref.c:229), so a hand-built block with stopFirst past its stop table sails through the seam whose stated purpose is that hand-built blocks cannot run the GPU walker past a stream, and ui_stop_color indexes the stop SSBO out of bounds on every painted pixel (a large stopFirst reads past the whole uiFrameBuffer binding 〜 robustness-dependent garbage or device-lost); the lone ref guard itself wraps in uint32, so stopFirst near UINT32_MAX slips even it 〜 test: anotest_uistopguard
- seam-validation

components.c:72 〜 ano_vk_register_texture is the only route a loaded texture's ownership takes into the teardown registry 〜 cleanupVulkan walks primitives.textureBuffers and nothing else destroys the loaded views/images (cleanup.c:64-:71) 〜 yet it returns void and its realloc-failure arm logs ANO_ERROR and drops the TextureData record (:76-:78), so the glTF caller cannot hear the refusal and proceeds to bindless-register and draw the view (ano_GltfParser.c:282-:288) whose VkImage/VkImageView/GpuAllocation now orphan permanently: still resident, still sampled every frame, unreachable at shutdown, one whole texture per refused growth under exactly the loading-spree memory pressure that makes realloc refuse; the mesh twin ano_vk_register_mesh (:44) shares the void-drop shape and has no production caller at all 〜 dead code 〜 test: anotest_texregisterguard
- ownership-leak

ano_GltfParser.c:277 〜 parseGltf hears createTextureImage's false and discharges nothing: the callee's post-create failure arms return false with the just-created VkImage and texture-arena allocation already written through the out-params into loadedImages[t]/loadedAllocs[t] (texture.c:429-:433 transition arm 〜 its strand half noted under texture.c:426 〜 plus the :446-:450 view arm that today cannot fire only because createTextureImageView swallows its own failure, the tallied swapchain.c:428 family, and the arm the texture.c:437 fix adds), yet the parser's failure route is textureLoaded[t] = false and continue (:276) 〜 adoption into the teardown registry is success-only (ano_vk_register_texture at :282, the sole route cleanupVulkan ever walks), no failure arm destroys the image or frees the allocation, and :619-:621 free the host arrays holding the only copies of the handles 〜 one whole device texture image plus arena allocation orphans permanently per failed load, armed by exactly the fixes the tallied callee entries demand, so repairing texture.c's arms without a parser-side discharge converts today's staging leak into a full texture leak 〜 test: anotest_gltftexleakguard
- ownership-leak

ano_render_bridge.c:92 〜 ano_render_bridge_destroy tears the commands ring down through ano_spsc_destroy (:51), which frees only ring->buffer, so every RenderCommand still enqueued is discarded without honoring bulk_owned: the mi_malloc'd render-owned copies the submit helpers packed and pointed cmd.text/cmd.ui/cmd.update/cmd.destroy at (ano_render_text_set :152, ano_render_ui_set :246, producer.c :56/:90) lose their last reference with the ring 〜 the producer relinquished them at push per the header's copy-at-submit/render-frees contract (anoptic_render.h:435), and every other drop path frees the copy (full-ring reject at submit, replace/registry-full/clear at adoption), only the in-ring-at-destroy window frees nothing 〜 main.c stops draining at its last drawFrame before setting g_logicShouldStop, then joins and calls unInitVulkan (vulkanMaster.c:89) with no drain between, so the final logic ticks' TEXT_SET/UI_SET blocks (per-tick HUD and music-panel submissions, up to ~384 KiB per text block) land exactly in that window and leak every shutdown; an embedder cycling initVulkan/unInitVulkan on device loss or level reload, or any bridge-owning harness, accumulates them unbounded 〜 test: anotest_bridgeguard
- ownership-leak



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_collate.c:75 〜 ce_push_rune consults the decomp table before the CE table, and the generated tables disagree about coverage: U+01EF (ǯ, ezh-caron, Skolt Sami) and U+0374 (Greek numeral sign) carry correct direct CE listings ([270B.020.02]+[0000.028.02] and [04F2.020.02] in ano_collate_tables.h) that are unreachable because their decomp entries redirect through U+0292 / U+02B9, both trimmed out of the CE table, so ce_push_cp falls to UCA implicit weights (primary 0xFBC0, the after-every-listed-script band); the uppercase sibling U+01EE decomposes through U+01B7, which the trim kept, so only the lowercase form is poisoned 〜 the case pair the module itself maps (anorune_to_lower(U+01EE) == U+01EF) splits across the whole collation space: anostr_collate puts Ǯ before あ but ǯ after it, anostr_eq_base(Ǯ, ǯ) is false while every healthy decomposing pair (Ǒ/ǒ) holds, anostr_sort places "Ǯ" in the Latin band right after z (prefix key 270B…) and "ǯ" past kana among the Han implicits (FBC0.8292…), and base-letter search cannot match ǯ words 〜 test: anotest_strguard
- table-coverage-gap

ano_strings_ops.c:86 〜 anostr_join sizes its result as sep.len * (count-1) plus part lengths in uint64, so sep.len near UINT32_MAX with count ≈ 2^32+3 wraps the product to ≤ UINT32_MAX and slips the :89 oversize guard that exists to reject exactly this; the write loop then streams ~2^64 bytes into the ~4 GiB allocation 〜 the cheapest trigger needs a 64 GiB parts array (2^32+3 16-byte anostr_t entries), far past any engine caller but constructible on a large-memory host 〜 test: pending 〜 needs a 64 GiB parts array
- checked-arithmetic

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

ano_synth.c:227 〜 ano_synth_score_tempo validates only bpm and hands straight to clock_add, which unconditionally dereferences the last anchor (:157), but the anchor array exists only after score_begin/live_begin 〜 on a fresh synth anchors is NULL and anchorMask 0, so the documented-order misuse (anoptic_synth.h:80) that every sibling entry point reports with false (score_bar/score_event via their zero-cap checks, score_end via barCount, live_bar via !live, score_frames/time_at via scoreReady) is a deterministic NULL deref on the logic thread; the same unguarded path called after score_end silently mutates the tempo map the notes were already frame-stamped against 〜 test: anotest_synthtempoguard
- seam-validation

ano_synth.c:202 〜 score_begin sizes the anchor array as tempoCount + 1u for the implicit beat-0 seed, so tempoCount UINT32_MAX wraps anchorCap to 0 before the sizing 〜 no huge allocation needed 〜 and the zero-count mi_heap_calloc returns a non-NULL minimal block (malloc(0) semantics) that passes the :211 NULL guard, so :213 writes the 24-byte seed anchor s->anchors[0] out of bounds and begin returns true against the header's begin-counts-size-allocations contract (anoptic_synth.h:81); the surviving anchorCap 0 then makes clock_add's :164 fullness check reject the very first tempo point the begin just promised room for, a deterministic functional proxy even where the heap overwrite lands silently 〜 test: anotest_synthbeginguard
- checked-arithmetic

### Implementation bugs

ano_synth.c:246 〜 the score_event guard rejects velocity == 0 but never the documented upper bounds (velocity 1..127, pitch 0..127 per AnoNoteEvent), so an event with velocity 200 or pitch 130 returns true and enters the schedule; the voice then renders at powf(v/127, 1.5) ≈ 2x the contract's amplitude ceiling, and merge_ties keys chains on pitch & 0x7F so an out-of-range pitch aliases a different in-range pitch's tie chain and silently merges two different-pitch notes into one 〜 the live twin at :429/:431 has the same one-sided filter 〜 test: anotest_synthguard
- seam-validation

### Interlink / Composition bugs 



## Text

### Interface-level bugs and logic inefficiencies

text_gpos.c:304 〜 ano_gpos_extract_kerns resolves subtable offsets with unchecked uint32 arithmetic 〜 the type-9 Extension unwrap does so += innerOff where innerOff is a raw 32-bit extensionOffset (:299), and the fmt-2 class matrix indexes off + 16 + (c1*c2n+c2)*rec with c1n/c2n up to 0xFFFF and rec up to 32 (:186) 〜 so a malformed table whose offset overflows uint32 lands the resolved offset back INSIDE the buffer at a different structure; every g16/g32 read then stays in bounds, the malformation is never caught, and the function returns 0 (success) against its header contract "Bounds-checked. Malformed -> nonzero with dense possibly partial. 0 = success including 'no kerns'." (text_internal.h:89) 〜 a caller feeding a corrupt or hostile GPOS cannot tell it from a well-formed table with no kerns, so the nonzero false arm it needs to fall back (legacy 'kern' table, warn, refuse) never fires and text lays out with silently wrong or dropped spacing 〜 pattern class A (unchecked add/multiply offset wrap) at a documented contract boundary 〜 test: anotest_gposwrapguard
- checked-arithmetic

text_raster_ref.c:92 〜 ano_text_window_sum fetches pts[g->pointOffset] unconditionally BEFORE the curveCount loop that owns the stream walk, and its contract states no curveCount > 0 precondition (text_internal.h:96 "Unclamped coverage sum ... Pure, any thread"; docs/text/font-render.md:444 drives it directly as the offline shader-compare harness) while the bake mints exactly the entry that breaks the fetch: a zero-curve glyph 〜 MISSING codepoint or blank outline like space, both documented output (anoptic_text.h:82 "Missing -> blank", :48 "0 = blank") 〜 keeps pointOffset = stream.count as assigned before load (text_bake.c:565) with nothing pushed after, so a zero-curve TAIL glyph has pointOffset == pointCount and the fetch reads one uint32 past the points blob, and an all-blank bake (stream empty, points NULL 〜 text_bake.c:639) NULL-derefs outright; the fetched value is then discarded (the loop never runs, the sum is 0.0 regardless), so the defect is the read itself 〜 silent on plain heap, a deterministic crash at a page edge, under ASan, or on the NULL stream 〜 and every in-tree consumer compensates with its own curveCount guard (text_raster_ref.c:122, text_shape.c:98, textcoverage.glsl:142, textraster.comp:55, textworld.vert:42), unanimity that proves the guard belongs inside the documented boundary and keeps the hole latent for the next direct caller 〜 pattern class D 〜 test: anotest_textblanksumguard
- seam-validation

text_shape.c:126 〜 layout_core sets the final-line step from runs[runCount-1].sizePx unconditionally and ano_text_measure_runs (:195) returns penY + that step, but anoptic_text.h:113 promises "byteCount 0 is a no-op"; a trailing byteCount-0 run is at once a no-op and runs[runCount-1], so its sizePx sets the measured height though it styles nothing, while a leading/middle no-op run stays transparent (skipped by the run-advance loop) 〜 a caller that appends an empty trailing style span (a per-span run list ending on a zero-width style/cursor) gets a text box sized to that empty span's sizePx, not the last rendered line's: measure_runs("AA", [{2,32px},{0,64px}]) reports height 64 where the unsplit [{2,32px}] reports 32 〜 test: anotest_textguard
- noop-not-honored

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

time_linux.c:132 〜 ano_sleep's failure path perrors and returns errno, but clock_nanosleep reports errors in its return value without setting errno, so a real failure returns stale errno 〜 possibly 0, i.e. success 〜 instead of the status the loop already holds in sleepStatus; latent, the non-EINTR path needs a kernel-level failure today's argument conversion cannot produce 〜 test: pending 〜 linux-only and needs a clock_nanosleep failure injection seam
- wrong-error-source

time_win64.c:148 〜 ano_timestamp_ticks in TSC mode returns raw __rdtsc with no re-anchor across power transitions: the TSC does not survive S3 sleep or S4 hibernate (the core power domain drops and firmware restarts the counter near zero 〜 the reason both Windows and Linux re-base their own TSC-derived clocks on resume), the invariant-TSC check at :55 only covers P/C/T states, the election at :130 is frozen for the process, and ano_ticks_to_ns is a pure function of the frozen cachedTscHz, so the first post-resume stamp lands below every pre-sleep stamp and ano_timestamp_raw/us/ms follow it backward; the QPC path the same function uses on non-invariant-TSC CPUs is immune (the kernel re-biases QPC at resume), as are linux CLOCK_MONOTONIC and darwin mach_absolute_time 〜 the header's monotonic promise (anoptic_time.h:20) breaks exactly on the machines that elected the fast path 〜 every u64 now-start delta held across a lid-close or hibernate wraps toward 2^64: log_core.c:395's anchor math stamps records ~146 years into the future at 4 GHz, audio watchdog and frame-pacing deltas explode, and ano_sleep's own elapsed check (:345) sees ~2^64 and cuts a mid-suspend sleep short 〜 confirmed in source by two passes (no power-broadcast hook, no re-election, no bias term anywhere in the tree); no deterministic trigger seam (needs a real S3/S4 resume on Windows x64, and the clock mode is a TU-private static with no injection hook) 〜 test: pending
- clock-not-reanchored

time_win64.c:310 〜 ano_sleep computes target_ns = us * 1000 in uint64, so us > UINT64_MAX/1000 (a ~585-year request) wraps target_ns near zero and the call returns success almost immediately against the header's "sleep for us microseconds"; the macos twin has the same wrap at time_macos.c:168, while time_linux.c splits us into tv_sec/tv_nsec and is immune 〜 test: pending 〜 a correct ~585-year sleep cannot be awaited by a test
- checked-arithmetic

time_win64.c:337 〜 the no-timer fallback casts coarse_ns/1e6 to DWORD, so a ≥49.7-day sleep truncates its coarse stage and the spin tail then busywaits the missing weeks at 100% core instead of yielding, and a request landing exactly on 4294967295 ms becomes Sleep(INFINITE), a permanent hang; needs the hi-res waitable timer unavailable or SetWaitableTimer failing plus a multi-week argument 〜 test: pending 〜 no timer-failure seam and the correct behavior is a weeks-long wait
- truncating-cast

time_win64.c:252 〜 ano_busywait's loop guard tests startTime != 0 && endTime != 0 while the module's clock error sentinel is UINT64_MAX (ano_sleep at :312 checks the right one), so the guard is dead: a persistently failing clock returns UINT64_MAX twice, the delta stays 0 and busywait spins forever instead of erroring, and a legitimate 0 timestamp ends the wait early as success; latent 〜 QPC cannot fail on XP+ per the comment at :40 〜 and the linux/macos busywaits carry no sentinel check at all 〜 test: pending 〜 no clock-failure injection seam
- wrong-error-source

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ui_path.c:99 〜 ano_ui_path_fill guards its quad budget (qn at :95/:122/:139) but never the contour counter (cn), so every MOVE writes cstart[cn++] into the fixed 513-entry stack array cstart[UI_PATH_MAX_QUADS + 1] unchecked and the :151 seal adds one more; a path with more than 512 empty contours 〜 legal input the contract promises ANO_UI_REF_NONE for ("the path is empty") 〜 sprays cstart past its end over the live quad buffer q[] sitting just above it, and the emit pass reads the corrupted geometry back out and faults 〜 test: anotest_uipathguard
- fixed-array-overflow

ui_build.c:236 〜 paint_push's fullness guard adds b->stopCount + stopCount in uint32, so once any stops are resident a stopCount near UINT32_MAX wraps the sum under stopCap, the guard passes, and the :239 copy loop writes ~2^32 32-byte stops past the caller's array 〜 reached from the public ano_ui_paint_linear/radial/conic and a direct breach of the header's "full array -> ANO_UI_REF_NONE, no mutation" promise at anoptic_ui.h:121; absurd-argument territory, but the guard is defeated rather than the input rejected 〜 test: anotest_uipaintguard
- checked-arithmetic

ui_tiles.c:66 〜 ano_ui_tile_build computes nTiles = tilesX * tilesY and guards nTiles + 1 > offsetsCap in uint32, so 65536 x 65536 wraps nTiles to 0, the cap guard passes, *ok stays true for a 2^32-tile grid the buffers cannot hold, and any prim in the scene scatters pass-1 counts into offsets[] far past offsetsCap; tilesX = UINT32_MAX with tilesY = 1 wraps nTiles+1 to 0 instead and the :73 zero-fill writes 2^32 offsets out of bounds 〜 absurd tile counts, but "*ok false if a cap is too small" is exactly the promise the wrap defeats 〜 test: anotest_uitileguard
- checked-arithmetic

ui_raster_ref.c:229 〜 ano_ui_ref_paint's stop-window reject arm computes pa->stopFirst + pa->stopCount in uint32, so stopFirst near UINT32_MAX wraps the sum under s->stopCount, the guard passes, and ui_stop_color reads s->stops[stopFirst] ~137 GB past the table on its first comparison 〜 a direct breach of the header's "Out-of-range ref fails CLOSED (transparent)" promise (anoptic_ui.h:275) in the CPU mirror the GPU evaluator cites as its fail-closed reference, and the exact wrap shape the bridge entry (ano_render_bridge.c:204) shows no upstream layer catches, so the last line of defense crashes on the block it exists to reject; the plain non-wrapping out-of-range window fails closed correctly 〜 test: anotest_uirefstopguard
- checked-arithmetic

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 

main.c:391 〜 music_world_start bounds its telemetry handshake at 200 tries x 5 ms (:388-390) and then seeds ano_synth_transport_start((t.blockIndex + 8u) * t.blockFrames) unconditionally, but the acquire contract returns false with *out untouched until the mixer's first publish (anoptic_audio.h:375 documents "false before first mixer publish"; audio_bridge.h:144 bails before writing a byte of out while the version is 0), so exhausting the retries is indistinguishable from success and the AnoAudioTelemetry t declared at :388 is consumed uninitialized 〜 two indeterminate stack words multiply into the absolute worldFrame 〜 every other bounded wait in the function has a handled false path, and this one falls through to logging "composing live" and returning true instead of the documented false -> silent run; the telemetry readout at :917 and anotest_audiotone's wait_telemetry both check this same call's return, only the boot path drops the false arm 〜 a mixer thread that publishes nothing for ~1 s after ano_audio_init returns true (loaded box, sanitizer-grade slowdown, wedged device period; starvation on that scale is documented reality on the 2-core sanitizer CI runners) starts the transport at a garbage frame: far-future / huge garbage parks the "few blocks ahead of playhead" start effectively forever as permanent silence reported as a healthy music world, past-frame / small garbage opens with a genLate storm, and the read is UB either way 〜 confirmed in source by two passes; no deterministic field trigger headless (the composition pins all generator hooks to ano_synth_*, a fresh mixer publishes telemetry in its first loop pass before any device pull, music_world_start is a static in src/engine/main.c, and the field trigger itself is OS starvation of a live mixer) 〜 test: anotest_boottelemetryguard (tentative) 〜 compiles the real main.c TU and forces the false arm with a contract-faithful acquire shim at the seam; the field trigger is not reachable deterministically
- retry-exhaustion



## Leads for future passes

(unverified suspicions surfaced during census iterations 〜 chase, confirm, then move up into a tally or strike)

- filesystem_win64.c:33 〜 len >= MAXPATH checked on the full exe path before trimming the filename; linux/macos check after trimming 〜 256-259 char full paths whose dir fits fail only on win64.
- filesystem_win64.c:23 〜 the ANSI-API debt comment covers GetModuleFileNameA only; CreateFileA and getenv("APPDATA") share the non-ASCII mangling but are not acknowledged.
- log↔filesystem seam 〜 log_core.c:834 and log_crash.c:34 consume ano_fs_logpath; behavior when it legitimately returns length 0 unaudited.
- memory↔every-module seam 〜 MI_OVERRIDE=OFF (CMakeLists.txt:105) + macro-only override (anoptic_memory.h:15) means two live allocators partitioned by "TU includes anoptic_memory.h or not"; safe today (verified ano_meshoptimizer.c is self-contained), but any internally-allocating libc call (getline/asprintf/scandir) in a macro'd TU frees a glibc pointer with mi_free 〜 deserves a dedicated composition pass.
- instance.c:192 〜 getRequiredExtensions strdups extension strings, createInstance frees only the array 〜 per-string leak (vulkan_backend iteration).
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
- engine↔music panel 〜 main.c:613 guards chordDegree but not keyTonic/mode; cfg->keyTonic is unclamped at music_conductor.c:705 〜 a negative indexes PC_NAMES/MODE_NAMES out of bounds.
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
