# ANO_MESHOPTIMIZER Implementation Plan

## 1. Goal and Scope
The goal of the `ano_meshoptimizer` side quest is to develop an in-house, highly optimized, dependency-free C library tailored strictly for generating meshlets and their corresponding bounds. This is essential to feed the new Vulkan Mesh Shading pipeline and power GPU-driven culling (frustum, occlusion, and backface).

By studying the `meshoptimizer` header, we have identified the exact subset of functions necessary to achieve feature parity for our specific pipeline:

### Identified Parity Targets
1. **`ano_build_meshlets_bound`** (Equivalent to `meshopt_buildMeshletsBound`)
2. **`ano_optimize_vertex_cache`** (Required for the two-pass packing architecture, equivalent to `meshopt_optimizeVertexCache`)
3. **`ano_build_meshlets`** (Equivalent to `meshopt_buildMeshlets`)
4. **`ano_compute_meshlet_bounds`** (Equivalent to `meshopt_computeMeshletBounds`)

## 2. Core Data Structures
We must strictly separate our CPU-side structures from our GPU-deployed structures to guarantee perfect `std430` alignment and bandwidth efficiency.

```c
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
```

## 3. Theoretical Approach to Meshlet Generation

### The Clustering Algorithm: Two-Pass Architecture
To avoid the severe CPU bottleneck of running complex float matrices or normal cone math inside the clustering loop, we will use a **two-pass architecture**:

1. **Global Reordering**: First, perform a highly efficient linear-time global optimization pass on the index buffer (e.g., vertex cache optimization). This groups topologically and spatially adjacent triangles together in linear memory.
2. **Linear Packing (`ano_build_meshlets`)**: Because the input is pre-sorted into tight neighborhoods, the meshlet builder functions solely as a highly streamlined packer. It walks the index buffer sequentially using a lightweight local frontier queue prioritizing vertex reuse count, bypassing the need for expensive spatial/cone evaluations at each step.

### Bounding Volumes (`ano_compute_meshlet_bounds`)
The tightness of bounding volumes directly dictates Task Shader culling efficiency. A loose sphere fails to cull invisible meshlets, flooding the GPU with redundant work. Therefore, we prioritize tight bounds over raw CPU baking speed:
- **Bounding Sphere**: We will avoid loose approximations like Ritter's algorithm. Instead, we will generate extremely tight spheres using Principal Component Analysis (PCA) initialization followed by an iterative refinement pass (or a mini-Welzl approximation). 
- **Normal Cone**: We will average all triangle normals in the meshlet, normalize, and find the maximum angle deviation. If the deviation exceeds ~90 degrees, the cone is invalidated (backface culling disabled for this meshlet).

## 4. Low-Level C Optimizations

To ensure our implementation hits peak performance, we will employ the following C techniques:

### Memory & Cache Efficiency
- **Zero Dynamic Allocations**: The core algorithm will require the caller to provide a scratch memory arena. No `malloc` inside the clustering loop.
- **Direct AoS Processing**: Standard engine assets load mesh data as an interleaved Array of Structures (AoS). We will **avoid** the costly memory-copy overhead of transforming this into a Structure of Arrays (SoA).
- **Bitsets**: Use packed bitsets (via `uint64_t` arrays) to track processed geometry, replacing slow boolean arrays and enabling fast `__builtin_ctzll` scanning.

### Vectorization (SIMD)
- Since we process interleaved AoS data directly, we will rely on modern CPU SIMD capabilities to handle interleaving elegantly. We will utilize strided vector loads and hardware-specific deinterleaving operations (like ARM NEON’s `vld3q_f32` or advanced AVX strided gathers) to read positions and normals efficiently.

### Parallelization
- The API will be inherently thread-safe and re-entrant, allowing a task-graph system to partition different meshes (or sub-meshes) concurrently without shared state.

## 5. Execution Roadmap
1. Define the API headers and `ano_meshlet_t` / `ano_meshlet_bounds_gpu_t` structures with perfect `std430` alignment.
2. Implement the global cache/spatial reindexing pass (`ano_optimize_vertex_cache`).
3. Implement the two-pass linear meshlet packer (`ano_build_meshlets`).
4. Implement the tight-fitting Bounds calculation logic using PCA/iterative refinement.
5. Add SIMD fast-paths with hardware-specific strided gather/deinterleave operations.
6. Verify output tightness and performance against `meshopt_buildMeshlets`.
