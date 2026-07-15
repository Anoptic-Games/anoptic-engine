/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Engine <-> renderer public contract: lifecycle + logic->render commands.
// Sole renderer header for the engine entry point. Render world (Vulkan + GLFW) on main thread; logic/ECS sole command producer via AnoRenderBridge. Transport (SPSC rings, bridge, events, DisplayState) private to src/render_bridge/.
// This is where the renderer contract is declared: function signatures, constants, and types used as inputs or outputs by those functions. It is the bridge betwixt engine <===> renderer.

#ifndef ANOPTIC_RENDER_H
#define ANOPTIC_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <anoptic_math.h>      // mat4, Vector4
#include <anoptic_resources.h> // anores_t, ano_res_lifetime, ano_res_read (renderer retains handles)
#include <anoptic_text.h>      // AnoFontBake, AnoGlyphInstance (logic-side text shaping)
#include <anoptic_ui.h>        // AnoUiPrim/Clip/Paint/Stop + builder (logic-side UI layout)


/* Renderer Lifecycle */

// Render world; runs on the main thread. GLFW pins window + events to main (required on macOS).

// Bring up render world (window, device, assets). false on failure.
bool initVulkan(void);

// A celebration. Tears the render world down; destroys the bridge.
void unInitVulkan(void);

// Render one frame; drains pending render commands.
void drawFrame(void);

// true once the window has been asked to close.
bool anoShouldClose(void);


/* Bridge Handle */

// Opaque logic<->render channel. Created in initVulkan(), destroyed in unInitVulkan(). Transport private in src/render_bridge/.
typedef struct AnoRenderBridge AnoRenderBridge;

// Producer endpoint. Valid once initVulkan() has returned.
AnoRenderBridge *anoRenderBridge(void);


/* Loaded Assets */

// After initVulkan(): query asset GPU mesh/material indices; logic emits creates. Read-only. Render world owns assets; logic composes the scene.

// One spawnable primitive: GPU mesh + material + world transform under caller's root.
typedef struct AnoRenderableDesc
{
    mat4     transform;
    uint32_t mesh_index;
    uint32_t material_index;
} AnoRenderableDesc;

// Renderer device context. Opaque; definition is render-private.
typedef struct VulkanContext VulkanContext;

// TODO(W7, M12): unimplemented. Bind conditioned scene to GPU (LOD geometry, gated textures, baked materials). Returns 'GBND' derived handle {geometry_pool_index | material_index | bindless_index} under res_rid_derived(scene_rid, 'GBND'). Sentinel on failure. Renderer retains handles; names no path. Replaces parseGltf + ModelAsset; queries take anores_t binding.
anores_t ano_render_bind_scene(VulkanContext *ctx, ano_res_lifetime lifetime,
                               const ano_res_read *read, anores_t scene);

// Asset slot count at init (index space for queries below).
// TODO(W7, M12): deleted; logic holds binding handles.
uint32_t anoRenderAssetCount(void);

// Flatten asset `asset_id` at `root` into primitives. Returns TOTAL count; fills out[0..min(count,cap)). Cap 0 / out NULL sizes. Out-of-range -> 0.
// TODO(W7, M12): takes anores_t binding, not asset_id.
uint32_t anoRenderAssetPrimitives(uint32_t asset_id, const mat4 root, AnoRenderableDesc *out, uint32_t cap);

// Fallback cube mesh index + default material for procedural renderables.
uint32_t anoRenderFallbackMesh(void);
uint32_t anoRenderDefaultMaterial(void);

// Immutable font bake for logic-side ano_text_shape/_runs (any thread). NULL if text init failed (shape yields 0). Valid after initVulkan(); read-only.
const AnoFontBake *anoRenderTextBake(void);

// Static light-palette rows are [0, this). Runtime attach owns rows above. Scene light_index MUST be < this.
uint32_t anoRenderStaticLightBase(void);


/* Command Protocol */

// Logic -> render.

// Absent-attribute sentinels. NO_MESH = no geometry (cull skips); NO_LIGHT = drives no light.
#define ANO_RENDER_NO_MESH  0xFFFFFFFFu
#define ANO_RENDER_NO_LIGHT 0xFFFFFFFFu

// Fallback cube geometry-pool index; assigned first at upload.
#define FALLBACK_MESH_INDEX 0

