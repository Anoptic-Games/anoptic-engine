#ifndef ANO_MESHOPTIMIZER_H
#define ANO_MESHOPTIMIZER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * Returns the maximum number of meshlets that could be generated for a given index count.
 */
size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles);

/**
 * Optimizes the index buffer for vertex cache efficiency, which inherently clusters triangles 
 * spatially/topologically for linear packing.
 * 
 * destination must contain enough space for index_count elements.
 */
void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count);

/**
 * Builds meshlets via linear packing. The indices must ideally be optimized via ano_optimize_vertex_cache first.
 * 
 * meshlets: must contain enough space for worst-case meshlets (ano_build_meshlets_bound).
 * meshlet_vertices: array to hold the unrolled unique vertices for all meshlets (max_meshlets * max_vertices).
 * meshlet_triangles: array to hold the unrolled local triangle indices (max_meshlets * max_triangles * 3).
 * 
 * Returns the number of meshlets generated.
 */
size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles);

/**
 * Computes the bounds for a specific meshlet using Ritter's bounding sphere algorithm for the sphere 
 * and average + max deviation for the cone.
 */
ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles,
                                                    size_t triangle_count, const float* vertex_positions,
                                                    size_t vertex_count, size_t vertex_positions_stride);

/**
 * Returns the simplification scale of a vertex set: the largest bounding-box axis extent.
 * ano_simplify's target_error is expressed relative to this, and its result error comes back in the
 * same object units (result_relative * scale). Pass the same positions/stride as ano_simplify.
 *
 * vertex_positions: float[3] per vertex, byte stride vertex_positions_stride.
 * Returns the largest axis extent (0 if vertex_count == 0).
 */
float ano_simplify_scale(const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride);

/**
 * Simplifies a triangle mesh by quadric-error edge collapse, producing a shorter index buffer that
 * references a SUBSET of the SAME vertices — no new vertex positions are created, so every LOD level
 * shares one vertex buffer.
 *
 * destination: receives the simplified indices; must hold index_count entries (NOT target_index_count,
 *     since over-target intermediate states are written then compacted). May alias indices.
 * indices / index_count: source triangle list; index_count is floored to a multiple of 3.
 * vertex_positions / vertex_count / vertex_positions_stride: float[3] positions, byte stride.
 * target_index_count: desired index count (floored to a multiple of 3). The result may stop short of
 *     it when open borders or non-manifold topology block further collapses.
 * target_error: maximum collapse error allowed, relative to ano_simplify_scale (0.01 == 1% of extent).
 * out_result_error (nullable): receives the achieved error in object units (result_relative * scale).
 *
 * Returns the resulting index count (multiple of 3, <= floored index_count). Degenerate source
 * triangles (coincident-position corners) are always dropped, so the output is a valid subset mesh
 * even when target_index_count >= index_count. Coincident positions are welded so attribute (uv/normal)
 * seams collapse topologically while surviving wedges keep their own vertices; open borders are locked
 * to a polyline so silhouettes do not erode. Geometry-only: attribute drift at collapsed seam corners
 * is accepted (attribute-aware simplification is a later refinement).
 */
size_t ano_simplify(uint32_t* destination, const uint32_t* indices, size_t index_count,
                    const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                    size_t target_index_count, float target_error, float* out_result_error);

#ifdef __cplusplus
}
#endif

#endif // ANO_MESHOPTIMIZER_H
