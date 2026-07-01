#include "ano_meshoptimizer.h"
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

// Tuned to minimize the ACMR of a GPU that has a cache profile similar to NVidia and AMD
static const vertex_score_table_t kVertexScoreTable = {
    {0.0f, 0.779f, 0.791f, 0.789f, 0.981f, 0.843f, 0.726f, 0.847f, 0.882f, 0.867f, 0.799f, 0.642f, 0.613f, 0.600f, 0.568f, 0.372f, 0.234f},
    {0.0f, 0.995f, 0.713f, 0.450f, 0.404f, 0.059f, 0.005f, 0.147f, 0.006f}
};

typedef struct {
    uint32_t* counts;
    uint32_t* offsets;
    uint32_t* data;
} triangle_adjacency_t;

static inline float get_vertex_score(const vertex_score_table_t* table, int cache_position, uint32_t live_triangles) {
    assert(cache_position >= -1 && cache_position < (int)K_CACHE_SIZE_MAX);
    uint32_t live_triangles_clamped = live_triangles < K_VALENCE_MAX ? live_triangles : K_VALENCE_MAX;
    return table->cache[1 + cache_position] + table->live[live_triangles_clamped];
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
        if (!emitted_flags[*input_cursor]) {
            return *input_cursor;
        }
        (*input_cursor)++;
    }
    return ~0u;
}

static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}


size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles) {
    size_t safe_index_count = index_count - (index_count % 3);
    if (max_vertices < 3) return 0;
    if (max_triangles < 1) return 0;

    // Clamp limits for safety
    if (max_vertices > 256) max_vertices = 256;
    if (max_triangles > 256) max_triangles = 256;

    size_t max_vertices_conservative = max_vertices - 2;
    size_t meshlet_limit_vertices = (safe_index_count + max_vertices_conservative - 1) / max_vertices_conservative;
    size_t meshlet_limit_triangles = (safe_index_count / 3 + max_triangles - 1) / max_triangles;

    return meshlet_limit_vertices > meshlet_limit_triangles ? meshlet_limit_vertices : meshlet_limit_triangles;
}

void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count) {
    assert(index_count % 3 == 0);

    if (index_count == 0 || vertex_count == 0) {
        return;
    }

    const uint32_t* src_indices = indices;
    uint32_t* indices_copy = NULL;

    // support in-place optimization
    if (destination == indices) {
        indices_copy = (uint32_t*)malloc(index_count * sizeof(uint32_t));
        if (!indices_copy) {
            return;
        }
        memcpy(indices_copy, indices, index_count * sizeof(uint32_t));
        src_indices = indices_copy;
    }

    uint32_t cache_size = 16;
    size_t face_count = index_count / 3;

    // Partition a single block of scratch memory for all helper arrays
    size_t counts_offset = 0;
    size_t offsets_offset = align_up(counts_offset + vertex_count * sizeof(uint32_t), 16);
    size_t data_offset = align_up(offsets_offset + vertex_count * sizeof(uint32_t), 16);
    size_t emitted_flags_offset = align_up(data_offset + index_count * sizeof(uint32_t), 16);
    size_t vertex_scores_offset = align_up(emitted_flags_offset + face_count * sizeof(uint8_t), 16);
    size_t triangle_scores_offset = align_up(vertex_scores_offset + vertex_count * sizeof(float), 16);
    size_t total_memory_size = align_up(triangle_scores_offset + face_count * sizeof(float), 16);

    void* scratch = malloc(total_memory_size);
    if (!scratch) {
        free(indices_copy);
        return;
    }

    uint32_t* counts = (uint32_t*)((char*)scratch + counts_offset);
    uint32_t* offsets = (uint32_t*)((char*)scratch + offsets_offset);
    uint32_t* data = (uint32_t*)((char*)scratch + data_offset);
    uint8_t* emitted_flags = (uint8_t*)((char*)scratch + emitted_flags_offset);
    float* vertex_scores = (float*)((char*)scratch + vertex_scores_offset);
    float* triangle_scores = (float*)((char*)scratch + triangle_scores_offset);

    memset(counts, 0, vertex_count * sizeof(uint32_t));
    memset(emitted_flags, 0, face_count * sizeof(uint8_t));

    triangle_adjacency_t adjacency;
    adjacency.counts = counts;
    adjacency.offsets = offsets;
    adjacency.data = data;

    build_triangle_adjacency(&adjacency, src_indices, index_count, vertex_count);

    uint32_t* live_triangles = adjacency.counts;
    const vertex_score_table_t* table = &kVertexScoreTable;

    for (size_t i = 0; i < vertex_count; ++i) {
        vertex_scores[i] = get_vertex_score(table, -1, live_triangles[i]);
    }

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

            uint32_t* neighbors = &adjacency.data[0] + adjacency.offsets[index];
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

            if (adjacency.counts[index] == 0) {
                continue;
            }

            int cache_position = i >= cache_size ? -1 : (int)i;

            float score = get_vertex_score(table, cache_position, live_triangles[index]);
            float score_diff = score - vertex_scores[index];

            vertex_scores[index] = score;

            const uint32_t* neighbors_begin = &adjacency.data[0] + adjacency.offsets[index];
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

    free(scratch);
    free(indices_copy);
}

