#include "ano_meshoptimizer.h"
#include <string.h>
#include <math.h>
#include <float.h>

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
    (void)vertex_count;
    // TODO: Implement a linear-time vertex cache optimizer (e.g., Forsyth's algorithm or Tipsify).
    // For now, we perform a direct copy assuming the mesh was pre-optimized in the asset pipeline.
    memcpy(destination, indices, index_count * sizeof(uint32_t));
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
