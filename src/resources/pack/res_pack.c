/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The anopak runtime. STUB.
//
// TODO(W4, M14): mount (header + TOC read, header_hash checked, ANO_PACK_FLAG_SORTED
// REQUIRED), bsearch lookup by rid with an rid2 refusal, chunked reads through the codec,
// ranged reads that decode only the chunks they touch, and the DETERMINISTIC builder (same
// input tree -> byte-identical pack).
//
// The oracle that decides this landed: res_gfx parse_count must read 0 after loading a
// BAKED scene. Prose is not evidence.

#include "res_pack.h"

#include <string.h>

#include "../codec/res_codec.h"

int res_pack_mount(const char *prefix, ano_fspath file)
{
    (void)prefix; (void)file;
    return -1;                                  // TODO(W4, M14)
}

void res_pack_unmount_all(void)
{
                                                // TODO(W4, M14)
}

int res_pack_count(void)
{
    return 0;                                   // TODO(W4, M14)
}

const res_pack *res_pack_at(int i)
{
    (void)i;
    return NULL;                                // TODO(W4, M14)
}

int res_pack_find(const res_pack *pack, const char *logical, size_t len)
{
    (void)pack; (void)logical; (void)len;
    return -1;                                  // TODO(W4, M14)
}

int res_pack_read_sink(const res_pack *pack, uint32_t entry, const res_sink *sink,
                       size_t *out_size)
{
    (void)pack; (void)entry; (void)sink; (void)out_size;
    return -1;                                  // TODO(W4, M14)
}

int res_pack_read_range(const res_pack *pack, uint32_t entry, uint64_t off, size_t len,
                        void *dst)
{
    (void)pack; (void)entry; (void)off; (void)len; (void)dst;
    return -1;                                  // TODO(W4, M14)
}

int res_pack_build(const char *src_dir, const char *out_pack, uint8_t codec)
{
    (void)src_dir; (void)out_pack; (void)codec;
    return -1;                                  // TODO(W4, M14)
}

int ano_res_mount_pack(const char *prefix, ano_fspath pack_file)
{
    return res_pack_mount(prefix, pack_file);
}

int ano_res_pack_build(const char *src_dir, const char *out_pack)
{
    return res_pack_build(src_dir, out_pack, RES_CODEC_LZ4);
}
