/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Windows devices: WASAPI (primary), DirectSound (fallback). ole32/avrt/dsound runtime-loaded.
// Hand-declared COM vtables (miniaudio layouts). COM stays on the device thread.

#if defined(_WIN32)


#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <stddef.h>

#include "audio_internal.h"
#include "audio_pull.h"
#include <anoptic_memory_typed.h>

#include <anoptic_log.h>
#include <anoptic_time.h>

/* Wave formats */

// AnoWfxExt is flat (not nested AnoWfx): nesting would shift fields by +2 pad.

typedef struct AnoWfx
{
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} AnoWfx;

typedef struct AnoWfxExt
{
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
    union {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID  SubFormat;
} AnoWfxExt;

static_assert(offsetof(AnoWfxExt, Samples) == 18, "extensible fields start at 18");
static_assert(offsetof(AnoWfxExt, dwChannelMask) == 20, "channel mask at 20");
static_assert(offsetof(AnoWfxExt, SubFormat) == 24, "subformat at 24");

static AnoWfxExt wfx_extensible(AnoAudioFormat format)
{
    const AnoAudioWaveProjection projection =
        ano_audio_project_wave(format, AnoAudioWaveEncoding::extensible);
    AnoWfxExt w = {0};
    if (!projection.valid)
        return w;
    w.wFormatTag      = projection.formatTag;
    w.nChannels       = projection.channels;
    w.nSamplesPerSec  = projection.sampleRate;
    w.wBitsPerSample  = projection.bitsPerSample;
    w.nBlockAlign     = projection.blockAlign;
    w.nAvgBytesPerSec = projection.averageBytesPerSecond;
    w.cbSize          = projection.extraSize;
    w.Samples.wValidBitsPerSample = projection.validBitsPerSample;
    w.dwChannelMask = projection.channelMask;
    w.SubFormat = GUID{projection.subFormatData1, 0x0000, 0x0010,
                       {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    return w;
}

static AnoWfx wfx_classic(AnoAudioFormat format)
{
    const AnoAudioWaveProjection projection =
        ano_audio_project_wave(format, AnoAudioWaveEncoding::classic);
    if (!projection.valid)
        return {};
    return {
        .wFormatTag = projection.formatTag,
        .nChannels = projection.channels,
        .nSamplesPerSec = projection.sampleRate,
        .nAvgBytesPerSec = projection.averageBytesPerSecond,
        .nBlockAlign = projection.blockAlign,
        .wBitsPerSample = projection.bitsPerSample,
    };
}

/* WASAPI COM surface */

static const GUID ANO_CLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID ANO_IID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID ANO_IID_IAudioClient =
    { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const GUID ANO_IID_IAudioClient3 =
    { 0x7ED4EE07, 0x8E67, 0x4CD4, { 0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42 } };
static const GUID ANO_IID_IAudioRenderClient =
    { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };

#define ANO_CLSCTX_ALL 0x17u
#define ANO_AUDCLNT_SHAREMODE_SHARED 0
#define ANO_AUDCLNT_STREAMFLAGS_EVENTCALLBACK       0x00040000u
#define ANO_AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000u
#define ANO_AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM      0x80000000u
#define ANO_AUDCLNT_BUFFERFLAGS_SILENT 2u

// Terminal AUDCLNT refusals (audioclient.h). Outlive the reporting client.
#define ANO_AUDCLNT_E_NOT_INITIALIZED     ((HRESULT)0x88890001L)
#define ANO_AUDCLNT_E_DEVICE_INVALIDATED  ((HRESULT)0x88890004L)
#define ANO_AUDCLNT_E_SERVICE_NOT_RUNNING ((HRESULT)0x88890010L)

// Terminal IAudioRenderClient refusals (audioclient.h). Outstanding packet uncleared here.
#define ANO_AUDCLNT_E_OUT_OF_ORDER ((HRESULT)0x88890007L)
#define ANO_AUDCLNT_E_INVALID_SIZE ((HRESULT)0x88890009L)

typedef struct AnoIMMDeviceEnumerator AnoIMMDeviceEnumerator;
typedef struct AnoIMMDevice           AnoIMMDevice;
typedef struct AnoIAudioClient        AnoIAudioClient;
typedef struct AnoIAudioClient3       AnoIAudioClient3;
typedef struct AnoIAudioRenderClient  AnoIAudioRenderClient;

typedef struct AnoIMMDeviceEnumeratorVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIMMDeviceEnumerator *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIMMDeviceEnumerator *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIMMDeviceEnumerator *);
    HRESULT (STDMETHODCALLTYPE *EnumAudioEndpoints)(AnoIMMDeviceEnumerator *, int, DWORD, void **);
    HRESULT (STDMETHODCALLTYPE *GetDefaultAudioEndpoint)(AnoIMMDeviceEnumerator *, int, int, AnoIMMDevice **);
    HRESULT (STDMETHODCALLTYPE *GetDevice)(AnoIMMDeviceEnumerator *, const WCHAR *, AnoIMMDevice **);
    HRESULT (STDMETHODCALLTYPE *RegisterEndpointNotificationCallback)(AnoIMMDeviceEnumerator *, void *);
    HRESULT (STDMETHODCALLTYPE *UnregisterEndpointNotificationCallback)(AnoIMMDeviceEnumerator *, void *);
} AnoIMMDeviceEnumeratorVtbl;
struct AnoIMMDeviceEnumerator { AnoIMMDeviceEnumeratorVtbl *v; };

typedef struct AnoIMMDeviceVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIMMDevice *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIMMDevice *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIMMDevice *);
    HRESULT (STDMETHODCALLTYPE *Activate)(AnoIMMDevice *, const GUID *, DWORD, void *, void **);
    HRESULT (STDMETHODCALLTYPE *OpenPropertyStore)(AnoIMMDevice *, DWORD, void **);
    HRESULT (STDMETHODCALLTYPE *GetId)(AnoIMMDevice *, WCHAR **);
    HRESULT (STDMETHODCALLTYPE *GetState)(AnoIMMDevice *, DWORD *);
} AnoIMMDeviceVtbl;
struct AnoIMMDevice { AnoIMMDeviceVtbl *v; };

