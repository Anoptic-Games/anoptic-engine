## Core glTF 2.0 PBR Properties

### 1. `pbrMetallicRoughness` Parameter Block

This object defines the primary inputs for glTF's baseline metallic-roughness evaluation.

| Property | Type / Format | Default Value | Color Space / Handling | Description |
| --- | --- | --- | --- | --- |
| **`baseColorFactor`** | `number[4]` | `[1.0, 1.0, 1.0, 1.0]` | **sRGB** (RGB), **Linear** (A) | Array of 4 floating-point values representing the RGBA material tint. |
| **`baseColorTexture`** | `textureInfo` | `null` | **sRGB** | The main diffuse texture map for dielectrics, or reflection color for metals. |
| **`metallicFactor`** | `number` | `1.0` | **Linear** | Scalar value from $0.0$ (dielectric/insulator) to $1.0$ (pure conductor/metal). |
| **`roughnessFactor`** | `number` | `1.0` | **Linear** | Surface microfacet roughness scalar from $0.0$ (perfect mirror smooth) to $1.0$ (fully diffuse/matte). |
| **`metallicRoughnessTexture`** | `textureInfo` | `null` | **Linear** | A multi-channel packed texture map:<br>

<br>• **Green Channel**: Roughness values<br>

<br>• **Blue Channel**: Metalness values |

### 2. Top-Level Material Properties (Core)

These foundational PBR shading parameters live at the root of the material block.

| Property | Type / Format | Default Value | Color Space / Handling | Description |
| --- | --- | --- | --- | --- |
| **`normalTexture`** | `object` | `null` | **Linear** | Shading normal map. Tangent-space vectors are stored in RGB or RG channels. Includes an optional scalar **`scale`** parameter (`number`, default: `1.0`). |
| **`occlusionTexture`** | `object` | `null` | **Linear** | Ambient occlusion map. Static shading attenuation factor packed into the **Red** channel. Includes an optional scalar **`strength`** parameter (`number`, default: `1.0`). |
| **`emissiveFactor`** | `number[3]` | `[0.0, 0.0, 0.0]` | **sRGB** | Array of 3 floating-point numbers defining the base RGB illumination color emitted by the surface. |
| **`emissiveTexture`** | `textureInfo` | `null` | **sRGB** | Texture mapping containing self-illumination color data. |
| **`alphaMode`** | `string` | `"OPAQUE"` | *Enumeration* | Dictates alpha visibility behavior. Supported values: `"OPAQUE"`, `"MASK"`, or `"BLEND"`. |
| **`alphaCutoff`** | `number` | `0.5` | **Linear** | Specifies the discard threshold value when `alphaMode` is set to `"MASK"`. |
| **`doubleSided`** | `boolean` | `false` | *Boolean* | Toggles back-face culling. When true, both sides of the primitive geometry are illuminated and shaded using flipped normals. |

---

## Ratified Khronos PBR Extensions (`extensions`)

These extensions add parameters to model complex real-world microfacet interactions and volume effects.

### 1. Clearcoat (`KHR_materials_clearcoat`)

Simulates a thin, highly specular layer on top of the base material structure (e.g., automotive paint or varnished wood).

* **`clearcoatFactor`** (`number`): The scalar layer intensity from $0.0$ to $1.0$. Default: `0.0`.
* **`clearcoatTexture`** (`textureInfo`): Clearcoat strength map. Packed into the **Red** channel (**Linear**).
* **`clearcoatRoughnessFactor`** (`number`): Microfacet roughness of the coat layer from $0.0$ to $1.0$. Default: `0.0`.
* **`clearcoatRoughnessTexture`** (`textureInfo`): Clearcoat roughness map. Packed into the **Green** channel (**Linear**).
* **`clearcoatNormalTexture`** (`object`): Independent normal map for the clearcoat layer (**Linear**).

### 2. Transmission & Volume (`KHR_materials_transmission` & `KHR_materials_volume`)

Models physical light scattering and refraction through solid, dense transparent media.

* **Transmission Properties (`KHR_materials_transmission`)**:
* **`transmissionFactor`** (`number`): Percentage of light allowed to transmit through the surface. Default: `0.0`.
* **`transmissionTexture`** (`textureInfo`): Transmission map. Packed into the **Red** channel (**Linear**).


* **Volume Properties (`KHR_materials_volume`)**:
* **`thicknessFactor`** (`number`): The thin/thick volume boundary distance beneath the mesh geometry in scene meters. Default: `0.0`.
* **`thicknessTexture`** (`textureInfo`): Thickness profile map. Packed into the **Green** channel (**Linear**).
* **`attenuationDistance`** (`number`): Density tracking scalar. Represents the distance a ray travels before absorbing/scattering into `attenuationColor`. Default: `+Infinity`.
* **`attenuationColor`** (`number[3]`): RGB color shift at one `attenuationDistance` depth. Default: `[1.0, 1.0, 1.0]` (**sRGB**).



### 3. Index of Refraction (`KHR_materials_ior`)

* **`ior`** (`number`): Overrides the default dielectric Index of Refraction ($1.5$) used to calculate baseline Fresnel reflectivity $F_0$. Valid bounds: $[1.0, \infty)$. Default: `1.5`.

### 4. Specular BDRF Adjustments (`KHR_materials_specular`)

Allows tuning of dielectric reflectivity without forcing a fallback to the non-conserving Specular-Glossiness workflow.

* **`specularFactor`** (`number`): Base multiplier for dielectric specular strength. Default: `1.0`.
* **`specularTexture`** (`textureInfo`): Specular strength map packed into the **Alpha** channel (**Linear**).
* **`specularColorFactor`** (`number[3]`): Specular chromatic color tint. Default: `[1.0, 1.0, 1.0]` (**sRGB**).
* **`specularColorTexture`** (`textureInfo`): Specular color tint map (**sRGB**).

