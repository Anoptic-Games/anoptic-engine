/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <anoptic_time.h>
#include <anoptic_memory.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"

// Producer side of the lock-free bridge. Init after initVulkan; quiesce before teardown.

// Valid after initVulkan() returns.
AnoRenderBridge* anoRenderBridge(void) { return &rendererState.bridge; }
// Reserve next free transform-ring slice. False if still GPU-in-flight. Does not advance produceSeq.
// in: out; out: filled on success
bool ano_render_stream_begin(AnoStreamRegion* out) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    uint64_t seq = ts->produceSeq + 1u;
    if (seq > ts->ringSlices) {
        uint64_t prior = seq - ts->ringSlices; // seq that last used this slice
        if (atomic_load_explicit(&ts->reclaimSeq, memory_order_acquire) < prior)
            return false; // slice not yet GPU-reclaimed
    }
    uint32_t slice = (uint32_t)((seq - 1u) % ts->ringSlices);
    out->ids      = ts->idRing + (size_t)slice * ts->capacity;
    out->xforms   = ts->xformRingMapped + (size_t)slice * ts->capacity;
    out->capacity = ts->capacity;
    out->token    = seq;
    return true;
}

// Publish filled region as {seq,count}. Advances produceSeq only on enqueue success.
bool ano_render_stream_commit(const AnoStreamRegion* region, uint32_t count) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    if (count > ts->capacity) count = ts->capacity;
    RenderCommand cmd = { .kind = RCMD_STREAM_TRANSFORMS,
                          .stream_seq = region->token, .stream_count = count };
    if (!ano_render_submit(&rendererState.bridge, &cmd))
        return false;
    ts->produceSeq = region->token;
    return true;
}

// Mass field change into one render-owned block. Ring full -> free copy, false.
bool ano_render_submit_bulk_update(AnoRenderBridge* bridge, const RenderUpdateBatch* batch) {
    if (!batch || batch->count == 0) return true;
    uint32_t count = batch->count, fields = batch->fields;
    size_t bytes = sizeof(RenderUpdateBatch) + (size_t)count * sizeof(uint32_t); // struct + ids
    if (fields & RFIELD_TRANSFORM) bytes += (size_t)count * sizeof(mat4);
    if (fields & RFIELD_ANIM)      bytes += (size_t)count * sizeof(AnoMotionDescriptor);
    if (fields & RFIELD_MESH_MAT)  bytes += (size_t)count * sizeof(uint32_t) * 2u;
    if (fields & RFIELD_USERDATA)  bytes += (size_t)count * sizeof(AnoInstanceData);
    char* blk = mi_malloc(bytes);
    if (!blk) return false;
    RenderUpdateBatch* b = (RenderUpdateBatch*)blk;
    *b = (RenderUpdateBatch){ .count = count, .fields = fields };
    char* cur = blk + sizeof(RenderUpdateBatch);
    b->render_ids = (uint32_t*)cur;
    memcpy(cur, batch->render_ids, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    if (fields & RFIELD_TRANSFORM) {
        b->transforms = (mat4*)cur;
        memcpy(cur, batch->transforms, (size_t)count * sizeof(mat4)); cur += (size_t)count * sizeof(mat4);
    }
    if (fields & RFIELD_ANIM) {
        b->motion = (AnoMotionDescriptor*)cur;
        memcpy(cur, batch->motion, (size_t)count * sizeof(AnoMotionDescriptor)); cur += (size_t)count * sizeof(AnoMotionDescriptor);
    }
    if (fields & RFIELD_MESH_MAT) {
        b->mesh = (uint32_t*)cur;
        memcpy(cur, batch->mesh, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
        b->material = (uint32_t*)cur;
        memcpy(cur, batch->material, (size_t)count * sizeof(uint32_t)); cur += (size_t)count * sizeof(uint32_t);
    }
    if (fields & RFIELD_USERDATA) {
        b->instance_data = (AnoInstanceData*)cur;
        memcpy(cur, batch->instance_data, (size_t)count * sizeof(AnoInstanceData)); cur += (size_t)count * sizeof(AnoInstanceData);
    }
    RenderCommand cmd = { .kind = RCMD_BULK_UPDATE, .update = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) { mi_free(blk); return false; }
    return true;
}

// Mass despawn into one render-owned block. Same backpressure as bulk_update.
bool ano_render_submit_bulk_destroy(AnoRenderBridge* bridge, const uint32_t* render_ids, uint32_t count) {
    if (count == 0) return true;
    size_t bytes = sizeof(RenderDestroyBatch) + (size_t)count * sizeof(uint32_t);
    char* blk = mi_malloc(bytes);
    if (!blk) return false;
    RenderDestroyBatch* b = (RenderDestroyBatch*)blk;
    uint32_t* ids = (uint32_t*)(blk + sizeof(RenderDestroyBatch));
    memcpy(ids, render_ids, (size_t)count * sizeof(uint32_t));
    b->count = count;
    b->render_ids = ids;
    RenderCommand cmd = { .kind = RCMD_BULK_DESTROY, .destroy = b, .bulk_owned = true };
    if (!ano_render_submit(bridge, &cmd)) { mi_free(blk); return false; }
    return true;
}
