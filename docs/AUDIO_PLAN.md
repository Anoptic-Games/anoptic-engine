# AUDIO_PLAN — The Anoptic Audio Stack

Plan for the engine audio stack: the general audio module, the synthesizer, the musicgen port, and the logic-thread bridge. Goes with TECH_SPEC.md (the musicgen port contract) and THEORY_SPEC.md (the musical rules). Where this document and TECH_SPEC disagree about a music-system contract, TECH_SPEC wins; this document decides everything TECH_SPEC left to the engine — device backends, module boundaries, thread topology, and the bridge protocol.

Inputs: TECH_SPEC.md §12 (the audio-library requirements probe and its nine
findings), the musicgen prototype at `~/Documents/anoptic-musicgen` (generation
core is stdlib-only Python; its signalflow synth is the requirements probe, not
a port target), the render bridge (`src/render_bridge/`), and a
July-2026-verified survey of pure-C cross-platform audio output.

---

## 1. Findings

### 1.1 What musicgen demands of the stack

Distilled from TECH_SPEC §11–§12 and the prototype:

- A block-based realtime renderer: block 512 ≈ 11 ms at 48 kHz is the control
  cadence; commands apply at block boundaries; bar-level music decisions apply
  at bar edges. Offline rendering steps sub-blocks that stop at every event
  boundary — sample-accurate scheduling, not block-quantized.
- The audio thread owns the DSP graph (finding 9). Structural change arrives
  as queued commands applied at block boundaries. Voice allocation is a state
  flip in a preallocated pool, never a graph edit. No allocation, no locks on
  the audio thread.
- A fixed DSP primitive inventory (§12.2): band-limited oscillators, seeded
  noise, 2D wavetables, sampler, granular, ADSR/ASR, SVF, biquad EQ, DC
  blocker, variable single-tap delay, comb, allpass, Householder 4-line FDN,
  chorus, ping-pong, tanh saturator, feedback compressor with fixed makeup,
  lookahead limiter, hard clip, constant-power pan, mid/side width, TPDF
  dither, one-pole smoothing on every audible parameter.
- A console topology (§12.4): per-layer strips → dry sum; two send buses
  (FDN reverb, tempo-synced ping-pong); a master chain
  drive → compressor → limiter → clip → dither. This is a configuration of a
  general bus system, not a special-purpose machine.
- Voices are preset parameter blocks resolved at allocation (finding 4): fixed
  topology per voice class, constants from patch data, no per-voice branching
  in the render loop. Six layers, energy-tiered patches, synthesized drums.
- Two smoothing tiers (§12.5): the conductor slews musically; the audio side
  only ever glides retargets through one-poles (20–45 ms). The audio library
  never sees per-sample automation from above.
- Determinism gate (§12.7): seeded stochastic primitives, initialized DSP
  state, stable scheduling tiebreakers, and a churned-heap double-render
  bit-diff as a CI gate.
- Generation itself is single-threaded and microseconds per bar; the prototype
  proved the shape where one thread generates, mutates, and renders,
  interleaved between blocks. TECH_SPEC §11.4 explicitly names the split
  where the audio thread is the sole graph owner with a block-boundary command
  queue — the render-bridge pattern, named.

### 1.2 What the engine has today

- No audio code exists: no `include/anoptic_audio.h`, no `src/audio/`, no
  build-sequence slot in docs/TODO.md. This is the first audio module.
- The render bridge is the bridge template we already ship
  (`src/render_bridge/render_bridge.h`): a bounded lock-free SPSC ring
  (`AnoSpscRing`, cursors `_Alignas(ANO_THREAD_LINE)`), a latest-wins seqlock
  (`ano_seqpub_store/load`), copy-at-submit lifetime for POD commands,
  owned-`mi_malloc`-block lifetime for fat payloads (consumer frees),
  backpressure-retry on command overflow, best-effort + capacity advisory on
  the event ring. Design rule, verbatim from `anoptic_render.h`: discrete
  lossless facts ride a command/event ring; continuous latest-wins state rides
  a published double buffer.
