/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: recordCommandBuffer vs vkBeginCommandBuffer's failure arm. The begin check at
// record.c:29-:32 only logs 〜 the function is void, drawFrame has no failure channel
// (vulkanMaster.c:228) 〜 and the asyncLc split repeats the shape at :202 (prelude end logged,
// prelude submitted anyway) and :205 (main begin logged), so a begin refused under host/device
// memory pressure keeps recording: every vkCmd* from the :36 query-pool reset to the :287
// present barrier lands on a command buffer never put in RECORDING state
// (VUID-vkCmd*-commandBuffer-recording, UB on real drivers), the :297 vkEndCommandBuffer on it
// is a second state breach, and ano_frame_submit consumes the never-recorded buffer as if
// executable (submit.c:44/:72/:94) while the same module's submit twin proves the intended
// contract by returning false on its own failing call (submit.c:47-:51)
// (docs/BUGS.md, Render / Vulkan backend / Implementation, record.c:29).
// Harness: compiles the REAL record.c + passes.c TUs 〜 no GPU device, no loader. The vk stubs
// keep a per-command-buffer state machine (INITIAL/RECORDING/EXECUTABLE) and ledgers:
// vkBeginCommandBuffer fails on an armed Nth call with VK_ERROR_OUT_OF_HOST_MEMORY (the
// spec-clean refusal, out-state untouched), every vkCmd* tallies whether its command buffer is
// RECORDING, and the engine sub-recorders (shadow/views/text/composite/hiz tail) tally the
// handoff the same way; ano_ts mirrors profiling.c's timestampValidBits gate. entityCount 0
// keeps the shared-compute section out of scope 〜 the swapchain barriers, timestamps and
// sub-recorder handoffs are the signal.
// CONTROL A/B: nothing armed, plain and asyncLc-split paths 〜 full recording lands on RECORDING
// buffers, both ends make EXECUTABLE, so a reject-everything fix cannot pass.
// TRIGGER A: plain path, begin #1 refused (record.c:29) 〜 no vkCmd*, no sub-recorder handoff,
// no vkEndCommandBuffer may touch the dead buffer; fails today on all three.
// TRIGGER B: asyncLc path, begin #2 refused (record.c:205, the main CB) 〜 same contract; fails today.
// TRIGGER C: asyncLc path, begin #1 refused (record.c:29, the prelude CB) 〜 also exercises the
// :202 prelude-end-on-dead-buffer arm; fails today.
// A crash is a valid failure signal. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Globals record.c links against (owned by vulkanMaster.c in the real build) */

RendererState rendererState;
VulkanContext ctx;


/* Command-buffer state machine 〜 the spec lifecycle the stubs enforce */

enum { CB_INITIAL = 0, CB_RECORDING, CB_EXECUTABLE, CB_INVALID };

#define CB_TRACK_MAX 8u
static VkCommandBuffer g_cbKeys[CB_TRACK_MAX];
static int             g_cbVals[CB_TRACK_MAX];
static uint32_t        g_cbTracked;

// in: cb handle; out: pointer to its tracked lifecycle state (INITIAL on first sight).
static int* cb_state(VkCommandBuffer cb)
{
    for (uint32_t i = 0; i < g_cbTracked; i++) if (g_cbKeys[i] == cb) return &g_cbVals[i];
    g_cbKeys[g_cbTracked] = cb;
    g_cbVals[g_cbTracked] = CB_INITIAL;
    return &g_cbVals[g_cbTracked++];
}


/* Ledgers */

static uint32_t g_beginAttempts;       // vkBeginCommandBuffer calls
static uint32_t g_failBeginAt;         // 1-based begin call to refuse; 0 = never
static uint32_t g_cmdRecorded;         // vkCmd* on a RECORDING buffer
static uint32_t g_cmdOnDeadCB;         // vkCmd* on a buffer NOT in RECORDING state (the bug)
static uint32_t g_barrierCalls;        // vkCmdPipelineBarrier calls, any state
static uint32_t g_endCalls;            // vkEndCommandBuffer calls
static uint32_t g_endOnDeadCB;         // ends on a buffer NOT in RECORDING state (the bug)
static uint32_t g_subRecorders;        // engine sub-recorder handoffs, any state
static uint32_t g_subRecordersOnDeadCB;// handoffs of a buffer NOT in RECORDING state (the bug)

