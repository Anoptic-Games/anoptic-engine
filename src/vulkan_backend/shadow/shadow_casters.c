/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"

/* Runtime Shadow-Frustum Pools */

// Alloc a runtime frustum block base for a casting light (point = 6 cube faces, dir/spot = 1).
// ANO_SHADOW_NONE when the type's pool is exhausted.
static uint32_t shadow_frustum_alloc(RendererState* st, uint32_t lightType) {
    if (lightType == LIGHT_TYPE_POINT)
        return st->rtPointFreeCount  ? st->rtPointFree[--st->rtPointFreeCount]   : ANO_SHADOW_NONE;
    return     st->rtSingleFreeCount ? st->rtSingleFree[--st->rtSingleFreeCount] : ANO_SHADOW_NONE;
}
static void shadow_frustum_free(RendererState* st, uint32_t base) {
    if (base == ANO_SHADOW_NONE) return;
    // Bounds-guard the push.
    if (base >= ANO_SHADOW_RT_POINT_BASE) {
        if (st->rtPointFreeCount  < ANO_SHADOW_RT_POINT_COUNT)  st->rtPointFree[st->rtPointFreeCount++]   = base;
    } else {
        if (st->rtSingleFreeCount < ANO_SHADOW_RT_SINGLE_COUNT) st->rtSingleFree[st->rtSingleFreeCount++] = base;
    }
}

// Stage light shadow info+config (castsShadow=1, base, count); record base on registry row.
// Budget-full stages castsShadow=0 and stays shadowless. lightPalIdx = absolute palette row; regRow = relative registry row.
void shadow_caster_attach(RendererState* st, uint32_t lightPalIdx, uint32_t regRow,
                                 uint32_t lightType, uint32_t frameIndex) {
    uint32_t base = shadow_frustum_alloc(st, lightType);
    st->lightRegistry.rowShadowBase[regRow] = base; // NONE if budget full
    if (base == ANO_SHADOW_NONE) {
        ShadowLightInfo si = {0}; // clear prior caster's info on this row
        slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
        return;
    }
    uint32_t blockSize = (lightType == LIGHT_TYPE_POINT) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightPalIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .live = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // recycled block, layers stale
    // Row mirror is complete at attach time, seeded by the caller.
    LightData* mir = &st->lightRegistry.rowMirror[regRow];
    shadow_volume_set(st, base, blockSize, mir->transformIndex, mir->localOffset, mir->range);
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
}

// Free a registry row's runtime shadow caster (if any).
// Marks its frustum block inactive (mirror + SlotUpload), returns it to the pool, rowShadowBase -> NONE.
void shadow_caster_detach(RendererState* st, uint32_t regRow, uint32_t frameIndex) {
    uint32_t base = st->lightRegistry.rowShadowBase[regRow];
    if (base == ANO_SHADOW_NONE) return;
    uint32_t blockSize = (base >= ANO_SHADOW_RT_POINT_BASE) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = st->shadowCfgMirror[base + f];
        c.live = 0u;
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // freed block, stale content
    shadow_volume_clear(st, base, blockSize);
    shadow_frustum_free(st, base);
    st->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
}

// Disable + quarantine every runtime light attached to a renderable being destroyed.
// Stages enabled=0 into this frame per child, frees its runtime shadow frustum, rows return to the free-list.
void cascade_detach_lights(RendererState* state, uint32_t parentRid, uint32_t frameIndex) {
    uint32_t rows[64]; uint32_t n;
    LightData off = {0}; // enabled == 0
    do {
        n = light_registry_detach_children(&state->lightRegistry, parentRid, state->globalFrame, rows, 64u);
        for (uint32_t k = 0; k < n; k++) {
            slot_upload_stage(&state->lightBuffer, frameIndex, rows[k], &off);
            shadow_caster_detach(state, rows[k] - state->lightRegistry.base, frameIndex);
        }
    } while (n == 64u);
}

