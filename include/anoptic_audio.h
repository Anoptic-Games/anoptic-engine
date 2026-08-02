/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Mixer-thread audio world: sole owner of sources/buses/DSP. Lifecycle + command/event protocol.
// Structural change applies at block boundaries (default 512 frames @ 48 kHz).
// Commands/events: SPSC rings. Listener + telemetry: latest-wins seqlocks. Transport stays in src/audio/.
// Device backends selected inside ano_audio_init.

#ifndef ANOPTIC_AUDIO_H
#define ANOPTIC_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#define ANO_AUDIO_META(...) [[=__VA_ARGS__]]
#else
#define ANO_AUDIO_META(...)
#endif

#ifdef __cplusplus
extern "C" {
#endif


/* Fixed shape */

// Interleaved f32 stereo mix. Upstream and registered buffers match this rate.
// Registered buffers: mono (spatializable) or stereo (pan = balance, never positional).
#define ANO_AUDIO_CHANNELS 2

// Pool ceilings. Preallocated at init. Runtime allocate is a state flip, never a mixer-thread heap call.
#define ANO_AUDIO_MAX_BUSES   16
#define ANO_AUDIO_MAX_SOURCES 64
#define ANO_AUDIO_MAX_BUFFERS 256
#define ANO_AUDIO_MAX_FX      4 // insert slots per bus
#define ANO_AUDIO_MAX_SENDS   2 // post-fader sends per bus


/* Lifecycle */

// AUTO: platform best-first cascade to null
//   Linux: pipewire -> alsa -> null; Windows: wasapi -> dsound -> null; macOS: coreaudio -> null.
// Named: that backend or init fails. NULL_DEV: consume blocks at nominal cadence, no device.
// ANO_AUDIO_BACKEND (pipewire|alsa|wasapi|dsound|coreaudio|null) overrides.
typedef enum AnoAudioBackend
{
    ANO_AUDIO_BACKEND_AUTO = 0,
    ANO_AUDIO_BACKEND_NULL_DEV,
    ANO_AUDIO_BACKEND_PIPEWIRE,  // Linux
    ANO_AUDIO_BACKEND_ALSA,      // Linux fallback
    ANO_AUDIO_BACKEND_WASAPI,    // Windows
    ANO_AUDIO_BACKEND_DSOUND,    // Windows fallback
    ANO_AUDIO_BACKEND_COREAUDIO, // macOS
} AnoAudioBackend;

// Bus insert kinds. Chain fixed at init. Params retarget via ACMD_FX_SET.
typedef enum AnoAudioEffectKind
{
    ANO_AUDIO_FX_NONE = 0,
    ANO_AUDIO_FX_FILTER,     // TPT SVF (LP/HP/BP + resonance)
    ANO_AUDIO_FX_EQ3,        // low shelf + peak + high shelf
    ANO_AUDIO_FX_DCBLOCK,    // one-pole DC blocker
    ANO_AUDIO_FX_DRIVE,      // tanh saturator, pre-gain + trim
    ANO_AUDIO_FX_COMPRESSOR, // feedback compressor, bounded makeup
    ANO_AUDIO_FX_LIMITER,    // lookahead limiter
    ANO_AUDIO_FX_CHORUS,     // dual-rate modulated taps
    ANO_AUDIO_FX_REVERB,     // predelay -> diffusers -> FDN -> shelf
    ANO_AUDIO_FX_PINGPONG,   // cross-feedback stereo delay
    ANO_AUDIO_FX_WIDTH,      // mid/side width (0 = mono)
    ANO_AUDIO_FX_COUNT,
} AnoAudioEffectKind;

// Flat ACMD_FX_SET params. Continuous glide per-block. Modes/bypass instant.
typedef enum AnoAudioFxParam
{
    ANO_AUDIO_P_BYPASS = 0,        // nonzero = bypass

    ANO_AUDIO_P_FILTER_MODE = 16,  // AnoAudioFilterMode
    ANO_AUDIO_P_FILTER_CUTOFF,     // Hz
    ANO_AUDIO_P_FILTER_Q,

    ANO_AUDIO_P_EQ_LOW_GAIN_DB = 32,
    ANO_AUDIO_P_EQ_LOW_FREQ,
    ANO_AUDIO_P_EQ_MID_GAIN_DB,
    ANO_AUDIO_P_EQ_MID_FREQ,
    ANO_AUDIO_P_EQ_MID_Q,
    ANO_AUDIO_P_EQ_HIGH_GAIN_DB,
    ANO_AUDIO_P_EQ_HIGH_FREQ,

    ANO_AUDIO_P_DRIVE_AMOUNT = 48, // pre-gain into tanh, 0.1 .. 16
    ANO_AUDIO_P_DRIVE_TRIM,        // post trim, linear

    ANO_AUDIO_P_COMP_THRESHOLD = 64, // linear amplitude
    ANO_AUDIO_P_COMP_RATIO,
    ANO_AUDIO_P_COMP_ATTACK_MS,
    ANO_AUDIO_P_COMP_RELEASE_MS,
    ANO_AUDIO_P_COMP_MAKEUP,       // linear, clamped [0.25, 4], never implicit

    ANO_AUDIO_P_LIM_CEILING = 80,
    ANO_AUDIO_P_LIM_RELEASE_MS,

    ANO_AUDIO_P_CHORUS_RATE_HZ = 96,
    ANO_AUDIO_P_CHORUS_DEPTH_MS,
    ANO_AUDIO_P_CHORUS_MIX,

    ANO_AUDIO_P_REV_PREDELAY_MS = 112,
    ANO_AUDIO_P_REV_T60_S,
    ANO_AUDIO_P_REV_DAMP_HZ,
    ANO_AUDIO_P_REV_MIX,

    ANO_AUDIO_P_PP_TIME_MS = 128,
    ANO_AUDIO_P_PP_FEEDBACK,
    ANO_AUDIO_P_PP_MIX,

    ANO_AUDIO_P_WIDTH_AMOUNT = 144, // 0 mono .. 1 unity .. 2 wide
} AnoAudioFxParam;

// Bus 0 = master. Other parents and send targets must be lower index (acyclic).
// sendTarget 0 = unused. Master cannot be a send target.
typedef struct AnoAudioBusDesc
{
    uint32_t parent;                             // fold target bus index
    float    gain;                               // initial linear gain; 0 means 1.0
    uint32_t fx[ANO_AUDIO_MAX_FX];               // AnoAudioEffectKind per slot
    uint32_t sendTarget[ANO_AUDIO_MAX_SENDS];    // return bus; 0 = unused
    float    sendLevel[ANO_AUDIO_MAX_SENDS];     // initial linear send level
} AnoAudioBusDesc;

// Block generator on the mixer thread. After bus zero, before fold.
// Writes into busMix[b] (interleaved stereo); rides that bus's chain, fader, and sends.
// startFrame = absolute frame of the block's first sample. No alloc, lock, or bridge.
// Attach before init. Pointer immutable while running.
typedef void (*AnoAudioGenerator)(void *user, float *const *busMix, uint32_t busCount,
                                  uint32_t frames, uint64_t startFrame);

// Optional back-channel for a composing generator (steer / events / stats).
// Composer state lives on the mixer thread; the producer cannot reach it.
// Mixer never interprets these: forwards ACMD_MUSIC_*, publishes polled events, copies stats.
// All three run on the mixer thread at the block boundary. Optional; render-only needs none.
struct AnoAudioCommand;
struct AnoAudioEvent;
struct AnoAudioTelemetry;
typedef void (*AnoAudioGeneratorControl)(void *user, const struct AnoAudioCommand *cmd);
typedef uint32_t (*AnoAudioGeneratorPoll)(void *user, struct AnoAudioEvent *out,
                                          uint32_t cap);
typedef void (*AnoAudioGeneratorStats)(void *user, struct AnoAudioTelemetry *t);

// Generator-emitted desk commands. Applied like producer commands. Drained once per block, before render.
typedef uint32_t (*AnoAudioGeneratorCommands)(void *user, struct AnoAudioCommand *out,
                                              uint32_t cap);

// Zero any field for its default.
typedef struct AnoAudioConfig
{
    uint32_t sampleRate;   // default 48000
    uint32_t blockFrames;  // default 512, clamped [32, 4096]
    uint32_t deviceBlocks; // cooked-block ring depth; default 4
    uint32_t cmdCapacity;  // command ring (pow2); default 1024
    uint32_t evtCapacity;  // event ring (pow2); default 1024
    uint32_t busCount;     // including master; default 2, max ANO_AUDIO_MAX_BUSES
    uint32_t backend;      // AnoAudioBackend; default AUTO
    const AnoAudioBusDesc *busLayout; // NULL = every aux into master
    AnoAudioGenerator generator;
    void             *generatorUser;  // shared by all four generator hooks
    AnoAudioGeneratorControl  generatorControl;
    AnoAudioGeneratorPoll     generatorPoll;
    AnoAudioGeneratorStats    generatorStats;
    AnoAudioGeneratorCommands generatorCommands;
} AnoAudioConfig;

// Allocate pools, open backend, spawn mixer. NULL cfg = defaults.
// false on failure. Call from engine entry before logic produces.
bool ano_audio_init(const AnoAudioConfig *cfg);

// Join mixer/device, destroy bridge, release heap. After logic stops producing.
void ano_audio_shutdown(void);


/* Bridge handle */

// Opaque logic<->audio channel. Created in init, destroyed in shutdown.
typedef struct AnoAudioBridge AnoAudioBridge;

// Producer endpoint. NULL before init returns.
AnoAudioBridge *anoAudioBridge(void);


/* Command protocol: logic -> audio */

typedef enum AnoAudioSourceKind
{
    ANO_AUDIO_SOURCE_TONE = 0, // sine at desc.freqHz
    ANO_AUDIO_SOURCE_BUFFER,   // registered buffer_id
    ANO_AUDIO_SOURCE_COUNT,
} AnoAudioSourceKind;

typedef enum AnoAudioSourceFlags
{
    ANO_AUDIO_SOURCE_LOOP       = 1 << 0, // BUFFER: wrap instead of retire
    ANO_AUDIO_SOURCE_POSITIONAL = 1 << 1, // pose vs listener. Mono only; stereo plays non-positional. pan ignored.
} AnoAudioSourceFlags;

typedef enum AnoAudioFilterMode
{
    ANO_AUDIO_FILTER_OFF = 0,
    ANO_AUDIO_FILTER_LOWPASS,
    ANO_AUDIO_FILTER_HIGHPASS,
    ANO_AUDIO_FILTER_BANDPASS,
    ANO_AUDIO_FILTER_COUNT,
} AnoAudioFilterMode;

// Partial-update masks. Masked params retarget through a ~30 ms one-pole.
typedef enum AnoAudioFieldBits
{
    ANO_AUDIO_FIELD_GAIN     = 1 << 0, // source or bus
    ANO_AUDIO_FIELD_PAN      = 1 << 1, // source (non-positional)
    ANO_AUDIO_FIELD_FREQ     = 1 << 2, // TONE
    ANO_AUDIO_FIELD_POSITION = 1 << 3, // positional world position
    ANO_AUDIO_FIELD_RATE     = 1 << 4, // BUFFER playback rate
    ANO_AUDIO_FIELD_SEND0    = 1 << 5, // bus send slot 0
    ANO_AUDIO_FIELD_SEND1    = 1 << 6, // bus send slot 1
} AnoAudioFieldBits;

#ifdef __cplusplus
enum class AnoAudioCommandPayload : uint8_t { source_play, source_update, source_id, bus_update, fx_parameter, buffer_block, music_affect, music_key, music_tag, music_tag_value, music_seek };
enum class AnoAudioPayloadOwnership : uint8_t { inline_value, adopted, borrowed, returned };
enum class AnoAudioCommandTarget : uint8_t { mixer, generator };
struct AnoAudioCommandContract final { AnoAudioCommandPayload payload; AnoAudioPayloadOwnership ownership; AnoAudioCommandTarget target; };
#endif

typedef enum AnoAudioCommandKind
{
    ACMD_SOURCE_PLAY ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::source_play, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),
    ACMD_SOURCE_UPDATE ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::source_update, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),
    ACMD_SOURCE_STOP ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::source_id, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),
    ACMD_BUS_SET ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::bus_update, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),
    ACMD_FX_SET ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::fx_parameter, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),
    ACMD_BUFFER_REGISTER ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::buffer_block, AnoAudioPayloadOwnership::adopted, AnoAudioCommandTarget::mixer}),
    ACMD_BUFFER_RELEASE ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::source_id, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::mixer}),

    // Forwarded verbatim to generatorControl at block boundary. No-op without generator.
    ACMD_MUSIC_AFFECT ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_affect, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::generator}),
    ACMD_MUSIC_KEY ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_key, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::generator}),
    ACMD_MUSIC_MOTIF ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_tag, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::generator}),
    ACMD_MUSIC_OVERRIDE ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_tag_value, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::generator}),
    ACMD_MUSIC_RELEASE ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_tag, AnoAudioPayloadOwnership::inline_value, AnoAudioCommandTarget::generator}),

    // Borrowed engine snapshot (built off the audio thread). Adopt at next barline;
    // already-sounding plays out. Valid until AEVT_MUSIC_SEEKED.
    ACMD_MUSIC_SEEK ANO_AUDIO_META(AnoAudioCommandContract{AnoAudioCommandPayload::music_seek, AnoAudioPayloadOwnership::borrowed, AnoAudioCommandTarget::generator}),
} AnoAudioCommandKind;

