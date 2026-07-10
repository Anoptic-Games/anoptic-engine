# miniaudio.h legend

Line index for `docs/references/miniaudio.h` (miniaudio 0.11.25, 95,864 lines).
The file is a read-only protocol reference for the engine's hand-rolled device
backends — never edit it, or every index below goes stale. Read targeted ranges
only; the whole file is 3.1 MB. Regeneration commands are at the bottom.

miniaudio is not vendored into the build. We consult its per-platform device
code as ground truth for OS protocol handling (format negotiation, recovery
loops, COM plumbing, device-change tracking) while the engine keeps its own
mixer, transport, and per-platform `src/audio/audio_<plat>.c` files. Note it
has no PipeWire backend — for pw_stream the references are the PipeWire docs;
the PulseAudio section below matters only for its pipewire-pulse workarounds.

## File map

| Range | Content |
|---|---|
| 1–3736 | usage manual (prose): build flags, backend list, examples |
| 3737–6594 | decl: primitives — formats, dither, channel maps, conversion, resampler |
| 6596–11545 | decl: DEVICE I/O — enums, config/context/device structs, MA_API surface |
| 9947, 10113, 10299, 10617, 11110 | decl: decoding, encoding, resource manager, node graph, engine (unused by us) |
| 11552 | `MINIAUDIO_IMPLEMENTATION` begins |
| 12221 | `MA_DEFAULT_PERIOD_SIZE_IN_MILLISECONDS_LOW_LATENCY` (10 ms) and friends |
| 19473–20930 | impl: device-layer common machinery |
| 20931–42400 | impl: the backends (table below) |
| 42495–44500 | impl: device init/start/stop plumbing + worker thread |
| 46756 | `ma_pcm_f32_to_s16` impl (the dithered conversion our ALSA fallback mirrors) |

## Core device-layer anchors

The architecture to read before any backend:

| Line | Symbol | Why it matters |
|---|---|---|
| 7102 | `struct ma_device_config` | per-backend config arms: wasapi 7149, alsa 7156, pulse 7162, coreaudio 7166 |
| 7293 | `struct ma_backend_callbacks` | THE backend contract: onContextInit/onDeviceInit/onDeviceStart/onDeviceStop/onDeviceRead/onDeviceWrite/onDeviceDataLoop(+Wakeup) |
| 7372 | `struct ma_context` | dlopen'd symbol tables live in its union: dsound 7416, alsa 7515 (all `ma_proc` members — the full libasound import list), pulse 7587, coreaudio 7643 |
| 7781 | `struct ma_device` | per-device backend state union at 7863: wasapi 7872 (note actualBufferSizeInFramesPlayback comment on IAudioClient3 semantics 7876) |
| 20713/42495 | `ma_device__post_init_setup` | decl/impl: converts granted device format <-> client format |
| 20737 | `ma_device_audio_thread__default_read_write` | the generic data loop for read/write-style backends (ALSA uses this shape) |
| 42762 | `ma_worker_thread` | the audio thread: init handshake, data loop dispatch (42833) |
| 43173 | `ma_context_init` impl | backend priority order + fallback cascade |
| 43606 | `ma_device_init` impl | descriptor negotiation flow |
| 44270 / 44339 | `ma_device_start` / `ma_device_stop` impl | state machine + operation semaphores |
| 44491 | `ma_device_handle_backend_data_callback` | entry the callback-style backends (pulse, coreaudio, jack) feed |

## Backend section ranges

| Backend | Range | Data-delivery style |
|---|---|---|
| Null | 20931–21704 | timer-paced read/write (reference for a minimal backend) |
| WASAPI | 21705–25213 | event-driven read/write loop |
| DirectSound | 25214–26998 | polling data loop (`ma_device_data_loop__dsound` 26410) |
| WinMM | 26999–28098 | header-queue loop (legacy; we skip it) |
| ALSA | 28099–30259 | poll-based read/write loop |
| PulseAudio | 30260–32796 | mainloop callbacks |
| JACK | 32797–33468 | process callback |
| Core Audio | 33469–36713 | AudioUnit render callback |
| sndio | 36714–37560 | (BSD; out of scope) |
| audio(4) | 37561–38457 | (BSD; out of scope) |
| OSS | 38458–39091 | (out of scope) |
| AAudio | 39092–40227 | (Android; out of scope) |
| OpenSL | 40228–41491 | (Android; out of scope) |
| Web Audio | 41492–~42400 | (out of scope) |

## ALSA (Phase 1 fallback — read in this order)

28099–28495 is the dlopen surface: `ma_snd_*_proc` typedefs for every libasound
symbol used (e.g. `ma_snd_pcm_recover_proc` 28376); the matching `ma_proc`
table is ma_context's alsa arm at 7515; the `ma_dlsym` loading of all of them
sits in `ma_context_init__alsa` 30004–30259.

