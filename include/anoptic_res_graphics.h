/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- the graphics extension. Where parsing genuinely lives:
// glTF understanding (cgltf) and image decode (stb_image) happen INSIDE
// src/resources/graphics/ and appear nowhere else. Ingest runs through a monotonic
// parse-staging arena that winks out before return; the conditioned scene lives in
// manager memory as an owned resource, served through the same anores_t grammar as
// everything else: handles in, views out, generations retire.
//
// What a scene view serves is FILE TRUTH: geometry conditioned to the engine's vertex
// layout, the node hierarchy, materials with every factor and texture reference the
// file declares (feature bits below), and image entries as logical paths ready for
// ano_res_get + ano_resgfx_image. GPU concerns -- which features the active pipelines
// support, which images to decode, bindless registration, SSBO baking -- stay in the
// renderer, applied ON these views.

#ifndef ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H
#define ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H

#include <stdint.h>

#include "anoptic_filesystem.h"   // MAXPATH: image entries carry logical paths
#include "anoptic_math.h"         // mat4, row-major, the engine-wide convention
#include "anoptic_resources.h"

// ---------------------------------------------------------------------------------------------
// Material feature bits: pure file truth (what the glTF declares), the canonical
// definition. Values deliberately mirror the renderer's PbrFeatureFlags so the
// integration masks them straight against pipeline capabilities (static_asserted at
// the consumer).

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
};

// ---------------------------------------------------------------------------------------------
// The conditioned scene, all views into one manager-owned block.

// Matches the renderer's Vertex layout (8 packed floats); asserted at the consumer.
typedef struct anoresgfx_vertex {
    float position[3];
    float normal[3];        // (0,1,0) when the file has none
    float texcoord[2];      // 0 when the file has none
} anoresgfx_vertex;

// One drawable primitive: ranges into the scene's shared vertex/index arrays.
// Primitives without positions or indices are dropped at ingest (with a log line).
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
    int32_t  mesh;                          // into meshes[], -1 = none
    int32_t  parent;                        // into nodes[], -1 = root
    uint32_t child_first, child_count;      // into children[] (node indices)
} anoresgfx_node;

// A texture reference: an index into images[], -1 = absent. scale carries the slot's
// scalar (normal scale, occlusion strength), 1.0 elsewhere.
typedef struct anoresgfx_texref {
    int32_t image;
    float   scale;
} anoresgfx_texref;

// An image the file references, as a LOGICAL path resolved against the glTF's own
// directory (URI percent-decoding and ./.. collapsing done). "" when the image is not
// URI-addressed (embedded images are not served in v1). srgb aggregates the slots that
// sample it as color -- decode hint, exactly today's renderer classification.
typedef struct anoresgfx_image {
    char     path[MAXPATH];
    uint32_t srgb;
} anoresgfx_image;

// Every factor and reference the file declares; features says which groups carry
// meaning. Untouched groups hold glTF defaults, texrefs hold -1.
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

// The scene view: counted arrays borrowing manager memory, valid until the scene
// handle's generation retires. A zeroed struct means the handle was stale/sentinel.
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
} anoresgfx_scene;

// ---------------------------------------------------------------------------------------------
// Ingest and serve.

// Ingest a glTF resource into a conditioned scene. src is a live handle to the .gltf
// bytes (ano_res_get); sibling buffers (.bin) and data: URIs resolve through the
// namespace against the source's own logical directory. The scene becomes an owned
// resource keyed "<source>#gfx" -- single-copy: repeat ingest of the same source
// returns the same handle. src itself is left loaded (unload it if the raw JSON is no
// longer wanted). Sentinel on parse failure, one log line. Parse staging is a
// monotonic arena winked out before return; zero loose malloc/free.
anores_t ano_resgfx_model(anores_t src);

// The scene view for a conditioned handle. Zeroed struct on sentinel/stale handles.
anoresgfx_scene ano_resgfx_scene(anores_t scene);

// Decoded pixels, tightly packed RGBA8, top-left origin.
typedef struct anoresgfx_pixels {
    uint8_t *rgba;                  // caller-owned: free with ano_aligned_free
    uint32_t width, height;
} anoresgfx_pixels;

// Decode an image resource (PNG/JPEG/TGA/BMP/...) into RGBA8. The block is the
// CALLER's -- an ano_res_release-style hand-off; the src handle stays loaded (unload
// it to drop the encoded bytes). Zeroed struct on failure, one log line.
anoresgfx_pixels ano_resgfx_image(anores_t src);

#endif // ANOPTICENGINE_ANOPTIC_RES_GRAPHICS_H
