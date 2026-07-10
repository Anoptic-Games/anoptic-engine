/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_audio.h
 * @brief Public engine<->audio contract: the audio-stack lifecycle the engine
 *        entry point drives, plus the logic->audio command protocol it produces.
 *
 * The audio world runs on its own mixer thread — the sole owner of every audio
 * data structure (sources, buses, DSP state). Structural change arrives only as
 * commands applied at block boundaries; the block cadence (default 512 frames
 * at 48 kHz, ~11 ms) is the control granularity. The logic master is the sole
 * command producer and reaches the mixer through the opaque AnoAudioBridge
 * handle below. The transport (lock-free SPSC rings, seqlock lanes, the bridge
 * struct) is private to src/audio/ and never exposed here.
 *
 * Standing rule (inherited from the render bridge): discrete lossless facts
 * ride a command/event ring; continuous latest-wins state (listener pose down,
 * telemetry up) rides a published double buffer.
 *
 * Device backends are selected inside ano_audio_init and never leak through
 * this header. Phase 0 ships the null backend only (headless pacing); PipeWire/
 * ALSA, WASAPI/DirectSound, and CoreAudio arrive in later phases behind the
 * same contract.
 */

#ifndef ANOPTIC_AUDIO_H
#define ANOPTIC_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Fixed shape
// ---------------------------------------------------------------------------

// The mixer renders interleaved f32 stereo. Everything upstream of the device
// negotiation speaks this format; backends convert only where the OS cannot.
#define ANO_AUDIO_CHANNELS 2

// Compile-time pool ceilings. Pools are preallocated at init ("allocate" at
// runtime is a state flip, never a heap call on the mixer thread).
#define ANO_AUDIO_MAX_BUSES   8
#define ANO_AUDIO_MAX_SOURCES 64

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Device backend selection. AUTO picks the best backend the build and machine
// support (Phase 0: the null backend). NULL_DEV consumes cooked blocks at the
// nominal block cadence without a device — headless runs, CI, tests.
typedef enum AnoAudioBackend
{
    ANO_AUDIO_BACKEND_AUTO = 0,
    ANO_AUDIO_BACKEND_NULL_DEV,
} AnoAudioBackend;

// Init-time configuration. Zero any field for its default.
typedef struct AnoAudioConfig
{
    uint32_t sampleRate;   // engine mix rate; default 48000
    uint32_t blockFrames;  // frames per mix block; default 512, clamped [32, 4096]
    uint32_t deviceBlocks; // cooked-block ring depth toward the device; default 4
    uint32_t cmdCapacity;  // command ring capacity (rounded up to pow2); default 1024
    uint32_t evtCapacity;  // event ring capacity (rounded up to pow2); default 1024
    uint32_t busCount;     // buses including master (bus 0); default 2, max ANO_AUDIO_MAX_BUSES
    uint32_t backend;      // AnoAudioBackend; default AUTO
} AnoAudioConfig;

// Bring up the audio world: allocates every pool from a dedicated heap, opens
// the device backend, spawns the mixer thread. NULL cfg means all defaults.
// false on failure (nothing is left running). Not thread-safe; call from the
// engine entry point before the logic thread starts producing.
bool ano_audio_init(const AnoAudioConfig *cfg);

// Tear the audio world down: joins the mixer and device threads, destroys the
// bridge, releases the heap. Call after the logic thread has stopped producing.
void ano_audio_shutdown(void);

// ---------------------------------------------------------------------------
// Bridge handle
// ---------------------------------------------------------------------------

// Opaque logic<->audio channel. Created inside ano_audio_init, destroyed in
// ano_audio_shutdown. The producer holds it only to submit commands and poll
// events; the transport is defined privately in src/audio/.
typedef struct AnoAudioBridge AnoAudioBridge;

// Producer endpoint. Valid once ano_audio_init has returned; NULL before.
AnoAudioBridge *anoAudioBridge(void);