// in: cb + the vk entry name; tallies recording-state legality of one recorded command.
static void cmd_recorded(VkCommandBuffer cb, const char* name)
{
    if (*cb_state(cb) != CB_RECORDING) {
        g_cmdOnDeadCB++;
        printf("  ledger: %s on a command buffer not in RECORDING state (VUID-%s-commandBuffer-recording)\n", name, name);
        return;
    }
    g_cmdRecorded++;
}


/* Link seams 〜 the vk* entry points record.c calls (loader not linked) */

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
    (void)pBeginInfo;
    g_beginAttempts++;
    if (g_failBeginAt != 0u && g_beginAttempts == g_failBeginAt)
        return VK_ERROR_OUT_OF_HOST_MEMORY; // spec-clean refusal; the buffer stays in INITIAL state
    *cb_state(commandBuffer) = CB_RECORDING;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    g_endCalls++;
    int* st = cb_state(commandBuffer);
    if (*st != CB_RECORDING) {
        g_endOnDeadCB++;
        printf("  ledger: vkEndCommandBuffer on a command buffer not in RECORDING state (VUID-vkEndCommandBuffer-commandBuffer-00059)\n");
        *st = CB_INVALID;
        return VK_ERROR_OUT_OF_HOST_MEMORY; // no clean error exists; the call itself is the breach
    }
    *st = CB_EXECUTABLE;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{ (void)queryPool; (void)firstQuery; (void)queryCount; cmd_recorded(commandBuffer, "vkCmdResetQueryPool"); }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
{ (void)pipelineStage; (void)queryPool; (void)query; cmd_recorded(commandBuffer, "vkCmdWriteTimestamp"); }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
    g_barrierCalls++;
    cmd_recorded(commandBuffer, "vkCmdPipelineBarrier");
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{ (void)dstBuffer; (void)dstOffset; (void)size; (void)data; cmd_recorded(commandBuffer, "vkCmdFillBuffer"); }

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{ (void)pipelineBindPoint; (void)pipeline; cmd_recorded(commandBuffer, "vkCmdBindPipeline"); }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    (void)pipelineBindPoint; (void)layout; (void)firstSet; (void)descriptorSetCount;
    (void)pDescriptorSets; (void)dynamicOffsetCount; (void)pDynamicOffsets;
    cmd_recorded(commandBuffer, "vkCmdBindDescriptorSets");
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{ (void)layout; (void)stageFlags; (void)offset; (void)size; (void)pValues; cmd_recorded(commandBuffer, "vkCmdPushConstants"); }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{ (void)groupCountX; (void)groupCountY; (void)groupCountZ; cmd_recorded(commandBuffer, "vkCmdDispatch"); }


/* Link seams 〜 engine functions record.c calls (their TUs not linked) */

// Mirror of profiling.c's gate: a section stamp records only when the queue supports timestamps.
void ano_ts(VkCommandBuffer cmd, uint32_t query)
{
    (void)query;
    if (rendererState.timestampValidBits)
        cmd_recorded(cmd, "vkCmdWriteTimestamp");
}

// Never reached here (all staged[] stay 0); contract-faithful to the copy it would record.
void slot_upload_flush(VkCommandBuffer cmd, SlotUpload* b, uint32_t f)
{ (void)b; (void)f; cmd_recorded(cmd, "vkCmdCopyBuffer"); }

// in: cb + sub-recorder name; tallies the handoff 〜 in the real build each records many vkCmd*.
static void sub_recorder(VkCommandBuffer cmd, const char* name)
{
    g_subRecorders++;
    if (*cb_state(cmd) != CB_RECORDING) {
        g_subRecordersOnDeadCB++;
        printf("  ledger: %s handed a command buffer not in RECORDING state\n", name);
    }
}

