/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <anoptic_log.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/ui_raster.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/bridge/bridge.h"
#include "render_bridge/render_command_contract.h"

// Bridge consumer: drain commands -> GPU by slot; advance quarantine; report retired ids.

// Stage resolved CREATE/UPDATE/DESTROY flagged fields into this frame's delta staging.
// DESTROY dead-marks the entity slot so the cull pass skips it.

static void stage_command_fields(RendererState* s, const RenderCommand* c, uint32_t slot, uint32_t f)
{
    if (c->kind == RCMD_DESTROY) {
        GpuEntityInfo dead = { .meshIndex = NO_MESH_INDEX };
        slot_upload_stage(&s->culling.entity, f, slot, &dead);
        return;
    }

    uint32_t fields = c->kind == RCMD_CREATE
        ? ano::render_contract::allowed_fields(RCMD_CREATE) : c->fields;
    if (c->kind == RCMD_CREATE && c->light_index == ANO_RENDER_NO_LIGHT)
        fields &= ~RFIELD_LIGHT;

    if (fields & RFIELD_TRANSFORM) {
        slot_upload_stage(&s->initialTransformBuffer, f, slot, &c->transform);
        if (slot < s->slotMotionCap) memcpy(s->slotBasePose[slot], c->transform, sizeof(mat4));
    }
    if (fields & RFIELD_ANIM)
        slot_upload_stage(&s->motionBuffer, f, slot, &c->motion);
    if (fields & RFIELD_USERDATA)
        slot_upload_stage(&s->instanceDataBuffer, f, slot, &c->instance_data);
    if (fields & RFIELD_MESH_MAT) {
        GpuEntityInfo entity = {
            .meshIndex = c->mesh_index,
            .materialIndex = c->material_index,
        };
        slot_upload_stage(&s->culling.entity, f, slot, &entity);
        if (slot < s->slotMotionCap) s->slotMeshIdx[slot] = c->mesh_index;
    }
    // Teleport/mesh swap refreshes bound; ANIM via shadow_track_motion.
    if ((fields & (RFIELD_TRANSFORM | RFIELD_MESH_MAT)) && !(fields & RFIELD_ANIM))
        mover_refresh_slot(s, slot);
    if (fields & RFIELD_TRANSFORM)
        shadow_volumes_reparent(s, slot);
    // Static palette only; row already gated.
    if ((fields & RFIELD_LIGHT) && c->light_index < ANO_STATIC_LIGHT_COUNT) {
        // Static row: decode at slot origin; light_offset ignored.
        static const float kStaticOrigin[3] = {0};
        LightData L = light_data_from_params(&c->light, slot, kStaticOrigin);
        slot_upload_stage(&s->lightBuffer, f, c->light_index, &L);
        // Non-casting overwrite: revoke any bound static shadow block.
        if (!c->light.castsShadow) {
            unregister_static_shadow(s, c->light_index, f);
        } else if (c->kind == RCMD_UPDATE) {
            // UPDATE: log caster mismatch; never grants.
            uint32_t ownedType;
            if (!static_shadow_row_casts(s, c->light_index, &ownedType)) {
                // Never granted or grant refused.
                ano_log(ANO_ERROR, "Render bridge: static row %u raised castsShadow on UPDATE but owns "
                        "no shadow block; grants are CREATE-only, so the row stays shadowless. "
                        "Re-create it (budget permitting) to grant.", c->light_index);
            } else {
                if (ownedType != (uint32_t)c->light.type)
                    ano_log(ANO_ERROR, "Render bridge: static row %u UPDATE names light type %u, its "
                            "shadow block holds %u; re-create the row to change type.",
                            c->light_index, (uint32_t)c->light.type, ownedType);
                // Refresh owned volumes from restaged range; never grants.
                refresh_static_shadow(s, c->light_index, slot, L.range);
            }
        }
    }
}


