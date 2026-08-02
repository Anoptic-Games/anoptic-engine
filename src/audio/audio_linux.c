/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Linux devices: PipeWire (primary), ALSA (fallback). Both dlopen'd at start().

#include "audio_internal.h"
#include "audio_pull.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <anoptic_log.h>
#include <anoptic_time.h>

/* ALSA fallback */

// snd_pcm_set_params + blocking writei. Constants from alsa/pcm.h.
#define ANO_ALSA_STREAM_PLAYBACK   0

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
    AnoDspRng    dither; // TPDF for s16
    AnoAudioFormat format;
    float       *fbuf;
    int16_t     *sbuf;   // NULL when float granted
} AnoAlsaState;

static void *alsa_main(void *arg)
{
    AnoAudioMixer *mx = static_cast<AnoAudioMixer *>(arg);
    AnoAlsaState  *st = static_cast<AnoAlsaState *>(mx->deviceState);
    const uint32_t frames     = mx->blockFrames;
    const size_t   frameBytes = ano_audio_frame_bytes(st->format);
    while (atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
        ano_audio_pull_frames(mx, &st->pull, st->fbuf, frames);
        const void *src = st->fbuf;
        if (st->sbuf) {
            ano_audio_convert_f32_s16(st->fbuf, st->sbuf,
                                      frames * ANO_AUDIO_CHANNELS, &st->dither);
            src = st->sbuf;
        }
        uint32_t written = 0;
        while (written < frames && atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
            long n = st->pcm_writei(st->pcm, (const char *)src + (size_t)written * frameBytes,
                                    frames - written);
            if (n < 0) {
                if ((int)n == -EPIPE)
                    atomic_fetch_add_explicit(&mx->underruns, 1u, memory_order_relaxed);
                if (st->pcm_recover(st->pcm, (int)n, 1) < 0) {
                    ano_log(ANO_ERROR, "audio/alsa: unrecoverable write error: %s",
                            st->strerr((int)n));
                    ano_sleep(10000);
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
    AnoAlsaState *st = static_cast<AnoAlsaState *>(
        mi_heap_calloc(mx->heap, 1, sizeof *st));
    if (!st)
        return false;
    st->lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib)
        st->lib = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib) {
        mi_free(st);
        return false;
    }
    unsigned latencyUs =
        (unsigned)((uint64_t)mx->blockFrames * 4u * 1000000ull / mx->sampleRate);
    const AnoAudioFormat f32Format = ano_audio_mix_format(mx->sampleRate);
    const AnoAudioAlsaProjection f32 = ano_audio_project_alsa(f32Format);
    int err = 0;

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

    // Native mix first, reflected s16 fallback. latency = 4 blocks.
    st->format = f32Format;
    err = st->pcm_set_params(st->pcm, f32.format, f32.access,
                             f32.channels, f32.sampleRate, 1, latencyUs);
    if (err < 0) {
        st->format.sample = AnoAudioSampleType::s16;
        const AnoAudioAlsaProjection s16 = ano_audio_project_alsa(st->format);
        err = st->pcm_set_params(st->pcm, s16.format, s16.access,
                                 s16.channels, s16.sampleRate, 1, latencyUs);
        if (err < 0) {
            ano_log(ANO_WARN, "audio/alsa: set_params failed: %s", st->strerr(err));
            goto fail_pcm;
        }
        st->sbuf = static_cast<int16_t *>(
            mi_heap_calloc(mx->heap, (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS,
                           sizeof(int16_t)));
        if (!st->sbuf)
            goto fail_pcm;
        ano_dsp_rng_seed(&st->dither, 0xD17E4u);
    }
    st->fbuf = static_cast<float *>(
        mi_heap_calloc(mx->heap, (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS,
                       sizeof(float)));
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
            ano_audio_sample_name(st->format.sample), latencyUs);
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
    AnoAlsaState *st = static_cast<AnoAlsaState *>(mx->deviceState);
    if (!st)
        return; // start() failed and already joined/freed
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
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
        .backend = ANO_AUDIO_BACKEND_ALSA,
        .start = alsa_start,
        .stop  = alsa_stop,
    };
    return &api;
}

/* PipeWire */

// Minimal pw_stream ABI (docs/references/pipewire-abi.md). RT_PROCESS: pull only.

#define ANO_PW_ID_ANY               0xFFFFFFFFu
#define ANO_PW_DIRECTION_OUTPUT     1
#define ANO_PW_FLAG_AUTOCONNECT     (1u << 0)
#define ANO_PW_FLAG_MAP_BUFFERS     (1u << 2)
#define ANO_PW_FLAG_RT_PROCESS      (1u << 4)
#define ANO_PW_VERSION_STREAM_EVENTS 2u
#define ANO_PW_STATE_ERROR          (-1)
#define ANO_PW_STATE_PAUSED         2

typedef struct ano_pw_thread_loop ano_pw_thread_loop;
typedef struct ano_pw_loop        ano_pw_loop;
typedef struct ano_pw_stream      ano_pw_stream;
typedef struct ano_pw_properties  ano_pw_properties;

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

// Client reads buffer + requested only. Never sizeof this.
typedef struct AnoPwBuffer
{
    AnoSpaBuffer *buffer;
    void         *user_data;
    uint64_t      size;
    uint64_t      requested; // 0 = no suggestion
    uint64_t      time;
} AnoPwBuffer;

typedef struct AnoPwStreamEvents
{
    uint32_t version;
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
    void (*thread_loop_stop)(ano_pw_thread_loop *loop); // without lock
    void (*thread_loop_lock)(ano_pw_thread_loop *loop);
    void (*thread_loop_unlock)(ano_pw_thread_loop *loop);
    ano_pw_loop *(*thread_loop_get_loop)(ano_pw_thread_loop *loop);
    ano_pw_properties *(*properties_new)(const char *key, ...); // NULL-terminated k,v
    ano_pw_stream *(*stream_new_simple)(ano_pw_loop *loop, const char *name,
                                        ano_pw_properties *props, // takes ownership
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
    ANO_ATOMIC(int)         error; // state_changed(ERROR)
    AnoAudioMixer      *mx;
    AnoAudioFormat      format;
} AnoPipewireState;

// EnumFormat pod: one reflected interleaved raw format. 42 words / 168 bytes.
static uint32_t pw_build_format_pod(uint32_t *w, AnoAudioFormat format)
{
    const AnoAudioPipeWireProjection projection = ano_audio_project_pipewire(format);
    if (!projection.valid)
        return 0;
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
    w[i++] = projection.format; w[i++] = 0u;
    w[i++] = 0x10003u; w[i++] = 0u;       // prop AUDIO_rate
    w[i++] = 4u;       w[i++] = 4u;       //   Int pod
    w[i++] = projection.sampleRate; w[i++] = 0u;
    w[i++] = 0x10004u; w[i++] = 0u;       // prop AUDIO_channels
    w[i++] = 4u;       w[i++] = 4u;       //   Int pod
    w[i++] = projection.channels; w[i++] = 0u;
    w[i++] = 0x10005u; w[i++] = 0u;       // prop AUDIO_position
    w[i++] = 8u + projection.channels * 4u; w[i++] = 13u;
    w[i++] = 4u;       w[i++] = 3u;       //   child: size 4, Id
    w[i++] = projection.positions[0];
    w[i++] = projection.channels == 2u ? projection.positions[1] : 0u;
    return i * (uint32_t)sizeof(uint32_t);
}

// RT_PROCESS: no loop lock. Pull cooked frames, set chunk, queue.
static void pw_on_process(void *data)
{
    AnoPipewireState *st = static_cast<AnoPipewireState *>(data);
    AnoPwBuffer *b = st->api.stream_dequeue_buffer(st->stream);
    if (!b)
        return;
    AnoSpaData *d = &b->buffer->datas[0];
    if (!d->data) {
        st->api.stream_queue_buffer(st->stream, b);
        return;
    }
    const uint32_t stride = ano_audio_frame_bytes(st->format);
    uint32_t frames = d->maxsize / stride;
    if (b->requested != 0u && b->requested < frames)
        frames = (uint32_t)b->requested;
    ano_audio_pull_frames(st->mx, &st->pull, static_cast<float *>(d->data), frames);
    d->chunk->offset = 0u;
    d->chunk->stride = (int32_t)stride;
    d->chunk->size   = frames * stride;
    st->api.stream_queue_buffer(st->stream, b);
}

static void pw_on_state_changed(void *data, int oldState, int newState, const char *error)
{
    (void)oldState;
    AnoPipewireState *st = static_cast<AnoPipewireState *>(data);
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
    AnoPipewireState *st = static_cast<AnoPipewireState *>(
        mi_heap_calloc(mx->heap, 1, sizeof *st));
    if (!st)
        return false;
    st->mx = mx;
    st->format = ano_audio_mix_format(mx->sampleRate);
    if (!ano_audio_backend_supports(ANO_AUDIO_BACKEND_PIPEWIRE, st->format)) {
        mi_free(st);
        return false;
    }
    atomic_init(&st->error, 0);

    st->api.lib = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!st->api.lib) {
        mi_free(st);
        return false;
    }
    char latency[32] = {};
    char rateStr[32] = {};
    ano_pw_properties *props = NULL;
    uint32_t podWords[42] = {};
    const AnoSpaPod *params[1] = {};
    int res = 0;

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

    // request block as quantum (pull absorbs any grant)
    snprintf(latency, sizeof latency, "%u/%u", mx->blockFrames, mx->sampleRate);
    snprintf(rateStr, sizeof rateStr, "1/%u", mx->sampleRate);
    props = st->api.properties_new(
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

    if (pw_build_format_pod(podWords, st->format) == 0)
        goto fail_started;
    params[0] = reinterpret_cast<const AnoSpaPod *>(podWords);
    st->api.thread_loop_lock(st->loop);
    res = st->api.stream_connect(st->stream, ANO_PW_DIRECTION_OUTPUT, ANO_PW_ID_ANY,
                                 ANO_PW_FLAG_AUTOCONNECT | ANO_PW_FLAG_MAP_BUFFERS
                                     | ANO_PW_FLAG_RT_PROCESS,
                                 params, 1u);
    st->api.thread_loop_unlock(st->loop);
    if (res < 0)
        goto fail_started;

    // wait for PAUSED (or fail so AUTO can cascade)
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
    ano_log(ANO_INFO, "audio/pipewire: stream up (lib %s), %s %s %s, requested quantum %s.",
            st->api.get_library_version(), ano_audio_sample_name(st->format.sample),
            ano_audio_layout_name(st->format.layout),
            ano_audio_interleave_name(st->format.interleave), latency);
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
    AnoPipewireState *st = static_cast<AnoPipewireState *>(mx->deviceState);
    if (!st)
        return;
    st->api.thread_loop_stop(st->loop); // without lock
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
        .backend = ANO_AUDIO_BACKEND_PIPEWIRE,
        .start = pw_start,
        .stop  = pw_stop,
    };
    return &api;
}
