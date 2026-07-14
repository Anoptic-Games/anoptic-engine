/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The graphics ingest: cgltf and stb_image live HERE and nowhere else. All file bytes
// arrive through the resource namespace (cgltf's file callback routes through the
// mount walk); every parse-time allocation lands in a monotonic staging arena over a
// scoped heap, winked out when ingest returns. The conditioned scene is one
// self-contained offset-based block (load-in-place shaped, for the step-7 bake)
// adopted into the registry under "<source>#gfx".

#include <anoptic_res_graphics.h>

#include <anoptic_log.h>
#include <anoptic_memory_pools.h>

#include <stdio.h>
#include <string.h>

#include "../resources_ext.h"
#include "../resources_internal.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO               // decode from memory only: no file IO in here
#define STBI_NO_HDR                 // LDR only: keeps libm (pow) out of anoptic_core
#define STBI_NO_LINEAR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image.h>
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------------------------------
// The extension descriptor (M2). Graphics owns every kind the old core suffix switch used
// to classify -- shader, font, encoded image -- plus its own conditioned/derived kinds.
// classify() is that switch, verbatim, now living with its owner. derive/validate/deps_of/
// share_policy land with M9/M11; NULL is a declared absence, not a stub lie.

// Suffix classification. Inputs: a validated logical path and its length. Output: the
// owning fourcc, or 0 when this extension does not claim the path.
static uint32_t gfx_classify(const char *logical, size_t len)
{
    (void)len;
    const char *dot = strrchr(logical, '.');
    if (dot == NULL)
        return 0;
    if (strcmp(dot, ".spv") == 0 || strcmp(dot, ".vert") == 0 || strcmp(dot, ".frag") == 0)
        return RES_TAG_SHADER;
    if (strcmp(dot, ".ttf") == 0 || strcmp(dot, ".otf") == 0)
        return RES_TAG_FONT;
    if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return RES_TAG_IMAGE_ENC;
    return 0;
}

static const res_ext_kind GFX_KINDS[] = {
    { .tag = RES_TAG_GFX_SCENE,   .name = "graphics.scene",   .derived = true,  .bakeable = true  },
    { .tag = RES_TAG_GFX_BINDING, .name = "graphics.binding", .derived = true,  .bakeable = false },
    { .tag = RES_TAG_IMAGE_ENC,   .name = "graphics.image",   .derived = false, .bakeable = false },
    { .tag = RES_TAG_IMAGE_DEC,   .name = "graphics.pixels",  .derived = true,  .bakeable = false },
    { .tag = RES_TAG_FONT,        .name = "graphics.font",    .derived = false, .bakeable = false },
    { .tag = RES_TAG_SHADER,      .name = "graphics.shader",  .derived = false, .bakeable = false },
};

static const res_ext GFX_EXT = {
    .name = "graphics",
    .kinds = GFX_KINDS,
    .kind_count = sizeof GFX_KINDS / sizeof *GFX_KINDS,
    .classify = gfx_classify,
};

// Registration hook, called by res_registry_init before res_ext_freeze(). Declared by an
// extern in the registry rather than a header: every .h under src/resources/ is frozen at
// W0, and the extension roster call belongs to the W6 graphics split (res_gfx_ext.c).
void res_gfx_register_ext(void)
{
    (void)res_ext_register(&GFX_EXT);
}

// ---------------------------------------------------------------------------------------------
// The scene block: header + arrays at 16-aligned offsets, everything relative to the
// block base. Byte-deterministic (zero-filled before writing) so the future bake can
// demand byte-identical output.

#define SCENE_MAGIC   0x58464752u   // "RGFX"
#define SCENE_VERSION 1u

typedef struct scene_hdr {
    uint32_t magic, version;
    uint32_t vertex_count, index_count, prim_count, mesh_count, node_count,
             child_count, material_count, image_count, root_count;
    uint32_t pad;
    uint64_t off_vertices, off_indices, off_prims, off_meshes, off_nodes,
             off_children, off_materials, off_images, off_roots;
} scene_hdr;

static inline uint64_t align16(uint64_t v) { return (v + 15u) & ~UINT64_C(15); }

// ---------------------------------------------------------------------------------------------
// Path plumbing: the glTF's own logical directory is the base for every URI.

