# Radiance Cascades for Non-LTE Radiative Transfer

Math, scaling laws, and algorithms for **Radiance Cascades** on multi-dimensional non-Local Thermodynamic Equilibrium (non-LTE) radiative transfer, from the reference paper.

---

## 1. The Multi-Dimensional Non-LTE Problem

The fundamental numerical problem in multi-dimensional non-LTE radiative transfer is computing the radiation field throughout a domain to determine atomic transition rates. The quantity you need is the angle-averaged monochromatic intensity (zeroth moment of the radiation field) at frequency $\nu$ and position $\vec{p}$:

$$J_{\nu}(\vec{p})=\frac{1}{4\pi}\oint_{\Sigma^{2}}I_{\nu}(\vec{p},\hat{\omega})d\hat{\omega}$$

Where:

* $I_{\nu}(\vec{p},\hat{\omega})$ is the monochromatic specific intensity at position $\vec{p}$ propagating along the directional unit vector $\hat{\omega}$.
* $\Sigma^2$ denotes the unit sphere.

### Physical Nature of the Problem

The specific intensity distribution $I_{\nu}(\vec{p},\hat{\omega})$ is a five-dimensional function at each frequency $\nu$ (3D space + 2D angle). In the non-LTE regime, the plasma emission and absorption properties depend non-locally and non-linearly on this self-same radiation field via radiative transitions. Because light travels along straight paths in the non-relativistic limit, internal structural dependencies exist within $I_{\nu}(\vec{p},\hat{\omega})$ that can be exploited.

---

## 2. The Penumbra Criterion & Spatial-Angular Coupling

Traditional discrete ordinates ($S_N$) methods hit **ray effects** because they use a fixed, purely discrete angular sampling grid across the whole domain. Radiance Cascades kills those with an adaptive spatial-angular resolution trade-off under the **Penumbra Criterion**.

### Mathematical Derivation via Flatland Geometry

Consider a flat two-dimensional geometry where a linear light source of length $w$ is positioned at a perpendicular distance $d$ from an opaque blocker, illuminating a region of free space.

1. The blocker casts a partial shadow (penumbra). The angular size $\gamma$ of the penumbra cast at a perpendicular distance $h$ from the blocker is given by:

$$\gamma = 2 \arctan\left(\frac{w}{2d}\right)$$


2. The linear physical size $H(h)$ of the penumbra at a perpendicular distance $h$ below the blocker is:

$$H(h) = 2 \arctan\left(\frac{w}{2d}\right)h = \gamma h$$


3. To resolve the spatial variation inside this penumbra, the spatial sampling interval $\Delta_s$ must satisfy the Nyquist-adjacent condition:

$$\Delta_{s} < H(h)$$


4. Concurrently, to angularly resolve the light source from a total distance $D = d + h$, where the source subtends an angular size $\epsilon(D)$, the required angular sampling interval $\Delta_{\omega}$ must satisfy:

$$\Delta_{\omega} < \epsilon(D) = 2 \arctan\left(\frac{w}{2D}\right)$$



### Asymptotic Scaling Laws

In the far-field regime where $d \ll D$ and the angle is small, the relations simplify to $h \approx D$, yielding:

* $H(h) \propto D \implies$ Spatial resolution requirements relax linearly with distance.
* $\epsilon(D) \propto \frac{1}{D} \implies$ Angular resolution requirements tighten inversely with distance.

This yields the generalized **Penumbra Criterion** for spatial sampling ($\Delta_s$) and angular sampling ($\Delta_{\omega}$) as functions of distance $D$:

$$\begin{cases}\Delta_{S} < F(D) \propto D \\ \Delta_{\omega} < G\left(\frac{1}{D}\right) \propto \frac{1}{D}\end{cases}$$

Where $F$ and $G$ are linear functions. (Note: If $d \approx D$ and the angle is large, $\Delta_{\omega}$ scales superlinearly with $1/D$).

