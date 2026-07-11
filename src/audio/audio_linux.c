/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Linux device backends: PipeWire native (primary), ALSA (fallback).
 * Both are dlopen'd at start() — the engine carries no link-time or header
 * dependency on either library, and a missing library fails start() cleanly
 * so the AUTO cascade can move on. Protocol references: PipeWire docs for
 * pw_stream; miniaudio's ALSA backend for the libasound protocol
 * (docs/references/miniaudio-legend.md). ALSA constants verified against
 * /usr/include/alsa/pcm.h on this machine; PipeWire ABI constants verified
 * against upstream 1.0.5 headers (see the block comment at the PipeWire
 * section). The OS libraries hold internal control-path locks — that is the
 * platform-file exception, same class as the Vulkan backend. */

#include "audio_internal.h"
#include "audio_pull.h" // shared ring consumer with partial-block carry
#include "dsp/noise.h"  // TPDF dither at the final 16-bit quantization (ALSA s16 path)

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <anoptic_logging.h>
#include <anoptic_time.h>

// ---------------------------------------------------------------------------
// ALSA fallback backend
// ---------------------------------------------------------------------------
// The simple API: snd_pcm_set_params does the whole hw/sw-params negotiation
// (the full dance lives at miniaudio ma_device_init_by_type__alsa if we ever
// need finer control). Blocking snd_pcm_writei paces the device thread.
// Constants from alsa/pcm.h: STREAM_PLAYBACK=0, ACCESS_RW_INTERLEAVED=3,
// FORMAT_S16_LE=2, FORMAT_FLOAT_LE=14.

#define ANO_ALSA_STREAM_PLAYBACK   0
#define ANO_ALSA_RW_INTERLEAVED    3
#define ANO_ALSA_FORMAT_S16_LE     2
#define ANO_ALSA_FORMAT_FLOAT_LE   14

typedef struct ano_snd_pcm ano_snd_pcm; // opaque snd_pcm_t

typedef struct AnoAlsaState
{
    void *lib;
    int  (*pcm_open)(ano_snd_pcm **pcm, const char *name, int stream, int mode);
    int  (*pcm_set_params)(ano_snd_pcm *pcm, int format, int access, unsigned channels,
                           unsigned rate, int soft_resample, unsigned latency_us);
    long (*pcm_writei)(ano_snd_pcm *pcm, const void *buffer, unsigned long frames);
    int  (*pcm_recover)(ano_snd_pcm *pcm, int err, int silent);
    int  (*pcm_prepare)(ano_snd_pcm *pcm);
    int  (*pcm_drain)(ano_snd_pcm *pcm);
    int  (*pcm_close)(ano_snd_pcm *pcm);
    const char *(*strerr)(int errnum);

    ano_snd_pcm *pcm;
    AnoAudioPull pull;
    AnoDspRng    dither; // seeded TPDF for the s16 path (finding 8: no global entropy)
    float       *fbuf;   // one mixer block of f32 pulled from the ring
    int16_t     *sbuf;   // s16 staging when the device refuses float; NULL when float granted
} AnoAlsaState;

static void *alsa_main(void *arg)
{
    AnoAudioMixer *mx = arg;
    AnoAlsaState  *st = mx->deviceState;
    const uint32_t frames     = mx->blockFrames;
    const size_t   frameBytes = st->sbuf ? ANO_AUDIO_CHANNELS * sizeof(int16_t)
                                         : ANO_AUDIO_CHANNELS * sizeof(float);
    while (atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
        ano_audio_pull_frames(mx, &st->pull, st->fbuf, frames);
        const void *src = st->fbuf;
        if (st->sbuf) {
            // final 16-bit quantization: the one place TPDF dither applies
            for (uint32_t i = 0; i < frames * ANO_AUDIO_CHANNELS; ++i) {
                float v = st->fbuf[i];
                v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
                float y = v * 32767.0f + ano_dsp_tpdf(&st->dither);
                if (y > 32767.0f) y = 32767.0f;
                if (y < -32768.0f) y = -32768.0f;
                st->sbuf[i] = (int16_t)(y >= 0.0f ? y + 0.5f : y - 0.5f);
            }
            src = st->sbuf;
        }
        // blocking write paces this thread at the device rate
        uint32_t written = 0;
        while (written < frames && atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
            long n = st->pcm_writei(st->pcm, (const char *)src + (size_t)written * frameBytes,
                                    frames - written);
            if (n < 0) {
                if ((int)n == -EPIPE) // xrun: the device ran dry
                    atomic_fetch_add_explicit(&mx->underruns, 1u, memory_order_relaxed);
                if (st->pcm_recover(st->pcm, (int)n, 1) < 0) {
                    ano_log(ANO_ERROR, "audio/alsa: unrecoverable write error: %s",
                            st->strerr((int)n));
                    ano_sleep(10000); // do not spin; retry the loop
                    break;
                }
                continue;
            }
            written += (uint32_t)n;
        }
    }
    return NULL;
}