- `include/anoptic_collections.h` is an empty stub — the generic lock-free
  collections have not landed. The render bridge keeps private copies of the
  ring and seqlock with a migrate-later note. The audio bridge does the same;
  it is the second consumer that justifies promoting both into
  `anoptic_collections.h` later.
- No arena API exists in the memory module: the primitives are mimalloc heaps
  (`mi_heap_new` + `LOCALHEAPATTR`), `ano_aligned_malloc`, and
  `ANO_CACHE_LINE`/`ANO_THREAD_LINE`. The render bridge allocates its rings
  from a dedicated `mi_heap_t`; the audio stack does the same and builds its
  own fixed pools on top.
- Threads: `ano_thread_create/join` wrap pthreads (winpthreads on win64, shim
  on macOS). Runtime today is three threads — main/render, logic
  (`anoLogicThreadMain`, ~2 ms tick, sole render-command producer), and the
  logger drain thread. The audio mixer thread becomes the fourth, spawned and
  joined exactly like the logic thread, shut down before its bridge is
  destroyed.
- Time: `ano_timestamp_ticks` + `ano_ticks_to_ns` for hot-path block timing,
  `ano_sleep` for pacing. Logging: `ano_log` enqueue is lock-free and safe
  from the audio thread; `ANO_NOW` (synchronous flush) is not.

### 1.3 Backend landscape (verified July 2026)

Facts checked against primary sources this week:

- miniaudio 0.11.25 (2026-03) is active, public-domain/MIT-0, and cleanly
  compiles down to a device-I/O-only layer (`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`,
  `MA_NO_DECODING`, …, custom allocation callbacks). Its data path is
  callback-driven and lock-free, but the control path holds internal mutexes.
  It has no native PipeWire backend — Linux rides pulse-compat with a forced
  25 ms default buffer — and none in the unreleased 0.12 branch. One 96k-line
  foreign TU.
- libsoundio is abandoned (last real release 2019, last commit mid-2023).
  PortAudio is maintained but mid-weight, still without IAudioClient3
  low-latency shared mode or native PipeWire. SDL3 audio is excellent and
  native-PipeWire but is a framework — excluded by charter. sokol_audio is
  too thin to adopt (f32 stereo only, no device selection) but is excellent
  reference reading.
- Hand-rolled backends in pure C are a proven path (miniaudio, sokol,
  libsoundio all do it): WASAPI via COM-from-C (`lpVtbl`), IAudioClient3
  event-driven shared mode (periods 128–480 frames driver-dependent, classic
  480/10 ms floor), `IMMNotificationClient` for default-device change;
  CoreAudio AUHAL is a pure-C API, 128–512-frame buffers routine, property
  listener for device change; Linux 2026 means PipeWire native `pw_stream`
  (`PW_STREAM_FLAG_RT_PROCESS` keeps the process callback lock-free;
  `PW_KEY_NODE_LATENCY` requests the quantum; dlopen `libpipewire-0.3` so
  there is no link-time dep) with a dlopen'd ALSA fallback for headless and
  non-PipeWire boxes. PipeWire is the default server on every major distro.
  Estimated effort: WASAPI 800–1500 lines, AUHAL 500–900, PipeWire + ALSA
   1100–1600.
- Format policy that avoids owning a device resampler: fix the engine mixer
  at f32 interleaved 48 kHz; WASAPI shared mixes at engine rate, AUHAL
  converts client→device, PipeWire's graph resamples. Only the raw-ALSA
  fallback needs one f32→s16 conversion loop. Safe latency budget across all
  three platforms: 512–1024 frames total buffering (~10–21 ms); go lower
  opportunistically, never as a requirement.

### 1.4 Backend decision

Ranked options:

- A — hand-rolled per-platform backends (recommended). `audio_linux.c`
  (pw_stream native + ALSA fallback, both dlopen'd), `audio_win64.c`
  (WASAPI/IAudioClient3), `audio_macos.c` (AUHAL), `audio_null.c` (headless /
  CI / offline). Only option that is native-PipeWire today, allocates through
  engine heaps, keeps engine code mutex-free, and maps one-to-one onto the
  `src/<mod>/` per-platform convention. Cost: we own the device testing
  matrix (hotplug, invalidation loops, Bluetooth rate switches); crib
  negotiation and reroute shapes from sokol_audio and miniaudio.
- C — hybrid sequencing: vendored device-only miniaudio behind
  `anoptic_audio.h` first, replaced platform-by-platform by A (PipeWire first,
  where miniaudio is weakest), keeping miniaudio in tests as a device-layer
  A/B oracle. Zero API churn; adopt only if A's Windows/macOS bring-up stalls.
- B — vendored miniaudio permanently: a decade of hardening on day one, but a
  96k-line TU, internal mutexes, and no native PipeWire. Not chosen.

Decision: A, sequenced PipeWire (dev machine) → null/offline (CI) → WASAPI →
AUHAL. The public header never names a backend; callers cannot tell which was
built, per the platform-abstraction charter. The device layer is deliberately
dumb — open, negotiate f32/48k, pull blocks from a ring, report state — so a
later swap of any single backend (or a temporary miniaudio shim) touches one
file.

---

## 2. Architecture

### 2.1 Thread and data-flow topology

```
logic thread                      audio thread (mixer, graph owner)         device thread(s)
─────────────                     ─────────────────────────────────         ────────────────
game systems                      block loop, per 512 frames:               OS-owned (PipeWire RT /
  │  ano_audio_* producer calls     1. drain ACMD ring                       WASAPI event thread /
  ▼                                 2. bar edge? -> advance_bar()            AUHAL render proc)
AnoAudioBridge                          -> schedule NoteEvents (synth)         │
  commands  ────────────────▶       3. render synth voices + SFX sources      │ pop cooked block
  events    ◀────────────────       4. mix buses, sends, master               │ from SPSC block ring
  listener seqlock ─────────▶       5. publish telemetry seqlock              │ (memcpy; silence +
  telemetry seqlock ◀───────        6. push cooked block ─────────────────▶  │  counter on underrun)
```

- The mixer thread is engine-owned (`ano_thread_create`), runs the block loop,
  and is the sole owner of every audio data structure. It is spawned by
  `ano_audio_init` and joined by `ano_audio_shutdown`, before the bridge dies.
- Device backends only ever touch the cooked-block SPSC ring (3–4 blocks of
  f32 stereo, ~4 KiB each). This normalizes all backends behind one shape,
  isolates OS callback quirks from the graph, makes headless/offline the same
  code minus the device, and costs one block of latency — the shape TECH_SPEC
  §11.4 prescribes.
- Pacing: the mixer produces whenever the block ring has space and
  `ano_sleep`s ~1 ms when full — lock-free, no condvar. If wakeup latency ever
  matters, the logger's condvar drainer is the fallback pattern to copy.
- The musicgen conductor runs on the audio thread, called at bar edges from
  the block loop. Generation is microseconds per bar against an 11 ms budget,
  and this is the prototype's proven single-threaded contract with the roles
  renamed: one thread generates, schedules, and renders, so determinism and
  sample-accurate bar placement need no cross-thread event shipping. The
  generation core stays a pure pull-based library, so hoisting it to another
  thread later is a driver change, not an architecture change.
- End-to-end SFX latency: logic tick (≤2 ms) + next block boundary (≤11 ms) +
  block ring (1–2 blocks) + OS period ≈ 25–45 ms typical. Acceptable for game
  SFX; block 256 is the shrink lever if ever needed.
- Allocation rule: all pools (voices, sources, buses, rings, per-bar arena)
  are preallocated at init from a dedicated `mi_heap_t` audio heap. The block
  loop steady state performs zero allocation. Rare control-path exceptions,
  documented: freeing an adopted config blob after applying it at a bar edge,
  mirroring the render bridge's consumer-frees rule.
- Locks: none in engine audio code. OS-internal control-path mutexes
  (pw_thread_loop lock, WASAPI COM) are confined to `audio_<platform>.c`,
  the same exception class as the Vulkan backend.

### 2.2 Module layout

Three modules plus the bridge, same layout as the rest of the engine:

| Layer | Public header | Source | Depends on |
|---|---|---|---|
| audio (device, mixer, buses, SFX, spatial, DSP lib, bridge) | `include/anoptic_audio.h` | `src/audio/` | memory, threads, time, logging |
| synth (voice pool, patches, scheduler, console config) | `include/anoptic_synth.h` | `src/synth/` | audio (buses, DSP lib), music (IR types) |
| music (generation core port per TECH_SPEC) | `include/anoptic_music.h` | `src/music/` | nothing below the stdlib shims |

- `anoptic_music.h` is created first and holds the IR (`AnoNoteEvent`,
  `AnoBarResult`, `AnoHarmonicContext`, `AnoMusicalParams`, tempo points,
  layer/tie enums) even before the music module has an implementation — the
  IR is the authoritative schema per TECH_SPEC §4 and the synth's input type.
  Header-level dependency only; no link inversion.
- Each module: common `ano_<mod>.c`, private headers inside `src/<mod>/`,
  module `CMakeLists.txt` appending to `anoptic_core` via
  `target_sources`, platform selection `if(WIN32)/elseif(APPLE)/elseif(UNIX)`
  (the `src/time/` pattern), one `add_subdirectory` line in the top-level
  registration block. Platform link deps: macOS adds
  `-framework CoreAudio AudioToolbox AudioUnit`; win64 adds `ole32` (and
  `avrt` for MMCSS); Linux adds nothing (dlopen).
- Private transport header `src/audio/audio_bridge.h` copies `AnoSpscRing` +
  seqlock from the render bridge verbatim (same alignment discipline, same
  migrate-to-collections note).

---

## 3. Layer 1: anoptic_audio

### 3.1 Device layer

One function-pointer table per backend, selected at init (env override
`ANO_AUDIO_BACKEND` for testing, `null` always available):

```c
// src/audio/audio_device.h (private)
typedef struct AnoAudioDevice {
    bool (*open)(AnoAudioDeviceConfig *cfg);   // negotiate f32/48k/stereo, request 512-frame period
    void (*close)(void);
    // device thread drains the cooked-block ring itself; open() wires it
} AnoAudioDevice;
```

Negotiation policy: request f32 interleaved stereo 48 kHz, 512-frame period;
accept what the OS grants and report the granted period in telemetry. Device
loss / default-device change: backend signals an atomic flag; the mixer thread
closes and reopens between blocks and emits `AEVT_DEVICE`. The engine never
resamples for the device except in the raw-ALSA fallback (one f32→s16 loop).

### 3.2 Mixer: buses, sources, spatialization

- Bus graph is fixed at init from a data-driven config: bus count, parent
  routing, insert chains, sends. Default game layout: `MASTER` ← {`SFX`, `UI`,
  `AMBIENT`, `MUSIC`}; the music console instantiates its six layer strips,
  two send buses, and console master chain as children of `MUSIC` (its
  compressor/limiter live there; `MASTER` carries its own safety limiter +
  dither). Structural graph change is a rebuild (TECH_SPEC §10.1's structural
  class); runtime change is parameter retargets and source attach/detach only.
- Sources: a preallocated pool (e.g. 256) of sampler-style players — buffer,
  frame cursor, rate (pitch), gain, pan or world position, loop flag, bus.
  One-shot SFX is a source that auto-retires; ambient loops are the same
  source looping. Every audible parameter retargets through a one-pole.
- Directional audio v0: sources with a world position are panned
  constant-power from listener-relative azimuth, attenuated by clamped
  inverse-distance (per-source rolloff/min/max), with an optional one-pole
  air-absorption lowpass driven by distance. Listener pose arrives by seqlock,
  sampled once per block, smoothed. Deliberate hooks, not features: per-source
  rate is already the doppler seam; the pan stage is the HRTF seam. Neither
  ships in v0.
- Effects/filters: per-bus insert slots running the shared DSP library
  (§3.3), parameter-addressable from the bridge via field-masked
  `ACMD_BUS_SET`. Send levels per source and per bus.
- Telemetry per block into the seqlock: per-bus peak/RMS, block render time
  (`ano_timestamp_ticks`), underrun count, granted device period — finding 3's
  diagnostics, kept out of offline renders by default (§12.7).

### 3.3 The DSP primitive library

`src/audio/dsp/` — plain C kernels over `float *restrict` blocks, shared by
bus inserts (audio module) and voices (synth module). Contents: exactly the
TECH_SPEC §12.2 inventory, built in dependency order (smoothers and SVF first,
FDN and limiter last). Rules, from the findings:

- Every stochastic primitive takes an explicit seed; every state struct has an
  init function that zeroes it (finding 8).
- Every buffer-position input declares clamp-or-wrap and enforces it in the
  node, loudly in debug (finding 7).
- Dynamics have specified, bounded makeup (finding 1). Detector primitives —
  asymmetric peak follower, gapless sliding-window max, linear
  ramp-in-T — are full primitives (finding 6).
- Block-feedback primitives reject loop delays shorter than one block loudly
  (finding 5).
- No `-ffast-math` anywhere in audio/synth/music TUs; `-ffp-contract=off` on
  the music module (bit-parity with Python needs stable op order); synth/audio
  determinism is per-platform golden, not cross-platform.

### 3.4 Public API sketch

```c
// include/anoptic_audio.h — platform-agnostic, ano_* only
bool ano_audio_init(const AnoAudioConfig *cfg);   // spawns mixer thread; null backend if headless
void ano_audio_shutdown(void);
AnoAudioBridge *anoAudioBridge(void);             // opaque; logic-side endpoints below

// producer endpoints (logic thread), mirroring anoptic_render.h shapes
bool ano_audio_submit(AnoAudioBridge *b, const AnoAudioCommand *cmd); // false = backpressure, retry
bool ano_audio_poll_event(AnoAudioBridge *b, AnoAudioEvent *out);
void ano_audio_publish_listener(AnoAudioBridge *b, const AnoAudioListener *l);
bool ano_audio_acquire_telemetry(AnoAudioBridge *b, AnoAudioTelemetry *out);

// offline / conformance path — same graph, no device (finding 2)
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames);
```

Buffers are loaded logic-side (WAV PCM16/f32 loader in the audio module,
converted to canonical f32/48k at load; windowed-sinc resample offline if the
file rate differs) and registered by command with an owned pointer; the audio
side adopts the block and retires it back through an event for logic-side
free, keeping frees off the audio thread. Compressed formats (vorbis/opus) are
deferred; if ever needed, stb_vorbis is the no-dep candidate.

---

## 4. Layer 2: anoptic_synth

Consumes bar batches of `AnoNoteEvent` + tempo points + the DSP-tier fields of
`AnoMusicalParams`; produces audio into the six layer buses. Private to the
audio thread at runtime; fully drivable offline.

- Voice pool: preallocated, fixed-capacity per voice class; allocate = state
  flip (finding 9). A voice is `{class, const patch *, phase/env/filter
  state, startFrame, endFrame}` — patch resolved at allocation, keytracking
  baked then, amplitude `(velocity/127)^1.5`, per-layer shared control nodes
  (cutoff smoother) fanning out to sounding voices.
- Patches are data: one table per voice class holding every constant from the
  prototype's patch set (warm/bright pads, sub-bass, delayed-vibrato leads,
  2-op FM arp, wavetable morph pad, bell sampler, granular shimmer, GM-keyed
  drums, environmental textures). Tuning, not architecture — retuning never
  touches code.
