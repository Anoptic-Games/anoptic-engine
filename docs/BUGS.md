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

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

music_arp.c:102 〜 ano_generate_arp emits one event per meter slot into AnoArpResult.events[ANO_METER_MAX_SLOTS] with no bound, but ano_meter_slots exceeds the 32-slot cap for any meter past 8 quarters (9/4 is 36, 12/4 is 48) and ano_music_create accepts such meters unvalidated, so at noteDensity > 0.65 the loop writes the tail past the stack array inside ano_engine_advance_bar 〜 the 33rd write lands on eventCount itself, recycling the tail into events[0..2] and silently truncating the bar to 3 events on top of the stack stomp 〜 the metric-weights twin at music_ir.c:63 clamps exactly this and the arp does not 〜 test: anotest_musicarpguard

### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_ops.c:86 〜 anostr_join sizes its result as sep.len * (count-1) plus part lengths in uint64, so sep.len near UINT32_MAX with count ≈ 2^32+3 wraps the product to ≤ UINT32_MAX and slips the :89 oversize guard that exists to reject exactly this; the write loop then streams ~2^64 bytes into the ~4 GiB allocation 〜 the cheapest trigger needs a 64 GiB parts array (2^32+3 16-byte anostr_t entries), far past any engine caller but constructible on a large-memory host 〜 test: pending 〜 needs a 64 GiB parts array

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

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

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 
