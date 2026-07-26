/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"

// Lock-free bridge producer.

// Valid after initVulkan() returns.
AnoRenderBridge* anoRenderBridge(void) { return &rendererState.bridge; }

// Reserve next free transform-ring slice into out.
// out: filled on success; false if slice in flight
// inv: produceSeq unchanged
bool ano_render_stream_begin(AnoStreamRegion* out) {
    TransformStreamBuffer* ts = &rendererState.transformStream;
    uint64_t seq = ts->produceSeq + 1u;
    if (seq > ts->ringSlices) {
        uint64_t prior = seq - ts->ringSlices;
        if (atomic_load_explicit(&ts->reclaimSeq, memory_order_acquire) < prior)
            return false;
    }
    uint32_t slice = (uint32_t)((seq - 1u) % ts->ringSlices);
    out->ids      = ts->idRing + (size_t)slice * ts->capacity;
    out->xforms   = ts->xformRingMapped + (size_t)slice * ts->capacity;
    out->capacity = ts->capacity;
    out->token    = seq;
    return true;
}

// Publish filled region as {seq,count}.
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