// "models/a/b.gltf" -> "models/a/" (empty for a root-level file). out holds MAXPATH.
static size_t dir_of(const char *logical, char *out)
{
    const char *slash = strrchr(logical, '/');
    if (slash == NULL) {
        out[0] = '\0';
        return 0;
    }
    size_t n = (size_t)(slash - logical) + 1;
    memcpy(out, logical, n);
    out[n] = '\0';
    return n;
}

// Collapse "./" and "seg/../" segments in place. Returns the new length, or 0 when the
// path escapes its root ("../" underflow) or ends empty.
static size_t path_collapse(char *p)
{
    size_t len = strlen(p);
    size_t out = 0;
    size_t seg_starts[64];
    size_t depth = 0;
    size_t i = 0;
    while (i <= len) {
        size_t start = i;
        while (i < len && p[i] != '/')
            i++;
        size_t n = i - start;
        if (n == 1 && p[start] == '.') {
            // drop
        } else if (n == 2 && p[start] == '.' && p[start + 1] == '.') {
            if (depth == 0)
                return 0;
            out = seg_starts[--depth];
        } else if (n > 0) {
            if (depth >= sizeof seg_starts / sizeof seg_starts[0])
                return 0;
            seg_starts[depth++] = out;
            memmove(p + out, p + start, n);
            out += n;
            p[out++] = '/';
        }
        i++;
    }
    if (out == 0)
        return 0;
    out--;                                      // drop the trailing '/'
    p[out] = '\0';
    return out;
}

