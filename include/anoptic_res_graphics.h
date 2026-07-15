/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Graphics extension. glTF (cgltf) and image decode (stb_image) live only in src/resources/graphics/.
// Ingest uses a monotonic parse-staging arena winked before return. Conditioned scene is manager-owned under "<source>#gfx".
// Scene view is FILE TRUTH for geometry/materials/URI images. GPU pipeline/bindless/SSBO concerns stay in the renderer.
// Vertex layout fields are fixed. Current ingest fills position/normal/texcoord0 only; other vertex attrs stay zero.
// Skins/animations/samplers/cameras/lights are typed here but not conditioned or served yet (view leaves them zero).

#ifndef ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H
#define ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H

#include <stdint.h>

#include "anoptic_filesystem.h"   // MAXPATH: image entries carry logical paths
#include "anoptic_math.h"         // mat4, row-major
#include "anoptic_resources.h"

// Scene block kind tag. Stable on disk and in packs.
#define ANORESGFX_TAG_SCENE   0x58464752u   // 'RGFX'
#define ANORESGFX_TAG_BINDING 0x444E4247u   // 'GBND' derived GPU binding table

/* Material feature bits */

// File truth from glTF. Mirror renderer PbrFeatureFlags. Append only.

enum {
    ANORESGFX_PBR_NONE                       = 0,
    ANORESGFX_PBR_BASE_COLOR_FACTOR          = 1 << 0,
    ANORESGFX_PBR_BASE_COLOR_TEXTURE         = 1 << 1,
    ANORESGFX_PBR_METALLIC_ROUGHNESS_FACTOR  = 1 << 2,
    ANORESGFX_PBR_METALLIC_ROUGHNESS_TEXTURE = 1 << 3,
    ANORESGFX_PBR_NORMAL_TEXTURE             = 1 << 4,
    ANORESGFX_PBR_OCCLUSION_TEXTURE          = 1 << 5,
    ANORESGFX_PBR_EMISSIVE_FACTOR            = 1 << 6,
    ANORESGFX_PBR_EMISSIVE_TEXTURE           = 1 << 7,
    ANORESGFX_PBR_ALPHA_MODE_OPAQUE          = 1 << 8,
    ANORESGFX_PBR_ALPHA_MODE_MASK            = 1 << 9,
    ANORESGFX_PBR_ALPHA_MODE_BLEND           = 1 << 10,
    ANORESGFX_PBR_DOUBLE_SIDED               = 1 << 11,
    ANORESGFX_PBR_CLEARCOAT                  = 1 << 12,
    ANORESGFX_PBR_TRANSMISSION               = 1 << 13,
    ANORESGFX_PBR_VOLUME                     = 1 << 14,
    ANORESGFX_PBR_IOR                        = 1 << 15,
    ANORESGFX_PBR_SPECULAR                   = 1 << 16,
    ANORESGFX_PBR_SHEEN                      = 1 << 17,
    ANORESGFX_PBR_IRIDESCENCE                = 1 << 18,
    ANORESGFX_PBR_ANISOTROPY                 = 1 << 19,
    ANORESGFX_PBR_DISPERSION                 = 1 << 20,
    ANORESGFX_PBR_DIFFUSE_TRANSMISSION       = 1 << 21,
    ANORESGFX_PBR_EMISSIVE_STRENGTH          = 1 << 22,
    ANORESGFX_PBR_SPECULAR_GLOSSINESS        = 1 << 23,
    ANORESGFX_PBR_TEXTURE_TRANSFORM          = 1 << 24,  // any slot carries KHR_texture_transform
    ANORESGFX_PBR_UNLIT                      = 1 << 25,  // KHR_materials_unlit
};

/* Conditioned scene views */

// Views into one manager-owned block.

// Engine vertex, 96 bytes. Ingest fills position, normal (default 0,1,0), texcoord0. Other fields stay zero.
typedef struct anoresgfx_vertex {
    float    position[3];
    float    normal[3];
    float    tangent[4];    // xyz = tangent, w = bitangent handedness sign (+/-1)
    float    color[4];      // COLOR_0, linear float4
    float    texcoord[2];   // TEXCOORD_0
    float    texcoord1[2];  // TEXCOORD_1
    uint16_t joints[4];     // JOINTS_0, widened to u16
    float    weights[4];    // WEIGHTS_0
} anoresgfx_vertex;

// Drawable primitive: ranges into shared vertex/index arrays. Dropped at ingest if no positions or indices.
typedef struct anoresgfx_prim {
    uint32_t vertex_first, vertex_count;
    uint32_t index_first,  index_count;     // indices are primitive-local (0-based)
    int32_t  material;                      // into materials[], -1 = none
} anoresgfx_prim;

typedef struct anoresgfx_mesh {
    uint32_t prim_first, prim_count;        // into prims[]
} anoresgfx_mesh;

