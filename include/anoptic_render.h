/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_render.h
 * @brief Public engine<->renderer contract: the renderer lifecycle the engine
 *        entry point drives, plus the logic->render command protocol it produces.
 *
 * This is the ONLY renderer header the engine entry point includes. The render
 * world (all Vulkan + GLFW) runs on the main thread; the logic/ECS master runs
 * on a child thread as the sole command producer and reaches the renderer through
 * the opaque AnoRenderBridge handle below. The transport mechanism (the lock-free
 * SPSC rings, the bridge struct, the render->logic event protocol, the logic-side
 * DisplayState projection) is private to the render_bridge module under src/ and
 * is never exposed here. Design of record: docs/artifacts/VK_BACKEND_INTEROP.md.
 */

/*
This is where the renderer contract is declared:
- Function Signatures
- Constants
- Types used as inputs or outputs by those functions

It is the bridge betwixt engine <===> renderer.
*/

#ifndef ANOPTIC_RENDER_H
#define ANOPTIC_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <anoptic_math.h> // mat4, Vector4
#include <anoptic_text.h> // AnoFontBake, AnoGlyphInstance (logic-side text shaping)

// ---------------------------------------------------------------------------
// Renderer lifecycle (render world; runs on the main thread)
// ---------------------------------------------------------------------------
// GLFW pins window + event handling to the process main thread (mandatory on
// macOS), so initVulkan / drawFrame / unInitVulkan all run on main().

// Bring up the render world (window, device, assets). false on failure.
bool initVulkan(void);

// A celebration. Tears the render world down; destroys the bridge.
void unInitVulkan(void);

// Render one frame; drains pending render commands.
void drawFrame(void);

// true once the window has been asked to close.
bool anoShouldClose(void);

// ---------------------------------------------------------------------------
// Bridge handle
// ---------------------------------------------------------------------------

// Opaque logic<->render channel. Created inside initVulkan(), destroyed in
// unInitVulkan(). The producer holds it only to submit commands; the transport
// is defined privately in src/render_bridge/.
typedef struct AnoRenderBridge AnoRenderBridge;

// Producer endpoint. Valid once initVulkan() has returned.
AnoRenderBridge *anoRenderBridge(void);

// ---------------------------------------------------------------------------
// Loaded-asset query (render world owns assets; logic composes the scene)
// ---------------------------------------------------------------------------
// initVulkan loads the glTF assets + the fallback cube and assigns each their GPU mesh/material
// indices. The logic master composes the scene from these: it queries an asset's primitives,
// assigns render_ids + motion, and emits the creates. Valid after initVulkan(); read-only.

// One renderable primitive ready to spawn: the render-assigned GPU mesh + material, and a world
// transform (the asset's node-local transform composed under the caller's root).
typedef struct AnoRenderableDesc
{
    mat4     transform;
    uint32_t mesh_index;
    uint32_t material_index;
} AnoRenderableDesc;

// Number of asset slots loaded at init (index space for the queries below).
uint32_t anoRenderAssetCount(void);

// Flatten loaded asset `asset_id` at `root` into renderable primitives. Returns the TOTAL count;
// fills out[0..min(count,cap)). Call with cap 0 (or out NULL) to size, then again to fill. An
// out-of-range asset_id returns 0.
uint32_t anoRenderAssetPrimitives(uint32_t asset_id, const mat4 root, AnoRenderableDesc *out, uint32_t cap);

// The fallback cube's geometry-pool mesh index and a default material, for procedural renderables
// (ground slab, debug markers) the logic master builds without an asset.
uint32_t anoRenderFallbackMesh(void);
uint32_t anoRenderDefaultMaterial(void);

// The renderer's baked font (FONT_RENDER.md), for LOGIC-SIDE shaping: game code shapes
// UTF-8 into AnoGlyphInstance arrays with ano_text_shape/_runs against this bake on any
// thread (the bake is immutable plain data, published before the logic thread starts)
// and ships the instances through ano_render_text_set below. NULL when the text stack
// failed init (missing font) — ano_text_shape over NULL yields 0 instances, so callers
// degrade to no text without a special path. Valid after initVulkan(); read-only.
const AnoFontBake *anoRenderTextBake(void);

