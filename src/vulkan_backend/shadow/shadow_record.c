/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/shadow/shadow.h"

// Whether a light type's direct occlusion is shadow-mapped (vs carried by the radiance cascade
// field) under the given AnoLightingMode. Mirrors lightUsesShadowMap() in flat.frag /
// transmission.frag — the shadow depth render (caster geometry) and the fragment PCF sample MUST
// agree, or a gated-off atlas layer gets sampled stale. HYBRID keeps directional + spot maps and
// routes point lights to RC; RC drops all shadow maps; SHADOWMAP keeps all (current behavior).
static inline bool lightTypeShadowMapped(uint32_t lightType, uint32_t mode)
{
    if (mode == ANO_LIGHTING_SHADOWMAP) return true;
    if (mode == ANO_LIGHTING_RC)        return false;
    return lightType != (uint32_t)LIGHT_TYPE_POINT; // ANO_LIGHTING_HYBRID
}

// Layered Power CDF shadow render + separable prefilter (the shadow region of the record path):
// classify + budget the active/dirty frustums, per-frustum depth render into the atlas sublayers,
// then the two blur phases; four phase barriers. Extracted verbatim; runs between the swapchain
// acquire barrier and the transparency sort in recordCommandBuffer.
void ano_shadow_record(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount)
{
    // === Layered Power CDF shadow render + separable prefilter ===
    // Three phases: (1) render each active frustum's nearest occluder as a one-hot (coverage=1, M=z)
    // in its depth band, MRT into the frustum's two atlas sublayers, depth-tested against the
    // frustum's own transient depth slice; (2) box blur-X atlas -> temp; (3) blur-Y temp -> atlas.
    // The lighting frags reconstruct occlusion as cumulative per-band coverage. Per frustum s the two
    // sublayers are the contiguous atlas layers [s*SUBLAYERS, s*SUBLAYERS+SUBLAYERS). (audit 4.7)
    //
    // Synchronization is per PHASE, not per layer: a barrier's execution scope is stage-global (not
    // scoped to the image it names), so the old per-layer barriers serialized all ~130 micro-passes
    // end-to-end. Passes within a phase touch disjoint subresources (own sublayers, own depth slice)
    // and need no mutual ordering; four whole-array barriers fence the phase boundaries. Command-
    // buffer state (pipeline, viewport, sets) persists across rendering instances: bound once per
    // phase, not per pass.
    if (entityCount > 0) {
        ShadowResources* sh = &rendererState.frames[rendererState.frameIndex].shadow;
        bool useMeshS = ctx.deviceCapabilities.meshShader;
        uint32_t maxDrawsS = rendererState.indirectBuffer.capacity;
        // Must equal the pipeline layout's push range flags exactly (task stage reads the same push).
        VkShaderStageFlags pcStageS = (useMeshS ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
            | VK_SHADER_STAGE_FRAGMENT_BIT // shadow_depth.frag reads shadowFrustumIndex (CDF depth linearization)
            | (rendererState.taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
        const ShadowFrustumConfig* shadowCfgs = rendererState.shadowCfgMirror; // render-thread mirror (device copy not host-readable)

        VkViewport shVp = { 0.0f, 0.0f, (float)ANO_SHADOW_DIM, (float)ANO_SHADOW_DIM, 0.0f, 1.0f };
        VkRect2D   shSc = { .offset = {0, 0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } };
        VkClearValue clearStats = {}; // all bands empty: coverage 0 (no occluder in this texel) -> lit
        VkClearValue clearDepth = {}; clearDepth.depthStencil.depth = 1.0f;
        VkClearValue clearMRT[2] = { clearStats, clearStats }; // both sublayers

        // Frustums rendered this frame (review finding 8): active (spare/runtime-freed slots skip),
        // shadow-mapped under the lighting mode (a layer carried by radiance cascades renders no
        // caster), and DIRTY — the shared atlas persists per-frustum content, so a clean frustum
        // skips its depth render + blur and its layers just ride the whole-array transitions
        // (content-preserving). Dirty = layer invalid (never built, or its light attached/detached/
        // changed — scoped hooks), a conservative epoch (scene mutation staged this frame, streamed
        // transforms, or a mover with no finite trajectory bound), or SWEPT MOTION EXPOSURE: a live
        // parametric mover whose whole-trajectory sphere reaches this frustum's light volume, or a
        // light that itself rides a mover, re-renders that frustum every frame — movers away from a
        // light no longer dirty it. A clean layer stays consistent with this frame's frustumBuffer
        // because its light is unchanged: shadowsetup rewrote the identical viewProj. Cached layers
        // keep their render-time LOD (camera-driven LOD drift does not invalidate; bounded
        // silhouette staleness). Freeze mode renders only never-built layers (the static-scene
        // ceiling); force-dirty pins the pre-cache behavior. When nothing renders, the whole region
        // — all four phase barriers included — is skipped: the atlas rests in SHADER_READ.
        // maxSub bounds the layered blur's layerCount to the rendered prefix.
        bool epochDirty = rendererState.shadowCacheMode == 1u
                       || (rendererState.shadowCacheMode == 0u
                           && (rendererState.shadowGlobalDirty
                               || rendererState.moverUnboundedCount > 0u
                               || (rendererState.sweptPoisoned && rendererState.motionActiveCount > 0u)
                               || rendererState.transformStream.count[rendererState.frameIndex] > 0u));
        if (epochDirty)
            for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) rendererState.shadowLayerValid[s] = false;
        rendererState.shadowGlobalDirty = false;

        // Pass 1 — classify. Matrix-dirty (light attached/changed/teleported, or riding a mover:
        // shadowsetup rewrites its viewProj from live state) renders unconditionally — deferral
        // would sample old content with a new matrix. Content-dirty with a stable light (mover
        // exposure, mutation epochs) keeps an identical matrix across frames, so it is safely
        // deferrable and budget-eligible. Force/freeze modes bypass the budget entirely.
        bool renderS[ANO_SHADOW_FRUSTUM_COUNT];
        bool candidate[ANO_SHADOW_FRUSTUM_COUNT];
        uint32_t renderCount = 0u, maxSub = 0u, candCount = 0u;
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            bool active = shadowCfgs[s].active && lightTypeShadowMapped(shadowCfgs[s].lightType, rendererState.lightingMode);
            renderS[s] = false; candidate[s] = false;
            if (!active) continue;
            if (rendererState.shadowCacheMode != 0u) { renderS[s] = !rendererState.shadowLayerValid[s]; continue; }
            const ShadowCasterVolume* v = &rendererState.shadowVolume[s];
            bool parentMover  = v->parentSlot != ANO_RENDER_SLOT_UNMAPPED
                             && v->parentSlot < rendererState.slotMotionCap
                             && rendererState.slotMotion[v->parentSlot];
            bool matrixDirty  = rendererState.shadowMatrixDirty[s] || parentMover;
            bool contentDirty = !rendererState.shadowLayerValid[s] || rendererState.shadowExposed[s] > 0u;
            if (matrixDirty)       renderS[s] = true;
            else if (contentDirty) { candidate[s] = true; candCount++; }
        }
        // Pass 2 — admit content-dirty candidates oldest-first up to the budget (0 = unlimited;
        // the budget is a content-refresh throttle on TOP of the mandatory renders, so a scene
        // full of moving lights can never starve caster-shadow refresh). Equal stamps round-robin
        // naturally: an admitted frustum's fresh stamp loses the next frame's scan. Deferral needs
        // no retry bookkeeping — the valid flag stays false and exposure counts persist.
        uint32_t budget = rendererState.shadowRenderBudget;
        uint32_t admit = (budget == 0u || budget > candCount) ? candCount : budget;
        for (uint32_t k = 0; k < admit; k++) {
            uint32_t best = ANO_SHADOW_FRUSTUM_COUNT;
            for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
                if (candidate[s] && (best == ANO_SHADOW_FRUSTUM_COUNT
                                     || rendererState.shadowLastRendered[s] < rendererState.shadowLastRendered[best]))
                    best = s;
            if (best == ANO_SHADOW_FRUSTUM_COUNT) break;
            candidate[best] = false;
            renderS[best] = true;
        }
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
            if (renderS[s]) { renderCount++; maxSub = (s + 1u) * ANO_SHADOW_ATLAS_SUBLAYERS; }
        g_shadowRenderAccum += renderCount;
        g_shadowRenderFrames++;

        // Phase barrier 1: atlas SHADER_READ -> COLOR (whole array, CONTENT PRESERVED — clean
        // frustums keep their layers; dirty ones re-render with loadOp CLEAR), temp UNDEFINED ->
        // COLOR (blur intermediate, content never crosses frames), transient depth UNDEFINED ->
        // DEPTH_ATTACHMENT (whole array). All three are shared across frames in flight: the
        // FRAGMENT source scope orders prior in-flight frames' atlas/temp reads (lighting frags,
        // blur) and EARLY|LATE the prior depth-slice use — the cross-frame WARs the per-frame
        // fence doesn't cover (same pattern as the Hi-Z pyramid rewrite below).
        if (renderCount > 0u) {
            VkImageMemoryBarrier pre[3];
            pre[0] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowAtlasImage, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            pre[1] = pre[0];
            pre[1].image = rendererState.shadowTempImage;
            pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // discard: repopulated by blur-X each use
            pre[2] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowDepthImage,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, ANO_SHADOW_FRUSTUM_COUNT } };
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, NULL, 0, NULL, 3, pre);
        }

        // --- Phase 1: per-frustum depth render (MRT into the frustum's two sublayers) ---
        // Disjoint targets across frustums: no inter-pass barriers, the renders may overlap.
        if (renderCount > 0u) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowPipeline);
            vkCmdSetViewport(cmd, 0, 1, &shVp);
            vkCmdSetScissor(cmd, 0, 1, &shSc);

            // Shadow pipeline reuses the FLAT layout (sets 0/1/2). Set 0 = view 0's global set; set 2 =
            // the shadow set (viewProjs). Bindless (set 1) is unused here but bound for compatibility.
            VkPipelineLayout flatLayout = rendererState.prototypes[PIPELINE_FLAT].layout;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 0, 1,
                &rendererState.frames[rendererState.frameIndex].views[0].globalSet, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 1, 1,
                &rendererState.bindlessTextures.set, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flatLayout, 2, 1,
                &sh->geomSet, 0, NULL);

            VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
            VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
            if (!useMeshS)
                vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                if (!renderS[s]) continue;
                uint32_t subBase = s * ANO_SHADOW_ATLAS_SUBLAYERS; // first of the frustum's 2 contiguous sublayers

                VkRenderingAttachmentInfo colorAtt[ANO_SHADOW_ATLAS_SUBLAYERS];
                for (uint32_t a = 0; a < ANO_SHADOW_ATLAS_SUBLAYERS; a++)
                    colorAtt[a] = (VkRenderingAttachmentInfo){ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = rendererState.shadowAtlasLayerView[subBase + a], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clearMRT[a] };
                VkRenderingAttachmentInfo depthAtt = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = rendererState.shadowDepthSliceView[s], .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .clearValue = clearDepth };
                VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                    .layerCount = 1, .colorAttachmentCount = ANO_SHADOW_ATLAS_SUBLAYERS, .pColorAttachments = colorAtt, .pDepthAttachment = &depthAtt };
                vkCmdBeginRendering(cmd, &ri);

                // Two caster partitions per frustum: solid at base + s, alpha-tested MASKED at
                // base + FRUSTUM_COUNT + s — drawn back-to-back into the same rendering instance
                // (shared depth slice keeps nearest-occluder selection across both classes), each
                // with its own pipeline (masked = uv+material geometry + alpha discard).
                uint32_t shadowBase = ANO_VIEW_COUNT * drawSlotCount;
                uint32_t partitions[2] = { shadowBase + s, shadowBase + ANO_SHADOW_FRUSTUM_COUNT + s };
                for (uint32_t m = 0; m < 2u; m++) {
                    uint32_t partition = partitions[m];
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        m == 0u ? rendererState.shadowPipeline : rendererState.shadowPipelineMasked);
                    uint32_t pcVals[2] = { partition * rendererState.culling.maxEntities, s }; // baseOffset, shadowFrustumIndex
                    vkCmdPushConstants(cmd, flatLayout, pcStageS, 0, sizeof(pcVals), pcVals);

                    VkDeviceSize countOffset = (VkDeviceSize)partition * sizeof(uint32_t);
                    if (useMeshS) {
                        VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                        if (ctx.deviceCapabilities.drawIndirectCount)
                            pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                        else
                            pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    } else {
                        VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDrawsS * sizeof(VkDrawIndexedIndirectCommand);
                        if (ctx.deviceCapabilities.drawIndirectCount)
                            vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset, drawCountBuf, countOffset, maxDrawsS, sizeof(VkDrawIndexedIndirectCommand));
                        else
                            vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset, entityCount, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }
                vkCmdEndRendering(cmd);
            }
        }

        // Phase barrier 2: whole atlas COLOR -> SHADER_READ for the blur-X sample — the only
        // ordering the depth renders need. Clean/inactive layers ride along (content preserved) so
        // the sampled array view sees one uniform layout.
        if (renderCount > 0u) {
            VkImageMemoryBarrier toRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &toRead);
        }

        // --- Phases 2 & 3: separable box blur (atlas -> temp -> atlas) over active sublayers ---
        // Every channel (per-band coverage / coverage*mean) is linearly filterable, so the 2D
        // footprint reduces to two 1D box passes exactly; the coverage gradient across a silhouette
        // is the penumbra. One layered render pass per direction when the device has vertex-stage
        // gl_Layer (shadowblur.vert routes the push-constant layer); else one single-layer pass per
        // active sublayer. Both paths are barrier-free within a direction: target layers are
        // disjoint and the common source is read-only.
        bool layeredBlur = ctx.deviceCapabilities.shaderOutputLayer;
        struct { float dir[2]; int32_t layer; int32_t pad; } bpc = {0};
        float invDim = 1.0f / (float)ANO_SHADOW_DIM;
        // The push spans both stages: shadowblur.vert reads layer, shadowblur.frag reads dir+layer
        // (the fallback vertex stage reads nothing; the range still covers it).
        const VkShaderStageFlags blurPcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        for (int pass = 0; renderCount > 0u && pass < 2; pass++) {
            VkImageView     dstArr = pass == 0 ? rendererState.shadowTempArrayView : rendererState.shadowAtlasArrayView;
            VkImageView*    dstLyr = pass == 0 ? rendererState.shadowTempLayerView : rendererState.shadowAtlasLayerView;
            VkDescriptorSet srcSet = pass == 0 ? sh->blurAtlasSet : sh->blurTempSet; // src = atlas (X) / temp (Y)
            bpc.dir[0] = pass == 0 ? invDim : 0.0f;
            bpc.dir[1] = pass == 0 ? 0.0f   : invDim;

            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowBlurPipeline);
                vkCmdSetViewport(cmd, 0, 1, &shVp);
                vkCmdSetScissor(cmd, 0, 1, &shSc);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.shadowBlurLayout,
                    0, 1, &srcSet, 0, NULL);

                if (layeredBlur) {
                    // One pass over the array prefix [0, maxSub); each draw routes to its sublayer
                    // via gl_Layer. Undrawn (inactive) layers load DONT_CARE and are never sampled.
                    VkRenderingAttachmentInfo bColor = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = dstArr, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
                    VkRenderingInfo bri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                        .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                        .layerCount = maxSub, .colorAttachmentCount = 1, .pColorAttachments = &bColor };
                    vkCmdBeginRendering(cmd, &bri);
                    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                        if (!renderS[s]) continue;
                        for (uint32_t sub = 0; sub < ANO_SHADOW_ATLAS_SUBLAYERS; sub++) {
                            bpc.layer = (int32_t)(s * ANO_SHADOW_ATLAS_SUBLAYERS + sub);
                            vkCmdPushConstants(cmd, rendererState.shadowBlurLayout, blurPcStages, 0, sizeof(bpc), &bpc);
                            vkCmdDraw(cmd, 3, 1, 0, 0);
                        }
                    }
                    vkCmdEndRendering(cmd);
                } else {
                    // Fallback: one single-layer pass per rendered sublayer, still no interleaved
                    // barriers (the win was never the pass count; it was the stage-global drains).
                    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
                        if (!renderS[s]) continue;
                        for (uint32_t sub = 0; sub < ANO_SHADOW_ATLAS_SUBLAYERS; sub++) {
                            uint32_t ss = s * ANO_SHADOW_ATLAS_SUBLAYERS + sub;
                            VkRenderingAttachmentInfo bColor = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                .imageView = dstLyr[ss], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .resolveMode = VK_RESOLVE_MODE_NONE, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
                            VkRenderingInfo bri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                .renderArea = { .offset = {0,0}, .extent = { ANO_SHADOW_DIM, ANO_SHADOW_DIM } },
                                .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &bColor };
                            vkCmdBeginRendering(cmd, &bri);
                            bpc.layer = (int32_t)ss;
                            vkCmdPushConstants(cmd, rendererState.shadowBlurLayout, blurPcStages, 0, sizeof(bpc), &bpc);
                            vkCmdDraw(cmd, 3, 1, 0, 0);
                            vkCmdEndRendering(cmd);
                        }
                    }
                }
            }

            if (pass == 0) {
                // Phase barrier 3: temp COLOR -> SHADER_READ (blur-Y samples it) and atlas
                // SHADER_READ -> COLOR (blur-Y overwrites it; WAR against blur-X's reads, so the
                // execution dependency FRAGMENT -> COLOR is what matters). One call, both images.
                VkImageMemoryBarrier xy[2];
                xy[0] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowTempImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                xy[1] = (VkImageMemoryBarrier){ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0, 0, NULL, 0, NULL, 2, xy);
            } else {
                // Phase barrier 4: atlas COLOR -> SHADER_READ (its rest state) for the lighting frags.
                VkImageMemoryBarrier fin = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = rendererState.shadowAtlasImage, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &fin);
            }
        }

        // Rendered layers are now faithful to their light + this frame's scene; the render also
        // retires any matrix dirt and stamps the frustum for budget fairness.
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
            if (renderS[s]) {
                rendererState.shadowLayerValid[s]   = true;
                rendererState.shadowMatrixDirty[s]  = false;
                rendererState.shadowLastRendered[s] = rendererState.globalFrame;
            }
    }
}
