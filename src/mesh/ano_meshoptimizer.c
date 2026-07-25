#include "ano_meshoptimizer.h"
#include <anoptic_memory.h>   // puts this TU's malloc/free in the engine allocator (MI_OVERRIDE is OFF)
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <assert.h>

#define K_CACHE_SIZE_MAX 16
#define K_VALENCE_MAX 8

typedef struct {
    float cache[1 + K_CACHE_SIZE_MAX];
    float live[1 + K_VALENCE_MAX];
} vertex_score_table_t;

// ACMR-tuned cache/valence scores (NVidia/AMD-like)
static const vertex_score_table_t kVertexScoreTable = {
    {0.0f, 0.779f, 0.791f, 0.789f, 0.981f, 0.843f, 0.726f, 0.847f, 0.882f, 0.867f, 0.799f, 0.642f, 0.613f, 0.600f, 0.568f, 0.372f, 0.234f},
    {0.0f, 0.995f, 0.713f, 0.450f, 0.404f, 0.059f, 0.005f, 0.147f, 0.006f}
};

typedef struct {
    uint32_t* counts;
    uint32_t* offsets;
    uint32_t* data;
} triangle_adjacency_t;

static inline float get_vertex_score(int cache_position, uint32_t live_triangles) {
    assert(cache_position >= -1 && cache_position < (int)K_CACHE_SIZE_MAX);
    uint32_t live = live_triangles < K_VALENCE_MAX ? live_triangles : K_VALENCE_MAX;
    return kVertexScoreTable.cache[1 + cache_position] + kVertexScoreTable.live[live];
}

static void build_triangle_adjacency(triangle_adjacency_t* adjacency, const uint32_t* indices, size_t index_count, size_t vertex_count) {
    size_t face_count = index_count / 3;

    memset(adjacency->counts, 0, vertex_count * sizeof(uint32_t));

    for (size_t i = 0; i < index_count; ++i) {
        assert(indices[i] < vertex_count);
        adjacency->counts[indices[i]]++;
    }

    uint32_t offset = 0;
    for (size_t i = 0; i < vertex_count; ++i) {
        adjacency->offsets[i] = offset;
        offset += adjacency->counts[i];
    }

    assert(offset == index_count);

    for (size_t i = 0; i < face_count; ++i) {
        uint32_t a = indices[i * 3 + 0];
        uint32_t b = indices[i * 3 + 1];
        uint32_t c = indices[i * 3 + 2];

        adjacency->data[adjacency->offsets[a]++] = (uint32_t)i;
        adjacency->data[adjacency->offsets[b]++] = (uint32_t)i;
        adjacency->data[adjacency->offsets[c]++] = (uint32_t)i;
    }

    for (size_t i = 0; i < vertex_count; ++i) {
        assert(adjacency->offsets[i] >= adjacency->counts[i]);
        adjacency->offsets[i] -= adjacency->counts[i];
    }
}

static uint32_t get_next_triangle_dead_end(uint32_t* input_cursor, const uint8_t* emitted_flags, size_t face_count) {
    while (*input_cursor < face_count) {
        if (!emitted_flags[*input_cursor]) return *input_cursor;
        (*input_cursor)++;
    }
    return ~0u;
}

static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// Reserve `bytes` at the running cursor; returns the offset and leaves the cursor 16-aligned past
// it. Carves chain through the cursor rather than by naming the previous field, so they cannot be
// mis-ordered, and the final cursor is the block size.
static inline size_t carve(size_t* cur, size_t bytes) {
    size_t at = *cur;
    *cur = align_up(at + bytes, 16);
    return at;
}

// Seal one meshlet: record its spans and copy its local vertex table out. Caller advances offsets.
static inline void meshlet_seal(ano_meshlet_t* m, uint32_t* meshlet_vertices, const uint32_t* current,
                                uint32_t vertex_offset, uint32_t triangle_offset,
                                uint32_t nverts, uint32_t ntris) {
    m->vertex_offset = vertex_offset;
    m->triangle_offset = triangle_offset;
    m->vertex_count = nverts;
    m->triangle_count = ntris;
    memcpy(&meshlet_vertices[vertex_offset], current, nverts * sizeof(uint32_t));
}



size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles) {
    size_t safe_index_count = index_count - (index_count % 3);
    if (max_vertices < 3) return 0;
    if (max_triangles < 1) return 0;

    // Clamp limits
    if (max_vertices > 256) max_vertices = 256;
    if (max_triangles > 256) max_triangles = 256;

    size_t max_vertices_conservative = max_vertices - 2;
    size_t meshlet_limit_vertices = (safe_index_count + max_vertices_conservative - 1) / max_vertices_conservative;
    size_t meshlet_limit_triangles = (safe_index_count / 3 + max_triangles - 1) / max_triangles;

    return meshlet_limit_vertices > meshlet_limit_triangles ? meshlet_limit_vertices : meshlet_limit_triangles;
}

