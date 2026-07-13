/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// FROZEN SEAM (freeze item 9). Block compression for pack entries. The codec id is ONE
// byte in the TOC and is stable forever; GDEFLATE's byte is RESERVED now so adding it
// later is not a format break.
//
// A pack entry is compressed in independent CHUNKS of RES_CODEC_CHUNK uncompressed bytes,
// so a ranged read decodes only the chunks it touches and never the whole entry. 496 KiB
// sits just under RMOS_CHUNK_MAX (512 KiB) with room for the worst-case incompressible
// expansion, so one chunk always fits one streaming chunk block.

#ifndef ANOPTIC_RES_CODEC_H
#define ANOPTIC_RES_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define RES_CODEC_CHUNK (496u * 1024u)

typedef enum res_codec_id {
    RES_CODEC_RAW      = 0,
    RES_CODEC_LZ4      = 1,
    RES_CODEC_ZSTD     = 2,   // built only with -DANOPTIC_ZSTD=ON
    RES_CODEC_GDEFLATE = 3,   // RESERVED. Never emitted; a TOC naming it is REFUSED, not guessed.
    RES_CODEC_COUNT,
} res_codec_id;

// true when this build can DECODE id. RES_CODEC_ZSTD is false unless ANOPTIC_ZSTD is ON;
// RES_CODEC_GDEFLATE is always false.
bool res_codec_available(res_codec_id id);

// Worst-case compressed size for src_len uncompressed bytes under id. 0 if unavailable.
size_t res_codec_bound(res_codec_id id, size_t src_len);

// Compress exactly one chunk (src_len <= RES_CODEC_CHUNK). Output: bytes written, or
// 0 when the result would not be smaller than the input (the builder then emits RAW).
size_t res_codec_encode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap);

// Decompress exactly one chunk. dst_cap is the chunk's KNOWN uncompressed length (from the
// TOC), never a guess. Output: bytes written, or 0 on any malformed input -- hostile bytes
// are refused, never trusted. Never UB.
size_t res_codec_decode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap);

#endif // ANOPTIC_RES_CODEC_H
