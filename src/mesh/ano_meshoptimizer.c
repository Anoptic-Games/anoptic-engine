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

// Tuned to minimize ACMR on NVidia/AMD-like cache profiles
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

    if (index_count == 0 || vertex_count == 0) {
        return;
    }

    const uint32_t* src_indices = indices;
    uint32_t* indices_copy = NULL;

    // Support in-place optimization
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

    // Partition one scratch block across all helper arrays
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
    
    // Clamp params to scratchpad bounds — prevents uint8_t index overflow
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
        
        // Linear scan for vertex reuse — cache-friendly, SIMD-amenable
        for (uint32_t j = 0; j < num_current_vertices; ++j) {
            if (current_meshlet_vertices[j] == a) a_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == b) b_idx = (int32_t)j;
            if (current_meshlet_vertices[j] == c) c_idx = (int32_t)j;
        }
        
        if (a_idx == -1) added_vertices++;
        if (b_idx == -1 && a != b) added_vertices++;
        if (c_idx == -1 && c != a && c != b) added_vertices++;
        
        // Limits exceeded — commit current meshlet and reset
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
        
        // Add new vertices and assign indices — map sentinels immediately to avoid duplicate insertion
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
    (void)vertex_count; // Unused here
    ano_meshlet_bounds_gpu_t bounds;
    memset(&bounds, 0, sizeof(bounds));
    
    if (triangle_count == 0) return bounds;

    // 1. Bounding sphere (Ritter's algorithm)
    // Start from the meshlet's first vertex
    uint32_t first_global_idx = meshlet_vertices[meshlet_triangles[0]];
    const float* p_start = (const float*)((const char*)vertex_positions + first_global_idx * vertex_positions_stride);

    // Furthest point from p_start
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

    // Furthest point from p_min
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

    // Center at the midpoint, radius half the distance
    bounds.center[0] = (p_min[0] + p_max[0]) * 0.5f;
    bounds.center[1] = (p_min[1] + p_max[1]) * 0.5f;
    bounds.center[2] = (p_min[2] + p_max[2]) * 0.5f;
    float rad_sq = max_dist_sq * 0.25f;
    bounds.radius = sqrtf(rad_sq);

    // Refine — grow the sphere to enclose any outside point
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

    // 2. Bounding cone
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
            // Accumulate unnormalized — weights avg_normal by triangle area
            avg_normal[0] += normal[0];
            avg_normal[1] += normal[1];
            avg_normal[2] += normal[2];
            
            // Normalize for the list — used for the angle deviation cutoff
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

        // Epsilon for safety, cap to max spread — backface culling invalid if < 0
        bounds.cone_cutoff = min_dot - 0.05f; 
        if (bounds.cone_cutoff < -1.0f) bounds.cone_cutoff = -1.0f;
        
        // Apex at the sphere center for culling
        bounds.cone_apex[0] = bounds.center[0];
        bounds.cone_apex[1] = bounds.center[1];
        bounds.cone_apex[2] = bounds.center[2];
    } else {
        bounds.cone_axis[0] = 0.0f; bounds.cone_axis[1] = 0.0f; bounds.cone_axis[2] = 1.0f;
        bounds.cone_cutoff = -1.0f; // Invalid
    }

    return bounds;
}
