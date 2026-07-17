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

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_meshoptimizer.c:282 〜 ano_build_meshlets clamps max_vertices/max_triangles only from above and skips the max_vertices < 3 / max_triangles < 1 rejection its sizing twin ano_build_meshlets_bound enforces at :85, so build(indices {0,1,2}, max_vertices 2) returns 1 meshlet with vertex_count 3 where bound() returned 0 〜 a caller sizing buffers from bound() per the header contract hands build zero-length arrays and takes a heap overwrite, and the emitted meshlet breaks the max_vertices promise the meshlet_vertices layout is built on 〜 test: anotest_meshguard

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

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
