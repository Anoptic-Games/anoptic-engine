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
