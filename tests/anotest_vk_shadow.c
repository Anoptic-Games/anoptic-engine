/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Static-row shadow contract: register / unregister / static_shadow_row_casts.
// Asserts cast rows, footprint, and GPU-told block. Not free-list order.
// Region: ANO_SHADOW_STATIC_FRUSTUM_COUNT; point=6 faces, dir/spot=1.
// Host-side staging only. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vulkan_backend/backend.h"
#include "vulkan_backend/shadow/shadow.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Render state. Heap fields from harness_init.
static RendererState st;

// in: SlotUpload lane b, element stride. out: none.
// Host staging for slot_upload_stage. No device.
static void fake_lane(SlotUpload* b, uint32_t stride)
{
    b->stride = stride;
    b->stagingCap = 256u;
    for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        b->stagingMapped[f] = calloc(b->stagingCap, stride);
        b->regions[f] = (VkBufferCopy*)calloc(b->stagingCap, sizeof(VkBufferCopy));
        b->staged[f] = 0u;
    }
}

// Reset slot_upload_flush queues (no device copy).
static void frame_end(void)
{
    st.shadowConfig.staged[0] = 0u;
    st.shadowInfo.staged[0] = 0u;
}

// Last staged value for index this frame, or NULL.
// Last region naming index wins.
static const void* lane_last(const SlotUpload* b, uint32_t index)
{
    const void* v = NULL;
    for (uint32_t r = 0; r < b->staged[0]; r++)
        if (b->regions[0][r].dstOffset == (VkDeviceSize)index * b->stride)
            v = (const char*)b->stagingMapped[0] + b->regions[0][r].srcOffset;
    return v;
}

// First live static block base + type for lightIdx.
// Walks shadowCfgMirror.
// out: base or ANO_SHADOW_NONE; outType set only when found
static uint32_t row_block(uint32_t lightIdx, uint32_t* outType)
{
    for (uint32_t s = 0; s < ANO_SHADOW_STATIC_FRUSTUM_COUNT; s++)
        if (st.shadowCfgMirror[s].live && st.shadowCfgMirror[s].lightIndex == lightIdx) {
            *outType = st.shadowCfgMirror[s].lightType;
            return s;
        }
    return ANO_SHADOW_NONE;
}

// True if query, mirror, and staged GPU info agree on cast footprint for lightType.
static bool row_casts_as(uint32_t lightIdx, uint32_t lightType)
{
    uint32_t queryType = LIGHT_TYPE_COUNT, mirrorType = LIGHT_TYPE_COUNT;
    bool query = static_shadow_row_casts(&st, lightIdx, &queryType);
    uint32_t base = row_block(lightIdx, &mirrorType);
    uint32_t want = lightType == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
    if (base == ANO_SHADOW_NONE || !query) return false;
    if (queryType != lightType || mirrorType != lightType) return false;
    if (base + want > ANO_SHADOW_STATIC_FRUSTUM_COUNT) return false;
    for (uint32_t f = 0; f < want; f++) {
        const ShadowFrustumConfig* c = &st.shadowCfgMirror[base + f];
        if (!c->live || c->lightIndex != lightIdx || c->lightType != lightType) return false;
        if (c->faceIndex != (lightType == LIGHT_TYPE_POINT ? f : 0u)) return false;
    }
    const ShadowLightInfo* si = (const ShadowLightInfo*)lane_last(&st.shadowInfo, lightIdx);
    if (si && (!si->castsShadow || si->baseFrustum != base || si->frustumCount != want)) return false;
    return true;
}

// Init static-rig + runtime pools. No Vulkan objects.
static void harness_init(void)
{
    fake_lane(&st.shadowConfig, sizeof(ShadowFrustumConfig));
    fake_lane(&st.shadowInfo, sizeof(ShadowLightInfo));
    st.shadowCfgMirror = (ShadowFrustumConfig*)calloc(ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig));
    st.rtSingleFreeCount = 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_RT_SINGLE_COUNT; s++)
        st.rtSingleFree[st.rtSingleFreeCount++] = ANO_SHADOW_RT_SINGLE_BASE + s;
    st.rtPointFreeCount = 0u;
    for (uint32_t b = 0; b < ANO_SHADOW_RT_POINT_COUNT; b++)
        st.rtPointFree[st.rtPointFreeCount++] = ANO_SHADOW_RT_POINT_BASE + b * ANO_SHADOW_CUBE_FACES;
}