- Scheduler: per-bar events convert beats→frames through the BeatClock (the
  piecewise tempo map, ported as specified in §11.1) into a deadline-sorted
  array with a stable sequence tiebreaker. The block renderer steps sub-blocks
  that stop at every event frame and every voice end frame — sample-accurate
  in both realtime and offline paths, which also closes the prototype's
  documented realtime tie-rearticulation gap (merged tie chains become one
  voice with one envelope everywhere).
- Hardware optimization strategy: structure first, intrinsics later. Voices
  render per-class in SoA batches (contiguous phase/env/coefficient arrays)
  through `restrict`-clean block kernels sized for autovectorization; measure;
  hand-write AVX2/NEON only for kernels that prove hot. Premature intrinsics
  are how the patch set stops being data. The budget argues for restraint: the
  prototype's full stack renders ~10× realtime in Python-hosted signalflow;
  a straight C port is unlikely to threaten the frame budget before SIMD.
- Console: instantiated as bus configuration (§3.2), not synth-internal
  plumbing. The synth owns only what is per-voice; strips, sends, ducking, the
  mod matrix, and one-shot cutoff sweeps ride the audio module's insert and
  retarget machinery.
- Testing seam: the prototype grows a small IR exporter (one JSON-line per
  event, per bar, plus tempo points and params — the flat textdump mode is the
  starting point). The C synth renders exported fixtures offline long before
  the music module exists, and the result is listenable and diffable. Mid-layer
  conformance harness for the stack, and the reason synth precedes musicgen in
  the build order.

