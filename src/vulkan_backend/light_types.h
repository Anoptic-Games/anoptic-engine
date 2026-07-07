/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Light-palette + runtime-registry types. A domain fragment of structs.h (included after the base
// includes + MAX_FRAMES_IN_FLIGHT define); relies on GpuAllocation. Not a standalone header.

#ifndef ANO_LIGHT_TYPES_H
#define ANO_LIGHT_TYPES_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/gpu_alloc.h"


// ---------------------------------------------------------------------------
// Lighting
//
// Punctual light sources following the glTF KHR_lights_punctual model, mirroring
// the engine's glTF-based MaterialData convention. A light is an entity
// component: its world-space position and direction are NOT stored here, they
// are derived in the fragment shader from the driving entity's live transform
// (transforms[transformIndex]) so GPU animation (orbit/spin) applies for free.
// Only photometric parameters and the transform link live in this struct.
//
// LightData is laid out as 5 x vec4 (80 bytes) for std430. Each leading vec3 +
// scalar packs into one 16-byte row (standard std430 vec3+scalar packing); the C
// layout below matches byte-for-byte.
// ---------------------------------------------------------------------------
typedef enum LightType
{
    LIGHT_TYPE_DIRECTIONAL = 0, // infinitely distant, uses direction only
    LIGHT_TYPE_POINT       = 1, // omnidirectional, uses position + range
    LIGHT_TYPE_SPOT        = 2, // cone, uses position + direction + range + cones
} LightType;

typedef struct LightData
{
    // row 0
    float       color[3];       // linear RGB, normalized (intensity carries magnitude)
    float       intensity;      // brightness multiplier (candela-like for point/spot, lux-like for directional)
    // row 1
    float       range;          // attenuation cutoff distance; <= 0 means unbounded (ignored for directional)
    float       innerConeCos;   // cosine of spot inner cone half-angle (full intensity within)
    float       outerConeCos;   // cosine of spot outer cone half-angle (zero intensity beyond)
    uint32_t    type;           // LightType
    // row 2
    uint32_t    transformIndex; // entity/transform index that drives world position + direction
    uint32_t    enabled;        // 0 = ignored, 1 = active
    uint32_t    pad0;
    uint32_t    pad1;
    // row 3 (audit 4.7 multi-light). Local offset in the driving entity's MODEL space: world pos =
    // transforms[transformIndex] * vec4(localOffset, 1). Lets many lights share ONE parent slot at
    // distinct positions (running lights / engine / cockpit) with no entity slot each; zero ==
    // driver origin (reproduces the pre-offset behaviour). Applied at ALL FOUR world-pos sites in
    // lockstep — flat.frag, transmission.frag, lightcull.comp (froxel binning), shadowsetup.comp
    // (shadow eye, so offset lights also cast correctly). Edit those five mirrors together.
    float       localOffset[3];
    uint32_t    pad2;
    // row 4 (audit 4.7 fanned spots). Light direction in the driving entity's MODEL space: world
    // forward = normalize(mat3(transforms[transformIndex]) * localDir). Lets spots/directionals on a
    // shared parent slot aim independently instead of all following the parent's -Z. (0,0,-1)
    // reproduces the prior -lx[2] behaviour (the C layer defaults a zero vector to it). Decoded at
    // the three direction sites in lockstep: flat.frag, transmission.frag, shadowsetup.comp (spot
    // shadow eye). lightcull.comp bins by position+range only, so it ignores this field (but its
    // struct mirror MUST still carry the row for a matching stride).
    float       localDir[3];
    uint32_t    pad3;
} LightData; // 80 bytes
_Static_assert(sizeof(LightData) == 80, "LightData must be 80B (5x vec4) to match the GLSL std430 mirrors");

// Runtime light lifecycle (audit 4.7 Phase 3). Render-side authority over the DYNAMIC region of the
// light palette: rows [base, base+capacity); rows [0, base) are the permanent init-rig lights. Maps a
// producer-owned light_id -> palette row, records each row's parent render_id for the parent-DESTROY
// cascade, and frame-quarantines a detached row before reuse (mirrors RenderSlotTable's quarantine).
// highWater is the dynamic peak; the cull light count is base + highWater. light_registry_compact
// peels the trailing free run each drain to lower highWater after a permanent drop, so the cull bound
// reclaims past the concurrent peak. Render-thread only; no synchronization. ANO_RENDER_SLOT_UNMAPPED == unmapped.
enum { LIGHT_ROW_FREE = 0u, LIGHT_ROW_LIVE = 1u, LIGHT_ROW_QUARANTINED = 2u };

typedef struct LightRowQuarantine { uint32_t row; uint64_t safeFrame; } LightRowQuarantine;

typedef struct LightRegistry
{
    uint32_t   base;            // dynamic rows start here in the palette (== init light count)
    uint32_t   capacity;        // dynamic row ceiling (palette capacity - base)
    uint32_t   framesInFlight;  // == MAX_FRAMES_IN_FLIGHT

    uint32_t  *idToRow;         // light_id -> relative row [0,highWater), or UNMAPPED. Grown on demand.
    uint32_t   idCapacity;

    uint8_t   *rowState;        // [rowsCapacity] LIGHT_ROW_*
    uint32_t  *rowParent;       // [rowsCapacity] parent render_id  (LIVE rows; drives the cascade scan)
    uint32_t  *rowLightId;      // [rowsCapacity] light_id in the row (LIVE rows; to unmap on cascade)
    LightData *rowMirror;       // [rowsCapacity] CPU mirror of each row's staged LightData; the
                                // read-modify-write base for partial RCMD_LIGHT_UPDATE (device copy
                                // is not host-readable). Written fully on attach/full-update.
    uint32_t  *rowShadowBase;   // [rowsCapacity] the runtime shadow frustum block base this row's light
                                // owns (ANO_SHADOW_NONE = non-casting); freed back to the pool on detach.
    uint32_t   rowsCapacity;

    uint32_t  *freeRows;        // stack of FREE relative rows (holes below highWater)
    uint32_t   freeCount, freeCapacity;

    uint32_t   highWater;       // peak relative rows used; cull light count = base + highWater

    LightRowQuarantine *quarantine;
    uint32_t   quarantineCount, quarantineCapacity;
} LightRegistry;


typedef struct LightBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    LightData*      mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max lights
    uint32_t        count;      // current light count
} LightBuffer;

#endif // ANO_LIGHT_TYPES_H