// Read a whole logical file through the mount walk into `heap`. 0 / -1.
static int gfx_slurp(mi_heap_t *heap, const char *logical, void **out, size_t *out_size)
{
    size_t len;
    if (res_path_validate(logical, &len) != 0)
        return -1;
    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++) {
        int rc = res_read_all(heap, cand[i].str, out, out_size);
        if (rc == 0)
            return 0;
        if (rc != -1)
            return -1;                          // opened but failed: do not shadow
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// cgltf hooks: allocations bump through the arena (free is a no-op; the arena winks
// out), file reads run through the namespace into the scoped scratch heap.

typedef struct ingest_ctx {
    mi_heap_t         *scratch;
    ano_mem_monotonic *arena;
} ingest_ctx;

static void *cg_alloc(void *user, cgltf_size size)
{
    ingest_ctx *ctx = user;
    return ano_mem_monotonic_alloc(ctx->arena, size ? size : 1, 0);
}

static void cg_free(void *user, void *ptr)
{
    (void)user;
    (void)ptr;                                  // staging: reclaimed by the wink-out
}

static cgltf_result cg_read(const struct cgltf_memory_options *memory_options,
                            const struct cgltf_file_options *file_options,
                            const char *path, cgltf_size *size, void **data)
{
    (void)memory_options;
    ingest_ctx *ctx = file_options->user_data;
    char norm[MAXPATH];
    size_t plen = strlen(path);
    if (plen >= sizeof norm)
        return cgltf_result_file_not_found;
    memcpy(norm, path, plen + 1);
    if (path_collapse(norm) == 0)
        return cgltf_result_file_not_found;
    void  *buf = NULL;
    size_t got = 0;
    if (gfx_slurp(ctx->scratch, norm, &buf, &got) != 0) {
        ano_log(ANO_ERROR, "res_graphics: buffer not found in any mount: %s", norm);
        return cgltf_result_file_not_found;
    }
    *size = got;
    *data = buf;
    return cgltf_result_success;
}

static void cg_release(const struct cgltf_memory_options *memory_options,
                       const struct cgltf_file_options *file_options, void *data,
                       cgltf_size size)
{
    (void)memory_options;
    (void)file_options;
    (void)data;                                 // scratch heap owns every read
    (void)size;
}

// ---------------------------------------------------------------------------------------------
// Material conditioning: pure file truth.

static uint32_t material_features(const cgltf_material *m)
{
    uint32_t f = ANORESGFX_PBR_NONE;
    if (m->has_pbr_metallic_roughness) {
        f |= ANORESGFX_PBR_BASE_COLOR_FACTOR | ANORESGFX_PBR_METALLIC_ROUGHNESS_FACTOR;
        if (m->pbr_metallic_roughness.base_color_texture.texture)
            f |= ANORESGFX_PBR_BASE_COLOR_TEXTURE;
        if (m->pbr_metallic_roughness.metallic_roughness_texture.texture)
            f |= ANORESGFX_PBR_METALLIC_ROUGHNESS_TEXTURE;
    }
    if (m->normal_texture.texture)    f |= ANORESGFX_PBR_NORMAL_TEXTURE;
    if (m->occlusion_texture.texture) f |= ANORESGFX_PBR_OCCLUSION_TEXTURE;
    if (m->emissive_texture.texture)  f |= ANORESGFX_PBR_EMISSIVE_TEXTURE;
    if (m->emissive_factor[0] > 0.0f || m->emissive_factor[1] > 0.0f
        || m->emissive_factor[2] > 0.0f)
        f |= ANORESGFX_PBR_EMISSIVE_FACTOR;
    if (m->alpha_mode == cgltf_alpha_mode_opaque) f |= ANORESGFX_PBR_ALPHA_MODE_OPAQUE;
    else if (m->alpha_mode == cgltf_alpha_mode_mask) f |= ANORESGFX_PBR_ALPHA_MODE_MASK;
    else if (m->alpha_mode == cgltf_alpha_mode_blend) f |= ANORESGFX_PBR_ALPHA_MODE_BLEND;
    if (m->double_sided)              f |= ANORESGFX_PBR_DOUBLE_SIDED;
    if (m->has_clearcoat)             f |= ANORESGFX_PBR_CLEARCOAT;
    if (m->has_transmission)          f |= ANORESGFX_PBR_TRANSMISSION;
    if (m->has_volume)                f |= ANORESGFX_PBR_VOLUME;
    if (m->has_ior)                   f |= ANORESGFX_PBR_IOR;
    if (m->has_specular)              f |= ANORESGFX_PBR_SPECULAR;
    if (m->has_sheen)                 f |= ANORESGFX_PBR_SHEEN;
    if (m->has_iridescence)           f |= ANORESGFX_PBR_IRIDESCENCE;
    if (m->has_anisotropy)            f |= ANORESGFX_PBR_ANISOTROPY;
    if (m->has_dispersion)            f |= ANORESGFX_PBR_DISPERSION;
    if (m->has_diffuse_transmission)  f |= ANORESGFX_PBR_DIFFUSE_TRANSMISSION;
    if (m->has_emissive_strength)     f |= ANORESGFX_PBR_EMISSIVE_STRENGTH;
    if (m->has_pbr_specular_glossiness) f |= ANORESGFX_PBR_SPECULAR_GLOSSINESS;
    return f;
}

static anoresgfx_texref texref(const cgltf_data *data, const cgltf_texture_view *tv)
{
    anoresgfx_texref r = { -1, 1.0f };
    if (tv->texture && tv->texture->image)
        r.image = (int32_t)(tv->texture->image - data->images);
    r.scale = tv->scale != 0.0f ? tv->scale : 1.0f;
    return r;
}

static void condition_material(const cgltf_data *data, const cgltf_material *m,
                               anoresgfx_material *out)
{
    if (m->name)
        strncpy(out->name, m->name, sizeof out->name - 1);
    out->features = material_features(m);

    // glTF defaults first, file values where declared.
    out->base_color_factor[0] = out->base_color_factor[1] = 1.0f;
    out->base_color_factor[2] = out->base_color_factor[3] = 1.0f;
    out->metallic_factor  = 1.0f;
    out->roughness_factor = 1.0f;
    out->base_color = out->metallic_roughness = (anoresgfx_texref){ -1, 1.0f };
    if (m->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pmr = &m->pbr_metallic_roughness;
        for (int i = 0; i < 4; i++)
            out->base_color_factor[i] = pmr->base_color_factor[i];
        out->metallic_factor    = pmr->metallic_factor;
        out->roughness_factor   = pmr->roughness_factor;
        out->base_color         = texref(data, &pmr->base_color_texture);
        out->metallic_roughness = texref(data, &pmr->metallic_roughness_texture);
    }

    out->normal    = texref(data, &m->normal_texture);
    out->occlusion = texref(data, &m->occlusion_texture);
    out->emissive  = texref(data, &m->emissive_texture);
    for (int i = 0; i < 3; i++)
        out->emissive_factor[i] = m->emissive_factor[i];
    out->alpha_mode = m->alpha_mode == cgltf_alpha_mode_mask ? 1u
                    : m->alpha_mode == cgltf_alpha_mode_blend ? 2u : 0u;
    out->alpha_cutoff = m->alpha_cutoff;
    out->double_sided = m->double_sided ? 1u : 0u;

    out->clearcoat_factor           = m->clearcoat.clearcoat_factor;
    out->clearcoat_roughness_factor = m->clearcoat.clearcoat_roughness_factor;
    out->clearcoat           = texref(data, &m->clearcoat.clearcoat_texture);
    out->clearcoat_roughness = texref(data, &m->clearcoat.clearcoat_roughness_texture);
    out->clearcoat_normal    = texref(data, &m->clearcoat.clearcoat_normal_texture);

    out->transmission_factor = m->transmission.transmission_factor;
    out->transmission        = texref(data, &m->transmission.transmission_texture);

    out->thickness_factor     = m->volume.thickness_factor;
    out->attenuation_distance = m->volume.attenuation_distance;
    for (int i = 0; i < 3; i++)
        out->attenuation_color[i] = m->volume.attenuation_color[i];
    out->thickness = texref(data, &m->volume.thickness_texture);

    out->ior = m->has_ior ? m->ior.ior : 1.5f;

    out->specular_factor = m->has_specular ? m->specular.specular_factor : 1.0f;
    for (int i = 0; i < 3; i++)
        out->specular_color_factor[i] = m->has_specular ? m->specular.specular_color_factor[i] : 1.0f;
    out->specular       = texref(data, &m->specular.specular_texture);
    out->specular_color = texref(data, &m->specular.specular_color_texture);

    out->sheen_roughness_factor = m->sheen.sheen_roughness_factor;
    for (int i = 0; i < 3; i++)
        out->sheen_color_factor[i] = m->sheen.sheen_color_factor[i];
    out->sheen_color     = texref(data, &m->sheen.sheen_color_texture);
    out->sheen_roughness = texref(data, &m->sheen.sheen_roughness_texture);

    out->iridescence_factor        = m->iridescence.iridescence_factor;
    out->iridescence_ior           = m->has_iridescence ? m->iridescence.iridescence_ior : 1.3f;
    out->iridescence_thickness_min = m->iridescence.iridescence_thickness_min;
    out->iridescence_thickness_max = m->iridescence.iridescence_thickness_max;
    out->iridescence           = texref(data, &m->iridescence.iridescence_texture);
    out->iridescence_thickness = texref(data, &m->iridescence.iridescence_thickness_texture);

    out->anisotropy_strength = m->anisotropy.anisotropy_strength;
    out->anisotropy_rotation = m->anisotropy.anisotropy_rotation;
    out->anisotropy = texref(data, &m->anisotropy.anisotropy_texture);

    out->dispersion = m->dispersion.dispersion;

    out->diffuse_transmission_factor = m->diffuse_transmission.diffuse_transmission_factor;
    for (int i = 0; i < 3; i++)
        out->diffuse_transmission_color_factor[i] =
            m->diffuse_transmission.diffuse_transmission_color_factor[i];
    out->diffuse_transmission = texref(data, &m->diffuse_transmission.diffuse_transmission_texture);
    out->diffuse_transmission_color =
        texref(data, &m->diffuse_transmission.diffuse_transmission_color_texture);

    out->emissive_strength = m->has_emissive_strength
                           ? m->emissive_strength.emissive_strength : 1.0f;
}

// Aggregate the color-slot classification per image (the decode-as-sRGB hint).
static void mark_srgb(const cgltf_data *data, anoresgfx_image *images)
{
    for (size_t mi = 0; mi < data->materials_count; mi++) {
        const cgltf_material *m = &data->materials[mi];
        const cgltf_texture_view *color_slots[] = {
            &m->pbr_metallic_roughness.base_color_texture,
            &m->emissive_texture,
            &m->specular.specular_color_texture,
            &m->sheen.sheen_color_texture,
            &m->diffuse_transmission.diffuse_transmission_color_texture,
        };
        for (size_t s = 0; s < sizeof color_slots / sizeof color_slots[0]; s++)
            if (color_slots[s]->texture && color_slots[s]->texture->image)
                images[color_slots[s]->texture->image - data->images].srgb = 1;
    }
}

// ---------------------------------------------------------------------------------------------
// Ingest.

anores_t ano_resgfx_model(ano_res_lifetime lifetime, const ano_res_read *read, anores_t src)
{
    anores_t none = {0};
    anostr_t bytes = ano_res_bytes(read, src);
    char srcname[MAXPATH];
    if (anostr_len(bytes) == 0
        || res_registry_name(read, src, srcname, sizeof srcname) != 0) {
        ano_log(ANO_ERROR, "res_graphics: model ingest on a stale/sentinel handle");
        return none;
    }
    char key[MAXPATH + 8];
    int kw = snprintf(key, sizeof key, "%s#gfx", srcname);
    if (kw < 0 || kw >= (int)sizeof key || kw >= MAXPATH) {
        ano_log(ANO_ERROR, "res_graphics: scene key overflows: %s", srcname);
        return none;
    }
    anores_t existing = res_registry_find(read, key);
    if (existing.gen != 0)
        return existing;                        // single-copy: already conditioned

    // Parse staging: a monotonic arena over a scoped heap; ALL of it winks out at
    // return (cgltf allocations, buffer bytes, everything).
    mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
    if (scratch == NULL)
        return none;
    ano_mem_monotonic *arena = ano_mem_monotonic_make(ano_mem_parent_heap(scratch), 1u << 20);
    if (arena == NULL)
        return none;
    ingest_ctx ictx = { scratch, arena };

    cgltf_options opts = {0};
    opts.memory.alloc_func = cg_alloc;
    opts.memory.free_func  = cg_free;
    opts.memory.user_data  = &ictx;
    opts.file.read         = cg_read;
    opts.file.release      = cg_release;
    opts.file.user_data    = &ictx;

    cgltf_data *data = NULL;
    if (cgltf_parse(&opts, anostr_bytes(&bytes), anostr_len(bytes), &data)
        != cgltf_result_success) {
        ano_log(ANO_ERROR, "res_graphics: glTF parse failed: %s", srcname);
        return none;
    }
    if (cgltf_load_buffers(&opts, data, srcname) != cgltf_result_success) {
        ano_log(ANO_ERROR, "res_graphics: glTF buffers failed: %s", srcname);
        return none;
    }
    // Accessor offsets/strides/counts against buffer sizes: without this, a hostile
    // file walks the conditioning loops out of bounds.
    if (cgltf_validate(data) != cgltf_result_success) {
        ano_log(ANO_ERROR, "res_graphics: glTF failed validation: %s", srcname);
        return none;
    }

    // Pass 1: counts. A primitive without positions or indices is dropped.
    uint64_t vtotal = 0, itotal = 0, ptotal = 0, ctotal = 0, rtotal = 0;
    for (size_t m = 0; m < data->meshes_count; m++) {
        for (size_t p = 0; p < data->meshes[m].primitives_count; p++) {
            const cgltf_primitive *prim = &data->meshes[m].primitives[p];
            const cgltf_accessor *pos = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++)
                if (prim->attributes[a].type == cgltf_attribute_type_position)
                    pos = prim->attributes[a].data;
            if (!pos || !prim->indices) {
                ano_log(ANO_WARN, "res_graphics: primitive without positions or "
                                  "indices dropped (%s)", srcname);
                continue;
            }
            vtotal += pos->count;
            itotal += prim->indices->count;
            ptotal += 1;
        }
    }
    for (size_t n = 0; n < data->nodes_count; n++) {
        ctotal += data->nodes[n].children_count;
        if (data->nodes[n].parent == NULL)
            rtotal += 1;
    }
    if (vtotal > UINT32_MAX || itotal > UINT32_MAX || ctotal > UINT32_MAX) {
        ano_log(ANO_ERROR, "res_graphics: scene exceeds 32-bit counts: %s", srcname);
        return none;
    }

    // Layout the block.
    scene_hdr hdr = {0};
    hdr.magic          = SCENE_MAGIC;
    hdr.version        = SCENE_VERSION;
    hdr.vertex_count   = (uint32_t)vtotal;
    hdr.index_count    = (uint32_t)itotal;
    hdr.prim_count     = (uint32_t)ptotal;
    hdr.mesh_count     = (uint32_t)data->meshes_count;
    hdr.node_count     = (uint32_t)data->nodes_count;
    hdr.child_count    = (uint32_t)ctotal;
    hdr.material_count = (uint32_t)data->materials_count;
    hdr.image_count    = (uint32_t)data->images_count;
    hdr.root_count     = (uint32_t)rtotal;
    uint64_t off = align16(sizeof hdr);
    hdr.off_vertices  = off; off = align16(off + vtotal * sizeof(anoresgfx_vertex));
    hdr.off_indices   = off; off = align16(off + itotal * sizeof(uint32_t));
    hdr.off_prims     = off; off = align16(off + ptotal * sizeof(anoresgfx_prim));
    hdr.off_meshes    = off; off = align16(off + hdr.mesh_count * sizeof(anoresgfx_mesh));
    hdr.off_nodes     = off; off = align16(off + hdr.node_count * sizeof(anoresgfx_node));
    hdr.off_children  = off; off = align16(off + ctotal * sizeof(uint32_t));
    hdr.off_materials = off; off = align16(off + hdr.material_count * sizeof(anoresgfx_material));
    hdr.off_images    = off; off = align16(off + hdr.image_count * sizeof(anoresgfx_image));
    hdr.off_roots     = off; off = align16(off + rtotal * sizeof(uint32_t));
    uint64_t total = off;
    if (total > SIZE_MAX - 1) {
        ano_log(ANO_ERROR, "res_graphics: scene block too large: %s", srcname);
        return none;
    }

    res_place_plan scene_plan = {
        .tag = RES_TAG_GFX_SCENE,
        .lifetime = lifetime,
        .role = RES_ROLE_DERIVED,
        .operation = RES_OP_ADOPT,
        .destination = RES_DEST_BULK,
        .provenance = RES_PROVENANCE_CONDITIONED,
        .alignment = ANO_CACHE_LINE,
    };
    res_owned_block scene_block = {0};
    if (res_owned_alloc(&scene_plan, (size_t)total, &scene_block) != 0) {
        ano_log(ANO_ERROR, "res_graphics: scene block allocation failed: %s", srcname);
        return none;
    }
    uint8_t *blk = scene_block.data;
    memset(blk, 0, (size_t)total + 1);          // deterministic padding + guard NUL
    memcpy(blk, &hdr, sizeof hdr);

    anoresgfx_vertex   *vertices  = (anoresgfx_vertex *)(blk + hdr.off_vertices);
    uint32_t           *indices   = (uint32_t *)(blk + hdr.off_indices);
    anoresgfx_prim     *prims     = (anoresgfx_prim *)(blk + hdr.off_prims);
    anoresgfx_mesh     *meshes    = (anoresgfx_mesh *)(blk + hdr.off_meshes);
    anoresgfx_node     *nodes     = (anoresgfx_node *)(blk + hdr.off_nodes);
    uint32_t           *children  = (uint32_t *)(blk + hdr.off_children);
    anoresgfx_material *materials = (anoresgfx_material *)(blk + hdr.off_materials);
    anoresgfx_image    *images    = (anoresgfx_image *)(blk + hdr.off_images);
    uint32_t           *roots     = (uint32_t *)(blk + hdr.off_roots);

    // Pass 2: geometry, conditioned to the engine vertex layout.
    uint32_t vcur = 0, icur = 0, pcur = 0;
    for (size_t m = 0; m < data->meshes_count; m++) {
        meshes[m].prim_first = pcur;
        for (size_t p = 0; p < data->meshes[m].primitives_count; p++) {
            const cgltf_primitive *prim = &data->meshes[m].primitives[p];
            const cgltf_accessor *pos = NULL, *nrm = NULL, *tex = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++) {
                if (prim->attributes[a].type == cgltf_attribute_type_position)
                    pos = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_normal)
                    nrm = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_texcoord)
                    tex = prim->attributes[a].data;
            }
            if (!pos || !prim->indices)
                continue;
            uint32_t vcount = (uint32_t)pos->count;
            uint32_t icount = (uint32_t)prim->indices->count;
            for (uint32_t v = 0; v < vcount; v++) {
                anoresgfx_vertex *dst = &vertices[vcur + v];
                cgltf_accessor_read_float(pos, v, dst->position, 3);
                if (nrm) {
                    cgltf_accessor_read_float(nrm, v, dst->normal, 3);
                } else {
                    dst->normal[0] = 0.0f;
                    dst->normal[1] = 1.0f;
                    dst->normal[2] = 0.0f;
                }
                if (tex)
                    cgltf_accessor_read_float(tex, v, dst->texcoord, 2);
            }
            for (uint32_t i = 0; i < icount; i++)
                indices[icur + i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
            prims[pcur] = (anoresgfx_prim){
                .vertex_first = vcur, .vertex_count = vcount,
                .index_first  = icur, .index_count  = icount,
                .material = prim->material
                          ? (int32_t)(prim->material - data->materials) : -1,
            };
            vcur += vcount;
            icur += icount;
            pcur += 1;
        }
        meshes[m].prim_count = pcur - meshes[m].prim_first;
    }

    // Nodes, children, roots.
    uint32_t ccur = 0, rcur = 0;
    for (size_t n = 0; n < data->nodes_count; n++) {
        const cgltf_node *cg = &data->nodes[n];
        anoresgfx_node *out = &nodes[n];
        if (cg->name)
            strncpy(out->name, cg->name, sizeof out->name - 1);
        cgltf_float matrix[16];
        cgltf_node_transform_local(cg, matrix);
        memcpy(out->local, matrix, sizeof matrix);
        out->mesh        = cg->mesh ? (int32_t)(cg->mesh - data->meshes) : -1;
        out->parent      = cg->parent ? (int32_t)(cg->parent - data->nodes) : -1;
        out->child_first = ccur;
        out->child_count = (uint32_t)cg->children_count;
        for (size_t c = 0; c < cg->children_count; c++)
            children[ccur++] = (uint32_t)(cg->children[c] - data->nodes);
        if (cg->parent == NULL)
            roots[rcur++] = (uint32_t)n;
    }

    // Materials and images.
    for (size_t m = 0; m < data->materials_count; m++)
        condition_material(data, &data->materials[m], &materials[m]);
    char dir[MAXPATH];
    size_t dlen = dir_of(srcname, dir);
    for (size_t im = 0; im < data->images_count; im++) {
        const cgltf_image *img = &data->images[im];
        if (img->uri == NULL || strncmp(img->uri, "data:", 5) == 0)
            continue;                           // non-URI images are not served in v1
        char joined[MAXPATH * 2];
        size_t ulen = strlen(img->uri);
        if (dlen + ulen >= sizeof joined)
            continue;
        memcpy(joined, dir, dlen);
        memcpy(joined + dlen, img->uri, ulen + 1);
        cgltf_decode_uri(joined + dlen);
        size_t jlen = path_collapse(joined);
        size_t vl;
        if (jlen == 0 || jlen >= MAXPATH || res_path_validate(joined, &vl) != 0) {
            ano_log(ANO_WARN, "res_graphics: image URI unusable, skipped: %s", img->uri);
            continue;
        }
        memcpy(images[im].path, joined, jlen + 1);
    }
    mark_srgb(data, images);

    size_t dep_cap = 1 + hdr.image_count;
    res_dependency_meta *deps = ano_mem_monotonic_alloc(arena,
                                                         dep_cap * sizeof *deps,
                                                         _Alignof(res_dependency_meta));
    size_t dep_count = 0;
    if (deps != NULL) {
        deps[dep_count++] = (res_dependency_meta){
            .rid = src.rid, .tag = RES_TAG_BYTES, .flags = 1,
        };
        for (uint32_t i = 0; i < hdr.image_count; i++) {
            size_t plen = strlen(images[i].path);
            if (plen == 0)
                continue;
            deps[dep_count++] = (res_dependency_meta){
                .rid = res_fnv1a64(images[i].path, plen),
                .tag = RES_TAG_IMAGE_ENC,
                .flags = images[i].srgb,
            };
        }
    }
    anores_t adopted = res_registry_adopt(key, &scene_block, deps, dep_count);
    if (adopted.gen == 0)
        res_owned_free(&scene_block, RES_FREE_RETAIL);
    return adopted;
}   // scratch heap dies here: cgltf data, buffers, and the arena wink out

