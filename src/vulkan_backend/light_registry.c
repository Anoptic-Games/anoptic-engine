/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <stdlib.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/light_registry.h"

// Runtime light registry. Render-thread only, no locks. Owns dynamic light palette [base, base+capacity).

void light_registry_init(LightRegistry* r, uint32_t base, uint32_t capacity, uint32_t framesInFlight) {
    memset(r, 0, sizeof(*r));
    r->base = base;
    r->capacity = capacity;
    r->framesInFlight = framesInFlight;
}

void light_registry_destroy(LightRegistry* r) {
    free(r->idToRow); free(r->rowState); free(r->rowParent); free(r->rowLightId); free(r->rowMirror);
    free(r->rowShadowBase); free(r->freeRows); free(r->quarantine);
    memset(r, 0, sizeof(*r));
}

// Grow the per-row arrays (state/parent/lightId share rowsCapacity), zero/UNMAPPED-filling the tail.
static bool lr_reserve_rows(LightRegistry* r, uint32_t need) {
    if (need <= r->rowsCapacity) return true;
    uint32_t nc = r->rowsCapacity ? r->rowsCapacity : 16u;
    while (nc < need) nc *= 2u;
    uint8_t*  s  = realloc(r->rowState,   nc);
    uint32_t* pp = realloc(r->rowParent,  (size_t)nc * sizeof(uint32_t));
    uint32_t* li = realloc(r->rowLightId, (size_t)nc * sizeof(uint32_t));
    LightData* mi = realloc(r->rowMirror, (size_t)nc * sizeof(LightData));
    uint32_t* sb = realloc(r->rowShadowBase, (size_t)nc * sizeof(uint32_t));
    if (s)  r->rowState   = s;
    if (pp) r->rowParent  = pp;
    if (li) r->rowLightId = li;
    if (mi) r->rowMirror  = mi;
    if (sb) r->rowShadowBase = sb;
    if (!s || !pp || !li || !mi || !sb) return false;
    for (uint32_t i = r->rowsCapacity; i < nc; i++) {
        s[i] = LIGHT_ROW_FREE; pp[i] = ANO_RENDER_SLOT_UNMAPPED; li[i] = ANO_RENDER_SLOT_UNMAPPED;
        sb[i] = ANO_SHADOW_NONE;
    }
    r->rowsCapacity = nc;
    return true;
}

static bool lr_reserve_ids(LightRegistry* r, uint32_t need) {
    if (need <= r->idCapacity) return true;
    uint32_t nc = r->idCapacity ? r->idCapacity : 16u;
    while (nc < need) {
        if (nc > (0xFFFFFFFFu / 2u)) { nc = need; break; } // doubling cap
        nc *= 2u;
    }
    uint32_t* p = realloc(r->idToRow, (size_t)nc * sizeof(uint32_t));
    if (!p) return false;
    for (uint32_t i = r->idCapacity; i < nc; i++) p[i] = ANO_RENDER_SLOT_UNMAPPED;
    r->idToRow = p; r->idCapacity = nc;
    return true;
}

static void lr_push_free(LightRegistry* r, uint32_t row) {
    if (r->freeCount >= r->freeCapacity) {
        uint32_t nc = r->freeCapacity ? r->freeCapacity * 2u : 16u;
        uint32_t* p = realloc(r->freeRows, (size_t)nc * sizeof(uint32_t));
        if (!p) return; // OOM, row leaks
        r->freeRows = p; r->freeCapacity = nc;
    }
    r->freeRows[r->freeCount++] = row;
}

static void lr_push_quarantine(LightRegistry* r, uint32_t row, uint64_t safeFrame) {
    if (r->quarantineCount >= r->quarantineCapacity) {
        uint32_t nc = r->quarantineCapacity ? r->quarantineCapacity * 2u : 16u;
        LightRowQuarantine* p = realloc(r->quarantine, (size_t)nc * sizeof(LightRowQuarantine));
        if (!p) return; // OOM, row stays quarantined
        r->quarantine = p; r->quarantineCapacity = nc;
    }
    r->quarantine[r->quarantineCount++] = (LightRowQuarantine){ .row = row, .safeFrame = safeFrame };
}

// Map light_id to a fresh row driven by parentRid. Returns ABSOLUTE palette row, or UNMAPPED on exhaustion/OOM/double-attach.
uint32_t light_registry_alloc(LightRegistry* r, uint32_t light_id, uint32_t parentRid) {
    if (light_id == ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED; // sentinel reserved
    if (!lr_reserve_ids(r, light_id + 1u)) return ANO_RENDER_SLOT_UNMAPPED;
    if (r->idToRow[light_id] != ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED; // double-attach
    uint32_t row;
    if (r->freeCount > 0u) {
        row = r->freeRows[--r->freeCount];           // reuse expired hole
    } else {
        if (r->highWater >= r->capacity) return ANO_RENDER_SLOT_UNMAPPED; // palette full
        if (!lr_reserve_rows(r, r->highWater + 1u)) return ANO_RENDER_SLOT_UNMAPPED;
        row = r->highWater++;
    }
    r->rowState[row]     = LIGHT_ROW_LIVE;
    r->rowParent[row]    = parentRid;
    r->rowLightId[row]   = light_id;
    r->idToRow[light_id] = row;
    return r->base + row;
}

uint32_t light_registry_resolve(const LightRegistry* r, uint32_t light_id) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    return (row == ANO_RENDER_SLOT_UNMAPPED) ? ANO_RENDER_SLOT_UNMAPPED : r->base + row;
}

uint32_t light_registry_parent_of(const LightRegistry* r, uint32_t light_id) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    return (row == ANO_RENDER_SLOT_UNMAPPED) ? ANO_RENDER_SLOT_UNMAPPED : r->rowParent[row];
}