// Light-palette rows [0, anoRenderStaticLightBase()) are the STATIC region the logic master fills
// with scene light-entities (RCMD_CREATE carrying light params + light_index in this range; casting
// lights get a static shadow frustum). The runtime attach registry (ano_render_light_attach) owns
// the rows above it. A scene light-entity's light_index MUST be < this value.
uint32_t anoRenderStaticLightBase(void);

// ---------------------------------------------------------------------------
// Command protocol: logic -> render
// ---------------------------------------------------------------------------

// Absent-attribute sentinels, shared by logic and render. A renderable with
// ANO_RENDER_NO_MESH carries no geometry (the cull pass skips it — e.g. a pure
// light); ANO_RENDER_NO_LIGHT marks a renderable that drives no light.
#define ANO_RENDER_NO_MESH  0xFFFFFFFFu
#define ANO_RENDER_NO_LIGHT 0xFFFFFFFFu

// Geometry pool index of the fallback cube; assigned first at upload time.
#define FALLBACK_MESH_INDEX 0

// glTF KHR_lights_punctual model. Photometric parameters only; world
// position/direction are derived render-side from the driving renderable's live
// transform, so animated lights need no per-frame light traffic.
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
    float           localDir[3];  // spot/dir aim in the parent's MODEL space; world forward =
                                  // rotate(parent, localDir). (0,0,0) -> parent -Z (the default).
                                  // Lets spots on a shared parent slot fan independently. Point: ignored.
    uint32_t        castsShadow;  // RCMD_LIGHT_ATTACH: 1 = allocate a runtime shadow frustum so this
                                  // light casts (within the runtime budget; silently shadowless if full).
                                  // dir/spot = 1 frustum, point = 6 (a cube). Set at attach; togglable
                                  // afterward via ano_render_light_update_fields + ANO_LIGHT_FIELD_CAST.
} RenderLightParams;

// Field mask for ano_render_light_update_fields: which RenderLightParams fields (+ the model-space
// offset) the update writes. Unnamed fields are PRESERVED from the light's current render-side state,
// so a producer can pulse just intensity/color without re-sending position/type. The transform/parent
// are render-derived and always refreshed. ALL == the full overwrite (what ano_render_light_update does).
enum {
    ANO_LIGHT_FIELD_COLOR     = 1 << 0, // color[3]
    ANO_LIGHT_FIELD_INTENSITY = 1 << 1, // intensity
    ANO_LIGHT_FIELD_RANGE     = 1 << 2, // range
    ANO_LIGHT_FIELD_CONE      = 1 << 3, // innerConeCos + outerConeCos
    ANO_LIGHT_FIELD_TYPE      = 1 << 4, // type
    ANO_LIGHT_FIELD_OFFSET    = 1 << 5, // light_offset[3]
    ANO_LIGHT_FIELD_DIRECTION = 1 << 6, // localDir[3] (spot/dir aim)
    ANO_LIGHT_FIELD_ALL       = (1 << 7) - 1, // the full-overwrite set (bits 0..6); preserves cast state
    ANO_LIGHT_FIELD_CAST      = 1 << 7, // toggle shadow casting via castsShadow. Deliberately OUTSIDE
                                        // ALL: casting allocates/frees a whole shadow-frustum block, so
                                        // it flips only on an explicit request, never as a side effect
                                        // of a full update. On->off frees the block (shadowless within a
                                        // frame); off->on re-allocates if the runtime budget allows.
};

// Occlusion model selector, profiled head-to-head against radiance cascades (see
// docs/artifacts/RADIANCE_CASCADES.md). Drives, per light type, whether a light's direct
// occlusion is sampled from its conventional shadow map this frame or carried by the radiance
// cascade field. The renderer also gates the shadow depth render itself on this, so a type that
// is RC-occluded pays no shadow-map render cost (the memory/bandwidth win under measurement).
// The default preserves the existing all-shadow-mapped behavior. Toggle live with the L key.
typedef enum AnoLightingMode
{
    ANO_LIGHTING_SHADOWMAP  = 0, // all sources shadow-mapped (default; current renderer)
    ANO_LIGHTING_HYBRID     = 1, // radiance cascades for point lights, shadow maps for directional + spot
    ANO_LIGHTING_RC         = 2, // all sources via radiance cascades (no shadow maps rendered)
    ANO_LIGHTING_MODE_COUNT = 3,
} AnoLightingMode;

