Notes on the **SCANLINE SWEEPER** glyph renderer: theory, math, and how it actually runs.

### 1. Theoretical Foundation

* 
**Continuous Domain Coverage Estimation**: Usual GPU vector / Bézier rasterizers sample winding numbers at discrete sub-pixel points. SCANLINE SWEEPER stays in the continuous domain. It estimates coverage for one rectangular pixel window by tracking and integrating the signed areas each intersecting curve sweeps.


* 
**Elimination of Singular Events**: Winding-number methods fight floating-point singularities — curve starts exactly on a sample, tangent to a ray, or through a sample point. Area integration only moves a little when roots jitter, so you don't need fancy singular-event handling.


* 
**Jordan Curve Theorem Alignment**: No winding numbers in the shade pass. Via the Jordan Curve Theorem, each curve adds its signed area to the pixel. Upward curves contribute positive area; downward curves negative.


* 
**Dynamically Varying Footprints**: Coverage is over an explicit rectangular em-space window matching each pixel. The footprint is computed per pixel at runtime, so AA holds under arbitrary 3D perspective / orientation without grid snap or invalidating precomputed caches.



---

### 2. Mathematical Formulation

* 
**Curve Monotonicity & Preprocessing**: Offline (or preprocess), cubic Béziers become directed piecewise quadratic Béziers, subdivided at most twice until every segment is strictly $x$-monotonic and $y$-monotonic on $t \in [0, 1]$. Monotonicity ⇒ any scanline or window edge hits the curve at most once ⇒ one root in the domain.


* 
**Quadratic Form Re-expression**: For the shader, quadratic $B(t)$ with controls $p_0, p_1, p_2$ is:



$$B(t) = (p_0 + p_2 - 2p_1)t^2 + 2(p_1 - p_0)t + p_0$$


Axis-aligned line intersections use:


* Second-degree coefficient: $q_a = p_0 + p_2 - 2p_1$
* First-degree coefficient: $q_b = 2(p_1 - p_0)$
* Constant relative to target line: $q_c = p_0 - \text{target}$


* 
**Monotonic Root-Solving**: Solve $q_a t^2 + q_b t + q_c = 0$. Monotonicity means one root; use:



$$t = \frac{-q_b + \text{sign}(c_2 - c_0)\sqrt{d}}{2q_a}$$


with $d = q_b^2 - 4q_a q_c$. If $q_a \approx 0$ (nearly linear), fall back to $t = \frac{\text{target} - c_0}{c_2 - c_0}$.


* 
**Area Quadrature**: Curve sweeps right toward the right window edge; integrate swept area in the window between $t_0$ and $t_1$. Treat the in-window segment as linear → trapezoid:



$$h_B = B_y(t_1) - B_y(t_0)$$


$$\text{Area}_{\text{trapezoid}} = \frac{h_B \cdot (2w - B_x(t_0) - B_x(t_1))}{2}$$


Strictly vertical segments take a fast path as a rectangle: $\text{Area} = \text{sign}(\Delta y) \cdot b \cdot h$, with $b = \min(w, w - p_0.x)$ and $h = y_{\text{max}} - y_{\text{min}}$.



---

### 3. Implementation Details

* 
**Bounding-Box Culling**: Monotonic curves have AABB limits from endpoints $p_0$ and $p_2$ alone. Cull anything wholly above, below, or right of the pixel scanline window.


* 
**Clamping-Based Integration**: Skip branching on every geometric cut configuration. Compute up to four line intersections with the pixel bounds. Clamp $t$ and intersection points to $[0, \text{size}]$ so out-of-window regions collapse to zero — triangles and edge cases without branches.


* 
**Memory Optimization**: Curves live in structured storage buffers, not textures. Control points go up as compact `binary16` IEEE floats and unpack to 32-bit in-shader. Bandwidth is the bottleneck, so share vertices: a curve's first control is the previous curve's last; a sentinel resets the chain.


* **Execution Architecture**:
* 
*Pixel Shader*: One pixel at a time; curve data in non-uniform registers; optional wave loops to scalarize curve access across a wave.


* 
*Compute Shader (Recommended)*: Map thread groups to horizontal scanline partitions. Pair with an accel structure that packs overlapping curves into matching stripes (no indirection) so a wave never straddles a partition. Threads coop-load geometry and evaluate quadratic coefficients via groupshared or wave intrinsics.




* 
**Coverage Assembly & Contours**: Final coverage = sum of valid signed areas / window area. Gamma-correct that ratio for pixel alpha. Overlapping glyph contours break the formula (accumulated ratio can exceed 1); clean outlines ahead of time, or evaluate each contour separately and average at the end.