// ---------------------------------------------------------------------------
// Command protocol: logic -> audio
// ---------------------------------------------------------------------------

// Source generator kinds. Phase 0 ships the procedural tone (the bring-up and
// test oscillator); buffer-backed sources (SFX playback) arrive in Phase 2.
typedef enum AnoAudioSourceKind
{
    ANO_AUDIO_SOURCE_TONE = 0, // sine at desc.freqHz
} AnoAudioSourceKind;

// Partial-update masks for ACMD_SOURCE_UPDATE / ACMD_BUS_SET. Every masked
// parameter is a retarget: the mixer glides it through a one-pole (~30 ms), so
// commands never zipper.
typedef enum AnoAudioFieldBits
{
    ANO_AUDIO_FIELD_GAIN = 1 << 0,
    ANO_AUDIO_FIELD_PAN  = 1 << 1,
    ANO_AUDIO_FIELD_FREQ = 1 << 2,
} AnoAudioFieldBits;

typedef enum AnoAudioCommandKind
{
    ACMD_SOURCE_PLAY,   // start (or restart) the source named by source_id from desc
    ACMD_SOURCE_UPDATE, // retarget the fields masked in `fields`
    ACMD_SOURCE_STOP,   // begin the release ramp; retires when inaudible
    ACMD_BUS_SET,       // retarget bus parameters (fields: GAIN)
} AnoAudioCommandKind;

// One playable source description (ACMD_SOURCE_PLAY payload).
typedef struct AnoAudioSourceDesc
{
    uint32_t kind;           // AnoAudioSourceKind
    uint32_t bus;            // target bus index [0, busCount)
    float    gain;           // linear amplitude; fades in from silence over the smoothing window
    float    pan;            // -1 (left) .. +1 (right), constant-power
    float    freqHz;         // TONE: oscillator frequency
    uint64_t durationFrames; // 0 = sustained until ACMD_SOURCE_STOP; else release starts here
} AnoAudioSourceDesc;

// POD, fixed-size, copied by value through the ring at submit; the caller's
// command need not outlive the call.
typedef struct AnoAudioCommand
{
    uint32_t kind;      // AnoAudioCommandKind
    uint32_t source_id; // producer-owned logical handle (PLAY/UPDATE/STOP); reuse only after AEVT_SOURCE_RETIRED
    uint32_t fields;    // AnoAudioFieldBits (UPDATE / BUS_SET)
    uint32_t bus;       // ACMD_BUS_SET target bus index
    float    gain;      // UPDATE|GAIN retarget, or BUS_SET|GAIN retarget
    float    pan;       // UPDATE|PAN retarget
    float    freqHz;    // UPDATE|FREQ retarget
    AnoAudioSourceDesc desc; // ACMD_SOURCE_PLAY only
} AnoAudioCommand;

// Enqueue one command. Returns false ONLY when the command ring is full.
//
// Overflow policy (the contract, not "caller decides"): false means BACKPRESSURE,
// not loss — the producer must retain the command and retry on a later tick; it
// must NOT drop it (a dropped STOP strands a sounding voice; a dropped PLAY is a
// silent cue). The lock-free SPSC ring cannot grow live and spinning here would
// couple logic to the mixer, so retry is the policy.
bool ano_audio_submit(AnoAudioBridge *bridge, const AnoAudioCommand *cmd);

// ---------------------------------------------------------------------------
// Event protocol: audio -> logic
// ---------------------------------------------------------------------------

typedef enum AnoAudioEventKind
{
    AEVT_SOURCE_RETIRED, // a source finished (natural end or STOP ramp); source_id may be recycled
    AEVT_CAPACITY,       // mixer-side capacity advisory (voice pool full: a PLAY was dropped)
} AnoAudioEventKind;

