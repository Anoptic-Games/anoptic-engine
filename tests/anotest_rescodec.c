/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for src/resources/codec/res_codec.h: the RAW and LZ4 chunk codecs.
 *   - round-trip: compressible, incompressible, and single-byte chunks;
 *   - the "not smaller" contract: an incompressible chunk encodes to 0, so the builder
 *     emits RAW rather than paying to store noise;
 *   - availability: ZSTD is off in a default build and GDEFLATE's byte is RESERVED --
 *     both must report unavailable and REFUSE to decode, never guess;
 *   - totality: hostile bytes (truncated, garbage, a lying length) yield 0, never UB.
 *     That is the only reason a packed chunk may arrive from an untrusted file at all.
 * Exit 0 == pass.
 *
 * TODO(W4, M14): the zstd arm (under -DANOPTIC_ZSTD=ON), the chunk-index matrix, and the
 * TOC corruption battery. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "codec/res_codec.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define N 8192

static void roundtrip(res_codec_id id, const uint8_t *src, size_t len, const char *what)
{
    static uint8_t enc[RES_CODEC_CHUNK + 4096];
    static uint8_t dec[RES_CODEC_CHUNK];
    size_t bound = res_codec_bound(id, len);
    CHECK(bound >= len, "bound covers the input");
    CHECK(bound <= sizeof enc, "bound fits the test buffer");

    size_t n = res_codec_encode(id, src, len, enc, sizeof enc);
    if (n == 0) {                       // legal: "would not be smaller" -> the builder emits RAW
        printf("  %s: incompressible, encoder declined (RAW fallback)\n", what);
        return;
    }
    CHECK(n < len || id == RES_CODEC_RAW, "a compressed chunk is strictly smaller");
    size_t m = res_codec_decode(id, enc, n, dec, len);
    CHECK(m == len, "decode returns the known uncompressed length");
    CHECK(m == len && memcmp(dec, src, len) == 0, "decode round-trips byte-exact");
}

// Fill a buffer with a mixed pattern: long runs (compressible) spliced with xorshift noise,
// so LZ4 both compresses parts and hits the incompressible per-chunk RAW fallback on others.
static void fill_mixed(uint8_t *p, size_t n, uint64_t seed)
{
    uint64_t s = seed | 1;
    for (size_t i = 0; i < n; i++) {
        if ((i / 313) & 1) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            p[i] = (uint8_t)s;
        } else {
            p[i] = (uint8_t)(i & 0x3f);
        }
    }
}

// One exact-size single-chunk round-trip on the heap, for the boundary sizes the codec's
// per-chunk contract lives or dies on: 1, 495 KiB, and exactly RES_CODEC_CHUNK (496 KiB).
static void roundtrip_exact(res_codec_id id, size_t len, const char *what)
{
    size_t bound = res_codec_bound(id, len);
    CHECK(bound >= len, "bound covers the input");
    uint8_t *src = malloc(len ? len : 1);
    uint8_t *enc = malloc(bound ? bound : 1);
    uint8_t *dec = malloc(len ? len : 1);
    if (src == NULL || enc == NULL || dec == NULL) {
        CHECK(0, "boundary alloc");
        free(src); free(enc); free(dec);
        return;
    }
    fill_mixed(src, len, 0x1234u ^ len);
    size_t n = res_codec_encode(id, src, len, enc, bound);
    if (n == 0) {
        printf("  %s: encoder declined (RAW fallback)\n", what);
    } else {
        size_t m = res_codec_decode(id, enc, n, dec, len);
        CHECK(m == len && memcmp(dec, src, len) == 0, what);
    }
    free(src); free(enc); free(dec);
}