#ifdef __cplusplus
struct AnoAudioFieldUse final { uint32_t fields; uint32_t commands; };
struct AnoAudioPointerPayloadUse final { uint32_t commands; };
#endif

// Longest ACMD_MUSIC_* name, NUL included.
#define ANO_AUDIO_TAG_MAX 24

typedef struct AnoAudioSourceDesc
{
    uint32_t kind;           // AnoAudioSourceKind
    uint32_t bus;            // [0, busCount)
    uint32_t buffer_id;      // BUFFER
    uint32_t flags;          // AnoAudioSourceFlags
    float    gain;           // linear. fades in over smoothing window
    float    pan;            // -1 .. +1 constant-power. ignored if POSITIONAL
    float    freqHz;         // TONE
    float    rate;           // BUFFER pitch. 0 = 1.0
    float    position[3];    // POSITIONAL world position
    float    minDist;        // full gain inside. 0 = 1.0
    float    maxDist;        // attenuation floor distance. 0 = 50.0
    float    rolloff;        // inverse-distance slope. 0 = 1.0
    uint64_t durationFrames; // 0 = until STOP. else release starts here
} AnoAudioSourceDesc;

// POD, copied by value. Exception: block is adopted (REGISTER) or borrowed (SEEK).
typedef struct AnoAudioCommand
{
    uint32_t kind;        // AnoAudioCommandKind
    uint32_t source_id;   // logical handle. buffer id for ACMD_BUFFER_*
    uint32_t fields;      // AnoAudioFieldBits (UPDATE / BUS_SET)
    uint32_t bus;         // BUS_SET / FX_SET
    float    gain ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_GAIN, (1u << ACMD_SOURCE_UPDATE) | (1u << ACMD_BUS_SET)});
    float    pan ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_PAN, 1u << ACMD_SOURCE_UPDATE});
    float    freqHz ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_FREQ, 1u << ACMD_SOURCE_UPDATE});
    float    rate ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_RATE, 1u << ACMD_SOURCE_UPDATE});
    float    position ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_POSITION, 1u << ACMD_SOURCE_UPDATE})[3];
    float    send ANO_AUDIO_META(AnoAudioFieldUse{ANO_AUDIO_FIELD_SEND0 | ANO_AUDIO_FIELD_SEND1, 1u << ACMD_BUS_SET})[2];
    uint32_t fxSlot;      // [0, ANO_AUDIO_MAX_FX)
    uint32_t paramId;     // AnoAudioFxParam. MUSIC_KEY tonic
    float    value;       // FX_SET / MUSIC_OVERRIDE
    float    affect[3];   // MUSIC_AFFECT: valence, energy, tension (NAN = leave)
    bool     urgent;      // MUSIC_AFFECT / _KEY: next barline
    char     tag[ANO_AUDIO_TAG_MAX]; // MOTIF / OVERRIDE / RELEASE
    const void *block ANO_AUDIO_META(AnoAudioPointerPayloadUse{(1u << ACMD_BUFFER_REGISTER) | (1u << ACMD_MUSIC_SEEK)}); // REGISTER adopted; SEEK borrowed
    AnoAudioSourceDesc desc; // SOURCE_PLAY
} AnoAudioCommand;