// Fixed-size POD, sub-tagged on `kind`; rides the events ring.
// Retirement facts are lossless: the mixer re-emits them at block boundaries
// until they land. CAPACITY advisories are best-effort.
typedef struct AnoAudioEvent
{
    AnoAudioEventKind kind;
    union {
        uint32_t source_id; // AEVT_SOURCE_RETIRED
    } u;
} AnoAudioEvent;

// Dequeue the next audio->logic event. false if none pending. The logic master
// is the sole consumer and should drain every tick so the ring never backs up.
bool ano_audio_poll_event(AnoAudioBridge *bridge, AnoAudioEvent *out);

// ---------------------------------------------------------------------------
// Published latest-wins lanes
// ---------------------------------------------------------------------------

// Listener pose for directional sources (logic -> audio, latest-wins). Sampled
// by the mixer once per block. Unused by the Phase 0 graph; the lane exists so
// the protocol never widens when spatialization lands in Phase 2.
typedef struct AnoAudioListener
{
    float    pos[3];     // world position
    float    forward[3]; // facing direction (unit)
    float    up[3];      // world up (unit)
    uint64_t seq;        // producer's monotonic publish counter (diagnostics)
} AnoAudioListener;

// Per-block mixer telemetry (audio -> logic, latest-wins). The realtime meter
// frame; offline renders never publish it, so metering cannot perturb a render.
typedef struct AnoAudioTelemetry
{
    uint64_t blockIndex;    // blocks rendered since init
    uint64_t blockCpuNs;    // render time of the latest block
    float    masterPeak;    // |peak| of the latest block, post master gain
    uint32_t underruns;     // device-side ring misses since init
    uint32_t sourcesActive; // sounding voices in the latest block
    uint32_t sampleRate;    // granted engine rate
    uint32_t blockFrames;   // granted block size
} AnoAudioTelemetry;

// Publish the listener pose. Latest-wins; call at most once per logic tick.
// Single-producer (the logic master that owns the bridge).
void ano_audio_publish_listener(AnoAudioBridge *bridge, const AnoAudioListener *l);

// Copy the latest telemetry frame into `out`. false (out untouched) before the
// mixer publishes its first block.
bool ano_audio_acquire_telemetry(AnoAudioBridge *bridge, AnoAudioTelemetry *out);

// ---------------------------------------------------------------------------
// Offline rendering
// ---------------------------------------------------------------------------
// The same mixer core driven headless on the calling thread: no device, no
// threads, no telemetry, synchronous capture into a caller buffer. Commands are
// applied at the first block boundary at-or-after their frame stamp — the same
// block-edge semantics the realtime path has. Deterministic: identical desc and
// frame count render byte-identical output on any heap state (the CI gate).

// One scheduled command. Arrays must be sorted by `frame` ascending.
typedef struct AnoAudioOfflineEvent
{
    uint64_t        frame; // absolute frame at/after which the command applies
    AnoAudioCommand cmd;
} AnoAudioOfflineEvent;

typedef struct AnoAudioOfflineDesc
{
    uint32_t sampleRate;  // default 48000
    uint32_t blockFrames; // default 512, clamped [32, 4096]
    uint32_t busCount;    // default 2, max ANO_AUDIO_MAX_BUSES
    const AnoAudioOfflineEvent *events; // sorted by frame; may be NULL when eventCount is 0
    uint32_t eventCount;
} AnoAudioOfflineDesc;

// Render `frames` frames into out[frames * ANO_AUDIO_CHANNELS] (interleaved
// f32). NULL desc means all defaults. Independent of ano_audio_init — usable
// with or without a live audio world. false on bad args or allocation failure.
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames);

// Development utility: write interleaved f32 samples as an IEEE-float WAV.
// Truncates any existing file. false on I/O failure or bad args.
bool ano_audio_wav_write(const char *path, const float *interleaved,
                         uint64_t frames, uint32_t channels, uint32_t sampleRate);

#endif // ANOPTIC_AUDIO_H