typedef struct AnoIAudioClientVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIAudioClient *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIAudioClient *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(AnoIAudioClient *, int, DWORD, int64_t, int64_t, const AnoWfx *, const GUID *);
    HRESULT (STDMETHODCALLTYPE *GetBufferSize)(AnoIAudioClient *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetStreamLatency)(AnoIAudioClient *, int64_t *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPadding)(AnoIAudioClient *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *IsFormatSupported)(AnoIAudioClient *, int, const AnoWfx *, AnoWfx **);
    HRESULT (STDMETHODCALLTYPE *GetMixFormat)(AnoIAudioClient *, AnoWfx **);
    HRESULT (STDMETHODCALLTYPE *GetDevicePeriod)(AnoIAudioClient *, int64_t *, int64_t *);
    HRESULT (STDMETHODCALLTYPE *Start)(AnoIAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Stop)(AnoIAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Reset)(AnoIAudioClient *);
    HRESULT (STDMETHODCALLTYPE *SetEventHandle)(AnoIAudioClient *, HANDLE);
    HRESULT (STDMETHODCALLTYPE *GetService)(AnoIAudioClient *, const GUID *, void **);
} AnoIAudioClientVtbl;
struct AnoIAudioClient { AnoIAudioClientVtbl *v; };

typedef struct AnoIAudioClient3Vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIAudioClient3 *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIAudioClient3 *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIAudioClient3 *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(AnoIAudioClient3 *, int, DWORD, int64_t, int64_t, const AnoWfx *, const GUID *);
    HRESULT (STDMETHODCALLTYPE *GetBufferSize)(AnoIAudioClient3 *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetStreamLatency)(AnoIAudioClient3 *, int64_t *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPadding)(AnoIAudioClient3 *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *IsFormatSupported)(AnoIAudioClient3 *, int, const AnoWfx *, AnoWfx **);
    HRESULT (STDMETHODCALLTYPE *GetMixFormat)(AnoIAudioClient3 *, AnoWfx **);
    HRESULT (STDMETHODCALLTYPE *GetDevicePeriod)(AnoIAudioClient3 *, int64_t *, int64_t *);
    HRESULT (STDMETHODCALLTYPE *Start)(AnoIAudioClient3 *);
    HRESULT (STDMETHODCALLTYPE *Stop)(AnoIAudioClient3 *);
    HRESULT (STDMETHODCALLTYPE *Reset)(AnoIAudioClient3 *);
    HRESULT (STDMETHODCALLTYPE *SetEventHandle)(AnoIAudioClient3 *, HANDLE);
    HRESULT (STDMETHODCALLTYPE *GetService)(AnoIAudioClient3 *, const GUID *, void **);
    HRESULT (STDMETHODCALLTYPE *IsOffloadCapable)(AnoIAudioClient3 *, int, BOOL *);
    HRESULT (STDMETHODCALLTYPE *SetClientProperties)(AnoIAudioClient3 *, const void *);
    HRESULT (STDMETHODCALLTYPE *GetBufferSizeLimits)(AnoIAudioClient3 *, const AnoWfx *, BOOL, int64_t *, int64_t *);
    HRESULT (STDMETHODCALLTYPE *GetSharedModeEnginePeriod)(AnoIAudioClient3 *, const AnoWfx *, UINT32 *, UINT32 *, UINT32 *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentSharedModeEnginePeriod)(AnoIAudioClient3 *, AnoWfx **, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *InitializeSharedAudioStream)(AnoIAudioClient3 *, DWORD, UINT32, const AnoWfx *, const GUID *);
} AnoIAudioClient3Vtbl;
struct AnoIAudioClient3 { AnoIAudioClient3Vtbl *v; };

typedef struct AnoIAudioRenderClientVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIAudioRenderClient *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIAudioRenderClient *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIAudioRenderClient *);
    HRESULT (STDMETHODCALLTYPE *GetBuffer)(AnoIAudioRenderClient *, UINT32, BYTE **);
    HRESULT (STDMETHODCALLTYPE *ReleaseBuffer)(AnoIAudioRenderClient *, UINT32, DWORD);
} AnoIAudioRenderClientVtbl;
struct AnoIAudioRenderClient { AnoIAudioRenderClientVtbl *v; };

/* WASAPI backend */

// Device thread: default shared endpoint, event-driven. Client3 when rates match.
// Else AUTOCONVERTPCM. Loop: wait -> GetCurrentPadding -> fill.

typedef enum AnoWinInit
{
    ANO_WIN_INIT_PENDING = 0,
    ANO_WIN_INIT_OK      = 1,
    ANO_WIN_INIT_FAILED  = -1,
} AnoWinInit;

typedef struct AnoWasapiState
{
    HMODULE ole32;
    HRESULT (WINAPI *coInit)(void *, DWORD);
    void    (WINAPI *coUninit)(void);
    HRESULT (WINAPI *coCreate)(const GUID *, void *, DWORD, const GUID *, void **);
    void    (WINAPI *coTaskFree)(void *);
    HMODULE avrt;
    HANDLE  (WINAPI *avSet)(const char *, DWORD *);
    BOOL    (WINAPI *avRevert)(HANDLE);

    ANO_ATOMIC(int)  init; // AnoWinInit
    AnoAudioPull pull;
} AnoWasapiState;

// GetBuffer + ReleaseBuffer one packet. NULL dst still acquired: ReleaseBuffer(0).
//   in:  render (non-NULL), frames (>0, <= writable), mx/pull (NULL mx = silence)
//   out: true if filled+released. *outHr always written. S_OK on success and NULL-dst arm
//   inv: every succeeded GetBuffer gets exactly one ReleaseBuffer
[[nodiscard]] static bool wasapi_write_checked(AnoIAudioRenderClient *render, UINT32 frames,
                                               AnoAudioMixer *mx, AnoAudioPull *pull, HRESULT *outHr)
{
    BYTE   *dst = NULL;
    HRESULT hr  = render->v->GetBuffer(render, frames, &dst);
    if (FAILED(hr)) {
        *outHr = hr;
        return false;
    }
    if (!dst) {
        render->v->ReleaseBuffer(render, 0, 0); // acquired but unusable: return it unwritten
        *outHr = S_OK;
        return false;
    }
    if (mx)
        ano_audio_pull_frames(mx, pull, (float *)dst, frames);
    hr = render->v->ReleaseBuffer(render, frames, mx ? 0u : ANO_AUDCLNT_BUFFERFLAGS_SILENT);
    *outHr = hr;
    return SUCCEEDED(hr);
}

// Prefill write with no refusal latch (caller has no loop).
//   out: true if filled+released
[[nodiscard]] static bool wasapi_write(AnoIAudioRenderClient *render, UINT32 frames,
                                       AnoAudioMixer *mx, AnoAudioPull *pull)
{
    HRESULT hr = S_OK;
    return wasapi_write_checked(render, frames, mx, pull, &hr);
}

// Undocumented refusal budget per render-loop arm (padding and packet counted apart).
#define ANO_WASAPI_REFUSAL_LIMIT 50u

// IAudioClient terminal refusals (invalidate, service down, not-init, E_POINTER).
//   in:  FAILED hr from a client call in the render loop
//   out: true if same client can never succeed again
[[nodiscard]] static bool wasapi_terminal(HRESULT hr)
{
    return hr == ANO_AUDCLNT_E_DEVICE_INVALIDATED || hr == ANO_AUDCLNT_E_SERVICE_NOT_RUNNING
        || hr == ANO_AUDCLNT_E_NOT_INITIALIZED    || hr == E_POINTER;
}

// IAudioRenderClient terminal refusals (wasapi_terminal + OUT_OF_ORDER + INVALID_SIZE).
//   in:  FAILED hr from a render-client call in the loop
//   out: true if same stream can never succeed again
[[nodiscard]] static bool wasapi_packet_terminal(HRESULT hr)
{
    return wasapi_terminal(hr) || hr == ANO_AUDCLNT_E_OUT_OF_ORDER
        || hr == ANO_AUDCLNT_E_INVALID_SIZE;
}

