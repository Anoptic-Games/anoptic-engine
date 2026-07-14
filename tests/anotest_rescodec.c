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

    printf(failures ? "res_codec: %d FAILURE(S)\n" : "res_codec: OK\n", failures);
    return failures ? 1 : 0;
}
