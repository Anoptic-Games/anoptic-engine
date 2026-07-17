/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_record_composite's aux-view inset placement (record_views.c:302, x sibling at
// :301). The inset origin y = H - margin - (insetH + margin) * (idx + 1) + margin is computed
// in uint32 and cast to int32, so swapchain height H <= 22 wraps y negative (H=22 gives -1,
// H=2 gives -14) and the value rides verbatim into vkCmdSetScissor offset.y 〜 a
// VUID-vkCmdSetScissor-x-00595 breach (scissor offsets must be >= 0, UB on real drivers) on
// every composited frame, with H <= 2 additionally emitting a zero-height inset viewport
// against VUID-VkViewport-height-01772. Reachable: the composite runs unconditionally for
// ANO_VIEW_COUNT 2, imageExtent adopts the surface currentExtent 〜 the raw Win32 client size
// (swapchain.c:73/:167) 〜 and recreateSwapChain's only floor is the 0x0 wait loop
// (swapchain.c:340-:345); Win32 minimum tracking size protects the x at :301 but the client
// height drags freely into [1,22]
// (docs/BUGS.md, Render / Vulkan backend / Implementation, record_views.c:302).
// Harness: compiles the REAL record_views.c + passes.c TUs 〜 no GPU device, no loader. The vk
// stubs capture every vkCmdSetScissor / vkCmdSetViewport argument; the harness drives
// ano_record_composite directly at chosen swapchain extents.
// CONTROL A: 800x600 〜 exactly one fullscreen + one inset placement, inset at the intended
// {518, 384, 266x200}, everything legal, so a reject-everything fix cannot pass.
// CONTROL B: 800x23 〜 the exact legality boundary (y lands on 0); every captured placement
// must be legal, no exact-shape demand so a skip-tiny-insets fix stays free to differ.
// TRIGGER A: 800x22 〜 one texel shorter: today the inset scissor/viewport y is -1; fails.
// TRIGGER B: 800x2 〜 deep drag: today y is -14 and the inset viewport height is 0; fails.
// A crash is a valid failure signal. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Globals record_views.c links against (owned by vulkanMaster.c in the real build) */

RendererState rendererState;
VulkanContext ctx;
PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT = NULL;


/* Ledgers 〜 every dynamic-state placement the composite records */

#define CAP_MAX 16u
static VkRect2D   g_scissors[CAP_MAX];
static uint32_t   g_scissorCount;
static VkViewport g_viewports[CAP_MAX];
static uint32_t   g_viewportCount;