typedef struct anoresgfx_node {
    char     name[64];
    mat4     local;                         // local transform, row-major
    int32_t  mesh;                          // into meshes[],     -1 = none
    int32_t  parent;                        // into nodes[],      -1 = root
    uint32_t child_first, child_count;      // into children[] (node indices)
    int32_t  skin;                          // reserved, ingest leaves 0
    int32_t  camera;                        // reserved, ingest leaves 0
    int32_t  light;                         // reserved, ingest leaves 0
    uint32_t _pad;
} anoresgfx_node;

/* Skinning (typed, not conditioned yet) */

typedef struct anoresgfx_skin {
    char     name[64];
    uint32_t joint_first, joint_count;      // into joints[] and inverse_binds[], parallel
    int32_t  skeleton;                      // into nodes[], -1 = none
    uint32_t _pad;
} anoresgfx_skin;

/* Animation (typed, not conditioned yet) */

typedef enum anoresgfx_interp {
    ANORESGFX_INTERP_LINEAR = 0,
    ANORESGFX_INTERP_STEP,
    ANORESGFX_INTERP_CUBICSPLINE,           // output stride is 3x: in-tangent, value, out-tangent
} anoresgfx_interp;

typedef enum anoresgfx_anim_path {
    ANORESGFX_PATH_TRANSLATION = 0,         // vec3
    ANORESGFX_PATH_ROTATION,                // vec4 quaternion (xyzw)
    ANORESGFX_PATH_SCALE,                   // vec3
    ANORESGFX_PATH_WEIGHTS,                 // morph weights, `components` per key
} anoresgfx_anim_path;

typedef struct anoresgfx_anim_sampler {
    uint32_t input_first,  input_count;     // into anim_input[]  (keyframe times, seconds)
    uint32_t output_first, output_count;    // into anim_output[] (floats)
    uint32_t interpolation;                 // anoresgfx_interp
    uint32_t components;                    // floats per key (3, 4, or morph target count)
} anoresgfx_anim_sampler;

typedef struct anoresgfx_anim_channel {
    uint32_t sampler;                       // into anim_samplers[]
    int32_t  target_node;                   // into nodes[], -1 = channel dropped at ingest
    uint32_t path;                          // anoresgfx_anim_path
    uint32_t _pad;
} anoresgfx_anim_channel;

typedef struct anoresgfx_animation {
    char     name[64];
    uint32_t channel_first, channel_count;  // into anim_channels[]
    float    duration;                      // seconds, max input time across samplers
    uint32_t _pad;
} anoresgfx_animation;

/* Texture references and the sampler table */

// glTF sampler enums, verbatim (0 = unspecified, engine default).
typedef struct anoresgfx_sampler {
    uint32_t mag_filter, min_filter;        // GL enums as the file declares them
    uint32_t wrap_s, wrap_t;
} anoresgfx_sampler;

enum {
    ANORESGFX_TEXREF_TRANSFORM = 1u << 0,   // KHR_texture_transform present on this slot
};

// Texture ref into images[], -1 = absent. Ingest fills image + scale. sampler/uv/xform stay 0.
typedef struct anoresgfx_texref {
    int32_t  image;
    float    scale;
    uint32_t uv_set;
    int32_t  sampler;                       // into samplers[], -1 = engine default
    uint32_t flags;                         // ANORESGFX_TEXREF_*
    float    xform_offset[2];
    float    xform_rotation;                // radians, counter-clockwise about (0,0)
    float    xform_scale[2];
} anoresgfx_texref;

/* Images */

// URI images carry a logical path. Embedded/data-URI images are skipped at ingest (path empty, bytes_len 0).

typedef struct anoresgfx_image {
    char     path[MAXPATH];
    uint32_t srgb;
    uint32_t mime;                          // FOURCC-ish: 'PNG ', 'JPEG', 'KTX2', 0 = unknown
    uint64_t bytes_off;                     // reserved: offset INTO THE SCENE BLOCK
    uint64_t bytes_len;                     // reserved: 0 today
} anoresgfx_image;

#define ANORESGFX_MIME_PNG  0x20474E50u   // 'PNG '
#define ANORESGFX_MIME_JPEG 0x4745504Au   // 'JPEG'
#define ANORESGFX_MIME_KTX2 0x3258544Bu   // 'KTX2'

/* Cameras and lights (typed, not conditioned yet) */

typedef enum anoresgfx_camera_type {
    ANORESGFX_CAMERA_PERSPECTIVE = 0,
    ANORESGFX_CAMERA_ORTHOGRAPHIC,
} anoresgfx_camera_type;

typedef struct anoresgfx_camera {
    char     name[64];
    uint32_t type;                          // anoresgfx_camera_type
    float    yfov;                          // perspective: vertical fov, radians
    float    aspect;                        // perspective: 0 = use viewport
    float    xmag, ymag;                    // orthographic half-extents
    float    znear, zfar;                   // zfar 0 = infinite perspective
} anoresgfx_camera;

typedef enum anoresgfx_light_type {
    ANORESGFX_LIGHT_DIRECTIONAL = 0,
    ANORESGFX_LIGHT_POINT,
    ANORESGFX_LIGHT_SPOT,
} anoresgfx_light_type;

