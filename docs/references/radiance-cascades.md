# Radiance Cascades for Non-LTE Radiative Transfer

Math, scaling laws, and algorithms for **Radiance Cascades** on multi-dimensional non-Local Thermodynamic Equilibrium (non-LTE) radiative transfer, from the reference paper.

---

## 1. The Multi-Dimensional Non-LTE Problem

Multi-dimensional non-LTE radiative transfer needs the radiation field over the domain for atomic transition rates. Quantity: angle-averaged monochromatic intensity (zeroth moment) at frequency $\nu$ and position $\vec{p}$:

$$J_{\nu}(\vec{p})=\frac{1}{4\pi}\oint_{\Sigma^{2}}I_{\nu}(\vec{p},\hat{\omega})d\hat{\omega}$$

Where:

* $I_{\nu}(\vec{p},\hat{\omega})$ is the monochromatic specific intensity at position $\vec{p}$ propagating along the directional unit vector $\hat{\omega}$.
* $\Sigma^2$ denotes the unit sphere.

### Physical Nature of the Problem

$I_{\nu}(\vec{p},\hat{\omega})$ is five-dimensional at each $\nu$ (3D space + 2D angle). In non-LTE, plasma emission and absorption depend non-locally and non-linearly on that same field via radiative transitions. Non-relativistic light follows straight paths, so $I_{\nu}(\vec{p},\hat{\omega})$ has exploitable internal structure.

---

## 2. The Penumbra Criterion & Spatial-Angular Coupling

Traditional discrete ordinates ($S_N$) hit **ray effects** from a fixed discrete angular sampling grid across the domain. Radiance Cascades kills those with an adaptive spatial-angular resolution trade-off under the **Penumbra Criterion**.

### Mathematical Derivation via Flatland Geometry

Flatland: linear light source of length $w$ at perpendicular distance $d$ from an opaque blocker, illuminating free space.

1. The blocker casts a partial shadow (penumbra). Angular size $\gamma$ of the penumbra at perpendicular distance $h$ from the blocker:

$$\gamma = 2 \arctan\left(\frac{w}{2d}\right)$$


2. Linear physical size $H(h)$ of the penumbra at perpendicular distance $h$ below the blocker:

$$H(h) = 2 \arctan\left(\frac{w}{2d}\right)h = \gamma h$$


3. To resolve spatial variation inside this penumbra, spatial sampling interval $\Delta_s$ must satisfy the Nyquist-adjacent condition:

$$\Delta_{s} < H(h)$$


4. Concurrently, to angularly resolve the light source from total distance $D = d + h$, where the source subtends angular size $\epsilon(D)$, required angular sampling interval $\Delta_{\omega}$:

$$\Delta_{\omega} < \epsilon(D) = 2 \arctan\left(\frac{w}{2D}\right)$$



### Asymptotic Scaling Laws

Far-field $d \ll D$ with small angle: $h \approx D$, so:

* $H(h) \propto D \implies$ Spatial resolution requirements relax linearly with distance.
* $\epsilon(D) \propto \frac{1}{D} \implies$ Angular resolution requirements tighten inversely with distance.

Generalized **Penumbra Criterion** for spatial sampling ($\Delta_s$) and angular sampling ($\Delta_{\omega}$) vs distance $D$:

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

Subscript $b\rightarrow a$: integrate from upwind boundary $b$ downwind to $a$. Interval holds illumination at $\vec{p}$ from that shell segment plus that segment's capacity to occlude further upwind radiation.

### Spatial Shift Invariance

Radiance intervals are shift-invariant along their travel direction:

$$\mathcal{R}_{a+x,\, b+x}(\vec{p},\hat{\omega}) = \mathcal{R}_{a,b}(\vec{p} + x\hat{\omega},\, \hat{\omega})$$

### Merging Algebra

Contiguous radiance intervals along a ray path join analytically via the non-commutative merging operator $\mathcal{M}$. Adjacent intervals covering $[a,b]$ and $[b,c]$ along $\hat{\omega}$ relative to $\vec{p}$:

$$\mathcal{R}_{a,c}(\vec{p},\hat{\omega}) = \mathcal{M}\left(\mathcal{R}_{a,b}(\vec{p},\hat{\omega}),\, \mathcal{R}_{b,c}(\vec{p},\hat{\omega})\right)$$

$$\mathcal{M}\left(\mathcal{R}_{a,b},\, \mathcal{R}_{b,c}\right) = \left[I_{c\rightarrow b}(\vec{p},\hat{\omega}) + \exp\left(-\tau_{c\rightarrow b}(\vec{p},\hat{\omega})\right)I_{b\rightarrow a}(\vec{p},\hat{\omega}),\,\, \tau_{c\rightarrow b}(\vec{p},\hat{\omega}) + \tau_{b\rightarrow a}(\vec{p},\hat{\omega})\right]$$

Composition = exact formal solution of the unpolarized RTE.

---

## 4. Radiance Cascades Hierarchy and Discretization

A *Radiance Cascade* $i$ is the complete set of radiance intervals for distance shell $[t_i, t_{i+1}]$ over all spatial sample sites $\vec{p}$ and angular samples $\hat{\omega} \in \Sigma^2$.

### Exponential Scaling Laws

Successive cascades use exponential branching factor $\alpha \ge 1$:

$$\begin{cases}\Delta_{s} \propto 2^i \\ \Delta_{\omega} \propto \frac{1}{2^{\alpha i}}\end{cases}$$

Where:

* $\Delta_s$ is the spatial spacing between probe centers on a uniform grid at cascade level $i$.
* $\Delta_{\omega}$ is the angular sample spacing (size of the directional control cones) at cascade level $i$.

Shell boundaries for cascade $i$:


$$\begin{cases}t_0 = 0 \\ \lim_{i\rightarrow\infty} t_i = \infty\end{cases}$$

Rectilinear grid with branching factor $\alpha = 1$:

* **Cascade 0 (Shortest range):** Probes at max spatial density ($\Delta_s = 1\text{ cell}$). Rays from $t_0 = 0$ out to short $t_1$, low base angular samples (e.g., 4 rays in 2D Flatland).
* **Cascade $i$:** Probe spatial density halved per axis ($\Delta_s = 2^i$); angular resolution doubled ($\Delta_{\omega}$ decreases by $2^{\alpha i}$). Ray segments span $t_i$ to $t_{i+1}$.

---

## 5. Interpolated Ray Analysis & Asymptotic Complexity

Angle-averaged field $J(\vec{p})$ at any high-resolution grid cell is reconstructed by down-sampling and combining higher cascades onto Cascade 0 via $n$-linear spatial and angular interpolation.

### Ray Count Scaling Analysis (2D Flatland with $\alpha = 1$)

Let $P_0$ be the number of spatial probes and $W_0$ be the number of angular samples at Cascade 0. Raw rays at level 0:


$$N_{C_0} = P_0 W_0$$

Cascade 1: spatial grid down-sampled by 2 per axis ($P_1 = P_0 / 4$ in 2D); angular samples doubled ($W_1 = 2W_0$). Rays at level 1:


$$N_{C_1} = P_1 W_1 = \left(\frac{P_0}{4}\right)(2W_0) = \frac{P_0 W_0}{2} = \frac{N_{C_0}}{2}$$

By induction, rays at cascade level $i$:


$$N_{C_i} = \frac{N_{C_0}}{2^i}$$

For $I$ cascades, total rays evaluated is bounded by a geometric series:

$$\text{Rays Computed} = \sum_{i=0}^{I-1} N_{C_i} = N_{C_0} \sum_{i=0}^{I-1} \left(\frac{1}{2}\right)^i < 2N_{C_0}$$

With spatial bilinear interpolation and angular splitting, high-resolution rays *constructed* at the finest scale on the outermost boundary shell still scale exponentially:

$$\text{Rays Constructed} = N_I' = 2^I N_{C_0}$$

### General Branching Factor $\alpha = 2$

With branching factor $\alpha = 2$ on a 2D spatial grid:

* Spatial probes down-sample by 4 ($P_i = P_{i-1}/4$).
* Angular samples up-sample by $2^\alpha = 4$ ($W_i = 4W_{i-1}$).
* So rays computed at *every* cascade layer stay constant: $N_{C_i} = N_{C_0}$.
* Total cost scales linearly with cascade count ($I \cdot N_{C_0}$); constructed angular resolution at the domain edge scales exponentially as $2^{2I} N_{C_0}$.

---

## 6. Parallax Artefacts & The Bilinear Fix

Higher cascades down-sample probe locations. A localized high-opacity emitter can then be visible to a low-level probe and to the higher-level bilinear parents at once. That parallax breaks energy conservation and rings around sharp interfaces.

### Algorithmic Mechanics of the Bilinear Fix

**Bilinear Fix:** replace plain spatial interpolation with analytic ray reprojection.

1. Let $\vec{p}$ be a probe center at cascade level $i$. Standard: merge its radiance interval with an angularly pooled bilinear interpolate from four parent probes $\{A, B, C, D\}$ at level $i+1$.
2. Bilinear Fix: spawn **four distinct radiance intervals** from $\vec{p}$ instead of one from the standard origin.
3. Each traces along a modified vector to the child-cone start of $A$, $B$, $C$, or $D$.
4. Each reprojected interval merges independently with its matching cascade $i+1$ sample via $\mathcal{M}$:

$$\mathcal{R}^k = \mathcal{M}\left(\mathcal{R}_{\text{reprojected}, k},\, \mathcal{R}_{i+1, k}\right) \quad \text{for } k \in \{A,B,C,D\}$$


5. Final parallax-corrected state: weighted linear combination with bilinear weights $w_k$:

$$\mathcal{R}_{\text{final}} = \sum_{k \in \{A,B,C,D\}} w_k \mathcal{R}^k$$



*Cost Penalty:* The Bilinear Fix increases the total number of rays requiring explicit path-tracing by a factor of $4\times$ on all cascade layers $i > 0$.

---

## 7. Numerical Formal Solution of the RTE

### The Monochromatic Radiative Transfer Equation

Propagation of specific intensity along $\hat{\omega}$:

$$\hat{\omega}\cdot\nabla I_{\nu,\hat{\omega}}=\eta_{\nu,\hat{\omega}}-\chi_{\nu,\hat{\omega}}I_{\nu,\hat{\omega}}$$

Where:

* $\eta_{\nu,\hat{\omega}}$ is the monochromatic macroscopic emissivity coefficient.
* $\chi_{\nu,\hat{\omega}}$ is the monochromatic macroscopic total opacity coefficient.

### Piecewise Constant Direct Integration

Rays track a uniform rectilinear grid via Digital Differential Analyzer (DDA). In each grid cell $k$ over path length $\Delta s$, thermodynamic state, material properties, and atomic populations are constant. Formal solution from $s$ to $s + \Delta s$:

$$\tau_{\nu}(s + \Delta s) = \tau_{\nu}(s) + \chi_{\nu,k,\hat{\omega}} \Delta s$$

$$I_{\nu}(s + \Delta s) = I_{\nu}(s) \exp\left(-\chi_{\nu,k,\hat{\omega}} \Delta s\right) + \frac{\eta_{\nu,k,\hat{\omega}}}{\chi_{\nu,k,\hat{\omega}}} \left(1 - \exp\left(-\chi_{\nu,k,\hat{\omega}} \Delta s\right)\right)$$

Where $\frac{\eta_{\nu,k,\hat{\omega}}}{\chi_{\nu,k,\hat{\omega}}} = S_{\nu,k}$ defines the localized cell source function (neglecting background isotropic scattering).

### Velocity Frame Transformation

Moving media: velocity-independent terms (continua) once per wavelength. Line profiles are velocity-dependent (Doppler). Frame transform evaluates the observer-frame profile on-the-fly from $\vec{v}_k \cdot \hat{\omega}$:

$$v_{\text{projected}} = \vec{v}_k \cdot \hat{\omega}$$

---

## 8. Multi-Level Accelerated Lambda Iteration (MALI)

