/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <math.h>
#include <string.h>
#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/shadow/shadow.h"

// Invalidate a frustum block's cached atlas layers and mark it matrix-dirty. NONE/no-op safe.
void shadow_layers_invalidate(RendererState* st, uint32_t base, uint32_t count) {
    if (base == ANO_SHADOW_NONE) return;
    for (uint32_t f = 0; f < count && base + f < ANO_SHADOW_FRUSTUM_COUNT; f++) {
        st->shadowLayerValid[base + f]  = false;
        st->shadowMatrixDirty[base + f] = true;
    }
}

// --- Swept-bound motion exposure (review finding 8, deferred half) --------------------------------
// One static world sphere bounds each parametric mover for all time; unbounded movers count moverUnboundedCount.

// Conservative world-space sphere containing the slot's mesh over its WHOLE trajectory.
// in: base pose (columns 0-2 linear L, column 3 translation T), model mesh sphere (cm, rm), motion.
// out: c/r; returns false when the trajectory has no finite bound.
static bool mover_swept_bound(const mat4 base, const float cm[3], float rm,
                              const AnoMotionDescriptor* m, float c[3], float* r)
{
    const float* l0 = base[0]; const float* l1 = base[1]; const float* l2 = base[2]; const float* T = base[3];
    float frob = sqrtf(l0[0]*l0[0] + l0[1]*l0[1] + l0[2]*l0[2]
                     + l1[0]*l1[0] + l1[1]*l1[1] + l1[2]*l1[2]
                     + l2[0]*l2[0] + l2[1]*l2[1] + l2[2]*l2[2]);
    float C[3] = { l0[0]*cm[0] + l1[0]*cm[1] + l2[0]*cm[2] + T[0],   // static world sphere L*cm + T
                   l0[1]*cm[0] + l1[1]*cm[1] + l2[1]*cm[2] + T[1],
                   l0[2]*cm[0] + l1[2]*cm[1] + l2[2]*cm[2] + T[2] };
    float Rw = frob * rm;
    float speed = sqrtf(m->p0.v[0]*m->p0.v[0] + m->p0.v[1]*m->p0.v[1] + m->p0.v[2]*m->p0.v[2]);

    switch (m->type) {
    case (uint32_t)ANO_MOTION_SPIN:
        // base * R about the LOCAL origin, position pinned at T.
        if (speed <= 1e-4f) { c[0] = C[0]; c[1] = C[1]; c[2] = C[2]; *r = Rw; return true; } // GPU treats as static
        c[0] = T[0]; c[1] = T[1]; c[2] = T[2];
        *r = frob * (sqrtf(cm[0]*cm[0] + cm[1]*cm[1] + cm[2]*cm[2]) + rm);
        return true;
    case (uint32_t)ANO_MOTION_ORBIT: {
        // R * base about a world axis through the ORIGIN; sweep sphere about the axis foot, radius = axis dist + Rw.
        if (speed <= 1e-4f) { c[0] = C[0]; c[1] = C[1]; c[2] = C[2]; *r = Rw; return true; }
        float ax = m->p0.v[0] / speed, ay = m->p0.v[1] / speed, az = m->p0.v[2] / speed;
        float h = C[0]*ax + C[1]*ay + C[2]*az;
        c[0] = h * ax; c[1] = h * ay; c[2] = h * az;
        float dx = C[0] - c[0], dy = C[1] - c[1], dz = C[2] - c[2];
        *r = sqrtf(dx*dx + dy*dy + dz*dz) + Rw;
        return true;
    }
    case (uint32_t)ANO_MOTION_LINEAR:
        if (speed <= 1e-4f) { c[0] = C[0]; c[1] = C[1]; c[2] = C[2]; *r = Rw; return true; }
        return false; // unbounded in time
    case (uint32_t)ANO_MOTION_KEPLER: {
        // Apoapsis a(1+e) bounds the ellipse from the focus at the base position.
        float a = m->p0.v[0], e = m->p0.v[1];
        if (!(a > 0.0f) || !(e >= 0.0f) || e >= 1.0f) return false; // open/degenerate orbit
        c[0] = C[0]; c[1] = C[1]; c[2] = C[2];
        *r = Rw + a * (1.0f + e);
        return true;
    }
    default:
        return false; // STREAMED / unknown, no closed form
    }
}