// Continuous, GPU-derived motion. The producer establishes a motion ONCE (via
// RFIELD_ANIM) and the GPU update pass derives the live transform from global time
// every frame, so a steady trajectory costs zero per-tick bridge traffic; only a
// discrete change of trajectory re-sends. Arbitrary CPU/physics/pathfinding-driven
// motion has no closed form and does NOT belong here — it takes the streamed path.
typedef enum AnoMotionType
{
    ANO_MOTION_STATIC = 0, // no motion; live transform == base pose
    ANO_MOTION_SPIN,       // constant-rate rotation in the body's local frame (base * R)
    ANO_MOTION_ORBIT,      // constant-rate revolution about a world axis    (R * base)
    ANO_MOTION_LINEAR,     // constant-velocity translation from the base pose
    ANO_MOTION_KEPLER,     // closed-form elliptical orbit; base-pose origin is the focus
    ANO_MOTION_STREAMED,   // CPU-driven; transform arrives per-tick via RCMD_STREAM_TRANSFORMS
} AnoMotionType;

// Per-renderable motion parameters. 48 bytes; matches std430
// { uint type; uint flags; float epoch; float pad; vec4 p0; vec4 p1; }. `epoch` is
// the global-time stamp the motion was established (t0); the GPU evaluates at
// (time - epoch), so re-basing a trajectory is one RFIELD_ANIM command. The eight
// p0/p1 floats hold every type's params inline — Kepler needs no side pool:
//   SPIN / ORBIT : p0.xyz = axis * angular_speed (rad/s)
//   LINEAR       : p0.xyz = velocity (units/s)
//   KEPLER       : p0 = (semiMajorAxis, eccentricity, inclination, longAscendingNode)
//                  p1 = (argPeriapsis, meanAnomalyAtEpoch, meanMotion, _) [rad]
typedef struct AnoMotionDescriptor
{
    uint32_t type;   // AnoMotionType
    uint32_t flags;  // reserved
    float    epoch;  // t0: global-time stamp the motion was established
    float    _pad;   // aligns p0 to a 16-byte boundary
    Vector4  p0;
    Vector4  p1;
} AnoMotionDescriptor; // 48 bytes

typedef enum RenderCommandKind
{
    RCMD_CREATE,       // new renderable; carries full initial state
    RCMD_UPDATE,       // discrete change(s) to an existing renderable (see `fields`)
    RCMD_DESTROY,      // remove a renderable (render_id only)
    RCMD_BULK_CREATE,  // contiguous batch of new renderables (mass spawn); see `batch`
    RCMD_BULK_UPDATE,  // one shared field mask applied across a render_id array; see `update`
    RCMD_BULK_DESTROY, // mass despawn of a render_id array; see `destroy`
    RCMD_STREAM_TRANSFORMS, // publishes one streamed-transform ring slice; carries {stream_seq, stream_count}, see ano_render_stream_begin
    RCMD_LIGHT_ATTACH,      // attach a runtime light to a renderable (render_id = parent); see ano_render_light_attach
    RCMD_LIGHT_UPDATE,      // change an attached light's params/offset (addressed by light_id)
    RCMD_LIGHT_DETACH,      // remove an attached light (addressed by light_id)
    RCMD_TEXT_SET,          // replace a screen-text block's shaped instances (addressed by text_id); see ano_render_text_set
    RCMD_TEXT_CLEAR,        // remove a screen-text block (addressed by text_id)
} RenderCommandKind;

// Which payload fields a CREATE/UPDATE carries. A single UPDATE may set several
// bits: that is the "<=1 message per entity per tick" invariant made literal.
typedef enum RenderFieldBits
{
    RFIELD_TRANSFORM = 1 << 0, // teleport: rewrite the BASE pose (initialTransform), never the GPU-output transform
    RFIELD_MESH_MAT  = 1 << 1, // mesh and/or material index
    RFIELD_ANIM      = 1 << 2, // GPU animation parameters (establishes/changes continuous motion)
    RFIELD_LIGHT     = 1 << 3, // light photometric parameters
    RFIELD_USERDATA  = 1 << 4, // packed per-entity instance channel (tint/flags/scalars)
} RenderFieldBits;

