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

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 — no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_linux.c:65 〜 ano_fs_userpath accepts mkdir's EEXIST as success without checking the existing entry is a directory, so a regular file or dangling symlink squatting ~/.anoptic returns a non-empty path nothing can be created under; the macos (:69) and win64 (:61) twins and the fs_mkdir primitive (:82) behind ano_fs_logpath share the one-sided filter 〜 the header's "Non-empty result is ready to write into" promise breaks: every save/config open under the returned path fails ENOTDIR while the resolver reports success, and a file named logs beside the exe kills the log and crash-log directory the same silent way 〜 test: anotest_fsguard

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

log_core.c:817 〜 the drain batch g_batch is sized ring bytes + 16 per record on the claim that a record's rendered text fits its ring footprint, but a deferred record renders at its format width, not its stored size 〜 "%*d" width 4000 holds one 64-byte ring line yet emits ~4016 batch bytes, so one pass over a ~164-record backlog walks blen past g_batchCap (the per-record prefix memcpy and newline are unchecked), the size_t room subtraction underflows and unbounds every later record into a multi-MB heap overwrite on the draining thread 〜 test: anotest_logflood

### Interlink / Composition bugs 



## Math

(anoptic_math.h is types-only as of 2026-07-17 — mat4/Vector2/3/4 PODs, no ops, no src/ module; ops live in render/vertex.h. Composition audits belong to the render_bridge and vulkan_backend interlink edges.)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

memalign_linux.c:13 〜 ano_aligned_malloc is a bare forward to mi_malloc_aligned, which follows malloc's zero-size convention and hands back a live minimum-size block, while the header (anoptic_memory.h:47) promises NULL when size is 0; only the alignment-0 half of that sentence holds (mimalloc's power-of-two reject), so the contract is half-implemented, and the macos (:13) and win64 (:13) twins share the one-line forward 〜 the documented zero-size sentinel never fires: a caller branching on NULL to reject a degenerate count*stride==0 request instead gets success plus a block it believes cannot exist, so its reject path is unreachable and each such call leaks one block under "NULL means nothing was allocated" 〜 test: anotest_memguard

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_meshoptimizer.c:282 〜 ano_build_meshlets clamps max_vertices/max_triangles only from above and skips the max_vertices < 3 / max_triangles < 1 rejection its sizing twin ano_build_meshlets_bound enforces at :85, so build(indices {0,1,2}, max_vertices 2) returns 1 meshlet with vertex_count 3 where bound() returned 0 〜 a caller sizing buffers from bound() per the header contract hands build zero-length arrays and takes a heap overwrite, and the emitted meshlet breaks the max_vertices promise the meshlet_vertices layout is built on 〜 test: anotest_meshguard

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

music_voicing.c:114 〜 ano_voice_chord builds its candidate table in a plain function-scope static (static Cand cands[256], 6 KiB, comment "single-threaded conductor context"), shared across every engine in the process, while the module's own hosting design (ANOPTIC_MUSICGEN.md seek: rebuild a second engine off-thread while the callback-hosted composer keeps advancing the live one) runs two engines through advance_bar -> generate_pad -> ano_voice_chord concurrently; the dedupe scan, cost pass, and final out[] copy read entries the other thread is rewriting, so a wrong pad voicing is selected and enters st->prevVoicing 〜 the anoptic_music.h:481 contract "Same config+seed+bar => byte-identical" breaks the moment any second engine composes concurrently: the off-thread seek snapshot is not the piece linear play would have produced (ACMD_MUSIC_SEEK adopts audibly different music, musicdrive's sample-identical-seek invariant fails), the live bar can sound a foreign chord, and TSan confirms the write-write race on ano_voice_chord.cands from both threads 〜 test: anotest_musicguard

### Interlink / Composition bugs 

