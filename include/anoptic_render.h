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

// render_id 0's original geometry index (stand-in producer helper). Read after init.
uint32_t anoRenderEntity0Mesh(void);

// Copies render_id's seeded base pose into `out` (stand-in stream-producer helper, so a
// streamed transform stays in the same world space as the normal path). Read after init.
// Returns false (out untouched) if render_id is unmapped.
bool anoRenderEntityBaseTransform(uint32_t render_id, mat4 out);

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

// Initial-state batch referenced by RCMD_BULK_CREATE. The producer hands ownership
// of the arrays (arena-allocated, length `count`) to the render master, which
// block-writes a contiguous slot range and releases the batch when consumed.
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

#endif // ANOPTIC_RENDER_H
