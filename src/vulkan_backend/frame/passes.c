/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"
#include "vulkan_backend/frame/frame.h"

// Frame pass table. ORDER encodes the depth/EQUAL contract, do not reorder. Shared via frame.h.
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
        .dispatchX  = 0,  // runtime streamCount
    },
    // 1b. Per-light world-pose precompute, once per frame. Shared, before the geometry passes.
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTSETUP,
        .dispatchX  = 0,  // runtime light count
    },
    // 2. Shadow-frustum setup (light-space viewProj + planes). Shared, precedes cull.
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_SHADOWSETUP,
        .dispatchX  = 0,  // runtime shadow-frustum count
    },
    // 3. GPU culling (camera + shadow frustums, single pass)
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_CULL,
        .dispatchX  = 0,  // runtime entityCount
    },
    // 3. Clustered-forward light assignment. perView, each view bins lights into its own froxel lists.
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_LIGHTCULL,
        .dispatchX  = 0,  // runtime cluster count
        .perView    = true,
    },
    // 4a. Depth pre-pass (perView). Opaque geometry depth-only, lays down nearest depth for EQUAL shading.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 2,  // depth-only variant
        .perView                = true,
        .colorAttachmentCount   = 0,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // opaque + transmission load this depth
    },
    // 4b. Depth pre-pass, two-sided lane. Opaque doubleSided partition with cullMode NONE.
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
    // 4. Opaque geometry (perView, into HDR target + depth)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .perView                = true,
        .colorAttachmentCount   = 2,  // [0] HDR color, [1] R32_UINT picking id
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // EQUAL-test against the pre-pass depth
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,      // transmission pass loads this depth
        .depthBarrierBefore     = true,                             // wait on BOTH pre-passes' depth writes
        .resolveMode            = VK_RESOLVE_MODE_NONE,             // resolve once, in the LAST color pass (additive)
    },
    // 4c. Opaque two-sided lane. Same shading as 4 with cullMode NONE, LOADs 4's color/id.
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
    // 4d. Alpha-tested cutout lane (glTF alphaMode MASK). Draws after opaque with LESS + depth write.
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
    // 6. Additive glows (order-independent ONE/ONE). Drawn last, depth-tested no write. LAST color pass carries the view's only MSAA->HDR resolve.
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_ADDITIVE,
        .implementationIndex    = 0,  // single additive variant
        .perView                = true,
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,        // test against opaque depth (no write)
        .depthStoreOp           = VK_ATTACHMENT_STORE_OP_STORE,     // Hi-Z reduce reads this depth
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
};

const uint32_t ano_frame_pass_count = sizeof(ano_frame_passes) / sizeof(ano_frame_passes[0]);