// ---------------------------------------------------------------------------------------------
// Serving.

anoresgfx_scene ano_resgfx_scene(const ano_res_read *read, anores_t scene)
{
    anoresgfx_scene v = {0};
    anostr_t bytes = ano_res_bytes(read, scene);
    size_t len = anostr_len(bytes);
    if (len < sizeof(scene_hdr))
        return v;
    const uint8_t *blk = (const uint8_t *)anostr_bytes(&bytes);
    scene_hdr hdr;
    memcpy(&hdr, blk, sizeof hdr);
    if (hdr.magic != SCENE_MAGIC || hdr.version != SCENE_VERSION)
        return v;
    // Bounds: every array must land inside the block (bake files will come from disk,
    // and any loaded handle can be passed here). Overflow-proof form: offset within
    // len first, then count against the REMAINING bytes -- no u64 wrap can pass.
#define SCENE_ARR_OK(off, count, T) \
    ((off) <= len && (uint64_t)(count) <= ((uint64_t)len - (off)) / sizeof(T))
    if (!SCENE_ARR_OK(hdr.off_vertices, hdr.vertex_count, anoresgfx_vertex)
        || !SCENE_ARR_OK(hdr.off_indices, hdr.index_count, uint32_t)
        || !SCENE_ARR_OK(hdr.off_prims, hdr.prim_count, anoresgfx_prim)
        || !SCENE_ARR_OK(hdr.off_meshes, hdr.mesh_count, anoresgfx_mesh)
        || !SCENE_ARR_OK(hdr.off_nodes, hdr.node_count, anoresgfx_node)
        || !SCENE_ARR_OK(hdr.off_children, hdr.child_count, uint32_t)
        || !SCENE_ARR_OK(hdr.off_materials, hdr.material_count, anoresgfx_material)
        || !SCENE_ARR_OK(hdr.off_images, hdr.image_count, anoresgfx_image)
        || !SCENE_ARR_OK(hdr.off_roots, hdr.root_count, uint32_t))
        return v;
#undef SCENE_ARR_OK

    v.vertices       = (const anoresgfx_vertex *)(blk + hdr.off_vertices);
    v.vertex_count   = hdr.vertex_count;
    v.indices        = (const uint32_t *)(blk + hdr.off_indices);
    v.index_count    = hdr.index_count;
    v.prims          = (const anoresgfx_prim *)(blk + hdr.off_prims);
    v.prim_count     = hdr.prim_count;
    v.meshes         = (const anoresgfx_mesh *)(blk + hdr.off_meshes);
    v.mesh_count     = hdr.mesh_count;
    v.nodes          = (const anoresgfx_node *)(blk + hdr.off_nodes);
    v.node_count     = hdr.node_count;
    v.children       = (const uint32_t *)(blk + hdr.off_children);
    v.child_count    = hdr.child_count;
    v.materials      = (const anoresgfx_material *)(blk + hdr.off_materials);
    v.material_count = hdr.material_count;
    v.images         = (const anoresgfx_image *)(blk + hdr.off_images);
    v.image_count    = hdr.image_count;
    v.roots          = (const uint32_t *)(blk + hdr.off_roots);
    v.root_count     = hdr.root_count;
    return v;
}

