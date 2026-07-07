/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <anoptic_logging.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/bridge/bridge.h"

// ---------------------------------------------------------------------------
// ECS <-> render bridge consumer.
//
// Drains discrete state-transition commands from the logic thread and applies
// them to the mapped GPU buffers by render slot, propagating each across all
// MAX_FRAMES_IN_FLIGHT copies, then advances the slot quarantine and reports
// retired render_ids back. Cost is O(pending changes), never O(entities).
//
// NOTE: the slot authority is wired and live, but the legacy entities[] +
// per-frame updateCullingBuffers() path is still authoritative for the existing
// scene. With no command producer running yet this consumer drains an empty ring
// (a no-op beyond advancing the frame counter), so it is behavior-neutral. The
// cutover — making slots/slotHighWater authoritative and deleting the O(N)
// rewrite — is gated on on-hardware verification.
// ---------------------------------------------------------------------------

// Applies one resolved command's flagged fields to a single frame's buffers.
// Teleports target initialTransform (the GPU animation pass derives the live
// transform from it); mesh/material land in the cull entity SSBO; light params
// translate into GPU LightData with the driving slot as transformIndex.
// Stages a resolved CREATE/UPDATE/DESTROY command's flagged fields into THIS frame's delta
// staging (the device-local copies are uploaded by the flush in recordCommandBuffer). DESTROY
// dead-marks the entity slot so the cull pass skips it. Mirrors the field set of the former
// direct mapped writes; one upload per frame is enough since the device buffers are shared.

static void stage_command_fields(RendererState* s, const RenderCommand* c, uint32_t slot, uint32_t f)
{
    if (c->kind == RCMD_DESTROY) {
        uint32_t dead[2] = { NO_MESH_INDEX, 0u }; // dead-mark: meshIndex == NO_MESH_INDEX
        slot_upload_stage(&s->culling.entity, f, slot, dead);
        return;
    }

    uint32_t fields = (c->kind == RCMD_CREATE)
        ? (RFIELD_TRANSFORM | RFIELD_MESH_MAT | RFIELD_ANIM | RFIELD_USERDATA |
           (c->light_index != ANO_RENDER_NO_LIGHT ? RFIELD_LIGHT : 0u))
        : c->fields;

    if (fields & RFIELD_TRANSFORM) {
        slot_upload_stage(&s->initialTransformBuffer, f, slot, &c->transform);
        if (slot < s->slotMotionCap) memcpy(s->slotBasePose[slot], c->transform, sizeof(mat4));
    }
    if (fields & RFIELD_ANIM)
        slot_upload_stage(&s->motionBuffer, f, slot, &c->motion);
    if (fields & RFIELD_USERDATA)
        slot_upload_stage(&s->instanceDataBuffer, f, slot, &c->instance_data);
    if (fields & RFIELD_MESH_MAT) {
        uint32_t ent[2] = { c->mesh_index, c->material_index };
        slot_upload_stage(&s->culling.entity, f, slot, ent);
        if (slot < s->slotMotionCap) s->slotMeshIdx[slot] = c->mesh_index;
    }
    // Swept exposure: a teleport moves this slot's trajectory bound AND any caster volume the slot
    // drives; a mesh swap resizes the bound. ANIM (re)bounds via shadow_track_motion at the caller.
    if ((fields & (RFIELD_TRANSFORM | RFIELD_MESH_MAT)) && !(fields & RFIELD_ANIM))
        mover_refresh_slot(s, slot);
    if (fields & RFIELD_TRANSFORM)
        shadow_volumes_reparent(s, slot);
    // A create/update light-entity writes the STATIC palette region only; runtime lights take the
    // RCMD_LIGHT_* path. Bound the index so a stray static-region command can't clobber a runtime row.
    if ((fields & RFIELD_LIGHT) && c->light_index < ANO_STATIC_LIGHT_COUNT) {
        LightData L = {0};
        L.color[0]       = c->light.color[0];
        L.color[1]       = c->light.color[1];
        L.color[2]       = c->light.color[2];
        L.intensity      = c->light.intensity;
        L.range          = c->light.range;
        L.innerConeCos   = c->light.innerConeCos;
        L.outerConeCos   = c->light.outerConeCos;
        L.type           = (uint32_t)c->light.type;
        L.transformIndex = slot; // world pos/dir derived from this slot's live transform
        L.enabled        = 1u;
        slot_upload_stage(&s->lightBuffer, f, c->light_index, &L);
    }
}