// A 5 MiB payload driven through the codec chunk by chunk, exactly as the pack builder and
// reader do: encode each RES_CODEC_CHUNK slice (RAW-fallback when it does not shrink), then
// decode each back into place with the KNOWN chunk length. Byte-exact end to end.
static void roundtrip_chunked(res_codec_id id, size_t total, const char *what)
{
    uint8_t *src = malloc(total);
    uint8_t *out = malloc(total);
    size_t   ebound = res_codec_bound(id, RES_CODEC_CHUNK);
    uint8_t *enc = malloc(ebound ? ebound : RES_CODEC_CHUNK);
    if (src == NULL || out == NULL || enc == NULL) {
        CHECK(0, "chunked alloc");
        free(src); free(out); free(enc);
        return;
    }
    fill_mixed(src, total, 0xC0FFEEu);
    int ok = 1;
    for (size_t off = 0; off < total; off += RES_CODEC_CHUNK) {
        size_t raw = total - off > RES_CODEC_CHUNK ? RES_CODEC_CHUNK : total - off;
        size_t n = res_codec_encode(id, src + off, raw, enc, ebound);
        if (n == 0) {                           // not smaller: the chunk ships RAW
            memcpy(out + off, src + off, raw);
        } else {
            size_t m = res_codec_decode(id, enc, n, out + off, raw);
            if (m != raw) { ok = 0; break; }
        }
    }
    CHECK(ok && memcmp(out, src, total) == 0, what);
    free(src); free(out); free(enc);
}