// Stage held stream slice into scatter lane; re-resolve only on resolveGen bump.
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

// Gate LightType + static light_index on drained cmds that carry a light.
// in:  c (mutated in place)
// out: false == drop command (no batch block owned)
[[nodiscard]] static bool gate_light_domain(RenderCommand* c)
{
    const AnoRenderCommandContract *contract = ano::render_contract::commands.find(static_cast<size_t>(c->kind));
    if (!contract)
        return false;
    switch (contract->lightPolicy) {
    case AnoRenderLightPolicy::entity:
        // Refuse bad type/row; entity lands unlit (NO_LIGHT).
        if (c->light_index != ANO_RENDER_NO_LIGHT &&
            ((uint32_t)c->light.type >= LIGHT_TYPE_COUNT || c->light_index >= ANO_STATIC_LIGHT_COUNT)) {
            ano_log(ANO_ERROR, "Render bridge: light payload refused (row %u, type %u); entity lands unlit.",
                    c->light_index, (uint32_t)c->light.type);
            c->light_index = ANO_RENDER_NO_LIGHT;
        }
        return true;
    case AnoRenderLightPolicy::attach:
        return (uint32_t)c->light.type < LIGHT_TYPE_COUNT; // no light to attach: drop
    case AnoRenderLightPolicy::update: {
        // Partial updates carry a type only when the mask names it (0 == ALL).
        uint32_t fields = c->light_fields ? c->light_fields : ANO_LIGHT_FIELD_ALL;
        return !(fields & ANO_LIGHT_FIELD_TYPE) || (uint32_t)c->light.type < LIGHT_TYPE_COUNT;
    }
    case AnoRenderLightPolicy::none:
        return true;
    }
    return false;
}