static void *wasapi_main(void *arg)
{
    AnoAudioMixer  *mx = static_cast<AnoAudioMixer *>(arg);
    AnoWasapiState *st = static_cast<AnoWasapiState *>(mx->deviceState);

    AnoIMMDeviceEnumerator *enumr  = NULL;
    AnoIMMDevice           *dev    = NULL;
    AnoIAudioClient        *client = NULL;
    AnoIAudioRenderClient  *render = NULL;
    HANDLE evt = NULL, mmcss = NULL;
    UINT32 bufferFrames = 0;
    bool   viaClient3 = false, started = false, comUp = false;
    const AnoAudioFormat format = ano_audio_mix_format(mx->sampleRate);
    AnoWfxExt wfx = wfx_extensible(format);
    AnoWfx *mix = NULL;
    uint32_t mixRate = 0, mixChannels = 0;
    uint32_t refusals = 0, packetRefusals = 0;

    if (FAILED(st->coInit(NULL, 0 /* COINIT_MULTITHREADED */)))
        goto fail;
    comUp = true;
    if (FAILED(st->coCreate(&ANO_CLSID_MMDeviceEnumerator, NULL, ANO_CLSCTX_ALL,
                            &ANO_IID_IMMDeviceEnumerator, (void **)&enumr)))
        goto fail;
    if (FAILED(enumr->v->GetDefaultAudioEndpoint(enumr, 0 /* eRender */, 0 /* eConsole */, &dev)))
        goto fail;
    if (FAILED(dev->v->Activate(dev, &ANO_IID_IAudioClient, ANO_CLSCTX_ALL, NULL,
                                (void **)&client)))
        goto fail;

    // Client3 only when mix rate/channels match (no AUTOCONVERTPCM on that path).
    if (SUCCEEDED(client->v->GetMixFormat(client, &mix)) && mix) {
        mixRate     = mix->nSamplesPerSec;
        mixChannels = mix->nChannels;
        st->coTaskFree(mix);
    }
    if (mixRate == mx->sampleRate && mixChannels == wfx.nChannels) {
        AnoIAudioClient3 *c3 = NULL;
        if (SUCCEEDED(client->v->QueryInterface(client, &ANO_IID_IAudioClient3, (void **)&c3))) {
            UINT32 def = 0, fund = 0, mn = 0, mx3 = 0;
            if (SUCCEEDED(c3->v->GetSharedModeEnginePeriod(c3, (const AnoWfx *)&wfx,
                                                           &def, &fund, &mn, &mx3))
                && fund > 0u) {
                UINT32 period = mx->blockFrames / fund * fund;
                if (period < mn) period = mn;
                if (period > mx3) period = mx3;
                if (SUCCEEDED(c3->v->InitializeSharedAudioStream(
                        c3, ANO_AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period,
                        (const AnoWfx *)&wfx, NULL)))
                    viaClient3 = true;
            }
            c3->v->Release(c3);
        }
    }
    if (!viaClient3) {
        // classic shared + AUTOCONVERTPCM
        int64_t bufferDuration = (int64_t)mx->blockFrames * 4 * 10000000ll / mx->sampleRate;
        DWORD flags = ANO_AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                    | ANO_AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                    | ANO_AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (FAILED(client->v->Initialize(client, ANO_AUDCLNT_SHAREMODE_SHARED, flags,
                                         bufferDuration, 0, (const AnoWfx *)&wfx, NULL)))
            goto fail;
    }

    if (FAILED(client->v->GetBufferSize(client, &bufferFrames)) || bufferFrames == 0u)
        goto fail;
    evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!evt || FAILED(client->v->SetEventHandle(client, evt)))
        goto fail;
    if (FAILED(client->v->GetService(client, &ANO_IID_IAudioRenderClient, (void **)&render)))
        goto fail;

    // prefill silence
    if (!wasapi_write(render, bufferFrames, NULL, NULL))
        ano_log(ANO_WARN, "audio/wasapi: silence prefill refused; starting on an unfilled buffer.");

    if (st->avSet) {
        DWORD idx = 0;
        mmcss = st->avSet("Pro Audio", &idx);
    }
    if (FAILED(client->v->Start(client)))
        goto fail;
    started = true;

    ano_log(ANO_INFO, "audio/wasapi: shared %s, %u Hz engine (mix %u Hz), device buffer %u frames.",
            viaClient3 ? "low-latency (IAudioClient3)" : "AUTOCONVERTPCM",
            mx->sampleRate, mixRate, bufferFrames);
    atomic_store_explicit(&st->init, ANO_WIN_INIT_OK, memory_order_release);

    // Terminal loop exit falls through fail: (same unwind as init). deviceRun/init stay. Audio silent.
    while (atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
        DWORD waited = WaitForSingleObject(evt, 2000);
        if (waited == WAIT_FAILED) {
            // WAIT_FAILED: evt owned here, never recovers
            ano_log(ANO_ERROR, "audio/wasapi: render event wait failed (%lu); stopping the device"
                    " thread, audio stays silent.", GetLastError());
            break;
        }
        // padding even on timeout: only classifier when the endpoint stops signalling
        UINT32 padding = 0;
        HRESULT hr = client->v->GetCurrentPadding(client, &padding);
        if (FAILED(hr)) {
            bool terminal = wasapi_terminal(hr);
            if (terminal || ++refusals >= ANO_WASAPI_REFUSAL_LIMIT) {
                ano_log(ANO_ERROR, "audio/wasapi: %s padding refusal (HRESULT 0x%08lX); stopping"
                        " the device thread, audio stays silent.",
                        terminal ? "terminal" : "unrelenting", (unsigned long)hr);
                break;
            }
            ano_sleep(10000); // transient: the next period may still land
            continue;
        }
        refusals = 0;
        if (waited != WAIT_OBJECT_0)
            continue; // no period elapsed; that read was a probe, not a turn to write
        UINT32 writable = bufferFrames - padding;
        if (writable == 0u)
            continue;
        // packet latch (OUT_OF_ORDER etc.): padding stays S_OK so client latch never sees it
        HRESULT wr = S_OK;
        if (!wasapi_write_checked(render, writable, mx, &st->pull, &wr)) {
            bool terminal = wasapi_packet_terminal(wr);
            if (terminal || ++packetRefusals >= ANO_WASAPI_REFUSAL_LIMIT) {
                ano_log(ANO_ERROR, "audio/wasapi: %s packet refusal (HRESULT 0x%08lX); stopping"
                        " the device thread, audio stays silent.",
                        terminal ? "terminal" : "unrelenting", (unsigned long)wr);
                break;
            }
            continue; // one packet skipped; the event fires again next period
        }
        packetRefusals = 0;
    }

