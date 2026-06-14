/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <mesh/ano_meshoptimizer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static void test_meshlet_bounds_calculation() {
    printf("Running test_meshlet_bounds_calculation...\n");

    // Let's create a single triangle
    uint32_t meshlet_vertices[] = { 0, 1, 2 };
    uint8_t meshlet_triangles[] = { 0, 1, 2 };
    
    // Vertex positions: (0,0,0), (1,0,0), (0,1,0)
    float vertex_positions[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };

    ano_meshlet_bounds_gpu_t bounds = ano_compute_meshlet_bounds(
        meshlet_vertices,
        meshlet_triangles,
        1,
        vertex_positions,
        3,
        sizeof(float) * 3
    );

    // Ritter's sphere should enclose the triangle vertices:
    // Furthest points in (0,0,0)-(1,0,0)-(0,1,0)
    // Distance between (1,0,0) and (0,1,0) is sqrt(2) ~ 1.4142
    // Center of sphere should be midpoint: (0.5, 0.5, 0.0)
    // Radius should be sqrt(2)/2 ~ 0.7071
    assert(fabs(bounds.center[0] - 0.5f) < 1e-4f);
    assert(fabs(bounds.center[1] - 0.5f) < 1e-4f);
    assert(fabs(bounds.center[2] - 0.0f) < 1e-4f);
    assert(fabs(bounds.radius - 0.70710678f) < 1e-4f);

    // Normal is (0,0,1). The cone axis should be (0,0,1).
    assert(fabs(bounds.cone_axis[0] - 0.0f) < 1e-4f);
    assert(fabs(bounds.cone_axis[1] - 0.0f) < 1e-4f);
    assert(fabs(bounds.cone_axis[2] - 1.0f) < 1e-4f);
    assert(bounds.cone_cutoff > 0.0f); // Should be 1.0f - epsilon
}

static void test_degenerate_triangles() {
    printf("Running test_degenerate_triangles...\n");

    // Triangles: [0, 1, 2] (normal), [2, 2, 3] (degenerate - duplicate indices), [3, 4, 3] (degenerate - duplicate indices)
    uint32_t indices[] = {
        0, 1, 2,
        2, 2, 3,
        3, 4, 3
    };

    // Worst-case meshlets bound
    size_t bound = ano_build_meshlets_bound(9, 64, 126);
    assert(bound > 0);

    ano_meshlet_t* meshlets = calloc(bound, sizeof(ano_meshlet_t));
    uint32_t* meshlet_vertices = calloc(bound * 64, sizeof(uint32_t));
    uint8_t* meshlet_triangles = calloc(bound * 126 * 3, sizeof(uint8_t));

    size_t meshlet_count = ano_build_meshlets(
        meshlets,
        meshlet_vertices,
        meshlet_triangles,
        indices,
        9,
        64,
        126
    );

    assert(meshlet_count == 1);
    
    // Vertices should be: 0, 1, 2, 3, 4 (only 5 vertices total, no duplicates)
    assert(meshlets[0].vertex_count == 5);
    assert(meshlet_vertices[0] == 0);
    assert(meshlet_vertices[1] == 1);
    assert(meshlet_vertices[2] == 2);
    assert(meshlet_vertices[3] == 3);
    assert(meshlet_vertices[4] == 4);

    // Triangles local indices should be:
    // [0, 1, 2], [2, 2, 3], [3, 4, 3]
    assert(meshlet_triangles[0] == 0);
    assert(meshlet_triangles[1] == 1);
    assert(meshlet_triangles[2] == 2);
    
    assert(meshlet_triangles[3] == 2);
    assert(meshlet_triangles[4] == 2);
    assert(meshlet_triangles[5] == 3);
    
    assert(meshlet_triangles[6] == 3);
    assert(meshlet_triangles[7] == 4);
    assert(meshlet_triangles[8] == 3);

    free(meshlets);
    free(meshlet_vertices);
    free(meshlet_triangles);
}

static void test_meshlet_limits() {
    printf("Running test_meshlet_limits...\n");

    // 10 triangles with no shared vertices
    // 30 indices in total
    uint32_t indices[30];
    for (uint32_t i = 0; i < 30; ++i) {
        indices[i] = i;
    }

    // Set max_vertices = 9 (max 3 triangles per meshlet with 0 vertex reuse)
    // max_triangles = 5
    size_t bound = ano_build_meshlets_bound(30, 9, 5);
    assert(bound > 0);

    ano_meshlet_t* meshlets = calloc(bound, sizeof(ano_meshlet_t));
    uint32_t* meshlet_vertices = calloc(bound * 9, sizeof(uint32_t));
    uint8_t* meshlet_triangles = calloc(bound * 5 * 3, sizeof(uint8_t));

    size_t meshlet_count = ano_build_meshlets(
        meshlets,
        meshlet_vertices,
        meshlet_triangles,
        indices,
        30,
        9,
        5
    );

    // With max_vertices = 9, we can fit exactly 3 triangles in each meshlet (needs 9 vertices).
    // The 4th triangle needs 3 more, which would exceed 9.
    // So it must split.
    // 10 triangles total / 3 triangles per meshlet = 4 meshlets (3, 3, 3, 1)
    assert(meshlet_count == 4);
    assert(meshlets[0].vertex_count == 9);
    assert(meshlets[0].triangle_count == 3);
    assert(meshlets[3].vertex_count == 3);
    assert(meshlets[3].triangle_count == 1);

    free(meshlets);
    free(meshlet_vertices);
    free(meshlet_triangles);
}

static void test_bounds_checks() {
    printf("Running test_bounds_checks...\n");

    // Safe indices
    uint32_t indices[] = { 0, 1, 2, 3 }; // non-multiple of 3

    size_t bound = ano_build_meshlets_bound(4, 300, 300); // parameters exceed 256
    assert(bound > 0);

    ano_meshlet_t* meshlets = calloc(bound, sizeof(ano_meshlet_t));
    uint32_t* meshlet_vertices = calloc(bound * 256, sizeof(uint32_t));
    uint8_t* meshlet_triangles = calloc(bound * 256 * 3, sizeof(uint8_t));

    size_t meshlet_count = ano_build_meshlets(
        meshlets,
        meshlet_vertices,
        meshlet_triangles,
        indices,
        4, // 4 indices (will round down to 3)
        300,
        300
    );

    assert(meshlet_count == 1);
    assert(meshlets[0].vertex_count == 3);
    assert(meshlets[0].triangle_count == 1);

    free(meshlets);
    free(meshlet_vertices);
    free(meshlet_triangles);
}

int main() {
    test_meshlet_bounds_calculation();
    test_degenerate_triangles();
    test_meshlet_limits();
    test_bounds_checks();
    printf("All tests passed successfully!\n");
    return 0;
}
