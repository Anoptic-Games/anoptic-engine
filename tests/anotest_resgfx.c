/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_res_graphics.h: glTF ingest through the namespace and RGBA
 * decode, against handcrafted ground truth.
 *   - a hand-written .gltf + .bin pair staged under the base mount: every vertex,
 *     index, node, transform, material factor, and feature bit is asserted against
 *     the JSON written right here;
 *   - URI plumbing: sibling .bin via the glTF's own logical directory, percent-encoded
 *     image URI decoded and grafted onto the source directory, sRGB slot aggregation;
 *   - a base64 data: URI variant (exercises the arena-backed cgltf memory path);
 *   - single-copy: repeat ingest returns the same scene handle; stale source refuses;
 *   - image decode: an 8x8 RGBA pattern round-trips PNG -> stb decode byte-exactly
 *     (stb_image_write here is the independent encoder oracle);
 *   - optional real-asset smoke (ANO_TEST_ASSETS staged by CMake when present).
 * Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_res_graphics.h"
#include "anoptic_log.h"
#include "templates/scratch.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image_write.h>
#pragma GCC diagnostic pop

static int failures = 0;
static ano_res_lifetime g_lifetime;
static ano_res_reader g_reader = { .lane = ANO_RES_READER_NONE };
static ano_res_read g_read;
#define ano_res_get(path) ano_res_get(g_lifetime, (path))
#define ano_res_unload(handle) ano_res_unload(g_lifetime, (handle))
#define ano_resgfx_model(handle) ano_resgfx_model(g_lifetime, &g_read, (handle))
#define ano_resgfx_scene(handle) ano_resgfx_scene(&g_read, (handle))
#define ano_resgfx_image(handle) ano_resgfx_image(g_lifetime, &g_read, (handle))
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define CHECKF(a, b, msg) CHECK((a) > (b) - 1e-6f && (a) < (b) + 1e-6f, msg)

/* Ground truth */

// One triangle, two nodes, one textured material.

static const float TRI_POS[9] = { 0, 0, 0,   1, 0, 0,   0, 1, 0 };
static const float TRI_NRM[9] = { 0, 0, 1,   0, 0, 1,   0, 0, 1 };
static const float TRI_UV[6]  = { 0, 0,      1, 0,      0, 1 };
static const uint16_t TRI_IDX[3] = { 0, 1, 2 };

// bin layout: pos @0 (36), nrm @36 (36), uv @72 (24), idx @96 (6). 102 bytes.
static size_t build_bin(uint8_t *out)
{
    memcpy(out, TRI_POS, 36);
    memcpy(out + 36, TRI_NRM, 36);
    memcpy(out + 72, TRI_UV, 24);
    memcpy(out + 96, TRI_IDX, 6);
    return 102;
}