/* Link seams 〜 the vk* entry points record_views.c calls (loader not linked) */

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
{
    (void)commandBuffer; (void)firstScissor;
    for (uint32_t i = 0; i < scissorCount && g_scissorCount < CAP_MAX; i++)
        g_scissors[g_scissorCount++] = pScissors[i];
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
{
    (void)commandBuffer; (void)firstViewport;
    for (uint32_t i = 0; i < viewportCount && g_viewportCount < CAP_MAX; i++)
        g_viewports[g_viewportCount++] = pViewports[i];
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo)
{ (void)commandBuffer; (void)pRenderingInfo; }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{ (void)commandBuffer; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{ (void)commandBuffer; (void)pipelineBindPoint; (void)pipeline; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)firstSet;
    (void)descriptorSetCount; (void)pDescriptorSets; (void)dynamicOffsetCount; (void)pDynamicOffsets;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{ (void)commandBuffer; (void)vertexCount; (void)instanceCount; (void)firstVertex; (void)firstInstance; }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{ (void)commandBuffer; (void)groupCountX; (void)groupCountY; (void)groupCountZ; }

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{ (void)commandBuffer; (void)layout; (void)stageFlags; (void)offset; (void)size; (void)pValues; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{ (void)commandBuffer; (void)buffer; (void)offset; (void)indexType; }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{ (void)commandBuffer; (void)buffer; (void)offset; (void)drawCount; (void)stride; }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{ (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{ (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstBuffer; (void)regionCount; (void)pRegions; }


/* Link seams 〜 engine functions record_views.c calls (their TUs not linked) */

// Draw-slot stand-in (components.c not linked); the harness never records geometry.
uint32_t ano_draw_slot_of(PipelineType type) { (void)type; return ANO_NO_DRAW_SLOT; }

void ano_vk_text_record_world(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex, uint32_t view)
{ (void)state; (void)cmd; (void)frameIndex; (void)view; }

void ano_vk_text_record_composite(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex)
{ (void)state; (void)cmd; (void)frameIndex; }


/* Phase runner 〜 one ano_record_composite pass at a chosen swapchain extent, ledgers reset */

static VkImageView g_swapViews[1];

// in: W,H = swapchain extent the surface reported (the raw client size on Win32).
static void run_composite(uint32_t W, uint32_t H)
{
    memset(&rendererState, 0, sizeof rendererState);
    memset(&ctx, 0, sizeof ctx);
    g_scissorCount = 0;
    g_viewportCount = 0;

    rendererState.frameIndex = 0;
    rendererState.imageExtent = (VkExtent2D){ W, H };
    g_swapViews[0] = (VkImageView)(uintptr_t)0x51CEu;
    rendererState.views = g_swapViews;
    rendererState.tonemapPipeline = (VkPipeline)(uintptr_t)0x70AEu;
    rendererState.tonemapLayout = (VkPipelineLayout)(uintptr_t)0x70AFu;

    ano_record_composite((VkCommandBuffer)(uintptr_t)0xCB01u, 0u);
}

// in: phase label; prints every captured placement, then CHECKs the spec legality every
// placement must satisfy regardless of layout choice: scissor offsets >= 0
// (VUID-vkCmdSetScissor-x-00595) and viewport width/height > 0 (VUID-VkViewport-width-01770 /
// -height-01772). Skipping the inset entirely satisfies this vacuously 〜 the control pins shape.
static void check_all_legal(const char* phase)
{
    for (uint32_t i = 0; i < g_scissorCount; i++) {
        printf("  %s: scissor[%u] offset {%d, %d} extent {%u, %u}\n", phase, i,
               g_scissors[i].offset.x, g_scissors[i].offset.y,
               g_scissors[i].extent.width, g_scissors[i].extent.height);
        CHECK(g_scissors[i].offset.x >= 0, "scissor offset.x must be >= 0 (VUID-vkCmdSetScissor-x-00595, record_views.c:301)");
        CHECK(g_scissors[i].offset.y >= 0, "scissor offset.y must be >= 0 (VUID-vkCmdSetScissor-x-00595, record_views.c:302)");
    }
    for (uint32_t i = 0; i < g_viewportCount; i++) {
        printf("  %s: viewport[%u] pos {%.1f, %.1f} size {%.1f, %.1f}\n", phase, i,
               (double)g_viewports[i].x, (double)g_viewports[i].y,
               (double)g_viewports[i].width, (double)g_viewports[i].height);
        CHECK(g_viewports[i].width > 0.0f, "viewport width must be > 0 (VUID-VkViewport-width-01770)");
        CHECK(g_viewports[i].height > 0.0f, "viewport height must be > 0 (VUID-VkViewport-height-01772, record_views.c:304)");
    }
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control A: 800x600 〜 the composite must place view 0 fullscreen and the one aux inset at
    // the intended bottom-right slot {518, 384, 266x200}, all legal, so a reject-everything or
    // never-inset fix cannot pass
    printf("control A: 800x600 composite\n");
    run_composite(800u, 600u);
    CHECK(g_scissorCount == 2u, "control A: one fullscreen + one inset scissor");
    CHECK(g_viewportCount == 2u, "control A: one fullscreen + one inset viewport");
    if (g_scissorCount == 2u) {
        CHECK(g_scissors[0].offset.x == 0 && g_scissors[0].offset.y == 0
              && g_scissors[0].extent.width == 800u && g_scissors[0].extent.height == 600u,
              "control A: view 0 scissor covers the swapchain");
        CHECK(g_scissors[1].offset.x == 518 && g_scissors[1].offset.y == 384,
              "control A: inset scissor at the intended {518, 384}");
        CHECK(g_scissors[1].extent.width == 266u && g_scissors[1].extent.height == 200u,
              "control A: inset scissor extent W/3 x H/3");
    }
    check_all_legal("control A");

    // control B: 800x23 〜 the exact boundary: today's arithmetic lands the inset y on 0, still
    // legal; every placement a fix keeps at this height must stay legal
    printf("control B: 800x23 composite (legality boundary)\n");
    run_composite(800u, 23u);
    check_all_legal("control B");

    // trigger A: 800x22 〜 one texel below the boundary, a client height any Win32 user can drag
    // to (recreateSwapChain blocks only 0x0): H - H/3 - 16 wraps in uint32 and the int32 cast
    // hands vkCmdSetScissor offset.y == -1; fails today
    printf("trigger A: 800x22 composite (record_views.c:302 wraps y to -1)\n");
    run_composite(800u, 22u);
    CHECK(g_scissorCount >= 1u, "trigger A: view 0 still composites");
    check_all_legal("trigger A");

    // trigger B: 800x2 〜 deep drag: the inset scissor y wraps to -14 and insetH = 0 makes the
    // inset viewport zero-height; fails today on both counts
    printf("trigger B: 800x2 composite (record_views.c:302 wraps y to -14, insetH 0)\n");
    run_composite(800u, 2u);
    CHECK(g_scissorCount >= 1u, "trigger B: view 0 still composites");
    check_all_legal("trigger B");

    if (failures) {
        printf("anotest_insetscissorguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_insetscissorguard: all passed\n");
    return 0;
}