| Line | Function |
|---|---|
| 28687 | `ma_context_open_pcm__alsa` — device-name resolution + `snd_pcm_open` mode flags |
| 29212 | `ma_device_init_by_type__alsa` — THE hw_params negotiation dance: access mode (mmap vs rw), format (incl. float fallback order), channels, rate, period/buffer sizing, sw_params |
| 29650 | `ma_device_init__alsa` |
| 29677 / 29710 | `ma_device_start__alsa` / `ma_device_stop__alsa` (start/drain/drop rules) |
| 29770 | `ma_device_wait__alsa` — poll() on PCM descriptors + the wakeup eventfd trick |
| 29842 / 29898 | `ma_device_read__alsa` / `ma_device_write__alsa` — `snd_pcm_recover` on EPIPE/ESTRPIPE (29874, 29929: xrun/suspend recovery, silent=true) |
| 29960 | `ma_device_data_loop_wakeup__alsa` — eventfd write to break poll() |
| 28509 | device blacklists + name-format helpers (28496–28686) |

Enumeration (28807) and device-info rate iteration (29007–29192) matter only
when we add device selection; the engine currently opens "default".

## PulseAudio (only as pipewire-pulse behavior reference)

We write no Pulse code, but pipewire-pulse mimics these paths.

| Line | Anchor |
|---|---|
| 31926 | the PipeWire small-buffer glitch workaround: 25 ms forced default period (comment + `ma_calculate_period_size_in_frames_from_descriptor__pulse` 31922) |
| 32138, 32300 | PipeWire AUX-channel-map bug workarounds in device info/init |
| 31785 | `ma_device_write_to_stream__pulse` — begin_write/write cycle shape |
| 31948 | `ma_device_init__pulse` — buffer attr (tlength/maxlength) negotiation |
| 32392 | cork/uncork start/stop semantics |

## WASAPI (Phase 5)

COM-from-C plumbing: IIDs/CLSIDs table 21776+ (`MA_IID_IAudioClient3` 21776),
`ma_IMMNotificationClient` vtable struct 21886, `ma_IAudioClient3` vtable
22123–22200. `MA_AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` 21817.

| Line | Function |
|---|---|
| 22367 / 22475 | `OnDeviceStateChanged` / `OnDefaultDeviceChanged` — the notification client impl; vtable instance 22590 |
| 22626–22800 | the context command thread (device ops marshalled off the COM callback thread — the pattern our reopen-between-blocks design replaces) |
| 23626 | `ma_device_init_internal__wasapi` — the negotiation core: IAudioClient3 `GetSharedModeEnginePeriod`/`InitializeSharedAudioStream` low-latency path, AUTOCONVERTPCM handling (23657: the flag is NOT used with IAudioClient3 — the documented incompatibility), format fallback, event-callback wiring |
| 24108 / 24514 | `ma_device_reinit__wasapi` / `ma_device_reroute__wasapi` — device-loss + default-change recovery |
| 24229 | `ma_device_init__wasapi` |
| 24573 / 24704 | start / stop |
| 24725 / 24922 | read / write — `GetCurrentPadding`-driven chunking |
| 25062 | `ma_context_init__wasapi` — runtime-linked ole32/CoInitialize handling |
| 3643 | manual note: IAudioClient3 + AUTOCONVERTPCM failure mode (0x88890021) |

## DirectSound (Phase 5 fallback)

| Line | Function |
|---|---|
| 25580 | speaker-config -> channel map |
| 26118 | period sizing from descriptor |
| 26135 | `ma_device_init__dsound` — primary/secondary buffer creation, WAVEFORMATEXTENSIBLE |
| 26410 | `ma_device_data_loop__dsound` — the play-cursor chase loop (the whole art of DSound) |
| 26949 | `ma_context_init__dsound` — dlopen dsound.dll, `DirectSoundCreate` |

## Core Audio (Phase 5)

| Line | Function |
|---|---|
| 33895 | device object-ID enumeration (AudioObjectGetPropertyData patterns) |
| 34465 | `ma_find_best_format__coreaudio` |
| 35089 | `ma_on_output__coreaudio` — the AudioUnit render callback (ring feed shape) |
| 35378 | `ma_default_device_changed__coreaudio` + tracking init 35453–35560 |
| 35769 | `ma_device_init_internal__coreaudio` — AUHAL setup: enable IO, set device, stream format, buffer size, callback |
| 36261 / 36419 / 36443 | init / start / stop |
| 36518 | `ma_context_init__coreaudio` — runtime-linked CoreFoundation/CoreAudio/AudioToolbox |

## Regeneration

Against a fresh copy, in `docs/references/`:

    wc -l miniaudio.h
    grep -n "Backend$" miniaudio.h                          # section banners
    grep -n "^static [A-Za-z_][A-Za-z_0-9 *]*ma_[a-z0-9_]*__<backend>(" miniaudio.h
    grep -n "^struct ma_context$\|^struct ma_device$" miniaudio.h
    grep -n "MINIAUDIO_IMPLEMENTATION\|IAudioClient3\|IMMNotificationClient" miniaudio.h

Every table above came from these passes plus targeted greps quoted in place.