music_arp.c:106 〜 the arp lane clamps velocity to 1..127 (:87) and then adds its +4 slot accent with no re-clamp (its own header admits "Accented slots add 4 with no re-clamp"), and both inward parameter paths hand it an unclamped velocityCenter: expand() copies the public uint8_t field raw into the internal int (music_host.c:81) and the "velocity_center" override stores and uses the raw double (music_host.c:183, music_conductor.c:599/607 — ACMD_MUSIC_OVERRIDE forwards game-supplied values verbatim at ano_synth.c:703), even though the outward bridge clamps velocityCenter to 0..127 (music_ir.c:109), declaring the very domain the inward side never enforces; every other lane clamps its final velocity (bass :62, pad :198, melody velocity_of, counter :253, perc :216, imitation :141) and the arp's default modifier chain is echo-only, which clamps echo copies but not originals, so any velocityCenter >= 140 makes every accented arp slot — slot 0 of every bar, which the skip logic never masks — emit velocity 128..131, violating AnoNoteEvent's documented 1..127 〜 the host copies event cores verbatim into AnoMusicBar (music_host.c:233), music_pump feeds them to ano_synth_live_bar whose one-sided guard (ano_synth.c:429, batch twin :246 — tallied Synth-side) stages them, and the voice renders amp = powf(131/127, 1.5) ≈ 1.048 (synth_voices.c:353), above the ceiling any contract-legal velocity can produce, on the downbeat of every bar the arp sounds — in-tree traffic through a plain console override crosses the broken guard, so the music emitter defect and the synth acceptor defect compose end-to-end 〜 test: anotest_musicsynthguard



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_GltfParser.c:30 〜 parseGltf goes from cgltf_parse_file/cgltf_load_buffers straight to per-element cgltf_accessor_read_float (:80) and cgltf_accessor_read_index (:96) without ever calling cgltf_validate, the library's only accessor-vs-bufferView and bufferView-vs-buffer byte-range gate; those helpers compute buffer->data + view->offset + accessor->offset + stride*index with no bounds check, and load_buffers only verifies the .bin/base64 payload reaches the declared byteLength, never the accessor math, so a file whose accessor count*stride (or view offset+size) overruns the loaded buffer walks off the end of the heap block 〜 a truncated or hostile asset is read out of bounds during init (a large claimed count reads gigabytes past the block and faults the loader), garbage vertices and out-of-range index values flow into geometry_pool_upload_chain and the GPU index buffer, and parseGltf returns a non-NULL "successfully parsed" asset instead of the NULL its own :25/:31 error paths establish for bad files 〜 test: anotest_gltfguard

slot_upload.c:277 〜 ensureEntityCapacity recreates every entity-scaled buffer — growBufferSet (:36) vkDestroyBuffer's each old per-frame live-transform SSBO after binding its replacement — then re-points descriptors through updateUboDescriptorSets alone, but the shadowsetup compute set's binding 1 is that same transformBuffer.buffer[i] (descriptors.c:357/:376) and its only writer, updateShadowDescriptorSets, runs exactly once in initVulkan (vulkanMaster.c:666); nothing re-runs it on growth, and swapchain recreation re-runs only the Hi-Z and tonemap sets 〜 the first CREATE/BULK_CREATE that pushes slotHighWater past the current capacity (INITIAL_ENTITY_CAPACITY 10000) leaves frames[i].shadow.setupSet binding 1 referencing a destroyed VkBuffer, and PIPELINE_COMPUTE_SHADOWSETUP binds that set every frame (record.c:133, passes.c pass 2), so validation flags every subsequent dispatch and on a live device shadowsetup.comp reads whatever the bump allocator kept behind the freed handle: every shadow-frustum viewProj and the fragment-stage sampling-viewProj UBO derive from stale or garbage parent transforms, shadows permanently detach from their lights (or the device faults) after the first entity growth — a boundary the demo scene never crosses 〜 test: anotest_vkguard

### Interlink / Composition bugs 