// Enqueue one command. false = ring full only.
// Backpressure: retain and retry on a later tick; never drop (dropped STOP strands a voice).
bool ano_audio_submit(AnoAudioBridge *bridge, const AnoAudioCommand *cmd);


/* Event protocol: audio -> logic */

#ifdef __cplusplus
enum class AnoAudioEventPayloadKind : uint8_t { none, source_id, buffer, music, seeked_bar };
struct AnoAudioEventContract final { AnoAudioEventPayloadKind payload; AnoAudioPayloadOwnership ownership; };
#endif

typedef enum AnoAudioEventKind
{
    AEVT_SOURCE_RETIRED ANO_AUDIO_META(AnoAudioEventContract{AnoAudioEventPayloadKind::source_id, AnoAudioPayloadOwnership::inline_value}),
    AEVT_BUFFER_RETIRED ANO_AUDIO_META(AnoAudioEventContract{AnoAudioEventPayloadKind::buffer, AnoAudioPayloadOwnership::returned}),
    AEVT_CAPACITY ANO_AUDIO_META(AnoAudioEventContract{AnoAudioEventPayloadKind::none, AnoAudioPayloadOwnership::inline_value}),
    AEVT_MUSIC_BAR ANO_AUDIO_META(AnoAudioEventContract{AnoAudioEventPayloadKind::music, AnoAudioPayloadOwnership::inline_value}),
    AEVT_MUSIC_SEEKED ANO_AUDIO_META(AnoAudioEventContract{AnoAudioEventPayloadKind::seeked_bar, AnoAudioPayloadOwnership::inline_value}),
} AnoAudioEventKind;