fail:
    if (started)
        client->v->Stop(client);
    if (mmcss && st->avRevert)
        st->avRevert(mmcss);
    if (render) render->v->Release(render);
    if (client) client->v->Release(client);
    if (dev)    dev->v->Release(dev);
    if (enumr)  enumr->v->Release(enumr);
    if (evt)    CloseHandle(evt);
    if (comUp)  st->coUninit();
    if (!started)
        atomic_store_explicit(&st->init, ANO_WIN_INIT_FAILED, memory_order_release);
    return NULL;
}

static bool wasapi_start(AnoAudioMixer *mx)
{
    AnoWasapiState *st = ano::heap_allocate_zero<AnoWasapiState>(mx->heap, 1);
    if (!st)
        return false;
    st->ole32 = LoadLibraryA("ole32.dll");
    if (!st->ole32) {
        mi_free(st);
        return false;
    }
    *(FARPROC *)&st->coInit     = GetProcAddress(st->ole32, "CoInitializeEx");
    *(FARPROC *)&st->coUninit   = GetProcAddress(st->ole32, "CoUninitialize");
    *(FARPROC *)&st->coCreate   = GetProcAddress(st->ole32, "CoCreateInstance");
    *(FARPROC *)&st->coTaskFree = GetProcAddress(st->ole32, "CoTaskMemFree");
    if (!st->coInit || !st->coUninit || !st->coCreate || !st->coTaskFree) {
        FreeLibrary(st->ole32);
        mi_free(st);
        return false;
    }
    st->avrt = LoadLibraryA("avrt.dll"); // optional
    if (st->avrt) {
        *(FARPROC *)&st->avSet    = GetProcAddress(st->avrt, "AvSetMmThreadCharacteristicsA");
        *(FARPROC *)&st->avRevert = GetProcAddress(st->avrt, "AvRevertMmThreadCharacteristics");
    }
    atomic_init(&st->init, ANO_WIN_INIT_PENDING);

    mx->deviceState = st;
    atomic_store_explicit(&mx->deviceRun, true, memory_order_release);
    if (ano_thread_create(&mx->deviceThread, NULL, wasapi_main, mx) != 0)
        goto fail;

    // wait for device-thread init (bounded)
    for (uint32_t waited = 0; waited < 5000u; waited += 5u) {
        int s = atomic_load_explicit(&st->init, memory_order_acquire);
        if (s == ANO_WIN_INIT_OK)
            return true;
        if (s == ANO_WIN_INIT_FAILED)
            break;
        ano_sleep(5000);
    }
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
fail:
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    mx->deviceState = NULL;
    if (st->avrt) FreeLibrary(st->avrt);
    FreeLibrary(st->ole32);
    mi_free(st);
    return false;
}

static void wasapi_stop(AnoAudioMixer *mx)
{
    AnoWasapiState *st = static_cast<AnoWasapiState *>(mx->deviceState);
    if (!st)
        return; // start() failed and already joined/freed
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
    if (st->avrt) FreeLibrary(st->avrt);
    FreeLibrary(st->ole32);
    mi_free(st);
    mx->deviceState = NULL;
}

