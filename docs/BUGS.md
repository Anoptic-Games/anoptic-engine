# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_audio.c:257 〜 buffer_register computes frames * channels * sizeof(float) in uint64 with no wrap guard, so frames ≥ 2^62 wraps bytes64 past 2^64 to a tiny value that passes the SIZE_MAX check; a near-empty block is allocated, the header keeps the huge frame count, and the call returns true instead of rejecting bad args 〜 any voice playing that buffer reads far out of bounds on the mixer thread 〜 test: anotest_audioguard

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
