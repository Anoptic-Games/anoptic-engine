An analysis of the **SCANLINE SWEEPER** glyph rendering algorithm reveals its theoretical foundations, mathematical framework, and practical implementation details:

### 1. Theoretical Foundation

* 
**Continuous Domain Coverage Estimation**: Traditional GPU-based vector or Bézier curve rasterizers typically evaluate winding numbers at discrete sub-pixel sample points. In contrast, the SCANLINE SWEEPER operates entirely within the continuous domain. It analytically estimates coverage for a single rectangular pixel window by tracking and integrating the signed areas swept by each intersecting curve.


* 
**Elimination of Singular Events**: Traditional winding-number approaches struggle with numerical floating-point precision when handling singular events—such as curves starting exactly at a sample point, lying perfectly tangent to a ray, or passing directly through a sample point. Because the sweeper integrates continuous area, small perturbations in root-finding yield only marginal changes to the final coverage estimate, side-stepping the need for complex singular-event handling.


* 
**Jordan Curve Theorem Alignment**: Winding numbers are completely omitted from the shading pass. Utilizing the Jordan Curve Theorem, each curve adds its signed area contribution additively to the pixel. Upwards-moving curves produce positive area contributions, whereas downwards-moving curves produce negative contributions.


* 
**Dynamically Varying Footprints**: The algorithm evaluates coverage over an explicit rectangular bounding window in em-space matching each pixel. Because this footprint is dynamically calculated per-pixel at runtime, the algorithm yields robust anti-aliasing under arbitrary 3D perspective projections or orientations without snapping to a grid or invalidating pre-computed caches.



---

### 2. Mathematical Formulation

* 
**Curve Monotonicity & Preprocessing**: During an offline or preprocessing phase, cubic Bézier curves are decomposed into directed piecewise quadratic Béziers. These are subdivided (up to two times) so that every curve segment is strictly both $x$-monotonic and $y$-monotonic over the unit interval $t \in [0, 1]$. Monotonicity ensures that any given scanline or window edge intersects the curve at most once, which guarantees a single root within the curve's domain.


* 
**Quadratic Form Re-expression**: For optimal execution in the shader, a quadratic Bézier curve $B(t)$ defined by control points $p_0, p_1, p_2$ is expressed as:



$$B(t) = (p_0 + p_2 - 2p_1)t^2 + 2(p_1 - p_0)t + p_0$$


When solving for intersections along an axis-aligned target line, the polynomial coefficients are computed as:


* Second-degree coefficient: $q_a = p_0 + p_2 - 2p_1$
* First-degree coefficient: $q_b = 2(p_1 - p_0)$
* Constant coefficient relative to target line: $q_c = p_0 - \text{target}$


* 
**Monotonic Root-Solving**: Intersections are evaluated by solving $q_a t^2 + q_b t + q_c = 0$. Due to monotonicity, only one root is checked using the quadratic formula:



$$t = \frac{-q_b + \text{sign}(c_2 - c_0)\sqrt{d}}{2q_a}$$


where $d = q_b^2 - 4q_a q_c$. If $q_a \approx 0$ (the curve segment is approximately linear), the solver falls back to linear interpolation: $t = \frac{\text{target} - c_0}{c_2 - c_0}$.


* 
**Area Quadrature**: Assuming a curve sweeps to the right toward the right window edge, the swept area inside the window bounded by parameters $t_0$ and $t_1$ is integrated. Treating the segment within the window as linear yields a trapezoidal area estimate:



$$h_B = B_y(t_1) - B_y(t_0)$$


$$\text{Area}_{\text{trapezoid}} = \frac{h_B \cdot (2w - B_x(t_0) - B_x(t_1))}{2}$$


Strictly vertical segments are caught via a fast path and computed as a pure rectangle : $\text{Area} = \text{sign}(\Delta y) \cdot b \cdot h$, where $b = \min(w, w - p_0.x)$ and $h = y_{\text{max}} - y_{\text{min}}$.



---

### 3. Implementation Details

* 
**Bounding-Box Culling**: Because all curves are preprocessed to be monotonic, their spatial limits match the axis-aligned bounding box defined entirely by their endpoints $p_0$ and $p_2$. Shaders use this property to trivially cull irrelevant geometry; curves completely above, below, or to the right of the pixel's scanline window are skipped immediately.


* 
**Clamping-Based Integration**: To avoid branching on the numerous geometric configurations of how a curve cuts through a pixel, the shader computes up to four line intersections for the enclosing pixel boundaries. By clamping the parameter inputs $t$ and evaluating intersection points to the pixel boundaries ($[0, \text{size}]$), regions outside the window naturally collapse or absorb zeros, gracefully handling triangular or rectangular edge cases without branching.


* 
**Memory Optimization**: Curve data is loaded from structured storage buffers rather than textures. Control points are uploaded as compact `binary16` IEEE floats and unpacked to 32-bit precision inside the shader. To reduce memory bandwidth—the primary bottleneck—curves exploit shared vertices: the first control point of a curve is implicitly treated as the last control point of the preceding curve, with a sentinel value utilized to trigger resets.


* **Execution Architecture**:
* 
*Pixel Shader*: Processes pixels individually, loading curve data into non-uniform registers; optional wave loops can be used to scalarize and unify curve access across a wave.


* 
*Compute Shader (Recommended)*: Maps thread groups to discrete horizontal scanline partitions. Combined with an acceleration structure that groups overlapping curves into matching stripes without indirections , this prevents threads within a wave from straddling partition boundaries. Threads can cooperatively load geometry and evaluate quadratic coefficients in parallel using groupshared memory or wave intrinsics.




* 
**Coverage Assembly & Contours**: The final coverage ratio is the summation of all valid signed area contributions divided by the total area of the window. This ratio undergoes gamma correction to dictate the final pixel alpha value. Overlapping glyph contours will break the formula (as the accumulated area ratio denominator will exceed 1) ; they must either be resolved ahead of time via outline clean-up tools or processed independently by evaluating coverage for each contour separately and averaging the results at the end.
