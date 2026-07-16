## Mathematical Theory

Kill "light bleeding" by approximating the depth distribution inside a filtered shadow-map area with statistics, not a hard depth compare.

### 1. The Step Visibility Function

Classic shadow mapping uses a non-linear step visibility $v(z_{r})$:


$$v(z_{r}) = \epsilon(z_{o} - z_{r})$$

* **0** when the occluder is closer than the receiver ($z_{o} < z_{r}$), **1** otherwise.


* The compare is non-linear, so linear filtering aliases.



### 2. Statistical Probability Framework

Don't evaluate the step directly. Model the probability that a shaded point passes the depth test. Bias with mean depth ($\bar{z}_{o}$) so planar surfaces don't self-occlude:


$$V(z_{r}) = \begin{cases} P(z_{o} \ge z_{r}) & \text{if } z_{r} > \bar{z}_{o} \\ 1 & \text{otherwise} \end{cases}$$



With the CDF $F(z) = P(z_{o} < z)$ that becomes:


$$v(z_{r}) = \frac{1 - F(z_{r})}{1 - F(\bar{z}_{o})}$$

 if $z_{r} > \bar{z}_{o}$, and **1** otherwise.

### 3. Normalized Depth Parameter

Normalize depth to $t$:


$$t = \frac{z - z_{min}}{z_{max} - z_{min}} = \frac{z - z_{min}}{\Delta z}$$



* $\Delta z = z_{max} - z_{min}$.


* Then $F(z) = 0$ if $t < 0$, $F(z) = 1$ if $t > 1$, and $F(z)$ is monotonically non-decreasing on $[0, 1]$.



### 4. CDF Power Function Approximation

Fit the CDF between the bounds with a power curve and a free parameter $\beta$:


$$F(z) = t^{\beta}$$

 where $z(t) = t\Delta z + z_{min}$ 

Match the mean depth ($\bar{z}_{o}$) by integrating the distribution:


$$\bar{z}_{o} = \int_{0}^{1} (t\Delta z + z_{min})\beta t^{\beta-1} dt = \frac{\beta\Delta z}{\beta + 1} + z_{min}$$



Solve for $\beta$:


$$\beta = \frac{\bar{z}_{o} - z_{min}}{z_{max} - \bar{z}_{o}}$$



### 5. Final Visibility Formula

With normalized mean depth $\bar{t} = \frac{\bar{z}_{o} - z_{min}}{\Delta z} = \frac{\beta}{\beta + 1}$, the runtime visibility is:


$$v(t) = \frac{1 - t^{\beta}}{1 - \left(\frac{\beta}{\beta + 1}\right)^{\beta}}$$

 if $t > \bar{t}$, and **1** otherwise.

---

## Implementation Details

### 1. Texture Storing Requirements

* 
**Three-Channel Storage**: Store at least three values in the shadow map: min depth ($z_{min}$), max depth ($z_{max}$), and mean depth ($\bar{z}_{o}$).



### 2. Filtering Operation

* 
**Separable Kernel**: Filter the depth buffer over an area of interest with a separable kernel.


* 
**Shadow Softness**: Filter size is shadow softness; bigger area → blurrier shadows.



### 3. Precision and Hardware Performance

* 
**16-bit Floating-Point Optimization**: VSM and ESM are precision-hungry and usually want **32 bits** per channel. Power CDF runs clean on **16 bits** per channel.


* 
**Texture Efficiency**: Stays sharp at lower resolutions and needs fewer samples for clean AA, so you can run lower-res shadow maps.



### 4. Scalability and Artifact Mitigation

* 
**Layered Extension**: In heavy scenes where bleeding still shows, layer it the same way as Layered Variance Shadow Maps (LVSM) until the artifacts are gone.


* 
**Contact Shadow Stability**: Holds up on contact shadows that ESM usually breaks, and doesn't light-leak from multiple distant occluders.