anoresgfx_pixels ano_resgfx_image(ano_res_lifetime lifetime, const ano_res_read *read,
                                  anores_t src)
{
    anoresgfx_pixels px = {0};
    anostr_t bytes = ano_res_bytes(read, src);
    if (anostr_len(bytes) == 0 || anostr_len(bytes) > INT32_MAX) {
        ano_log(ANO_ERROR, "res_graphics: image decode on a stale/oversize handle");
        return px;
    }
    int w = 0, h = 0, ch = 0;
    // stb allocates through this TU's malloc, which is mimalloc via the override
    // header -- so the block is ano_aligned_free-able by the caller.
    uint8_t *rgba = stbi_load_from_memory((const stbi_uc *)anostr_bytes(&bytes),
                                          (int)anostr_len(bytes), &w, &h, &ch, 4);
    if (rgba == NULL || w <= 0 || h <= 0) {
        ano_log(ANO_ERROR, "res_graphics: image decode failed (%s)",
                stbi_failure_reason());
        if (rgba)
            stbi_image_free(rgba);
        return px;
    }
    size_t pixel_bytes = (size_t)w * (size_t)h * 4;
    res_place_plan plan = {
        .tag = RES_TAG_IMAGE_DEC,
        .lifetime = lifetime,
        .role = RES_ROLE_TRANSFER,
        .operation = RES_OP_DECODE,
        .destination = RES_DEST_EXTERNAL_TRANSFER,
        .provenance = RES_PROVENANCE_DECODED,
        .alignment = _Alignof(max_align_t),
    };
    // TODO(W6, M12): manager-owned pixels -- STBI_MALLOC/REALLOC/FREE route into the staging
    // arena, decode then copy ONCE into the planned home, and res_account_copy charges that
    // copy honestly. This external-allocation charge is never reversed, which is why
    // `allocations == frees at shutdown` cannot be an oracle yet.
    res_registry_external_allocation(&plan, pixel_bytes);
    px.rgba   = rgba;
    px.width  = (uint32_t)w;
    px.height = (uint32_t)h;
    return px;
}