---

## 5. Layer 3: anoptic_music

The TECH_SPEC port, placed: `src/music/` mirrors the prototype's L0–L3 layering (theory kernel, IR, generators, conductor/orchestration) with the parallel consumers (linter adapter, textdump, trace) in dev builds. This document only adds the engine-integration decisions:

- Hosting: the conductor lives behind the pull API and is driven at bar edges
  by the audio thread's block loop (§2.1). Control commands are drained from
  the bridge at block boundaries and applied at the boundaries TECH_SPEC §9.3
  quantizes them to.
- Memory: one preallocated per-bar arena (fixed block, reset each
  `advance_bar`) for events, traces, and scratch; sequential state in fixed
  structs; phrase caches as `phrase % W` ring buffers. No general-heap traffic
  per note. This is TECH_SPEC §15's guidance made concrete, and it needs no
  engine arena module — a fixed buffer with a cursor suffices.
- Determinism infrastructure: self-contained BLAKE2b-8 (RFC 7693 reference,
  ~200 lines), MT19937 with CPython `init_by_array` seeding, and the exact
  `random/randint/choice/choices` draw semantics; banker's rounding helpers;
  the stream-key registry with byte-exact key spellings. Phase 1 targets
  bit-compatible `raw_events` against Python goldens; `gauss` (Humanize) can
  wait — it sits on the post-modifier surface only.
