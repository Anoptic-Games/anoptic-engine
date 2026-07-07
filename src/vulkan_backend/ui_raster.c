/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane: GPU plumbing (docs/ui/ui-render.md §7 step 3). Owns the per-frame
// table buffers and their raster-set bindings 4-7; the prim math lives in src/ui and
// ports into textraster.comp at step 4; compose/bridge arrive at step 5. The lane is
// visually inert here: uiPrimCount/uiClipCount stay 0, the dispatch never reads the
// tables, frame totals are unchanged.

#include "vulkan_backend/ui_raster.h"
#include "vulkan_backend/instance/instanceInit.h"

#include <anoptic_logging.h>
#include <anoptic_ui.h>

// Region layout inside one uiFrameBuffer, in binding order 4-7. Offsets must satisfy
// minStorageBufferOffsetAlignment; the spec caps that limit at 256, which every
// region size below is a multiple of.
#define ANO_UI_PRIM_BYTES  (ANO_UI_MAX_PRIMS * (uint32_t)sizeof(AnoUiPrim))
#define ANO_UI_CLIP_OFF    ANO_UI_PRIM_BYTES
#define ANO_UI_CLIP_BYTES  (ANO_UI_MAX_CLIPS * (uint32_t)sizeof(AnoUiClip))
#define ANO_UI_PAINT_OFF   (ANO_UI_CLIP_OFF + ANO_UI_CLIP_BYTES)
#define ANO_UI_PAINT_BYTES (ANO_UI_MAX_PAINTS * (uint32_t)sizeof(AnoUiPaint))
#define ANO_UI_STOP_OFF    (ANO_UI_PAINT_OFF + ANO_UI_PAINT_BYTES)
#define ANO_UI_STOP_BYTES  (ANO_UI_MAX_STOPS * (uint32_t)sizeof(AnoUiStop))
#define ANO_UI_FRAME_BYTES (ANO_UI_STOP_OFF + ANO_UI_STOP_BYTES)

static_assert((ANO_UI_CLIP_OFF % 256u) == 0 && (ANO_UI_PAINT_OFF % 256u) == 0
                  && (ANO_UI_STOP_OFF % 256u) == 0,
              "region offsets must satisfy the worst-case storage-buffer alignment");

// In: ctx/state after ano_vk_text_init (the shared raster set is created later, in
// ano_vk_text_create_sets; only the buffers must exist by then). Out: true always;
// failure clears uiOverlay and leaves the fallback binding path to ui_write_sets.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
    {
        // No shared dispatch to ride; the gate log already said why.
        state->uiOverlay = false;
        return true;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (!createDataBuffer(ctx, &gpuAllocator, ANO_UI_FRAME_BYTES,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &state->frames[i].uiFrameBuffer, &state->frames[i].uiFrameAlloc))
        {
            ano_log(ANO_WARN, "UI overlay disabled: table buffer creation failed.");
            state->uiOverlay = false;
            return true;
        }
        state->frames[i].uiFrameMapped = state->frames[i].uiFrameAlloc.mapped;
    }
    ano_log(ANO_INFO, "UI overlay: tables resident (%u KiB x%u slots)%s",
            ANO_UI_FRAME_BYTES / 1024u, (unsigned)MAX_FRAMES_IN_FLIGHT,
            state->uiOverlay ? "" : ", compose pinned off");
    return true;
}

// Writes bindings 4-7 of every slot's shared raster set. When a slot's table buffer
// is absent (creation failure), the bindings point at the slot's text frame buffer:
// any valid SSBO keeps the set legal, and pinned-zero counts mean it is never read.
void ano_vk_ui_write_sets(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];
        VkDescriptorBufferInfo infos[4];
        if (fr->uiFrameBuffer != VK_NULL_HANDLE)
        {
            infos[0] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, 0, ANO_UI_PRIM_BYTES };
            infos[1] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_CLIP_OFF, ANO_UI_CLIP_BYTES };
            infos[2] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_PAINT_OFF, ANO_UI_PAINT_BYTES };
            infos[3] = (VkDescriptorBufferInfo){ fr->uiFrameBuffer, ANO_UI_STOP_OFF, ANO_UI_STOP_BYTES };
        }
        else
        {
            for (int k = 0; k < 4; k++)
                infos[k] = (VkDescriptorBufferInfo){ fr->textFrameBuffer, 0, VK_WHOLE_SIZE };
        }
        VkWriteDescriptorSet writes[4] = {};
        for (uint32_t w = 0; w < 4; ++w)
        {
            writes[w].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[w].dstSet = fr->textRasterSet;
            writes[w].dstBinding = 4 + w;
            writes[w].descriptorCount = 1;
            writes[w].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[w].pBufferInfo = &infos[w];
        }
        vkUpdateDescriptorSets(ctx->device, 4, writes, 0, NULL);
    }
}

void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (state->frames[i].uiFrameBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx->device, state->frames[i].uiFrameBuffer, NULL);
            state->frames[i].uiFrameBuffer = VK_NULL_HANDLE;
            state->frames[i].uiFrameMapped = NULL;
        }
    }
}
