/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anopak archive format. 32-byte header, 48-byte TOC entry, LE, fixed.
// Disk identity is {rid, rid2} plus kind FOURCC. Dense kind id never reaches disk. TOC sorted by rid (bsearch).
// Mount costs header+TOC only. Ranged read decodes only touched chunks.

#ifndef ANOPTICENGINE_ANOPTIC_RES_PACK_H
#define ANOPTICENGINE_ANOPTIC_RES_PACK_H

#include <stddef.h>
#include <stdint.h>

#define ANO_PACK_MAGIC   0x4B41504Fu   // 'OPAK' little-endian
#define ANO_PACK_VERSION 1u
#define ANO_PACK_MAX     8             // mounted packs

/* Header flags */

enum {
    ANO_PACK_FLAG_SORTED = 1u << 0,    // TOC sorted ascending by rid (always set, refused otherwise)
};

/* Entry flags */

enum {
    ANO_PACK_ENTRY_BLOCK = 1u << 0,    // raw bytes are a res_block_hdr plane-set block
};

/* Header */

// 32 bytes. header_hash is FNV-1a-64 over the first 24 bytes.
typedef struct ano_pack_header {
    uint32_t magic;          // ANO_PACK_MAGIC
    uint16_t version;        // ANO_PACK_VERSION
    uint8_t  codec;          // default codec for every entry (res_codec_id)
    uint8_t  flags;          // ANO_PACK_FLAG_*
    uint32_t entry_count;
    uint32_t reserved;
    uint64_t toc_off;        // absolute file offset of entry[0]
    uint64_t header_hash;
} ano_pack_header;
static_assert(sizeof(ano_pack_header) == 32, "the anopak header is 32 bytes, forever");

/* TOC entry */

// 48 bytes.
typedef struct ano_pack_entry {
    uint64_t rid;            // FNV-1a-64 basis A over the logical path
    uint64_t rid2;           // basis B. {rid, rid2} is 128-bit identity. mismatch is REFUSED.
    uint64_t data_off;       // absolute file offset of the entry's chunk index
    uint64_t raw_size;       // uncompressed bytes
    uint64_t stored_size;    // bytes on disk (chunk index + every stored chunk)
    uint32_t tag;            // FOURCC kind
    uint8_t  codec;          // res_codec_id for this entry
    uint8_t  flags;          // ANO_PACK_ENTRY_*
    uint16_t reserved;
} ano_pack_entry;
static_assert(sizeof(ano_pack_entry) == 48, "the anopak TOC entry is 48 bytes, forever");

#endif // ANOPTICENGINE_ANOPTIC_RES_PACK_H
