/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: register_static_shadow light-type domain. anoptic_render.h documents
// RenderLightParams.type as RenderLightType {0,1,2}, but no one between ano_render_submit
// (a raw ring push) and the consumer validates it: apply.c:132 forwards (uint32_t)cmd.light.type
// raw, and register_static_shadow indexes shadowTypeUsed[3] with it 〜 read at
// shadow_casters.c:97, write at :114 (docs/BUGS.md, Render / Vulkan backend / Interface-level).
// The fields directly after the array are the runtime shadow frustum free-lists, so type 7
// aliases rtSingleFreeCount and type 10 aliases rtPointFreeCount: when a drained pool holds 0
// the budget guard reads that 0, passes, and the += 1 resurrects the empty free-list 〜 the
// next runtime caster pops a frustum block already owned by a live light. Larger types index
// arbitrarily far past RendererState. Controls pin correct spot/dir/point registration and
// budget refusal, so a reject-everything fix cannot pass; offsetof controls pin the aliasing
// so layout drift cannot silently disarm the trigger. Deterministic, no RNG, no device 〜
// only host-side staging is faked in. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "vulkan_backend/shadow/shadow.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Zero-initialized render state; heap-sized fields are wired up in main.
static RendererState st;

// Inputs: SlotUpload lane b, element stride in bytes. Output: none.
// Host-side staging only 〜 enough for slot_upload_stage to queue deltas with no device;
// cap far above anything this test stages, so the vkDeviceWaitIdle growth path never runs.
static void fake_lane(SlotUpload* b, uint32_t stride)
{
    b->stride = stride;
    b->stagingCap = 64u;
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        b->stagingMapped[f] = calloc(64u, stride);
        b->regions[f] = (VkBufferCopy*)calloc(64u, sizeof(VkBufferCopy));
        b->staged[f] = 0u;
    }
}

int main(void)
{
    // Harness: mirror shadow_resources.c's static-rig + runtime-pool init, fake the two
    // config lanes, allocate the frustum config mirror. No Vulkan object is ever touched.
    fake_lane(&st.shadowConfig, sizeof(ShadowFrustumConfig));
    fake_lane(&st.shadowInfo, sizeof(ShadowLightInfo));
    st.shadowCfgMirror = (ShadowFrustumConfig*)calloc(ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig));
    st.shadowFrustumNext = 0u;
    st.rtSingleFreeCount = 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_RT_SINGLE_COUNT; s++)
        st.rtSingleFree[st.rtSingleFreeCount++] = ANO_SHADOW_RT_SINGLE_BASE + s;
    st.rtPointFreeCount = 0u;
    for (uint32_t b = 0; b < ANO_SHADOW_RT_POINT_COUNT; b++)
        st.rtPointFree[st.rtPointFreeCount++] = ANO_SHADOW_RT_POINT_BASE + b * ANO_SHADOW_CUBE_FACES;

    // control: the aliasing the trigger relies on 〜 shadowTypeUsed[7] / [10] land exactly on
    // the free-list counters; if layout drifts these fail and the trigger must be re-aimed
    CHECK(offsetof(RendererState, rtSingleFreeCount) == offsetof(RendererState, shadowTypeUsed) + 7u * sizeof(uint32_t),
          "shadowTypeUsed[7] aliases rtSingleFreeCount");
    CHECK(offsetof(RendererState, rtPointFreeCount) == offsetof(RendererState, shadowTypeUsed) + 10u * sizeof(uint32_t),
          "shadowTypeUsed[10] aliases rtPointFreeCount");

    // control: a spot registers 〜 budget counted, one frustum consumed, config live
    register_static_shadow(&st, 0u, LIGHT_TYPE_SPOT, 0u, 0u, 5.0f);
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_SPOT] == 1u, "spot counted in its budget");
    CHECK(st.shadowFrustumNext == 1u, "spot consumed one static frustum");
    CHECK(st.shadowCfgMirror[0].active == 1u && st.shadowCfgMirror[0].lightType == LIGHT_TYPE_SPOT,
          "spot config mirrored live");
    CHECK(st.shadowConfig.staged[0] == 1u && st.shadowInfo.staged[0] == 1u, "spot staged config + info");

    // control: directional budget honored 〜 the second dir is refused, nothing consumed
    register_static_shadow(&st, 1u, LIGHT_TYPE_DIRECTIONAL, 0u, 0u, 0.0f);
    register_static_shadow(&st, 2u, LIGHT_TYPE_DIRECTIONAL, 0u, 0u, 0.0f);
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_DIRECTIONAL] == 1u, "second directional refused by budget");
    CHECK(st.shadowFrustumNext == 2u, "refused directional consumed no frustum");

    // control: a point registers a 6-face block
    register_static_shadow(&st, 3u, LIGHT_TYPE_POINT, 0u, 0u, 8.0f);
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_POINT] == 1u, "point counted in its budget");
    CHECK(st.shadowFrustumNext == 8u, "point consumed six static frustums");

    // control: none of the valid registrations touched the runtime pools
    CHECK(st.rtSingleFreeCount == ANO_SHADOW_RT_SINGLE_COUNT, "valid casters left the single pool alone");
    CHECK(st.rtPointFreeCount == ANO_SHADOW_RT_POINT_COUNT, "valid casters left the point pool alone");

    // trigger: single pool drained (all four runtime dir/spot casters attached), then a
    // CREATE-with-light carrying out-of-enum type 7 arrives 〜 the budget guard reads
    // shadowTypeUsed[7] == rtSingleFreeCount == 0, passes, and the += 1 resurrects the
    // empty free-list; the next runtime attach double-allocates a live frustum block
    st.rtSingleFreeCount = 0u;
    register_static_shadow(&st, 4u, 7u, 0u, 0u, 5.0f);
    CHECK(st.rtSingleFreeCount == 0u, "out-of-enum type 7 must not resurrect the drained single pool");

    // trigger: same aliasing one field further 〜 type 10 lands on rtPointFreeCount
    st.rtPointFreeCount = 0u;
    register_static_shadow(&st, 5u, 10u, 0u, 0u, 5.0f);
    CHECK(st.rtPointFreeCount == 0u, "out-of-enum type 10 must not resurrect the drained point pool");

    // the in-enum budget accounting survives either way 〜 the corruption is purely out of bounds
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_DIRECTIONAL] == 1u, "dir budget untouched by out-of-enum types");
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_POINT] == 1u, "point budget untouched by out-of-enum types");
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_SPOT] == 1u, "spot budget untouched by out-of-enum types");

    if (failures) {
        printf("anotest_shadowtypeguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_shadowtypeguard: all passed\n");
    return 0;
}