typedef struct AnoAudioBufferRetiredEvent { uint32_t buffer_id; void *block; } AnoAudioBufferRetiredEvent;
typedef struct AnoAudioMusicBarEvent {
    int32_t bar, keyTonic, mode, chordDegree, chordInversion;
    int8_t  cadencePolicy;
    bool    isCadence, keyArrived, motifStated;
} AnoAudioMusicBarEvent;

#ifdef __cplusplus
struct AnoAudioEventPayloadFor final { AnoAudioEventKind kind; AnoAudioEventPayloadKind payload; };
#endif

typedef union AnoAudioEventPayload
{
    uint32_t source_id ANO_AUDIO_META(AnoAudioEventPayloadFor{AEVT_SOURCE_RETIRED, AnoAudioEventPayloadKind::source_id});
    AnoAudioBufferRetiredEvent buffer ANO_AUDIO_META(AnoAudioEventPayloadFor{AEVT_BUFFER_RETIRED, AnoAudioEventPayloadKind::buffer});
    AnoAudioMusicBarEvent music ANO_AUDIO_META(AnoAudioEventPayloadFor{AEVT_MUSIC_BAR, AnoAudioEventPayloadKind::music});
    int32_t seekedBar ANO_AUDIO_META(AnoAudioEventPayloadFor{AEVT_MUSIC_SEEKED, AnoAudioEventPayloadKind::seeked_bar});
} AnoAudioEventPayload;