### 5. Sheen (`KHR_materials_sheen`)

Simulates cloth backscattering caused by micro-fibers (e.g., velvet, satin, and soft fabrics).

* **`sheenColorFactor`** (`number[3]`): Sheen specular color response. Default: `[0.0, 0.0, 0.0]` (**sRGB**).
* **`sheenColorTexture`** (`textureInfo`): Sheen color map (**sRGB**).
* **`sheenRoughnessFactor`** (`number`): Roughness of the fiber scattering layer. Default: `0.0`.
* **`sheenRoughnessTexture`** (`textureInfo`): Sheen roughness map packed into the **Alpha** channel (**Linear**).

### 6. Iridescence (`KHR_materials_iridescence`)

Simulates thin-film wave interference, creating shifting rainbow hues across viewing angles (e.g., soap bubbles, insect wings).

* **`iridescenceFactor`** (`number`): Iridescence intensity weight. Default: `0.0`.
* **`iridescenceTexture`** (`textureInfo`): Intensity map packed into the **Red** channel (**Linear**).
* **`iridescenceIor`** (`number`): Refractive index of the thin-film coat layer. Default: `1.3`.
* **`iridescenceThicknessMinimum`** (`number`): Minimum thin-film thickness boundary in nanometers. Default: `100.0`.
* **`iridescenceThicknessMaximum`** (`number`): Maximum thin-film thickness boundary in nanometers. Default: `400.0`.
* **`iridescenceThicknessTexture`** (`textureInfo`): Thickness map interpolating between min/max bounds. Packed into the **Green** channel (**Linear**).

### 7. Anisotropy (`KHR_materials_anisotropy`)

Enables non-symmetrical specular highlights found on micro-grooved surfaces like brushed metal or hair.

* **`anisotropyStrength`** (`number`): Degree of specular elongation. Default: `0.0`.
* **`anisotropyRotation`** (`number`): Tangent space counter-clockwise rotation vector measured in radians. Default: `0.0`.
* **`anisotropyTexture`** (`textureInfo`): Multi-channel directional data map (**Linear**):
* **Red & Green Channels**: Encodes the direction vector in $[-1, 1]$ tangent/bitangent space.
* **Blue Channel**: Encodes per-pixel anisotropy strength multipliers.



### 8. Dispersion (`KHR_materials_dispersion`)

* **`dispersion`** (`number`): Adds angular color splitting (chromatic aberration) to transmissive volumes. Quantified using a transformed Abbe number calculation: 
$$\text{dispersion} = \frac{20}{V_d}$$


 Default: `0.0` (no dispersion).

### 9. Diffuse Transmission (`KHR_materials_diffuse_transmission`)

Models light diffusely passing through thin translucent surfaces (e.g., leaves, paper lanterns).

* **`diffuseTransmissionFactor`** (`number`): Scaling value for transmitted diffuse light. Default: `0.0`.
* **`diffuseTransmissionTexture`** (`textureInfo`): Transmission intensity map packed into the **Alpha** channel (**Linear**).
* **`diffuseTransmissionColorFactor`** (`number[3]`): Transmitted RGB tint color. Default: `[1.0, 1.0, 1.0]` (**sRGB**).
* **`diffuseTransmissionColorTexture`** (`textureInfo`): Transmitted color map (**sRGB**).

### 10. Emissive Strength (`KHR_materials_emissive_strength`)

* **`emissiveStrength`** (`number`): A high-dynamic-range scalar multiplier for `emissiveFactor` allowing physical luminance values exceeding $1.0$ (e.g., to guide post-process bloom or physically based light values). Default: `1.0`.

---

## Legacy Extensions (Superseded)

* **`KHR_materials_pbrSpecularGlossiness`**: The original alternative pipeline to metallic-roughness. It contains `diffuseFactor` (`number[4]`), `diffuseTexture` (`sRGB`), `specularFactor` (`number[3]`), `glossinessFactor` (`number`), and `specularGlossinessTexture` (Specular RGB in sRGB, Glossiness in Alpha). **It is officially superseded by `KHR_materials_specular`.**

---

## Resource Formats and Optimization Standards

### Supported Texture Formats

glTF enforces a constrained set of image formats to ensure predictable cross-platform loading:

1. **PNG (`image/png`)**: Standard asset format for lossless textures and data packed with highly discrete alpha masking channels.
2. **JPEG (`image/jpeg`)**: Standard choice for color images without alpha details where lossy file sizes are acceptable.
3. **KTX 2.0 (`image/ktx2` via `KHR_texture_basisu`)**: The optimized standard. Encodes textures into Basis Universal formats (ETC1S or UASTC). These remain compressed in system memory and stream straight into GPU hardware-compressed texture layouts (BC7, ASTC, ETC2) without CPU inflation stutters.
4. **WebP (`image/webp` via `EXT_texture_webp`)** / **AVIF (`image/avif` via `EXT_texture_avif`)**: High-efficiency container formats for standard web runtimes.

### Component Packing (The ORM Mapping Standard)

To avoid excessive GPU texture descriptor updates and server requests, single-channel properties are tightly interleaved into single 3-component textures. The standard packing for standard metallic-roughness surfaces is the **ORM Texture Map**:

> **Red Channel:** Ambient Occlusion (`occlusionTexture`)
> **Green Channel:** Roughness (`metallicRoughnessTexture`)
> **Blue Channel:** Metallic (`metallicRoughnessTexture`)
