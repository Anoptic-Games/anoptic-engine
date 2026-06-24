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
 * Returns the max meshlets generable for a given index count.
 */
size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles);

/**
 * Optimizes the index buffer for vertex cache efficiency — this clusters triangles spatially for linear packing.
 *
 * destination must hold index_count elements.
 */
void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count);

/**
 * Builds meshlets via linear packing — indices should be optimized via ano_optimize_vertex_cache first.
 *
 * meshlets: holds worst-case meshlets (ano_build_meshlets_bound).
 * meshlet_vertices: unrolled unique vertices for all meshlets (max_meshlets * max_vertices).
 * meshlet_triangles: unrolled local triangle indices (max_meshlets * max_triangles * 3).
 *
 * Returns the meshlet count.
 */
size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles);

/**
 * Computes a meshlet's bounds — Ritter's algorithm for the sphere, average + max deviation for the cone.
 */
ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles, 
                                                    size_t triangle_count, const float* vertex_positions, 
                                                    size_t vertex_count, size_t vertex_positions_stride);

#ifdef __cplusplus
}
#endif

#endif // ANO_MESHOPTIMIZER_H
