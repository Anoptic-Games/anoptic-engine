#ifndef ANO_MESHOPTIMIZER_H
#define ANO_MESHOPTIMIZER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Types */

typedef struct {
    uint32_t vertex_offset;
    uint32_t triangle_offset;
    uint32_t vertex_count;
    uint32_t triangle_count;
} ano_meshlet_t;

/* Optimized for Vulkan std430 / scalar buffer layouts */
typedef struct {
    float center[3];     float radius;       /* 16 bytes */
    float cone_apex[3];  float cone_cutoff;  /* 16 bytes */
    float cone_axis[3];  float padding;      /* 16 bytes */
} ano_meshlet_bounds_gpu_t;


/* Meshlets */

// Max meshlets for a given index count.
size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles);

// Reorders indices for vertex cache. destination holds index_count elements.
void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count);

// Builds meshlets by linear packing. Prefer cache-optimized indices first.
// meshlets: worst-case count from ano_build_meshlets_bound.
// meshlet_vertices: max_meshlets * max_vertices unique verts.
// meshlet_triangles: max_meshlets * max_triangles * 3 local indices.
// Returns meshlet count.
size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles);

// Meshlet bounds: Ritter sphere + meshopt normal cone (axis = mean unit normal,
// cutoff = sin(max deviation), 1 past hemisphere). flat.task cull:
// dot(center-eye, axis) >= cutoff*|center-eye| + radius.
ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles,
                                                    size_t triangle_count, const float* vertex_positions,
                                                    size_t vertex_count, size_t vertex_positions_stride);


/* Simplify */

// Largest bbox axis extent. ano_simplify target_error is relative to this.
// out_result_error is object units (relative * scale).
float ano_simplify_scale(const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride);

// QEM edge collapse. Same vertex buffer, shorter index buffer (subset of verts).
// destination: index_count slots, may alias indices.
// target_index_count: desired count (may stop early on locked topology).
// target_error: max relative error vs ano_simplify_scale.
// out_result_error: achieved error in object units (nullable).
// Returns index count (multiple of 3). Drops coincident-position degenerates.
// Welds coincident positions. Borders slide along border only. Geometry-only.
size_t ano_simplify(uint32_t* destination, const uint32_t* indices, size_t index_count,
                    const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                    size_t target_index_count, float target_error, float* out_result_error);

// Default edge_len_factor: max resulting edge = this * mean source edge length.
#define ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT 8.0f

// ano_simplify plus growth guards. Caps edge stretch at
// edge_len_factor * mean source edge (unit-extent space). <=0 disables guards
// (byte-identical to ano_simplify). Default 8.0. Also: abs bbox backstop,
// ~75deg fold, collinear drop, link/tetra exclusion, feature-slide (dihedral>45),
// max 60deg cumulative normal drift. Other params match ano_simplify.
size_t ano_simplify_ex(uint32_t* destination, const uint32_t* indices, size_t index_count,
                       const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                       size_t target_index_count, float target_error, float edge_len_factor,
                       float* out_result_error);

#ifdef __cplusplus
}
#endif

#endif // ANO_MESHOPTIMIZER_H