// glTF KHR_lights_punctual photometrics. World pose derived render-side from parent transform.
typedef enum RenderLightType
{
    RENDER_LIGHT_DIRECTIONAL = 0,
    RENDER_LIGHT_POINT       = 1,
    RENDER_LIGHT_SPOT        = 2,
} RenderLightType;

typedef struct RenderLightParams
{
    float           color[3];     // linear RGB, normalized (intensity carries magnitude)
    float           intensity;    // candela-like (point/spot) or lux-like (directional)
    float           range;        // attenuation cutoff; <= 0 == unbounded (ignored for directional)
    float           innerConeCos; // spot inner cone half-angle cosine
    float           outerConeCos; // spot outer cone half-angle cosine
    RenderLightType type;
    float           localDir[3];  // spot/dir aim in parent MODEL space; (0,0,0) -> parent -Z. Point: ignored.
    uint32_t        castsShadow;  // 1 = allocate runtime shadow frustum (budget; silent fail if full). dir/spot = 1, point = 6. Togglable via ANO_LIGHT_FIELD_CAST.
} RenderLightParams;

// Field mask for ano_render_light_update_fields. Unnamed fields preserved. ALL = full overwrite (preserves cast). CAST is outside ALL: allocate/free shadow frustum only on explicit request.
enum {
    ANO_LIGHT_FIELD_COLOR     = 1 << 0, // color[3]
    ANO_LIGHT_FIELD_INTENSITY = 1 << 1, // intensity
    ANO_LIGHT_FIELD_RANGE     = 1 << 2, // range
    ANO_LIGHT_FIELD_CONE      = 1 << 3, // innerConeCos + outerConeCos
    ANO_LIGHT_FIELD_TYPE      = 1 << 4, // type
    ANO_LIGHT_FIELD_OFFSET    = 1 << 5, // light_offset[3]
    ANO_LIGHT_FIELD_DIRECTION = 1 << 6, // localDir[3] (spot/dir aim)
    ANO_LIGHT_FIELD_ALL       = (1 << 7) - 1, // bits 0..6; preserves cast state
    ANO_LIGHT_FIELD_CAST      = 1 << 7, // toggle castsShadow. Outside ALL. On->off frees frustum; off->on reallocates if budget allows.
};

// Per light-type occlusion: shadow map vs radiance cascades. RC-occluded types skip shadow depth render. Default = all shadow-mapped. L key toggles.
typedef enum AnoLightingMode
{
    ANO_LIGHTING_SHADOWMAP  = 0, // all sources shadow-mapped (default)
    ANO_LIGHTING_HYBRID     = 1, // RC for point, shadow maps for dir + spot
    ANO_LIGHTING_RC         = 2, // all via radiance cascades (no shadow maps)
    ANO_LIGHTING_MODE_COUNT = 3,
} AnoLightingMode;

// Continuous GPU-derived motion. Set once via RFIELD_ANIM; GPU evaluates from global time. Streamed = CPU/physics path.
typedef enum AnoMotionType
{
    ANO_MOTION_STATIC = 0, // live transform == base pose
    ANO_MOTION_SPIN,       // constant-rate local rotation (base * R)
    ANO_MOTION_ORBIT,      // constant-rate revolution about world axis (R * base)
    ANO_MOTION_LINEAR,     // constant-velocity translation from base
    ANO_MOTION_KEPLER,     // elliptical orbit; base origin is the focus
    ANO_MOTION_STREAMED,   // CPU-driven; transform via RCMD_STREAM_TRANSFORMS
} AnoMotionType;

// Per-renderable motion. 48 bytes std430. epoch = t0; GPU uses (time - epoch).
//   SPIN/ORBIT : p0.xyz = axis * angular_speed (rad/s)
//   LINEAR     : p0.xyz = velocity (units/s)
//   KEPLER     : p0 = (a, e, i, Ω); p1 = (ω, M0, n, _) [rad]
typedef struct AnoMotionDescriptor
{
    uint32_t type;   // AnoMotionType
    uint32_t flags;  // reserved
    float    epoch;  // t0 global-time stamp
    float    _pad;   // aligns p0 to 16 bytes
    Vector4  p0;
    Vector4  p1;
} AnoMotionDescriptor; // 48 bytes

