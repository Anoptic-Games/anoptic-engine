## Mathematical Theory

The core objective of this method is to eliminate "light bleeding" artifacts by using a statistical approach to approximate the depth distribution within a filtered shadow map area.

### 1. The Step Visibility Function

Traditionally, shadow mapping relies on a non-linear step visibility function $v(z_{r})$ to determine if a point is in shadow:


$$v(z_{r}) = \epsilon(z_{o} - z_{r})$$

* This function yields **0** if the occluder distance ($z_{o}$) from the light source is smaller than the receiver distance ($z_{r}$), and **1** otherwise.


* Because this comparison is non-linear, standard linear filtering introduces aliasing.



### 2. Statistical Probability Framework

Instead of evaluating the step function directly, this statistical method models the probability that a shaded point passes the depth test. To eliminate self-occlusion on planar surfaces, the visibility function is biased using the average depth ($\bar{z}_{o}$):


$$V(z_{r}) = \begin{cases} P(z_{o} \ge z_{r}) & \text{if } z_{r} > \bar{z}_{o} \\ 1 & \text{otherwise} \end{cases}$$



By utilizing the Cumulative Distribution Function (CDF) of the random depth variable, defined as $F(z) = P(z_{o} < z)$, the visibility formulation becomes:


$$v(z_{r}) = \frac{1 - F(z_{r})}{1 - F(\bar{z}_{o})}$$

 if $z_{r} > \bar{z}_{o}$, and **1** otherwise.

### 3. Normalized Depth Parameter

To simplify the notation and math, a normalized depth parameter $t$ is introduced:


$$t = \frac{z - z_{min}}{z_{max} - z_{min}} = \frac{z - z_{min}}{\Delta z}$$



* Where $\Delta z = z_{max} - z_{min}$.


* Under this normalization, $F(z) = 0$ if $t < 0$, $F(z) = 1$ if $t > 1$, and $F(z)$ is monotonically non-decreasing in the $[0, 1]$ interval.



### 4. CDF Power Function Approximation

The paper proposes fitting the CDF between the boundaries using a power function curve governed by a data-fitting parameter $\beta$:


$$F(z) = t^{\beta}$$

 where $z(t) = t\Delta z + z_{min}$ 

To satisfy the constraint that the function matches the mean depth ($\bar{z}_{o}$), the integration of the distribution is defined as:


$$\bar{z}_{o} = \int_{0}^{1} (t\Delta z + z_{min})\beta t^{\beta-1} dt = \frac{\beta\Delta z}{\beta + 1} + z_{min}$$



Solving this equation specifically for the fitting parameter $\beta$ yields:


$$\beta = \frac{\bar{z}_{o} - z_{min}}{z_{max} - \bar{z}_{o}}$$



### 5. Final Visibility Formula

By defining the normalized expected depth parameter as $\bar{t} = \frac{\bar{z}_{o} - z_{min}}{\Delta z} = \frac{\beta}{\beta + 1}$, the definitive visibility function evaluated at runtime is:


$$v(t) = \frac{1 - t^{\beta}}{1 - \left(\frac{\beta}{\beta + 1}\right)^{\beta}}$$

 if $t > \bar{t}$, and **1** otherwise.

---

## Implementation Details

### 1. Texture Storing Requirements

* 
**Three-Channel Storage**: The Power CDF filtering method requires storing at least three distinct values in the shadow map texture channels: the minimum depth ($z_{min}$), the maximum depth ($z_{max}$), and the mean depth ($\bar{z}_{o}$).



### 2. Filtering Operation

* 
**Separable Kernel**: The filtering mechanism evaluates an area of interest in the depth buffer using a separable kernel configuration.


* 
**Shadow Softness**: The size of the filtered area directly controls shadow softness; a larger filter area yields more blurred shadows.



### 3. Precision and Hardware Performance

* 
**16-bit Floating-Point Optimization**: While alternatives like Variance Shadow Maps (VSM) and Exponential Shadow Maps (ESM) are highly sensitive to floating-point precision and generally require **32 bits** per channel, the Power CDF technique functions seamlessly with **16 bits** per channel without producing visual artifacts.


* 
**Texture Efficiency**: Because it maintains clarity at lower resolutions and requires fewer texture samples to maintain sharp antialiasing, lower-resolution shadow maps can be used to optimize rendering workloads.



### 4. Scalability and Artifact Mitigation

* 
**Layered Extension**: In exceptionally complex scenes where light bleeding might still manifest, the technique can be mapped into a layered framework (identical to Layered Variance Shadow Maps / LVSM) to fully eradicate artifacts.


* 
**Contact Shadow Stability**: The algorithm is robust against contact shadow flaws typically introduced by ESM and remains unaffected by light-leaking artifacts triggered by multiple distant occluders.