// Sole decode: light type -> static budget row + ceiling. NULL == outside LightType.
static uint32_t* shadow_static_budget(RendererState* st, uint32_t lightType, uint32_t* outBudget) {
    switch (lightType) {
    case LIGHT_TYPE_DIRECTIONAL: *outBudget = ANO_SHADOW_DIR_COUNT;   return &st->shadowTypeUsed[LIGHT_TYPE_DIRECTIONAL];
    case LIGHT_TYPE_POINT:       *outBudget = ANO_SHADOW_POINT_COUNT; return &st->shadowTypeUsed[LIGHT_TYPE_POINT];
    case LIGHT_TYPE_SPOT:        *outBudget = ANO_SHADOW_SPOT_COUNT;  return &st->shadowTypeUsed[LIGHT_TYPE_SPOT];
    default:                     *outBudget = 0u;                     return NULL;
    }
}

// Advance *s to next active config owned by lightIdx; footprint + type from stored config.
// in:  s, scan cursor (in/out); outSize/outType, found block footprint and light type
// out: bool, false == no further owned block; out-params untouched then
static bool next_owned_block(const RendererState* st, uint32_t lightIdx, uint32_t* s,
                             uint32_t* outSize, uint32_t* outType) {
    for (; *s < st->shadowFrustumNext; (*s)++) {
        ShadowFrustumConfig owner = st->shadowCfgMirror[*s];
        if (!owner.live || owner.lightIndex != lightIdx) continue;
        *outSize = owner.lightType == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
        *outType = owner.lightType;
        return true;
    }
    return false;
}

/* Static Shadow-Frustum Reclamation */

// Retire a released static block by exact footprint. NONE is a no-op.
// Two shapes only: point (6) and single (1). Other footprints have no free-list.
// Push capacity = region bound (structs.h).
static void static_frustum_free(RendererState* st, uint32_t base, uint32_t blockSize) {
    if (base == ANO_SHADOW_NONE) return;
    if (blockSize == ANO_SHADOW_CUBE_FACES) {
        if (st->stPointFreeCount  < ANO_SHADOW_ST_POINT_FREE_CAP)  st->stPointFree[st->stPointFreeCount++]   = base;
    } else if (blockSize == 1u) {
        if (st->stSingleFreeCount < ANO_SHADOW_ST_SINGLE_FREE_CAP) st->stSingleFree[st->stSingleFreeCount++] = base;
    }
}
// Base of a retired blockSize block, or ANO_SHADOW_NONE. No split/coalesce.
static uint32_t static_frustum_alloc(RendererState* st, uint32_t blockSize) {
    if (blockSize == ANO_SHADOW_CUBE_FACES)
        return st->stPointFreeCount  ? st->stPointFree[--st->stPointFreeCount]   : ANO_SHADOW_NONE;
    if (blockSize == 1u)
        return st->stSingleFreeCount ? st->stSingleFree[--st->stSingleFreeCount] : ANO_SHADOW_NONE;
    return ANO_SHADOW_NONE;
}

// Release every static frustum this palette row owns: inactive, clear volumes, return budget.
// Hold one matching wantSize for in-place rewrite; retire the rest. Cursor never rewinds.
// in:  lightIdx, static palette row; wantSize, re-register footprint (0 = none)
// out: base of first released block with footprint == wantSize, else ANO_SHADOW_NONE
// inv: active below shadowFrustumNext are register_static_shadow's; budget dec cannot underflow; runtime sits above; block contiguous from first entry
static uint32_t static_shadow_release_owned(RendererState* st, uint32_t lightIdx,
                                            uint32_t frameIndex, uint32_t wantSize) {
    uint32_t reuse = ANO_SHADOW_NONE;
    bool released = false;
    uint32_t blockSize, blockType;
    for (uint32_t s = 0; next_owned_block(st, lightIdx, &s, &blockSize, &blockType); s += blockSize) {
        for (uint32_t f = 0; f < blockSize; f++) {
            ShadowFrustumConfig c = st->shadowCfgMirror[s + f];
            c.live = 0u;
            st->shadowCfgMirror[s + f] = c;
            slot_upload_stage(&st->shadowConfig, frameIndex, s + f, &c);
        }
        shadow_layers_invalidate(st, s, blockSize); // released block, stale content
        shadow_volume_clear(st, s, blockSize);
        // Decrement through the sole decode point: the released block's OWN type, never the caller's.
        uint32_t oldBudget;
        uint32_t* oldUsed = shadow_static_budget(st, blockType, &oldBudget);
        if (oldUsed) *oldUsed -= 1u;
        if (reuse == ANO_SHADOW_NONE && blockSize == wantSize) reuse = s; // held for the caller's in-place rewrite
        else static_frustum_free(st, s, blockSize);                       // otherwise retire, never strand
        released = true;
    }
    if (released) {
        ShadowLightInfo si = {0}; // the row's info named a dead block; last-write-wins in-frame
        slot_upload_stage(&st->shadowInfo, frameIndex, lightIdx, &si);
    }
    return reuse;
}