typedef enum RenderCommandKind
{
    RCMD_CREATE,       // new renderable; full initial state
    RCMD_UPDATE,       // discrete change(s); see `fields`
    RCMD_DESTROY,      // remove renderable (render_id only)
    RCMD_BULK_CREATE,  // contiguous mass spawn; see `batch`
    RCMD_BULK_UPDATE,  // shared field mask across render_id array; see `update`
    RCMD_BULK_DESTROY, // mass despawn; see `destroy`
    RCMD_STREAM_TRANSFORMS, // one streamed-transform ring slice; see ano_render_stream_begin
    RCMD_LIGHT_ATTACH,      // attach runtime light to renderable; see ano_render_light_attach
    RCMD_LIGHT_UPDATE,      // change attached light params/offset (by light_id)
    RCMD_LIGHT_DETACH,      // remove attached light (by light_id)
    RCMD_TEXT_SET,          // replace screen-text block; see ano_render_text_set
    RCMD_TEXT_CLEAR,        // remove screen-text block
    RCMD_UI_SET,            // replace UI block; see ano_render_ui_set
    RCMD_UI_CLEAR,          // remove UI block
} RenderCommandKind;

// Which payload fields a CREATE/UPDATE carries. Several bits OK (<=1 message per entity per tick).
typedef enum RenderFieldBits
{
    RFIELD_TRANSFORM = 1 << 0, // teleport: rewrite BASE pose, not GPU-output transform
    RFIELD_MESH_MAT  = 1 << 1, // mesh and/or material index
    RFIELD_ANIM      = 1 << 2, // GPU motion parameters
    RFIELD_LIGHT     = 1 << 3, // light photometrics
    RFIELD_USERDATA  = 1 << 4, // packed per-entity instance channel
} RenderFieldBits;

// Per-entity instance channel. Game owns pack/unpack.
//   packed[0] : RGBA8 tint (unpackUnorm4x8)
//   packed[1] : flags (bit 0 = tint enabled)
//   packed[2..3], params : reserved
// All-zero = inert (renderer ignores).
typedef struct AnoInstanceData
{
    uint32_t packed[4];
    Vector4  params;
} AnoInstanceData; // 32 bytes; std430 { uvec4 packed; vec4 params; }

// RCMD_BULK_CREATE payload. No public submit helper. UPDATE/DESTROY helpers copy-at-submit; render frees those.
typedef struct RenderCreateBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names
    const mat4     *transforms;  // [count] base poses
    const AnoMotionDescriptor *motion; // [count] GPU motion (ANO_MOTION_STATIC for none)
    const uint32_t *mesh;        // [count] geometry pool indices (ANO_RENDER_NO_MESH allowed)
    const uint32_t *material;    // [count] material palette indices
} RenderCreateBatch;

// RCMD_BULK_UPDATE: one shared fields mask across render_ids. Only flagged arrays read (rest may be NULL). RFIELD_LIGHT not bulk. Submit copies; caller arrays live until return.
typedef struct RenderUpdateBatch
{
    uint32_t        count;
    uint32_t        fields;       // RenderFieldBits shared by every entry
    const uint32_t *render_ids;   // [count] targets (unresolved skipped)
    const mat4     *transforms;   // [count] if RFIELD_TRANSFORM
    const AnoMotionDescriptor *motion;        // [count] if RFIELD_ANIM
    const uint32_t *mesh;         // [count] if RFIELD_MESH_MAT
    const uint32_t *material;     // [count] if RFIELD_MESH_MAT
    const AnoInstanceData *instance_data;     // [count] if RFIELD_USERDATA
} RenderUpdateBatch;

// RCMD_BULK_DESTROY: render_id array in one ring message. Submit copies; caller lives until return.
typedef struct RenderDestroyBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] (unresolved skipped)
} RenderDestroyBatch;

// Screen-text instance capacity. One block truncates to this; composed union truncates in block order.
#define ANO_RENDER_TEXT_MAX 8192u

// RCMD_TEXT_SET block. text_id producer-owned. SET replaces; CLEAR removes. Logical units of overlay (uiWidth/uiHeight). Submit copies.
typedef struct RenderTextBlock
{
    uint32_t                count;
    const AnoGlyphInstance *instances;  // [count] shaped glyphs (48-byte GPU ABI)
} RenderTextBlock;

