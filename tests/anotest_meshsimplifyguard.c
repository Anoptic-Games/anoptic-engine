/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_simplify collapse validity on the guards-off path. ano_simplify delegates to
// ano_simplify_ex with edge_len_factor 0, and ano_simplify_ex runs its link/tetra collapse
// exclusion only when the growth guards are on (maxEdge2 != FLT_MAX at
// ano_meshoptimizer.c:955), so the base public API executes topologically illegal collapses
// the guards-on twin rejects (docs/BUGS.md, Mesh / Implementation). On a 3-triangle cone fan
// one rim collapse rewrites a surviving triangle onto the remaining rim pair and the output
// is the same face twice: two coincident opposite-wound triangles, a zero-volume sack that
// z-fights and defeats the header's degenerate-dropping promise on its way into a LOD chain.
// Controls pin passthrough, a legal border collapse, and the guards-on twin refusing this
// exact collapse, so a reject-everything fix cannot pass. Deterministic: the simplifier has
// no RNG and the cheapest candidate is unique. No device, no allocation beyond the module's
// own. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include <mesh/ano_meshoptimizer.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Inputs: index buffer idx of n indices (whole triangles). Output: 1 if any two triangles
// share the same unordered vertex triple, else 0. Invariant: n is a multiple of 3.
static int has_duplicate_face(const uint32_t* idx, size_t n) {
    for (size_t i = 0; i + 3 <= n; i += 3) {
        for (size_t k = i + 3; k + 3 <= n; k += 3) {
            uint32_t a[3] = { idx[i], idx[i+1], idx[i+2] };
            uint32_t b[3] = { idx[k], idx[k+1], idx[k+2] };
            for (int x = 0; x < 2; ++x) for (int y = 0; y < 2 - x; ++y) {
                if (a[y] > a[y+1]) { uint32_t t = a[y]; a[y] = a[y+1]; a[y+1] = t; }
                if (b[y] > b[y+1]) { uint32_t t = b[y]; b[y] = b[y+1]; b[y+1] = t; }
            }
            if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) return 1;
        }
    }
    return 0;
}

int main(void)
{
    // the trigger mesh: an open cone fan 〜 apex 0, rim 1 (b), 2 (v, 20 deg from b), 3 (j opposite);
    // every spoke edge is manifold, the rim is border, and any rim-onto-rim collapse leaves the
    // two survivors spanning the same {apex, rim, rim} triple
    const float cone_pos[4 * 3] = {
        0.0f,    0.0f,    2.0f,   // 0 apex
        1.0f,    0.0f,    0.0f,   // 1 b
        0.9397f, 0.3420f, 0.0f,   // 2 v
       -1.0f,    0.0f,    0.0f,   // 3 j
    };
    const uint32_t cone_idx[9] = { 0, 1, 2,   0, 2, 3,   0, 3, 1 };

    // control: passthrough (target >= input) returns the clean fan intact
    {
        uint32_t dst[9] = { 0 };
        size_t n = ano_simplify(dst, cone_idx, 9, cone_pos, 4, 12, 9, 0.5f, NULL);
        CHECK(n == 9, "passthrough keeps all 9 indices");
        CHECK(!has_duplicate_face(dst, n), "passthrough emits no duplicate face");
    }

    // control: a legal collapse still simplifies 〜 flat strip, vertex 1 collinear on the border,
    // so the simplifier is live and a reject-everything fix cannot pass
    {
        const float strip_pos[4 * 3] = { 0,0,0,  1,0,0,  2,0,0,  0,1,0 };
        const uint32_t strip_idx[6] = { 0, 1, 3,   1, 2, 3 };
        uint32_t dst[6] = { 0 };
        float err = -1.0f;
        size_t n = ano_simplify(dst, strip_idx, 6, strip_pos, 4, 12, 3, 0.1f, &err);
        CHECK(n == 3, "collinear border vertex collapses to one tri");
        CHECK(!has_duplicate_face(dst, n), "legal collapse emits no duplicate face");
        CHECK(err <= 0.01f, "collinear collapse reports ~zero error");
    }

    // control: the guards-on twin refuses this exact collapse (its link condition names it
    // illegal), returning the fan untouched with no duplicates
    {
        uint32_t dst[9] = { 0 };
        size_t n = ano_simplify_ex(dst, cone_idx, 9, cone_pos, 4, 12, 6, 0.5f,
                                   ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT, NULL);
        CHECK(n % 3 == 0, "guards-on returns whole triangles");
        CHECK(!has_duplicate_face(dst, n), "guards-on emits no duplicate face");
    }

    // trigger: base ano_simplify, one collapse (9 -> 6) 〜 the rim collapse duplicates a face:
    // T2 (0,2,3) rewrites onto (0,2,1) against surviving T1 (0,1,2), a coincident opposite-wound
    // pair with zero enclosed volume
    {
        uint32_t dst[9] = { 0 };
        float err = -1.0f;
        size_t n = ano_simplify(dst, cone_idx, 9, cone_pos, 4, 12, 6, 0.5f, &err);
        CHECK(n % 3 == 0, "guards-off returns whole triangles");
        for (size_t i = 0; i < n; ++i)
            CHECK(dst[i] < 4, "guards-off emits only source vertex ids");
        CHECK(!has_duplicate_face(dst, n), "guards-off emits no duplicate face (link condition)");
    }

    if (failures) {
        printf("anotest_meshsimplifyguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_meshsimplifyguard: all passed\n");
    return 0;
}
