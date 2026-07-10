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
// Registered buffers are canonical too: f32 interleaved at the engine rate,
// mono (spatializable) or stereo (pan acts as balance, never positional).
#define ANO_AUDIO_CHANNELS 2

// Compile-time pool ceilings. Pools are preallocated at init ("allocate" at
// runtime is a state flip, never a heap call on the mixer thread).
#define ANO_AUDIO_MAX_BUSES   8
#define ANO_AUDIO_MAX_SOURCES 64
#define ANO_AUDIO_MAX_BUFFERS 256

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Device backend selection. AUTO tries the platform's backends best-first
// (Linux: pipewire -> alsa -> null) and settles on the first that opens; a
// specific value demands exactly that backend and init fails if it cannot
// open. NULL_DEV consumes cooked blocks at the nominal block cadence without
// a device — headless runs, CI, tests. The environment variable
// ANO_AUDIO_BACKEND (pipewire | alsa | null) overrides the config for testing.
typedef enum AnoAudioBackend
{
    ANO_AUDIO_BACKEND_AUTO = 0,
    ANO_AUDIO_BACKEND_NULL_DEV,
    ANO_AUDIO_BACKEND_PIPEWIRE, // Linux
    ANO_AUDIO_BACKEND_ALSA,     // Linux fallback
} AnoAudioBackend;

// One bus in the mix tree. Bus 0 is the master (its parent is ignored); every
// other bus folds into its parent, which must have a LOWER index — parents
// before children, so the tree is acyclic by construction.
typedef struct AnoAudioBusDesc
{
    uint32_t parent; // index of the bus this one sums into
    float    gain;   // initial linear gain; 0 means 1.0
} AnoAudioBusDesc;

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
    const AnoAudioBusDesc *busLayout; // optional busCount entries; NULL = every aux sums into master
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

// Source generator kinds.
typedef enum AnoAudioSourceKind
{
    ANO_AUDIO_SOURCE_TONE = 0, // sine at desc.freqHz
    ANO_AUDIO_SOURCE_BUFFER,   // registered buffer named by desc.buffer_id
} AnoAudioSourceKind;

// Source behavior flags.
typedef enum AnoAudioSourceFlags
{
    ANO_AUDIO_SOURCE_LOOP       = 1 << 0, // BUFFER: wrap at the end instead of retiring
    ANO_AUDIO_SOURCE_POSITIONAL = 1 << 1, // pan/attenuation/air-absorption derive from
                                          // desc.position vs the listener; desc.pan is
                                          // ignored. Mono signals only (a stereo buffer
                                          // logs and plays non-positional).
} AnoAudioSourceFlags;

// Per-bus insert filter modes (a TPT state-variable filter).
typedef enum AnoAudioFilterMode
{
    ANO_AUDIO_FILTER_OFF = 0,
    ANO_AUDIO_FILTER_LOWPASS,
    ANO_AUDIO_FILTER_HIGHPASS,
    ANO_AUDIO_FILTER_BANDPASS,
} AnoAudioFilterMode;

// Partial-update masks for ACMD_SOURCE_UPDATE / ACMD_BUS_SET. Every masked
// parameter is a retarget: the mixer glides it through a one-pole (~30 ms), so
// commands never zipper.
typedef enum AnoAudioFieldBits
{
    ANO_AUDIO_FIELD_GAIN        = 1 << 0, // source or bus
    ANO_AUDIO_FIELD_PAN         = 1 << 1, // source (non-positional)
    ANO_AUDIO_FIELD_FREQ        = 1 << 2, // TONE source
    ANO_AUDIO_FIELD_POSITION    = 1 << 3, // positional source world position
    ANO_AUDIO_FIELD_RATE        = 1 << 4, // BUFFER source playback rate (pitch glide)
    ANO_AUDIO_FIELD_CUTOFF      = 1 << 5, // bus filter cutoff Hz
    ANO_AUDIO_FIELD_Q           = 1 << 6, // bus filter resonance
    ANO_AUDIO_FIELD_FILTER_MODE = 1 << 7, // bus filter mode (instant, not slewed)
} AnoAudioFieldBits;