void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count) {
    assert(index_count % 3 == 0);

    if (index_count == 0 || vertex_count == 0) return;

    // Both acquisitions below are pass-local; the scoped heap is the only discharge.
    mi_heap_t* pass LOCALHEAPATTR = mi_heap_new();
    if (!pass) return;

    const uint32_t* src_indices = indices;
    uint32_t* indices_copy = NULL;

    // Support in-place optimization
    if (destination == indices) {
        indices_copy = (uint32_t*)mi_heap_malloc(pass, index_count * sizeof(uint32_t));
        if (!indices_copy) return;
        memcpy(indices_copy, indices, index_count * sizeof(uint32_t));
        src_indices = indices_copy;
    }

    uint32_t cache_size = 16;
    size_t face_count = index_count / 3;

    // Partition one scratch block across all helper arrays
    size_t total_memory_size = 0;
    size_t counts_offset          = carve(&total_memory_size, vertex_count * sizeof(uint32_t));
    size_t offsets_offset         = carve(&total_memory_size, vertex_count * sizeof(uint32_t));
    size_t data_offset            = carve(&total_memory_size, index_count * sizeof(uint32_t));
    size_t emitted_flags_offset   = carve(&total_memory_size, face_count * sizeof(uint8_t));
    size_t vertex_scores_offset   = carve(&total_memory_size, vertex_count * sizeof(float));
    size_t triangle_scores_offset = carve(&total_memory_size, face_count * sizeof(float));

    void* scratch = mi_heap_malloc(pass, total_memory_size);
    if (!scratch) return;

    uint32_t* counts = (uint32_t*)((char*)scratch + counts_offset);
    uint32_t* offsets = (uint32_t*)((char*)scratch + offsets_offset);
    uint32_t* data = (uint32_t*)((char*)scratch + data_offset);
    uint8_t* emitted_flags = (uint8_t*)((char*)scratch + emitted_flags_offset);
    float* vertex_scores = (float*)((char*)scratch + vertex_scores_offset);
    float* triangle_scores = (float*)((char*)scratch + triangle_scores_offset);

    memset(counts, 0, vertex_count * sizeof(uint32_t));
    memset(emitted_flags, 0, face_count * sizeof(uint8_t));

    triangle_adjacency_t adjacency = { counts, offsets, data };

    build_triangle_adjacency(&adjacency, src_indices, index_count, vertex_count);

    uint32_t* live_triangles = adjacency.counts;

    for (size_t i = 0; i < vertex_count; ++i) vertex_scores[i] = get_vertex_score(-1, live_triangles[i]);

    for (size_t i = 0; i < face_count; ++i) {
        uint32_t a = src_indices[i * 3 + 0];
        uint32_t b = src_indices[i * 3 + 1];
        uint32_t c = src_indices[i * 3 + 2];

        triangle_scores[i] = vertex_scores[a] + vertex_scores[b] + vertex_scores[c];
    }

    uint32_t cache_holder[2 * (K_CACHE_SIZE_MAX + 4)];
    uint32_t* cache = cache_holder;
    uint32_t* cache_new = cache_holder + K_CACHE_SIZE_MAX + 4;
    size_t cache_count = 0;

    uint32_t current_triangle = 0;
    uint32_t input_cursor = 1;
    uint32_t output_triangle = 0;

    while (current_triangle != ~0u) {
        assert(output_triangle < face_count);

        uint32_t a = src_indices[current_triangle * 3 + 0];
        uint32_t b = src_indices[current_triangle * 3 + 1];
        uint32_t c = src_indices[current_triangle * 3 + 2];

        destination[output_triangle * 3 + 0] = a;
        destination[output_triangle * 3 + 1] = b;
        destination[output_triangle * 3 + 2] = c;
        output_triangle++;

        emitted_flags[current_triangle] = 1;
        triangle_scores[current_triangle] = 0.0f;

        size_t cache_write = 0;
        cache_new[cache_write++] = a;
        cache_new[cache_write++] = b;
        cache_new[cache_write++] = c;

        for (size_t i = 0; i < cache_count; ++i) {
            uint32_t index = cache[i];
            cache_new[cache_write] = index;
            cache_write += (index != a) & (index != b) & (index != c);
        }

        uint32_t* cache_temp = cache;
        cache = cache_new;
        cache_new = cache_temp;
        cache_count = cache_write > cache_size ? cache_size : cache_write;

        for (size_t k = 0; k < 3; ++k) {
            uint32_t index = src_indices[current_triangle * 3 + k];

            uint32_t* neighbors = adjacency.data + adjacency.offsets[index];
            size_t neighbors_size = adjacency.counts[index];

            for (size_t i = 0; i < neighbors_size; ++i) {
                uint32_t tri = neighbors[i];

                if (tri == current_triangle) {
                    neighbors[i] = neighbors[neighbors_size - 1];
                    adjacency.counts[index]--;
                    break;
                }
            }
        }

        uint32_t best_triangle = ~0u;
        float best_score = 0.0f;

        for (size_t i = 0; i < cache_write; ++i) {
            uint32_t index = cache[i];

            if (adjacency.counts[index] == 0) continue;

            int cache_position = i >= cache_size ? -1 : (int)i;

            float score = get_vertex_score(cache_position, live_triangles[index]);
            float score_diff = score - vertex_scores[index];

            vertex_scores[index] = score;

            const uint32_t* neighbors_begin = adjacency.data + adjacency.offsets[index];
            const uint32_t* neighbors_end = neighbors_begin + adjacency.counts[index];

            for (const uint32_t* it = neighbors_begin; it != neighbors_end; ++it) {
                uint32_t tri = *it;
                assert(!emitted_flags[tri]);

                float tri_score = triangle_scores[tri] + score_diff;
                assert(tri_score > 0.0f);

                if (best_score < tri_score) {
                    best_triangle = tri;
                    best_score = tri_score;
                }

                triangle_scores[tri] = tri_score;
            }
        }

        current_triangle = best_triangle;

        if (current_triangle == ~0u) {
            current_triangle = get_next_triangle_dead_end(&input_cursor, emitted_flags, face_count);
        }
    }

    assert(input_cursor == face_count);
    assert(output_triangle == face_count);
}

size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles) {
    size_t meshlet_count = 0;

    // Same acceptance domain as ano_build_meshlets_bound: a config it sizes 0 packs nothing.
    if (max_vertices < 3) return 0;
    if (max_triangles < 1) return 0;

    // Clamp to uint8_t local-index range
    if (max_vertices > 256) max_vertices = 256;
    if (max_triangles > 256) max_triangles = 256;
    
    uint32_t current_meshlet_vertices[256];
    uint32_t num_current_vertices = 0;
    uint32_t num_current_triangles = 0;
    
    uint32_t vertex_offset = 0;
    uint32_t triangle_offset = 0;

    size_t safe_index_count = index_count - (index_count % 3);

    for (size_t i = 0; i < safe_index_count; i += 3) {
        uint32_t a = indices[i + 0];
        uint32_t b = indices[i + 1];
        uint32_t c = indices[i + 2];
        
        int32_t a_idx = -1, b_idx = -1, c_idx = -1;
        uint32_t added_vertices = 0;
        
        // Linear scan for vertex reuse
        for (uint32_t j = 0; j < num_current_vertices; ++j) {
            if (current_meshlet_vertices[j] == a) a_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == b) b_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == c) c_idx = (int32_t)j;
        }
        
        if (a_idx == -1) added_vertices++;
        if (b_idx == -1 && a != b) added_vertices++;
        if (c_idx == -1 && c != a && c != b) added_vertices++;
        
        // Commit meshlet when vertex/triangle limits exceeded
        if (num_current_vertices + added_vertices > max_vertices || num_current_triangles >= max_triangles) {
            if (num_current_triangles > 0) {
                meshlet_seal(&meshlets[meshlet_count++], meshlet_vertices, current_meshlet_vertices,
                             vertex_offset, triangle_offset, num_current_vertices, num_current_triangles);
                vertex_offset += num_current_vertices;
                triangle_offset += num_current_triangles * 3;
            }
            num_current_vertices = 0;
            num_current_triangles = 0;
            a_idx = -1; b_idx = -1; c_idx = -1;
        }
        
        // Insert new verts, map duplicate corners of the same triangle
        if (a_idx == -1) {
            a_idx = (int32_t)num_current_vertices;
            current_meshlet_vertices[num_current_vertices++] = a;
            if (b == a) b_idx = a_idx;
            if (c == a) c_idx = a_idx;
        }
        if (b_idx == -1) {
            b_idx = (int32_t)num_current_vertices;
            current_meshlet_vertices[num_current_vertices++] = b;
            if (c == b) c_idx = b_idx;
        }
        if (c_idx == -1) {
            c_idx = (int32_t)num_current_vertices;
            current_meshlet_vertices[num_current_vertices++] = c;
        }
        
        // Emit triangle
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 0] = (uint8_t)a_idx;
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 1] = (uint8_t)b_idx;
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 2] = (uint8_t)c_idx;
        num_current_triangles++;
    }
    
    // Commit the final meshlet
    if (num_current_triangles > 0)
        meshlet_seal(&meshlets[meshlet_count++], meshlet_vertices, current_meshlet_vertices,
                     vertex_offset, triangle_offset, num_current_vertices, num_current_triangles);

    return meshlet_count;
}