// Open-ended per-renderable instance channel: one packed integer lane plus one
// full-precision lane, interpreted by the fragment shader as the game sees fit.
// This is the single general per-entity attribute slot — tint, flags, scalars,
// small indices — that exists so adding the next per-instance attribute never
// re-widens the bridge contract at six lockstep sites again.
//
// Convention (v1; the game owns the CPU pack <-> shader unpack, both in its layer):
//   packed[0] : RGBA8 tint               (GLSL unpackUnorm4x8)
//   packed[1] : flag bits                 (bit 0 = tint enabled; the rest reserved)
//   packed[2] : reserved (e.g. two fp16 scalars / two u16 texture-layer indices)
//   packed[3] : reserved
//   params    : reserved full-precision scalars (e.g. anim phase, build progress)
// All-zero is the inert default: flags clear -> the renderer ignores the slot, so
// fresh/recycled slots render exactly as before until a game explicitly opts in.
typedef struct AnoInstanceData
{
    uint32_t packed[4];
    Vector4  params;
} AnoInstanceData; // 32 bytes; matches std430 { uvec4 packed; vec4 params; }

// Initial-state batch referenced by RCMD_BULK_CREATE. init owns-and-frees its stack batch
// UPDATE/DESTROY helpers copy-at-submit and render frees
typedef struct RenderCreateBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names
    const mat4     *transforms;  // [count] base poses
    const AnoMotionDescriptor *motion; // [count] GPU motion descriptors (ANO_MOTION_STATIC for none)
    const uint32_t *mesh;        // [count] geometry pool indices (ANO_RENDER_NO_MESH allowed)
    const uint32_t *material;    // [count] material palette indices
} RenderCreateBatch;

// Mass field change (RCMD_BULK_UPDATE): ONE shared `fields` mask applied across a
// render_id array, with parallel value arrays — only the flagged fields' arrays are read
// (the rest may be NULL). This is the mass-event analogue of a single RFIELD_* UPDATE:
// a battle re-skinning thousands of ships (RFIELD_MESH_MAT) or a solar flare flipping
// 100k colonists' state (RFIELD_USERDATA) becomes O(1) ring messages. RFIELD_LIGHT is not
// bulk (lights are few). Submit via ano_render_submit_bulk_update, which copies the batch
// — the caller's arrays need only live until that call returns.
typedef struct RenderUpdateBatch
{
    uint32_t        count;
    uint32_t        fields;       // RenderFieldBits shared by every entry; only these arrays are consumed
    const uint32_t *render_ids;   // [count] targets (unresolved ids are skipped)
    const mat4     *transforms;   // [count] if fields & RFIELD_TRANSFORM (teleport: rewrites base pose)
    const AnoMotionDescriptor *motion;        // [count] if fields & RFIELD_ANIM
    const uint32_t *mesh;         // [count] if fields & RFIELD_MESH_MAT
    const uint32_t *material;     // [count] if fields & RFIELD_MESH_MAT
    const AnoInstanceData *instance_data;     // [count] if fields & RFIELD_USERDATA
} RenderUpdateBatch;

// Mass despawn (RCMD_BULK_DESTROY): a render_id array retired in one ring message.
// Symmetric with RCMD_BULK_CREATE; submit via ano_render_submit_bulk_destroy, which
// copies the array — the caller's array need only live until that call returns.
typedef struct RenderDestroyBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names to retire (unresolved ids are skipped)
} RenderDestroyBatch;

// The screen-text region's total instance capacity. One block never exceeds it (a larger
// submit truncates); the render side composes all live blocks after its own internal text
// (profiling OSD) into the same region, truncating in block order if the union overflows.
#define ANO_RENDER_TEXT_MAX 8192u

// One screen-text block (RCMD_TEXT_SET): shaped glyph instances, addressed by a
// producer-owned text_id (its namespace to assign and recycle, like light_id). SET
// REPLACES the block's previous contents (creating it if new); CLEAR removes it. The
// producer shapes with ano_text_shape/_runs against anoRenderTextBake() — instances are
// screen-space pixels, composited over the frame after the render-internal overlay text.
// Submit via ano_render_text_set, which copies the array into one render-owned block —
// the caller's array need only live until that call returns.
typedef struct RenderTextBlock
{
    uint32_t                count;
    const AnoGlyphInstance *instances;  // [count] shaped glyphs (48-byte GPU ABI)
} RenderTextBlock;