typedef enum AnoAudioCommandKind
{
    ACMD_SOURCE_PLAY,     // start (or restart) the source named by source_id from desc
    ACMD_SOURCE_UPDATE,   // retarget the fields masked in `fields`
    ACMD_SOURCE_STOP,     // begin the release ramp; retires when inaudible
    ACMD_BUS_SET,         // retarget bus parameters (fields: GAIN/CUTOFF/Q/FILTER_MODE)
    ACMD_BUFFER_REGISTER, // adopt an owned sample block under source_id-as-buffer-id
    ACMD_BUFFER_RELEASE,  // retire a buffer; the block comes home via AEVT_BUFFER_RETIRED
} AnoAudioCommandKind;

// One playable source description (ACMD_SOURCE_PLAY payload).
typedef struct AnoAudioSourceDesc
{
    uint32_t kind;           // AnoAudioSourceKind
    uint32_t bus;            // target bus index [0, busCount)
    uint32_t buffer_id;      // BUFFER: a registered buffer's id
    uint32_t flags;          // AnoAudioSourceFlags
    float    gain;           // linear amplitude; fades in from silence over the smoothing window
    float    pan;            // -1 (left) .. +1 (right), constant-power; ignored when POSITIONAL
    float    freqHz;         // TONE: oscillator frequency
    float    rate;           // BUFFER: playback-rate multiplier (pitch); 0 = 1.0
    float    position[3];    // POSITIONAL: world position
    float    minDist;        // full gain inside this radius; 0 = 1.0
    float    maxDist;        // attenuation stops growing past this; 0 = 50.0
    float    rolloff;        // inverse-distance slope; 0 = 1.0
    uint64_t durationFrames; // 0 = sustained until ACMD_SOURCE_STOP; else release starts here
} AnoAudioSourceDesc;

// POD, fixed-size, copied by value through the ring at submit; the caller's
// command need not outlive the call. Buffer registration is the exception:
// `block` is an owned allocation the mixer adopts (use the helpers below).
typedef struct AnoAudioCommand
{
    uint32_t kind;        // AnoAudioCommandKind
    uint32_t source_id;   // producer-owned logical handle; the buffer id for ACMD_BUFFER_*
    uint32_t fields;      // AnoAudioFieldBits (UPDATE / BUS_SET)
    uint32_t bus;         // ACMD_BUS_SET target bus index
    float    gain;        // UPDATE|GAIN retarget, or BUS_SET|GAIN retarget
    float    pan;         // UPDATE|PAN retarget
    float    freqHz;      // UPDATE|FREQ retarget
    float    rate;        // UPDATE|RATE retarget
    float    position[3]; // UPDATE|POSITION retarget
    float    cutoffHz;    // BUS_SET|CUTOFF retarget
    float    q;           // BUS_SET|Q retarget
    uint32_t filterMode;  // BUS_SET|FILTER_MODE (AnoAudioFilterMode)
    const void *block;    // ACMD_BUFFER_REGISTER: the adopted block
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
    AEVT_BUFFER_RETIRED, // a released buffer's block, home for freeing (ano_audio_block_free)
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
        struct {
            uint32_t buffer_id;
            void    *block;  // free with ano_audio_block_free once received
        } buffer;           // AEVT_BUFFER_RETIRED
    } u;
} AnoAudioEvent;

// Dequeue the next audio->logic event. false if none pending. The logic master
// is the sole consumer and should drain every tick so the ring never backs up.
bool ano_audio_poll_event(AnoAudioBridge *bridge, AnoAudioEvent *out);

// ---------------------------------------------------------------------------
// Sample buffers
// ---------------------------------------------------------------------------
// Buffers are registered from the logic thread: the helper copies the samples
// into one owned block at submit (the caller's array need only live until the
// call returns), the mixer adopts it, and after ACMD_BUFFER_RELEASE the block
// rides home in AEVT_BUFFER_RETIRED for logic-side freeing — memory is never
// freed on the mixer thread. Releasing a buffer with sounding voices stops
// them first; the retirement waits until the last voice quiets.