static bool alsa_start(AnoAudioMixer *mx)
{
    AnoAlsaState *st = mi_heap_calloc(mx->heap, 1, sizeof *st);
    if (!st)
        return false;
    st->lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib)
        st->lib = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib) {
        mi_free(st);
        return false; // no ALSA on this machine: let the cascade move on
    }

#define ANO_ALSA_SYM(dst, name) do { \
        *(void **)&(dst) = dlsym(st->lib, name); \
        if (!(dst)) goto fail; \
    } while (0)
    ANO_ALSA_SYM(st->pcm_open,       "snd_pcm_open");
    ANO_ALSA_SYM(st->pcm_set_params, "snd_pcm_set_params");
    ANO_ALSA_SYM(st->pcm_writei,     "snd_pcm_writei");
    ANO_ALSA_SYM(st->pcm_recover,    "snd_pcm_recover");
    ANO_ALSA_SYM(st->pcm_prepare,    "snd_pcm_prepare");
    ANO_ALSA_SYM(st->pcm_drain,      "snd_pcm_drain");
    ANO_ALSA_SYM(st->pcm_close,      "snd_pcm_close");
    ANO_ALSA_SYM(st->strerr,         "snd_strerror");
#undef ANO_ALSA_SYM

    if (st->pcm_open(&st->pcm, "default", ANO_ALSA_STREAM_PLAYBACK, 0) < 0)
        goto fail;

    // request the block-ring depth as device latency; float first, s16 fallback
    unsigned latencyUs = (unsigned)((uint64_t)mx->blockFrames * 4u * 1000000ull / mx->sampleRate);
    int err = st->pcm_set_params(st->pcm, ANO_ALSA_FORMAT_FLOAT_LE, ANO_ALSA_RW_INTERLEAVED,
                                 ANO_AUDIO_CHANNELS, mx->sampleRate, 1, latencyUs);
    if (err < 0) {
        err = st->pcm_set_params(st->pcm, ANO_ALSA_FORMAT_S16_LE, ANO_ALSA_RW_INTERLEAVED,
                                 ANO_AUDIO_CHANNELS, mx->sampleRate, 1, latencyUs);
        if (err < 0) {
            ano_log(ANO_WARN, "audio/alsa: set_params failed: %s", st->strerr(err));
            goto fail_pcm;
        }
        st->sbuf = mi_heap_calloc(mx->heap, (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS,
                                  sizeof(int16_t));
        if (!st->sbuf)
            goto fail_pcm;
        ano_dsp_rng_seed(&st->dither, 0xD17E4u);
    }
    st->fbuf = mi_heap_calloc(mx->heap, (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS, sizeof(float));
    if (!st->fbuf)
        goto fail_pcm;

    mx->deviceState = st;
    atomic_store_explicit(&mx->deviceRun, true, memory_order_release);
    if (ano_thread_create(&mx->deviceThread, NULL, alsa_main, mx) != 0) {
        atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
        mx->deviceState = NULL;
        goto fail_pcm;
    }
    ano_log(ANO_INFO, "audio/alsa: device 'default' open, %s, latency request %u us.",
            st->sbuf ? "s16 (float refused)" : "f32", latencyUs);
    return true;

fail_pcm:
    st->pcm_close(st->pcm);
fail:
    if (st->fbuf) mi_free(st->fbuf);
    if (st->sbuf) mi_free(st->sbuf);
    dlclose(st->lib);
    mi_free(st);
    return false;
}

static void alsa_stop(AnoAudioMixer *mx)
{
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
    AnoAlsaState *st = mx->deviceState;
    if (!st)
        return;
    st->pcm_drain(st->pcm);
    st->pcm_close(st->pcm);
    dlclose(st->lib);
    if (st->fbuf) mi_free(st->fbuf);
    if (st->sbuf) mi_free(st->sbuf);
    mi_free(st);
    mx->deviceState = NULL;
}

const AnoAudioDeviceApi *ano_audio_device_alsa(void)
{
    static const AnoAudioDeviceApi api = {
        .name  = "alsa",
        .start = alsa_start,
        .stop  = alsa_stop,
    };
    return &api;
}

// ---------------------------------------------------------------------------
// PipeWire native backend
// ---------------------------------------------------------------------------
// Hand-declared minimal pw_stream ABI, verified against upstream 1.0.5 headers
// and this machine's runtime lib — every constant, layout, and the format-pod
// byte sequence below are documented in docs/references/pipewire-abi.md.
// pw_thread_loop owns the data thread; with RT_PROCESS the process callback
// runs on the RT thread without the loop lock, so its only work is the
// lock-free ring pull. Control operations (connect, state polls) take the
// loop lock — OS-internal, control-path-only, the platform-file exception.

#define ANO_PW_ID_ANY               0xFFFFFFFFu
#define ANO_PW_DIRECTION_OUTPUT     1
#define ANO_PW_FLAG_AUTOCONNECT     (1u << 0)
#define ANO_PW_FLAG_MAP_BUFFERS     (1u << 2)
#define ANO_PW_FLAG_RT_PROCESS      (1u << 4)
#define ANO_PW_VERSION_STREAM_EVENTS 2u
#define ANO_PW_STATE_ERROR          (-1)
#define ANO_PW_STATE_PAUSED         2

typedef struct ano_pw_thread_loop ano_pw_thread_loop; // opaque
typedef struct ano_pw_loop        ano_pw_loop;        // opaque
typedef struct ano_pw_stream      ano_pw_stream;      // opaque
typedef struct ano_pw_properties  ano_pw_properties;  // opaque

typedef struct AnoSpaPod
{
    uint32_t size;
    uint32_t type;
} AnoSpaPod;

typedef struct AnoSpaChunk
{
    uint32_t offset;
    uint32_t size;
    int32_t  stride;
    int32_t  flags;
} AnoSpaChunk;

typedef struct AnoSpaData
{
    uint32_t     type;
    uint32_t     flags;
    int64_t      fd;
    uint32_t     mapoffset;
    uint32_t     maxsize;
    void        *data;
    AnoSpaChunk *chunk;
} AnoSpaData;

typedef struct AnoSpaBuffer
{
    uint32_t    n_metas;
    uint32_t    n_datas;
    void       *metas;
    AnoSpaData *datas;
} AnoSpaBuffer;

// Stream-allocated; the client reads `buffer` and `requested` only (`time` is
// a 1.0.5 tail addition — never sizeof this from the client side).
typedef struct AnoPwBuffer
{
    AnoSpaBuffer *buffer;
    void         *user_data;
    uint64_t      size;
    uint64_t      requested; // frames suggested by the graph; 0 = no suggestion
    uint64_t      time;
} AnoPwBuffer;

// Held by POINTER for the stream's lifetime — instance below is static const.
typedef struct AnoPwStreamEvents
{
    uint32_t version; // ANO_PW_VERSION_STREAM_EVENTS
    void (*destroy)(void *data);
    void (*state_changed)(void *data, int oldState, int newState, const char *error);
    void (*control_info)(void *data, uint32_t id, const void *control);
    void (*io_changed)(void *data, uint32_t id, void *area, uint32_t size);
    void (*param_changed)(void *data, uint32_t id, const AnoSpaPod *param);
    void (*add_buffer)(void *data, AnoPwBuffer *buffer);
    void (*remove_buffer)(void *data, AnoPwBuffer *buffer);
    void (*process)(void *data);
    void (*drained)(void *data);
    void (*command)(void *data, const void *command);
    void (*trigger_done)(void *data);
} AnoPwStreamEvents;

typedef struct AnoPwApi
{
    void *lib;
    void (*init)(int *argc, char ***argv);
    void (*deinit)(void);
    const char *(*get_library_version)(void);
    ano_pw_thread_loop *(*thread_loop_new)(const char *name, const void *props);
    void (*thread_loop_destroy)(ano_pw_thread_loop *loop);
    int  (*thread_loop_start)(ano_pw_thread_loop *loop);
    void (*thread_loop_stop)(ano_pw_thread_loop *loop); // call WITHOUT the lock held
    void (*thread_loop_lock)(ano_pw_thread_loop *loop);
    void (*thread_loop_unlock)(ano_pw_thread_loop *loop);
    ano_pw_loop *(*thread_loop_get_loop)(ano_pw_thread_loop *loop);
    ano_pw_properties *(*properties_new)(const char *key, ...); // k,v pairs, NULL-terminated
    ano_pw_stream *(*stream_new_simple)(ano_pw_loop *loop, const char *name,
                                        ano_pw_properties *props, // ownership taken
                                        const AnoPwStreamEvents *events, void *data);
    void (*stream_destroy)(ano_pw_stream *stream);
    int  (*stream_connect)(ano_pw_stream *stream, int direction, uint32_t targetId,
                           uint32_t flags, const AnoSpaPod **params, uint32_t nParams);
    int  (*stream_disconnect)(ano_pw_stream *stream);
    int  (*stream_get_state)(ano_pw_stream *stream, const char **error);
    AnoPwBuffer *(*stream_dequeue_buffer)(ano_pw_stream *stream);
    int  (*stream_queue_buffer)(ano_pw_stream *stream, AnoPwBuffer *buffer);
} AnoPwApi;

typedef struct AnoPipewireState
{
    AnoPwApi            api;
    ano_pw_thread_loop *loop;
    ano_pw_stream      *stream;
    AnoAudioPull        pull;
    _Atomic int         error; // set by state_changed(ERROR); mixer restart policy later
    AnoAudioMixer      *mx;
} AnoPipewireState;

// The fixed EnumFormat object pod (mediaType=audio, mediaSubtype=raw,
// format=F32, rate, channels=2, position=[FL,FR]) — the byte-exact output of
// spa_format_audio_raw_build for these values. 42 words / 168 bytes.
static uint32_t pw_build_format_pod(uint32_t *w, uint32_t rate)
{
    uint32_t i = 0;
    w[i++] = 160u;     w[i++] = 15u;      // pod header: body size, SPA_TYPE_Object
    w[i++] = 0x40003u; w[i++] = 3u;       // object body: OBJECT_Format, id EnumFormat
    w[i++] = 1u;       w[i++] = 0u;       // prop mediaType
    w[i++] = 4u;       w[i++] = 3u;       //   Id pod
    w[i++] = 1u;       w[i++] = 0u;       //   audio, pad
    w[i++] = 2u;       w[i++] = 0u;       // prop mediaSubtype
    w[i++] = 4u;       w[i++] = 3u;       //   Id pod
    w[i++] = 1u;       w[i++] = 0u;       //   raw, pad
    w[i++] = 0x10001u; w[i++] = 0u;       // prop AUDIO_format
    w[i++] = 4u;       w[i++] = 3u;       //   Id pod
    w[i++] = 283u;     w[i++] = 0u;       //   F32_LE, pad
    w[i++] = 0x10003u; w[i++] = 0u;       // prop AUDIO_rate
    w[i++] = 4u;       w[i++] = 4u;       //   Int pod
    w[i++] = rate;     w[i++] = 0u;       //   rate, pad
    w[i++] = 0x10004u; w[i++] = 0u;       // prop AUDIO_channels
    w[i++] = 4u;       w[i++] = 4u;       //   Int pod
    w[i++] = 2u;       w[i++] = 0u;       //   stereo, pad
    w[i++] = 0x10005u; w[i++] = 0u;       // prop AUDIO_position
    w[i++] = 16u;      w[i++] = 13u;      //   Array pod, body 16
    w[i++] = 4u;       w[i++] = 3u;       //   child: size 4, Id
    w[i++] = 3u;       w[i++] = 4u;       //   FL, FR (packed, no pad needed)
    return i * (uint32_t)sizeof(uint32_t);
}

// RT data thread, no loop lock (RT_PROCESS): pull cooked frames, set the chunk.
static void pw_on_process(void *data)
{
    AnoPipewireState *st = data;
    AnoPwBuffer *b = st->api.stream_dequeue_buffer(st->stream);
    if (!b)
        return;
    AnoSpaData *d = &b->buffer->datas[0];
    if (!d->data) { // not mapped (should not happen with MAP_BUFFERS)
        st->api.stream_queue_buffer(st->stream, b);
        return;
    }
    const uint32_t stride = ANO_AUDIO_CHANNELS * (uint32_t)sizeof(float);
    uint32_t frames = d->maxsize / stride;
    if (b->requested != 0u && b->requested < frames)
        frames = (uint32_t)b->requested;
    ano_audio_pull_frames(st->mx, &st->pull, (float *)d->data, frames);
    d->chunk->offset = 0u;
    d->chunk->stride = (int32_t)stride;
    d->chunk->size   = frames * stride;
    st->api.stream_queue_buffer(st->stream, b);
}

static void pw_on_state_changed(void *data, int oldState, int newState, const char *error)
{
    (void)oldState;
    AnoPipewireState *st = data;
    if (newState == ANO_PW_STATE_ERROR) {
        atomic_store_explicit(&st->error, 1, memory_order_release);
        ano_log(ANO_ERROR, "audio/pipewire: stream error: %s", error ? error : "(none)");
    }
}

static const AnoPwStreamEvents g_pwEvents = {
    .version       = ANO_PW_VERSION_STREAM_EVENTS,
    .state_changed = pw_on_state_changed,
    .process       = pw_on_process,
};

static bool pw_start(AnoAudioMixer *mx)
{
    AnoPipewireState *st = mi_heap_calloc(mx->heap, 1, sizeof *st);
    if (!st)
        return false;
    st->mx = mx;
    atomic_init(&st->error, 0);

    st->api.lib = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!st->api.lib) {
        mi_free(st);
        return false; // no PipeWire on this machine: cascade moves on
    }

#define ANO_PW_SYM(dst, name) do { \
        *(void **)&(st->api.dst) = dlsym(st->api.lib, name); \
        if (!st->api.dst) goto fail_lib; \
    } while (0)
    ANO_PW_SYM(init,                 "pw_init");
    ANO_PW_SYM(deinit,               "pw_deinit");
    ANO_PW_SYM(get_library_version,  "pw_get_library_version");
    ANO_PW_SYM(thread_loop_new,      "pw_thread_loop_new");
    ANO_PW_SYM(thread_loop_destroy,  "pw_thread_loop_destroy");
    ANO_PW_SYM(thread_loop_start,    "pw_thread_loop_start");
    ANO_PW_SYM(thread_loop_stop,     "pw_thread_loop_stop");
    ANO_PW_SYM(thread_loop_lock,     "pw_thread_loop_lock");
    ANO_PW_SYM(thread_loop_unlock,   "pw_thread_loop_unlock");
    ANO_PW_SYM(thread_loop_get_loop, "pw_thread_loop_get_loop");
    ANO_PW_SYM(properties_new,       "pw_properties_new");
    ANO_PW_SYM(stream_new_simple,    "pw_stream_new_simple");
    ANO_PW_SYM(stream_destroy,       "pw_stream_destroy");
    ANO_PW_SYM(stream_connect,       "pw_stream_connect");
    ANO_PW_SYM(stream_disconnect,    "pw_stream_disconnect");
    ANO_PW_SYM(stream_get_state,     "pw_stream_get_state");
    ANO_PW_SYM(stream_dequeue_buffer, "pw_stream_dequeue_buffer");
    ANO_PW_SYM(stream_queue_buffer,  "pw_stream_queue_buffer");