int main(void)
{
    printf("res_codec: RAW + LZ4 chunk codecs\n");

    static uint8_t compressible[N];
    for (size_t i = 0; i < N; i++)
        compressible[i] = (uint8_t)("anoptic" [i % 7]);

    static uint8_t noise[N];
    uint64_t s = 0x9e3779b97f4a7c15u;
    for (size_t i = 0; i < N; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        noise[i] = (uint8_t)s;
    }

    CHECK(res_codec_available(RES_CODEC_RAW), "RAW is always available");
    CHECK(res_codec_available(RES_CODEC_LZ4), "LZ4 is vendored and always available");
    CHECK(!res_codec_available(RES_CODEC_GDEFLATE), "GDEFLATE's byte is RESERVED, never available");
#ifndef ANOPTIC_ZSTD
    CHECK(!res_codec_available(RES_CODEC_ZSTD), "ZSTD is off in a default build");
#endif

    roundtrip(RES_CODEC_RAW, compressible, N, "raw/compressible");
    roundtrip(RES_CODEC_LZ4, compressible, N, "lz4/compressible");
    roundtrip(RES_CODEC_LZ4, noise, N, "lz4/noise");
    roundtrip(RES_CODEC_RAW, noise, 1, "raw/one-byte");
    roundtrip(RES_CODEC_LZ4, noise, 1, "lz4/one-byte");

    // An unavailable codec REFUSES; it never guesses.
    uint8_t out[64];
    CHECK(res_codec_decode(RES_CODEC_GDEFLATE, noise, 16, out, sizeof out) == 0,
          "a reserved codec refuses to decode");
    CHECK(res_codec_bound(RES_CODEC_GDEFLATE, 16) == 0, "a reserved codec has no bound");

    // Totality: hostile bytes never read or write out of bounds, and a lie about the length
    // never becomes a full reconstruction. (ASan/UBSan is what proves the "never UB" half.)
    static uint8_t enc[RES_CODEC_CHUNK + 4096];
    static uint8_t dec[N];
    size_t n = res_codec_encode(RES_CODEC_LZ4, compressible, N, enc, sizeof enc);
    CHECK(n > 0 && n < N, "compressible input compresses");
    if (n > 2) {
        CHECK(res_codec_decode(RES_CODEC_LZ4, enc, n / 2, dec, N) != N,
              "a truncated lz4 chunk can never yield a full chunk");
        size_t g = res_codec_decode(RES_CODEC_LZ4, noise, 64, dec, N);
        CHECK(g <= N, "garbage stays inside the destination");
    }
    CHECK(res_codec_decode(RES_CODEC_LZ4, NULL, 8, dec, N) == 0, "NULL src is refused");
    CHECK(res_codec_decode(RES_CODEC_LZ4, enc, 0, dec, N) == 0, "zero-length src is refused");
    CHECK(res_codec_decode(RES_CODEC_RAW, enc, n, dec, n + 1) == 0,
          "a RAW chunk whose length disagrees with the TOC is refused");
    CHECK(res_codec_encode(RES_CODEC_LZ4, compressible, RES_CODEC_CHUNK + 1, enc, sizeof enc) == 0,
          "a chunk larger than RES_CODEC_CHUNK is refused");

    // ------------------------------------------------------------------------------------
    // Size matrix: 0, 1, 495 KiB, 496 KiB (== RES_CODEC_CHUNK), 497 KiB (refused), and a
    // 5 MiB multi-chunk drive. The codec is a single-chunk primitive: the last is exercised
    // exactly as the pack layers it.
    const size_t K = 1024;
    CHECK(res_codec_encode(RES_CODEC_RAW, compressible, 0, enc, sizeof enc) == 0, "encode of 0 bytes is refused");
    CHECK(res_codec_encode(RES_CODEC_LZ4, compressible, 0, enc, sizeof enc) == 0, "lz4 encode of 0 bytes is refused");
    CHECK(res_codec_bound(RES_CODEC_LZ4, 0) == 0, "bound of 0 bytes is 0");

    roundtrip_exact(RES_CODEC_RAW, 1,          "raw/1B exact");
    roundtrip_exact(RES_CODEC_LZ4, 1,          "lz4/1B exact");
    roundtrip_exact(RES_CODEC_RAW, 495 * K,    "raw/495KiB exact");
    roundtrip_exact(RES_CODEC_LZ4, 495 * K,    "lz4/495KiB exact");
    roundtrip_exact(RES_CODEC_RAW, RES_CODEC_CHUNK, "raw/496KiB(==CHUNK) exact");
    roundtrip_exact(RES_CODEC_LZ4, RES_CODEC_CHUNK, "lz4/496KiB(==CHUNK) exact");

    // 497 KiB is one chunk too big: encode refuses, and decode with an over-CHUNK dst_cap
    // refuses too (dst_cap > RES_CODEC_CHUNK is never a legal chunk length).
    CHECK(res_codec_encode(RES_CODEC_LZ4, compressible, 497 * K, enc, sizeof enc) == 0,
          "497KiB encode is refused (> CHUNK)");
    CHECK(res_codec_decode(RES_CODEC_LZ4, enc, 16, dec, 497 * K) == 0,
          "497KiB dst_cap decode is refused (> CHUNK)");

    roundtrip_chunked(RES_CODEC_RAW, 5u * 1024 * 1024, "raw/5MiB chunked");
    roundtrip_chunked(RES_CODEC_LZ4, 5u * 1024 * 1024, "lz4/5MiB chunked");

    // ------------------------------------------------------------------------------------
    // Decode bound enforcement: dst_cap is the TOC-KNOWN uncompressed length. A decode that
    // would land fewer or more bytes than dst_cap is a lie and yields 0, and it never writes
    // past the destination (a guard byte just past dst_cap must survive).
    static uint8_t guarded[N + 1];
    size_t gn = res_codec_encode(RES_CODEC_LZ4, compressible, N, enc, sizeof enc);
    CHECK(gn > 0, "guard: compressible encodes");
    guarded[N] = 0x5A;
    size_t gm = res_codec_decode(RES_CODEC_LZ4, enc, gn, guarded, N);
    CHECK(gm == N && guarded[N] == 0x5A, "decode fills exactly dst_cap and no more");
    CHECK(res_codec_decode(RES_CODEC_LZ4, enc, gn, dec, N + 1) == 0,
          "a dst_cap larger than the true length is refused (short decode)");
    CHECK(res_codec_decode(RES_CODEC_LZ4, enc, gn, dec, N - 1) == 0,
          "a dst_cap smaller than the true length is refused");

    printf(failures ? "res_codec: %d FAILURE(S)\n" : "res_codec: OK\n", failures);
    return failures ? 1 : 0;
}
