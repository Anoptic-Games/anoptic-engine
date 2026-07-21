# PipeWire dlopen ABI sheet (verified 1.0.5)

The minimal `libpipewire-0.3.so.0` surface `src/audio/audio_linux.c` hand-declares
for pw_stream playback, extracted from upstream headers at tag 1.0.5 and verified
against this machine's runtime lib (`nm -D`: all symbols present). SPA pods are
wire-stable serialized ABI; the structs below are public stable ABI. x86-64 LP64.
If the backend ever misbehaves on a newer PipeWire, re-verify against this sheet
first.

## Constants

| Symbolic | Value |
|---|---|
| SPA_TYPE_Id / Int / Array / Object | 3 / 4 / 13 / 15 |
| SPA_TYPE_OBJECT_Format | 0x40003 |
| SPA_PARAM_EnumFormat / Format | 3 / 4 |
| SPA_FORMAT_mediaType / mediaSubtype | 1 / 2 |
| SPA_FORMAT_AUDIO_format / rate / channels / position | 0x10001 / 0x10003 / 0x10004 / 0x10005 |
| SPA_MEDIA_TYPE_audio, SPA_MEDIA_SUBTYPE_raw | 1, 1 |
| SPA_AUDIO_FORMAT_F32 (= F32_LE on LE) | 283 (0x11B) |
| SPA_AUDIO_FORMAT_S16 (= S16_LE on LE) | 259 (0x103) |
| SPA_AUDIO_CHANNEL_FL / FR | 3 / 4 |
| PW_ID_ANY | 0xFFFFFFFF |
| PW_DIRECTION_OUTPUT (= SPA_DIRECTION_OUTPUT) | 1 |
| PW_STREAM_FLAG_AUTOCONNECT / MAP_BUFFERS / RT_PROCESS | 1<<0 / 1<<2 / 1<<4 |
| PW_VERSION_STREAM_EVENTS | 2 |
| pw_stream_state: ERROR / UNCONNECTED / CONNECTING / PAUSED / STREAMING | -1 / 0 / 1 / 2 / 3 |

## Structs (LP64 layouts)

- `spa_pod { uint32 size; uint32 type; }` 〜 size counts the body only; every
  complete pod is padded to an 8-byte boundary. Id/Int values: 12-byte pod +
  4 pad. Array bodies pack elements with no per-element headers or padding.
- `spa_pod_object body { uint32 type; uint32 id; }` then a series of props.
- `spa_pod_prop { uint32 key; uint32 flags; spa_pod value; }`.
- `spa_chunk { uint32 offset; uint32 size; int32 stride; int32 flags; }` (16 B).
- `spa_data { uint32 type; uint32 flags; int64 fd; uint32 mapoffset; uint32 maxsize; void *data; spa_chunk *chunk; }` (40 B).
- `spa_buffer { uint32 n_metas; uint32 n_datas; spa_meta *metas; spa_data *datas; }` (24 B).
- `pw_buffer { spa_buffer *buffer; void *user_data; uint64 size; uint64 requested; uint64 time; }`
  (40 B at 1.0.5 〜 `requested` since 0.3.49, `time` since 1.0.5; stream-allocated,
  never sizeof'd by the client).
- `pw_stream_events` (96 B): `uint32 version` (+4 pad) then function pointers in
  order: destroy, state_changed(data, old, state, error), control_info,
  io_changed, param_changed, add_buffer, remove_buffer, process(data), drained,
  command, trigger_done. All optional (NULL-checked). The struct is held by
  POINTER for the stream's lifetime 〜 it must be static or outlive the stream.

## Functions (all in libpipewire-0.3.so.0)

```c
void pw_init(int *argc, char **argv[]);            // both NULL ok
void pw_deinit(void);
const char *pw_get_library_version(void);
struct pw_thread_loop *pw_thread_loop_new(const char *name, const struct spa_dict *props);
void pw_thread_loop_destroy(struct pw_thread_loop *);
int  pw_thread_loop_start(struct pw_thread_loop *);
void pw_thread_loop_stop(struct pw_thread_loop *); // call WITHOUT the lock held
void pw_thread_loop_lock(struct pw_thread_loop *); // recursive
void pw_thread_loop_unlock(struct pw_thread_loop *);
struct pw_loop *pw_thread_loop_get_loop(struct pw_thread_loop *);
struct pw_properties *pw_properties_new(const char *key, ...); // k,v pairs, NULL-terminated
struct pw_stream *pw_stream_new_simple(struct pw_loop *, const char *name,
        struct pw_properties *props /* ownership taken */,
        const struct pw_stream_events *, void *data);
void pw_stream_destroy(struct pw_stream *);
int  pw_stream_connect(struct pw_stream *, enum pw_direction, uint32_t target_id,
        enum pw_stream_flags, const struct spa_pod **params, uint32_t n_params);
int  pw_stream_disconnect(struct pw_stream *);
enum pw_stream_state pw_stream_get_state(struct pw_stream *, const char **error);
struct pw_buffer *pw_stream_dequeue_buffer(struct pw_stream *);
int  pw_stream_queue_buffer(struct pw_stream *, struct pw_buffer *);
```

Property key strings: `media.type`, `media.category`, `media.role`, `node.name`,
`node.latency` ("512/48000"), `node.rate` ("1/48000"), `application.name`.

## The EnumFormat pod (fixed F32/rate/2ch, position [FL,FR])

168 bytes, 42 little-endian u32 words; body size 160. Word layout (the builder in
audio_linux.c reproduces exactly this; prop order matches
spa_format_audio_raw_build): object header {160, 15} {0x40003, 3}; then props
mediaType=Id 1, mediaSubtype=Id 1, AUDIO_format=Id 283, AUDIO_rate=Int rate,
AUDIO_channels=Int 2 (each: {key, 0} {4, type} {value, pad}); then
AUDIO_position {0x10005, 0} {16, 13} {4, 3} then packed [3, 4].

Fixed (non-choice) values are the canonical form 〜 upstream
`src/examples/audio-src.c` connects with exactly one such EnumFormat pod and
flags AUTOCONNECT|MAP_BUFFERS|RT_PROCESS.

## Teardown order (from upstream examples + thread-loop.h)

`pw_thread_loop_stop` (lock NOT held) → `pw_stream_destroy` → 
`pw_thread_loop_destroy` → `pw_deinit`. Connect/disconnect and `get_state`
polling happen under `pw_thread_loop_lock`. With RT_PROCESS the `process`
callback runs on the RT data thread without the loop lock 〜 it must stay
lock-free (our ring pop is).