- Conformance: IR serialization out of dev builds, a thin Python adapter
  rehydrating prototype IR objects, and the full lint family as the oracle,
  per §14. CI gates: double-render bit-identity, every-flag-off byte-identity,
  churned-heap audio render bit-diff.

---

## 6. The bridge

One `AnoAudioBridge`, mirroring `AnoRenderBridge` field for field:

```c
struct AnoAudioBridge {                    // src/audio/audio_bridge.h (private)
    AnoSpscRing commands;                  // logic -> audio (AnoAudioCommand, POD, copied by value)
    AnoSpscRing events;                    // audio -> logic (AnoAudioEvent, <= 32 bytes, static-asserted)
    AnoAudioListener  listener;  _Alignas(ANO_CACHE_LINE) _Atomic uint64_t listenerVersion;  // logic publishes
    AnoAudioTelemetry telemetry; _Alignas(ANO_CACHE_LINE) _Atomic uint64_t telemetryVersion; // audio publishes
};
```

Command protocol (public, `anoptic_audio.h`):

```c
typedef enum AnoAudioCommandKind {
    // sources and buffers
    ACMD_SOURCE_PLAY,        // buffer id, bus, gain, pan | world pos, rate, loop; source id logic-allocated
    ACMD_SOURCE_UPDATE,      // field-masked retargets (AFIELD_GAIN | AFIELD_POSITION | ...)
    ACMD_SOURCE_STOP,        // immediate or release-tail
    ACMD_BUFFER_REGISTER,    // owned f32 block, adopted by audio side
    ACMD_BUFFER_RELEASE,     // slot dies; block retired back via AEVT for logic-side free
    // buses
    ACMD_BUS_SET,            // gain / insert params / send levels, field-masked
    // music control plane (TECH_SPEC §9-§10; applied at quantization boundaries)
    ACMD_MUSIC_TRANSPORT,    // start / stop / seek(bar) / reseed(seed) — structural, deterministic rebuild
    ACMD_MUSIC_AFFECT,       // valence, energy, tension, urgent
    ACMD_MUSIC_OVERRIDE,     // set/clear, param id, typed value
    ACMD_MUSIC_REQUEST_KEY,  // tonic pc, urgent
    ACMD_MUSIC_REQUEST_MOTIF,// tag id
    ACMD_MUSIC_CONFIG,       // owned blob: MappingTable / config sub-object hot-swap at next bar edge
} AnoAudioCommandKind;
```

