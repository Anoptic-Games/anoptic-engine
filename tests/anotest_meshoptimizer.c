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

static void test_vertex_cache_optimization() {
    printf("Running test_vertex_cache_optimization...\n");

    // Simple grid: 2 triangles (4 vertices)
    // Triangles: [0, 1, 2], [2, 1, 3]
    uint32_t indices[] = {
        0, 1, 2,
        2, 1, 3
    };
    uint32_t optimized[6];

    ano_optimize_vertex_cache(optimized, indices, 6, 4);

    // Make sure we still have 6 indices and they represent the same set of triangles
    int found_t1 = 0;
    int found_t2 = 0;
    for (int i = 0; i < 6; i += 3) {
        uint32_t a = optimized[i];
        uint32_t b = optimized[i+1];
        uint32_t c = optimized[i+2];
        
        // Check if it's triangle (0,1,2) in some rotation/permutation
        if ((a == 0 && b == 1 && c == 2) || (a == 1 && b == 2 && c == 0) || (a == 2 && b == 0 && c == 1) ||
            (a == 0 && b == 2 && c == 1) || (a == 1 && b == 0 && c == 2) || (a == 2 && b == 1 && c == 0)) {
            found_t1 = 1;
        }
        // Check if it's triangle (2,1,3) in some rotation/permutation
        if ((a == 2 && b == 1 && c == 3) || (a == 1 && b == 3 && c == 2) || (a == 3 && b == 2 && c == 1) ||
            (a == 2 && b == 3 && c == 1) || (a == 1 && b == 2 && c == 3) || (a == 3 && b == 1 && c == 2)) {
            found_t2 = 1;
        }
    }
    assert(found_t1 && found_t2);

    // Test in-place optimization
    uint32_t in_place[6] = { 0, 1, 2, 2, 1, 3 };
    ano_optimize_vertex_cache(in_place, in_place, 6, 4);
    
    found_t1 = 0;
    found_t2 = 0;
    for (int i = 0; i < 6; i += 3) {
        uint32_t a = in_place[i];
        uint32_t b = in_place[i+1];
        uint32_t c = in_place[i+2];
        if ((a == 0 && b == 1 && c == 2) || (a == 1 && b == 2 && c == 0) || (a == 2 && b == 0 && c == 1) ||
            (a == 0 && b == 2 && c == 1) || (a == 1 && b == 0 && c == 2) || (a == 2 && b == 1 && c == 0)) {
            found_t1 = 1;
        }
        if ((a == 2 && b == 1 && c == 3) || (a == 1 && b == 3 && c == 2) || (a == 3 && b == 2 && c == 1) ||
            (a == 2 && b == 3 && c == 1) || (a == 1 && b == 2 && c == 3) || (a == 3 && b == 1 && c == 2)) {
            found_t2 = 1;
        }
    }
    assert(found_t1 && found_t2);
}

// Every output triangle must reference in-range, distinct vertices (no degenerates).
static void validate_indices(const uint32_t* idx, size_t count, uint32_t vertex_count) {
    assert(count % 3 == 0);
    for (size_t t = 0; t < count; t += 3) {
        uint32_t a = idx[t], b = idx[t+1], c = idx[t+2];
        assert(a < vertex_count && b < vertex_count && c < vertex_count);
        assert(a != b && b != c && a != c);
    }
}

