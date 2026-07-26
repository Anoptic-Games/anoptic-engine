# AUDIO_PLAN 〜 The Anoptic Audio Stack

Plan for the engine audio stack: the general audio module, the synthesizer, the musicgen port, and the logic-thread bridge. Goes with TECH_SPEC.md (musicgen port contract) and THEORY_SPEC.md (musical rules). On music-system contract conflict, TECH_SPEC wins. This document covers what TECH_SPEC left to the engine: device backends, module boundaries, thread topology, bridge protocol.

Inputs: TECH_SPEC.md §12 (audio-library requirements probe, nine findings), musicgen prototype at `~/Documents/anoptic-musicgen` (stdlib-only Python generation core; signalflow synth is the requirements probe, not a port target), render bridge (`src/render_bridge/`), July-2026-verified survey of pure-C cross-platform audio output.

---

## 1. Findings

### 1.1 What musicgen demands of the stack

Distilled from TECH_SPEC §11–§12 and the prototype:

- Block-based realtime renderer: block 512 ≈ 11 ms at 48 kHz control cadence; commands at block boundaries; bar-level music decisions at bar edges. Offline: sub-blocks stop at every event boundary (sample-accurate, not block-quantized).
- Audio thread owns the DSP graph (finding 9). Structural change = queued commands at block boundaries. Voice allocation = state flip in a preallocated pool, never a graph edit. No allocation, no locks on the audio thread.
- Fixed DSP primitive inventory (§12.2): band-limited oscillators, seeded noise, 2D wavetables, sampler, granular, ADSR/ASR, SVF, biquad EQ, DC blocker, variable single-tap delay, comb, allpass, Householder 4-line FDN, chorus, ping-pong, tanh saturator, feedback compressor with fixed makeup, lookahead limiter, hard clip, constant-power pan, mid/side width, TPDF dither, one-pole smoothing on every audible parameter.
- Console topology (§12.4): per-layer strips → dry sum; two send buses (FDN reverb, tempo-synced ping-pong); master chain drive → compressor → limiter → clip → dither. Config of a general bus system, not a special-purpose machine.
- Voices = preset parameter blocks resolved at allocation (finding 4): fixed topology per voice class, constants from patch data, no per-voice branching in the render loop. Six layers, energy-tiered patches, synthesized drums.
- Two smoothing tiers (§12.5): conductor slews musically; audio side glides retargets through one-poles (20–45 ms). Audio library never sees per-sample automation from above.
- Determinism gate (§12.7): seeded stochastic primitives, initialized DSP state, stable scheduling tiebreakers, churned-heap double-render bit-diff CI gate.
- Generation is single-threaded, µs per bar. One thread generates, mutates, and renders between blocks (prototype shape). TECH_SPEC §11.4: audio thread sole graph owner with block-boundary command queue (render-bridge pattern).

### 1.2 What the engine has today

- No audio code yet: no `include/anoptic_audio.h`, no `src/audio/`, no build-sequence slot in docs/TODO.md. First audio module.
- Render bridge is the shipped bridge template (`src/render_bridge/render_bridge.h`): bounded lock-free SPSC ring (`AnoSpscRing`, cursors `_Alignas(ANO_THREAD_LINE)`), latest-wins seqlock (`ano_seqpub_store/load`), copy-at-submit for POD commands, owned-`mi_malloc`-block for fat payloads (consumer frees), backpressure-retry on command overflow, best-effort + capacity advisory on the event ring. Design rule from `anoptic_render.h`: discrete lossless facts on command/event ring; continuous latest-wins state on published double buffer.
- `include/anoptic_collections.h` is an empty stub; generic lock-free collections not landed. Render bridge keeps private ring/seqlock copies with a migrate-later note. Audio bridge does the same; second consumer. Promote both into `anoptic_collections.h` later.
- No arena API in memory: primitives are mimalloc heaps (`mi_heap_new` + `LOCALHEAPATTR`), `ano_aligned_malloc`, `ANO_CACHE_LINE`/`ANO_THREAD_LINE`. Render bridge rings from a dedicated `mi_heap_t`; audio stack does the same and builds fixed pools on top.
- Threads: `ano_thread_create/join` wrap pthreads (winpthreads on win64, shim on macOS). Runtime today: three threads (main/render, logic via `anoLogicThreadMain` ~2 ms tick / sole render-command producer, logger drain). Audio mixer is the fourth, spawned/joined like the logic thread, shut down before its bridge is destroyed.
- Time: `ano_timestamp_ticks` + `ano_ticks_to_ns` for hot-path block timing, `ano_sleep` for pacing. Logging: `ano_log` enqueue is lock-free and audio-thread-safe; `ANO_NOW` (synchronous flush) is not.