// Per-block caps for RCMD_UI_SET. Overflowing the composed union skips the whole block.
#define ANO_RENDER_UI_MAX_PRIMS  1024u
#define ANO_RENDER_UI_MAX_CLIPS  64u
#define ANO_RENDER_UI_MAX_PAINTS 64u
#define ANO_RENDER_UI_MAX_STOPS  256u
#define ANO_RENDER_UI_MAX_CURVES 8192u // packed path curve words per block
#define ANO_RENDER_UI_MAX_GLYPHS 2048u

// UI block surface. Coords are logical units of that surface; mapping folded once at compose. v0: OVERLAY only.
#define ANO_UI_SURFACE_OVERLAY 0u

// RCMD_UI_SET block. ui_id producer-owned. Full replace. Compose by (layer, creation order); prim index = paint order. Refs block-local. Submit packs into one render-owned allocation.
typedef struct RenderUiBlock
{
    uint32_t layer;
    uint32_t surface;  // ANO_UI_SURFACE_* (v0: always OVERLAY)
    float    scroll[2];
    uint32_t primCount;
    uint32_t clipCount;
    uint32_t paintCount;
    uint32_t stopCount;
    uint32_t curveCount; // packed path curve words in curves[]
    uint32_t glyphCount;
    const AnoUiPrim        *prims;
    const AnoUiClip        *clips;
    const AnoUiPaint       *paints;
    const AnoUiStop        *stops;
    const uint32_t         *curves;
    const AnoGlyphInstance *glyphs;
} RenderUiBlock;

// Zero-copy streamed-transform write region. begin reserves GPU ring slice; write ids/xforms; commit publishes RCMD_STREAM_TRANSFORMS. Valid between begin and commit; single-producer.
typedef struct AnoStreamRegion
{
    uint32_t *ids;       // [capacity] streamed render_ids
    mat4     *xforms;    // [capacity] live world transforms (initialTransform space)
    uint32_t  capacity;  // entries this slice holds (STREAM_CAPACITY)
    uint64_t  token;     // opaque slice identity; pass back to commit
} AnoStreamRegion;

// POD, fixed-size, copied by value through the ring. CREATE needs full payload; UPDATE reads flagged fields only.
typedef struct RenderCommand
{
    RenderCommandKind kind;
    uint32_t          render_id;        // logical name; CREATE/UPDATE/DESTROY
    uint32_t          fields;           // RenderFieldBits, for CREATE/UPDATE

    mat4              transform;        // base pose (CREATE, or UPDATE | RFIELD_TRANSFORM)
    AnoMotionDescriptor motion;         // GPU motion (CREATE, or UPDATE | RFIELD_ANIM)
    uint32_t          mesh_index;       // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          material_index;   // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          light_index;      // ANO_RENDER_NO_LIGHT if not a light
    RenderLightParams light;            // CREATE (if light) or UPDATE | RFIELD_LIGHT; also RCMD_LIGHT_ATTACH/UPDATE
    uint32_t          light_id;         // RCMD_LIGHT_* : producer-owned logical light handle
    float             light_offset[3];  // RCMD_LIGHT_ATTACH/UPDATE : offset in parent model space
    uint32_t          light_fields;     // RCMD_LIGHT_UPDATE : ANO_LIGHT_FIELD_* mask (0 == ALL)
    AnoInstanceData   instance_data;    // CREATE, or UPDATE | RFIELD_USERDATA (zero == inert)

    const RenderCreateBatch *batch;     // RCMD_BULK_CREATE only
    const RenderUpdateBatch *update;    // RCMD_BULK_UPDATE only
    const RenderDestroyBatch *destroy;  // RCMD_BULK_DESTROY only
    const RenderTextBlock *text;        // RCMD_TEXT_SET only (render-owned copy; registry adopts)
    uint32_t          text_id;          // RCMD_TEXT_SET/CLEAR : producer-owned block handle
    const RenderUiBlock *ui;            // RCMD_UI_SET only (render-owned copy; registry adopts)
    uint32_t          ui_id;            // RCMD_UI_SET/CLEAR : producer-owned block handle
    bool              bulk_owned;       // render owns pointed payload (helpers). BULK_* freed on apply; TEXT/UI registry adopts
    uint64_t          stream_seq;       // RCMD_STREAM_TRANSFORMS: published ring-slice token
    uint32_t          stream_count;     // RCMD_STREAM_TRANSFORMS: entries in the slice
} RenderCommand;

// Enqueue one command. false = ring full (BACKPRESSURE): retain and retry; never drop (dropped DESTROY strands a slot). Mass events use bulk endpoints so one tick stays O(1) ring messages.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd);