// Register STATIC-region caster for staged light row. Allocates frustum (point=6, dir/spot=1);
// stages config (active=1) + light info (castsShadow=1, base, count). Past budget/region -> shadowless.
// Outside LightType -> shadowless. Re-reg REPLACES: release prior, rewrite matching footprint in place.
// Footprint change retires old, allocs from free-list before cursor bump. Region-full = overflow contract.
void register_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t lightType,
                                   uint32_t frameIndex, uint32_t parentSlot, float range) {
    uint32_t budget;
    uint32_t* used = shadow_static_budget(st, lightType, &budget);
    if (!used) return; // not a LightType, stays shadowless
    uint32_t blockSize = lightType == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
    uint32_t base = static_shadow_release_owned(st, lightIdx, frameIndex, blockSize);
    if (*used >= budget) { // budget full, stays shadowless (release already cleared the info)
        static_frustum_free(st, base, blockSize); // the rewrite it was held for is not happening
        return;
    }
    if (base == ANO_SHADOW_NONE)
        base = static_frustum_alloc(st, blockSize); // a retired block of this exact shape
    if (base == ANO_SHADOW_NONE) { // nothing to reuse: bump the region
        if (st->shadowFrustumNext + blockSize > ANO_SHADOW_STATIC_FRUSTUM_COUNT)
            return; // region full, stays shadowless
        base = st->shadowFrustumNext;
        st->shadowFrustumNext += blockSize;
    }
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .live = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // fresh/reused static block, render before first sample
    // create-with-light rides the slot origin (localOffset zero).
    float zeroOff[3] = { 0.0f, 0.0f, 0.0f };
    shadow_volume_set(st, base, blockSize, parentSlot, zeroOff, range);
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightIdx, &si);
    *used += 1u;
}

// Revoke a static palette row's casting: release its blocks, leave it lit and shadowless.
// Declared in backend.h beside the static-row lifetime contract. Command path only.
void unregister_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t frameIndex) {
    (void)static_shadow_release_owned(st, lightIdx, frameIndex, 0u); // 0 = nothing to reuse for
}

// UPDATE half of static-row contract (backend.h): restage owned volumes from new range; stale layers.
// Command path only. No grants, budget, or region moves. Block-less row = no-op.
// in:  lightIdx, static palette row; parentSlot, live transform slot; range, restaged range
// out: none
void refresh_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t parentSlot, float range) {
    float zeroOff[3] = { 0.0f, 0.0f, 0.0f }; // static rows ride the slot origin, as at registration
    uint32_t blockSize, blockType;
    for (uint32_t s = 0; next_owned_block(st, lightIdx, &s, &blockSize, &blockType); s += blockSize) {
        shadow_layers_invalidate(st, s, blockSize); // photometrics moved, cached layers stale
        shadow_volume_set(st, s, blockSize, parentSlot, zeroOff, range);
    }
}

// Query half of static-row contract (backend.h): does the row own an active static block.
// Same ownership decode as refresh_static_shadow. No caller-supplied type.
// in:  lightIdx, static palette row
// out: bool, true if row casts; outType = stored LightType, or LIGHT_TYPE_COUNT if block-less
[[nodiscard]] bool static_shadow_row_casts(const RendererState* st, uint32_t lightIdx, uint32_t* outType) {
    uint32_t s = 0u, blockSize;
    if (next_owned_block(st, lightIdx, &s, &blockSize, outType)) return true;
    *outType = LIGHT_TYPE_COUNT; // block-less: no type to disagree with
    return false;
}