### 1.3 Backend landscape (verified July 2026)

Facts checked against primary sources this week:

- miniaudio 0.11.25 (2026-03): active, public-domain/MIT-0, compiles to device-I/O-only (`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_DECODING`, …, custom alloc callbacks). Data path callback-driven and lock-free; control path holds internal mutexes. No native PipeWire (Linux rides pulse-compat with forced 25 ms default buffer); none in unreleased 0.12. One 96k-line foreign TU.
- libsoundio abandoned (last real release 2019, last commit mid-2023). PortAudio maintained but mid-weight; no IAudioClient3 low-latency shared mode, no native PipeWire. SDL3 audio excellent and native-PipeWire but is a framework (charter-excluded). sokol_audio too thin to adopt (f32 stereo only, no device selection); excellent reference reading.
- Hand-rolled pure-C backends are proven (miniaudio, sokol, libsoundio): WASAPI via COM-from-C (`lpVtbl`), IAudioClient3 event-driven shared mode (periods 128–480 frames driver-dependent, classic 480/10 ms floor), `IMMNotificationClient` for default-device change; CoreAudio AUHAL pure-C API, 128–512-frame buffers routine, property listener for device change; Linux 2026 = PipeWire native `pw_stream` (`PW_STREAM_FLAG_RT_PROCESS` keeps process callback lock-free; `PW_KEY_NODE_LATENCY` requests quantum; dlopen `libpipewire-0.3`, no link-time dep) + dlopen'd ALSA fallback for headless/non-PipeWire. PipeWire is default server on every major distro. Effort estimate: WASAPI 800–1500 lines, AUHAL 500–900, PipeWire + ALSA 1100–1600.
- Format policy (no owned device resampler): engine mixer fixed at f32 interleaved 48 kHz; WASAPI shared mixes at engine rate, AUHAL converts client→device, PipeWire graph resamples. Only raw-ALSA fallback needs one f32→s16 conversion loop. Safe latency budget all platforms: 512–1024 frames total buffering (~10–21 ms); go lower opportunistically, never as a requirement.

### 1.4 Backend decision

Ranked options:

- A 〜 hand-rolled per-platform backends (recommended). `audio_linux.c` (pw_stream native + ALSA fallback, both dlopen'd), `audio_win64.c` (WASAPI/IAudioClient3), `audio_macos.c` (AUHAL), `audio_null.c` (headless / CI / offline). Only option native-PipeWire today, allocates through engine heaps, keeps engine code mutex-free, maps 1:1 onto `src/<mod>/` per-platform convention. Cost: own the device testing matrix (hotplug, invalidation loops, Bluetooth rate switches); crib negotiation/reroute shapes from sokol_audio and miniaudio.
- C 〜 hybrid sequencing: vendored device-only miniaudio behind `anoptic_audio.h` first, replaced platform-by-platform by A (PipeWire first, where miniaudio is weakest), keep miniaudio in tests as device-layer A/B oracle. Zero API churn; adopt only if A's Windows/macOS bring-up stalls.
- B 〜 vendored miniaudio permanently: decade of hardening on day one, but 96k-line TU, internal mutexes, no native PipeWire. Not chosen.

Decision: A, sequenced PipeWire (dev machine) → null/offline (CI) → WASAPI → AUHAL. Public header never names a backend; callers cannot tell which was built (platform-abstraction charter). Device layer is deliberately dumb: open, negotiate f32/48k, pull blocks from a ring, report state. Later swap of any single backend (or temporary miniaudio shim) touches one file.

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

- Mixer thread is engine-owned (`ano_thread_create`), runs the block loop, sole owner of every audio data structure. Spawned by `ano_audio_init`, joined by `ano_audio_shutdown`, before the bridge dies.
- Device backends only touch the cooked-block SPSC ring (3–4 blocks of f32 stereo, ~4 KiB each). Normalizes all backends behind one shape, isolates OS callback quirks from the graph, makes headless/offline the same code minus the device. Cost: one block of latency (TECH_SPEC §11.4 shape).
- Pacing: mixer produces when the block ring has space; `ano_sleep` ~1 ms when full. Lock-free, no condvar. If wakeup latency matters, copy the logger's condvar drainer pattern.
- Musicgen conductor runs on the audio thread at bar edges from the block loop. Generation is µs/bar vs 11 ms budget; one thread generates, schedules, and renders (prototype contract, roles renamed). No cross-thread event shipping for bar placement. Pull-based generation core: hoist to another thread later is a driver change only.
- End-to-end SFX latency: logic tick (≤2 ms) + next block boundary (≤11 ms) + block ring (1–2 blocks) + OS period ≈ 25–45 ms typical. Acceptable for game SFX; block 256 is the shrink lever if needed.
- Allocation: all pools (voices, sources, buses, rings, per-bar arena) preallocated at init from a dedicated `mi_heap_t` audio heap. Block-loop steady state: zero allocation. Rare control-path exceptions, documented: freeing an adopted config blob after applying it at a bar edge (render-bridge consumer-frees rule).
- Locks: none in engine audio code. OS-internal control-path mutexes (pw_thread_loop lock, WASAPI COM) confined to `audio_<platform>.c` (same exception class as Vulkan backend).

### 2.2 Module layout

Three modules plus the bridge, same layout as the rest of the engine:

| Layer | Public header | Source | Depends on |
|---|---|---|---|
| audio (device, mixer, buses, SFX, spatial, DSP lib, bridge) | `include/anoptic_audio.h` | `src/audio/` | memory, threads, time, logging |
| synth (voice pool, patches, scheduler, console config) | `include/anoptic_synth.h` | `src/synth/` | audio (buses, DSP lib), music (IR types) |
| music (generation core port per TECH_SPEC) | `include/anoptic_music.h` | `src/music/` | nothing below the stdlib shims |

- `anoptic_music.h` first: holds the IR (`AnoNoteEvent`, `AnoBarResult`, `AnoHarmonicContext`, `AnoMusicalParams`, tempo points, layer/tie enums) even before music has an implementation. IR is the authoritative schema per TECH_SPEC §4 and the synth's input type. Header-level dependency only; no link inversion.
- Each module: common `ano_<mod>.c`, private headers inside `src/<mod>/`, module `CMakeLists.txt` appending to `anoptic_core` via `target_sources`, platform selection `if(WIN32)/elseif(APPLE)/elseif(UNIX)` (`src/time/` pattern), one `add_subdirectory` in the top-level registration block. Platform link deps: macOS `-framework CoreAudio AudioToolbox AudioUnit`; win64 `ole32` (and `avrt` for MMCSS); Linux nothing (dlopen).
- Private transport header `src/audio/audio_bridge.h` copies `AnoSpscRing` + seqlock from the render bridge verbatim (same alignment discipline, same migrate-to-collections note).

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

Negotiation policy: request f32 interleaved stereo 48 kHz, 512-frame period; accept what the OS grants; report granted period in telemetry. Device loss / default-device change: backend signals an atomic flag; mixer closes and reopens between blocks and emits `AEVT_DEVICE`. Engine never resamples for the device except raw-ALSA fallback (one f32→s16 loop).

### 3.2 Mixer: buses, sources, spatialization

- Bus graph fixed at init from data-driven config: bus count, parent routing, insert chains, sends. Default game layout: `MASTER` ← {`SFX`, `UI`, `AMBIENT`, `MUSIC`}; music console instantiates six layer strips, two send buses, and console master chain as children of `MUSIC` (compressor/limiter live there; `MASTER` carries its own safety limiter + dither). Structural graph change = rebuild (TECH_SPEC §10.1 structural class); runtime change = parameter retargets and source attach/detach only.
- Sources: preallocated pool (e.g. 256) of sampler-style players: buffer, frame cursor, rate (pitch), gain, pan or world position, loop flag, bus. One-shot SFX = source that auto-retires; ambient loops = same source looping. Every audible parameter retargets through a one-pole.
- Directional audio v0: world-position sources panned constant-power from listener-relative azimuth, attenuated by clamped inverse-distance (per-source rolloff/min/max), optional one-pole air-absorption lowpass by distance. Listener pose via seqlock, sampled once per block, smoothed. Hooks reserved, not shipped in v0: per-source rate = doppler seam; pan stage = HRTF seam.
- Effects/filters: per-bus insert slots running the shared DSP library (§3.3), parameter-addressable from the bridge via field-masked `ACMD_BUS_SET`. Send levels per source and per bus.
- Telemetry per block into the seqlock: per-bus peak/RMS, block render time (`ano_timestamp_ticks`), underrun count, granted device period. Finding 3 diagnostics; kept out of offline renders by default (§12.7).

### 3.3 The DSP primitive library

`src/audio/dsp/`: plain C kernels over `float *restrict` blocks, shared by bus inserts (audio) and voices (synth). Contents: exactly TECH_SPEC §12.2 inventory, built in dependency order (smoothers and SVF first, FDN and limiter last). Rules from the findings:

- Every stochastic primitive takes an explicit seed; every state struct has an init that zeroes it (finding 8).
- Every buffer-position input declares clamp-or-wrap and enforces it in the node, loudly in debug (finding 7).
- Dynamics have specified, bounded makeup (finding 1). Detector primitives (asymmetric peak follower, gapless sliding-window max, linear ramp-in-T) are full primitives (finding 6).
- Block-feedback primitives reject loop delays shorter than one block loudly (finding 5).
- No `-ffast-math` in audio/synth/music TUs; `-ffp-contract=off` on music (bit-parity with Python needs stable op order); synth/audio determinism is per-platform golden, not cross-platform.

### 3.4 Public API sketch

```c
// include/anoptic_audio.h: platform-agnostic, ano_* only
bool ano_audio_init(const AnoAudioConfig *cfg);   // spawns mixer thread; null backend if headless
void ano_audio_shutdown(void);
AnoAudioBridge *anoAudioBridge(void);             // opaque; logic-side endpoints below

// producer endpoints (logic thread), mirroring anoptic_render.h shapes
bool ano_audio_submit(AnoAudioBridge *b, const AnoAudioCommand *cmd); // false = backpressure, retry
bool ano_audio_poll_event(AnoAudioBridge *b, AnoAudioEvent *out);
void ano_audio_publish_listener(AnoAudioBridge *b, const AnoAudioListener *l);
bool ano_audio_acquire_telemetry(AnoAudioBridge *b, AnoAudioTelemetry *out);

// offline / conformance path: same graph, no device (finding 2)
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames);
```

Buffers loaded logic-side (WAV PCM16/f32 loader in audio module, converted to canonical f32/48k at load; windowed-sinc resample offline if file rate differs) and registered by command with an owned pointer; audio side adopts the block and retires it back through an event for logic-side free (frees stay off the audio thread). Compressed formats (vorbis/opus) deferred; stb_vorbis is the no-dep candidate if ever needed.

---

## 4. Layer 2: anoptic_synth

Consumes bar batches of `AnoNoteEvent` + tempo points + DSP-tier fields of `AnoMusicalParams`; produces audio into the six layer buses. Private to the audio thread at runtime; fully drivable offline.

- Voice pool: preallocated, fixed-capacity per voice class; allocate = state flip (finding 9). A voice is `{class, const patch *, phase/env/filter state, startFrame, endFrame}`: patch resolved at allocation, keytracking baked then, amplitude `(velocity/127)^1.5`, per-layer shared control nodes (cutoff smoother) fanning out to sounding voices.
- Patches are data: one table per voice class holding every constant from the prototype's patch set (warm/bright pads, sub-bass, delayed-vibrato leads, 2-op FM arp, wavetable morph pad, bell sampler, granular shimmer, GM-keyed drums, environmental textures). Tuning, not architecture: retuning never touches code.
- Scheduler: per-bar events convert beats→frames through the BeatClock (piecewise tempo map, ported per §11.1) into a deadline-sorted array with stable sequence tiebreaker. Block renderer steps sub-blocks that stop at every event frame and every voice end frame. Sample-accurate in realtime and offline; closes the prototype's realtime tie-rearticulation gap (merged tie chains = one voice, one envelope everywhere).
- Hardware: structure first, intrinsics later. Voices render per-class in SoA batches (contiguous phase/env/coefficient arrays) through `restrict`-clean block kernels sized for autovectorization; measure; AVX2/NEON only for kernels that prove hot. Keep patches as data. Prototype full stack ~10× realtime in Python signalflow; C port unlikely to need SIMD for budget.
- Console: bus configuration (§3.2), not synth-internal plumbing. Synth owns per-voice state only; strips, sends, ducking, mod matrix, and one-shot cutoff sweeps ride audio-module insert/retarget machinery.
- Testing seam: prototype grows a small IR exporter (one JSON-line per event, per bar, plus tempo points and params; flat textdump mode is the starting point). C synth renders exported fixtures offline before the music module exists; result listenable and diffable. Mid-layer conformance harness; synth precedes musicgen in build order.

---

## 5. Layer 3: anoptic_music

TECH_SPEC port, placed: `src/music/` mirrors the prototype's L0–L3 layering (theory kernel, IR, generators, conductor/orchestration) with parallel consumers (linter adapter, textdump, trace) in dev builds. Engine-integration decisions only:

- Hosting: conductor behind the pull API, driven at bar edges by the audio thread's block loop (§2.1). Control commands drained from the bridge at block boundaries and applied at the boundaries TECH_SPEC §9.3 quantizes them to.
- Memory: one preallocated per-bar arena (fixed block, reset each `advance_bar`) for events, traces, and scratch; sequential state in fixed structs; phrase caches as `phrase % W` ring buffers. No general-heap traffic per note. TECH_SPEC §15 made concrete; no engine arena module needed (fixed buffer + cursor).
- Determinism infrastructure: self-contained BLAKE2b-8 (RFC 7693 reference, ~200 lines), MT19937 with CPython `init_by_array` seeding, exact `random/randint/choice/choices` draw semantics; banker's rounding helpers; stream-key registry with byte-exact key spellings. Phase 1 targets bit-compatible `raw_events` against Python goldens; `gauss` (Humanize) can wait (post-modifier surface only).
- Conformance: IR serialization out of dev builds, thin Python adapter rehydrating prototype IR objects, full lint family as oracle, per §14. CI gates: double-render bit-identity, every-flag-off byte-identity, churned-heap audio render bit-diff.

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
    ACMD_MUSIC_TRANSPORT,    // start / stop / seek(bar) / reseed(seed): structural, deterministic rebuild
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

- `ano_audio_submit` returning false is backpressure: retain and retry next tick, never drop. Event ring is best-effort for coalescible samples with `AEVT_CAPACITY` advisories. `AEVT_SOURCE_RETIRED`, `AEVT_BUFFER_RETIRED`, and `AEVT_MUSIC_BAR` are facts the logic side must not miss; mixer retries them at subsequent block boundaries until they land.
- POD commands copied by value at submit; fat payloads (buffer data, config blobs) packed into one `mi_malloc` block at submit, adopted by the consumer; frees happen logic-side via retirement events except config blobs, freed at the bar edge that consumes them.
- Continuous state never rides the rings: listener pose down, telemetry (playhead beat as double, bar, tempo, phrase position, key/mode, sounding chord id, bus meters, block CPU, underruns) up, both latest-wins seqlocks published once per block. Beat-synced visuals read telemetry; bar-edge reactions use `AEVT_MUSIC_BAR`.

This bridge is the public API of the entire stack: logic thread never links against synth or music internals, only `anoptic_audio.h` commands and events. Same isolation the render bridge gives the renderer; integration seam TECH_SPEC §9.1 mandates (affect in, telemetry out, no game-semantic inputs below this line).

---

## 7. Build sequence

Ordered so every phase has a runnable, testable exit criterion. Device backends and DSP work parallelize freely after phase 0.

- Phase 0 〜 scaffolding. Module skeletons, headers, CMake registration; private ring/seqlock copies; mixer thread + block loop against null device; `ano_audio_render_offline` writing WAV. Exit: headless test renders a smoothed sine through a bus to a byte-stable WAV, twice, bit-identical, on a churned heap.
- Phase 1 〜 first sound. PipeWire backend (dev machine). Exit: audible tone and a WAV one-shot triggered over the bridge from the logic thread; underrun-free steady state; telemetry visible logic-side.
- Phase 2 〜 mixer feature-complete. Source pool, WAV loader + registration round-trip, buses/inserts/sends from config, constant-power spatialization + listener seqlock, `ACMD_BUS_SET` retargeting, master safety limiter. Exit: demo scene plays positioned one-shots and a looping ambient bed with a moving listener; filter sweep commanded from logic glides without zipper.
- Phase 3 〜 DSP library complete. Full §12.2 inventory with unit tests per primitive (impulse/step responses pinned as goldens; property tests for detectors and limiter). Exit: console topology instantiates from config and processes pink noise through strips → sends → master to a pinned golden.
- Phase 4 〜 synth. IR types in `anoptic_music.h`; prototype IR exporter; voice pool, BeatClock, scheduler, per-class kernels, patch tables. Exit: Python-exported journey-demo IR fixture renders offline through the C synth to a listenable WAV; tie chains render as single voices; double-render bit-identical.
- Phase 5 〜 WASAPI and AUHAL backends, in either order. Exit per platform: phase 2's demo runs natively; device unplug/replug and default-device change survive; granted period logged.
- Phase 6 〜 musicgen port per TECH_SPEC's own phased conformance (§8.4, §14): bit-compatible core → Python-linted acceptance matrix → re-baseline. Exit: acceptance matrix green against the Python oracle in CI.
- Phase 7 〜 integration. Conductor driven at bar edges on the audio thread, full control plane over the bridge, `AEVT_MUSIC_BAR`/telemetry consumed by a demo that steers affect from gameplay input. Exit: three-minute background-listen test, steered live, with save/seek via deterministic reconstruction.

Rough scale: audio module 4–6k lines, synth 5–8k, music port on the order of the prototype's ~10k. Phases 0–3 are pure-engine work with no musicgen dependency and immediately give the engine general SFX capability.

## 8. Risks and open questions

- Device matrix ownership is the price of option A: Bluetooth rate switches, `AUDCLNT_E_DEVICE_INVALIDATED` reopen loops, PipeWire quantum negotiation. Mitigations: reopen-between-blocks confines it to one file per platform; miniaudio/sokol are reference implementations; option C remains available per-platform without API churn.
- Audio-thread hosting of `advance_bar` assumes µs-scale bars hold in C under worst-case phrase machinery. Measured per-bar time ships in telemetry from phase 7's first build; escape hatch (generation on another thread, one bar ahead) is a driver swap by design.
- CPython RNG bit-parity (rejection-sampling `randrange`, order-sensitive `choices`) is fiddly; confined to `src/music/rng.c`, validated by byte-diffing `raw_events` before anything audible depends on it.
- Cross-platform float determinism deliberately not promised for DSP; per-platform goldens plus RMS cross-checks (ui-render reference-evaluator precedent). Generation core (integer/branch logic over pinned float op order) does target cross-platform identity.
- Sample rate: engine fixes 48 kHz; prototype conformance goldens are 44.1 kHz. Synth and DSP take rate as an init parameter (all §12.2 constants specified in time units, not samples). Offline conformance at 44.1 kHz; live engine at 48 kHz.
- Sandbox/CI cannot open audio devices; every gate through phase 4 runs on the null device and offline renders by construction. Hardware verification (PipeWire bring-up onward) happens on the desktop, like renderer HW passes.

## 9. Deferred / out of scope

HRTF and doppler (seams reserved in pan and rate stages); compressed SFX formats (stb_vorbis noted as no-dep candidate); resource-manager integration for buffer streaming (buffers ride the bridge until Step 6 lands); promotion of SPSC ring/seqlock into `anoptic_collections.h` (when audio bridge becomes their second consumer, per render bridge's migrate-later note); `foreshadow()` and multi-bar signatures (TECH_SPEC §16; nothing here precludes them); exclusive-mode / low-latency-pro paths (IAudioClient3 small periods are opportunistic bonus, never a requirement).
