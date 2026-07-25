/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the camera-pose guard at the public publish seam (ano_render_publish_view,
// render_bridge/ano_render_bridge.c). lookAt (vulkan_backend/vertex/vertex.c:104) is
// branch-free: coincident eye/center, a zero up, an up parallel to forward, or any
// non-finite field divides by zero and NaN-poisons the whole view matrix, and nothing
// downstream catches it 〜 invertMat4's det == 0.0f test is false for NaN and every
// frustum/cull compare is NaN-false, so the frame goes black. The viewState lane is
// latest-wins with no handshake, so one bad publish blackens every recorded frame until
// the producer happens to publish a good pose again (a producer that publishes its camera
// once at init never does). No in-tree producer degenerates; the seam is public, so the
// guard has to live at the publish, and lookAt stays branch-free.
// Three properties: every degenerate pose is dropped with the last accepted pose intact;
// a thousand valid poses (including one only ~0.057 deg off-parallel, just above the
// guard's epsilon floor) pass through byte-identical; and a rejection before any accepted
// publish leaves the lane unpublished so the renderer keeps its built-in camera.
// Headless, no device, deterministic. Exit 0 == pass.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <mimalloc.h>

#include "render_bridge/render_bridge.h" // private transport: bridge + acquire endpoint

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define SWEEP 1000u

// in:  out, eye/center/up (3 floats each), fovYDeg, seq
// out: fully written pose
// inv: memset first 〜 the seqlock lane copies object representation, so padding must be
//      deterministic for the byte-exact compares below.
static void pose_make(AnoViewState *out, const float eye[3], const float center[3],
                      const float up[3], float fovYDeg, uint64_t seq)
{
    memset(out, 0, sizeof *out);
    for (int k = 0; k < 3; k++) {
        out->eye[k]    = eye[k];
        out->center[k] = center[k];
        out->up[k]     = up[k];
    }
    out->fovYDeg = fovYDeg;
    out->seq     = seq;
}

// in:  eye, center
// out: up set 1e-3 rad off the eye->center axis (sin^2 == 1e-6, 100x the guard's 1e-8 floor)
// inv: must stay ACCEPTED 〜 this is the over-rejection probe.
static void near_parallel_up(const float eye[3], const float center[3], float up[3])
{
    float d[3] = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
    float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float f[3] = { d[0] / dl, d[1] / dl, d[2] / dl };
    float w[3] = { 0.0f, 1.0f, 0.0f };                    // any axis off f
    float pd = f[0] * w[0] + f[1] * w[1] + f[2] * w[2];
    float g[3] = { w[0] - pd * f[0], w[1] - pd * f[1], w[2] - pd * f[2] }; // g perp f
    float gl = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const float th = 1e-3f;
    for (int k = 0; k < 3; k++)
        up[k] = cosf(th) * f[k] + sinf(th) * (g[k] / gl);
}

int main(void)
{
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "heap creation");
    if (heap == NULL) return 1;

    AnoRenderBridge bridge; // stack: _Alignas on the lanes propagates
    CHECK(ano_render_bridge_init(&bridge, heap, 8, 8), "bridge init");
    if (failures) return 1;

    const float eye0[3]    = { 0.0f, 0.9f, 3.5f };
    const float center0[3] = { 0.0f, 0.15f, 0.0f };
    const float up0[3]     = { 0.0f, 1.0f, 0.0f };

    /* 1. Rejection: the last accepted pose stands through every degenerate publish. */

    AnoViewState good;
    pose_make(&good, eye0, center0, up0, 45.0f, 1u);
    ano_render_publish_view(&bridge, &good);

    AnoViewState got;
    memset(&got, 0, sizeof got);
    CHECK(ano_render_acquire_view(&bridge, &got), "acquire after the first valid publish");
    CHECK(memcmp(&got, &good, sizeof good) == 0, "acquired pose is the published one");

    // Each case starts from `good` and poisons exactly one thing (fwd == center0 - eye0).
    const float fwd[3] = { center0[0] - eye0[0], center0[1] - eye0[1], center0[2] - eye0[2] };
    enum { BAD = 10 };
    AnoViewState bad[BAD];
    const char  *why[BAD];
    for (uint32_t i = 0; i < BAD; i++) { bad[i] = good; bad[i].seq = 100u + i; }

    why[0] = "eye == center";
    for (int k = 0; k < 3; k++) bad[0].center[k] = eye0[k];
    why[1] = "zero up";
    for (int k = 0; k < 3; k++) bad[1].up[k] = 0.0f;
    why[2] = "up parallel to forward";
    for (int k = 0; k < 3; k++) bad[2].up[k] = fwd[k];
    why[3] = "up anti-parallel to forward";
    for (int k = 0; k < 3; k++) bad[3].up[k] = -fwd[k];
    why[4] = "NaN center";
    bad[4].center[1] = NAN;
    why[5] = "inf eye";
    bad[5].eye[2] = INFINITY;
    why[6] = "NaN up";
    bad[6].up[0] = NAN;
    why[7] = "fovY 0";
    bad[7].fovYDeg = 0.0f;
    why[8] = "fovY NaN";
    bad[8].fovYDeg = NAN;
    why[9] = "fovY 180";
    bad[9].fovYDeg = 180.0f;

    for (uint32_t i = 0; i < BAD; i++) {
        ano_render_publish_view(&bridge, &bad[i]);
        memset(&got, 0, sizeof got);
        if (!ano_render_acquire_view(&bridge, &got) || memcmp(&got, &good, sizeof good) != 0) {
            printf("  degenerate pose reached the lane: %s\n", why[i]);
            CHECK(false, "degenerate publish rejected, previous pose stands");
        }
    }

    /* 2. No-op on the valid path: over-rejection would black the frame just as hard. */

    uint32_t sweepErr = 0, sweepNear = 0;
    for (uint32_t i = 0; i < SWEEP; i++) {
        float a = (float)i * (6.2831853f / (float)SWEEP);
        float eye[3]    = { 4.0f * sinf(a), 0.5f + 2.0f * cosf(a * 0.5f), 4.0f * cosf(a) };
        float center[3] = { 0.1f * cosf(a), 0.15f, -0.1f * sinf(a) };
        float up[3]     = { 0.0f, 1.0f, 0.0f };
        if (i % 100u == 37u) { near_parallel_up(eye, center, up); sweepNear++; }
        AnoViewState v;
        pose_make(&v, eye, center, up, 30.0f + (float)(i % 80u), 1000u + i);
        ano_render_publish_view(&bridge, &v);
        AnoViewState out;
        memset(&out, 0, sizeof out);
        if (!ano_render_acquire_view(&bridge, &out) || memcmp(&out, &v, sizeof v) != 0)
            sweepErr++;
    }
    if (sweepErr != 0)
        printf("  %u of %u valid poses did not survive publish/acquire\n", sweepErr, SWEEP);
    CHECK(sweepErr == 0, "every valid orbit pose publishes through byte-identical");
    CHECK(sweepNear == 10u, "near-parallel poses exercised");

    /* 3. Cold start: a rejection before any accepted publish leaves the lane unpublished. */

    AnoRenderBridge cold;
    CHECK(ano_render_bridge_init(&cold, heap, 8, 8), "cold bridge init");

    AnoViewState degenerate;
    pose_make(&degenerate, eye0, eye0, up0, 45.0f, 1u); // eye == center
    ano_render_publish_view(&cold, &degenerate);

    AnoViewState untouched;
    memset(&untouched, 0xA5, sizeof untouched);
    CHECK(!ano_render_acquire_view(&cold, &untouched),
          "cold bridge stays unpublished after a rejected pose (renderer keeps its fallback)");
    uint32_t clobbered = 0;
    for (size_t i = 0; i < sizeof untouched; i++)
        if (((const unsigned char *)&untouched)[i] != 0xA5u) clobbered++;
    CHECK(clobbered == 0, "failed acquire leaves the destination untouched");

    // The rejected publish must not wedge the lane: a good pose still lands.
    AnoViewState first;
    pose_make(&first, eye0, center0, up0, 60.0f, 2u);
    ano_render_publish_view(&cold, &first);
    memset(&got, 0, sizeof got);
    CHECK(ano_render_acquire_view(&cold, &got) && memcmp(&got, &first, sizeof first) == 0,
          "a valid pose after a rejection publishes normally");

    ano_render_bridge_destroy(&cold);
    ano_render_bridge_destroy(&bridge);
    mi_heap_destroy(heap);

    if (failures == 0) { printf("anotest_cameraposeguard: all checks passed\n"); return 0; }
    printf("anotest_cameraposeguard: %d check(s) failed\n", failures);
    return 1;
}
