# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_audio.c:257 〜 buffer_register computes frames * channels * sizeof(float) in uint64 with no wrap guard, so frames ≥ 2^62 wraps bytes64 past 2^64 to a tiny value that passes the SIZE_MAX check; a near-empty block is allocated, the header keeps the huge frame count, and the call returns true instead of rejecting bad args 〜 any voice playing that buffer reads far out of bounds on the mixer thread 〜 test: anotest_audioguard

audio_wav.c:34 〜 wav_write has the same unchecked frames * channels * sizeof(float) product, so frames near 2^62 wraps dataBytes64 to a tiny value that slips under the RIFF 32-bit guard; a truncated WAV is written whose fact chunk claims the wrapped frame count and the call returns true instead of rejecting bad args 〜 a caller saving a capture gets silent success and a lying file 〜 test: anotest_wavguard

### Interlink / Composition bugs 



## Collections

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

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 