// Mover sphere vs a frustum's caster volume. An unbounded volume (radius < 0) is exposed by any mover.
static bool mover_exposes(const ShadowCasterVolume* v, const float c[3], float r) {
    if (v->radius < 0.0f) return true;
    float dx = c[0] - v->center[0], dy = c[1] - v->center[1], dz = c[2] - v->center[2];
    float rr = r + v->radius;
    return dx*dx + dy*dy + dz*dz <= rr*rr;
}

// Rebuild one mover's exposure mask against every configured caster volume (bound changed/created).
static void mover_expose_rebuild(RendererState* st, MoverBound* mb) {
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
        if (mb->exposeMask & (1ull << s)) st->shadowExposed[s]--;
    mb->exposeMask = 0u;
    if (mb->unbounded) return;
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
        if (st->shadowVolume[s].parentSlot == ANO_RENDER_SLOT_UNMAPPED) continue;
        if (mover_exposes(&st->shadowVolume[s], mb->center, mb->radius)) {
            mb->exposeMask |= 1ull << s;
            st->shadowExposed[s]++;
        }
    }
}

// Recompute a mover's bound from the slot mirrors + retained descriptor, then its exposure.
// Mesh-less/unregistered slots bound to the pose origin.
static void mover_bound_refresh(RendererState* st, MoverBound* mb) {
    float cm[3] = { 0.0f, 0.0f, 0.0f }, rm = 0.0f;
    uint32_t mi = st->slotMeshIdx[mb->slot];
    if (mi < st->globalGeometryPool.meshCount) {
        MeshRegion* mesh = &st->globalGeometryPool.meshes[mi];
        cm[0] = mesh->boundingSphereCenter[0];
        cm[1] = mesh->boundingSphereCenter[1];
        cm[2] = mesh->boundingSphereCenter[2];
        rm    = mesh->boundingSphereRadius;
    }
    uint32_t was = mb->unbounded;
    bool bounded = st->sweptExposure
        && mover_swept_bound(st->slotBasePose[mb->slot], cm, rm, &mb->motion, mb->center, &mb->radius);
    mb->unbounded = bounded ? 0u : 1u;
    if (mb->unbounded && !was)      st->moverUnboundedCount++;
    else if (!mb->unbounded && was) st->moverUnboundedCount--;
    mover_expose_rebuild(st, mb);
}

// Upsert the slot's mover record (motion established or changed).
// Growth failure poisons the feature into the permanent blanket fallback.
static void mover_set(RendererState* st, uint32_t slot, const AnoMotionDescriptor* m) {
    uint32_t idx = st->slotMoverIdx[slot];
    if (idx == ANO_RENDER_SLOT_UNMAPPED) {
        if (st->sweptPoisoned) return;
        if (st->moverCount == st->moverCap) {
            uint32_t nc = st->moverCap ? st->moverCap * 2u : 64u;
            MoverBound* nm = (MoverBound*)realloc(st->movers, (size_t)nc * sizeof(MoverBound));
            if (!nm) {
                ano_log(ANO_ERROR, "Shadow cache: mover array growth failed; swept exposure disabled.");
                st->sweptPoisoned = true;
                return;
            }
            st->movers = nm;
            st->moverCap = nc;
        }
        idx = st->moverCount++;
        st->movers[idx] = (MoverBound){ .slot = slot };
        st->slotMoverIdx[slot] = idx;
    }
    st->movers[idx].motion = *m;
    mover_bound_refresh(st, &st->movers[idx]);
}

// Drop the slot's mover record, release its exposure contributions, swap-remove, re-point the moved slot.
static void mover_remove(RendererState* st, uint32_t slot) {
    uint32_t idx = st->slotMoverIdx[slot];
    if (idx == ANO_RENDER_SLOT_UNMAPPED) return;
    MoverBound* mb = &st->movers[idx];
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
        if (mb->exposeMask & (1ull << s)) st->shadowExposed[s]--;
    if (mb->unbounded && st->moverUnboundedCount) st->moverUnboundedCount--;
    uint32_t last = --st->moverCount;
    if (idx != last) {
        st->movers[idx] = st->movers[last];
        st->slotMoverIdx[st->movers[idx].slot] = idx;
    }
    st->slotMoverIdx[slot] = ANO_RENDER_SLOT_UNMAPPED;
}

// Slot's pose or mesh mirror changed, recompute its mover bound. No-op for non-movers.
void mover_refresh_slot(RendererState* st, uint32_t slot) {
    if (slot < st->slotMotionCap && st->slotMoverIdx[slot] != ANO_RENDER_SLOT_UNMAPPED)
        mover_bound_refresh(st, &st->movers[st->slotMoverIdx[slot]]);
}