static const char *GLTF_JSON_FMT =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{%s\"byteLength\":102}],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},"
    "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
    "\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
    "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
    "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
    "\"materials\":[{\"name\":\"matA\",\"pbrMetallicRoughness\":{"
    "\"baseColorTexture\":{\"index\":0},\"baseColorFactor\":[0.5,0.6,0.7,1.0],"
    "\"metallicFactor\":0.25,\"roughnessFactor\":0.75},"
    "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.4,\"doubleSided\":true,"
    "\"emissiveFactor\":[0.1,0.2,0.3]}],"
    "\"textures\":[{\"source\":0}],"
    "\"images\":[{\"uri\":\"tex/checker%%20map.png\"}],"
    "\"nodes\":[{\"name\":\"root\",\"children\":[1],\"translation\":[1,2,3]},"
    "{\"name\":\"child\",\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = len == 0 || fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static size_t base64(const uint8_t *in, size_t n, char *out)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = tbl[v >> 18 & 63];
        out[o++] = tbl[v >> 12 & 63];
        out[o++] = i + 1 < n ? tbl[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

<<<<<<< HEAD
// Ground truth for one ingested scene. Shape checks gate only the DEREFERENCES that follow on THIS scene.
// Unusable shape is a LOUD failure, never a quiet skip.
=======
// ---------------------------------------------------------------------------------------------

// Ground truth for one ingested scene. The shape checks below gate only the DEREFERENCES that
// follow them, and only on THIS scene: keying that gate on the global `failures` (as this did)
// silently skipped every per-field assertion once any unrelated earlier check had failed, so a
// single staging typo could hide every wrong vertex in the file. An unusable shape is now a
// LOUD failure, never a quiet skip.
>>>>>>> block-b1-base
static void check_scene(anoresgfx_scene s, const char *variant)
{
    printf("  checking scene variant: %s\n", variant);
    int shape = failures;
    CHECK(s.vertex_count == 3 && s.index_count == 3, "counts: geometry");
    CHECK(s.prim_count == 1 && s.mesh_count == 1, "counts: prims/meshes");
    CHECK(s.node_count == 2 && s.child_count == 1 && s.root_count == 1, "counts: nodes");
    CHECK(s.material_count == 1 && s.image_count == 1, "counts: materials/images");

    bool derefable = failures == shape && s.vertices && s.indices && s.prims && s.meshes
                     && s.nodes && s.children && s.roots && s.materials && s.images;
    if (!derefable) {
        printf("FAIL: scene shape unusable, ground truth NOT checked (%s) (%s:%d)\n",
               variant, __FILE__, __LINE__);
        failures++;
        return;
    }

    for (int v = 0; v < 3; v++) {
        CHECK(memcmp(s.vertices[v].position, TRI_POS + v * 3, 12) == 0, "vertex position");
        CHECK(memcmp(s.vertices[v].normal, TRI_NRM + v * 3, 12) == 0, "vertex normal");
        CHECK(memcmp(s.vertices[v].texcoord, TRI_UV + v * 2, 8) == 0, "vertex uv");
    }
    CHECK(s.indices[0] == 0 && s.indices[1] == 1 && s.indices[2] == 2, "indices");
    CHECK(s.prims[0].vertex_first == 0 && s.prims[0].vertex_count == 3
          && s.prims[0].index_first == 0 && s.prims[0].index_count == 3, "prim ranges");
    CHECK(s.prims[0].material == 0, "prim material");
    CHECK(s.meshes[0].prim_first == 0 && s.meshes[0].prim_count == 1, "mesh prim range");

    CHECK(strcmp(s.nodes[0].name, "root") == 0, "node0 name");
    CHECK(s.nodes[0].parent == -1 && s.nodes[0].mesh == -1, "node0 shape");
    CHECK(s.nodes[0].child_count == 1 && s.children[s.nodes[0].child_first] == 1,
          "node0 child");
    // glTF column-major float16: translation sits at elements 12..14.
    const float *m = &s.nodes[0].local[0][0];
    CHECKF(m[12], 1.0f, "translation x");
    CHECKF(m[13], 2.0f, "translation y");
    CHECKF(m[14], 3.0f, "translation z");
    CHECK(strcmp(s.nodes[1].name, "child") == 0, "node1 name");
    CHECK(s.nodes[1].parent == 0 && s.nodes[1].mesh == 0, "node1 shape");
    CHECK(s.roots[0] == 0, "root list");

    const anoresgfx_material *mat = &s.materials[0];
    CHECK(strcmp(mat->name, "matA") == 0, "material name");
    uint32_t want = ANORESGFX_PBR_BASE_COLOR_FACTOR | ANORESGFX_PBR_BASE_COLOR_TEXTURE
                  | ANORESGFX_PBR_METALLIC_ROUGHNESS_FACTOR
                  | ANORESGFX_PBR_ALPHA_MODE_MASK | ANORESGFX_PBR_DOUBLE_SIDED
                  | ANORESGFX_PBR_EMISSIVE_FACTOR;
    CHECK(mat->features == want, "material feature bits");
    CHECKF(mat->base_color_factor[0], 0.5f, "base color r");
    CHECKF(mat->base_color_factor[1], 0.6f, "base color g");
    CHECKF(mat->base_color_factor[2], 0.7f, "base color b");
    CHECKF(mat->metallic_factor, 0.25f, "metallic");
    CHECKF(mat->roughness_factor, 0.75f, "roughness");
    CHECK(mat->base_color.image == 0, "base color texref");
    CHECK(mat->metallic_roughness.image == -1, "absent texref is -1");
    CHECK(mat->alpha_mode == 1, "alpha mode mask");
    CHECKF(mat->alpha_cutoff, 0.4f, "alpha cutoff");
    CHECK(mat->double_sided == 1, "double sided");
    CHECKF(mat->emissive_factor[0], 0.1f, "emissive r");
    CHECKF(mat->emissive_factor[2], 0.3f, "emissive b");

    CHECK(strcmp(s.images[0].path, "anotest_res/gfx/tex/checker map.png") == 0,
          "image URI decoded and grafted onto the source dir");
    CHECK(s.images[0].srgb == 1, "base-color slot marks sRGB");
}

static void test_model_ingest(void)
{
    uint8_t bin[128];
    size_t binLen = build_bin(bin);
    char json[4096];
    snprintf(json, sizeof json, GLTF_JSON_FMT, "\"uri\":\"tri.bin\",");

    scratch_make_dir("resources");
    scratch_make_dir("resources/anotest_res");
    scratch_make_dir("resources/anotest_res/gfx");
    CHECK(write_file("resources/anotest_res/gfx/tri.gltf", json, strlen(json)), "stage gltf");
    CHECK(write_file("resources/anotest_res/gfx/tri.bin", bin, binLen), "stage bin");

    anores_t src = ano_res_get("anotest_res/gfx/tri.gltf");
    CHECK(src.gen != 0, "source handle");
    anores_t scene = ano_resgfx_model(src);
    CHECK(scene.gen != 0, "ingest yields a scene handle");
    CHECK(scene.rid == ANOSTR_SID("anotest_res/gfx/tri.gltf#gfx"),
          "scene key rid in the SID space");
    check_scene(ano_resgfx_scene(scene), "external .bin");

    // Single-copy: same source, same scene.
    anores_t again = ano_resgfx_model(src);
    CHECK(again.rid == scene.rid && again.slot == scene.slot && again.gen == scene.gen,
          "repeat ingest is the same handle");

    // Stale source refuses.
    CHECK(ano_res_unload(src) == 0, "unload raw gltf");
    anores_t stale = ano_resgfx_model(src);
    CHECK(stale.gen == 0, "stale source ingest is the sentinel");
    // The conditioned scene survives its source.
    CHECK(ano_resgfx_scene(scene).vertex_count == 3, "scene outlives the raw bytes");

    // Sentinel/stale scene handles serve zeroed views.
    anoresgfx_scene z = ano_resgfx_scene((anores_t){0});
    CHECK(z.vertex_count == 0 && z.vertices == NULL, "sentinel scene view is zeroed");
    CHECK(ano_res_unload(scene) == 0, "unload scene");
    z = ano_resgfx_scene(scene);
    CHECK(z.vertex_count == 0, "stale scene view is zeroed");

    // data: URI variant. Whole buffer rides base64 through the arena.
    char b64[256];
    base64(bin, binLen, b64);
    char uri[512];
    snprintf(uri, sizeof uri, "\"uri\":\"data:application/octet-stream;base64,%s\",", b64);
    snprintf(json, sizeof json, GLTF_JSON_FMT, uri);
    CHECK(write_file("resources/anotest_res/gfx/tri_embed.gltf", json, strlen(json)),
          "stage embedded gltf");
    anores_t esrc = ano_res_get("anotest_res/gfx/tri_embed.gltf");
    anores_t escene = ano_resgfx_model(esrc);
    CHECK(escene.gen != 0, "embedded ingest yields a scene");
    // Note: the image path check inside expects the tri_embed dir, same as tri's.
    anoresgfx_scene es = ano_resgfx_scene(escene);
    CHECK(es.vertex_count == 3 && es.index_count == 3, "embedded geometry counts");
    if (es.vertex_count == 3)
        CHECK(memcmp(es.vertices[2].position, TRI_POS + 6, 12) == 0, "embedded vertex data");
    ano_res_unload(escene);
    ano_res_unload(esrc);
}

static void test_image_decode(void)
{
    // 8x8 RGBA pattern, PNG-encoded by stb_image_write (independent of decode under test). Must round-trip byte-exactly.
    uint8_t pixels[8 * 8 * 4];
    for (int i = 0; i < 8 * 8; i++) {
        pixels[i * 4 + 0] = (uint8_t)(i * 3);
        pixels[i * 4 + 1] = (uint8_t)(255 - i);
        pixels[i * 4 + 2] = (uint8_t)(i * 7 + 11);
        pixels[i * 4 + 3] = (uint8_t)(i % 5 == 0 ? 200 : 255);
    }
    int pngLen = 0;
    unsigned char *png = stbi_write_png_to_mem(pixels, 8 * 4, 8, 8, 4, &pngLen);
    CHECK(png != NULL && pngLen > 0, "png encode (oracle)");
    if (!png)
        return;
    CHECK(write_file("resources/anotest_res/gfx/pattern.png", png, (size_t)pngLen),
          "stage png");
    free(png);      // stbi_write allocates with this TU's malloc

    anores_t img = ano_res_get("anotest_res/gfx/pattern.png");
    CHECK(img.gen != 0, "png handle");
    anoresgfx_pixels px = ano_resgfx_image(img);
    CHECK(px.rgba != NULL, "decode succeeds");
    CHECK(px.width == 8 && px.height == 8, "decode dimensions");
    if (px.rgba)
        CHECK(memcmp(px.rgba, pixels, sizeof pixels) == 0, "decode round-trips byte-exact");
    ano_aligned_free(px.rgba);
    ano_res_unload(img);

    // Garbage bytes refuse politely.
    CHECK(write_file("resources/anotest_res/gfx/junk.png", "notapng", 7), "stage junk");
    anores_t junk = ano_res_get("anotest_res/gfx/junk.png");
    anoresgfx_pixels jpx = ano_resgfx_image(junk);
    CHECK(jpx.rgba == NULL && jpx.width == 0, "garbage decode is zeroed");
    ano_res_unload(junk);
    anoresgfx_pixels spx = ano_resgfx_image((anores_t){0});
    CHECK(spx.rgba == NULL, "sentinel decode is zeroed");
}

static void test_real_assets(void)
{
#ifdef ANO_TEST_ASSETS
    // Best effort: the source-tree assets dir exists on the build machine only.
    FILE *probe = fopen(ANO_TEST_ASSETS "/viking_room.gltf", "rb");
    if (!probe) {
        printf("  (real-asset smoke skipped: %s not present)\n", ANO_TEST_ASSETS);
        return;
    }
    fclose(probe);
    anores_t src = ano_res_get("assets_real/viking_room.gltf");
    CHECK(src.gen != 0, "viking_room loads through the graft mount");
    anores_t scene = ano_resgfx_model(src);
    CHECK(scene.gen != 0, "viking_room ingests");
    anoresgfx_scene s = ano_resgfx_scene(scene);
    CHECK(s.vertex_count > 1000 && s.index_count > 1000, "viking_room geometry present");
    CHECK(s.mesh_count >= 1 && s.node_count >= 1 && s.root_count >= 1, "viking_room graph");
    CHECK(s.image_count >= 1 && s.images[0].path[0] != '\0', "viking_room image path");
    printf("  viking_room: %u verts, %u indices, %u nodes, %u images\n",
           s.vertex_count, s.index_count, s.node_count, s.image_count);
    // Its texture decodes through the same namespace.
    if (s.image_count >= 1 && s.images[0].path[0]) {
        anores_t tex = ano_res_get(s.images[0].path);
        CHECK(tex.gen != 0, "viking_room texture loads by conditioned path");
        anoresgfx_pixels px = ano_resgfx_image(tex);
        CHECK(px.rgba != NULL && px.width > 0, "viking_room texture decodes");
        ano_aligned_free(px.rgba);
        ano_res_unload(tex);
    }
    ano_res_unload(scene);
    ano_res_unload(src);
#else
    printf("  (real-asset smoke not compiled in)\n");
#endif
}

static void cleanup(void)
{
    remove("resources/anotest_res/gfx/tri.gltf");
    remove("resources/anotest_res/gfx/tri.bin");
    remove("resources/anotest_res/gfx/tri_embed.gltf");
    remove("resources/anotest_res/gfx/pattern.png");
    remove("resources/anotest_res/gfx/junk.png");
    scratch_remove_dir("resources/anotest_res/gfx");
    scratch_remove_dir("resources/anotest_res");
}

int main(void)
{
    scratch_anchor_to_exe();
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)logAlive;

    CHECK(ano_res_init() == 0, "ano_res_init");
    g_lifetime = ano_res_lifetime_engine();
#ifdef ANO_TEST_ASSETS
    {
        ano_fspath assets = {0};
        int w = snprintf(assets.str, sizeof assets.str, "%s", ANO_TEST_ASSETS);
        if (w > 0 && w < (int)sizeof assets.str) {
            assets.length = (uint16_t)w;
            (void)ano_res_mount("assets_real/", assets);
        }
    }
#endif
    CHECK(ano_res_reader_register(&g_reader) == 0, "reader register");
    CHECK(ano_res_read_begin(&g_reader, &g_read) == 0, "read begin");

    test_model_ingest();
    test_image_decode();
    test_real_assets();
    cleanup();
    ano_res_read_end(&g_read);
    CHECK(ano_res_reader_unregister(&g_reader) == 0, "reader unregister");
    (void)ano_res_collect();
    CHECK(ano_res_shutdown() == 0, "resource shutdown");

    if (failures == 0) { printf("anotest_resgfx: all checks passed\n"); return 0; }
    printf("anotest_resgfx: %d check(s) failed\n", failures);
    return 1;
}