**Core Insight:** * *Near-field* radiance contributions exhibit high spatial frequency but low angular frequency.

* *Far-field* radiance contributions exhibit low spatial frequency but high angular frequency.

---

## 3. Radiance Intervals & Merging Algebra

The basic unit of radiance cascades is the **Radiance Interval**.

### Definition

For an arbitrary volume of emitting and absorbing media, the radiance interval $\mathcal{R}_{a,b}(\vec{p},\hat{\omega})$ is a two-element vector containing the monochromatic specific intensity $I$ and the monochromatic optical depth $\tau$ accumulated along a linear path segment $\vec{P}(t) = \vec{p} + t\hat{\omega}$ for $t \in [a,b]$:

$$\mathcal{R}_{a,b}(\vec{p},\hat{\omega}) = \left[I_{b\rightarrow a}(\vec{p},\hat{\omega}),\, \tau_{b\rightarrow a}(\vec{p},\hat{\omega})\right]$$

Where the subscript $b\rightarrow a$ indicates integration from the upwind boundary $b$ downwind to $a$. The interval encodes both the target illumination received by $\vec{p}$ from that specific shell segment and the physical capacity of that path segment to occlude further upwind radiation.

### Spatial Shift Invariance

Due to linear propagation, radiance intervals are shift-invariant along their direction of travel:

$$\mathcal{R}_{a+x,\, b+x}(\vec{p},\hat{\omega}) = \mathcal{R}_{a,b}(\vec{p} + x\hat{\omega},\, \hat{\omega})$$

### Merging Algebra

Contiguous radiance intervals along a ray path can be joined analytically using the non-commutative merging operator $\mathcal{M}$. For two adjacent intervals covering $[a,b]$ and $[b,c]$ along direction $\hat{\omega}$ relative to point $\vec{p}$:

$$\mathcal{R}_{a,c}(\vec{p},\hat{\omega}) = \mathcal{M}\left(\mathcal{R}_{a,b}(\vec{p},\hat{\omega}),\, \mathcal{R}_{b,c}(\vec{p},\hat{\omega})\right)$$

$$\mathcal{M}\left(\mathcal{R}_{a,b},\, \mathcal{R}_{b,c}\right) = \left[I_{c\rightarrow b}(\vec{p},\hat{\omega}) + \exp\left(-\tau_{c\rightarrow b}(\vec{p},\hat{\omega})\right)I_{b\rightarrow a}(\vec{p},\hat{\omega}),\,\, \tau_{c\rightarrow b}(\vec{p},\hat{\omega}) + \tau_{b\rightarrow a}(\vec{p},\hat{\omega})\right]$$

This composition rule is derived directly from the exact formal solution to the unpolarized radiative transfer equation.

---

## 4. Radiance Cascades Hierarchy and Discretization

A *Radiance Cascade* $i$ is defined as the complete set of radiance intervals representing a specific distance shell $[t_i, t_{i+1}]$ mapped over all spatial sample sites $\vec{p}$ and angular samples $\hat{\omega} \in \Sigma^2$.

### Exponential Scaling Laws

To hit the Penumbra Criterion cheaply, successive cascades use an exponential branching factor $\alpha \ge 1$:

$$\begin{cases}\Delta_{s} \propto 2^i \\ \Delta_{\omega} \propto \frac{1}{2^{\alpha i}}\end{cases}$$

Where:

* $\Delta_s$ is the spatial spacing between probe centers on a uniform grid at cascade level $i$.
* $\Delta_{\omega}$ is the angular sample spacing (size of the directional control cones) at cascade level $i$.

The boundaries of the shell for cascade $i$ are bounded by the boundary conditions:


$$\begin{cases}t_0 = 0 \\ \lim_{i\rightarrow\infty} t_i = \infty\end{cases}$$

In a rectilinear grid implementation with branching factor $\alpha = 1$:

* **Cascade 0 (Shortest range):** Probes are placed at maximum spatial density ($\Delta_s = 1\text{ cell}$). It tracks rays starting at $t_0 = 0$ out to a short distance $t_1$, using a low number of base angular samples (e.g., 4 rays in 2D Flatland).
* **Cascade $i$:** The probe spatial density is halved along each axis ($\Delta_s = 2^i$), while the angular resolution is doubled ($\Delta_{\omega}$ decreases by $2^{\alpha i}$). The ray segments span from $t_i$ to $t_{i+1}$.

---

## 5. Interpolated Ray Analysis & Asymptotic Complexity

The total angle-averaged radiation field $J(\vec{p})$ at any arbitrary high-resolution grid cell is reconstructed by down-sampling and combining information from the higher cascade levels onto Cascade 0 via $n$-linear spatial and angular interpolation.

### Ray Count Scaling Analysis (2D Flatland with $\alpha = 1$)

Let $P_0$ be the number of spatial probes and $W_0$ be the number of angular samples at Cascade 0. The number of raw rays computed at level 0 is:


$$N_{C_0} = P_0 W_0$$

For Cascade 1, the spatial grid is down-sampled by 2 on each axis (halving resolution, so $P_1 = P_0 / 4$ in 2D), and the angular samples are doubled ($W_1 = 2W_0$). The number of computed rays at level 1 is:


$$N_{C_1} = P_1 W_1 = \left(\frac{P_0}{4}\right)(2W_0) = \frac{P_0 W_0}{2} = \frac{N_{C_0}}{2}$$

By induction, the number of computed rays at any cascade level $i$ scales as:


$$N_{C_i} = \frac{N_{C_0}}{2^i}$$

For a hierarchy containing $I$ total cascades, the total number of rays mathematically evaluated is bounded by a geometric series:

$$\text{Rays Computed} = \sum_{i=0}^{I-1} N_{C_i} = N_{C_0} \sum_{i=0}^{I-1} \left(\frac{1}{2}\right)^i < 2N_{C_0}$$

With spatial bilinear interpolation and angular splitting, the number of high-resolution rays *constructed* at the finest scale on the outermost boundary shell still scales exponentially:

$$\text{Rays Constructed} = N_I' = 2^I N_{C_0}$$

### General Branching Factor $\alpha = 2$

With branching factor $\alpha = 2$ on a 2D spatial grid:

* Spatial probes down-sample by 4 ($P_i = P_{i-1}/4$).
* Angular samples up-sample by $2^\alpha = 4$ ($W_i = 4W_{i-1}$).
* So rays computed at *every* cascade layer stay constant: $N_{C_i} = N_{C_0}$.
* Total cost scales linearly with cascade count ($I \cdot N_{C_0}$); constructed angular resolution at the domain edge scales exponentially as $2^{2I} N_{C_0}$.

---

## 6. Parallax Artefacts & The Bilinear Fix

Higher cascades down-sample probe locations, so a localized high-opacity emitter can be visible to a low-level probe and to the higher-level probes used for bilinear interpolation at once. That parallax breaks energy conservation and shows up as ringing around sharp interfaces.

### Algorithmic Mechanics of the Bilinear Fix

Replace plain spatial interpolation with an analytic ray reprojection 〜 the **Bilinear Fix**:

1. Let $\vec{p}$ be a probe center at cascade level $i$. Under standard execution, its radiance interval merges directly with an angularly pooled value bilinearly interpolated from four neighboring "parent" probes $\{A, B, C, D\}$ at cascade level $i+1$.
2. Under the Bilinear Fix, rather than tracing a single interval from $\vec{p}$'s standard origin, **four distinct radiance intervals** are spawned from $\vec{p}$.
3. Each of these four intervals is explicitly traced along modified vectors leading directly to the specific spatial starting positions of the child cones belonging to probes $A, B, C,$ and $D$ respectively.
4. Each individual reprojected interval is merged independently with its exact corresponding sample from cascade $i+1$ using the standard merging operator $\mathcal{M}$:

$$\mathcal{R}^k = \mathcal{M}\left(\mathcal{R}_{\text{reprojected}, k},\, \mathcal{R}_{i+1, k}\right) \quad \text{for } k \in \{A,B,C,D\}$$


5. The final un-occluded, parallax-corrected radiance field state is computed as the weighted linear combination using the standard geometric bilinear weights $w_k$:

$$\mathcal{R}_{\text{final}} = \sum_{k \in \{A,B,C,D\}} w_k \mathcal{R}^k$$



*Cost Penalty:* The Bilinear Fix increases the total number of rays requiring explicit path-tracing by a factor of $4\times$ on all cascade layers $i > 0$.

---

## 7. Numerical Formal Solution of the RTE

### The Monochromatic Radiative Transfer Equation

The propagation of specific intensity along a directional vector $\hat{\omega}$ is governed by:

$$\hat{\omega}\cdot\nabla I_{\nu,\hat{\omega}}=\eta_{\nu,\hat{\omega}}-\chi_{\nu,\hat{\omega}}I_{\nu,\hat{\omega}}$$

Where:

* $\eta_{\nu,\hat{\omega}}$ is the monochromatic macroscopic emissivity coefficient.
* $\chi_{\nu,\hat{\omega}}$ is the monochromatic macroscopic total opacity coefficient.

### Piecewise Constant Direct Integration

Rays are tracked through a uniform rectilinear grid using a Digital Differential Analyzer (DDA) algorithm. Within any singular grid cell $k$ along a path length segment $\Delta s$, the thermodynamic state, material properties, and atomic populations are assumed constant. The localized formal solution integrating from path position $s$ to $s + \Delta s$ is given by:

$$\tau_{\nu}(s + \Delta s) = \tau_{\nu}(s) + \chi_{\nu,k,\hat{\omega}} \Delta s$$

$$I_{\nu}(s + \Delta s) = I_{\nu}(s) \exp\left(-\chi_{\nu,k,\hat{\omega}} \Delta s\right) + \frac{\eta_{\nu,k,\hat{\omega}}}{\chi_{\nu,k,\hat{\omega}}} \left(1 - \exp\left(-\chi_{\nu,k,\hat{\omega}} \Delta s\right)\right)$$

Where $\frac{\eta_{\nu,k,\hat{\omega}}}{\chi_{\nu,k,\hat{\omega}}} = S_{\nu,k}$ defines the localized cell source function (neglecting background isotropic scattering).

### Velocity Frame Transformation

For moving media, velocity-independent terms (continua) are computed once per wavelength. Line profiles are velocity-dependent due to Doppler shifts. The localized frame transformation evaluates the profile in the observer's frame on-the-fly using the inner product of the bulk macroscopic fluid velocity vector $\vec{v}_k$ and the ray direction vector $\hat{\omega}$:

$$v_{\text{projected}} = \vec{v}_k \cdot \hat{\omega}$$

---

## 8. Multi-Level Accelerated Lambda Iteration (MALI)

For self-consistent atomic level populations, DexRT couples the Radiance Cascades formal solver to a preconditioned Multi-level Accelerated Lambda Iteration scheme using Rybicki & Hummer (1992) "same-preconditioning."

### Diagonal Approximate Lambda Operator (ALO)

Operating under the piecewise constant spatial assumption, a purely local, diagonal operator $\Lambda^*$ is constructed exclusively on Cascade 0 grid cells. The directional monochromatic ALO contribution for a grid cell $k$ along direction $\hat{\omega}$ is defined as the cell's internal core-to-edge optical depth transmittance:

$$\Lambda_{\nu,\hat{\omega}}^*(k) = \exp\left(-\tau_{\nu,k,\hat{\omega}}\right)$$