void ano_shadow_record(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount)
{ (void)entityCount; (void)drawSlotCount; sub_recorder(cmd, "ano_shadow_record"); }

void ano_record_views(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount)
{ (void)entityCount; (void)drawSlotCount; sub_recorder(cmd, "ano_record_views"); }

void ano_record_composite(VkCommandBuffer cmd, uint32_t imageIndex)
{ (void)imageIndex; sub_recorder(cmd, "ano_record_composite"); }

void ano_record_hiz_tail(VkCommandBuffer cmd)
{ sub_recorder(cmd, "ano_record_hiz_tail"); }

void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex)
{ (void)state; (void)frameIndex; sub_recorder(cmd, "ano_vk_text_record"); }

// Draw-slot table stand-ins (components.c not linked); no transmission lane keeps tpsort out of scope.
uint32_t ano_draw_pipeline_count(void) { return 5u; }
uint32_t ano_draw_partition_count(void) { return 5u; }
uint32_t ano_draw_slot_of(PipelineType type) { (void)type; return ANO_NO_DRAW_SLOT; }


/* Phase runner 〜 one recordCommandBuffer pass over fresh state, ledgers reset */

static VkImage g_images[1];

// in: asyncLc = split-CB path; failBeginAt = 1-based vkBeginCommandBuffer call to refuse (0 = none).
static void run_phase(bool asyncLc, uint32_t failBeginAt)
{
    memset(&rendererState, 0, sizeof rendererState);
    memset(&ctx, 0, sizeof ctx);
    g_cbTracked = 0;
    g_beginAttempts = g_cmdRecorded = g_cmdOnDeadCB = g_barrierCalls = 0;
    g_endCalls = g_endOnDeadCB = g_subRecorders = g_subRecordersOnDeadCB = 0;
    g_failBeginAt = failBeginAt;

    rendererState.frameIndex = 0;
    rendererState.entityCount = 0;          // shared-compute section out of scope
    rendererState.timestampValidBits = 1u;  // frame-begin stamps record (record.c:35-:38)
    rendererState.asyncLc = asyncLc;
    rendererState.frames[0].commandBuffer = (VkCommandBuffer)(uintptr_t)0xCB01u;
    rendererState.frames[0].preludeCommandBuffer = (VkCommandBuffer)(uintptr_t)0xCB02u;
    rendererState.frames[0].timestampPool = (VkQueryPool)(uintptr_t)0x9001u;
    g_images[0] = (VkImage)(uintptr_t)0x1A01u;
    rendererState.images = g_images;

    recordCommandBuffer(0u);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control A: plain path, nothing armed 〜 full recording on a RECORDING buffer, executable end,
    // so the harness plumbing is proven and a reject-everything fix cannot pass
    printf("control A: plain path, no failure armed\n");
    run_phase(false, 0u);
    CHECK(g_beginAttempts == 1u, "control A: one begin on the plain path");
    CHECK(g_cmdRecorded >= 4u, "control A: commands recorded on the live buffer");
    CHECK(g_barrierCalls == 2u, "control A: both swapchain transitions recorded (record.c:225/:287)");
    CHECK(g_subRecorders == 5u, "control A: all five sub-recorders invoked (shadow/views/text/composite/hiz)");
    CHECK(g_endCalls == 1u && g_endOnDeadCB == 0u, "control A: one clean end");
    CHECK(g_cmdOnDeadCB == 0u && g_subRecordersOnDeadCB == 0u, "control A: nothing landed on a dead buffer");
    CHECK(*cb_state(rendererState.frames[0].commandBuffer) == CB_EXECUTABLE, "control A: main buffer ended EXECUTABLE");
    printf("control A: begins=%u cmds=%u barriers=%u subs=%u ends=%u dead=%u\n",
           g_beginAttempts, g_cmdRecorded, g_barrierCalls, g_subRecorders, g_endCalls, g_cmdOnDeadCB);

    // control B: asyncLc split, nothing armed 〜 prelude + main both begin, record, and end clean
    printf("control B: asyncLc split path, no failure armed\n");
    run_phase(true, 0u);
    CHECK(g_beginAttempts == 2u, "control B: two begins on the split path (prelude + main)");
    CHECK(g_endCalls == 2u && g_endOnDeadCB == 0u, "control B: two clean ends");
    CHECK(g_cmdOnDeadCB == 0u && g_subRecordersOnDeadCB == 0u, "control B: nothing landed on a dead buffer");
    CHECK(*cb_state(rendererState.frames[0].preludeCommandBuffer) == CB_EXECUTABLE, "control B: prelude buffer ended EXECUTABLE");
    CHECK(*cb_state(rendererState.frames[0].commandBuffer) == CB_EXECUTABLE, "control B: main buffer ended EXECUTABLE");
    printf("control B: begins=%u cmds=%u barriers=%u subs=%u ends=%u dead=%u\n",
           g_beginAttempts, g_cmdRecorded, g_barrierCalls, g_subRecorders, g_endCalls, g_cmdOnDeadCB);

    // trigger A: plain path, begin #1 refused (record.c:29) 〜 the arm logs and falls through, so
    // today every command, every sub-recorder handoff, and the end land on a buffer still in the
    // INITIAL state; the fix must stop recording (and give drawFrame a failure channel)
    printf("trigger A: plain path, vkBeginCommandBuffer refuses call #1 (record.c:29)\n");
    run_phase(false, 1u);
    CHECK(g_cmdOnDeadCB == 0u, "trigger A: no vkCmd* may land on a command buffer whose begin failed (record.c:29-:32 falls through)");
    CHECK(g_subRecordersOnDeadCB == 0u, "trigger A: no sub-recorder may be handed the dead command buffer (record.c:230/:257/:262/:264/:266)");
    CHECK(g_endOnDeadCB == 0u, "trigger A: vkEndCommandBuffer must not close a buffer that never began (record.c:297)");
    printf("trigger A: deadCmds=%u deadSubs=%u deadEnds=%u\n", g_cmdOnDeadCB, g_subRecordersOnDeadCB, g_endOnDeadCB);

    // trigger B: asyncLc split, begin #2 refused (record.c:205, the main CB) 〜 the prelude is
    // healthy but everything after :209 records into the dead main buffer
    printf("trigger B: asyncLc path, vkBeginCommandBuffer refuses call #2 (record.c:205)\n");
    run_phase(true, 2u);
    CHECK(g_cmdOnDeadCB == 0u, "trigger B: no vkCmd* may land on the main command buffer whose begin failed (record.c:205-:206 falls through)");
    CHECK(g_subRecordersOnDeadCB == 0u, "trigger B: no sub-recorder may be handed the dead main command buffer");
    CHECK(g_endOnDeadCB == 0u, "trigger B: vkEndCommandBuffer must not close a buffer that never began (record.c:297)");
    printf("trigger B: deadCmds=%u deadSubs=%u deadEnds=%u\n", g_cmdOnDeadCB, g_subRecordersOnDeadCB, g_endOnDeadCB);

    // trigger C: asyncLc split, begin #1 refused (record.c:29, the prelude CB) 〜 the prelude's
    // commands and its :202 end land on the dead buffer, then submit.c:72 would consume it
    printf("trigger C: asyncLc path, vkBeginCommandBuffer refuses call #1 (record.c:29, prelude)\n");
    run_phase(true, 1u);
    CHECK(g_cmdOnDeadCB == 0u, "trigger C: no vkCmd* may land on the prelude command buffer whose begin failed (record.c:29-:32 falls through)");
    CHECK(g_endOnDeadCB == 0u, "trigger C: vkEndCommandBuffer must not close a prelude that never began (record.c:202)");
    printf("trigger C: deadCmds=%u deadSubs=%u deadEnds=%u\n", g_cmdOnDeadCB, g_subRecordersOnDeadCB, g_endOnDeadCB);

    if (failures) {
        printf("anotest_recordbeginguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_recordbeginguard: all passed\n");
    return 0;
}
