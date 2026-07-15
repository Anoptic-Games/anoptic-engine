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

    // One triangle
    uint32_t meshlet_vertices[] = { 0, 1, 2 };
    uint8_t meshlet_triangles[] = { 0, 1, 2 };
    
    // Positions: (0,0,0), (1,0,0), (0,1,0)
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

    // Ritter: center (0.5,0.5,0), radius sqrt(2)/2
    assert(fabs(bounds.center[0] - 0.5f) < 1e-4f);
    assert(fabs(bounds.center[1] - 0.5f) < 1e-4f);
    assert(fabs(bounds.center[2] - 0.0f) < 1e-4f);
    assert(fabs(bounds.radius - 0.70710678f) < 1e-4f);

    // Cone axis (0,0,1). Flat triangle -> cutoff == 0.
    assert(fabs(bounds.cone_axis[0] - 0.0f) < 1e-4f);
    assert(fabs(bounds.cone_axis[1] - 0.0f) < 1e-4f);
    assert(fabs(bounds.cone_axis[2] - 1.0f) < 1e-4f);
    // meshopt: cutoff = sin(max normal deviation). Zero spread -> 0.
    assert(bounds.cone_cutoff >= 0.0f && bounds.cone_cutoff < 1e-4f);
}

static void test_degenerate_triangles() {
    printf("Running test_degenerate_triangles...\n");

    // [0,1,2] normal, [2,2,3] and [3,4,3] index-degenerate
    uint32_t indices[] = {
        0, 1, 2,
        2, 2, 3,
        3, 4, 3
    };

    // Worst-case meshlet bound
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
    
    // Unique verts 0..4
    assert(meshlets[0].vertex_count == 5);
    assert(meshlet_vertices[0] == 0);
    assert(meshlet_vertices[1] == 1);
    assert(meshlet_vertices[2] == 2);
    assert(meshlet_vertices[3] == 3);
    assert(meshlet_vertices[4] == 4);

    // Local indices: [0,1,2], [2,2,3], [3,4,3]
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

    // 10 tris, no shared verts (30 indices)
    uint32_t indices[30];
    for (uint32_t i = 0; i < 30; ++i) {
        indices[i] = i;
    }

    // max_vertices=9 (3 tris/meshlet), max_triangles=5
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

    // 10 tris / 3 per meshlet -> 4 meshlets (3,3,3,1)
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

    uint32_t indices[] = { 0, 1, 2, 3 }; // non-multiple of 3

    size_t bound = ano_build_meshlets_bound(4, 300, 300); // params exceed 256
    assert(bound > 0);

    ano_meshlet_t* meshlets = calloc(bound, sizeof(ano_meshlet_t));
    uint32_t* meshlet_vertices = calloc(bound * 256, sizeof(uint32_t));
    uint8_t* meshlet_triangles = calloc(bound * 256 * 3, sizeof(uint8_t));

    size_t meshlet_count = ano_build_meshlets(
        meshlets,
        meshlet_vertices,
        meshlet_triangles,
        indices,
        4, // floors to 3
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

    // 2 tris: [0,1,2], [2,1,3]
    uint32_t indices[] = {
        0, 1, 2,
        2, 1, 3
    };
    uint32_t optimized[6];

    ano_optimize_vertex_cache(optimized, indices, 6, 4);

    // Same two triangles (any winding/rotation)
    int found_t1 = 0;
    int found_t2 = 0;
    for (int i = 0; i < 6; i += 3) {
        uint32_t a = optimized[i];
        uint32_t b = optimized[i+1];
        uint32_t c = optimized[i+2];
        
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

    // In-place
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

// Output tris: in-range, distinct verts (no degenerates).
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

    // Largest axis extent: x=2
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

    // target_index_count >= index_count -> input unchanged
    uint32_t indices[] = { 0, 1, 2 };
    float positions[] = { 0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f };
    uint32_t out[3] = { 99, 99, 99 };
    float err = -1.0f;

    size_t r = ano_simplify(out, indices, 3, positions, 3, sizeof(float) * 3, 3, 0.01f, &err);
    assert(r == 3);
    assert(out[0] == 0 && out[1] == 1 && out[2] == 2);
    assert(err == 0.0f);

    // Passthrough still drops coincident-position degenerates.
    uint32_t dindices[] = { 0, 1, 2,  0, 3, 1 };  // vertex 3 coincides with vertex 0
    float dpositions[] = { 0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f };
    uint32_t dout[6];
    size_t dr = ano_simplify(dout, dindices, 6, dpositions, 4, sizeof(float) * 3, 6, 0.01f, NULL);
    assert(dr == 3);
    validate_indices(dout, dr, 4);
}

static void test_simplify_degenerate_input() {
    printf("Running test_simplify_degenerate_input...\n");

    // One real + two index-degenerate tris: terminates with a valid mesh.
    uint32_t indices[] = { 0, 1, 2,  2, 2, 3,  3, 4, 3 };
    float positions[] = {
        0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 1.0f,1.0f,0.0f, 2.0f,2.0f,0.0f
    };
    uint32_t out[9];

    size_t r = ano_simplify(out, indices, 9, positions, 5, sizeof(float) * 3, 3, 0.5f, NULL);
    validate_indices(out, r, 5);
    assert(r == 3); // only the non-degenerate triangle
}

// n x n grid in z=0, two CCW tris per cell (+Z normals).
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
    assert(r <= target);       // flat: count budget binds
    validate_indices(out, r, N*N);
    assert(err >= 0.0f && err < 0.1f); // flat grid ~zero error

    // destination may alias indices
    size_t r2 = ano_simplify(indices, indices, ic, positions, N*N, sizeof(float) * 3, target, 0.05f, NULL);
    assert(r2 == r);
    validate_indices(indices, r2, N*N);
}

static void test_simplify_error_budget() {
    printf("Running test_simplify_error_budget...\n");

    // Bumpy grid: target_error binds (flat grid cannot exercise this path).
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

    // Generous budget hits count target. Tiny budget stops early.
    size_t rg = ano_simplify(out_g, indices, ic, positions, N*N, sizeof(float)*3, target, 1.0f, &err_g);
    size_t rs = ano_simplify(out_s, indices, ic, positions, N*N, sizeof(float)*3, target, 1e-4f, &err_s);

    validate_indices(out_g, rg, N*N);
    validate_indices(out_s, rs, N*N);
    assert(rg <= target);          // generous: count binds
    assert(rs > target);           // strict: error binds first
    assert(rg < ic && rs < ic);    // both reduce
    assert(err_g >= 0.0f && err_s >= 0.0f);
}

// Longest edge over an index buffer, object units.
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

// Guarded path: edge_len_factor > 0 must stop coplanar bridge triangles.
static void test_simplify_guard_bridge() {
    printf("Running test_simplify_guard_bridge...\n");

    enum { N = 12 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    size_t ic = build_grid(N, positions, indices);
    float extent = ano_simplify_scale(positions, N*N, sizeof(float)*3);
    size_t target = (ic / 8 / 3) * 3;  // hard decimate (~1/8)

    uint32_t guard_off[(N-1)*(N-1)*2*3], guard_on[(N-1)*(N-1)*2*3];
    size_t r_off = ano_simplify_ex(guard_off, indices, ic, positions, N*N, sizeof(float)*3,
                                   target, 0.05f, 0.0f, NULL);
    size_t r_on  = ano_simplify_ex(guard_on, indices, ic, positions, N*N, sizeof(float)*3,
                                   target, 0.05f, ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT, NULL);
    validate_indices(guard_off, r_off, N*N);
    validate_indices(guard_on,  r_on,  N*N);

    // OFF: edge spans most of the model. ON: no surface-spanning edge.
    assert(max_edge_len(guard_off, r_off, positions) > 0.75f * extent);
    assert(max_edge_len(guard_on, r_on, positions) < 0.5f * extent);
    assert(r_on > 0 && r_on < ic);
}

// Max radius sqrt(x^2+z^2) over output verts with y >= ymin.
static float max_radius_above(const uint32_t* idx, size_t r, const float* pos, float ymin) {
    float mx = 0.0f;
    for (size_t i = 0; i < r; ++i) {
        const float* p = &pos[idx[i]*3];
        if (p[1] >= ymin) { float rad = sqrtf(p[0]*p[0] + p[2]*p[2]); if (rad > mx) mx = rad; }
    }
    return mx;
}

// True if any two output tris share the same unordered vertex set.
static int has_dup_face(const uint32_t* idx, size_t r) {
    size_t nt = r / 3;
    for (size_t i = 0; i < nt; ++i) {
        uint32_t a0=idx[i*3], a1=idx[i*3+1], a2=idx[i*3+2];
        if (a0>a1){uint32_t t=a0;a0=a1;a1=t;} if (a1>a2){uint32_t t=a1;a1=a2;a2=t;} if (a0>a1){uint32_t t=a0;a0=a1;a1=t;}
        for (size_t j = i+1; j < nt; ++j) {
            uint32_t b0=idx[j*3], b1=idx[j*3+1], b2=idx[j*3+2];
            if (b0>b1){uint32_t t=b0;b0=b1;b1=t;} if (b1>b2){uint32_t t=b1;b1=b2;b2=t;} if (b0>b1){uint32_t t=b0;b0=b1;b1=t;}
            if (a0==b0 && a1==b1 && a2==b2) return 1;
        }
    }
    return 0;
}

// T1: edge_len_factor <= 0 must match ano_simplify byte-for-byte.
static void test_simplify_guard_byte_identity() {
    printf("Running test_simplify_guard_byte_identity...\n");
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
            uint32_t i00=y*N+x, i10=y*N+x+1, i01=(y+1)*N+x, i11=(y+1)*N+x+1;
            indices[ic++]=i00; indices[ic++]=i10; indices[ic++]=i11;
            indices[ic++]=i00; indices[ic++]=i11; indices[ic++]=i01;
        }
    size_t target = (ic / 4 / 3) * 3;
    uint32_t a[(N-1)*(N-1)*2*3], b[(N-1)*(N-1)*2*3];
    size_t r0 = ano_simplify_ex(a, indices, ic, positions, N*N, sizeof(float)*3, target, 0.05f, 0.0f, NULL);
    size_t r1 = ano_simplify(b, indices, ic, positions, N*N, sizeof(float)*3, target, 0.05f, NULL);
    assert(r0 == r1);
    assert(memcmp(a, b, r0 * sizeof(uint32_t)) == 0);
}

// T4: gentle 1/2 flat target: guards must not gut planar decimation.
static void test_simplify_flat_not_gutted() {
    printf("Running test_simplify_flat_not_gutted...\n");
    enum { N = 20 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    size_t ic = build_grid(N, positions, indices);
    size_t target = (ic / 2 / 3) * 3;   // gentle 1/2, cap-inert
    uint32_t off[(N-1)*(N-1)*2*3], on[(N-1)*(N-1)*2*3];
    size_t r_off = ano_simplify_ex(off, indices, ic, positions, N*N, sizeof(float)*3, target, 0.05f, 0.0f, NULL);
    size_t r_on  = ano_simplify_ex(on,  indices, ic, positions, N*N, sizeof(float)*3, target, 0.05f, 8.0f, NULL);
    assert(r_on < ic);            // still decimates
    // Allow +3 slack: unguarded may overshoot by one collapse (2 tris).
    assert(r_on <= target);       // reaches budget
    assert(r_on <= r_off + 3);    // within one triangle of unguarded
    validate_indices(on, r_on, N*N);
}

// T3: narrow concave trench must not bridge (longest edge bound).
static void test_simplify_concave_trench() {
    printf("Running test_simplify_concave_trench...\n");
    enum { N = 16 };
    float positions[N*N*3];
    uint32_t indices[(N-1)*(N-1)*2*3];
    size_t ic = build_grid(N, positions, indices);   // flat z=0
    for (uint32_t x = 0; x < N; ++x) positions[((N/2)*N+x)*3+2] = -1.5f;   // middle row into a V
    float extent = ano_simplify_scale(positions, N*N, sizeof(float)*3);
    size_t target = (ic / 8 / 3) * 3;
    uint32_t out[(N-1)*(N-1)*2*3];
    size_t r = ano_simplify_ex(out, indices, ic, positions, N*N, sizeof(float)*3, target, 0.05f, 8.0f, NULL);
    validate_indices(out, r, N*N);
    assert(r > 0 && r < ic);
    assert(max_edge_len(out, r, positions) < 0.5f * extent);
}

// T2: closed faceted pillar stays valid and rim-safe under guards. Honest scope: does not isolate feature-slide alone.
static void test_simplify_pillar_silhouette() {
    printf("Running test_simplify_pillar_silhouette...\n");
    enum { N = 16 };
    const float PI = 3.14159265358979f, R = 1.0f, H = 8.0f;
    // layout: topRing[i]=i, botRing[i]=N+i, topCenter=2N, botCenter=2N+1, skirt=2N+2..2N+4
    float pos[(2*N+5)*3];
    uint32_t idx[(2*N + N + N + 1)*3];
    for (uint32_t i = 0; i < N; ++i) {
        float th = 2.0f*PI*(float)i/(float)N, cx = R*cosf(th), cz = R*sinf(th);
        pos[i*3+0]=cx; pos[i*3+1]=H; pos[i*3+2]=cz;               // top ring
        pos[(N+i)*3+0]=cx; pos[(N+i)*3+1]=0.0f; pos[(N+i)*3+2]=cz; // bottom ring
    }
    pos[(2*N)*3+0]=0; pos[(2*N)*3+1]=H; pos[(2*N)*3+2]=0;         // top center
    pos[(2*N+1)*3+0]=0; pos[(2*N+1)*3+1]=0; pos[(2*N+1)*3+2]=0;   // bottom center
    pos[(2*N+2)*3+0]=100; pos[(2*N+2)*3+1]=0; pos[(2*N+2)*3+2]=0; // far skirt (blows up extent)
    pos[(2*N+3)*3+0]=101; pos[(2*N+3)*3+1]=0; pos[(2*N+3)*3+2]=0;
    pos[(2*N+4)*3+0]=100; pos[(2*N+4)*3+1]=0; pos[(2*N+4)*3+2]=1;
    size_t k = 0;
    for (uint32_t i = 0; i < N; ++i) {
        uint32_t j = (i+1)%N;
        idx[k++]=N+i; idx[k++]=i;   idx[k++]=j;      // side tri 1
        idx[k++]=N+i; idx[k++]=j;   idx[k++]=N+j;    // side tri 2
        idx[k++]=2*N; idx[k++]=j;   idx[k++]=i;      // top cap fan
        idx[k++]=2*N+1; idx[k++]=N+i; idx[k++]=N+j;  // bottom cap fan
    }
    idx[k++]=2*N+2; idx[k++]=2*N+3; idx[k++]=2*N+4;  // skirt
    size_t ic = k, vc = 2*N+5;
    size_t target = (ic / 8 / 3) * 3;
    uint32_t out[(2*N + N + N + 1)*3], out0[(2*N + N + N + 1)*3];
    size_t r_on  = ano_simplify_ex(out,  idx, ic, pos, vc, sizeof(float)*3, target, 2.0f, 8.0f, NULL);
    size_t r_off = ano_simplify_ex(out0, idx, ic, pos, vc, sizeof(float)*3, target, 2.0f, 0.0f, NULL);
    validate_indices(out, r_on, vc);
    assert(r_on > 0 && r_on < ic);                         // decimated vertical strips
    assert(max_radius_above(out, r_on, pos, 0.8f*H) >= 0.9f*R);  // rim silhouette preserved
    assert(r_on >= r_off);                                 // feature path keeps >= tris
}

// T5: near-flat tetra (+ skirt): link/tetra exclusion blocks doubled faces.
static void test_simplify_tetra_link() {
    printf("Running test_simplify_tetra_link...\n");
    float pos[7*3] = {
        0.0f,0.0f,0.0f,   1.0f,0.0f,0.0f,   0.5f,0.866f,0.0f,   0.5f,0.289f,0.06f,  // near-flat tetra
        100.0f,0.0f,0.0f, 101.0f,0.0f,0.0f, 100.0f,0.0f,1.0f                          // far skirt
    };
    uint32_t idx[5*3] = { 0,1,2,  0,3,1,  1,3,2,  2,3,0,  4,5,6 };
    size_t ic = 15, vc = 7;
    uint32_t out[5*3];
    size_t r = ano_simplify_ex(out, idx, ic, pos, vc, sizeof(float)*3, 9u, 2.0f, 8.0f, NULL);
    validate_indices(out, r, vc);
    assert(!has_dup_face(out, r));   // no non-manifold doubled face
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
    test_simplify_guard_byte_identity();
    test_simplify_flat_not_gutted();
    test_simplify_concave_trench();
    test_simplify_pillar_silhouette();
    test_simplify_tetra_link();
    printf("All tests passed successfully!\n");
    return 0;
}