size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles) {
    size_t meshlet_count = 0;
    
    // Clamp parameters to stay within local scratchpad bounds and prevent uint8_t index overflows
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
        
        // Local linear scan for vertex reuse (cache-friendly, SIMD-amenable)
        for (uint32_t j = 0; j < num_current_vertices; ++j) {
            if (current_meshlet_vertices[j] == a) a_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == b) b_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == c) c_idx = (int32_t)j;
        }
        
        if (a_idx == -1) added_vertices++;
        if (b_idx == -1 && a != b) added_vertices++;
        if (c_idx == -1 && c != a && c != b) added_vertices++;
        
        // If limits exceeded, commit current meshlet and reset
        if (num_current_vertices + added_vertices > max_vertices || num_current_triangles >= max_triangles) {
            if (num_current_triangles > 0) {
                meshlets[meshlet_count].vertex_offset = vertex_offset;
                meshlets[meshlet_count].triangle_offset = triangle_offset;
                meshlets[meshlet_count].vertex_count = num_current_vertices;
                meshlets[meshlet_count].triangle_count = num_current_triangles;
                meshlet_count++;
                
                for (uint32_t j = 0; j < num_current_vertices; ++j) {
                    meshlet_vertices[vertex_offset + j] = current_meshlet_vertices[j];
                }
                
                vertex_offset += num_current_vertices;
                triangle_offset += num_current_triangles * 3;
            }
            
            num_current_vertices = 0;
            num_current_triangles = 0;
            a_idx = -1; b_idx = -1; c_idx = -1;
        }
        
        // Add new vertices and assign indices. We map local index sentinels immediately to avoid duplicate vertex insertion bugs.
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
        
        // Add triangle
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 0] = (uint8_t)a_idx;
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 1] = (uint8_t)b_idx;
        meshlet_triangles[triangle_offset + num_current_triangles * 3 + 2] = (uint8_t)c_idx;
        num_current_triangles++;
    }
    
    // Commit the final meshlet
    if (num_current_triangles > 0) {
        meshlets[meshlet_count].vertex_offset = vertex_offset;
        meshlets[meshlet_count].triangle_offset = triangle_offset;
        meshlets[meshlet_count].vertex_count = num_current_vertices;
        meshlets[meshlet_count].triangle_count = num_current_triangles;
        meshlet_count++;
        
        for (uint32_t j = 0; j < num_current_vertices; ++j) {
            meshlet_vertices[vertex_offset + j] = current_meshlet_vertices[j];
        }
    }
    
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

ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles, 
                                                    size_t triangle_count, const float* vertex_positions, 
                                                    size_t vertex_count, size_t vertex_positions_stride) {
    (void)vertex_count; // Unused in this direct calculation
    ano_meshlet_bounds_gpu_t bounds;
    memset(&bounds, 0, sizeof(bounds));
    
    if (triangle_count == 0) return bounds;

    // 1. Calculate Bounding Sphere (Ritter's bounding sphere algorithm)
    // Find a starting point (use the first vertex in the meshlet)
    uint32_t first_global_idx = meshlet_vertices[meshlet_triangles[0]];
    const float* p_start = (const float*)((const char*)vertex_positions + first_global_idx * vertex_positions_stride);

    // Find the point furthest from p_start
    float max_dist_sq = -1.0f;
    const float* p_min = p_start;
    for (size_t i = 0; i < triangle_count * 3; ++i) {
        uint32_t global_idx = meshlet_vertices[meshlet_triangles[i]];
        const float* pos = (const float*)((const char*)vertex_positions + global_idx * vertex_positions_stride);
        float d0 = pos[0] - p_start[0];
        float d1 = pos[1] - p_start[1];
        float d2 = pos[2] - p_start[2];
        float dist_sq = d0*d0 + d1*d1 + d2*d2;
        if (dist_sq > max_dist_sq) {
            max_dist_sq = dist_sq;
            p_min = pos;
        }
    }

    // Find the point furthest from p_min
    max_dist_sq = -1.0f;
    const float* p_max = p_min;
    for (size_t i = 0; i < triangle_count * 3; ++i) {
        uint32_t global_idx = meshlet_vertices[meshlet_triangles[i]];
        const float* pos = (const float*)((const char*)vertex_positions + global_idx * vertex_positions_stride);
        float d0 = pos[0] - p_min[0];
        float d1 = pos[1] - p_min[1];
        float d2 = pos[2] - p_min[2];
        float dist_sq = d0*d0 + d1*d1 + d2*d2;
        if (dist_sq > max_dist_sq) {
            max_dist_sq = dist_sq;
            p_max = pos;
        }
    }

    // Initialize sphere with center at the midpoint and radius as half the distance
    bounds.center[0] = (p_min[0] + p_max[0]) * 0.5f;
    bounds.center[1] = (p_min[1] + p_max[1]) * 0.5f;
    bounds.center[2] = (p_min[2] + p_max[2]) * 0.5f;
    float rad_sq = max_dist_sq * 0.25f;
    bounds.radius = sqrtf(rad_sq);

    // Refine: check if all points are inside. If not, grow the sphere.
    for (size_t i = 0; i < triangle_count * 3; ++i) {
        uint32_t global_idx = meshlet_vertices[meshlet_triangles[i]];
        const float* pos = (const float*)((const char*)vertex_positions + global_idx * vertex_positions_stride);
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

    // 2. Calculate Bounding Cone
    float avg_normal[3] = { 0.0f, 0.0f, 0.0f };
    float triangle_normals[256][3]; // Max 256 triangles
    size_t valid_normals = 0;

    size_t cone_triangle_count = triangle_count > 256 ? 256 : triangle_count;

    for (size_t i = 0; i < cone_triangle_count; ++i) {
        uint32_t idx0 = meshlet_vertices[meshlet_triangles[i * 3 + 0]];
        uint32_t idx1 = meshlet_vertices[meshlet_triangles[i * 3 + 1]];
        uint32_t idx2 = meshlet_vertices[meshlet_triangles[i * 3 + 2]];

        const float* p0 = (const float*)((const char*)vertex_positions + idx0 * vertex_positions_stride);
        const float* p1 = (const float*)((const char*)vertex_positions + idx1 * vertex_positions_stride);
        const float* p2 = (const float*)((const char*)vertex_positions + idx2 * vertex_positions_stride);

        float edge1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        float edge2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        
        float normal[3];
        cross_product(edge1, edge2, normal);
        
        float len = sqrtf(dot_product(normal, normal));
        if (len > 1e-6f) {
            // Keep the normal unnormalized when adding to avg_normal so it's weighted by triangle area
            avg_normal[0] += normal[0];
            avg_normal[1] += normal[1];
            avg_normal[2] += normal[2];
            
            // Normalize for storing in the list (used for angle deviation cutoff)
            triangle_normals[valid_normals][0] = normal[0] / len;
            triangle_normals[valid_normals][1] = normal[1] / len;
            triangle_normals[valid_normals][2] = normal[2] / len;
            valid_normals++;
        }
    }

    if (valid_normals > 0) {
        float avg_len = sqrtf(dot_product(avg_normal, avg_normal));
        if (avg_len > 1e-6f) {
            bounds.cone_axis[0] = avg_normal[0] / avg_len;
            bounds.cone_axis[1] = avg_normal[1] / avg_len;
            bounds.cone_axis[2] = avg_normal[2] / avg_len;
        } else {
            bounds.cone_axis[0] = 0.0f; bounds.cone_axis[1] = 0.0f; bounds.cone_axis[2] = 1.0f;
        }

        float min_dot = 1.0f;
        for (size_t i = 0; i < valid_normals; ++i) {
            float d = dot_product(bounds.cone_axis, triangle_normals[i]);
            if (d < min_dot) min_dot = d;
        }

        // Add a small epsilon to cutoff for safety, cap to max spread (backface culling invalid if < 0)
        bounds.cone_cutoff = min_dot - 0.05f; 
        if (bounds.cone_cutoff < -1.0f) bounds.cone_cutoff = -1.0f;
        
        // Apex is placed at the sphere center for culling.
        bounds.cone_apex[0] = bounds.center[0];
        bounds.cone_apex[1] = bounds.center[1];
        bounds.cone_apex[2] = bounds.center[2];
    } else {
        bounds.cone_axis[0] = 0.0f; bounds.cone_axis[1] = 0.0f; bounds.cone_axis[2] = 1.0f;
        bounds.cone_cutoff = -1.0f; // Invalid
    }

    return bounds;
}

// ---------------------------------------------------------------------------
// Mesh simplification (LOD production). Quadric-error endpoint-snap edge collapse
// (Garland-Heckbert quadrics). The collapse always snaps one EXISTING endpoint onto the other —
// it never invents a vertex position — so every LOD level is the same vertex buffer plus a shorter
// decimated index buffer. Coincident positions are welded so uv/normal seam-split vertices collapse
// as one topological point; open boundary vertices are restricted to a polyline so silhouettes do
// not erode. Geometry-only: attributes are not folded into the quadric (a later refinement).
// ---------------------------------------------------------------------------

// 11-float symmetric error quadric. error(v) = vT A v + 2 bT v + c, with all terms pre-weighted; w
// is the accumulated weight so quadric_error can return a roughly area-independent squared distance.
typedef struct {
    float a00, a11, a22;
    float a10, a20, a21;
    float b0, b1, b2;
    float c;
    float w;
} ano_quadric_t;

// A scored, directed collapse: snap vertex v onto vertex t, costing `cost` (normalized squared dist).
typedef struct { float cost; uint32_t v; uint32_t t; } ano_collapse_t;

static const float ANO_BORDER_WEIGHT = 10.0f;  // border constraint quadric weight (vs ~unit face area)
static const size_t ANO_SIMPLIFY_MAX_PASSES = 1000u;
static const float ANO_SIMPLIFY_AREA_EPS2 = 1e-24f;    // (2*area)^2 drop threshold; matches the |cross|<1e-12
                                                       // face-plane skip so Guard 5 drops only no-quadric faces
static const float ANO_SIMPLIFY_ABS_EDGE_FRAC = 0.25f; // absolute edge cap as a fraction of the largest bbox
                                                       // axis (normalized extent==1); backstops coarse flats

static inline uint32_t ano_float_bits(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static inline size_t ano_ceil_pow2(size_t x) { size_t p = 16; while (p < x) p <<= 1; return p; }

// Plane (a,b,c unit normal, offset d) weighted by w, as a quadric.
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

// Weighted-average squared distance of point p to the quadric (>= 0).
static inline float ano_quadric_error(const ano_quadric_t* q, const float* p) {
    float x = p[0], y = p[1], z = p[2];
    float r = q->a00*x*x + q->a11*y*y + q->a22*z*z
            + 2.0f*(q->a10*x*y + q->a20*x*z + q->a21*y*z)
            + 2.0f*(q->b0*x + q->b1*y + q->b2*z)
            + q->c;
    r = fabsf(r);
    return q->w > 1e-12f ? r / q->w : r;
}

// Resolve a collapse chain with path halving. collapse[i]==i means i survives.
static uint32_t ano_resolve(uint32_t* collapse, uint32_t i) {
    while (collapse[i] != i) {
        collapse[i] = collapse[collapse[i]];
        i = collapse[i];
    }
    return i;
}

// Undirected-edge open-addressing multiset over a fixed pow2 table. Key packs (lo<<32)|hi with
// lo<hi, so it is never 0 (the empty sentinel). Used per pass to count edge uses: 1 == border,
// 2 == manifold, >2 == complex (non-manifold).
static void ano_edge_add(uint64_t* keys, uint32_t* cnt, size_t cap, uint32_t lo, uint32_t hi) {
    uint64_t key = ((uint64_t)lo << 32) | (uint64_t)hi;
    size_t mask = cap - 1;
    size_t h = (size_t)((key * 11400714819323198485ull) >> 32) & mask;
    for (;;) {
        if (keys[h] == key) { cnt[h]++; return; }
        if (keys[h] == 0)   { keys[h] = key; cnt[h] = 1; return; }
        h = (h + 1) & mask;
    }
}

static uint32_t ano_edge_get(const uint64_t* keys, const uint32_t* cnt, size_t cap, uint32_t lo, uint32_t hi) {
    uint64_t key = ((uint64_t)lo << 32) | (uint64_t)hi;
    size_t mask = cap - 1;
    size_t h = (size_t)((key * 11400714819323198485ull) >> 32) & mask;
    for (;;) {
        uint64_t k = keys[h];
        if (k == key) return cnt[h];
        if (k == 0)   return 0;
        h = (h + 1) & mask;
    }
}

static int ano_collapse_cmp(const void* a, const void* b) {
    float ca = ((const ano_collapse_t*)a)->cost, cb = ((const ano_collapse_t*)b)->cost;
    return (ca < cb) ? -1 : (ca > cb) ? 1 : 0;
}

// Rebuild the undirected edge-use multiset for the current triangle list. cnt==1 border,
// 2 manifold, >2 complex. Cleared then filled; keys/cnt sized to a pow2 >= ~2x unique edges.
static void ano_count_edges(uint64_t* keys, uint32_t* cnt, size_t cap, const uint32_t* tri, size_t ntri) {
    memset(keys, 0, cap * sizeof(uint64_t));
    memset(cnt, 0, cap * sizeof(uint32_t));
    for (size_t t = 0; t < ntri; ++t) {
        uint32_t a = tri[t*3+0], b = tri[t*3+1], c = tri[t*3+2];
        ano_edge_add(keys, cnt, cap, a<b?a:b, a<b?b:a);
        ano_edge_add(keys, cnt, cap, b<c?b:c, b<c?c:b);
        ano_edge_add(keys, cnt, cap, a<c?a:c, a<c?c:a);
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

// Guard-disabled baseline: identical behavior to the original simplifier (edge_len_factor <= 0). Kept
// so the tests and the A/B-comparison path exercise the exact pre-guard collapse decisions.
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

    // Target already met by the input: no collapses, but still drop any degenerate (coincident-
    // position) source triangle so the result is always a valid subset mesh, exactly as the
    // simplified path guarantees — output validity must not depend on target_index_count.
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

    // Single allocation sweep; on any failure, pass the input through unchanged.
    float* npos          = (float*)malloc(vertex_count * 3 * sizeof(float));
    uint32_t* remap      = (uint32_t*)malloc(vertex_count * sizeof(uint32_t));
    uint32_t* collapse   = (uint32_t*)malloc(vertex_count * sizeof(uint32_t));
    ano_quadric_t* Q     = (ano_quadric_t*)malloc(vertex_count * sizeof(ano_quadric_t));
    uint8_t* kind        = (uint8_t*)malloc(vertex_count);
    uint8_t* locked      = (uint8_t*)malloc(vertex_count);
    uint32_t* outid      = (uint32_t*)malloc(vertex_count * sizeof(uint32_t));
    uint32_t* adjCounts  = (uint32_t*)malloc(vertex_count * sizeof(uint32_t));
    uint32_t* adjOff     = (uint32_t*)malloc(vertex_count * sizeof(uint32_t));
    uint32_t* adjData    = (uint32_t*)malloc(ic * sizeof(uint32_t));
    uint32_t* wtri       = (uint32_t*)malloc(ic * sizeof(uint32_t));
    uint32_t* wtmp       = (uint32_t*)malloc(ic * sizeof(uint32_t));
    ano_collapse_t* cand = (ano_collapse_t*)malloc(vertex_count * sizeof(ano_collapse_t));
    size_t weldCap = ano_ceil_pow2(vertex_count * 2 + 16);
    int32_t* weld        = (int32_t*)malloc(weldCap * sizeof(int32_t));
    size_t ecap = ano_ceil_pow2(tri0 * 4 + 16);
    uint64_t* ekeys      = (uint64_t*)malloc(ecap * sizeof(uint64_t));
    uint32_t* ecnt       = (uint32_t*)malloc(ecap * sizeof(uint32_t));

    if (!npos || !remap || !collapse || !Q || !kind || !locked || !outid || !adjCounts || !adjOff ||
        !adjData || !wtri || !wtmp || !cand || !weld || !ekeys || !ecnt) {
        free(npos); free(remap); free(collapse); free(Q); free(kind); free(locked); free(outid);
        free(adjCounts); free(adjOff); free(adjData); free(wtri); free(wtmp); free(cand);
        free(weld); free(ekeys); free(ecnt);
        if (destination != indices) memcpy(destination, indices, ic * sizeof(uint32_t));
        return ic;
    }

    // Normalize positions to a unit-extent space so quadric magnitudes stay well-conditioned;
    // +0.0f folds -0.0 to +0.0 so seam wedges at the origin weld. Errors convert back via *extent.
    const char* vbase = (const char*)vertex_positions;
    for (size_t i = 0; i < vertex_count; ++i) {
        const float* p = (const float*)(vbase + i * vertex_positions_stride);
        npos[i*3+0] = p[0] * invscale + 0.0f;
        npos[i*3+1] = p[1] * invscale + 0.0f;
        npos[i*3+2] = p[2] * invscale + 0.0f;
    }

    // Position weld: remap[v] = lowest-indexed vertex sharing v's exact position (the canonical id
    // used for all topology/collapse decisions). Surviving wedges keep their own ids in the output.
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
    free(weld);

    // Working triangle list in canonical (welded) space; drop triangles already degenerate post-weld.
    size_t tris = 0;
    for (size_t t = 0; t < tri0; ++t) {
        uint32_t a = remap[indices[t*3+0]], b = remap[indices[t*3+1]], c = remap[indices[t*3+2]];
        if (a == b || b == c || a == c) continue;
        // Guard 5: drop distinct-index but position-collinear source triangles. They contribute no
        // quadric (same |cross|<1e-12 threshold as the face-plane accumulation below) yet add spurious
        // edges that skew the per-pass border/manifold classification. Working set only; emit untouched.
        if (edge_len_factor > 0.0f) {
            const float* pa = &npos[a*3]; const float* pb = &npos[b*3]; const float* pc = &npos[c*3];
            float g1[3] = { pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2] };
            float g2[3] = { pc[0]-pa[0], pc[1]-pa[1], pc[2]-pa[2] };
            float gc[3]; cross_product(g1, g2, gc);
            if (dot_product(gc, gc) < ANO_SIMPLIFY_AREA_EPS2) continue;
        }
        wtri[tris*3+0] = a; wtri[tris*3+1] = b; wtri[tris*3+2] = c; tris++;
    }

    size_t target_tris = target_ic / 3;
    float err_limit = target_error * target_error;  // relative error -> normalized squared distance
    float result_err2 = 0.0f;

    triangle_adjacency_t adj = { adjCounts, adjOff, adjData };

    // Accumulate quadrics ONCE over the original welded mesh: area-weighted face planes plus an
    // in-plane perpendicular constraint on every original border edge (resists boundary slide).
    // Collapses MERGE quadrics into survivors, so error is always measured against the original
    // surface, not the partially-simplified one — that is what makes target_error a real budget.
    ano_count_edges(ekeys, ecnt, ecap, wtri, tris);
    memset(Q, 0, vertex_count * sizeof(ano_quadric_t));
    for (size_t t = 0; t < tris; ++t) {
        uint32_t ia = wtri[t*3+0], ib = wtri[t*3+1], ic2 = wtri[t*3+2];
        const float* p0 = &npos[ia*3]; const float* p1 = &npos[ib*3]; const float* p2 = &npos[ic2*3];
        float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        float n[3]; cross_product(e1, e2, n);
        float len = sqrtf(dot_product(n, n));
        if (len < 1e-12f) continue;
        float area = len * 0.5f;
        float nx = n[0]/len, ny = n[1]/len, nz = n[2]/len;
        float d = -(nx*p0[0] + ny*p0[1] + nz*p0[2]);
        ano_quadric_t fq; ano_quadric_from_plane(&fq, nx, ny, nz, d, area);
        ano_quadric_add(&Q[ia], &fq); ano_quadric_add(&Q[ib], &fq); ano_quadric_add(&Q[ic2], &fq);

        const uint32_t tri[3] = { ia, ib, ic2 };
        for (int k = 0; k < 3; ++k) {
            uint32_t x = tri[k], y = tri[(k+1)%3], z = tri[(k+2)%3];
            if (ano_edge_get(ekeys, ecnt, ecap, x<y?x:y, x<y?y:x) != 1) continue;
            const float* q0 = &npos[x*3]; const float* q1 = &npos[y*3]; const float* q2 = &npos[z*3];
            float ed[3] = { q1[0]-q0[0], q1[1]-q0[1], q1[2]-q0[2] };
            float el = sqrtf(dot_product(ed, ed));
            if (el < 1e-12f) continue;
            float eu[3] = { ed[0]/el, ed[1]/el, ed[2]/el };
            float tv[3] = { q2[0]-q0[0], q2[1]-q0[1], q2[2]-q0[2] };
            float kdot = dot_product(tv, eu);
            float pe[3] = { tv[0]-eu[0]*kdot, tv[1]-eu[1]*kdot, tv[2]-eu[2]*kdot };
            float pl = sqrtf(dot_product(pe, pe));
            if (pl < 1e-12f) continue;
            float pnx = pe[0]/pl, pny = pe[1]/pl, pnz = pe[2]/pl;
            float bd = -(pnx*q0[0] + pny*q0[1] + pnz*q0[2]);
            float bw = el * ANO_BORDER_WEIGHT;
            ano_quadric_t bq; ano_quadric_from_plane(&bq, pnx, pny, pnz, bd, bw);
            ano_quadric_add(&Q[x], &bq); ano_quadric_add(&Q[y], &bq);
        }
    }

    // In-plane degeneracy guard scale. QEM error is ~0 for moving a vertex anywhere within a coplanar
    // surface, so nothing below bounds triangle GROWTH; chained in-plane collapses otherwise let one
    // survivor's 1-ring span an entire flat region (a floor/wall bridge). Cap the resulting-triangle
    // longest edge and the collapse move distance at edge_len_factor * mean source edge (normalized
    // unit-extent space, so it adapts to mesh density; on tiny meshes it exceeds the extent and is inert).
    // Computed ONCE over the welded source, giving an absolute cap that forces roughly uniform coarsening.
    // edge_len_factor <= 0 disables it (maxEdge2 = FLT_MAX): today's exact behavior, the A/B baseline.
    float maxEdge2 = FLT_MAX;
    if (edge_len_factor > 0.0f && tris > 0) {
        double edgesum = 0.0;
        for (size_t t = 0; t < tris; ++t) {
            const float* q0 = &npos[wtri[t*3+0]*3];
            const float* q1 = &npos[wtri[t*3+1]*3];
            const float* q2 = &npos[wtri[t*3+2]*3];
            float d0[3] = { q1[0]-q0[0], q1[1]-q0[1], q1[2]-q0[2] };
            float d1[3] = { q2[0]-q1[0], q2[1]-q1[1], q2[2]-q1[2] };
            float d2[3] = { q0[0]-q2[0], q0[1]-q2[1], q0[2]-q2[2] };
            edgesum += sqrt((double)dot_product(d0, d0))
                     + sqrt((double)dot_product(d1, d1))
                     + sqrt((double)dot_product(d2, d2));
        }
        float mean_edge = (float)(edgesum / (double)(tris * 3));
        if (mean_edge > 0.0f) {
            float m = edge_len_factor * mean_edge;
            // Absolute backstop: on a COARSE flat surface edge_len_factor*mean_edge is itself a large
            // fraction of the extent, so a surface-spanning triangle still fits under it and the bridge
            // re-forms. Clamp to a fraction of the largest axis (normalized extent==1) so no surviving
            // edge can cross the surface at any mesh density. Inert on dense meshes (8*mean < 0.25).
            if (m > ANO_SIMPLIFY_ABS_EDGE_FRAC) m = ANO_SIMPLIFY_ABS_EDGE_FRAC;
            maxEdge2 = m * m;
        }
    }

    for (size_t pass = 0; pass < ANO_SIMPLIFY_MAX_PASSES && tris > target_tris; ++pass) {
        size_t tris_before = tris;

        // Incident-triangle lists per canonical vertex (counts[v] == incident triangle count).
        build_triangle_adjacency(&adj, wtri, tris_before * 3, vertex_count);

        // Current edge-use counts -> per-vertex kind (border / locked-complex / manifold). Topology
        // changes as the mesh simplifies, so kinds are rebuilt each pass; the quadrics are not.
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

        // Score the cheapest legal collapse out of each vertex.
        size_t ncand = 0;
        for (uint32_t v = 0; v < (uint32_t)vertex_count; ++v) {
            if (adjCounts[v] == 0 || kind[v] == ANO_VK_LOCKED) continue;
            float best = FLT_MAX; uint32_t bestnb = v;
            for (uint32_t a = 0; a < adjCounts[v]; ++a) {
                uint32_t t = adjData[adjOff[v] + a];
                for (int k = 0; k < 3; ++k) {
                    uint32_t nb = wtri[t*3+k];
                    if (nb == v) continue;
                    if (kind[v] == ANO_VK_BORDER) {
                        // Border may only slide to another border vertex along a border edge.
                        if (kind[nb] != ANO_VK_BORDER) continue;
                        if (ano_edge_get(ekeys, ecnt, ecap, v<nb?v:nb, v<nb?nb:v) != 1) continue;
                    } else {
                        if (kind[nb] == ANO_VK_LOCKED) continue;
                    }
                    // Pre-filter: never snap v across more than the growth cap (bounds the move length;
                    // the resulting-triangle cap in the flip guard is the hard backstop). Inert when off.
                    float dv[3] = { npos[nb*3]-npos[v*3], npos[nb*3+1]-npos[v*3+1], npos[nb*3+2]-npos[v*3+2] };
                    if (dot_product(dv, dv) > maxEdge2) continue;
                    float cost = ano_quadric_error(&Q[v], &npos[nb*3]);
                    if (cost < best) { best = cost; bestnb = nb; }
                }
            }
            if (best < FLT_MAX && bestnb != v) {
                cand[ncand].cost = best; cand[ncand].v = v; cand[ncand].t = bestnb; ncand++;
            }
        }
        qsort(cand, ncand, sizeof(ano_collapse_t), ano_collapse_cmp);

        // Apply collapses cheapest-first as a maximal matching (each vertex collapses at most once
        // per pass), skipping any that would flip a triangle. Stop at the count or error budget.
        memset(locked, 0, vertex_count);
        size_t collapses = 0;
        size_t rtris = tris_before;
        for (size_t i = 0; i < ncand; ++i) {
            if (rtris <= target_tris) break;
            if (cand[i].cost > err_limit) break;  // sorted: nothing cheaper remains
            uint32_t v = cand[i].v, j = cand[i].t;
            if (locked[v] || locked[j]) continue;

            // Flip guard: every incident triangle not destroyed by the collapse must keep its facing.
            int flip = 0; uint32_t removed = 0;
            for (uint32_t a = 0; a < adjCounts[v] && !flip; ++a) {
                uint32_t t = adjData[adjOff[v] + a];
                uint32_t c0 = wtri[t*3+0], c1 = wtri[t*3+1], c2 = wtri[t*3+2];
                if (c0 == j || c1 == j || c2 == j) { removed++; continue; }
                float o0[3], o1[3], o2[3];
                o0[0]=npos[c0*3]; o0[1]=npos[c0*3+1]; o0[2]=npos[c0*3+2];
                o1[0]=npos[c1*3]; o1[1]=npos[c1*3+1]; o1[2]=npos[c1*3+2];
                o2[0]=npos[c2*3]; o2[1]=npos[c2*3+1]; o2[2]=npos[c2*3+2];
                float* mv = (c0==v) ? o0 : (c1==v) ? o1 : o2;
                float oe1[3] = { o1[0]-o0[0], o1[1]-o0[1], o1[2]-o0[2] };
                float oe2[3] = { o2[0]-o0[0], o2[1]-o0[1], o2[2]-o0[2] };
                float on[3]; cross_product(oe1, oe2, on);
                mv[0]=npos[j*3]; mv[1]=npos[j*3+1]; mv[2]=npos[j*3+2];
                float ne1[3] = { o1[0]-o0[0], o1[1]-o0[1], o1[2]-o0[2] };
                float ne2[3] = { o2[0]-o0[0], o2[1]-o0[1], o2[2]-o0[2] };
                float nn[3]; cross_product(ne1, ne2, nn);
                float nlen2 = dot_product(nn, nn);
                // Fold/needle + growth guard. nlen2==(2*newArea)^2, |on|==2*oldArea.
                if (nlen2 < 1e-20f) { flip = 1; }               // exact sliver (also guards sqrtf(0))
                else if (maxEdge2 == FLT_MAX) {                 // guards off: baseline fold-only test
                    if (dot_product(on, nn) < 0.0f) flip = 1;
                } else {                                        // guards on: tighter fold + growth cap
                    // Guard 4: reject a >~75deg normal turn (fold/cap/spike) the sign test passes; RHS is
                    // the geometric mean of old/new areas, so it is scale-free (meshopt hasTriangleFlip).
                    float olen2 = dot_product(on, on);
                    if (dot_product(on, nn) <= 0.25f * sqrtf(olen2 * nlen2)) flip = 1;
                    else {
                        // Growth cap (anti-bridge): reject if any resulting edge exceeds the cap. This is
                        // the decisive in-plane guard the fold test misses (a bridge keeps its facing).
                        // ne1=o1-o0, ne2=o2-o0 are the moved edges; ne3=o2-o1 the third.
                        float ne3[3] = { o2[0]-o1[0], o2[1]-o1[1], o2[2]-o1[2] };
                        if (dot_product(ne1, ne1) > maxEdge2 || dot_product(ne2, ne2) > maxEdge2 ||
                            dot_product(ne3, ne3) > maxEdge2) flip = 1;
                    }
                }
            }
            if (flip) continue;

            collapse[v] = j; locked[v] = 1; locked[j] = 1;
            // Guard 6: with the growth cap on, lock EVERY corner of v's incident triangles (not just v/j)
            // so no other collapse this pass moves a second corner of a triangle this one already moved.
            // That makes the resulting-edge cap a true per-pass invariant instead of a ~3x per-collapse
            // bound (every guard check above is then evaluated against un-mutated geometry). Inert when off.
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

        // Rebuild the canonical triangle list through the collapse map, dropping degenerates.
        size_t newtris = 0;
        for (size_t t = 0; t < tris_before; ++t) {
            uint32_t a = ano_resolve(collapse, wtri[t*3+0]);
            uint32_t b = ano_resolve(collapse, wtri[t*3+1]);
            uint32_t c = ano_resolve(collapse, wtri[t*3+2]);
            if (a == b || b == c || a == c) continue;
            wtmp[newtris*3+0] = a; wtmp[newtris*3+1] = b; wtmp[newtris*3+2] = c; newtris++;
        }
        uint32_t* swap = wtri; wtri = wtmp; wtmp = swap;
        tris = newtris;

        if (collapses == 0 || tris >= tris_before) break;  // converged / no progress
    }

    // Output mapping per ORIGINAL vertex: a vertex whose position survives keeps its own id (so seam
    // wedges stay distinct); a vertex whose position was collapsed snaps to the survivor canonical id.
    for (uint32_t o = 0; o < (uint32_t)vertex_count; ++o) {
        uint32_t c = remap[o];
        uint32_t r = ano_resolve(collapse, c);
        outid[o] = (r == c) ? o : r;
    }

    // Emit surviving source triangles. Degeneracy is tested in canonical position space so a triangle
    // collapsed onto one position is dropped even when its kept corners are distinct seam wedges.
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

    free(npos); free(remap); free(collapse); free(Q); free(kind); free(locked); free(outid);
    free(adjCounts); free(adjOff); free(adjData); free(wtri); free(wtmp); free(cand);
    free(ekeys); free(ecnt);
    return outcount;
}
