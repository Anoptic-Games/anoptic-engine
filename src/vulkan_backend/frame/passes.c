/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/frame/frame.h"

// The frame pass table. ONE array; its ORDER encodes the depth/EQUAL contract (4a prepass CLEAR ->
// 4b two-sided LOAD -> 4 opaque EQUAL -> 4c two-sided opaque -> 4d masked LESS+write -> 5 transmission
// -> 6 additive, resolve only in the last). Do not reorder. Consumed by the shared-compute loop
// (record.c) and the per-view geometry loop (record_views.c). Renamed from the former static
// g_framePasses so both TUs share it via frame.h.
const RenderPassDef ano_frame_passes[] = {
    // 0. GPU animation update
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_UPDATE,
        .dispatchX  = 0,
    },
    // 1. Streamed-transform scatter (overwrites ANO_MOTION_STREAMED slots with CPU data)
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_SCATTER,
        .dispatchX  = 0,  // computed from streamCount at runtime
    },
    // 1b. Per-light world-pose precompute: resolve each light's world position + forward from its (now
    //     final) driving transform ONCE per frame, so the fragment passes stop reloading the 64B mat4
    //     and re-deriving lightPos/lightForward per fragment. Shared (not per view); after update+scatter
    //     finalize transforms, before the geometry passes that read the pose (set 0 binding 12).
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTSETUP,
        .dispatchX  = 0,  // computed from light count at runtime
    },
    // 2. Shadow-frustum setup: build each shadow map's light-space viewProj + planes from the
    //    light's live transform. Shared (not per view); must precede cull (which tests them).
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_SHADOWSETUP,
        .dispatchX  = 0,  // computed from shadow-frustum count at runtime
    },
    // 3. GPU culling (camera + shadow frustums, single pass)
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_CULL,
        .dispatchX  = 0,  // computed from entityCount at runtime
    },
    // 3. Clustered-forward light assignment (froxel light lists for the fragment passes).
    //    perView: each view bins lights against its own frustum into its own froxel lists.
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTCULL,
        .dispatchX  = 0,  // computed from cluster count at runtime
        .perView    = true,
    },
    // 4a. Depth pre-pass (perView): the opaque geometry rendered depth-only (fragment stage stripped,
    //     no color) to lay down the nearest depth. The opaque pass below then shades each visible pixel
    //     exactly once via an EQUAL test, removing overdraw of the heavy lighting shader (the frame's
    //     dominant cost). Same FLAT draw partition + geometry module -> bit-identical depth for EQUAL.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 2,  // depth-only variant
        .perView                = true,
        .colorAttachmentCount   = 0,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // opaque + transmission load this depth
    },
    // 4b. Depth pre-pass, two-sided lane (review finding 7): the opaque doubleSided partition laid
    //     down with cullMode NONE. LOADs 4a's depth (which CLEARed); the barrier orders 4a's writes
    //     under this pass's LESS test.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex    = 2,  // depth-only variant
        .perView                = true,
        .colorAttachmentCount   = 0,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,
        .depthBarrierBefore     = true,                             // wait on 4a's depth writes
    },
    // 4. Opaque geometry (perView: rendered once per view into that view's HDR target + depth)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 2,  // [0] HDR color, [1] R32_UINT picking id (audit 3.1)
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // EQUAL-test against the pre-pass depth
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // transmission pass loads this depth
        .depthBarrierBefore     = true,                             // wait on BOTH pre-passes' depth writes
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 4c. Opaque two-sided lane (review finding 7): same shading as 4 with cullMode NONE, LOADing
    //     4's color/id (4 CLEARed; its depth barrier already ordered both pre-passes, and EQUAL
    //     assigns each pixel to exactly one lane). Both opaque passes resolve the picking id; the
    //     LAST resolve — this one — is what ano_collect_pick reads, so both lanes stay pickable.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 2,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 4d. Alpha-tested cutout lane (glTF alphaMode MASK: foliage, chains). No pre-pass — a
    //     fragment-less pre-pass can't discard, and EQUAL against its solid-quad depth would hole
    //     the background — so this lane draws AFTER the opaque lanes with LESS + depth WRITE
    //     (correctly occluded by opaque, correctly occluding the transparent lanes below), the
    //     alpha discard + alpha-to-coverage shaping its coverage. Both color attachments load
    //     (HDR + picking id: cutout survivors are pickable by silhouette). The depth barrier
    //     orders the pre-passes' writes (RAW) and the opaque lanes' EQUAL reads (WAR) under this
    //     pass's depth test+write.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT_MASKED,
        .implementationIndex    = 0,  // masked variant (LESS + write + A2C)
        .perView                = true,
        .colorAttachmentCount   = 2,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,
        .depthBarrierBefore     = true,
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 5. Transmissive geometry (depth-sorted "over" lane)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_TRANSMISSION,
        .implementationIndex    = 1,  // blended transmission variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // additive pass below loads this depth
        .depthBarrierBefore     = true,                             // wait on the masked lane's depth writes
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 6. Additive glows (order-independent ONE/ONE). Drawn last so glows composite on top; depth-
    //    tested against opaque depth (hidden behind solids) but no depth write, so layers all add.
    //    As the LAST color pass it carries the view's ONLY MSAA->HDR resolve (review finding 5):
    //    the MSAA surface persists across opaque/transmission/additive (LOAD + STORE), so
    //    resolving in every pass just rewrote the same texels three times.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_ADDITIVE,
        .implementationIndex    = 0,  // single additive variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,     // Hi-Z reduce (review 4.9 step 3) reads this depth
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
};

const uint32_t ano_frame_pass_count = sizeof(ano_frame_passes) / sizeof(ano_frame_passes[0]);