// Bulk endpoints. Copy into render-owned block. false = ring full, retry (copy released). Zero count = no-op true.
bool ano_render_submit_bulk_update(AnoRenderBridge *bridge, const RenderUpdateBatch *batch);
bool ano_render_submit_bulk_destroy(AnoRenderBridge *bridge, const uint32_t *render_ids, uint32_t count);

// Streamed-transform lane. begin false = all slices in flight (drop tick; last published pose held). commit false = ring full (slice unpublished). Single-producer; valid after init.
bool ano_render_stream_begin(AnoStreamRegion *out);
bool ano_render_stream_commit(const AnoStreamRegion *region, uint32_t count);

// Runtime lights on a parent renderable by producer-owned light_id. Offset in parent model space. Same backpressure. Detach implicit on parent DESTROY.
//   attach: light_id unmapped; parent CREATE must precede in ring order.
//   update: full params + offset.
//   detach: idempotent.
bool ano_render_light_attach(AnoRenderBridge *bridge, uint32_t light_id, uint32_t parent_render_id,
                             const RenderLightParams *params, float ox, float oy, float oz);
bool ano_render_light_update(AnoRenderBridge *bridge, uint32_t light_id,
                             const RenderLightParams *params, float ox, float oy, float oz);
// Partial update: only fields named in `fields` written; rest preserved. ano_render_light_update == ALL. Same backpressure.
bool ano_render_light_update_fields(AnoRenderBridge *bridge, uint32_t light_id,
                                    const RenderLightParams *params, float ox, float oy, float oz,
                                    uint32_t fields);
bool ano_render_light_detach(AnoRenderBridge *bridge, uint32_t light_id);

// Screen-text set/clear. set copies and REPLACES; count 0 clears. false = ring full. Dropped SET is stale-only (retry if one-shot matters).
bool ano_render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
                         const AnoGlyphInstance *instances, uint32_t count);
bool ano_render_text_clear(AnoRenderBridge *bridge, uint32_t text_id);

// UI set/clear. set packs and REPLACES. Invalid block (cap/ref/curve overflow) dropped with warning, returns true. false = ring full.
bool ano_render_ui_set(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t layer,
                       const AnoUiBuilder *ui,
                       const AnoGlyphInstance *glyphs, uint32_t glyphCount);
bool ano_render_ui_clear(AnoRenderBridge *bridge, uint32_t ui_id);


/* Back-Channel */

// Render -> logic. Three lanes: events (SPSC RenderEvent; lifetime lossless, INPUT best-effort — flood-drop so INPUT cannot starve lossless headroom), snapshot (latest-wins RenderSnapshot), viewstate (logic owns camera via AnoViewState). Discrete facts on rings; continuous state on published buffers.

// Sentinel render_id for "cursor over no renderable" in REVENT_PICK_RESULT.
#define ANO_RENDER_NO_PICK 0xFFFFFFFFu

// Input event kinds. GLFW codes forwarded as stable integers. New device = new AnoInputKind + union arm.
typedef enum AnoInputKind
{
    ANO_INPUT_KEY,                // physical key transition
    ANO_INPUT_MOUSE_BUTTON,       // mouse button transition
    ANO_INPUT_CURSOR_POS,         // absolute cursor (overlay logical units, origin top-left)
    ANO_INPUT_SCROLL,             // scroll wheel delta
    ANO_INPUT_FOCUS,              // window focus gained/lost
    ANO_INPUT_FRAMEBUFFER_RESIZE, // framebuffer size changed, device px
    ANO_INPUT_CHAR,               // text input codepoint
} AnoInputKind;

// One input sample. Fixed-size POD; rides events ring inside RenderEvent.
typedef struct AnoInputEvent
{
    uint32_t kind; // AnoInputKind
    union {
        struct { int32_t key, scancode, action, mods; } key;     // largest arm (16 B)
        struct { int32_t button, action, mods; }        button;
        struct { float   x, y; }                        cursor;  // overlay logical units
        struct { float   dx, dy; }                      scroll;
        struct { int32_t focused; }                     focus;   // 1 = gained, 0 = lost
        struct { uint32_t width, height; }              resize;
        struct { uint32_t codepoint; }                  ch;
    } u;
} AnoInputEvent;