#undef ANO_PW_SYM

    st->api.init(NULL, NULL);

    st->loop = st->api.thread_loop_new("anoptic-audio", NULL);
    if (!st->loop)
        goto fail_deinit;

    // request our block as the quantum; the graph may grant more or less —
    // the carry-aware pull absorbs any quantum
    char latency[32], rateStr[32];
    snprintf(latency, sizeof latency, "%u/%u", mx->blockFrames, mx->sampleRate);
    snprintf(rateStr, sizeof rateStr, "1/%u", mx->sampleRate);
    ano_pw_properties *props = st->api.properties_new(
        "media.type",       "Audio",
        "media.category",   "Playback",
        "media.role",       "Game",
        "node.name",        "anoptic-engine",
        "application.name", "Anoptic Engine",
        "node.latency",     latency,
        "node.rate",        rateStr,
        NULL);
    st->stream = st->api.stream_new_simple(st->api.thread_loop_get_loop(st->loop),
                                           "anoptic-audio", props, &g_pwEvents, st);
    if (!st->stream)
        goto fail_loop;
    if (st->api.thread_loop_start(st->loop) != 0)
        goto fail_stream;

    uint32_t podWords[42];
    pw_build_format_pod(podWords, mx->sampleRate);
    const AnoSpaPod *params[1] = { (const AnoSpaPod *)podWords };
    st->api.thread_loop_lock(st->loop);
    int res = st->api.stream_connect(st->stream, ANO_PW_DIRECTION_OUTPUT, ANO_PW_ID_ANY,
                                     ANO_PW_FLAG_AUTOCONNECT | ANO_PW_FLAG_MAP_BUFFERS
                                         | ANO_PW_FLAG_RT_PROCESS,
                                     params, 1u);
    st->api.thread_loop_unlock(st->loop);
    if (res < 0)
        goto fail_started;

    // wait out CONNECTING: no server / refused negotiation must fail here so
    // the cascade can fall through to ALSA instead of playing silence
    for (uint32_t waited = 0;; waited += 50u) {
        st->api.thread_loop_lock(st->loop);
        int state = st->api.stream_get_state(st->stream, NULL);
        st->api.thread_loop_unlock(st->loop);
        if (state == ANO_PW_STATE_ERROR || atomic_load_explicit(&st->error, memory_order_acquire))
            goto fail_started;
        if (state >= ANO_PW_STATE_PAUSED)
            break;
        if (waited >= 2000u) {
            ano_log(ANO_WARN, "audio/pipewire: stream stuck connecting; falling back.");
            goto fail_started;
        }
        ano_sleep(50000);
    }

    mx->deviceState = st;
    ano_log(ANO_INFO, "audio/pipewire: stream up (lib %s), requested quantum %s.",
            st->api.get_library_version(), latency);
    return true;

fail_started:
    st->api.thread_loop_stop(st->loop); // without the lock
fail_stream:
    st->api.stream_destroy(st->stream);
fail_loop:
    st->api.thread_loop_destroy(st->loop);
fail_deinit:
    st->api.deinit();
fail_lib:
    dlclose(st->api.lib);
    mi_free(st);
    return false;
}

static void pw_stop(AnoAudioMixer *mx)
{
    AnoPipewireState *st = mx->deviceState;
    if (!st)
        return;
    // upstream teardown order: stop the loop (lock NOT held), then destroy
    st->api.thread_loop_stop(st->loop);
    st->api.stream_destroy(st->stream);
    st->api.thread_loop_destroy(st->loop);
    st->api.deinit();
    dlclose(st->api.lib);
    mi_free(st);
    mx->deviceState = NULL;
}

const AnoAudioDeviceApi *ano_audio_device_pipewire(void)
{
    static const AnoAudioDeviceApi api = {
        .name  = "pipewire",
        .start = pw_start,
        .stop  = pw_stop,
    };
    return &api;
}
