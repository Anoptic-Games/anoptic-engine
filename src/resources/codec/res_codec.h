/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Block compression for pack entries. Codec id is ONE TOC byte, stable.
// GDEFLATE byte is RESERVED. Entries compress in independent RES_CODEC_CHUNK chunks (496 KiB under RMOS_CHUNK_MAX).

#ifndef ANOPTIC_RES_CODEC_H
#define ANOPTIC_RES_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define RES_CODEC_CHUNK (496u * 1024u)

typedef enum res_codec_id {
    RES_CODEC_RAW      = 0,
    RES_CODEC_LZ4      = 1,
    RES_CODEC_ZSTD     = 2,   // built only with -DANOPTIC_ZSTD=ON
    RES_CODEC_GDEFLATE = 3,   // RESERVED. Never emitted. TOC naming it is REFUSED.
    RES_CODEC_COUNT,
} res_codec_id;

// true when this build can DECODE id. ZSTD needs ANOPTIC_ZSTD. GDEFLATE always false.
bool res_codec_available(res_codec_id id);

// Worst-case compressed size for src_len under id. 0 if unavailable.
size_t res_codec_bound(res_codec_id id, size_t src_len);

// Compress one chunk (src_len <= RES_CODEC_CHUNK). Output: bytes written, or 0 when not smaller (emit RAW).
size_t res_codec_encode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap);

// Decompress one chunk. dst_cap is the known uncompressed length. 0 on malformed input. Never UB.
size_t res_codec_decode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap);

#endif // ANOPTIC_RES_CODEC_H