// Zero-copy producer write-region for the streamed-transform lane (Path B v2). Rather
// than copy a per-tick batch through the command ring, the producer reserves the next
// free GPU ring slice (ano_render_stream_begin), writes its render_ids + live world
// transforms straight into the mapped arrays, and publishes one tiny control command
// (ano_render_stream_commit -> RCMD_STREAM_TRANSFORMS). The scatter pass reads the slice
// in place via a dynamic descriptor offset — the matrices are never copied render-side,
// and the render master holds the last published slice so a tick with no new batch keeps
// its pose. `ids` and `xforms` are parallel, length up to `capacity`; `token` is opaque
// (carries the slice identity back to commit). Valid only between a successful begin and
// its commit, single-producer (the logic/ECS thread that owns the bridge).
typedef struct AnoStreamRegion
{
    uint32_t *ids;       // [capacity] destination for streamed render_ids
    mat4     *xforms;    // [capacity] destination for live world transforms (initialTransform space)
    uint32_t  capacity;  // entries this slice holds (STREAM_CAPACITY)
    uint64_t  token;     // opaque slice identity; pass back to ano_render_stream_commit
} AnoStreamRegion;

// POD, fixed-size, copied by value through the ring. ~fat (holds a mat4) but
// CREATE needs it; UPDATE only reads the fields flagged in `fields`.
typedef struct RenderCommand
{
    RenderCommandKind kind;
    uint32_t          render_id;        // logical name; valid for CREATE/UPDATE/DESTROY
    uint32_t          fields;           // RenderFieldBits, for CREATE/UPDATE

    mat4              transform;        // base pose (CREATE, or UPDATE | RFIELD_TRANSFORM)
    AnoMotionDescriptor motion;         // GPU motion params (CREATE, or UPDATE | RFIELD_ANIM)
    uint32_t          mesh_index;       // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          material_index;   // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          light_index;      // ANO_RENDER_NO_LIGHT if not a light
    RenderLightParams light;            // CREATE (if light) or UPDATE | RFIELD_LIGHT; also RCMD_LIGHT_ATTACH/UPDATE
    uint32_t          light_id;         // RCMD_LIGHT_* : producer-owned logical light handle
    float             light_offset[3];  // RCMD_LIGHT_ATTACH/UPDATE : offset in the parent's model space
    uint32_t          light_fields;     // RCMD_LIGHT_UPDATE : ANO_LIGHT_FIELD_* mask (0 == ALL, full overwrite)
    AnoInstanceData   instance_data;    // CREATE, or UPDATE | RFIELD_USERDATA (zero == inert)

    const RenderCreateBatch *batch;     // RCMD_BULK_CREATE only
    const RenderUpdateBatch *update;    // RCMD_BULK_UPDATE only
    const RenderDestroyBatch *destroy;  // RCMD_BULK_DESTROY only
    const RenderTextBlock *text;        // RCMD_TEXT_SET only (render-owned copy; the registry adopts it)
    uint32_t          text_id;          // RCMD_TEXT_SET/CLEAR : producer-owned logical block handle
    bool              bulk_owned;       // render side frees the batch block after consumption (set by the bulk submit helpers)
    uint64_t          stream_seq;       // RCMD_STREAM_TRANSFORMS: published ring-slice token
    uint32_t          stream_count;     // RCMD_STREAM_TRANSFORMS: entries in the slice
} RenderCommand;

// Enqueue one command. Returns false ONLY when the command ring is full.
//
// Overflow policy (the contract, not "caller decides"): false means BACKPRESSURE, not
// loss — the producer must retain the command and retry on a later tick; it must NOT
// drop it (a dropped DESTROY strands a slot; a dropped CREATE is an invisible entity).
// The lock-free SPSC ring cannot be grown live, and spinning here would couple logic to
// render, so retry is the policy. Mass events MUST use the bulk commands below so a
// single tick is O(1) ring messages and never approaches the ceiling in the first place.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd);

// Bulk producer endpoints. Each copies the batch into one render-owned block (released
// render-side after the change has reached every frame in flight), so the caller's arrays
// need only live until the call returns. Same backpressure contract as ano_render_submit:
// false == ring full, retry (the copy is released and nothing is enqueued); never drops.
// A zero count is a no-op (returns true).
bool ano_render_submit_bulk_update(AnoRenderBridge *bridge, const RenderUpdateBatch *batch);
bool ano_render_submit_bulk_destroy(AnoRenderBridge *bridge, const uint32_t *render_ids, uint32_t count);