// Fixed POD. Retirement facts re-emit until landed. CAPACITY best-effort.
typedef struct AnoAudioEvent
{
    AnoAudioEventKind kind;
    // MUSIC_BAR is a projection of AnoMusicMeaning, held to the audible downbeat.
    // MUSIC_SEEKED carries the adoption bar; the borrowed snapshot may then be freed.
    AnoAudioEventPayload u;
} AnoAudioEvent;

// Dequeue next event. false if empty. Sole consumer: drain every tick.
bool ano_audio_poll_event(AnoAudioBridge *bridge, AnoAudioEvent *out);


/* Sample buffers */

// Register from logic. Copies to owned block at submit; never free on mixer.
// RELEASE stops voices; block returns via AEVT_BUFFER_RETIRED after last voice quiets.
// Teardown frees remaining resident, queued, or un-polled blocks. No AEVT after shutdown; do not free adopted blocks after return.

// Register interleaved f32 (1-2 ch, engine rate). false = backpressure or bad args.
bool ano_audio_buffer_register(AnoAudioBridge *bridge, uint32_t buffer_id,
                               const float *interleaved, uint64_t frames, uint32_t channels);

// Begin buffer retirement. Same backpressure as submit.
bool ano_audio_buffer_release(AnoAudioBridge *bridge, uint32_t buffer_id);