Event protocol:

```c
typedef enum AnoAudioEventKind {
    AEVT_SOURCE_RETIRED,     // one-shot finished or stop completed; returns source id
    AEVT_BUFFER_RETIRED,     // released buffer block handed back for free
    AEVT_CAPACITY,           // event ring pressure advisory (samples were dropped)
    AEVT_DEVICE,             // device lost / default changed / period changed
    AEVT_MUSIC_BAR,          // lossless bar-edge marker: bar, key, chord sym id, cadence flags, tempo
    AEVT_MUSIC_MARK,         // dramaturg spend, cadence arrival, key-change arrival, motif landed
} AnoAudioEventKind;
```

Rules, inherited verbatim from the render bridge:

- `ano_audio_submit` returning false is backpressure: retain and retry next
  tick, never drop. The event ring is best-effort for coalescible samples with
  `AEVT_CAPACITY` advisories — but `AEVT_SOURCE_RETIRED`, `AEVT_BUFFER_RETIRED`
  and `AEVT_MUSIC_BAR` are facts the logic side must not miss, so the mixer
  retries them at subsequent block boundaries until they land.
- POD commands are copied by value at submit; fat payloads (buffer data,
  config blobs) are packed into one `mi_malloc` block at submit, adopted by
  the consumer; frees happen logic-side via retirement events except for
  config blobs, freed at the bar edge that consumes them.
- Continuous state never rides the rings: listener pose down, telemetry
  (playhead beat as a double, bar, tempo, phrase position, key/mode, sounding
  chord id, bus meters, block CPU, underruns) up, both latest-wins seqlocks
  published once per block. Gameplay that wants beat-synced visuals reads the
  telemetry; gameplay that must not miss a bar reacts to `AEVT_MUSIC_BAR`.

This bridge is the Public API of the entire stack: the logic thread never links against synth or music internals, only `anoptic_audio.h` commands and events — the same isolation the render bridge gives the renderer, and the integration seam TECH_SPEC §9.1 mandates (affect in, telemetry out, no game-semantic inputs below this line).

---

## 7. Build sequence

Ordered so every phase has a runnable, testable exit criterion; device
backends and DSP work parallelize freely after phase 0.

- Phase 0 — scaffolding. Module skeletons, headers, CMake registration; the
  private ring/seqlock copies; mixer thread + block loop against the null
  device; `ano_audio_render_offline` writing WAV. Exit: headless test renders
  a smoothed sine through a bus to a byte-stable WAV, twice, bit-identical,
  on a churned heap.
- Phase 1 — first sound. PipeWire backend (dev machine). Exit: audible tone
  and a WAV one-shot triggered over the bridge from the logic thread, with
  underrun-free steady state and telemetry visible logic-side.