static inline float dot_product(const float* a, const float* b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static inline void cross_product(const float* a, const float* b, float* out) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static inline void v3_sub(const float* a, const float* b, float* out) {
    out[0] = a[0]-b[0]; out[1] = a[1]-b[1]; out[2] = a[2]-b[2];
}

static inline void v3_copy(const float* a, float* out) { out[0]=a[0]; out[1]=a[1]; out[2]=a[2]; }

static inline void v3_div(float* v, float s) { v[0] /= s; v[1] /= s; v[2] /= s; }

static inline float v3_dist2(const float* a, const float* b) {
    float d[3]; v3_sub(a, b, d); return dot_product(d, d);
}

// Position of meshlet-local corner i.
static inline const float* meshlet_pos(const uint32_t* mv, const uint8_t* mt,
                                       const float* vp, size_t stride, size_t i) {
    return (const float*)((const char*)vp + (size_t)mv[mt[i]] * stride);
}

// Corner furthest from ref over [0, n). out: its squared distance. Ritter seed pass.
static const float* meshlet_furthest(const uint32_t* mv, const uint8_t* mt, const float* vp,
                                     size_t stride, size_t n, const float* ref, float* out_d2) {
    float best = -1.0f;
    const float* bp = ref;
    for (size_t i = 0; i < n; ++i) {
        const float* p = meshlet_pos(mv, mt, vp, stride, i);
        float d2 = v3_dist2(p, ref);
        if (d2 > best) { best = d2; bp = p; }
    }
    *out_d2 = best;
    return bp;
}

ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles, 
                                                    size_t triangle_count, const float* vertex_positions, 
                                                    size_t vertex_count, size_t vertex_positions_stride) {
    (void)vertex_count; // Unused here
    ano_meshlet_bounds_gpu_t bounds;
    memset(&bounds, 0, sizeof(bounds));
    
    if (triangle_count == 0) return bounds;

    // 1. Ritter bounding sphere: seed on the two mutually furthest corners.
    const size_t corners = triangle_count * 3;
    const float* p_start = meshlet_pos(meshlet_vertices, meshlet_triangles,
                                       vertex_positions, vertex_positions_stride, 0);
    float max_dist_sq;
    const float* p_min = meshlet_furthest(meshlet_vertices, meshlet_triangles, vertex_positions,
                                          vertex_positions_stride, corners, p_start, &max_dist_sq);
    const float* p_max = meshlet_furthest(meshlet_vertices, meshlet_triangles, vertex_positions,
                                          vertex_positions_stride, corners, p_min, &max_dist_sq);

    // Midpoint center, half-diameter radius
    bounds.center[0] = (p_min[0] + p_max[0]) * 0.5f;
    bounds.center[1] = (p_min[1] + p_max[1]) * 0.5f;
    bounds.center[2] = (p_min[2] + p_max[2]) * 0.5f;
    float rad_sq = max_dist_sq * 0.25f;
    bounds.radius = sqrtf(rad_sq);

    // Grow sphere to enclose outliers
    for (size_t i = 0; i < corners; ++i) {
        const float* pos = meshlet_pos(meshlet_vertices, meshlet_triangles,
                                       vertex_positions, vertex_positions_stride, i);
        float d0 = pos[0] - bounds.center[0];
        float d1 = pos[1] - bounds.center[1];
        float d2 = pos[2] - bounds.center[2];
        float dist_sq = d0*d0 + d1*d1 + d2*d2;
        if (dist_sq > rad_sq) {
            float dist = sqrtf(dist_sq);
            float new_radius = (bounds.radius + dist) * 0.5f;
            float k = (new_radius - bounds.radius) / dist;
            bounds.center[0] += d0 * k;
            bounds.center[1] += d1 * k;
            bounds.center[2] += d2 * k;
            bounds.radius = new_radius;
            rad_sq = new_radius * new_radius;
        }
    }

    // 2. Normal cone (meshopt): cutoff = sin(spread), 1 past hemisphere. flat.task:
    //   dot(center-eye, axis) >= cutoff*|center-eye| + radius => fully backfacing.
    float avg_normal[3] = { 0.0f, 0.0f, 0.0f };
    float triangle_normals[256][3]; // Max 256 triangles
    size_t valid_normals = 0;

    size_t cone_triangle_count = triangle_count > 256 ? 256 : triangle_count;

    for (size_t i = 0; i < cone_triangle_count; ++i) {
        const float* p0 = meshlet_pos(meshlet_vertices, meshlet_triangles, vertex_positions, vertex_positions_stride, i*3+0);
        const float* p1 = meshlet_pos(meshlet_vertices, meshlet_triangles, vertex_positions, vertex_positions_stride, i*3+1);
        const float* p2 = meshlet_pos(meshlet_vertices, meshlet_triangles, vertex_positions, vertex_positions_stride, i*3+2);

        float edge1[3], edge2[3], normal[3];
        v3_sub(p1, p0, edge1);
        v3_sub(p2, p0, edge2);
        cross_product(edge1, edge2, normal);

        float len = sqrtf(dot_product(normal, normal));
        if (len > 1e-6f) {
            // Average unit normals (facing spread, not area-weighted)
            float* tn = triangle_normals[valid_normals++];
            v3_copy(normal, tn);
            v3_div(tn, len);
            avg_normal[0] += tn[0]; avg_normal[1] += tn[1]; avg_normal[2] += tn[2];
        }
    }

    // Degenerate meshlet: never cone-cull
    bounds.cone_axis[0] = 0.0f; bounds.cone_axis[1] = 0.0f; bounds.cone_axis[2] = 1.0f;
    bounds.cone_cutoff = 1.0f;
    v3_copy(bounds.center, bounds.cone_apex);

    if (valid_normals > 0) {
        float avg_len = sqrtf(dot_product(avg_normal, avg_normal));
        if (avg_len > 1e-6f) {
            v3_copy(avg_normal, bounds.cone_axis);
            v3_div(bounds.cone_axis, avg_len);

            float min_dot = 1.0f;
            for (size_t i = 0; i < valid_normals; ++i) {
                float d = dot_product(bounds.cone_axis, triangle_normals[i]);
                if (d < min_dot) min_dot = d;
            }

            bounds.cone_cutoff = min_dot <= 0.0f ? 1.0f : sqrtf(1.0f - min_dot * min_dot);
        }
    }

    return bounds;
}

/* Mesh Simplification */

// LOD. Endpoint-snap QEM. Same VBO, shorter IBO.
// Welds coincident positions. Borders slide along border only. Geometry-only.

// 11-float symmetric quadric: error(v) = vT A v + 2 bT v + c. w normalizes by weight.
typedef struct {
    float a00, a11, a22;
    float a10, a20, a21;
    float b0, b1, b2;
    float c;
    float w;
} ano_quadric_t;