render_bridge/ano_render_bridge.c:92 〜 ano_render_bridge_destroy tears the commands ring down through ano_spsc_destroy (:51), which frees only ring->buffer, so every RenderCommand still enqueued is discarded without honoring bulk_owned: the mi_malloc'd render-owned copies the submit helpers packed and pointed cmd.text/cmd.ui/cmd.update/cmd.destroy at (ano_render_text_set :152, ano_render_ui_set :246, producer.c :56/:90) lose their last reference with the ring — the producer relinquished them at push per the header's copy-at-submit/render-frees contract (anoptic_render.h:435), and every other drop path frees the copy (full-ring reject at submit, replace/registry-full/clear at adoption), only the in-ring-at-destroy window frees nothing 〜 main.c stops draining at its last drawFrame before setting g_logicShouldStop, then joins and calls unInitVulkan (vulkanMaster.c:89) with no drain between, so the final logic ticks' TEXT_SET/UI_SET blocks (per-tick HUD and music-panel submissions, up to ~384 KiB per text block) land exactly in that window and leak every shutdown; an embedder cycling initVulkan/unInitVulkan on device loss or level reload, or any bridge-owning harness, accumulates them unbounded 〜 test: anotest_bridgeguard



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_collate.c:75 〜 ce_push_rune consults the decomp table before the CE table, and the generated tables disagree about coverage: U+01EF (ǯ, ezh-caron, Skolt Sami) and U+0374 (Greek numeral sign) carry correct direct CE listings ([270B.020.02]+[0000.028.02] and [04F2.020.02] in ano_collate_tables.h) that are unreachable because their decomp entries redirect through U+0292 / U+02B9, both trimmed out of the CE table, so ce_push_cp falls to UCA implicit weights (primary 0xFBC0, the after-every-listed-script band); the uppercase sibling U+01EE decomposes through U+01B7, which the trim kept, so only the lowercase form is poisoned 〜 the case pair the module itself maps (anorune_to_lower(U+01EE) == U+01EF) splits across the whole collation space: anostr_collate puts Ǯ before あ but ǯ after it, anostr_eq_base(Ǯ, ǯ) is false while every healthy decomposing pair (Ǒ/ǒ) holds, anostr_sort places "Ǯ" in the Latin band right after z (prefix key 270B…) and "ǯ" past kana among the Han implicits (FBC0.8292…), and base-letter search cannot match ǯ words 〜 test: anotest_strguard

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_synth.c:246 〜 the score_event guard rejects velocity == 0 but never the documented upper bounds (velocity 1..127, pitch 0..127 per AnoNoteEvent), so an event with velocity 200 or pitch 130 returns true and enters the schedule; the voice then renders at powf(v/127, 1.5) ≈ 2x the contract's amplitude ceiling, and merge_ties keys chains on pitch & 0x7F so an out-of-range pitch aliases a different in-range pitch's tie chain and silently merges two different-pitch notes into one 〜 the live twin at :429/:431 has the same one-sided filter 〜 test: anotest_synthguard

### Interlink / Composition bugs 



## Text

### Interface-level bugs and logic inefficiencies

text_shape.c:126 〜 shape_core sets the final-line step from runs[runCount-1].sizePx unconditionally and ano_text_measure_runs (:195) returns penY + that step, but anoptic_text.h:113 promises "byteCount 0 is a no-op"; a trailing byteCount-0 run is at once a no-op and runs[runCount-1], so its sizePx sets the measured height though it styles nothing, while a leading/middle no-op run stays transparent (skipped by the run-advance loop) 〜 a caller that appends an empty trailing style span (a per-span run list ending on a zero-width style/cursor) gets a text box sized to that empty span's sizePx, not the last rendered line's: measure_runs("AA", [{2,32px},{0,64px}]) reports height 64 where the unsplit [{2,32px}] reports 32 〜 test: anotest_textguard

### Implementation bugs

### Interlink / Composition bugs 



## Threads

### Interface-level bugs and logic inefficiencies

### Implementation bugs

threads_macos.c:79 〜 the Darwin barrier gap-fill samples generation before the arrived fetch_add (:81), and the completing thread's arrived reset (:84) has no guard against increments landing between the count-th arrival and the reset, so in the over-subscribed reuse POSIX defines (more threads than count sharing the barrier, released cohort by cohort — glibc behind the Linux build handles it) a thread preempted between :79 and :81 while another cohort completes finds its :89 spin predicate already false and returns 0 as the sole arrival of a fresh round, and an increment racing the :84 reset is silently erased; exactly-count usage is provably correct, the divergence only bites shared-cohort usage 〜 an over-subscribed ano_thread_barrier_wait rendezvous (pairwise handoff, cohort batching) releases threads on macOS before count peers have arrived, so a waiter consumes partner data that was never written while the identical caller code is correct on linux/win64 〜 confirmed in source by three passes; no deterministic trigger seam from here (Darwin-only TU — forcing __APPLE__ on glibc collides the pthread_spinlock_t/pthread_barrier_t typedefs — and the window sits between two adjacent atomics, stress-only even on target) 〜 test: pending

### Interlink / Composition bugs 



## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