Where $\tau_{\nu,k,\hat{\omega}} = \chi_{\nu,k,\hat{\omega}} \cdot ds_{\text{mid}}$, representing the optical depth from the geometric center of cell $k$ to its exit boundary along direction $\hat{\omega}$.

The associated preconditioning operator $\Psi^*$, which normalizes the ALO against total opacity, is defined as:

$$\Psi_{\nu,\hat{\omega}}^*(k) = \frac{\Lambda_{\nu,\hat{\omega}}^*(k)}{\chi_{\nu,k,\hat{\omega}}}$$

### Effective Intensity Formulation

The coupled line radiation field is preconditioned on a per-transition basis. For an atomic transition from upper energy level $j$ to lower energy level $i$, the preconditioned effective intensity $I^{\text{eff}}$ at cell $k$ is calculated as:

$$I_{\nu,\hat{\omega};ji}^{\text{eff}}(k) = I_{\nu,\hat{\omega}}^{\dagger}(k) - \Psi_{\nu,\hat{\omega}}^*(k) \cdot U_{ji}(k)$$

Where:

* $\dagger$ denotes the state evaluated at the previous non-LTE iteration step.
* $I_{\nu,\hat{\omega}}^{\dagger}(k)$ is the current specific intensity field provided directly by the formal solver.
* $U_{ji}(k)$ is the preconditioned population transition rate term.

The $I^{\text{eff}}$ values are integrated over frequency and angle to update the off-diagonal terms of the global rate matrix $\Gamma_s$. The populations are then advanced toward statistical equilibrium by solving the linear system:

$$\Gamma_{s}\vec{n}_{s}=\vec{0}$$

Where $\vec{n}_s$ is the vector of atomic level populations for atomic species $s$. The diagonal terms of $\Gamma_s$ are closed via the particle conservation equation.

---

## 9. Thermodynamic Conservation Laws

MHD input models often fix pressure or charge conservation. Two auxiliary iterations hang off the population solver to keep those constraints.

### 1. Post-MALI Charge Conservation

Electron density $n_e$ is adjusted self-consistently after each statistical equilibrium update via a localized single-variable Newton-Raphson iteration:

$$n_e^{m+1} = n_e^m - \left[\left(\frac{\partial F_{\text{charge}}}{\partial n_e}\right)^{-1} F_{\text{charge}}\right]^m$$

Where $F_{\text{charge}} = \sum_{\text{ions}} Z \cdot n_{\text{ion}} - n_e = 0$, with hydrogen ionization states in the active Jacobian.

### 2. Pressure Conservation Equation

When modeling an atmosphere under local gas pressure equilibrium, the total macroscopic hydrogen number density $n_{\text{Htot}}$ must be scaled dynamically to counteract modifications in total particle count caused by changes in electron density $n_e$. The total ideal gas thermal pressure is defined as:

$$P = N_{\text{tot}} k_{B} T = \left(A_{\text{tot}} n_{\text{Htot}} + n_e\right) k_{B} T$$

Where:

* $N_{\text{tot}}$ is the sum total number density of all atomic nuclei and free electrons.
* $A_{\text{tot}} = \frac{N_{\text{nuclei}}}{n_{\text{Htot}}}$ is the fixed elemental chemical abundance fraction of all atomic species relative to hydrogen.
* $k_B$ is the Boltzmann constant and $T$ is the local plasma electron temperature.

The updated total hydrogen number density $n_{\text{Htot}}^{\text{new}}$ required to preserve the invariant input pressure field $P$ is computed linearly from the updated electron density $n_e^{\text{new}}$:

$$n_{\text{Htot}}^{\text{new}} = \frac{\frac{P}{k_B T} - n_e^{\text{new}}}{A_{\text{tot}}}$$

All individual atomic level populations $\vec{n}_s$ are subsequently rescaled proportionally to match this adjusted density baseline before entering the next formal solution pass.