// Free AEVT_BUFFER_RETIRED or ano_audio_wav_load block.
void ano_audio_block_free(void *block);


/* Published latest-wins lanes */

// Listener pose (logic -> audio). Sampled once per block.
typedef struct AnoAudioListener
{
    float    pos[3];
    float    forward[3]; // unit
    float    up[3];      // unit
    uint64_t seq;        // publish counter (diagnostics)
} AnoAudioListener;

// Per-block mixer telemetry (audio -> logic). Offline never publishes.
typedef struct AnoAudioTelemetry
{
    uint64_t blockIndex;
    uint64_t blockCpuNs;
    float    masterPeak;     // |peak|, pre clip guard
    uint32_t underruns;
    uint32_t sourcesActive;
    uint32_t sampleRate;
    uint32_t blockFrames;
    uint32_t clippedSamples;

    // generatorStats fill (0 if none)
    uint32_t genUs;      // latest block, microseconds
    uint32_t genUsMax;   // worst since start
    uint32_t genLate;    // work after the playhead needed it
    uint32_t genDropped; // schedule misses + voices the pool could not sound
} AnoAudioTelemetry;

// Publish listener. At most once per logic tick. Single producer.
void ano_audio_publish_listener(AnoAudioBridge *bridge, const AnoAudioListener *l);

// Copy latest telemetry. false before first mixer publish.
bool ano_audio_acquire_telemetry(AnoAudioBridge *bridge, AnoAudioTelemetry *out);


/* Offline rendering */

// Same mixer core, caller thread. No device, threads, or telemetry.
// Commands apply at first block boundary at-or-after frame stamp. Deterministic.

typedef struct AnoAudioOfflineEvent
{
    uint64_t        frame; // absolute apply frame
    AnoAudioCommand cmd;
} AnoAudioOfflineEvent;

// Pre-registered borrowed buffer. Must outlive the call. ACMD_BUFFER_* ignored offline.
typedef struct AnoAudioOfflineBuffer
{
    uint32_t     buffer_id;
    uint32_t     channels; // 1 or 2
    uint64_t     frames;
    const float *data;     // interleaved f32 at render rate
} AnoAudioOfflineBuffer;

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
    const AnoAudioBusDesc *busLayout;
    const AnoAudioOfflineEvent *events; // sorted by frame
    uint32_t eventCount;
    const AnoAudioOfflineBuffer *buffers;
    uint32_t bufferCount;
    const AnoAudioOfflineListener *listeners; // sorted by frame
    uint32_t listenerCount;
    AnoAudioGenerator generator;
    void             *generatorUser;
    // Offline steer + desk-automation only; no poll/stats (offline publishes neither).
    AnoAudioGeneratorControl  generatorControl;
    AnoAudioGeneratorCommands generatorCommands;
} AnoAudioOfflineDesc;

// Render frames into out[frames * ANO_AUDIO_CHANNELS]. Independent of init.
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames);

// Interleaved f32 -> IEEE-float WAV. Truncates existing. false on I/O or bad args.
bool ano_audio_wav_write(const char *path, const float *interleaved,
                         uint64_t frames, uint32_t channels, uint32_t sampleRate);

// Load WAV (PCM 16/24/32 or IEEE f32, 1-2 ch) to interleaved f32.
// Windowed-sinc to targetRate when rates differ. Free with ano_audio_block_free.
// Logic thread only.
float *ano_audio_wav_load(const char *path, uint32_t targetRate,
                          uint64_t *outFrames, uint32_t *outChannels);

#ifdef __cplusplus
}
#endif

#undef ANO_AUDIO_META

#endif // ANOPTIC_AUDIO_H