// Reset books to init state.
static void harness_reset(void)
{
    frame_end();
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++)
        st.shadowCfgMirror[s] = (ShadowFrustumConfig){0};
    st.shadowFrustumNext = 0u;
    st.stSingleFreeCount = st.stPointFreeCount = 0u;
    for (uint32_t t = 0; t < LIGHT_TYPE_COUNT; t++) st.shadowTypeUsed[t] = 0u;
}

// Grant each shape; refuse past type ceiling.
static void test_grant_and_budget(void)
{
    register_static_shadow(&st, 0u, LIGHT_TYPE_SPOT, 0u, 0u, 5.0f);
    CHECK(row_casts_as(0u, LIGHT_TYPE_SPOT), "a spot CREATE casts");

    register_static_shadow(&st, 1u, LIGHT_TYPE_POINT, 0u, 1u, 8.0f);
    CHECK(row_casts_as(1u, LIGHT_TYPE_POINT), "a point CREATE casts");

    uint32_t type;
    CHECK(row_block(0u, &type) != row_block(1u, &type), "distinct rows hold distinct blocks");

    // Past ceiling: shadowless, region unchanged.
    uint32_t region = st.shadowFrustumNext;
    register_static_shadow(&st, 2u, LIGHT_TYPE_SPOT, 0u, 2u, 5.0f);
    CHECK(!row_casts_as(2u, LIGHT_TYPE_SPOT), "a spot past budget stays shadowless");
    CHECK(st.shadowFrustumNext == region, "a refused caster consumes no region");
    harness_reset();
}

// Same-footprint rebuild in place; revoke leaves row lit and shadowless.
static void test_rebuild_and_revoke(void)
{
    uint32_t type;
    register_static_shadow(&st, 0u, LIGHT_TYPE_SPOT, 0u, 0u, 5.0f);
    uint32_t base = row_block(0u, &type);
    uint32_t region = st.shadowFrustumNext;
    frame_end();

    register_static_shadow(&st, 0u, LIGHT_TYPE_SPOT, 0u, 7u, 9.0f); // same shape, new parent
    CHECK(row_casts_as(0u, LIGHT_TYPE_SPOT), "a same-footprint rebuild still casts");
    CHECK(row_block(0u, &type) == base, "a same-footprint rebuild is rewritten in place");
    CHECK(st.shadowFrustumNext == region, "a same-footprint rebuild consumes no region");
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_SPOT] == 1u, "a same-footprint rebuild consumes no budget");
    CHECK(st.shadowVolume[base].parentSlot == 7u, "a rebuild rebinds the block to the new parent");
    frame_end();

    unregister_static_shadow(&st, 0u, 0u);
    CHECK(!static_shadow_row_casts(&st, 0u, &type), "a revoked row stops casting");
    CHECK(type == LIGHT_TYPE_COUNT, "a revoked row has no type to disagree with");
    const ShadowLightInfo* si = (const ShadowLightInfo*)lane_last(&st.shadowInfo, 0u);
    CHECK(si && !si->castsShadow, "a revoke tells the GPU the row is shadowless");
    CHECK(st.shadowTypeUsed[LIGHT_TYPE_SPOT] == 0u, "a revoke returns the budget row");
    CHECK(st.shadowFrustumNext == region, "a revoke does not move the region cursor");

    // Returned budget funds next caster of that type.
    register_static_shadow(&st, 1u, LIGHT_TYPE_SPOT, 0u, 1u, 5.0f);
    CHECK(row_casts_as(1u, LIGHT_TYPE_SPOT), "a revoked budget row funds a later spot");
    harness_reset();
}