void render_apply_commands(RendererState* state, uint32_t frameIndex)
{
    // Drain bridge -> delta staging. DESTROY dead-marks + retires (quarantine until drained).
    RenderCommand cmd;
    while (ano_render_next_command(&state->bridge, &cmd)) {
        // Refuse live-id CREATE before gate/grow; owns no block.
        if (cmd.kind == RCMD_CREATE &&
            render_slots_resolve(&state->slots, cmd.render_id) != ANO_RENDER_SLOT_UNMAPPED) {
            ano_log(ANO_ERROR, "Render bridge: CREATE names live render_id %u; command dropped "
                    "(destroy it first to re-create).", cmd.render_id);
            continue;
        }
        if (!gate_light_domain(&cmd)) continue; // light type/row outside its domain: refuse the command
        (void)ano::render_contract::visit_command(
            cmd.kind, [&]<RenderCommandKind Kind> {
        if constexpr (Kind == RCMD_STREAM_TRANSFORMS) {
            // Adopt published slice; bump resolveGen (scatter re-resolves when stagedGen lags).
            state->transformStream.curSeq   = cmd.stream_seq;
            state->transformStream.curCount = cmd.stream_count;
            state->transformStream.resolveGen++;
            return;

        } else if constexpr (Kind == RCMD_CREATE) {
            // Live id already refused at the drain head, so the id is unmapped here.
            // Grow if no recycled hole is available and the high-water is at the ceiling.
            if (state->slots.freeCount == 0u && state->slots.slotHighWater >= state->slots.slotCapacity &&
                !ensureEntityCapacity(state, state->slots.slotHighWater + 1u, frameIndex))
                return; // growth failed: drop the spawn
            uint32_t slot = render_slots_alloc(&state->slots, cmd.render_id);
            if (slot == ANO_RENDER_SLOT_UNMAPPED) return; // capacity/OOM/out-of-domain id: drop
            stage_command_fields(state, &cmd, slot, frameIndex); // stages the light photometrics if present
            shadow_track_motion(state, slot, &cmd.motion);
            state->shadowGlobalDirty = true; // caster set changed
            // A create-with-light that casts gets a static-region shadow frustum.
            if (cmd.light_index < ANO_STATIC_LIGHT_COUNT && cmd.light.castsShadow)
                register_static_shadow(state, cmd.light_index, (uint32_t)cmd.light.type, frameIndex,
                                       slot, cmd.light.range);
            return;

        } else if constexpr (Kind == RCMD_UPDATE) {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);
                if (cmd.fields & RFIELD_ANIM)
                    shadow_track_motion(state, slot, &cmd.motion);
                state->shadowGlobalDirty = true; // transform/mesh/motion may move a caster
            }
            return;

        } else if constexpr (Kind == RCMD_DESTROY) {
            uint32_t slot = render_slots_resolve(&state->slots, cmd.render_id);
            if (slot != ANO_RENDER_SLOT_UNMAPPED) {
                stage_command_fields(state, &cmd, slot, frameIndex);     // dead-mark
                shadow_track_motion(state, slot, NULL);                  // untrack before recycle
                state->shadowGlobalDirty = true; // caster set changed
                cascade_detach_lights(state, cmd.render_id, frameIndex); // disable lights riding this slot
                render_slots_retire(&state->slots, cmd.render_id, state->globalFrame);
            }
            return;

        } else if constexpr (Kind == RCMD_BULK_CREATE) {
            const RenderCreateBatch* b = cmd.batch;
            // Empty batch: release and bail before alloc_range logs.
            if (!b || b->count == 0u) { ano_render_command_release(&cmd); return; }
            // alloc_range needs a contiguous run from the high-water mark.
            if (!ensureEntityCapacity(state, state->slots.slotHighWater + b->count, frameIndex)) {
                ano_render_command_release(&cmd); return; // growth failed: drop the batch
            }
            // alloc_range refuses live/dup/cap/OOM atomically.
            uint32_t base = render_slots_alloc_range(&state->slots, b->render_ids, b->count);
            if (base == ANO_RENDER_SLOT_UNMAPPED) {
                ano_log(ANO_ERROR, "Render bridge: BULK_CREATE refused (live id, duplicate in batch, "
                        "capacity, or OOM); batch dropped.");
                ano_render_command_release(&cmd);
                return;
            }
            AnoInstanceData inert = {0};
            for (uint32_t e = 0; e < b->count; e++) {
                uint32_t slot = base + e; // alloc_range published render_ids[e] -> base + e
                // Mirror pose + mesh before track; fresh slots need no reparent.
                if (slot < state->slotMotionCap) {
                    memcpy(state->slotBasePose[slot], &b->transforms[e], sizeof(mat4));
                    state->slotMeshIdx[slot] = b->mesh[e];
                }
                slot_upload_stage(&state->initialTransformBuffer, frameIndex, slot, &b->transforms[e]);
                slot_upload_stage(&state->motionBuffer, frameIndex, slot, &b->motion[e]);
                shadow_track_motion(state, slot, &b->motion[e]);
                // No instance data: clear so recycled slot renders inert.
                slot_upload_stage(&state->instanceDataBuffer, frameIndex, slot, &inert);
                GpuEntityInfo entity = {
                    .meshIndex = b->mesh[e],
                    .materialIndex = b->material[e],
                };
                slot_upload_stage(&state->culling.entity, frameIndex, slot, &entity);
            }
            state->shadowGlobalDirty = true; // caster set changed
            ano_render_command_release(&cmd);
            return;

        } else if constexpr (Kind == RCMD_BULK_UPDATE) {
            // Apply shared field mask to resolvable targets.
            const RenderUpdateBatch* u = cmd.update;
            if (!u) return;
            for (uint32_t e = 0; e < u->count; e++) {
                uint32_t slot = render_slots_resolve(&state->slots, u->render_ids[e]);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                // Mirror pose + mesh first; the ANIM track reads them.
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
                    GpuEntityInfo entity = {
                        .meshIndex = u->mesh[e],
                        .materialIndex = u->material[e],
                    };
                    slot_upload_stage(&state->culling.entity, frameIndex, slot, &entity);
                }
                // Teleport / mesh swap upkeep, as in stage_command_fields.
                if ((u->fields & (RFIELD_TRANSFORM | RFIELD_MESH_MAT)) && !(u->fields & RFIELD_ANIM))
                    mover_refresh_slot(state, slot);
                if (u->fields & RFIELD_TRANSFORM)
                    shadow_volumes_reparent(state, slot);
            }
            state->shadowGlobalDirty = true; // casters may have moved/changed
            ano_render_command_release(&cmd);
            return;

        } else if constexpr (Kind == RCMD_BULK_DESTROY) {
            const RenderDestroyBatch* d = cmd.destroy;
            if (!d) return;
            GpuEntityInfo dead = { .meshIndex = NO_MESH_INDEX };
            for (uint32_t e = 0; e < d->count; e++) {
                uint32_t rid  = d->render_ids[e];
                uint32_t slot = render_slots_resolve(&state->slots, rid);
                if (slot == ANO_RENDER_SLOT_UNMAPPED) continue;
                slot_upload_stage(&state->culling.entity, frameIndex, slot, &dead);
                shadow_track_motion(state, slot, NULL); // untrack before recycle
                cascade_detach_lights(state, rid, frameIndex); // disable lights riding this slot
                render_slots_retire(&state->slots, rid, state->globalFrame);
            }
            state->shadowGlobalDirty = true; // caster set changed
            ano_render_command_release(&cmd);
            return;

        } else if constexpr (Kind == RCMD_LIGHT_ATTACH) {
            // Attach runtime light to renderable (slot transform + light_offset).
            uint32_t parentSlot = render_slots_resolve(&state->slots, cmd.render_id);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) return; // parent not (yet) resolvable: drop
            uint32_t row = light_registry_alloc(&state->lightRegistry, cmd.light_id, cmd.render_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) return; // palette full / double-attach: drop
            uint32_t regRow = row - state->lightRegistry.base;
            LightData L = light_data_from_params(&cmd.light, parentSlot, cmd.light_offset);
            state->lightRegistry.rowMirror[regRow] = L; // seed the partial-update RMW base
            slot_upload_stage(&state->lightBuffer, frameIndex, row, &L);
            // Alloc runtime frustum if casting; else stage non-casting info.
            if (cmd.light.castsShadow) {
                shadow_caster_attach(state, row, regRow, L.type, frameIndex);
            } else {
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
                state->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
            }
            return;

        } else if constexpr (Kind == RCMD_LIGHT_UPDATE) {
            uint32_t row = light_registry_resolve(&state->lightRegistry, cmd.light_id);
            if (row == ANO_RENDER_SLOT_UNMAPPED) return; // unknown light: drop
            uint32_t parentRid  = light_registry_parent_of(&state->lightRegistry, cmd.light_id);
            uint32_t parentSlot = render_slots_resolve(&state->slots, parentRid);
            if (parentSlot == ANO_RENDER_SLOT_UNMAPPED) return; // parent gone: drop
            // RMW mirror: merge fields, refresh transformIndex+enabled, re-stage.
            uint32_t fields = cmd.light_fields ? cmd.light_fields : ANO_LIGHT_FIELD_ALL;
            uint32_t regRow = row - state->lightRegistry.base;
            LightData* mir = &state->lightRegistry.rowMirror[regRow];
            uint32_t oldType = mir->type;
            light_apply_fields(mir, &cmd.light, cmd.light_offset, fields);
            mir->transformIndex = parentSlot;
            mir->enabled = 1u;
            slot_upload_stage(&state->lightBuffer, frameIndex, row, mir);
            // Caster transitions: CAST toggles; TYPE change re-allocates.
            bool isCasting   = state->lightRegistry.rowShadowBase[regRow] != ANO_SHADOW_NONE;
            bool wantCast    = (fields & ANO_LIGHT_FIELD_CAST) ? (cmd.light.castsShadow != 0u) : isCasting;
            bool typeChanged = mir->type != oldType;
            if (wantCast && (!isCasting || typeChanged)) {
                if (isCasting) shadow_caster_detach(state, regRow, frameIndex); // re-alloc for new type
                shadow_caster_attach(state, row, regRow, mir->type, frameIndex);
            } else if (!wantCast && isCasting) {
                // Toggle off while lit: free the frustum and re-stage non-casting info.
                shadow_caster_detach(state, regRow, frameIndex);
                ShadowLightInfo si = {0}; // castsShadow == 0
                slot_upload_stage(&state->shadowInfo, frameIndex, row, &si);
            }
            // Staying caster field change: stale cache layers + reinstall volume.
            shadow_layers_invalidate(state, state->lightRegistry.rowShadowBase[regRow],
                mir->type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u);
            if (state->lightRegistry.rowShadowBase[regRow] != ANO_SHADOW_NONE)
                shadow_volume_set(state, state->lightRegistry.rowShadowBase[regRow],
                    mir->type == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u,
                    mir->transformIndex, mir->localOffset, mir->range);
            return;

        } else if constexpr (Kind == RCMD_LIGHT_DETACH) {
            uint32_t row = light_registry_detach(&state->lightRegistry, cmd.light_id, state->globalFrame);
            if (row != ANO_RENDER_SLOT_UNMAPPED) {
                LightData off = {0}; // enabled == 0
                slot_upload_stage(&state->lightBuffer, frameIndex, row, &off);
                shadow_caster_detach(state, row - state->lightRegistry.base, frameIndex); // free its frustum if casting
            }
            return;

        } else if constexpr (Kind == RCMD_TEXT_SET) {
            // The registry adopts the packed block and frees it. NOT ano_render_command_release.
            ano_vk_text_block_set(state, cmd.text_id, cmd.text);
            return;

        } else if constexpr (Kind == RCMD_TEXT_CLEAR) {
            ano_vk_text_block_clear(state, cmd.text_id);
            return;

        } else if constexpr (Kind == RCMD_UI_SET) {
            // Same adoption contract as text blocks.
            ano_vk_ui_block_set(state, cmd.ui_id, cmd.ui);
            return;

        } else if constexpr (Kind == RCMD_UI_CLEAR) {
            ano_vk_ui_block_clear(state, cmd.ui_id);
            return;
        } else {
            static_assert(ano::dependent_false<Kind>, "unhandled render command");
        }
        });
    }

    // Free + report slots whose quarantine has elapsed (every referencing frame retired).
    uint32_t retired[64];
    uint32_t n;
    bool anyRetired = false;
    do {
        n = render_slots_collect_retired(&state->slots, state->globalFrame, retired, 64u);
        if (n) anyRetired = true;
        for (uint32_t i = 0; i < n; i++) {
            RenderEvent ev = { .kind = REVENT_SLOT_RETIRED };
            ev.u.render_id = retired[i];
            (void)ano_render_emit_event(&state->bridge, &ev);
        }
    } while (n == 64u);
    if (anyRetired) {
        state->transformStream.resolveGen++; // a freed/recycled slot invalidates cached resolves

        // Compact slotHighWater past trailing free slots so cull/update skip dead tail.
        render_slots_compact(&state->slots);
    }

    // Return expired light rows to free-list; cull light count = base + high-water.
    light_registry_collect(&state->lightRegistry, state->globalFrame);
    light_registry_compact(&state->lightRegistry); // peel trailing free rows -> shrink the cull light count
    state->lightBuffer.count = state->lightRegistry.base + state->lightRegistry.highWater;

    // Stage the held streamed slice into this frame against the now-settled slot map.
    stage_stream_frame(state, frameIndex);
}