static void test_simplify_scale() {
    printf("Running test_simplify_scale...\n");

    // Largest axis extent: x spans 2, y spans 1, z spans 0 -> 2.
    float positions[] = {
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    float scale = ano_simplify_scale(positions, 3, sizeof(float) * 3);
    assert(fabs(scale - 2.0f) < 1e-5f);

    assert(ano_simplify_scale(positions, 0, sizeof(float) * 3) == 0.0f);
}

static void test_simplify_passthrough() {
    printf("Running test_simplify_passthrough...\n");

    // target_index_count >= index_count returns the input unchanged.
    uint32_t indices[] = { 0, 1, 2 };
    float positions[] = { 0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f };
    uint32_t out[3] = { 99, 99, 99 };
    float err = -1.0f;

    size_t r = ano_simplify(out, indices, 3, positions, 3, sizeof(float) * 3, 3, 0.01f, &err);
    assert(r == 3);
    assert(out[0] == 0 && out[1] == 1 && out[2] == 2);
    assert(err == 0.0f);

    // Passthrough still drops degenerate (coincident-position) triangles: one real + one with two
    // corners at the same position, target >= index_count -> only the real triangle survives.
    uint32_t dindices[] = { 0, 1, 2,  0, 3, 1 };  // vertex 3 coincides with vertex 0
    float dpositions[] = { 0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f };
    uint32_t dout[6];
    size_t dr = ano_simplify(dout, dindices, 6, dpositions, 4, sizeof(float) * 3, 6, 0.01f, NULL);
    assert(dr == 3);
    validate_indices(dout, dr, 4);
}

static void test_simplify_degenerate_input() {
    printf("Running test_simplify_degenerate_input...\n");

    // One real triangle + two index-degenerate triangles: must terminate and emit a valid mesh.
    uint32_t indices[] = { 0, 1, 2,  2, 2, 3,  3, 4, 3 };
    float positions[] = {
        0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 1.0f,1.0f,0.0f, 2.0f,2.0f,0.0f
    };
    uint32_t out[9];

    size_t r = ano_simplify(out, indices, 9, positions, 5, sizeof(float) * 3, 3, 0.5f, NULL);
    validate_indices(out, r, 5);
    assert(r == 3); // only the one non-degenerate triangle survives
}

// Build an n x n grid of vertices in the z=0 plane, two triangles per cell (CCW, +Z normals).
static size_t build_grid(uint32_t n, float* positions, uint32_t* indices) {
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x) {
            positions[(y*n+x)*3+0] = (float)x;
            positions[(y*n+x)*3+1] = (float)y;
            positions[(y*n+x)*3+2] = 0.0f;
        }
    size_t k = 0;
    for (uint32_t y = 0; y < n - 1; ++y)
        for (uint32_t x = 0; x < n - 1; ++x) {
            uint32_t i00 = y*n+x, i10 = y*n+x+1, i01 = (y+1)*n+x, i11 = (y+1)*n+x+1;
            indices[k++] = i00; indices[k++] = i10; indices[k++] = i11;
            indices[k++] = i00; indices[k++] = i11; indices[k++] = i01;
        }
    return k;
}