// One row flips shape far past region size; later casters must still register.
static void test_footprint_churn(void)
{
    const uint32_t flips = ANO_SHADOW_STATIC_FRUSTUM_COUNT * 8u; // well past the region's own size
    for (uint32_t i = 0; i < flips; i++) {
        uint32_t type = (i & 1u) ? LIGHT_TYPE_SPOT : LIGHT_TYPE_POINT;
        register_static_shadow(&st, 0u, type, 0u, i & 7u, 4.0f);
        if (!row_casts_as(0u, type)) {
            printf("FAIL: churned row went shadowless at flip %u (%s:%d)\n", i, __FILE__, __LINE__);
            failures++;
            break; // one report per defect, not one per flip
        }
        frame_end();
    }
    CHECK(st.shadowFrustumNext <= ANO_SHADOW_STATIC_FRUSTUM_COUNT, "churn stays inside the static region");

    // Later casters still find region.
    register_static_shadow(&st, 1u, LIGHT_TYPE_DIRECTIONAL, 0u, 1u, 0.0f);
    CHECK(row_casts_as(1u, LIGHT_TYPE_DIRECTIONAL), "a directional after the churn still casts");
    register_static_shadow(&st, 2u, LIGHT_TYPE_POINT, 0u, 2u, 6.0f);
    CHECK(row_casts_as(2u, LIGHT_TYPE_POINT), "a point after the churn still casts");
    harness_reset();
}

// Full region + full budgets. Saturated-type flip refused; flip back restores.
static void test_churn_at_full_budget(void)
{
    register_static_shadow(&st, 0u, LIGHT_TYPE_DIRECTIONAL, 0u, 0u, 0.0f);
    register_static_shadow(&st, 1u, LIGHT_TYPE_SPOT, 0u, 1u, 5.0f);
    for (uint32_t p = 0; p < ANO_SHADOW_POINT_COUNT; p++)
        register_static_shadow(&st, 2u + p, LIGHT_TYPE_POINT, 0u, 2u + p, 7.0f);
    CHECK(st.shadowFrustumNext == ANO_SHADOW_STATIC_FRUSTUM_COUNT, "a full budget fills the region exactly");
    frame_end();

    int before = failures; // one report per defect, not one per round
    for (uint32_t i = 0; i < ANO_SHADOW_STATIC_FRUSTUM_COUNT * 4u && failures == before; i++) {
        // Row 0 dir->spot hits row 1's spot ceiling.
        register_static_shadow(&st, 0u, LIGHT_TYPE_SPOT, 0u, 0u, 5.0f);
        CHECK(!row_casts_as(0u, LIGHT_TYPE_SPOT), "a flip into a saturated type is refused");
        frame_end();
        register_static_shadow(&st, 0u, LIGHT_TYPE_DIRECTIONAL, 0u, 0u, 0.0f);
        CHECK(row_casts_as(0u, LIGHT_TYPE_DIRECTIONAL), "flipping back restores the caster");
        frame_end();

        // Point row round-trips via saturated spot.
        register_static_shadow(&st, 2u, LIGHT_TYPE_SPOT, 0u, 2u, 5.0f);
        frame_end();
        register_static_shadow(&st, 2u, LIGHT_TYPE_POINT, 0u, 2u, 7.0f);
        CHECK(row_casts_as(2u, LIGHT_TYPE_POINT), "a point round-trip keeps its shadow");
        frame_end();
    }
    CHECK(st.shadowFrustumNext == ANO_SHADOW_STATIC_FRUSTUM_COUNT, "full-budget churn never grows the region");
    CHECK(row_casts_as(1u, LIGHT_TYPE_SPOT), "the untouched spot row survives the churn");
    for (uint32_t p = 0; p < ANO_SHADOW_POINT_COUNT; p++)
        CHECK(row_casts_as(2u + p, LIGHT_TYPE_POINT), "every point row survives the churn");
    harness_reset();
}

int main(void)
{
    harness_init();
    CHECK(st.shadowCfgMirror != NULL, "config mirror allocated");

    test_grant_and_budget();
    test_rebuild_and_revoke();
    test_footprint_churn();
    test_churn_at_full_budget();

    // Static path left runtime pools untouched.
    CHECK(st.rtSingleFreeCount == ANO_SHADOW_RT_SINGLE_COUNT, "the static path left the runtime single pool alone");
    CHECK(st.rtPointFreeCount == ANO_SHADOW_RT_POINT_COUNT, "the static path left the runtime point pool alone");

    if (failures == 0) { printf("anotest_vk_shadow: all checks passed\n"); return 0; }
    printf("anotest_vk_shadow: %d check(s) failed\n", failures);
    return 1;
}
