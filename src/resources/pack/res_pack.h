/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Pack runtime. On-disk format is public (anoptic_res_pack.h).
// Pack mount is PREFIX + open file + TOC in manager memory. Walk pass 2 not wired yet.
// Never hands out a pointer into a mapped file.

#ifndef ANOPTIC_RES_PACK_H
#define ANOPTIC_RES_PACK_H

#include <stddef.h>
#include <stdint.h>

#include <anoptic_res_pack.h>

#include "../resources_internal.h"
#include "../resources_os.h"

// One mounted pack. Opaque to everything but pack/.
struct res_pack {
    anostr_t              prefix;     // canonical, "" or "seg/.../"
    ano_fspath            file;
    ano_pack_header       hdr;
    const ano_pack_entry *toc;        // manager-owned, entry_count entries, sorted by rid
    res_owned_block       toc_block;
};

int  res_pack_mount(const char *prefix, ano_fspath file);
void res_pack_unmount_all(void);

int  res_pack_count(void);
const res_pack *res_pack_at(int i);

// Locate `logical` inside pack. Output: TOC entry index, or -1. rid2 mismatch REFUSED.
int res_pack_find(const res_pack *pack, const char *logical, size_t len);

// Read entry whole into the sink, decoding chunk by chunk. 0 / -1.
int res_pack_read_sink(const res_pack *pack, uint32_t entry, const res_sink *sink,
                       size_t *out_size);

// Read [off, off+len) of entry. 0 / RES_RANGE_EOF / -1. Never a silent partial.
int res_pack_read_range(const res_pack *pack, uint32_t entry, uint64_t off, size_t len,
                        void *dst);

// Deterministic builder: same input tree -> byte-identical pack. 0 / -1.
int res_pack_build(const char *src_dir, const char *out_pack, uint8_t codec);

#endif // ANOPTIC_RES_PACK_H