static void test_simplify_grid() {
    printf("Running test_simplify_grid...\n");

    enum { N = 8 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    size_t ic = build_grid(N, positions, indices);

    size_t target = (ic / 2 / 3) * 3;  // ~50%
    uint32_t out[(N-1)*(N-1)*2*3];
    float err = -1.0f;

    size_t r = ano_simplify(out, indices, ic, positions, N*N, sizeof(float) * 3, target, 0.05f, &err);

    assert(r % 3 == 0);
    assert(r > 0 && r < ic);   // reduced, non-empty
    assert(r <= target);       // flat mesh: the count budget is reached
    validate_indices(out, r, N*N);
    assert(err >= 0.0f && err < 0.1f); // a perfectly flat grid simplifies at ~zero error

    // The contract allows destination == indices (in-place).
    size_t r2 = ano_simplify(indices, indices, ic, positions, N*N, sizeof(float) * 3, target, 0.05f, NULL);
    assert(r2 == r);
    validate_indices(indices, r2, N*N);
}

static void test_simplify_error_budget() {
    printf("Running test_simplify_error_budget...\n");

    // A curved (bumpy) grid: unlike a flat grid, collapses carry real quadric error, so target_error
    // actually binds. This exercises the error-accumulation path the flat-grid test cannot.
    enum { N = 10 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    for (uint32_t y = 0; y < N; ++y)
        for (uint32_t x = 0; x < N; ++x) {
            positions[(y*N+x)*3+0] = (float)x;
            positions[(y*N+x)*3+1] = (float)y;
            positions[(y*N+x)*3+2] = 0.6f * sinf((float)x * 0.9f) * cosf((float)y * 0.9f);
        }
    size_t ic = 0;
    for (uint32_t y = 0; y < N - 1; ++y)
        for (uint32_t x = 0; x < N - 1; ++x) {
            uint32_t i00 = y*N+x, i10 = y*N+x+1, i01 = (y+1)*N+x, i11 = (y+1)*N+x+1;
            indices[ic++] = i00; indices[ic++] = i10; indices[ic++] = i11;
            indices[ic++] = i00; indices[ic++] = i11; indices[ic++] = i01;
        }

    size_t target = (ic / 4 / 3) * 3;  // 25%
    uint32_t out_g[(N-1)*(N-1)*2*3], out_s[(N-1)*(N-1)*2*3];
    float err_g = -1.0f, err_s = -1.0f;

    // Generous budget reaches the count target; tiny budget stops short to preserve the shape.
    size_t rg = ano_simplify(out_g, indices, ic, positions, N*N, sizeof(float)*3, target, 1.0f, &err_g);
    size_t rs = ano_simplify(out_s, indices, ic, positions, N*N, sizeof(float)*3, target, 1e-4f, &err_s);

    validate_indices(out_g, rg, N*N);
    validate_indices(out_s, rs, N*N);
    assert(rg <= target);          // generous: count budget binds
    assert(rs > target);           // strict: error budget binds first, stays above the count target
    assert(rg < ic && rs < ic);    // both still reduce
    assert(err_g >= 0.0f && err_s >= 0.0f);
}

// Longest edge over an index buffer, in object units. Helper for the guard regression below.
static float max_edge_len(const uint32_t* idx, size_t r, const float* pos) {
    float mx = 0.0f;
    for (size_t t = 0; t < r; t += 3)
        for (int e = 0; e < 3; ++e) {
            const float* a = &pos[idx[t + e]*3];
            const float* b = &pos[idx[t + (e + 1) % 3]*3];
            float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
            float d = sqrtf(dx*dx + dy*dy + dz*dz);
            if (d > mx) mx = d;
        }
    return mx;
}

// Guarded path (ano_simplify_ex, edge_len_factor > 0): the in-plane growth cap must stop a coplanar
// "bridge" triangle — the Sponza courtyard-floor pathology. A coarse flat grid decimated hard bridges
// corner-to-corner with the guard OFF (a single triangle spanning most of the model, longer than any
// real edge); with the guard ON no surviving edge may span the surface, even if that means the count
// target is missed. Fails if the guard is ever removed (guarded output would then bridge like the
// unguarded one). Thresholds have wide margins over the measured 1.18-1.41x (off) / <=0.35x (on).
static void test_simplify_guard_bridge() {
    printf("Running test_simplify_guard_bridge...\n");

    enum { N = 12 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    size_t ic = build_grid(N, positions, indices);
    float extent = ano_simplify_scale(positions, N*N, sizeof(float)*3);
    size_t target = (ic / 8 / 3) * 3;  // decimate hard (~1/8) so an unguarded collapse bridges

    uint32_t guard_off[(N-1)*(N-1)*2*3], guard_on[(N-1)*(N-1)*2*3];
    size_t r_off = ano_simplify_ex(guard_off, indices, ic, positions, N*N, sizeof(float)*3,
                                   target, 0.05f, 0.0f, NULL);
    size_t r_on  = ano_simplify_ex(guard_on, indices, ic, positions, N*N, sizeof(float)*3,
                                   target, 0.05f, ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT, NULL);
    validate_indices(guard_off, r_off, N*N);
    validate_indices(guard_on,  r_on,  N*N);

    // Guard OFF reproduces the pathology: a triangle edge spanning most of the model.
    assert(max_edge_len(guard_off, r_off, positions) > 0.75f * extent);
    // Guard ON: no surviving edge spans the surface, yet the mesh still coarsens.
    assert(max_edge_len(guard_on, r_on, positions) < 0.5f * extent);
    assert(r_on > 0 && r_on < ic);
}

int main() {
    test_meshlet_bounds_calculation();
    test_degenerate_triangles();
    test_meshlet_limits();
    test_bounds_checks();
    test_vertex_cache_optimization();
    test_simplify_scale();
    test_simplify_passthrough();
    test_simplify_degenerate_input();
    test_simplify_grid();
    test_simplify_error_budget();
    test_simplify_guard_bridge();
    printf("All tests passed successfully!\n");
    return 0;
}

