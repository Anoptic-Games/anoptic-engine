/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anopak: build and inspect an anopak archive. STUB.
//
// TODO(W4, M14): `anopak build <src_dir> <out.anopak> [--codec raw|lz4|zstd]` and
// `anopak list <pack>`. The builder is DETERMINISTIC -- the same input tree produces a
// byte-identical pack, which is what makes a pack diffable, cacheable, and reproducible in
// CI. It shares res_pack_build with the engine; there is no second format writer.

#include <stdio.h>
#include <string.h>

#include <anoptic_res_pack.h>
#include <anoptic_resources.h>

static int usage(void)
{
    fprintf(stderr,
            "anopak -- the Anoptic archive builder\n"
            "  anopak build <src_dir> <out.anopak>   TODO(W4, M14)\n"
            "  anopak list  <pack>                   TODO(W4, M14)\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    if (strcmp(argv[1], "build") == 0) {
        if (argc != 4)
            return usage();
        int rc = ano_res_pack_build(argv[2], argv[3]);
        if (rc != 0) {
            fprintf(stderr, "anopak: build unimplemented (W4, M14)\n");
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        fprintf(stderr, "anopak: list unimplemented (W4, M14)\n");
        return 1;
    }
    return usage();
}