// Directed collapse: snap v onto t, cost = quadric error at t.
typedef struct { float cost; uint32_t v; uint32_t t; } ano_collapse_t;

static const float ANO_BORDER_WEIGHT = 10.0f;  // border-plane quadric weight
static const size_t ANO_SIMPLIFY_MAX_PASSES = 1000u;
static const float ANO_SIMPLIFY_AREA_EPS2 = 1e-24f;    // (2*area)^2: Guard 5 collinear drop (== |cross|<1e-12)
static const float ANO_SIMPLIFY_ABS_EDGE_FRAC = 0.25f; // abs edge cap vs largest bbox axis (extent==1)
static const float ANO_SIMPLIFY_MAX_NORMAL_DRIFT_COS = 0.5f; // cos(60deg): max cumulative face rotation from pass-0
static const float ANO_FEATURE_COS = 0.70710678f;      // cos(45deg): manifold dihedral past this = feature edge
// static const float ANO_SIMPLIFY_VOLUME_K = 0.0f;    // OFF: >0 enables swept-volume thickness guard

static inline uint32_t ano_float_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static inline size_t ano_ceil_pow2(size_t x) { size_t p = 16; while (p < x) p <<= 1; return p; }

// Unit plane (a,b,c,d) weighted by w -> quadric.
static void ano_quadric_from_plane(ano_quadric_t* q, float a, float b, float c, float d, float w) {
    q->a00 = a*a*w; q->a11 = b*b*w; q->a22 = c*c*w;
    q->a10 = a*b*w; q->a20 = a*c*w; q->a21 = b*c*w;
    q->b0 = a*d*w; q->b1 = b*d*w; q->b2 = c*d*w;
    q->c = d*d*w; q->w = w;
}

static inline void ano_quadric_add(ano_quadric_t* dst, const ano_quadric_t* s) {
    dst->a00 += s->a00; dst->a11 += s->a11; dst->a22 += s->a22;
    dst->a10 += s->a10; dst->a20 += s->a20; dst->a21 += s->a21;
    dst->b0 += s->b0; dst->b1 += s->b1; dst->b2 += s->b2;
    dst->c += s->c; dst->w += s->w;
}

// Weighted avg squared distance of p to the quadric (>= 0).
static inline float ano_quadric_error(const ano_quadric_t* q, const float* p) {
    float x = p[0], y = p[1], z = p[2];
    float r = q->a00*x*x + q->a11*y*y + q->a22*z*z
            + 2.0f*(q->a10*x*y + q->a20*x*z + q->a21*y*z)
            + 2.0f*(q->b0*x + q->b1*y + q->b2*z)
            + q->c;
    r = fabsf(r);
    return q->w > 1e-12f ? r / q->w : r;
}

// Collapse chain with path halving. collapse[i]==i survives.
// Unnormalized face normal of welded triangle (a,b,c) in npos. Returns |n|^2, i.e. (2*area)^2:
// callers compare it against a squared epsilon, or sqrt it to normalize.
static inline float ano_face_cross(const float* npos, uint32_t a, uint32_t b, uint32_t c, float* n) {
    float e1[3], e2[3];
    v3_sub(&npos[b*3], &npos[a*3], e1);
    v3_sub(&npos[c*3], &npos[a*3], e2);
    cross_product(e1, e2, n);
    return dot_product(n, n);
}

static uint32_t ano_resolve(uint32_t* collapse, uint32_t i) {
    while (collapse[i] != i) {
        collapse[i] = collapse[collapse[i]];
        i = collapse[i];
    }
    return i;
}

// Undirected edge multiset (open-addressing, pow2). Key=(lo<<32)|hi, lo<hi, 0=empty.
// Counts: 1=border, 2=manifold, >2=complex. Endpoints may be passed in either order.
static inline uint64_t ano_edge_key(uint32_t u, uint32_t v) {
    return u < v ? ((uint64_t)u << 32) | v : ((uint64_t)v << 32) | u;
}

// Slot holding key, else the first empty slot on its probe chain.
static inline size_t ano_edge_probe(const uint64_t* keys, size_t cap, uint64_t key) {
    size_t mask = cap - 1;
    size_t h = (size_t)((key * 11400714819323198485ull) >> 32) & mask;
    while (keys[h] != key && keys[h] != 0) h = (h + 1) & mask;
    return h;
}

static void ano_edge_add(uint64_t* keys, uint32_t* cnt, size_t cap, uint32_t u, uint32_t v) {
    uint64_t key = ano_edge_key(u, v);
    size_t h = ano_edge_probe(keys, cap, key);
    if (keys[h] == key) cnt[h]++;
    else { keys[h] = key; cnt[h] = 1; }
}

static uint32_t ano_edge_get(const uint64_t* keys, const uint32_t* cnt, size_t cap, uint32_t u, uint32_t v) {
    size_t h = ano_edge_probe(keys, cap, ano_edge_key(u, v));
    return keys[h] ? cnt[h] : 0;
}

// Slot of a known-present edge (same slot as use-count for feature companion table).
static size_t ano_edge_slot(const uint64_t* keys, size_t cap, uint32_t u, uint32_t v) {
    return ano_edge_probe(keys, cap, ano_edge_key(u, v));
}

static int ano_collapse_cmp(const void* a, const void* b) {
    float ca = ((const ano_collapse_t*)a)->cost, cb = ((const ano_collapse_t*)b)->cost;
    return (ca < cb) ? -1 : (ca > cb) ? 1 : 0;
}

// Rebuild edge-use multiset for tri[]. cap pow2 >= ~2x unique edges.
static void ano_count_edges(uint64_t* keys, uint32_t* cnt, size_t cap, const uint32_t* tri, size_t ntri) {
    memset(keys, 0, cap * sizeof(uint64_t));
    memset(cnt, 0, cap * sizeof(uint32_t));
    for (size_t t = 0; t < ntri; ++t) {
        uint32_t a = tri[t*3+0], b = tri[t*3+1], c = tri[t*3+2];
        ano_edge_add(keys, cnt, cap, a, b);
        ano_edge_add(keys, cnt, cap, b, c);
        ano_edge_add(keys, cnt, cap, a, c);
    }
}

float ano_simplify_scale(const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride) {
    if (vertex_count == 0) return 0.0f;
    const char* base = (const char*)vertex_positions;
    const float* p0 = (const float*)base;
    float mn[3] = { p0[0], p0[1], p0[2] };
    float mx[3] = { p0[0], p0[1], p0[2] };
    for (size_t i = 1; i < vertex_count; ++i) {
        const float* p = (const float*)(base + i * vertex_positions_stride);
        for (int k = 0; k < 3; ++k) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (p[k] > mx[k]) mx[k] = p[k];
        }
    }
    float ex = mx[0] - mn[0], ey = mx[1] - mn[1], ez = mx[2] - mn[2];
    float e = ex > ey ? ex : ey;
    return e > ez ? e : ez;
}

