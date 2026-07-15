/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Light-palette + runtime-registry types. Domain fragment of structs.h, relies on GpuAllocation.
// Not a standalone header.

#ifndef ANO_LIGHT_TYPES_H
#define ANO_LIGHT_TYPES_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/gpu_alloc.h"


/* Lighting */

// glTF KHR_lights_punctual. Pose from transforms[transformIndex]; photometric params here.
// LightData = 5x vec4 (80B) std430.
typedef enum LightType
{
    LIGHT_TYPE_DIRECTIONAL = 0, // direction only
    LIGHT_TYPE_POINT       = 1, // position + range
    LIGHT_TYPE_SPOT        = 2, // position + direction + range + cones
} LightType;

typedef struct LightData
{
    // row 0
    float       color[3];       // linear RGB, normalized
    float       intensity;      // brightness multiplier
    // row 1
    float       range;          // attenuation cutoff distance, <= 0 unbounded
    float       innerConeCos;   // spot inner cone half-angle cosine
    float       outerConeCos;   // spot outer cone half-angle cosine
    uint32_t    type;           // LightType
    // row 2
    uint32_t    transformIndex; // drives world position + direction
    uint32_t    enabled;        // 0 = ignored, 1 = active
    uint32_t    pad0;
    uint32_t    pad1;
    // row 3. Local model-space offset: world pos = transforms[transformIndex] * vec4(localOffset, 1).
    // Mirrored in flat.frag, transmission.frag, lightcull.comp, shadowsetup.comp.
    float       localOffset[3];
    uint32_t    pad2;
    // row 4. Local model-space direction: world forward = normalize(mat3(transforms[transformIndex]) * localDir).
    // Decoded in flat.frag, transmission.frag, shadowsetup.comp. lightcull.comp carries the row for stride only.
    float       localDir[3];
    uint32_t    pad3;
} LightData; // 80 bytes
_Static_assert(sizeof(LightData) == 80, "LightData must be 80B (5x vec4) to match the GLSL std430 mirrors");

// Runtime light lifecycle over dynamic palette [base, base+capacity). Cull count = base+highWater. Render-thread only.
enum { LIGHT_ROW_FREE = 0u, LIGHT_ROW_LIVE = 1u, LIGHT_ROW_QUARANTINED = 2u };

typedef struct LightRowQuarantine { uint32_t row; uint64_t safeFrame; } LightRowQuarantine;

typedef struct LightRegistry
{
    uint32_t   base;            // dynamic rows start here (== init light count)
    uint32_t   capacity;        // dynamic row ceiling
    uint32_t   framesInFlight;  // == MAX_FRAMES_IN_FLIGHT

    uint32_t  *idToRow;         // light_id -> relative row, or UNMAPPED
    uint32_t   idCapacity;

    uint8_t   *rowState;        // [rowsCapacity] LIGHT_ROW_*
    uint32_t  *rowParent;       // [rowsCapacity] parent render_id
    uint32_t  *rowLightId;      // [rowsCapacity] light_id
    LightData *rowMirror;       // [rowsCapacity] CPU mirror of staged LightData, RMW base for partial updates
    uint32_t  *rowShadowBase;   // [rowsCapacity] shadow frustum block base (ANO_SHADOW_NONE = non-casting)
    uint32_t   rowsCapacity;

    uint32_t  *freeRows;        // stack of FREE relative rows
    uint32_t   freeCount, freeCapacity;

    uint32_t   highWater;       // peak relative rows used

    LightRowQuarantine *quarantine;
    uint32_t   quarantineCount, quarantineCapacity;
} LightRegistry;


// Unused host-mapped FIF shape. Live light palette: SlotUpload (RendererState.lightBuffer).
typedef struct LightBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    LightData*      mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t        capacity;
    uint32_t        count;
} LightBuffer;

#endif // ANO_LIGHT_TYPES_H