// Stages the held streamed slice into this frame's scatter lane. The transform payload is
// NOT copied — scatter reads the producer-written ring slice directly via dynamic offset;
// only the cheap render_id -> slot resolve lands in slotMapped, and only when resolveGen
// moved (a new publish or a slot retirement). Otherwise the prior resolution, count,
// dynamic offset and frameSeq still hold, so a frame with no fresh publish re-binds the
// same slice for free (hold-last-value). curCount == 0 yields count 0 and scatter
// self-skips. frameSeq[frameIndex] records the seq this frame submits, for the reclaim.
// in:  state, frameIndex
// out: slotMapped[frameIndex], count[frameIndex], dynOffset[frameIndex], frameSeq[frameIndex]
static void stage_stream_frame(RendererState* state, uint32_t frameIndex)
{
    TransformStreamBuffer* ts = &state->transformStream;
    if (ts->stagedGen[frameIndex] == ts->resolveGen)
        return; // slot/count/offset/seq for this frame already current
    ts->stagedGen[frameIndex] = ts->resolveGen;
    ts->frameSeq[frameIndex]  = ts->curSeq;

    if (ts->curCount == 0) { ts->count[frameIndex] = 0; return; }

    uint32_t slice = (uint32_t)((ts->curSeq - 1u) % ts->ringSlices);
    const uint32_t* ids = ts->idRing + (size_t)slice * ts->capacity;
    uint32_t* slots = ts->slotMapped[frameIndex];
    for (uint32_t i = 0; i < ts->curCount; i++) {
        uint32_t slot = render_slots_resolve(&state->slots, ids[i]);
        slots[i] = (slot == ANO_RENDER_SLOT_UNMAPPED) ? STREAM_SLOT_SKIP : slot;
    }
    ts->count[frameIndex]     = ts->curCount;
    ts->dynOffset[frameIndex] = (uint32_t)((VkDeviceSize)slice * ts->sliceStride);
}

// Releases a bulk command's render-owned batch block (the single mi-heap allocation the
// bulk submit helpers pack the struct + arrays into). No-op for non-owned commands (e.g.
// init's renderHeap-resident BULK_CREATE). Render-thread only.
static void free_owned_bulk(const RenderCommand* c)
{
    if (!c->bulk_owned) return;
    void* blk = c->kind == RCMD_BULK_UPDATE  ? (void*)c->update
              : c->kind == RCMD_BULK_DESTROY ? (void*)c->destroy
              :                                (void*)c->batch;
    if (blk) mi_free(blk);
}



