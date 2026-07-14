/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Block compression over vendored LZ4 (external/lz4, BSD-2-Clause; see ATTRIBUTIONS.md).
// ZSTD is a build option (ANOPTIC_ZSTD, default OFF) and GDEFLATE's codec byte is RESERVED:
// a TOC that names an unavailable codec is REFUSED, never guessed.
//
// Every entry point is total: hostile bytes yield 0, never UB. LZ4_decompress_safe is the
// safe decoder by name and by contract -- it never reads past src_len and never writes past
// dst_cap, which is what lets a packed chunk arrive from an untrusted file at all.

#include "res_codec.h"

#include <string.h>

#include <lz4.h>

#ifdef ANOPTIC_ZSTD
#include <zstd.h>
#endif

bool res_codec_available(res_codec_id id)
{
    switch (id) {
    case RES_CODEC_RAW:
    case RES_CODEC_LZ4:
        return true;
#ifdef ANOPTIC_ZSTD
    case RES_CODEC_ZSTD:
        return true;
#endif
    default:
        return false;                            // GDEFLATE is reserved, never available
    }
}

size_t res_codec_bound(res_codec_id id, size_t src_len)
{
    if (src_len == 0 || src_len > RES_CODEC_CHUNK)
        return 0;
    switch (id) {
    case RES_CODEC_RAW:
        return src_len;
    case RES_CODEC_LZ4: {
        int n = LZ4_compressBound((int)src_len);
        return n > 0 ? (size_t)n : 0;
    }
#ifdef ANOPTIC_ZSTD
    case RES_CODEC_ZSTD:
        return ZSTD_compressBound(src_len);
#endif
    default:
        return 0;
    }
}

// Output: bytes written, or 0 when the result would not be SMALLER than the input (the
// builder then emits RAW -- an incompressible chunk must never cost more on disk).
size_t res_codec_encode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap)
{
    if (src == NULL || dst == NULL || src_len == 0 || src_len > RES_CODEC_CHUNK)
        return 0;
    switch (id) {
    case RES_CODEC_RAW:
        if (dst_cap < src_len)
            return 0;
        memcpy(dst, src, src_len);
        return src_len;
    case RES_CODEC_LZ4: {
        if (dst_cap > (size_t)0x7fffffff)
            dst_cap = 0x7fffffff;
        int n = LZ4_compress_default(src, dst, (int)src_len, (int)dst_cap);
        return n > 0 && (size_t)n < src_len ? (size_t)n : 0;
    }
#ifdef ANOPTIC_ZSTD
    case RES_CODEC_ZSTD: {
        size_t n = ZSTD_compress(dst, dst_cap, src, src_len, 9);
        if (ZSTD_isError(n) || n >= src_len)
            return 0;
        return n;
    }
#endif
    default:
        return 0;
    }
}

// dst_cap is the chunk's KNOWN uncompressed length (from the TOC), never a guess.
// Output: bytes written, or 0 on ANY malformed input.
size_t res_codec_decode(res_codec_id id, const void *src, size_t src_len,
                        void *dst, size_t dst_cap)
{
    if (src == NULL || dst == NULL || src_len == 0 || dst_cap == 0
        || dst_cap > RES_CODEC_CHUNK)
        return 0;
    switch (id) {
    case RES_CODEC_RAW:
        if (src_len != dst_cap)
            return 0;                            // RAW must be exactly its uncompressed length
        memcpy(dst, src, src_len);
        return src_len;
    case RES_CODEC_LZ4: {
        if (src_len > (size_t)0x7fffffff)
            return 0;
        // dst_cap is the TOC-known uncompressed length: a chunk that decodes SHORT is as
        // malformed as one that would overrun. LZ4_decompress_safe already refuses the
        // overrun (never writes past dst_cap); requiring n == dst_cap refuses the short lie.
        int n = LZ4_decompress_safe(src, dst, (int)src_len, (int)dst_cap);
        return n > 0 && (size_t)n == dst_cap ? (size_t)n : 0;
    }
#ifdef ANOPTIC_ZSTD
    case RES_CODEC_ZSTD: {
        size_t n = ZSTD_decompress(dst, dst_cap, src, src_len);
        return !ZSTD_isError(n) && n == dst_cap ? n : 0;
    }
#endif
    default:
        return 0;                                // an unavailable codec is a REFUSAL
    }
}
