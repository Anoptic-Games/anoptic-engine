/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anopak: build and inspect an anopak archive.
//
//   anopak build <src_dir> <out.anopak> [--codec raw|lz4]
//   anopak list  <pack>
//
// The builder is DETERMINISTIC -- the same input tree produces a byte-identical pack, which
// is what makes a pack diffable, cacheable, and reproducible in CI. It shares res_pack_build
// with the engine; there is no second format writer. `list` is a read-only inspector over
// the PUBLIC format header (anoptic_res_pack.h) and deliberately uses plain stdio: it must
// be able to interrogate a pack even when the engine refuses it.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_res_pack.h>
#include <anoptic_resources.h>

#include "resources/codec/res_codec.h"
#include "resources/pack/res_pack.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

static int usage(void)
{
    fprintf(stderr,
            "anopak -- the Anoptic archive builder\n"
            "  anopak build <src_dir> <out.anopak> [--codec raw|lz4]\n"
            "  anopak list  <pack>\n");
    return 2;
}

static const char *codec_name(uint8_t c)
{
    switch (c) {
    case RES_CODEC_RAW:      return "raw";
    case RES_CODEC_LZ4:      return "lz4";
    case RES_CODEC_ZSTD:     return "zstd";
    case RES_CODEC_GDEFLATE: return "gdeflate(RESERVED)";
    default:                 return "?";
    }
}

static int cmd_list(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "anopak: cannot open %s\n", path);
        return 1;
    }
    uint8_t hb[32];
    if (fread(hb, 1, sizeof hb, f) != sizeof hb) {
        fprintf(stderr, "anopak: truncated header\n");
        fclose(f);
        return 1;
    }
    uint32_t magic = rd32(hb);
    uint32_t count = rd32(hb + 8);
    uint64_t toc_off = rd64(hb + 16);
    printf("%s: magic %08x version %u codec %s flags %02x entries %u toc_off %" PRIu64 "\n",
           path, magic, rd16(hb + 4), codec_name(hb[6]), hb[7], count, toc_off);
    if (magic != ANO_PACK_MAGIC) {
        fprintf(stderr, "anopak: bad magic\n");
        fclose(f);
        return 1;
    }
    if (fseek(f, (long)toc_off, SEEK_SET) != 0) {
        fclose(f);
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t te[48];
        if (fread(te, 1, sizeof te, f) != sizeof te) {
            fprintf(stderr, "anopak: truncated TOC at entry %u\n", i);
            fclose(f);
            return 1;
        }
        uint32_t tag = rd32(te + 40);
        printf("  rid %016" PRIx64 " rid2 %016" PRIx64 " tag %c%c%c%c codec %-4s"
               " raw %10" PRIu64 " stored %10" PRIu64 " off %10" PRIu64 "%s\n",
               rd64(te), rd64(te + 8),
               (char)(tag & 0xff), (char)((tag >> 8) & 0xff),
               (char)((tag >> 16) & 0xff), (char)((tag >> 24) & 0xff),
               codec_name(te[44]), rd64(te + 24), rd64(te + 32), rd64(te + 16),
               (te[45] & ANO_PACK_ENTRY_BLOCK) ? " [block]" : "");
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    if (strcmp(argv[1], "build") == 0) {
        if (argc != 4 && argc != 6)
            return usage();
        uint8_t codec = RES_CODEC_LZ4;
        if (argc == 6) {
            if (strcmp(argv[4], "--codec") != 0)
                return usage();
            if (strcmp(argv[5], "raw") == 0)
                codec = RES_CODEC_RAW;
            else if (strcmp(argv[5], "lz4") == 0)
                codec = RES_CODEC_LZ4;
            else
                return usage();
        }
        if (res_pack_build(argv[2], argv[3], codec) != 0) {
            fprintf(stderr, "anopak: build failed\n");
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        if (argc != 3)
            return usage();
        return cmd_list(argv[2]);
    }
    return usage();
}