void render_apply_commands(RendererState* state, uint32_t frameIndex)
{
    // Drain the bridge and stage each command's changed per-slot fields into THIS frame's
    // delta staging (uploaded to the shared DEVICE_LOCAL buffers by recordCommandBuffer).
    // There is no cross-frame propagation list anymore: the authoritative per-slot buffers
    // are single copies shared by all frames, so one upload suffices. A DESTROY dead-marks
    // its slot and retires it immediately; the safeFrame = globalFrame + framesInFlight
    // quarantine then keeps the slot out of reuse until every frame that could still read the
    // old occupant has drained.
    RenderCommand cmd;
    while (ano_render_next_command(&state->bridge, &cmd)) {
        switch (cmd.kind) {
        case RCMD_STREAM_TRANSFORMS:
            // Adopt the published slice as the held snapshot; bump resolveGen so every frame
            // re-resolves it. The mapped writes preceded the producer's submit, so the slice
            // contents are visible after this drain's acquire.
            state->transformStream.curSeq   = cmd.stream_seq;
            state->transformStream.curCount = cmd.stream_count;
            state->transformStream.resolveGen++;
            break;

        case RCMD_CREATE: {
            // Grow if no recycled hole is available and the high-water is at the ceiling.
            if (state->slots.freeCount == 0u && state->slots.slotHighWater >= state->slots.slotCapacity &&
                !ensureEntityCapacity(state, state->slots.slotHighWater + 1u, frameIndex))
                break; // growth failed: drop the spawn
            uint32_t slot = render_slots_alloc(&state->slots, cmd.render_id);
            if (slot == ANO_RENDER_SLOT_UNMAPPED) break; // unexpected: drop rather than corrupt
            stage_command_fields(state, &cmd, slot, frameIndex); // stages the light photometrics if present
            shadow_track_motion(state, slot, &cmd.motion);
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
            // A create-with-light that casts gets a static-region shadow frustum (logic owns scene
            // lights now). Bounded to the static region, matching the light-staging guard above.
            if (cmd.light_index < ANO_STATIC_LIGHT_COUNT && cmd.light.castsShadow)
                register_static_shadow(state, cmd.light_index, (uint32_t)cmd.light.type, frameIndex,
                                       slot, cmd.light.range);
            break;
        }

        case RCMD_UPDATE: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);
                if (cmd.fields & RFIELD_ANIM)
                    shadow_track_motion(state, slot, &cmd.motion);
                state->shadowGlobalDirty = true; // transform/mesh/motion may move a caster (finding 8)
            }
            break;
        }

        case RCMD_DESTROY: {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);     // dead-mark
                shadow_track_motion(state, slot, NULL);                  // untrack before recycle
                state->shadowGlobalDirty = true; // caster set changed (review finding 8)
                cascade_detach_lights(state, cmd.render_id, frameIndex); // disable lights riding this slot
                render_slots_retire(&state->slots, cmd.render_id, state->globalFrame);
            }
            break;
        }

        case RCMD_BULK_CREATE: {
            const RenderCreateBatch* b = cmd.batch;
            if (!b) break;
            // alloc_range needs a contiguous run from the high-water mark.
            if (!ensureEntityCapacity(state, state->slots.slotHighWater + b->count, frameIndex)) {
                free_owned_bulk(&cmd); break; // growth failed: drop the batch
            }
            render_slots_alloc_range(&state->slots, b->render_ids, b->count);
            AnoInstanceData inert = {0};
            for (uint32_t e = 0; e < b->count; e++) {
                uint32_t slot = render_slots_resolve(&state->slots, b->render_ids[e]);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                // Mirrors before track: mover_set reads pose + mesh. Fresh (never-recycled) slots,
                // so no caster volume can reference them yet — reparent unneeded.
                if (slot < state->slotMotionCap) {
                    memcpy(state->slotBasePose[slot], &b->transforms[e], sizeof(mat4));
                    state->slotMeshIdx[slot] = b->mesh[e];
                }
                slot_upload_stage(&state->initialTransformBuffer, frameIndex, slot, &b->transforms[e]);
                slot_upload_stage(&state->motionBuffer, frameIndex, slot, &b->motion[e]);
                shadow_track_motion(state, slot, &b->motion[e]);
                // Batch carries no instance data; clear it so a recycled slot drops the prior
                // occupant's tint/flags and renders inert.
                slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &inert);
                uint32_t ent[2] = { b->mesh[e], b->material[e] };
                slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
            }
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_BULK_UPDATE: {
            // Apply the shared field mask to each resolvable target (unresolved ids dropped).
            const RenderUpdateBatch* u = cmd.update;
            if (!u) break;
            for (uint32_t e = 0; e < u->count; e++) {
                uint32_t slot = render_slots_resolve(&state->slots, u->render_ids[e]);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                // Mirrors first: the ANIM track below reads pose + mesh regardless of field order.
                if (slot < state->slotMotionCap) {
                    if (u->fields & RFIELD_TRANSFORM)
                        memcpy(state->slotBasePose[slot], &u->transforms[e], sizeof(mat4));
                    if (u->fields & RFIELD_MESH_MAT)
                        state->slotMeshIdx[slot] = u->mesh[e];
                }
                if (u->fields & RFIELD_TRANSFORM)
                    slot_upload_stage(&state->initialTransformBuffer, frameIndex, slot, &u->transforms[e]);
                if (u->fields & RFIELD_ANIM) {
                    slot_upload_stage(&state->motionBuffer, frameIndex, slot, &u->motion[e]);
                    shadow_track_motion(state, slot, &u->motion[e]);
                }
                if (u->fields & RFIELD_USERDATA)
                    slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &u->instance_data[e]);
                if (u->fields & RFIELD_MESH_MAT) {
                    uint32_t ent[2] = { u->mesh[e], u->material[e] };
                    slot_upload_stage(&state->culling.entity, frameIndex, slot, ent);
                }
                // Teleport / mesh swap upkeep, as in stage_command_fields (ANIM already re-bounded).
                if ((u->fields & (RFIELD_TRANSFORM | RFIELD_MESH_MAT)) && !(u->fields & RFIELD_ANIM))
                    mover_refresh_slot(state, slot);
                if (u->fields & RFIELD_TRANSFORM)
                    shadow_volumes_reparent(state, slot);
            }
            state->shadowGlobalDirty = true; // casters may have moved/changed (review finding 8)
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_BULK_DESTROY: {
            const RenderDestroyBatch* d = cmd.destroy;
            if (!d) break;
            uint32_t dead[2] = { NO_MESH_INDEX, 0u };
            for (uint32_t e = 0; e < d->count; e++) {
                uint32_t rid  = d->render_ids[e];
                uint32_t slot = render_slots_resolve(&state->slots, rid);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                slot_upload_stage(&state->culling.entity, frameIndex, slot, dead);
                shadow_track_motion(state, slot, NULL); // untrack before recycle
                cascade_detach_lights(state, rid, frameIndex); // disable lights riding this slot
                render_slots_retire(&state->slots, rid, state->globalFrame);
            }
            state->shadowGlobalDirty = true; // caster set changed (review finding 8)
            free_owned_bulk(&cmd);
            break;
        }

        case RCMD_LIGHT_ATTACH: {
            // Attach a runtime light to a renderable: it rides that slot's transform at light_offset.
            uint32_t parentSlot = render_slots_resolve(&state->slots, cmd.render_id);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) break; // parent not (yet) resolvable: drop
            uint32_t row = light_registry_alloc(&state->lightRegistry, cmd.light_id, cmd.render_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) break; // palette full / double-attach: drop
            uint32_t regRow = row - state->lightRegistry.base;
            LightData L = light_data_from_params(&cmd.light, parentSlot, cmd.light_offset);
            state->lightRegistry.rowMirror[regRow] = L; // seed the partial-update RMW base
            slot_upload_stage(&state->lightBuffer, frameIndex, row, &L);
            // Shadow caster: allocate a runtime frustum block if requested (and budget allows), else
            // stage non-casting info so a reused palette row never inherits a prior caster's info.
            if (cmd.light.castsShadow) {
                shadow_caster_attach(state, row, regRow, L.type, frameIndex);
            } else {
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
                state->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
            }
            break;
        }

        case RCMD_LIGHT_UPDATE: {
            uint32_t row = light_registry_resolve(&state->lightRegistry, cmd.light_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) break; // unknown light: drop
            uint32_t parentRid  = light_registry_parent_of(&state->lightRegistry, cmd.light_id);
            uint32_t parentSlot = render_slots_resolve(&state->slots, parentRid);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) break; // parent gone: drop (cascade clears it)
            // Read-modify-write the mirror: merge only the masked fields (0 == legacy full overwrite),
            // then refresh the render-derived transformIndex + enabled and re-stage the whole element.
            uint32_t fields = cmd.light_fields ? cmd.light_fields : ANO_LIGHT_FIELD_ALL;
            uint32_t regRow = row - state->lightRegistry.base;
            LightData* mir = &state->lightRegistry.rowMirror[regRow];
            uint32_t oldType = mir->type;
            light_apply_fields(mir, &cmd.light, cmd.light_offset, fields);
            mir->transformIndex = parentSlot;
            mir->enabled = 1u;
            slot_upload_stage(&state->lightBuffer, frameIndex, row, mir);
            // Shadow-caster transitions. castsShadow is preserved across a normal update; only an explicit
            // ANO_LIGHT_FIELD_CAST request (outside ALL) toggles it, so a full overwrite never silently
            // drops a caster. A live caster also re-allocates on a TYPE change (point = 6 frustums vs
            // single = 1; the stale frustumCount / cube-face fan would sample unallocated layers).
            bool isCasting   = state->lightRegistry.rowShadowBase[regRow] != ANO_SHADOW_NONE;
            bool wantCast    = (fields & ANO_LIGHT_FIELD_CAST) ? (cmd.light.castsShadow != 0u) : isCasting;
            bool typeChanged = mir->type != oldType;
            if (wantCast && (!isCasting || typeChanged)) {
                if (isCasting) shadow_caster_detach(state, regRow, frameIndex); // re-alloc for new type
                shadow_caster_attach(state, row, regRow, mir->type, frameIndex);
            } else if (!wantCast && isCasting) {
                // Toggle off while the light stays lit: free the frustum and re-stage non-casting info so
                // the fragment stops sampling the now-inactive block (detach alone leaves shadowInfo set).
                shadow_caster_detach(state, regRow, frameIndex);
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
            }
            // Changed fields on a staying caster (offset/direction/cone/range) stale its cached
            // layers (review finding 8); the attach/detach transitions above already invalidated.
            // The swept-exposure volume re-installs too (offset/range/parent may have moved it).
            shadow_layers_invalidate(state, state->lightRegistry.rowShadowBase[regRow],
                mir->type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u);
            if (state->lightRegistry.rowShadowBase[regRow] != ANO_SHADOW_NONE)
                shadow_volume_set(state, state->lightRegistry.rowShadowBase[regRow],
                    mir->type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u,
                    mir->transformIndex, mir->localOffset, mir->range);
            break;
        }

        case RCMD_LIGHT_DETACH: {
            uint32_t row = light_registry_detach(&state->lightRegistry, cmd.light_id, state->globalFrame);
            if (row != ANO_RENDER_SLOT_UNMAPPED) {
                LightData off = {0}; // enabled == 0
                slot_upload_stage(&state->lightBuffer, frameIndex, row, &off);
                shadow_caster_detach(state, row - state->lightRegistry.base, frameIndex); // free its frustum if casting
            }
            break;
        }

        case RCMD_TEXT_SET:
            // The registry adopts the packed block and frees it. NOT free_owned_bulk.
            ano_vk_text_block_set(state, cmd.text_id, cmd.text);
            break;

        case RCMD_TEXT_CLEAR:
            ano_vk_text_block_clear(state, cmd.text_id);
            break;

        default:
            break;
        }
    }

    // Free + report slots whose quarantine has elapsed (every referencing frame retired).
    uint32_t retired[64];
    uint32_t n;
    bool anyRetired = false;
    do {
        n = render_slots_collect_retired(&state->slots, state->globalFrame, retired, 64u);
        if (n) anyRetired = true;
        for (uint32_t i = 0; i < n; i++) {
            RenderEvent ev = { .kind = REVENT_SLOT_RETIRED, .u.render_id = retired[i] };
            (void)ano_render_emit_event(&state->bridge, &ev);
        }
    } while (n == 64u);
    if (anyRetired) {
        state->transformStream.resolveGen++; // a freed/recycled slot invalidates cached resolves

        // Reclaim the per-frame compute-dispatch bound: if this frame's retirements left a
        // trailing run of free slots at the top, drop slotHighWater past them so cull/update
        // stop dispatching over dead tail slots. Only fires when the free-list just changed
        // (alloc never lowers the high-water). No live slot moves; no VRAM is returned (the
        // buffers stay grown).
        render_slots_compact(&state->slots);
    }

    // Runtime light lifecycle (audit 4.7 Phase 3): return quarantine-expired light rows to the
    // free-list, then publish the cull light count = base + dynamic high-water. updateCullingBuffers
    // ran before this drain (per-frame order), so a light attached this frame is binned next frame,
    // one frame after its row data was staged + flushed — acceptable for a discrete attach/detach.
    light_registry_collect(&state->lightRegistry, state->globalFrame);
    light_registry_compact(&state->lightRegistry); // peel trailing free rows -> shrink the cull light count
    state->lightBuffer.count = state->lightRegistry.base + state->lightRegistry.highWater;

    // Stage the held streamed slice into this frame against the now-settled slot map.
    // Re-resolves only on a resolveGen bump (new publish or retirement); otherwise it
    // re-binds the same slice for free, so a streamed pose holds across ticks.
    stage_stream_frame(state, frameIndex);
}