// Streamed-transform lane (ANO_MOTION_STREAMED), zero-copy producer endpoint. `begin`
// reserves the next free ring slice and points `out` at its mapped id/transform arrays,
// returning false if every slice is still in flight on the GPU — the caller drops the
// tick, and since the render side holds the last published slice, a dropped tick simply
// repeats it. The producer fills out->ids[0..count) and out->xforms[0..count) (count <=
// out->capacity), then `commit` publishes via one bridge command. `commit` returns false
// if the command ring is full (the slice is left unpublished and reclaimed normally).
// Single-producer only (the thread that owns the bridge); valid after init.
bool ano_render_stream_begin(AnoStreamRegion *out);
bool ano_render_stream_commit(const AnoStreamRegion *region, uint32_t count);

// Runtime per-renderable lights (audit 4.7). A light is attached to a parent renderable by a
// producer-owned light_id and rides the parent's live transform at a model-space offset; many lights
// may share one parent, none cost an entity slot. Same backpressure contract as ano_render_submit
// (false == ring full: retry, never drop). The light_id namespace is the producer's to assign and
// recycle, independent of render_id. Detach is implicit on the parent's DESTROY (the render side
// disables and reclaims the parent's lights as part of the slot cascade), so an explicit detach is
// only needed to remove a light while its parent lives.
//   attach: light_id must be currently unmapped; the parent CREATE must precede it in ring order.
//   update: carries the full params + offset (the light payload is tiny; there is no partial mask).
//   detach: idempotent (an unknown/already-detached light_id is a no-op).
bool ano_render_light_attach(AnoRenderBridge *bridge, uint32_t light_id, uint32_t parent_render_id,
                             const RenderLightParams *params, float ox, float oy, float oz);
bool ano_render_light_update(AnoRenderBridge *bridge, uint32_t light_id,
                             const RenderLightParams *params, float ox, float oy, float oz);
// Partial update: only the RenderLightParams fields (+ offset) named in `fields` (ANO_LIGHT_FIELD_*)
// are written; the rest of the light's state is preserved render-side. ano_render_light_update is
// this with ANO_LIGHT_FIELD_ALL. Same backpressure contract (false == ring full: retry).
bool ano_render_light_update_fields(AnoRenderBridge *bridge, uint32_t light_id,
                                    const RenderLightParams *params, float ox, float oy, float oz,
                                    uint32_t fields);
bool ano_render_light_detach(AnoRenderBridge *bridge, uint32_t light_id);

// Screen-text blocks (FONT_RENDER.md, the v0 logic->render text path). `set` copies the
// shaped instances into one render-owned block (count truncated to ANO_RENDER_TEXT_MAX)
// and REPLACES block text_id's contents — the caller's array need only live until the
// call returns. `clear` removes the block (idempotent; unknown text_id is a no-op).
// Backpressure: false == ring full. Unlike CREATE/DESTROY, a dropped SET is harmless to
// skip — it is a full replace, so the block is merely stale until the producer's next
// set — but a producer that must not miss a one-shot set (or a clear) should retry.
// A set with count 0 clears the block. All blocks die with the renderer at shutdown.
bool ano_render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
                         const AnoGlyphInstance *instances, uint32_t count);
bool ano_render_text_clear(AnoRenderBridge *bridge, uint32_t text_id);

// ---------------------------------------------------------------------------
// Back-channel: render -> logic
// ---------------------------------------------------------------------------
// The render world owns the window, GLFW, and the resolved camera; the logic world owns gameplay.
// Three render->logic lanes carry everything that must flow home, designed once so a new fact
// never means a new ring or a wider enum lockstep again (audit 4.11 / 7.1):
//   - events    : the SPSC events ring, one typed RenderEvent per fact. Lifetime facts (slot
//                 retirement, batch acks) are lossless; forwarded INPUT is best-effort and is
//                 dropped under flood so it can never starve the reserved lossless headroom.
//   - snapshot  : a published latest-wins RenderSnapshot (the live view-0 camera/viewport), for
//                 picking rays and attention-driven simulation LOD.
//   - viewstate : the symmetric logic->render lane (AnoViewState), so logic owns the camera.
// Standing rule this establishes: discrete lossless facts ride a command/event ring; continuous
// latest-wins state rides a published double buffer.

// Sentinel render_id for "the cursor is over no renderable" in a REVENT_PICK_RESULT.
#define ANO_RENDER_NO_PICK 0xFFFFFFFFu