// Rebuild one frustum's exposure count from scratch, fixing every mover's mask bit for this frustum.
static void shadow_expose_rebuild_frustum(RendererState* st, uint32_t s) {
    uint64_t bit = 1ull << s;
    bool live = st->shadowVolume[s].parentSlot != ANO_RENDER_SLOT_UNMAPPED;
    uint32_t n = 0u;
    for (uint32_t i = 0; i < st->moverCount; i++) {
        MoverBound* mb = &st->movers[i];
        bool hit = live && !mb->unbounded && mover_exposes(&st->shadowVolume[s], mb->center, mb->radius);
        mb->exposeMask = hit ? (mb->exposeMask | bit) : (mb->exposeMask & ~bit);
        if (hit) n++;
    }
    st->shadowExposed[s] = n;
}

// Refresh a frustum's cached world influence sphere from its parent's BASE pose mirror + offset.
// A parent WITH motion gets a stale-by-design sphere, never consulted.
static void shadow_volume_recompute(RendererState* st, uint32_t s) {
    ShadowCasterVolume* v = &st->shadowVolume[s];
    uint32_t p = v->parentSlot;
    if (p == ANO_RENDER_SLOT_UNMAPPED) return;
    if (st->shadowCfgMirror[s].lightType == LIGHT_TYPE_DIRECTIONAL || v->range <= 0.0f
        || p >= st->slotMotionCap) {
        v->radius = -1.0f; // unbounded, any mover exposes
        return;
    }
    const float* b0 = st->slotBasePose[p][0]; const float* b1 = st->slotBasePose[p][1];
    const float* b2 = st->slotBasePose[p][2]; const float* b3 = st->slotBasePose[p][3];
    v->center[0] = b0[0]*v->offset[0] + b1[0]*v->offset[1] + b2[0]*v->offset[2] + b3[0];
    v->center[1] = b0[1]*v->offset[0] + b1[1]*v->offset[1] + b2[1]*v->offset[2] + b3[1];
    v->center[2] = b0[2]*v->offset[0] + b1[2]*v->offset[1] + b2[2]*v->offset[2] + b3[2];
    v->radius = v->range;
}

// Install a caster's influence volume on its frustum block + retest every mover against it.
// Call AFTER shadowCfgMirror is filled. NONE/no-op safe.
void shadow_volume_set(RendererState* st, uint32_t base, uint32_t count, uint32_t parentSlot,
                              const float off[3], float range) {
    if (base == ANO_SHADOW_NONE) return;
    for (uint32_t f = 0; f < count && base + f < ANO_SHADOW_FRUSTUM_COUNT; f++) {
        ShadowCasterVolume* v = &st->shadowVolume[base + f];
        v->parentSlot = parentSlot;
        v->offset[0] = off[0]; v->offset[1] = off[1]; v->offset[2] = off[2];
        v->range = range;
        shadow_volume_recompute(st, base + f);
        shadow_expose_rebuild_frustum(st, base + f);
    }
}

// Clear a detached caster's volumes (the block returns to the pool).
void shadow_volume_clear(RendererState* st, uint32_t base, uint32_t count) {
    if (base == ANO_SHADOW_NONE) return;
    for (uint32_t f = 0; f < count && base + f < ANO_SHADOW_FRUSTUM_COUNT; f++) {
        st->shadowVolume[base + f].parentSlot = ANO_RENDER_SLOT_UNMAPPED;
        shadow_expose_rebuild_frustum(st, base + f);
    }
}

// Parent slot teleported, refresh every caster volume it drives and mark the block matrix-dirty.
void shadow_volumes_reparent(RendererState* st, uint32_t slot) {
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
        if (st->shadowVolume[s].parentSlot == slot) {
            shadow_volume_recompute(st, s);
            shadow_expose_rebuild_frustum(st, s);
            st->shadowMatrixDirty[s] = true;
        }
}

// Per-slot mover bookkeeping (flags + swept-exposure records). Destroy untracks with a NULL descriptor.
void shadow_track_motion(RendererState* st, uint32_t slot, const AnoMotionDescriptor* m) {
    if (slot >= st->slotMotionCap) return;
    uint8_t on = (m && m->type != (uint32_t)ANO_MOTION_STATIC) ? 1u : 0u;
    if (st->slotMotion[slot] != on) {
        st->slotMotion[slot] = on;
        if (on) st->motionActiveCount++;
        else if (st->motionActiveCount) st->motionActiveCount--;
    }
    if (on) mover_set(st, slot, m);
    else    mover_remove(st, slot);
}

