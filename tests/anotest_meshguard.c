/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_build_meshlets / ano_build_meshlets_bound pair agreement. The header contract
// says to size the output buffers from bound(), and bound() rejects max_vertices < 3 and
// max_triangles < 1 by returning 0 〜 but build() only clamps those params from above (> 256)
// and packs anyway (docs/BUGS.md, Mesh / Implementation, ano_meshoptimizer.c:282). So
// build(indices {0,1,2}, max_vertices 2) returns 1 meshlet with vertex_count 3 where bound()
// returned 0: a caller following the contract allocated zero-length arrays and just took a
// heap overwrite, and the emitted meshlet breaks the max_vertices promise every consumer of
// the meshlet_vertices layout relies on. The triggers run with deliberately OVERSIZED buffers
// so the lie is observed as wrong values, not as corruption inside this test. Controls pin the
// valid-config path (a sane build packs correctly) so a reject-everything fix cannot pass.
// No device, no allocation, deterministic. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <mesh/ano_meshoptimizer.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // control: a contract-clean config packs correctly (the packer is live, not reject-everything)
    {
        uint32_t indices[6] = { 0, 1, 2,  2, 1, 3 };
        size_t promised = ano_build_meshlets_bound(6, 64, 126);
        CHECK(promised >= 1, "bound sizes a valid config");

        ano_meshlet_t meshlets[4] = { 0 };
        uint32_t mv[4 * 64] = { 0 };
        uint8_t mt[4 * 126 * 3] = { 0 };
        size_t built = ano_build_meshlets(meshlets, mv, mt, indices, 6, 64, 126);

        CHECK(built == 1, "two shared-edge tris pack into one meshlet");
        CHECK(built <= promised, "valid build stays within bound's sizing");
        CHECK(meshlets[0].vertex_count == 4, "meshlet holds the 4 unique verts");
        CHECK(meshlets[0].triangle_count == 2, "meshlet holds both tris");
        // local indices decode back to the global triangles
        CHECK(mv[mt[0]] == 0 && mv[mt[1]] == 1 && mv[mt[2]] == 2, "tri 0 decodes");
        CHECK(mv[mt[3]] == 2 && mv[mt[4]] == 1 && mv[mt[5]] == 3, "tri 1 decodes");
    }

    // control: the bounds the sizing twin DOES enforce
    CHECK(ano_build_meshlets_bound(3, 2, 64) == 0, "bound rejects max_vertices 2 (existing guard)");
    CHECK(ano_build_meshlets_bound(3, 64, 0) == 0, "bound rejects max_triangles 0 (existing guard)");

    // trigger: max_vertices 2 cannot hold a triangle, so build must emit nothing 〜 exactly
    // as many meshlets as bound() sized the buffers for 〜 not pack one that breaks the limit
    {
        uint32_t indices[3] = { 0, 1, 2 };
        size_t promised = ano_build_meshlets_bound(3, 2, 64);   // 0

        // oversized on purpose: contract sizing is promised == 0, real callers get the overwrite
        ano_meshlet_t meshlets[4] = { 0 };
        uint32_t mv[64] = { 0 };
        uint8_t mt[64] = { 0 };
        size_t built = ano_build_meshlets(meshlets, mv, mt, indices, 3, 2, 64);

        CHECK(built <= promised, "build emits no meshlet bound() did not size for (max_vertices 2)");
        if (built > 0)
            CHECK(meshlets[0].vertex_count <= 2, "emitted meshlet honors max_vertices 2");
    }

    // trigger: max_triangles 0 admits no triangle at all, same missing lower-bound guard
    {
        uint32_t indices[3] = { 0, 1, 2 };
        size_t promised = ano_build_meshlets_bound(3, 64, 0);   // 0

        ano_meshlet_t meshlets[4] = { 0 };
        uint32_t mv[64] = { 0 };
        uint8_t mt[64] = { 0 };
        size_t built = ano_build_meshlets(meshlets, mv, mt, indices, 3, 64, 0);

        CHECK(built <= promised, "build emits no meshlet bound() did not size for (max_triangles 0)");
        if (built > 0)
            CHECK(meshlets[0].triangle_count == 0, "emitted meshlet honors max_triangles 0");
    }

    if (failures) {
        printf("anotest_meshguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_meshguard: all passed\n");
    return 0;
}