// Render -> logic events. Render master sole producer (SPSC, totally ordered). Logic sole consumer via ano_render_poll_event.
typedef enum RenderEventKind
{
    REVENT_SLOT_RETIRED,   // render_id GPU slot clear of all in-flight frames; ECS may recycle
    REVENT_CAPACITY,       // unused advisory (payload unused; INPUT flood-drops without emitting)
    REVENT_INPUT,          // one AnoInputEvent from GLFW
    REVENT_PICK_RESULT,    // renderable under cursor (or ANO_RENDER_NO_PICK)
    REVENT_BATCH_CONSUMED, // unused (was borrowed-batch ack; helpers now copy, render frees)
} RenderEventKind;

typedef struct RenderEvent
{
    RenderEventKind kind;
    union {
        uint32_t      render_id;       // REVENT_SLOT_RETIRED
        AnoInputEvent input;           // REVENT_INPUT
        uint32_t      pick_render_id;  // REVENT_PICK_RESULT (ANO_RENDER_NO_PICK == none)
        uint64_t      batch_token;     // REVENT_BATCH_CONSUMED
    } u;
} RenderEvent;

// Latest-wins VIEW-0 camera for logic picking/LOD. Published once per frame; view 0 only.
typedef struct RenderSnapshot
{
    mat4     viewProj;     // proj * view for view 0 (column-major)
    mat4     invViewProj;  // inverse: unproject cursor to world picking ray
    Vector4  frustum[6];   // view-0 frustum planes (cull packing)
    uint32_t vpWidth;      // framebuffer extent matrices built for
    uint32_t vpHeight;
    // Overlay logical units: uiWidth/uiHeight = framebuffer / uiScale.
    float    uiWidth;
    float    uiHeight;
    float    uiScale;
    uint64_t frameId;      // monotonically increasing render frame counter
} RenderSnapshot;

// View-0 camera pose from logic. Renderer owns projection. Latest-wins; until first publish uses built-in camera.
typedef struct AnoViewState
{
    float    eye[3];     // camera world position
    float    center[3];  // look-at target (world)
    float    up[3];      // world up
    float    fovYDeg;    // vertical FOV, degrees
    uint64_t seq;        // producer publish counter (diagnostics)
} AnoViewState;

// Logic master endpoints. Producer side is render-private in src/render_bridge/.

// Dequeue next render->logic event. false if none. Sole consumer; drain every tick.
bool ano_render_poll_event(AnoRenderBridge *bridge, RenderEvent *out);

// Copy latest published snapshot into `out`. false (untouched) if no frame published yet.
bool ano_render_acquire_snapshot(AnoRenderBridge *bridge, RenderSnapshot *out);

// Publish view-0 camera for next recorded frame. Latest-wins; at most once per logic tick. Single-producer.
void ano_render_publish_view(AnoRenderBridge *bridge, const AnoViewState *view);

// Occlusion model from next recorded frame. Render thread only. L key cycles. Out-of-range ignored.
void            ano_render_set_lighting_mode(AnoLightingMode mode);
AnoLightingMode ano_render_get_lighting_mode(void);

// Per-view screen-area cull threshold (projected sphere radius, px). 0 disables; negative clamps to 0. Next frame; render thread.
void  ano_render_set_view_cull_threshold(uint32_t view, float pixels);
float ano_render_get_view_cull_threshold(uint32_t view);

// Per-view LOD threshold (projected sphere radius, px): size at which finest drops one level, then each further halving. 0 = always finest. Next frame; render thread.
void  ano_render_set_view_lod_threshold(uint32_t view, float pixels);
float ano_render_get_view_lod_threshold(uint32_t view);

// Global LOD bias added to auto-selected level (clamped per mesh). +coarser, -finer. LOD-chain meshes only. Next frame; render thread.
void    ano_render_set_lod_bias(int32_t bias);
int32_t ano_render_get_lod_bias(void);

// Shadow caster LOD bias (global; no per-caster screen metric). Clamped [0, max LOD]. LOD-chain meshes only. Next frame; render thread.
void    ano_render_set_shadow_lod_bias(int32_t bias);
int32_t ano_render_get_shadow_lod_bias(void);

// Per-view GPU Hi-Z occlusion cull. Rejects fully hidden entities (~1 frame latency). Conservative; off by default. Next frame; render thread.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable);
bool ano_render_get_view_hiz_enable(uint32_t view);

#endif // ANOPTIC_RENDER_H
