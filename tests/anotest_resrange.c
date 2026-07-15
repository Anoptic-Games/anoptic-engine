/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* resource ranges: partial reads that never lie
 *
 * Oracle for res_read_range / res_hash_file over a RES_SRC_DIR source, against files this
 * test stages itself and whose bytes it therefore knows exactly:
 *   - exact-range round-trips at [0,size), an interior slice, and the final byte;
 *   - len == 0 is delivered without IO (0), including at off == size;
 *   - a range that straddles or starts at EOF is RES_RANGE_EOF, never a silent partial;
 *   - 64-bit offset arithmetic: a >4 GiB offset into a tiny file is EOF, not a wrap;
 *   - an off + len that overflows uint64 is -2, not an out-of-bounds read;
 *   - invalid arguments (NULL src, NULL dst with len > 0) are -2;
 *   - res_hash_file returns the whole-file FNV-1a-64 and byte count, checked against an
 *     independent FNV computed right here.
 * A stub returning -1 fails every round-trip assertion. Exit 0 == pass. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <anoptic_filesystem.h>

#include "resources_internal.h"
#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define DIRNAME "range_scratch"

// Independent oracle: FNV-1a-64 over a byte span, without the code under test.
static uint64_t fnv_oracle(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    size_t w = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return w == len;
}

static res_source dir_src(const char *rel)
{
    res_source s = { .kind = RES_SRC_DIR };
    int w = snprintf(s.path.str, sizeof s.path.str, "%s", rel);
    s.path.length = (w > 0 && w < (int)sizeof s.path.str) ? (uint16_t)w : 0;
    return s;
}

int main(void)
{
    scratch_anchor_to_exe();
    printf("resource ranges: partial reads that never lie\n");

    scratch_make_dir(DIRNAME);

    // A 3000-byte file with a non-trivial, position-dependent pattern.
    enum { SIZE = 3000 };
    static uint8_t src[SIZE];
    for (int i = 0; i < SIZE; i++)
        src[i] = (uint8_t)(i * 131u + 7u);
    CHECK(write_file(DIRNAME "/data.bin", src, SIZE), "stage data.bin");

    res_source s = dir_src(DIRNAME "/data.bin");
    static uint8_t got[SIZE + 16];

    // Full-file exact range.
    memset(got, 0xAB, sizeof got);
    CHECK(res_read_range(&s, 0, SIZE, got) == 0, "full range returns 0");
    CHECK(memcmp(got, src, SIZE) == 0, "full range byte-exact");

    // Interior slice [1000, 1500).
    memset(got, 0xAB, sizeof got);
    CHECK(res_read_range(&s, 1000, 500, got) == 0, "interior slice returns 0");
    CHECK(memcmp(got, src + 1000, 500) == 0, "interior slice byte-exact");

    // The final byte.
    memset(got, 0xAB, sizeof got);
    CHECK(res_read_range(&s, SIZE - 1, 1, got) == 0, "last byte returns 0");
    CHECK(got[0] == src[SIZE - 1], "last byte value");

    // len == 0 is a no-op success, both mid-file and at EOF.
    CHECK(res_read_range(&s, 0, 0, got) == 0, "len 0 at start is 0");
    CHECK(res_read_range(&s, SIZE, 0, got) == 0, "len 0 at off==size is 0");
    CHECK(res_read_range(&s, 1234, 0, NULL) == 0, "len 0 with NULL dst is 0");

    // At and past EOF: RES_RANGE_EOF, never a partial.
    CHECK(res_read_range(&s, SIZE, 1, got) == RES_RANGE_EOF, "read at off==size is EOF");
    CHECK(res_read_range(&s, SIZE - 100, 200, got) == RES_RANGE_EOF, "straddling EOF is EOF");
    CHECK(res_read_range(&s, SIZE + 500, 1, got) == RES_RANGE_EOF, "well past EOF is EOF");

    // 64-bit offset arithmetic: a >4 GiB offset into a 3 KiB file is EOF (proves the offset
    // reaches the seam as 64 bits and is not truncated to a wrapped in-bounds value).
    CHECK(res_read_range(&s, UINT64_C(5) * 1024 * 1024 * 1024, 1, got) == RES_RANGE_EOF,
          "5 GiB offset is EOF, not a wrap");

    // off + len overflow is rejected before any IO.
    CHECK(res_read_range(&s, UINT64_MAX - 4, 16, got) == -2, "off+len overflow is -2");

    // Invalid arguments.
    CHECK(res_read_range(NULL, 0, 4, got) == -2, "NULL src is -2");
    CHECK(res_read_range(&s, 0, 4, NULL) == -2, "NULL dst with len>0 is -2");

    // An empty file: [0,0) succeeds, any positive length is EOF.
    CHECK(write_file(DIRNAME "/empty.bin", "", 0), "stage empty.bin");
    res_source es = dir_src(DIRNAME "/empty.bin");
    CHECK(res_read_range(&es, 0, 0, got) == 0, "empty file len 0 is 0");
    CHECK(res_read_range(&es, 0, 1, got) == RES_RANGE_EOF, "empty file len 1 is EOF");

    // A missing file is an IO failure, not EOF or a partial.
    res_source ms = dir_src(DIRNAME "/does_not_exist.bin");
    CHECK(res_read_range(&ms, 0, 4, got) == -1, "missing file is -1");

    // res_hash_file: whole-file FNV-1a-64 + size, checked against the independent oracle.
    uint64_t h = 0, sz = 0;
    CHECK(res_hash_file(&s, &h, &sz) == 0, "hash_file returns 0");
    CHECK(sz == SIZE, "hash_file size matches");
    CHECK(h == fnv_oracle(src, SIZE), "hash_file digest matches the oracle");
    uint64_t he = 0, sze = 1;
    CHECK(res_hash_file(&es, &he, &sze) == 0, "hash_file on empty returns 0");
    CHECK(sze == 0, "hash_file empty size is 0");
    CHECK(he == fnv_oracle(src, 0), "hash_file empty digest is the FNV basis");
    CHECK(res_hash_file(&ms, &h, &sz) == -1, "hash_file on missing is -1");

    // Cleanup.
    remove(DIRNAME "/data.bin");
    remove(DIRNAME "/empty.bin");
    scratch_remove_dir(DIRNAME);

    if (failures == 0) { printf("anotest_resrange: all checks passed\n"); return 0; }
    printf("anotest_resrange: %d check(s) failed\n", failures);
    return 1;
}