time_win64.c:148 〜 ano_timestamp_ticks in TSC mode returns raw __rdtsc with no re-anchor across power transitions: the TSC does not survive S3 sleep or S4 hibernate (the core power domain drops and firmware restarts the counter near zero — the reason both Windows and Linux re-base their own TSC-derived clocks on resume), the invariant-TSC check at :55 only covers P/C/T states, the election at :130 is frozen for the process, and ano_ticks_to_ns is a pure function of the frozen cachedTscHz, so the first post-resume stamp lands below every pre-sleep stamp and ano_timestamp_raw/us/ms follow it backward; the QPC path the same function uses on non-invariant-TSC CPUs is immune (the kernel re-biases QPC at resume), as are linux CLOCK_MONOTONIC and darwin mach_absolute_time 〜 the header's monotonic promise (anoptic_time.h:20) breaks exactly on the machines that elected the fast path — every u64 now-start delta held across a lid-close or hibernate wraps toward 2^64: log_core.c:395's anchor math stamps records ~146 years into the future at 4 GHz, audio watchdog and frame-pacing deltas explode, and ano_sleep's own elapsed check (:345) sees ~2^64 and cuts a mid-suspend sleep short 〜 confirmed in source by two passes (no power-broadcast hook, no re-election, no bias term anywhere in the tree); no deterministic trigger seam (needs a real S3/S4 resume on Windows x64, and the clock mode is a TU-private static with no injection hook) 〜 test: pending

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ui_path.c:99 〜 ano_ui_path_fill guards its quad budget (qn at :95/:122/:139) but never the contour counter (cn), so every MOVE writes cstart[cn++] into the fixed 513-entry stack array cstart[UI_PATH_MAX_QUADS + 1] unchecked and the :151 seal adds one more; a path with more than 512 empty contours 〜 legal input the contract promises ANO_UI_REF_NONE for ("the path is empty") 〜 sprays cstart past its end over the live quad buffer q[] sitting just above it, and the emit pass reads the corrupted geometry back out and faults 〜 test: anotest_uipathguard

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 

main.c:391 〜 music_world_start bounds its telemetry handshake at 200 tries x 5 ms (:388-390) but consumes the result unconditionally: ano_audio_acquire_telemetry is documented "false before first mixer publish" and the seqlock load behind it (audio_bridge.h:144) returns false before writing a byte of out, so when the spin exhausts, (t.blockIndex + 8) * t.blockFrames multiplies two indeterminate stack words into ano_synth_transport_start's absolute worldFrame; every other bounded wait in the function has a handled false path, and this one falls through to logging "composing live" and returning true instead of the documented false -> silent run 〜 a mixer thread that publishes nothing for ~1 s after ano_audio_init returns true (loaded box, sanitizer-grade slowdown, wedged device period) starts the transport at a garbage frame: far-future garbage is permanent silence reported as a healthy music world, past-frame garbage opens with a late-note storm, and the read is UB either way 〜 confirmed in source by two passes; no deterministic trigger seam headless (the composition pins all generator hooks to ano_synth_*, a fresh mixer publishes telemetry in its first loop pass before any device pull, and music_world_start is a static in src/engine/main.c no test can link) 〜 test: pending



## Leads for future passes

(unverified suspicions surfaced during census iterations — chase, confirm, then move up into a tally or strike)

