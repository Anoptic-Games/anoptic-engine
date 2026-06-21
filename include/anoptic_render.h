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

typedef enum RenderCommandKind
{
    RCMD_CREATE,       // new renderable; carries full initial state
    RCMD_UPDATE,       // discrete change(s) to an existing renderable (see `fields`)
    RCMD_DESTROY,      // remove a renderable (render_id only)
    RCMD_BULK_CREATE,  // contiguous batch of new renderables (mass spawn); see `batch`
} RenderCommandKind;

// Which payload fields a CREATE/UPDATE carries. A single UPDATE may set several
// bits: that is the "<=1 message per entity per tick" invariant made literal.
typedef enum RenderFieldBits
{
    RFIELD_TRANSFORM = 1 << 0, // teleport: rewrite the BASE pose (initialTransform), never the GPU-output transform
    RFIELD_MESH_MAT  = 1 << 1, // mesh and/or material index
    RFIELD_ANIM      = 1 << 2, // GPU animation parameters (establishes/changes continuous motion)
    RFIELD_LIGHT     = 1 << 3, // light photometric parameters
} RenderFieldBits;

// Initial-state batch referenced by RCMD_BULK_CREATE. The producer hands ownership
// of the arrays (arena-allocated, length `count`) to the render master, which
// block-writes a contiguous slot range and releases the batch when consumed.
typedef struct RenderCreateBatch
{
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names
    const mat4     *transforms;  // [count] base poses
    const Vector4  *anim;        // [count] animation params (xyz = axis*speed, w = orbit flag)
    const uint32_t *mesh;        // [count] geometry pool indices (ANO_RENDER_NO_MESH allowed)
    const uint32_t *material;    // [count] material palette indices
} RenderCreateBatch;

// POD, fixed-size, copied by value through the ring. ~fat (holds a mat4) but
// CREATE needs it; UPDATE only reads the fields flagged in `fields`.
typedef struct RenderCommand
{
    RenderCommandKind kind;
    uint32_t          render_id;        // logical name; valid for CREATE/UPDATE/DESTROY
    uint32_t          fields;           // RenderFieldBits, for CREATE/UPDATE

    mat4              transform;        // base pose (CREATE, or UPDATE | RFIELD_TRANSFORM)
    Vector4           angular_velocity; // anim params (CREATE, or UPDATE | RFIELD_ANIM)
    uint32_t          mesh_index;       // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          material_index;   // CREATE, or UPDATE | RFIELD_MESH_MAT
    uint32_t          light_index;      // ANO_RENDER_NO_LIGHT if not a light
    RenderLightParams light;            // CREATE (if light) or UPDATE | RFIELD_LIGHT

    const RenderCreateBatch *batch;     // RCMD_BULK_CREATE only
} RenderCommand;

// Enqueue one command. false if the command ring is full (caller decides: drop,
// spin, or grow upstream). Producer-side endpoint; the consuming/event endpoints
// are internal to src/render_bridge/.
bool ano_render_submit(AnoRenderBridge *bridge, const RenderCommand *cmd);

#endif // ANOPTIC_RENDER_H