For self-consistent atomic level populations, DexRT couples the Radiance Cascades formal solver to a preconditioned Multi-level Accelerated Lambda Iteration scheme using Rybicki & Hummer (1992) "same-preconditioning."

### Diagonal Approximate Lambda Operator (ALO)

Under piecewise-constant spatial assumption, a local diagonal operator $\Lambda^*$ lives only on Cascade 0 cells. Directional monochromatic ALO for cell $k$ along $\hat{\omega}$: core-to-edge optical depth transmittance:

$$\Lambda_{\nu,\hat{\omega}}^*(k) = \exp\left(-\tau_{\nu,k,\hat{\omega}}\right)$$

Where $\tau_{\nu,k,\hat{\omega}} = \chi_{\nu,k,\hat{\omega}} \cdot ds_{\text{mid}}$, representing the optical depth from the geometric center of cell $k$ to its exit boundary along direction $\hat{\omega}$.

Preconditioning operator $\Psi^*$ (ALO normalized by total opacity):

$$\Psi_{\nu,\hat{\omega}}^*(k) = \frac{\Lambda_{\nu,\hat{\omega}}^*(k)}{\chi_{\nu,k,\hat{\omega}}}$$

### Effective Intensity Formulation

Line radiation field preconditioned per transition. Upper $j$ → lower $i$, effective intensity $I^{\text{eff}}$ at cell $k$:

$$I_{\nu,\hat{\omega};ji}^{\text{eff}}(k) = I_{\nu,\hat{\omega}}^{\dagger}(k) - \Psi_{\nu,\hat{\omega}}^*(k) \cdot U_{ji}(k)$$

Where:

* $\dagger$ denotes the state evaluated at the previous non-LTE iteration step.
* $I_{\nu,\hat{\omega}}^{\dagger}(k)$ is the current specific intensity field provided directly by the formal solver.
* $U_{ji}(k)$ is the preconditioned population transition rate term.

Integrate $I^{\text{eff}}$ over frequency and angle into off-diagonal terms of rate matrix $\Gamma_s$. Advance populations toward statistical equilibrium:

$$\Gamma_{s}\vec{n}_{s}=\vec{0}$$

Where $\vec{n}_s$ is the vector of atomic level populations for atomic species $s$. Diagonal terms of $\Gamma_s$ close via the particle conservation equation.

---

## 9. Thermodynamic Conservation Laws

MHD input models often fix pressure or charge conservation. Two auxiliary iterations hang off the population solver to keep those constraints.

### 1. Post-MALI Charge Conservation

Electron density $n_e$ is adjusted self-consistently after each statistical equilibrium update via a localized single-variable Newton-Raphson iteration:

$$n_e^{m+1} = n_e^m - \left[\left(\frac{\partial F_{\text{charge}}}{\partial n_e}\right)^{-1} F_{\text{charge}}\right]^m$$

Where $F_{\text{charge}} = \sum_{\text{ions}} Z \cdot n_{\text{ion}} - n_e = 0$, with hydrogen ionization states in the active Jacobian.

### 2. Pressure Conservation Equation

Local gas pressure equilibrium: scale total macroscopic hydrogen number density $n_{\text{Htot}}$ with electron-density changes so total particle count keeps $P$ fixed. Ideal gas thermal pressure:

$$P = N_{\text{tot}} k_{B} T = \left(A_{\text{tot}} n_{\text{Htot}} + n_e\right) k_{B} T$$

Where:

* $N_{\text{tot}}$ is the sum total number density of all atomic nuclei and free electrons.
* $A_{\text{tot}} = \frac{N_{\text{nuclei}}}{n_{\text{Htot}}}$ is the fixed elemental chemical abundance fraction of all atomic species relative to hydrogen.
* $k_B$ is the Boltzmann constant and $T$ is the local plasma electron temperature.

$n_{\text{Htot}}^{\text{new}}$ from updated $n_e^{\text{new}}$ to hold input pressure $P$:

$$n_{\text{Htot}}^{\text{new}} = \frac{\frac{P}{k_B T} - n_e^{\text{new}}}{A_{\text{tot}}}$$

Rescale all $\vec{n}_s$ proportionally to the adjusted density baseline before the next formal solution pass.