const AnoAudioDeviceApi *ano_audio_device_wasapi(void)
{
    static const AnoAudioDeviceApi api = {
        .backend = ANO_AUDIO_BACKEND_WASAPI,
        .start = wasapi_start,
        .stop  = wasapi_stop,
    };
    return &api;
}

/* DirectSound fallback */

// dsound.dll. Looping 4-block secondary. Cursor chase. f32 first, else s16+TPDF.

#define ANO_DSSCL_PRIORITY             2u
#define ANO_DSBCAPS_PRIMARYBUFFER      0x00000001u
#define ANO_DSBCAPS_GLOBALFOCUS        0x00008000u
#define ANO_DSBCAPS_GETCURRENTPOSITION2 0x00010000u
#define ANO_DSBPLAY_LOOPING            0x00000001u
#define ANO_DSBSTATUS_PLAYING          0x00000001u
#define ANO_DSBSTATUS_BUFFERLOST       0x00000002u

typedef struct AnoIDirectSound       AnoIDirectSound;
typedef struct AnoIDirectSoundBuffer AnoIDirectSoundBuffer;

typedef struct AnoDsBufferDesc
{
    DWORD  dwSize;
    DWORD  dwFlags;
    DWORD  dwBufferBytes;
    DWORD  dwReserved;
    AnoWfx *lpwfxFormat;
    GUID   guid3DAlgorithm;
} AnoDsBufferDesc;

typedef struct AnoIDirectSoundVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIDirectSound *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIDirectSound *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIDirectSound *);
    HRESULT (STDMETHODCALLTYPE *CreateSoundBuffer)(AnoIDirectSound *, const AnoDsBufferDesc *, AnoIDirectSoundBuffer **, void *);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(AnoIDirectSound *, void *);
    HRESULT (STDMETHODCALLTYPE *DuplicateSoundBuffer)(AnoIDirectSound *, AnoIDirectSoundBuffer *, AnoIDirectSoundBuffer **);
    HRESULT (STDMETHODCALLTYPE *SetCooperativeLevel)(AnoIDirectSound *, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *Compact)(AnoIDirectSound *);
    HRESULT (STDMETHODCALLTYPE *GetSpeakerConfig)(AnoIDirectSound *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *SetSpeakerConfig)(AnoIDirectSound *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Initialize)(AnoIDirectSound *, const GUID *);
} AnoIDirectSoundVtbl;
struct AnoIDirectSound { AnoIDirectSoundVtbl *v; };

typedef struct AnoIDirectSoundBufferVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AnoIDirectSoundBuffer *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AnoIDirectSoundBuffer *);
    ULONG   (STDMETHODCALLTYPE *Release)(AnoIDirectSoundBuffer *);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(AnoIDirectSoundBuffer *, void *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPosition)(AnoIDirectSoundBuffer *, DWORD *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetFormat)(AnoIDirectSoundBuffer *, AnoWfx *, DWORD, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetVolume)(AnoIDirectSoundBuffer *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetPan)(AnoIDirectSoundBuffer *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetFrequency)(AnoIDirectSoundBuffer *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(AnoIDirectSoundBuffer *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(AnoIDirectSoundBuffer *, AnoIDirectSound *, const AnoDsBufferDesc *);
    HRESULT (STDMETHODCALLTYPE *Lock)(AnoIDirectSoundBuffer *, DWORD, DWORD, void **, DWORD *, void **, DWORD *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Play)(AnoIDirectSoundBuffer *, DWORD, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetCurrentPosition)(AnoIDirectSoundBuffer *, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetFormat)(AnoIDirectSoundBuffer *, const AnoWfx *);
    HRESULT (STDMETHODCALLTYPE *SetVolume)(AnoIDirectSoundBuffer *, LONG);
    HRESULT (STDMETHODCALLTYPE *SetPan)(AnoIDirectSoundBuffer *, LONG);
    HRESULT (STDMETHODCALLTYPE *SetFrequency)(AnoIDirectSoundBuffer *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Stop)(AnoIDirectSoundBuffer *);
    HRESULT (STDMETHODCALLTYPE *Unlock)(AnoIDirectSoundBuffer *, void *, DWORD, void *, DWORD);
    HRESULT (STDMETHODCALLTYPE *Restore)(AnoIDirectSoundBuffer *);
} AnoIDirectSoundBufferVtbl;
struct AnoIDirectSoundBuffer { AnoIDirectSoundBufferVtbl *v; };

typedef struct AnoDsoundState
{
    HMODULE lib;
    HRESULT (WINAPI *create)(const GUID *, AnoIDirectSound **, void *);

    ANO_ATOMIC(int)  init;
    AnoAudioPull pull;
    AnoDspRng    dither;
} AnoDsoundState;

// Fill one mixer block into locked region (s16 convert if needed).
static void dsound_fill(AnoAudioMixer *mx, AnoDsoundState *st, float *fbuf,
                        void *dst, uint32_t frames, AnoAudioFormat format)
{
    ano_audio_pull_frames(mx, &st->pull, fbuf, frames);
    if (format.sample == AnoAudioSampleType::f32) {
        memcpy(dst, fbuf, (size_t)frames * ano_audio_frame_bytes(format));
        return;
    }
    ano_audio_convert_f32_s16(fbuf, static_cast<int16_t *>(dst),
                              frames * ANO_AUDIO_CHANNELS, &st->dither);
}

// Ring needs recover when lost or not playing (lost may still report PLAYING).
//   in:  status from GetStatus
//   out: true when dsound_recover must run
[[nodiscard]] static bool dsound_needs_recovery(DWORD status)
{
    return (status & (ANO_DSBSTATUS_BUFFERLOST | ANO_DSBSTATUS_PLAYING)) != ANO_DSBSTATUS_PLAYING;
}

// Restore lost ring: silence, re-anchor writeCursor to 0, Play looping.
//   in:  buf, bufferBytes
//   out: true if silent+playing and *writeCursor in phase. false leaves stopped, cursor untouched
[[nodiscard]] static bool dsound_recover(AnoIDirectSoundBuffer *buf, uint32_t bufferBytes, DWORD *writeCursor)
{
    if (FAILED(buf->v->Restore(buf)))
        return false;

    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    if (FAILED(buf->v->Lock(buf, 0, bufferBytes, &p1, &n1, &p2, &n2, 0)))
        goto stopped;
    if (p1) memset(p1, 0, n1);
    if (p2) memset(p2, 0, n2);
    buf->v->Unlock(buf, p1, n1, p2, n2);

    if (FAILED(buf->v->Play(buf, 0, 0, ANO_DSBPLAY_LOOPING)))
        goto stopped;
    *writeCursor = 0;
    return true;

stopped:
    // force stopped so caller re-enters via needs_recovery (Stop is no-op if already stopped)
    buf->v->Stop(buf);
    return false;
}

static void *dsound_main(void *arg)
{
    AnoAudioMixer *mx = static_cast<AnoAudioMixer *>(arg);
    AnoDsoundState *st = static_cast<AnoDsoundState *>(mx->deviceState);

    AnoIDirectSound       *ds        = NULL;
    AnoIDirectSoundBuffer *primary   = NULL;
    AnoIDirectSoundBuffer *secondary = NULL;
    float *fbuf = NULL;
    bool started = false;
    HWND hwnd = NULL;
    AnoAudioFormat outputFormat = ano_audio_mix_format(mx->sampleRate);
    AnoWfxExt wfx = wfx_extensible(outputFormat);
    AnoDsBufferDesc pdesc = { .dwSize = sizeof pdesc, .dwFlags = ANO_DSBCAPS_PRIMARYBUFFER };
    uint32_t blockBytes = mx->blockFrames * ano_audio_frame_bytes(outputFormat);
    AnoDsBufferDesc sdesc = {
        .dwSize = sizeof sdesc,
        .dwFlags = ANO_DSBCAPS_GLOBALFOCUS | ANO_DSBCAPS_GETCURRENTPOSITION2,
        .dwBufferBytes = 4u * blockBytes,
        .lpwfxFormat = reinterpret_cast<AnoWfx *>(&wfx),
    };
    uint32_t bufferBytes = 0;
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    DWORD writeCursor = 0;

    if (FAILED(st->create(NULL, &ds, NULL)))
        goto fail;
    hwnd = GetForegroundWindow();
    if (!hwnd) hwnd = GetDesktopWindow();
    if (FAILED(ds->v->SetCooperativeLevel(ds, hwnd, ANO_DSSCL_PRIORITY)))
        goto fail;

    // primary format advisory
    if (SUCCEEDED(ds->v->CreateSoundBuffer(ds, &pdesc, &primary, NULL)))
        primary->v->SetFormat(primary, (const AnoWfx *)&wfx);

    if (FAILED(ds->v->CreateSoundBuffer(ds, &sdesc, &secondary, NULL))) {
        outputFormat.sample = AnoAudioSampleType::s16;
        AnoWfx w16 = wfx_classic(outputFormat);
        blockBytes = mx->blockFrames * ano_audio_frame_bytes(outputFormat);
        sdesc.dwBufferBytes = 4u * blockBytes;
        sdesc.lpwfxFormat   = &w16;
        if (FAILED(ds->v->CreateSoundBuffer(ds, &sdesc, &secondary, NULL)))
            goto fail;
        ano_dsp_rng_seed(&st->dither, 0xD50DDu);
    }
    bufferBytes = 4u * blockBytes;

    fbuf = ano::heap_allocate_zero<float>(
        mx->heap, (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS);
    if (!fbuf)
        goto fail;

    if (SUCCEEDED(secondary->v->Lock(secondary, 0, bufferBytes, &p1, &n1, &p2, &n2, 0))) {
        if (p1) memset(p1, 0, n1);
        if (p2) memset(p2, 0, n2);
        secondary->v->Unlock(secondary, p1, n1, p2, n2);
    }
    if (FAILED(secondary->v->Play(secondary, 0, 0, ANO_DSBPLAY_LOOPING)))
        goto fail;
    started = true;

    ano_log(ANO_INFO, "audio/dsound: secondary %s ring %u bytes (4 blocks).",
            ano_audio_sample_name(outputFormat.sample), bufferBytes);
    atomic_store_explicit(&st->init, ANO_WIN_INIT_OK, memory_order_release);

    // cursor chase: fill block-sized chunks the play cursor has consumed
    while (atomic_load_explicit(&mx->deviceRun, memory_order_acquire)) {
        DWORD status = 0;
        if (SUCCEEDED(secondary->v->GetStatus(secondary, &status))
            && dsound_needs_recovery(status)
            && !dsound_recover(secondary, bufferBytes, &writeCursor)) {
            ano_sleep(10000);   // still down: retry, do not chase a dead cursor
            continue;
        }
        DWORD play = 0;
        if (FAILED(secondary->v->GetCurrentPosition(secondary, &play, NULL))) {
            ano_sleep(10000);
            continue;
        }
        DWORD writable = (play + bufferBytes - writeCursor) % bufferBytes;
        bool  wrote = false;
        while (writable >= blockBytes) {
            if (FAILED(secondary->v->Lock(secondary, writeCursor, blockBytes,
                                          &p1, &n1, &p2, &n2, 0)))
                break;
            // block-aligned locks in a block-multiple ring never split
            if (n1 == blockBytes && p1)
                dsound_fill(mx, st, fbuf, p1, mx->blockFrames, outputFormat);
            secondary->v->Unlock(secondary, p1, n1, p2, n2);
            writeCursor = (writeCursor + blockBytes) % bufferBytes;
            writable   -= blockBytes;
            wrote = true;
        }
        if (!wrote)
            ano_sleep(2000);
    }

fail:
    if (started)
        secondary->v->Stop(secondary);
    if (secondary) secondary->v->Release(secondary);
    if (primary)   primary->v->Release(primary);
    if (ds)        ds->v->Release(ds);
    if (fbuf)      mi_free(fbuf);
    if (!started)
        atomic_store_explicit(&st->init, ANO_WIN_INIT_FAILED, memory_order_release);
    return NULL;
}

static bool dsound_start(AnoAudioMixer *mx)
{
    AnoDsoundState *st = ano::heap_allocate_zero<AnoDsoundState>(mx->heap, 1);
    if (!st)
        return false;
    st->lib = LoadLibraryA("dsound.dll");
    if (!st->lib) {
        mi_free(st);
        return false;
    }
    *(FARPROC *)&st->create = GetProcAddress(st->lib, "DirectSoundCreate");
    if (!st->create) {
        FreeLibrary(st->lib);
        mi_free(st);
        return false;
    }
    atomic_init(&st->init, ANO_WIN_INIT_PENDING);

    mx->deviceState = st;
    atomic_store_explicit(&mx->deviceRun, true, memory_order_release);
    if (ano_thread_create(&mx->deviceThread, NULL, dsound_main, mx) != 0)
        goto fail;
    for (uint32_t waited = 0; waited < 5000u; waited += 5u) {
        int s = atomic_load_explicit(&st->init, memory_order_acquire);
        if (s == ANO_WIN_INIT_OK)
            return true;
        if (s == ANO_WIN_INIT_FAILED)
            break;
        ano_sleep(5000);
    }
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
fail:
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    mx->deviceState = NULL;
    FreeLibrary(st->lib);
    mi_free(st);
    return false;
}

static void dsound_stop(AnoAudioMixer *mx)
{
    AnoDsoundState *st = static_cast<AnoDsoundState *>(mx->deviceState);
    if (!st)
        return; // start() failed and already joined/freed
    atomic_store_explicit(&mx->deviceRun, false, memory_order_release);
    ano_thread_join(mx->deviceThread, NULL);
    FreeLibrary(st->lib);
    mi_free(st);
    mx->deviceState = NULL;
}

const AnoAudioDeviceApi *ano_audio_device_dsound(void)
{
    static const AnoAudioDeviceApi api = {
        .backend = ANO_AUDIO_BACKEND_DSOUND,
        .start = dsound_start,
        .stop  = dsound_stop,
    };
    return &api;
}

#endif // _WIN32