// Quarantine light_id's row and unmap the id. Returns ABSOLUTE row to disable, or UNMAPPED if not mapped.
uint32_t light_registry_detach(LightRegistry* r, uint32_t light_id, uint64_t currentFrame) {
    if (light_id >= r->idCapacity) return ANO_RENDER_SLOT_UNMAPPED;
    uint32_t row = r->idToRow[light_id];
    if (row == ANO_RENDER_SLOT_UNMAPPED) return ANO_RENDER_SLOT_UNMAPPED;
    r->idToRow[light_id] = ANO_RENDER_SLOT_UNMAPPED;
    r->rowState[row]     = LIGHT_ROW_QUARANTINED;
    r->rowLightId[row]   = ANO_RENDER_SLOT_UNMAPPED;
    lr_push_quarantine(r, row, currentFrame + r->framesInFlight);
    return r->base + row;
}

// Detach every light attached to parentRid. Writes up to `max` ABSOLUTE rows to disable into out_rows, returns the count.
// Call in a loop while it returns `max`.
uint32_t light_registry_detach_children(LightRegistry* r, uint32_t parentRid,
                                               uint64_t currentFrame, uint32_t* out_rows, uint32_t max) {
    uint32_t n = 0;
    for (uint32_t row = 0; row < r->highWater && n < max; row++) {
        if (r->rowState[row] != LIGHT_ROW_LIVE || r->rowParent[row] != parentRid) continue;
        uint32_t lid = r->rowLightId[row];
        if (lid != ANO_RENDER_SLOT_UNMAPPED && lid < r->idCapacity) r->idToRow[lid] = ANO_RENDER_SLOT_UNMAPPED;
        r->rowState[row]   = LIGHT_ROW_QUARANTINED;
        r->rowLightId[row] = ANO_RENDER_SLOT_UNMAPPED;
        lr_push_quarantine(r, row, currentFrame + r->framesInFlight);
        out_rows[n++] = r->base + row;
    }
    return n;
}

// Return quarantined rows whose safeFrame has elapsed to the free-list for reuse.
void light_registry_collect(LightRegistry* r, uint64_t currentFrame) {
    uint32_t w = 0;
    for (uint32_t i = 0; i < r->quarantineCount; i++) {
        LightRowQuarantine q = r->quarantine[i];
        if (q.safeFrame <= currentFrame) {
            r->rowState[q.row] = LIGHT_ROW_FREE;
            lr_push_free(r, q.row);
        } else {
            r->quarantine[w++] = q; // not yet safe
        }
    }
    r->quarantineCount = w;
}

// Ascending u32 compare.
static int lr_cmp_u32_asc(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

// Peel the trailing contiguous run of FREE rows off the top, shrinking the published cull light count. Returns rows reclaimed.
// Call AFTER collect and BEFORE publishing the count.
uint32_t light_registry_compact(LightRegistry* r) {
    if (r->freeCount == 0u) return 0u;
    // Sort ascending so the trailing free run is a suffix.
    qsort(r->freeRows, r->freeCount, sizeof(uint32_t), lr_cmp_u32_asc);
    uint32_t before = r->highWater;
    while (r->freeCount > 0u && r->freeRows[r->freeCount - 1u] == r->highWater - 1u) {
        r->freeCount--;
        r->highWater--;
    }
    return before - r->highWater;
}

// Aim into LightData.localDir, defaulting a zero vector to model -Z.
static void light_set_dir(LightData* L, const float d[3]) {
    bool zero = (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f);
    L->localDir[0] = zero ? 0.0f : d[0];
    L->localDir[1] = zero ? 0.0f : d[1];
    L->localDir[2] = zero ? -1.0f : d[2];
}

// Build a GPU LightData from bridge params + a resolved parent slot (transformIndex) + local offset.
LightData light_data_from_params(const RenderLightParams* p, uint32_t transformIndex, const float off[3]) {
    LightData L = {0};
    L.color[0] = p->color[0]; L.color[1] = p->color[1]; L.color[2] = p->color[2];
    L.intensity = p->intensity; L.range = p->range;
    L.innerConeCos = p->innerConeCos; L.outerConeCos = p->outerConeCos;
    L.type = (uint32_t)p->type;
    L.transformIndex = transformIndex;
    L.localOffset[0] = off[0]; L.localOffset[1] = off[1]; L.localOffset[2] = off[2];
    light_set_dir(&L, p->localDir);
    L.enabled = 1u;
    return L;
}

// Merge only the producer fields named in `fields` into an existing mirror LightData. Unnamed fields keep their value.
void light_apply_fields(LightData* dst, const RenderLightParams* p, const float off[3], uint32_t fields) {
    if (fields & ANO_LIGHT_FIELD_COLOR)     { dst->color[0] = p->color[0]; dst->color[1] = p->color[1]; dst->color[2] = p->color[2]; }
    if (fields & ANO_LIGHT_FIELD_INTENSITY) dst->intensity = p->intensity;
    if (fields & ANO_LIGHT_FIELD_RANGE)     dst->range = p->range;
    if (fields & ANO_LIGHT_FIELD_CONE)      { dst->innerConeCos = p->innerConeCos; dst->outerConeCos = p->outerConeCos; }
    if (fields & ANO_LIGHT_FIELD_TYPE)      dst->type = (uint32_t)p->type;
    if (fields & ANO_LIGHT_FIELD_OFFSET)    { dst->localOffset[0] = off[0]; dst->localOffset[1] = off[1]; dst->localOffset[2] = off[2]; }
    if (fields & ANO_LIGHT_FIELD_DIRECTION) light_set_dir(dst, p->localDir);
}