// Input event kinds. GLFW codes (GLFW_KEY_*, GLFW_PRESS/RELEASE/REPEAT, GLFW_MOUSE_BUTTON_*) are
// forwarded verbatim as stable integers; an engine keymap / action-binding layer is the game's to
// add atop this raw stream. A new device (joystick, IME) adds an AnoInputKind + a union arm, never
// a new event kind or ring.
typedef enum AnoInputKind
{
    ANO_INPUT_KEY,                // physical key transition
    ANO_INPUT_MOUSE_BUTTON,       // mouse button transition
    ANO_INPUT_CURSOR_POS,         // absolute cursor position (framebuffer pixels, origin top-left)
    ANO_INPUT_SCROLL,             // scroll wheel delta
    ANO_INPUT_FOCUS,              // window focus gained/lost
    ANO_INPUT_FRAMEBUFFER_RESIZE, // framebuffer size changed (swapchain recreate stays render-side)
    ANO_INPUT_CHAR,               // text input codepoint (for typed UI)
} AnoInputKind;

// One input sample. Fixed-size POD, sub-tagged on `kind`; rides the events ring inside a RenderEvent.
typedef struct AnoInputEvent
{
    uint32_t kind; // AnoInputKind
    union {
        struct { int32_t key, scancode, action, mods; } key;     // largest arm (16 B)
        struct { int32_t button, action, mods; }        button;
        struct { float   x, y; }                        cursor;  // framebuffer pixels
        struct { float   dx, dy; }                      scroll;
        struct { int32_t focused; }                     focus;   // 1 = gained, 0 = lost
        struct { uint32_t width, height; }              resize;
        struct { uint32_t codepoint; }                  ch;
    } u;
} AnoInputEvent;

// Render -> logic event protocol. The render master is the SOLE producer (GLFW callbacks and slot
// retirement both run on the render thread), so the events ring stays SPSC and totally ordered.
// Every render->logic fact is one kind here; the payload is the matching union arm. The logic
// master is the sole consumer (ano_render_poll_event) and dispatches on `kind`.
typedef enum RenderEventKind
{
    REVENT_SLOT_RETIRED,   // a render_id's GPU slot cleared every frame in flight; the ECS may recycle it
    REVENT_CAPACITY,       // render-side capacity / dropped-sample advisory (payload unused)
    REVENT_INPUT,          // one AnoInputEvent forwarded from GLFW
    REVENT_PICK_RESULT,    // the renderable under the cursor (pick_render_id, or ANO_RENDER_NO_PICK)
    REVENT_BATCH_CONSUMED, // a borrowed bulk batch has reached every frame in flight; producer may free it
} RenderEventKind;

typedef struct RenderEvent
{
    RenderEventKind kind;
    union {
        uint32_t      render_id;       // REVENT_SLOT_RETIRED
        AnoInputEvent input;           // REVENT_INPUT
        uint32_t      pick_render_id;  // REVENT_PICK_RESULT (ANO_RENDER_NO_PICK == nothing under cursor)
        uint64_t      batch_token;     // REVENT_BATCH_CONSUMED
    } u;
} RenderEvent;

// Latest-wins publication of the live VIEW-0 (gameplay) camera, for logic-side picking rays and
// attention-driven LOD. Published once per recorded frame; logic ticks faster than the frame rate
// so it never laps the producer and reads tear-free without a lock. View 0 only: the render-private
// view count must not leak through this public header; multi-view widens this later.
typedef struct RenderSnapshot
{
    mat4     viewProj;     // proj * view for view 0 this frame (column-major)
    mat4     invViewProj;  // its inverse: unproject a cursor texel to a world-space picking ray
    Vector4  frustum[6];   // view-0 frustum planes (same packing as the cull pass)
    uint32_t vpWidth;      // framebuffer extent the matrices were built for
    uint32_t vpHeight;
    uint64_t frameId;      // monotonically increasing render frame counter
} RenderSnapshot;

// The view-0 camera pose logic wants the renderer to use. Pose only: the renderer still owns the
// projection (it resolves perspective from the live aspect/near/far). Published latest-wins by the
// logic master; until its first publish the renderer falls back to its built-in camera, so there is
// no init handshake and no first-frame regression. eye/center/up matches the renderer's lookAt.
typedef struct AnoViewState
{
    float    eye[3];     // camera world position
    float    center[3];  // look-at target (world)
    float    up[3];      // world up
    float    fovYDeg;    // vertical field of view, degrees
    uint64_t seq;        // producer's monotonic publish counter (diagnostics)
} AnoViewState;