- filesystem_win64.c:33 〜 len >= MAXPATH checked on the full exe path before trimming the filename; linux/macos check after trimming — 256-259 char full paths whose dir fits fail only on win64.
- filesystem_win64.c:23 〜 the ANSI-API debt comment covers GetModuleFileNameA only; CreateFileA and getenv("APPDATA") share the non-ASCII mangling but are not acknowledged.
- log↔filesystem seam 〜 log_core.c:834 and log_crash.c:34 consume ano_fs_logpath; behavior when it legitimately returns length 0 unaudited.
- anoptic_math.h:16 vs docs/math-conventions.md 〜 header comment declares mat4 row-major; the conventions doc establishes column-major — one of the two contracts is lying.
- memory↔every-module seam 〜 MI_OVERRIDE=OFF (CMakeLists.txt:105) + macro-only override (anoptic_memory.h:15) means two live allocators partitioned by "TU includes anoptic_memory.h or not"; safe today (verified ano_meshoptimizer.c is self-contained), but any internally-allocating libc call (getline/asprintf/scandir) in a macro'd TU frees a glibc pointer with mi_free — deserves a dedicated composition pass.
- instance.c:192 〜 getRequiredExtensions strdups extension strings, createInstance frees only the array — per-string leak (vulkan_backend iteration).
- scratch_process.c (src/render/gltf/) 〜 dead code: not in any CMakeLists, no includer.
- memalign_win64.c:1 〜 guards _WIN64 while CMake selects on WIN32; a 32-bit Windows build gets an empty TU and a link failure (marginal, engine is 64-bit-only).
- threads_macos.h:15 〜 PTHREAD_BARRIER_SERIAL_THREAD lives only in the private header ("Never include outside src/threads"); on Linux pthread.h supplies it, on Darwin nothing public does — portable caller code testing ano_thread_barrier_wait's documented serial return compiles on linux/win64 and fails to compile on macOS.
- threads↔log_crash seam 〜 the spawn trampoline's cleanup handler (threads.c:39) disarms the altstack before pthread TSD destructors run, so a stack-overflow crash inside any TSD destructor (mimalloc thread teardown included) faults with no alternate stack and the blackbox Stage-1 handler recurses instead of reporting.
- music_theory.c:48 〜 ano_mode_intervals lazy-bake static sets the baked flag after the table writes with no fence — same cross-thread shape as the voicing race, benign on x86 today.
- music_host.c cadence_policy 〜 ano_music_set_override("cadence_policy", -1) stores ANO_CADENCE_NONE which ano_next_chord indexes into CADENCE_TARGET[]/PRE_CADENCE_FUNCTION[] — OOB read for an enum-defined value.
- music_host.c override_apply 〜 has-flag written before value with plain stores; safe only while commands apply on the engine-owning thread — any future cross-thread ano_music_set_* re-opens it.
- ano_GltfParser.c:71 〜 parseGltf ignores geometry_pool_upload_chain's failure return; on pool exhaustion lodBase stays 0 and the primitive silently renders the fallback cube (FALLBACK_MESH_INDEX 0), same silent-0 for skipped primitives.
- render texture usage 〜 textureSrgb[] is set true per-usage; a texture shared between a color slot (baseColor/emissive) and a data slot (metallicRoughness/normal) uploads once as sRGB, silently corrupting the data usage.
- ano_GltfParser.c:76 〜 posAccessor->count / indices->count size_t→u32 casts unguarded; >4G-element accessors truncate even after a validate gate lands (marginal).
- cgltf↔memory seam 〜 cgltf's implementation TU compiles under the malloc-macro override, so its internal alloc/free stay mimalloc-consistent only within that TU; any future cgltf call from a non-macro'd TU sharing cgltf_data cross-frees allocators (instance of the MI_OVERRIDE=OFF lead).
- ano_unicode_tables.h case trim 〜 anorune_to_upper(0x0292)==0x0292 while to_lower(0x01B7)==0x0292 — the trim kept one direction of the Ʒ/ʒ pair; same trim-boundary disease as the tallied collation bug, a coverage-consistency pass over tools/gen_unicode_tables.c would catch the whole class.
- ano_strings_collate.c:504 〜 mi_malloc-failure fallbacks (qsort + fb_sym_cmp_) break documented sort stability for byte-equal values with distinct backings; not deterministically testable in-process.
- strings↔log seam 〜 log renders %.*s of anostr_fmt output raw on malformed UTF-8 — unaudited.
- anostr_sort_idx 〜 count > UINT32_MAX silently leaves the identity permutation while anostr_sort sorts; unrepresentable-permutation edge the header is silent on.
- text↔ui seam 〜 ano_quad_split_monotone/ano_half_pack/unpack defined in text_bake.c but re-declared with a duplicated AnoQuad type in ui_path.h; the two definitions must stay bit-identical and only a size static_assert guards them — ODR/ABI seam.
- render↔text seam 〜 textcoverage.glsl mirrors text_raster_ref.c statement-for-statement (the C ref is the GPU's validation oracle) with no enforcement coupling; drift in one silently corrupts the RMS verification.
- text_bake.c ano_text_window_sum 〜 reads pts[g->pointOffset] unconditionally when curveCount==0 — OOB by one uint32 for a blank last glyph with pointOffset==pointCount; every in-tree caller gates on curveCount>0, latent precondition gap.
- text GPOS caps 〜 GPOS_MAX_LOOKUPS=16 / GPOS_MAX_SUBS=32 truncate silently; a font spreading kern across more lookups/subtables loses pairs with no contract mention.
- engine↔render_bridge seam 〜 the blocking retry spins (main.c:48/162/683) never check g_logicShouldStop and main stops draining the command ring before joining logicThread (main.c:1057) — unreachable today only because the 4096-slot ring exceeds startup command volume; scene growth past ring capacity turns close-during-spawn into a shutdown hang.
- vulkanMaster.c:593 〜 ano_render_load_scene_assets failure returns false without unInitVulkan(), unlike every sibling failure arm — leak/asymmetry.
- engine↔music panel 〜 main.c:613 guards chordDegree but not keyTonic/mode; cfg->keyTonic is unclamped at music_conductor.c:705 — a negative indexes PC_NAMES/MODE_NAMES out of bounds.
- log_crash↔log teardown 〜 process-lifetime crash hooks outlive ANO_LOG_SCOPE_ATTR cleanup at main exit; POSIX path audited safe, log_crash_win64.c not audited.
- render_slots.c:84 〜 render_slots_alloc_range mid-loop logical_reserve OOM returns UNMAPPED leaving earlier mappings pointed into un-reserved high-water slots with no rollback — a later alloc double-maps them; OOM-only path.
- slot_upload.c growth VRAM 〜 growBufferSet/slot_upload_grow_device never return the old GpuAllocation (bump allocator, no free) — every entity growth leaks the prior span; "handle only" comment may mean accepted design.
- swapchain recreate↔descriptor ownership seam 〜 recreate re-runs only Hi-Z/tonemap descriptor sets (swapchain.c:389) — any per-frame resource recreated there but bound elsewhere repeats the shadowsetup-dangling-descriptor shape.
- vulkan_backend↔render_bridge adopted blocks 〜 RCMD_TEXT_SET/RCMD_UI_SET use a different free contract than free_owned_bulk — ownership seam unaudited.
- apply.c:346 〜 REVENT_SLOT_RETIRED emitted fire-and-forget while anoptic_render.h promises lifetime facts are lossless; a retirement burst past the 256-slot input reserve with slow logic drain silently strands render_ids (latent: main.c ignores the event today).
- bridge dead protocol 〜 REVENT_BATCH_CONSUMED has no emitter and RCMD_BULK_CREATE no submit helper; the header-documented borrowed-batch producer-frees-on-ack lifetime is unimplemented — a conforming producer waits forever. Near-certain future tally.
- ano_render_ui_set validation gap 〜 paints' stopFirst/stopCount never checked against the block's stopCount; after compose rebase a hand-built block samples other blocks' gradient stops (GPU bounded, CPU ref evaluator could read OOB).
- transformStream reclaimSeq 〜 produceSeq/curSeq cross threads via ring ordering (audited sound) but the reclaimSeq writer side is unaudited.
- music inward/outward clamp asymmetry (generic) 〜 music_ir.c:112/:115 clamp accentDepth/registerCenter outward while override_apply casts raw doubles inward (music_host.c:187/:188) — same shape as the tallied velocity seam; other lanes unaudited for overflow from those.
- tempo_bpm override 〜 raw double into mapped_params with no validation; a 0/negative pin crossing into ano_synth_live_bar's barSeconds = barQ*60/tempoBpm division is unaudited.
- time_win64.c:316 〜 ano_sleep with us <= 1000 skips the coarse stage entirely and is a pure busywait, while the header (anoptic_time.h:62) promises "Yields to the scheduler"; anotest_time.c:173 already calls it "spin-only on Windows", so decide whether the header or the implementation is the contract.
- time_linux.c:132 〜 clock_nanosleep returns its error directly and does not set errno, so the non-EINTR failure path perrors stale state and returns a stale errno (possibly 0 = success); unreachable with the tv_sec/tv_nsec this wrapper builds, but the convention is wrong.
- time suspend semantics 〜 even after the win64 TSC re-anchor lands, the three platforms disagree on what a monotonic delta held across a system sleep means: linux CLOCK_MONOTONIC and intel-mac mach_absolute_time exclude the sleep, win QPC and apple-silicon mach_absolute_time include it — the header is silent and the audio/music schedulers consume these deltas cross-platform.
