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
} RenderLightParams;

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
    RCMD_STREAM_TRANSFORMS, // per-tick CPU transforms for ANO_MOTION_STREAMED slots; see `stream`
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

// Per-tick transform snapshot for CPU-driven (ANO_MOTION_STREAMED) renderables,
// referenced by RCMD_STREAM_TRANSFORMS. Parallel arrays, length `count`; the render
// master resolves each render_id to its slot and scatters the matrix into the live
// transform buffer for the current frame only (ephemeral — re-sent every tick).
typedef struct RenderStreamBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names of streamed renderables
    const mat4     *transforms;  // [count] live world transforms for this tick
} RenderStreamBatch;

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
    RenderLightParams light;            // CREATE (if light) or UPDATE | RFIELD_LIGHT
    AnoInstanceData   instance_data;    // CREATE, or UPDATE | RFIELD_USERDATA (zero == inert)

    const RenderCreateBatch *batch;     // RCMD_BULK_CREATE only
    const RenderStreamBatch *stream;    // RCMD_STREAM_TRANSFORMS only
} RenderCommand;

// Enqueue one command. false if the command ring is full (caller decides: drop,
// spin, or grow upstream). Producer-side endpoint; the consuming/event endpoints
// are internal to src/render_bridge/.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd);

#endif // ANOPTIC_RENDER_H