// Logic master endpoints (consume events, read the snapshot, drive the camera). The producer
// endpoints (events/snapshot publish, viewstate consume) are render-private in src/render_bridge/.

// Dequeue the next render->logic event. false if none pending. The logic master is the sole
// consumer and must drain + dispatch on kind every tick (so the ring never backs up).
bool ano_render_poll_event(AnoRenderBridge *bridge, RenderEvent *out);

// Copy the latest published render snapshot into `out`. false (out untouched) if the renderer has
// not published a frame yet.
bool ano_render_acquire_snapshot(AnoRenderBridge *bridge, RenderSnapshot *out);

// Publish the view-0 camera pose for the renderer to use from its next recorded frame. Latest-wins;
// call at most once per logic tick. Single-producer (the logic master that owns the bridge).
void ano_render_publish_view(AnoRenderBridge *bridge, const AnoViewState *view);

// Lighting-mode control (see AnoLightingMode). Selects the occlusion model used from the next
// recorded frame onward. Set/read from the render thread (the frame record path is single
// threaded); the L key cycles modes through the same setter. Out-of-range values are ignored.
void            ano_render_set_lighting_mode(AnoLightingMode mode);
AnoLightingMode ano_render_get_lighting_mode(void);

// Per-view screen-area cull threshold, in pixels of projected bounding-sphere radius. An in-frustum
// entity smaller than this on the given view emits no draw, so a peripheral/main view can cull
// harder than a zoomed inset (e.g. a rifle-scope PiP). Independent per view; 0 disables the test for
// that view, negative is clamped to 0; an out-of-range view index is ignored. Applies from the next
// recorded frame. Set/read from the render thread (the frame record path is single threaded).
void  ano_render_set_view_cull_threshold(uint32_t view, float pixels);
float ano_render_get_view_cull_threshold(uint32_t view);

// Per-view LOD threshold, in pixels of projected bounding-sphere radius: the projected size at which
// an entity drops from its finest level to the next, each further halving of on-screen size dropping
// one more level. Independent per view (a peripheral view can bias to coarser meshes than a zoomed
// inset); 0 disables LOD selection for that view (always the finest level), negative clamps to 0.
// Inert until meshes are uploaded with LOD chains. Applies from the next recorded frame; render
// thread only. An out-of-range view index is ignored.
void  ano_render_set_view_lod_threshold(uint32_t view, float pixels);
float ano_render_get_view_lod_threshold(uint32_t view);

// Global LOD-level bias for debug inspection and tuning, added to every entity's automatically
// selected level (then clamped per-entity to that mesh's available levels). Positive biases toward
// coarser meshes, negative toward finer; a large positive value pins the whole scene to its coarsest
// level for close-up inspection, a large negative value forces the finest everywhere. Affects only
// meshes uploaded with LOD chains. Applies from the next recorded frame; render thread only.
void    ano_render_set_lod_bias(int32_t bias);
int32_t ano_render_get_lod_bias(void);

// Coarse shadow LOD bias: the LOD level shadow casters are decimated to. Shadow frustums have no
// per-caster screen-area metric, so this is one global level bias applied to every caster — shadows
// are low-frequency, so a coarser mesh in the depth atlas is cheap and usually imperceptible. Clamped
// per-entity to that mesh's available levels (non-LOD meshes always cast their base level). Clamped to
// [0, max LOD]; negative clamps to 0. Affects only meshes uploaded with LOD chains. Applies from the
// next recorded frame; render thread only.
void    ano_render_set_shadow_lod_bias(int32_t bias);
int32_t ano_render_get_shadow_lod_bias(void);

// Per-view GPU Hi-Z occlusion culling toggle. When enabled, the cull rejects entities fully hidden
// behind the previous frame's depth for that view (single-phase hierarchical-Z; reprojected, so it
// costs ~1 frame of latency — fast camera motion can briefly cull then reveal newly-disoccluded
// geometry). Conservative: it never culls visible geometry, only occluded. Independent per view; off
// by default. Applies from the next recorded frame; render thread only. Out-of-range view is ignored.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable);
bool ano_render_get_view_hiz_enable(uint32_t view);

#endif // ANOPTIC_RENDER_H