// Register `frames` frames of interleaved f32 (channels 1 or 2, engine rate)
// under buffer_id. false = backpressure (retry) or bad args/allocation
// failure; a duplicate live buffer_id is rejected mixer-side with a warning.
bool ano_audio_buffer_register(AnoAudioBridge *bridge, uint32_t buffer_id,
                               const float *interleaved, uint64_t frames, uint32_t channels);

// Begin retiring a buffer. Same backpressure contract as ano_audio_submit.
bool ano_audio_buffer_release(AnoAudioBridge *bridge, uint32_t buffer_id);

// Free a block returned by AEVT_BUFFER_RETIRED or ano_audio_wav_load.
void ano_audio_block_free(void *block);

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
    uint64_t blockIndex;     // blocks rendered since init
    uint64_t blockCpuNs;     // render time of the latest block
    float    masterPeak;     // |peak| of the latest block, pre clip guard
    uint32_t underruns;      // device-side ring misses since init
    uint32_t sourcesActive;  // sounding voices in the latest block
    uint32_t sampleRate;     // granted engine rate
    uint32_t blockFrames;    // granted block size
    uint32_t clippedSamples; // samples caught by the master clip guard since init
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

// A caller-owned sample buffer pre-registered before the render starts (the
// offline analog of ACMD_BUFFER_REGISTER; the data is borrowed, never freed,
// and must outlive the call). ACMD_BUFFER_* commands are ignored offline.
typedef struct AnoAudioOfflineBuffer
{
    uint32_t     buffer_id;
    uint32_t     channels; // 1 or 2
    uint64_t     frames;
    const float *data;     // interleaved f32 at the render rate
} AnoAudioOfflineBuffer;

// A listener pose applied at the first block boundary at-or-after `frame` —
// the offline analog of the latest-wins listener seqlock.
typedef struct AnoAudioOfflineListener
{
    uint64_t         frame;
    AnoAudioListener listener;
} AnoAudioOfflineListener;

typedef struct AnoAudioOfflineDesc
{
    uint32_t sampleRate;  // default 48000
    uint32_t blockFrames; // default 512, clamped [32, 4096]
    uint32_t busCount;    // default 2, max ANO_AUDIO_MAX_BUSES
    const AnoAudioBusDesc *busLayout;   // optional; NULL = every aux sums into master
    const AnoAudioOfflineEvent *events; // sorted by frame; may be NULL when eventCount is 0
    uint32_t eventCount;
    const AnoAudioOfflineBuffer *buffers; // pre-registered borrowed buffers
    uint32_t bufferCount;
    const AnoAudioOfflineListener *listeners; // sorted by frame
    uint32_t listenerCount;
} AnoAudioOfflineDesc;

// Render `frames` frames into out[frames * ANO_AUDIO_CHANNELS] (interleaved
// f32). NULL desc means all defaults. Independent of ano_audio_init — usable
// with or without a live audio world. false on bad args or allocation failure.
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames);

// Development utility: write interleaved f32 samples as an IEEE-float WAV.
// Truncates any existing file. false on I/O failure or bad args.
bool ano_audio_wav_write(const char *path, const float *interleaved,
                         uint64_t frames, uint32_t channels, uint32_t sampleRate);

// Load a WAV (PCM 16/24/32-bit or IEEE f32; 1-2 channels) into canonical
// interleaved f32, windowed-sinc resampled to targetRate when the file rate
// differs. Returns the sample block (free with ano_audio_block_free) and
// writes the frame count at targetRate and the channel count; NULL on parse
// or I/O failure. Logic-thread utility — never call from the mixer.
float *ano_audio_wav_load(const char *path, uint32_t targetRate,
                          uint64_t *outFrames, uint32_t *outChannels);

#endif // ANOPTIC_AUDIO_H