- Phase 2 — mixer feature-complete. Source pool, WAV loader + registration
  round-trip, buses/inserts/sends from config, constant-power spatialization +
  listener seqlock, `ACMD_BUS_SET` retargeting, master safety limiter. Exit: a
  demo scene plays positioned one-shots and a looping ambient bed with a
  moving listener; a filter sweep commanded from logic glides without zipper.
- Phase 3 — DSP library complete. The full §12.2 inventory with unit tests
  per primitive (impulse/step responses pinned as goldens; property tests for
  the detectors and limiter). Exit: the console topology instantiates from
  config and processes pink noise through strips → sends → master to a pinned
  golden.
- Phase 4 — synth. IR types in `anoptic_music.h`; prototype IR exporter; voice
  pool, BeatClock, scheduler, per-class kernels, patch tables. Exit: a
  Python-exported journey-demo IR fixture renders offline through the C synth
  to a listenable WAV; tie chains render as single voices; double-render
  bit-identical.
- Phase 5 — WASAPI and AUHAL backends, in either order. Exit per platform:
  phase 2's demo runs natively, device unplug/replug and default-device change
  survive, granted period logged.
- Phase 6 — musicgen port per TECH_SPEC's own phased conformance (§8.4, §14):
  bit-compatible core → Python-linted acceptance matrix → re-baseline. Exit:
  the acceptance matrix is green against the Python oracle in CI.
- Phase 7 — integration. Conductor driven at bar edges on the audio thread,
  full control plane over the bridge, `AEVT_MUSIC_BAR`/telemetry consumed by a
  demo that steers affect from gameplay input. Exit: the three-minute
  background-listen test, steered live, with save/seek via deterministic
  reconstruction.

Rough scale: audio module 4–6k lines, synth 5–8k, music port on the order of
the prototype's ~10k. Phases 0–3 are pure-engine work with no musicgen
dependency and immediately give the engine general SFX capability.

## 8. Risks and open questions

- Device matrix ownership is the price of option A: Bluetooth rate switches,
  `AUDCLNT_E_DEVICE_INVALIDATED` reopen loops, PipeWire quantum negotiation.
  Mitigations: the reopen-between-blocks design confines all of it to one file
  per platform; miniaudio/sokol are reference implementations; option C
  remains available per-platform without API churn.
- Audio-thread hosting of `advance_bar` assumes µs-scale bars hold in C under
  worst-case phrase machinery. Measured per-bar time ships in telemetry from
  phase 7's first build; the escape hatch (generation on another thread, one
  bar ahead) is a driver swap by design.
- CPython RNG bit-parity (rejection-sampling `randrange`, order-sensitive
  `choices`) is fiddly; it is confined to `src/music/rng.c` and validated by
  byte-diffing `raw_events` before anything audible depends on it.
- Cross-platform float determinism is deliberately not promised for DSP;
  per-platform goldens plus RMS cross-checks (the ui-render
  reference-evaluator precedent) keep it honest. The generation core, being
  integer/branch logic over pinned float op order, does target cross-platform
  identity.
- Sample rate: engine fixes 48 kHz; prototype conformance goldens are
  44.1 kHz. The synth and DSP take rate as an init parameter (all §12.2
  constants are specified in time units, not samples) — offline conformance
  runs at 44.1 kHz, the live engine at 48 kHz.
- Sandbox/CI cannot open audio devices; every gate through phase 4 runs on the
  null device and offline renders by construction. Hardware verification
  (PipeWire bring-up onward) happens on the desktop, like renderer HW passes.

## 9. Deferred / out of scope

HRTF and doppler (seams reserved in the pan and rate stages); compressed SFX
formats (stb_vorbis noted as the no-dep candidate); resource-manager
integration for buffer streaming (buffers ride the bridge until Step 6 lands);
promotion of the SPSC ring/seqlock into `anoptic_collections.h` (do it when
the audio bridge becomes their second consumer, per the render bridge's
migrate-later note); `foreshadow()` and multi-bar signatures (TECH_SPEC §16 —
nothing here precludes them); exclusive-mode / low-latency-pro paths
(IAudioClient3 small periods are an opportunistic bonus, never a requirement).