// kind codes
#define ANO_VK_MANIFOLD 0u
#define ANO_VK_BORDER   1u
#define ANO_VK_LOCKED   2u

// Guards off (edge_len_factor=0): byte-identical baseline.
size_t ano_simplify(uint32_t* destination, const uint32_t* indices, size_t index_count,
                    const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                    size_t target_index_count, float target_error, float* out_result_error) {
    return ano_simplify_ex(destination, indices, index_count, vertex_positions, vertex_count,
                           vertex_positions_stride, target_index_count, target_error, 0.0f,
                           out_result_error);
}

size_t ano_simplify_ex(uint32_t* destination, const uint32_t* indices, size_t index_count,
                       const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                       size_t target_index_count, float target_error, float edge_len_factor,
                       float* out_result_error) {
    if (out_result_error) *out_result_error = 0.0f;

    size_t tri0 = index_count / 3;
    size_t ic = tri0 * 3;  // floored to whole triangles
    size_t target_ic = (target_index_count / 3) * 3;

    if (ic == 0 || vertex_count == 0) return 0;

    // Passthrough: drop coincident-position degenerates, no collapses.
    if (target_ic >= ic) {
        const char* vb = (const char*)vertex_positions;
        size_t outc = 0;
        for (size_t t = 0; t < tri0; ++t) {
            uint32_t a = indices[t*3+0], b = indices[t*3+1], c = indices[t*3+2];
            const float* pa = (const float*)(vb + a*vertex_positions_stride);
            const float* pb = (const float*)(vb + b*vertex_positions_stride);
            const float* pc = (const float*)(vb + c*vertex_positions_stride);
            if ((pa[0]==pb[0] && pa[1]==pb[1] && pa[2]==pb[2]) ||
                (pb[0]==pc[0] && pb[1]==pc[1] && pb[2]==pc[2]) ||
                (pa[0]==pc[0] && pa[1]==pc[1] && pa[2]==pc[2])) continue;
            destination[outc++] = a; destination[outc++] = b; destination[outc++] = c;
        }
        return outc;
    }

    float extent = ano_simplify_scale(vertex_positions, vertex_count, vertex_positions_stride);
    float invscale = extent > 0.0f ? 1.0f / extent : 1.0f;

    // Scratch: one scoped heap owns the pass, carved into blocks by index domain. Arrays indexed
    // by the same thing share a block, so the 20 acquisitions this used to make are 4 (5 with
    // guards on) and there is no free epilogue 〜 mi_heap_destroy at scope exit is the discharge,
    // which is also why the OOM arm below simply returns. Blocks stay separate where lifetime or
    // arity differs: weld dies at the end of the weld pass, and the guards-on set never exists on
    // the base path. Sub-arrays are 16-aligned, over the strictest member here (u64 / quadric).
    // Sizes are exact: sizing and placement read the same offset variables, never a second list.
    size_t weldCap = ano_ceil_pow2(vertex_count * 2 + 16);
    size_t ecap    = ano_ceil_pow2(tri0 * 4 + 16);

    mi_heap_t* pass LOCALHEAPATTR = mi_heap_new();
    if (!pass) {
        if (destination != indices) memcpy(destination, indices, ic * sizeof(uint32_t));
        return ic;
    }

    // Per-vertex working state. Widest members first so each carve stays naturally aligned.
    size_t vBytes = 0;
    size_t vNpos      = carve(&vBytes, vertex_count * 3 * sizeof(float));
    size_t vQ         = carve(&vBytes, vertex_count * sizeof(ano_quadric_t));
    size_t vCand      = carve(&vBytes, vertex_count * sizeof(ano_collapse_t));
    size_t vRemap     = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vCollapse  = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vOutid     = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vAdjCounts = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vAdjOff    = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vLinkNbr   = carve(&vBytes, vertex_count * sizeof(uint32_t));
    size_t vKind      = carve(&vBytes, vertex_count);
    size_t vLocked    = carve(&vBytes, vertex_count);
    size_t vFeature   = carve(&vBytes, vertex_count);

    // Per-index working state: adjacency payload plus the working-triangle double buffer.
    size_t iBytes = 0;
    size_t iAdjData = carve(&iBytes, ic * sizeof(uint32_t));
    size_t iWtri    = carve(&iBytes, ic * sizeof(uint32_t));
    size_t iWtmp    = carve(&iBytes, ic * sizeof(uint32_t));

    // Edge hash, and on the guarded path the parallel first-incident-normal table.
    bool guarded  = edge_len_factor > 0.0f;
    size_t eBytes = 0;
    size_t eEkeys = carve(&eBytes, ecap * sizeof(uint64_t));
    size_t eEcnt  = carve(&eBytes, ecap * sizeof(uint32_t));
    size_t eEfn   = carve(&eBytes, guarded ? ecap * 3 * sizeof(float) : 0);
    size_t eEhas  = carve(&eBytes, guarded ? ecap : 0);

    char* vb = (char*)mi_heap_malloc(pass, vBytes);
    char* ib = (char*)mi_heap_malloc(pass, iBytes);
    char* eb = (char*)mi_heap_malloc(pass, eBytes);
    // Separate: the weld table is dead once the weld pass ends, and is released there.
    int32_t* weld = (int32_t*)mi_heap_malloc(pass, weldCap * sizeof(int32_t));
    // Separate: per-triangle drift reference, guarded path only.
    char* tb = guarded ? (char*)mi_heap_malloc(pass, align_up(tri0 * 3 * sizeof(float), 16) * 2) : NULL;

    if (!vb || !ib || !eb || !weld || (guarded && !tb)) {
        if (destination != indices) memcpy(destination, indices, ic * sizeof(uint32_t));
        return ic;   // scoped heap discharges whatever did land
    }

    float*          npos      = (float*)(vb + vNpos);
    ano_quadric_t*  Q         = (ano_quadric_t*)(vb + vQ);
    ano_collapse_t* cand      = (ano_collapse_t*)(vb + vCand);
    uint32_t*       remap     = (uint32_t*)(vb + vRemap);
    uint32_t*       collapse  = (uint32_t*)(vb + vCollapse);
    uint32_t*       outid     = (uint32_t*)(vb + vOutid);
    uint32_t*       adjCounts = (uint32_t*)(vb + vAdjCounts);
    uint32_t*       adjOff    = (uint32_t*)(vb + vAdjOff);
    uint32_t*       linkNbr   = (uint32_t*)(vb + vLinkNbr);   // link-cond ring stamp (both paths)
    uint8_t*        kind      = (uint8_t*)(vb + vKind);
    uint8_t*        locked    = (uint8_t*)(vb + vLocked);
    uint8_t*        feature   = (uint8_t*)(vb + vFeature);    // pass-0 dihedral feature flags (both paths)

    uint32_t* adjData = (uint32_t*)(ib + iAdjData);
    uint32_t* wtri    = (uint32_t*)(ib + iWtri);
    uint32_t* wtmp    = (uint32_t*)(ib + iWtmp);

    uint64_t* ekeys = (uint64_t*)(eb + eEkeys);
    uint32_t* ecnt  = (uint32_t*)(eb + eEcnt);
    float*    efn   = guarded ? (float*)(eb + eEfn) : NULL;   // feature detection: 1st incident normal per edge
    uint8_t*  ehas  = guarded ? (uint8_t*)(eb + eEhas) : NULL;

    // pass-0 face normal per wtri slot (drift ref), ping-pong pair.
    float* orig_n     = guarded ? (float*)tb : NULL;
    float* orig_n_tmp = guarded ? (float*)(tb + align_up(tri0 * 3 * sizeof(float), 16)) : NULL;
    memset(feature, 0, vertex_count);   // off path stays all-zero -> feature-slide inert

    // Unit-extent normalize. +0.0f folds -0. Result error *= extent.
    const char* vbase = (const char*)vertex_positions;
    for (size_t i = 0; i < vertex_count; ++i) {
        const float* p = (const float*)(vbase + i * vertex_positions_stride);
        npos[i*3+0] = p[0] * invscale + 0.0f;
        npos[i*3+1] = p[1] * invscale + 0.0f;
        npos[i*3+2] = p[2] * invscale + 0.0f;
    }

    // Weld: remap[v] = lowest index at exact position (canonical). Output keeps wedge ids.
    memset(weld, 0xFF, weldCap * sizeof(int32_t));
    for (uint32_t v = 0; v < (uint32_t)vertex_count; ++v) {
        uint32_t hh = ano_float_bits(npos[v*3+0]) * 73856093u
                    ^ ano_float_bits(npos[v*3+1]) * 19349663u
                    ^ ano_float_bits(npos[v*3+2]) * 83492791u;
        size_t h = hh & (weldCap - 1);
        for (;;) {
            int32_t e = weld[h];
            if (e < 0) { weld[h] = (int32_t)v; remap[v] = v; break; }
            if (npos[e*3+0] == npos[v*3+0] && npos[e*3+1] == npos[v*3+1] && npos[e*3+2] == npos[v*3+2]) {
                remap[v] = (uint32_t)e; break;
            }
            h = (h + 1) & (weldCap - 1);
        }
        collapse[v] = v;
    }
    mi_free(weld);   // weld pass over: release ahead of the collapse loop's peak

    // Working tris in welded space. Drop index-degenerate post-weld.
    size_t tris = 0;
    for (size_t t = 0; t < tri0; ++t) {
        uint32_t a = remap[indices[t*3+0]], b = remap[indices[t*3+1]], c = remap[indices[t*3+2]];
        if (a == b || b == c || a == c) continue;
        // Guard 5: drop collinear (no-quadric) source tris. Working set only.
        if (edge_len_factor > 0.0f) {
            float gc[3];
            float gl2 = ano_face_cross(npos, a, b, c, gc);
            if (gl2 < ANO_SIMPLIFY_AREA_EPS2) continue;
            // Pass-0 origin normal. Stays aligned with wtri.
            v3_copy(gc, &orig_n[tris*3]);
            v3_div(&orig_n[tris*3], sqrtf(gl2));
        }
        wtri[tris*3+0] = a; wtri[tris*3+1] = b; wtri[tris*3+2] = c; tris++;
    }

    size_t target_tris = target_ic / 3;
    float err_limit = target_error * target_error;  // relative error -> normalized squared distance
    float result_err2 = 0.0f;

    // Link scratch: linkNbr[w]==linkGen means w is in the current candidate ring.
    uint32_t linkGen = 0u;
    memset(linkNbr, 0, vertex_count * sizeof(uint32_t));

    triangle_adjacency_t adj = { adjCounts, adjOff, adjData };

    // Quadrics once on original welded mesh: area face planes + border in-plane constraints.
    // Collapses merge into survivors (error vs original surface).
    ano_count_edges(ekeys, ecnt, ecap, wtri, tris);
    memset(Q, 0, vertex_count * sizeof(ano_quadric_t));
    for (size_t t = 0; t < tris; ++t) {
        uint32_t ia = wtri[t*3+0], ib = wtri[t*3+1], ic2 = wtri[t*3+2];
        const float* p0 = &npos[ia*3];
        float n[3];
        float len = sqrtf(ano_face_cross(npos, ia, ib, ic2, n));
        if (len < 1e-12f) continue;
        float area = len * 0.5f;
        v3_div(n, len);
        float d = -(n[0]*p0[0] + n[1]*p0[1] + n[2]*p0[2]);
        ano_quadric_t fq; ano_quadric_from_plane(&fq, n[0], n[1], n[2], d, area);
        ano_quadric_add(&Q[ia], &fq); ano_quadric_add(&Q[ib], &fq); ano_quadric_add(&Q[ic2], &fq);

        const uint32_t tri[3] = { ia, ib, ic2 };
        for (int k = 0; k < 3; ++k) {
            uint32_t x = tri[k], y = tri[(k+1)%3], z = tri[(k+2)%3];
            if (ano_edge_get(ekeys, ecnt, ecap, x, y) != 1) continue;
            const float* q0 = &npos[x*3];
            float ed[3], eu[3], tv[3], pe[3];
            v3_sub(&npos[y*3], q0, ed);
            float el = sqrtf(dot_product(ed, ed));
            if (el < 1e-12f) continue;
            v3_copy(ed, eu); v3_div(eu, el);
            v3_sub(&npos[z*3], q0, tv);
            float kdot = dot_product(tv, eu);
            pe[0] = tv[0]-eu[0]*kdot; pe[1] = tv[1]-eu[1]*kdot; pe[2] = tv[2]-eu[2]*kdot;
            float pl = sqrtf(dot_product(pe, pe));
            if (pl < 1e-12f) continue;
            v3_div(pe, pl);
            float bd = -(pe[0]*q0[0] + pe[1]*q0[1] + pe[2]*q0[2]);
            ano_quadric_t bq; ano_quadric_from_plane(&bq, pe[0], pe[1], pe[2], bd, el * ANO_BORDER_WEIGHT);
            ano_quadric_add(&Q[x], &bq); ano_quadric_add(&Q[y], &bq);
        }
    }

    // Pass-0 features: manifold edge with dihedral past ANO_FEATURE_COS flags both ends. O(tris).
    if (edge_len_factor > 0.0f) {
        memset(ehas, 0, ecap);
        for (size_t t = 0; t < tris; ++t) {
            uint32_t ia = wtri[t*3+0], ib = wtri[t*3+1], ic2 = wtri[t*3+2];
            float n[3];
            float len = sqrtf(ano_face_cross(npos, ia, ib, ic2, n));
            if (len < 1e-12f) continue;
            v3_div(n, len);
            const uint32_t tri[3] = { ia, ib, ic2 };
            for (int k = 0; k < 3; ++k) {
                uint32_t x = tri[k], y = tri[(k+1)%3];
                if (ano_edge_get(ekeys, ecnt, ecap, x, y) != 2) continue;   // manifold edges only
                size_t s = ano_edge_slot(ekeys, ecap, x, y);
                if (!ehas[s]) { v3_copy(n, &efn[s*3]); ehas[s]=1; continue; }
                float dd = efn[s*3]*n[0] + efn[s*3+1]*n[1] + efn[s*3+2]*n[2]; // cos(dihedral deviation)
                if (dd < ANO_FEATURE_COS) { feature[x] = 1; feature[y] = 1; }
            }
        }
    }

    // Growth cap: QEM is ~0 anywhere on a coplanar surface, so nothing else bounds triangle GROWTH;
    // chained in-plane collapses otherwise bridge a floor/wall. Cap resulting edge / move at
    // edge_len_factor * mean source edge (unit-extent). <=0 -> maxEdge2=FLT_MAX (guards off).
    float maxEdge2 = FLT_MAX;
    if (edge_len_factor > 0.0f && tris > 0) {
        double edgesum = 0.0;
        for (size_t t = 0; t < tris; ++t) {
            const float* q0 = &npos[wtri[t*3+0]*3];
            const float* q1 = &npos[wtri[t*3+1]*3];
            const float* q2 = &npos[wtri[t*3+2]*3];
            float d0[3], d1[3], d2[3];
            v3_sub(q1, q0, d0); v3_sub(q2, q1, d1); v3_sub(q0, q2, d2);
            edgesum += sqrt((double)dot_product(d0, d0))
                     + sqrt((double)dot_product(d1, d1))
                     + sqrt((double)dot_product(d2, d2));
        }
        float mean_edge = (float)(edgesum / (double)(tris * 3));
        if (mean_edge > 0.0f) {
            float m = edge_len_factor * mean_edge;
            // Abs backstop: on coarse flats factor*mean can itself span the surface; clamp to
            // ANO_SIMPLIFY_ABS_EDGE_FRAC of bbox axis (extent==1). Inert on dense meshes.
            if (m > ANO_SIMPLIFY_ABS_EDGE_FRAC) m = ANO_SIMPLIFY_ABS_EDGE_FRAC;
            maxEdge2 = m * m;
        }
    }

    for (size_t pass = 0; pass < ANO_SIMPLIFY_MAX_PASSES && tris > target_tris; ++pass) {
        size_t tris_before = tris;

        // Incident tris per canonical vertex.
        build_triangle_adjacency(&adj, wtri, tris_before * 3, vertex_count);

        // Edge uses -> kind (border / locked-complex / manifold). Rebuild each pass; quadrics are not.
        ano_count_edges(ekeys, ecnt, ecap, wtri, tris_before);
        for (size_t v = 0; v < vertex_count; ++v)
            kind[v] = (adjCounts[v] == 0) ? ANO_VK_LOCKED : ANO_VK_MANIFOLD;
        for (size_t s = 0; s < ecap; ++s) {
            if (ekeys[s] == 0) continue;
            uint32_t lo = (uint32_t)(ekeys[s] >> 32), hi = (uint32_t)(ekeys[s] & 0xFFFFFFFFu);
            if (ecnt[s] == 1) {
                if (kind[lo] != ANO_VK_LOCKED) kind[lo] = ANO_VK_BORDER;
                if (kind[hi] != ANO_VK_LOCKED) kind[hi] = ANO_VK_BORDER;
            } else if (ecnt[s] > 2) {
                kind[lo] = ANO_VK_LOCKED; kind[hi] = ANO_VK_LOCKED;
            }
        }

        // Cheapest legal collapse per vertex.
        size_t ncand = 0;
        for (uint32_t v = 0; v < (uint32_t)vertex_count; ++v) {
            if (adjCounts[v] == 0 || kind[v] == ANO_VK_LOCKED) continue;
            float best = FLT_MAX; uint32_t bestnb;
            for (uint32_t a = 0; a < adjCounts[v]; ++a) {
                uint32_t t = adjData[adjOff[v] + a];
                for (int k = 0; k < 3; ++k) {
                    uint32_t nb = wtri[t*3+k];
                    if (nb == v) continue;
                    if (kind[v] == ANO_VK_BORDER) {
                        // Border slides only along a border edge to another border vert.
                        if (kind[nb] != ANO_VK_BORDER) continue;
                        if (ano_edge_get(ekeys, ecnt, ecap, v, nb) != 1) continue;
                    } else {
                        if (kind[nb] == ANO_VK_LOCKED) continue;
                        // Feature-slide: feature vert snaps only onto another feature vert (crease/rim along itself, never inward).
                        if (feature[v] && !feature[nb]) continue;
                    }
                    // Reject move longer than growth cap (inert when off).
                    float dv[3]; v3_sub(&npos[nb*3], &npos[v*3], dv);
                    if (dot_product(dv, dv) > maxEdge2) continue;
                    float cost = ano_quadric_error(&Q[v], &npos[nb*3]);
                    if (cost < best) { best = cost; bestnb = nb; }
                }
            }
            if (best < FLT_MAX) {
                cand[ncand].cost = best; cand[ncand].v = v; cand[ncand].t = bestnb; ncand++;
            }
        }
        qsort(cand, ncand, sizeof(ano_collapse_t), ano_collapse_cmp);

        // Maximal matching, cheapest first. Skip flips. Stop at count/error budget.
        memset(locked, 0, vertex_count);
        size_t collapses = 0;
        size_t rtris = tris_before;
        for (size_t i = 0; i < ncand; ++i) {
            if (rtris <= target_tris) break;
            if (cand[i].cost > err_limit) break;  // sorted: nothing cheaper remains
            uint32_t v = cand[i].v, j = cand[i].t;
            if (locked[v] || locked[j]) continue;

            // Link: v->j manifold-safe iff shared 1-ring == <=2 apexes of edge (v,j) tris.
            // Collapse validity, not a growth guard: runs on both paths.
            if (++linkGen == 0u) { memset(linkNbr, 0, vertex_count * sizeof(uint32_t)); linkGen = 1u; }
            uint32_t apex[2] = {0u, 0u}; uint32_t napex = 0;
            for (uint32_t a = 0; a < adjCounts[j]; ++a) {          // stamp N(j); collect (v,j) apexes
                uint32_t t = adjData[adjOff[j] + a];
                uint32_t c0 = wtri[t*3+0], c1 = wtri[t*3+1], c2 = wtri[t*3+2];
                linkNbr[c0] = linkGen; linkNbr[c1] = linkGen; linkNbr[c2] = linkGen;
                if (c0 == v || c1 == v || c2 == v) {
                    uint32_t w = (c0 != v && c0 != j) ? c0 : (c1 != v && c1 != j) ? c1 : c2;
                    if (napex < 2) apex[napex] = w;
                    napex++;
                }
            }
            int link_bad = 0;
            for (uint32_t a = 0; a < adjCounts[v] && !link_bad; ++a) {
                uint32_t t = adjData[adjOff[v] + a];
                for (int k = 0; k < 3; ++k) {
                    uint32_t w = wtri[t*3+k];
                    if (w == v || w == j) continue;
                    if (linkNbr[w] != linkGen) continue;
                    if (!(napex > 0 && w == apex[0]) && !(napex > 1 && w == apex[1])) { link_bad = 1; break; }
                }
            }
            // Tetra: if apexes a0,a1 face both v and j, v->j doubles (v,a0,a1)->(j,a0,a1).
            if (!link_bad && napex == 2) {
                uint32_t a0 = apex[0], a1 = apex[1]; int vf = 0, jf = 0;
                for (uint32_t a = 0; a < adjCounts[v]; ++a) {
                    const uint32_t* c = &wtri[adjData[adjOff[v] + a]*3];
                    if ((c[0]==a0||c[1]==a0||c[2]==a0) && (c[0]==a1||c[1]==a1||c[2]==a1)) { vf = 1; break; }
                }
                for (uint32_t a = 0; vf && a < adjCounts[j]; ++a) {
                    const uint32_t* c = &wtri[adjData[adjOff[j] + a]*3];
                    if ((c[0]==a0||c[1]==a0||c[2]==a0) && (c[0]==a1||c[1]==a1||c[2]==a1)) { jf = 1; break; }
                }
                if (vf && jf) link_bad = 1;
            }
            if (link_bad) continue;

            // Flip: surviving incident tris must keep facing.
            int flip = 0; uint32_t removed = 0;
            for (uint32_t a = 0; a < adjCounts[v] && !flip; ++a) {
                uint32_t t = adjData[adjOff[v] + a];
                uint32_t c0 = wtri[t*3+0], c1 = wtri[t*3+1], c2 = wtri[t*3+2];
                if (c0 == j || c1 == j || c2 == j) { removed++; continue; }
                float o0[3], o1[3], o2[3], oe1[3], oe2[3], on[3], ne1[3], ne2[3], nn[3];
                v3_copy(&npos[c0*3], o0); v3_copy(&npos[c1*3], o1); v3_copy(&npos[c2*3], o2);
                float* mv = (c0==v) ? o0 : (c1==v) ? o1 : o2;
                v3_sub(o1, o0, oe1); v3_sub(o2, o0, oe2);
                cross_product(oe1, oe2, on);
                v3_copy(&npos[j*3], mv);            // move v onto j, recompute the same winding
                v3_sub(o1, o0, ne1); v3_sub(o2, o0, ne2);
                cross_product(ne1, ne2, nn);
                float nlen2 = dot_product(nn, nn);
                // Fold/needle + growth. nlen2==(2*newArea)^2.
                if (nlen2 < 1e-20f) { flip = 1; }               // sliver
                else if (maxEdge2 == FLT_MAX) {                 // guards off: sign fold only
                    if (dot_product(on, nn) < 0.0f) flip = 1;
                } else {                                        // guards on: drift + fold + growth
                    // Drift: reject if face rotated >60deg from pass-0 origin normal (squared cos).
                    const float* onr = &orig_n[t*3];
                    float dnd = onr[0]*nn[0] + onr[1]*nn[1] + onr[2]*nn[2];
                    if (dnd < 0.0f || dnd*dnd <
                            ANO_SIMPLIFY_MAX_NORMAL_DRIFT_COS * ANO_SIMPLIFY_MAX_NORMAL_DRIFT_COS * nlen2) {
                        flip = 1; break;
                    }
                    // Guard 4: reject >~75deg fold. RHS = geo-mean of old/new areas.
                    float olen2 = dot_product(on, on);
                    if (dot_product(on, nn) <= 0.25f * sqrtf(olen2 * nlen2)) flip = 1;
                    else {
                        // Growth cap (anti-bridge): reject if any resulting edge exceeds maxEdge2.
                        // Fold misses this 〜 a bridge keeps its facing.
                        float ne3[3]; v3_sub(o2, o1, ne3);
                        if (dot_product(ne1, ne1) > maxEdge2 || dot_product(ne2, ne2) > maxEdge2 ||
                            dot_product(ne3, ne3) > maxEdge2) flip = 1;
                    }
                }
            }
            if (flip) continue;

            collapse[v] = j; locked[v] = 1; locked[j] = 1;
            // Guard 6: lock all corners of v's incident tris so the edge-cap is a per-pass invariant
            // (not a ~3x per-collapse bound against mutated geometry). Inert when off.
            if (maxEdge2 != FLT_MAX) {
                for (uint32_t a = 0; a < adjCounts[v]; ++a) {
                    uint32_t t = adjData[adjOff[v] + a];
                    locked[wtri[t*3+0]] = 1; locked[wtri[t*3+1]] = 1; locked[wtri[t*3+2]] = 1;
                }
            }
            ano_quadric_add(&Q[j], &Q[v]);
            rtris -= removed;
            if (cand[i].cost > result_err2) result_err2 = cand[i].cost;
            collapses++;
        }

        // Rebuild tris through collapse map. Drop degenerates.
        size_t newtris = 0;
        for (size_t t = 0; t < tris_before; ++t) {
            uint32_t a = ano_resolve(collapse, wtri[t*3+0]);
            uint32_t b = ano_resolve(collapse, wtri[t*3+1]);
            uint32_t c = ano_resolve(collapse, wtri[t*3+2]);
            if (a == b || b == c || a == c) continue;
            wtmp[newtris*3+0] = a; wtmp[newtris*3+1] = b; wtmp[newtris*3+2] = c;
            if (orig_n) v3_copy(&orig_n[t*3], &orig_n_tmp[newtris*3]);   // keep aligned with wtmp
            newtris++;
        }
        uint32_t* swap = wtri; wtri = wtmp; wtmp = swap;
        if (orig_n) { float* ns = orig_n; orig_n = orig_n_tmp; orig_n_tmp = ns; }  // parallel swap
        tris = newtris;

        if (collapses == 0 || tris >= tris_before) break;  // converged / no progress
    }

    // outid: survivor keeps own id (seam wedges distinct); collapsed snaps to canonical survivor.
    for (uint32_t o = 0; o < (uint32_t)vertex_count; ++o) {
        uint32_t c = remap[o];
        uint32_t r = ano_resolve(collapse, c);
        outid[o] = (r == c) ? o : r;
    }

    // Emit source tris that survive in canonical position space (drop even when kept corners are distinct seam wedges).
    size_t outcount = 0;
    for (size_t t = 0; t < tri0; ++t) {
        uint32_t o0 = indices[t*3+0], o1 = indices[t*3+1], o2 = indices[t*3+2];
        uint32_t r0 = ano_resolve(collapse, remap[o0]);
        uint32_t r1 = ano_resolve(collapse, remap[o1]);
        uint32_t r2 = ano_resolve(collapse, remap[o2]);
        if (r0 == r1 || r1 == r2 || r0 == r2) continue;
        destination[outcount++] = outid[o0];
        destination[outcount++] = outid[o1];
        destination[outcount++] = outid[o2];
    }

    if (out_result_error) *out_result_error = sqrtf(result_err2) * extent;

    return outcount;   // scratch discharged by the scoped heap
}