typedef struct anoresgfx_light {
    char     name[64];
    float    color[3];                      // linear RGB, normalized
    float    intensity;                     // candela (point/spot) or lux (directional)
    float    range;                         // 0 = unbounded
    float    inner_cone, outer_cone;        // spot half-angles, radians
    uint32_t type;                          // anoresgfx_light_type
} anoresgfx_light;

/* Material */

// Factors and refs the file declares. `features` marks meaningful groups. Untouched groups hold glTF defaults.
// TEXTURE_TRANSFORM / UNLIT feature bits are not stamped yet.

typedef struct anoresgfx_material {
    char     name[64];
    uint32_t features;                      // ANORESGFX_PBR_*

    float base_color_factor[4];
    float metallic_factor, roughness_factor;
    anoresgfx_texref base_color, metallic_roughness;

    anoresgfx_texref normal, occlusion, emissive;
    float    emissive_factor[3];
    uint32_t alpha_mode;                    // 0 opaque, 1 mask, 2 blend
    float    alpha_cutoff;
    uint32_t double_sided;

    float clearcoat_factor, clearcoat_roughness_factor;
    anoresgfx_texref clearcoat, clearcoat_roughness, clearcoat_normal;

    float transmission_factor;
    anoresgfx_texref transmission;

    float thickness_factor, attenuation_distance;
    float attenuation_color[3];
    anoresgfx_texref thickness;

    float ior;

    float specular_factor;
    float specular_color_factor[3];
    anoresgfx_texref specular, specular_color;

    float sheen_roughness_factor;
    float sheen_color_factor[3];
    anoresgfx_texref sheen_color, sheen_roughness;

    float iridescence_factor, iridescence_ior;
    float iridescence_thickness_min, iridescence_thickness_max;
    anoresgfx_texref iridescence, iridescence_thickness;

    float anisotropy_strength, anisotropy_rotation;
    anoresgfx_texref anisotropy;

    float dispersion;

    float diffuse_transmission_factor;
    float diffuse_transmission_color_factor[3];
    anoresgfx_texref diffuse_transmission, diffuse_transmission_color;

    float emissive_strength;
} anoresgfx_material;

/* Scene view */

// Counted arrays borrowing manager memory until the scene handle's generation retires.
// Served today: vertices/indices/prims/meshes/nodes/children/materials/images/roots.
// Skins/anims/samplers/cameras/lights stay zero. Zeroed means stale, sentinel, or failed validation.

typedef struct anoresgfx_scene {
    const anoresgfx_vertex   *vertices;   uint32_t vertex_count;
    const uint32_t           *indices;    uint32_t index_count;
    const anoresgfx_prim     *prims;      uint32_t prim_count;
    const anoresgfx_mesh     *meshes;     uint32_t mesh_count;
    const anoresgfx_node     *nodes;      uint32_t node_count;
    const uint32_t           *children;   uint32_t child_count;
    const anoresgfx_material *materials;  uint32_t material_count;
    const anoresgfx_image    *images;     uint32_t image_count;
    const uint32_t           *roots;      uint32_t root_count;   // parentless nodes

    const anoresgfx_skin     *skins;      uint32_t skin_count;
    const uint32_t           *joints;     uint32_t joint_count;  // node indices, skin-spanned
    const mat4               *inverse_binds;                     // parallel to joints[]

    const anoresgfx_animation     *animations;    uint32_t animation_count;
    const anoresgfx_anim_channel  *anim_channels; uint32_t anim_channel_count;
    const anoresgfx_anim_sampler  *anim_samplers; uint32_t anim_sampler_count;
    const float                   *anim_input;    uint32_t anim_input_count;
    const float                   *anim_output;   uint32_t anim_output_count;

    const anoresgfx_sampler  *samplers;   uint32_t sampler_count;
    const anoresgfx_camera   *cameras;    uint32_t camera_count;
    const anoresgfx_light    *lights;     uint32_t light_count;
} anoresgfx_scene;

/* Ingest and serve */

// Ingest glTF into a conditioned scene adopted as "<source>#gfx". Single-copy. Staging arena winked before return. Sentinel on parse failure.
anores_t ano_resgfx_model(ano_res_lifetime lifetime, const ano_res_read *read, anores_t src);

// Scene view for a conditioned handle. Pointers die with read. Zeroed on refusal.
anoresgfx_scene ano_resgfx_scene(const ano_res_read *read, anores_t scene);

// Declared. No definition yet.
anostr_t ano_resgfx_image_bytes(const ano_res_read *read, anores_t scene, uint32_t image);

// Decoded pixels, tightly packed RGBA8, top-left origin.
// CALLER frees rgba with ano_aligned_free. src must stay loaded for the borrow that fed decode.
typedef struct anoresgfx_pixels {
    uint8_t *rgba;
    uint32_t width, height;
} anoresgfx_pixels;

anoresgfx_pixels ano_resgfx_image(ano_res_lifetime lifetime, const ano_res_read *read,
                                  anores_t src);

#endif // ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H
