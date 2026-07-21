/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
/* block framing: the hostile-input battery (W5, M11)
 *
 * res_block_open is the single most fuzz-worthy function in the module: every baked or
 * packed block in the engine enters through it, so a hole here is every domain's hole. This
 * is the generic framing gate. It proves, against an INDEPENDENT reference (its own
 * FNV-1a-64 and its own rule set, never the implementation's helpers):
 *   - res_plane_layout is PURE, exact, grain-aligned, and refuses every overflow (including
 *     a count*elem product that WRAPS TO A SMALL nonzero value -- the case a later guard
 *     cannot backstop);
 *   - res_block_seal emits canonical, deterministic headers whose block_hash is exactly
 *     FNV-1a-64 over the whole block with the hash field zeroed, and refuses any table it
 *     could not later open;
 *   - res_block_open refuses truncation at every boundary, bad magic/version/layout_id, hash
 *     mismatch (bit flips across header and payload), offsets past end / inside the header /
 *     unaligned, NON-MONOTONE offsets isolated from the span rule by zero lengths,
 *     overlapping planes, counts past the block, plane_count over max, zero/near-empty
 *     blocks -- and accepts exactly the structurally-sound, hash-valid blocks at the
 *     boundary (> vs >= bugs die here);
 *   - a deterministic seeded xorshift64 fuzzer (no time()): raw byte mutations oracled by
 *     byte-identity, and structural mutations under a repatched valid hash oracled by the
 *     reference validator. Every open runs on an EXACT-SIZE heap copy so a sanitizer sees the
 *     true extent; every iteration matches the oracle, never crashes, never accepts a
 *     hash-invalid block. The interior-index battery (prim.material, node.mesh, ...) belongs
 *     to the graphics extension's load-time cross-reference pass, layered on this gate. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "resources_block.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); failures++; } \
} while (0)

static const uint32_t TEST_MAGIC = UINT32_C(0x52455342);
static const uint32_t TEST_VERSION = UINT32_C(7);
static const uint64_t TEST_LAYOUT_ID = UINT64_C(0x91d5b62f4a38c701);

static uint64_t ref_fnv1a64_zero_hash(const void *bytes, size_t len)
{
    const uint8_t *p = bytes;
    const size_t hash_begin = offsetof(res_block_hdr, block_hash);
    const size_t hash_end = hash_begin + sizeof(uint64_t);
    uint64_t hash = UINT64_C(0xcbf29ce484222325);

    for (size_t i = 0; i < len; ++i) {
        const uint8_t byte = i >= hash_begin && i < hash_end ? 0 : p[i];
        hash ^= byte;
        hash *= UINT64_C(0x00000100000001b3);
    }

    return hash;
}

static uint64_t load_u64(const void *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void store_u64(void *p, uint64_t value)
{
    memcpy(p, &value, sizeof(value));
}

static int ref_hash_valid(const void *bytes, size_t len)
{
    const uint8_t *p = bytes;

    if (bytes == NULL ||
        len < offsetof(res_block_hdr, block_hash) + sizeof(uint64_t)) {
        return 0;
    }

    return load_u64(p + offsetof(res_block_hdr, block_hash)) ==
           ref_fnv1a64_zero_hash(bytes, len);
}

static void ref_repatch_hash(void *bytes, size_t len)
{
    uint8_t *p = bytes;
    uint64_t zero = 0;

    CHECK(bytes != NULL);
    CHECK(len >= offsetof(res_block_hdr, block_hash) + sizeof(uint64_t));
    if (bytes == NULL ||
        len < offsetof(res_block_hdr, block_hash) + sizeof(uint64_t)) {
        return;
    }

    store_u64(p + offsetof(res_block_hdr, block_hash), zero);
    store_u64(p + offsetof(res_block_hdr, block_hash),
              ref_fnv1a64_zero_hash(bytes, len));
}

static int ref_block_accepts(const void *bytes, size_t len,
                             uint32_t magic, uint32_t version,
                             uint64_t layout_id)
{
    res_block_hdr hdr;
    uint64_t previous = 0;

    if (bytes == NULL || len < sizeof(res_block_hdr)) {
        return 0;
    }

    memcpy(&hdr, bytes, sizeof(hdr));

    if (hdr.magic != magic ||
        hdr.version != version ||
        hdr.layout_id != layout_id) {
        return 0;
    }

    if (hdr.plane_count > RES_BLOCK_PLANES_MAX) {
        return 0;
    }

    for (uint32_t i = 0; i < hdr.plane_count; ++i) {
        const uint64_t off = hdr.off[i];

        if ((off & (RES_PLANE_GRAIN - 1u)) != 0) {
            return 0;
        }
        if (off < sizeof(res_block_hdr)) {
            return 0;
        }
        if (off > (uint64_t)len) {
            return 0;
        }
        if (i != 0 && off < previous) {
            return 0;
        }

        previous = off;
    }

    for (uint32_t i = 0; i < hdr.plane_count; ++i) {
        const uint64_t next = i + 1u < hdr.plane_count
                            ? hdr.off[i + 1u]
                            : (uint64_t)len;
        const uint64_t span = next - hdr.off[i];

        if (hdr.len[i] > span) {
            return 0;
        }
    }

    return hdr.block_hash == ref_fnv1a64_zero_hash(bytes, len);
}

static int bytes_are_zero(const void *bytes, size_t len)
{
    const uint8_t *p = bytes;

    for (size_t i = 0; i < len; ++i) {
        if (p[i] != 0) {
            return 0;
        }
    }

    return 1;
}

static void validate_accepted_view(const uint8_t *bytes, size_t len,
                                   const res_block_view *view)
{
    res_block_hdr hdr;

    CHECK(bytes != NULL);
    CHECK(len >= sizeof(hdr));
    if (bytes == NULL || len < sizeof(hdr)) {
        return;
    }

    memcpy(&hdr, bytes, sizeof(hdr));

    CHECK(view->hdr == (const res_block_hdr *)(const void *)bytes);
    CHECK(view->size == len);

    for (uint32_t i = 0; i < hdr.plane_count; ++i) {
        const uint64_t next = i + 1u < hdr.plane_count
                            ? hdr.off[i + 1u]
                            : (uint64_t)len;

        CHECK((hdr.off[i] & (RES_PLANE_GRAIN - 1u)) == 0);
        CHECK(hdr.off[i] >= sizeof(res_block_hdr));
        CHECK(hdr.off[i] <= (uint64_t)len);
        if (i != 0) {
            CHECK(hdr.off[i] >= hdr.off[i - 1u]);
        }
        CHECK(hdr.len[i] <= next - hdr.off[i]);
        CHECK(view->plane[i] == bytes + (size_t)hdr.off[i]);
        CHECK(view->count[i] == hdr.len[i]);
    }

    for (uint32_t i = hdr.plane_count; i < RES_BLOCK_PLANES_MAX; ++i) {
        CHECK(view->plane[i] == NULL);
        CHECK(view->count[i] == 0);
    }
}

static int open_exact_copy(const void *source, size_t len,
                           uint32_t magic, uint32_t version,
                           uint64_t layout_id, int expected_accept)
{
    uint8_t *copy = malloc(len);
    res_block_view view;
    const int oracle = ref_block_accepts(source, len, magic, version,
                                         layout_id);
    int rc;

    if (expected_accept >= 0) {
        CHECK(oracle == expected_accept);
    }

    if (len != 0 && copy == NULL) {
        CHECK(0);
        return 0;
    }

    if (len != 0) {
        CHECK(source != NULL);
        if (source == NULL) {
            free(copy);
            return 0;
        }
        memcpy(copy, source, len);
    }

    memset(&view, 0xa5, sizeof(view));
    rc = res_block_open(copy, len, magic, version, layout_id, &view);

    CHECK(rc == (oracle ? 0 : -1));

    if (rc == 0) {
        CHECK(oracle);
        if (oracle) {
            validate_accepted_view(copy, len, &view);
        }
    } else {
        CHECK(bytes_are_zero(&view, sizeof(view)));
    }

    free(copy);
    return rc == 0;
}

static uint8_t image_byte(size_t offset)
{
    return (uint8_t)((offset * 131u + (offset >> 3u) + 0x5au) & 0xffu);
}

static void fill_image(uint8_t *bytes, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        bytes[i] = image_byte(i);
    }
}

static void check_golden_payload(const uint8_t *source, size_t len,
                                 const uint64_t *off,
                                 const uint64_t *count,
                                 const size_t *elem_size,
                                 size_t n_planes)
{
    uint8_t *copy = malloc(len);
    res_block_view view;
    int rc;

    CHECK(copy != NULL);
    if (copy == NULL) {
        return;
    }

    memcpy(copy, source, len);
    memset(&view, 0xa5, sizeof(view));

    rc = res_block_open(copy, len, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, &view);
    CHECK(rc == 0);

    if (rc == 0) {
        validate_accepted_view(copy, len, &view);

        for (size_t i = 0; i < n_planes; ++i) {
            const size_t payload_bytes = (size_t)count[i] * elem_size[i];
            const uint8_t *plane = view.plane[i];
            const uint8_t *expected = copy + (size_t)off[i];

            CHECK(view.plane[i] == expected);
            CHECK(view.count[i] == count[i]);

            if (plane == expected) {
                for (size_t j = 0; j < payload_bytes; ++j) {
                    CHECK(plane[j] == image_byte((size_t)off[i] + j));
                }
            }
        }
    }

    free(copy);
}

static uint64_t xorshift64(uint64_t *state)
{
    uint64_t x = *state;

    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

static void run_repatched_structural_case(uint8_t *work,
                                         size_t len,
                                         int expected_accept)
{
    ref_repatch_hash(work, len);
    CHECK(ref_hash_valid(work, len));
    open_exact_copy(work, len, TEST_MAGIC, TEST_VERSION,
                    TEST_LAYOUT_ID, expected_accept);
}

int main(void)
{
    enum { GOLD_PLANES = 4 };

    printf("block framing: the hostile-input battery\n");

    const size_t layout_count[GOLD_PLANES] = { 13u, 0u, 25u, 64u };
    const size_t elem_size[GOLD_PLANES] = { 1u, 8u, 3u, 1u };
    const uint64_t seal_len[GOLD_PLANES] = {
        UINT64_C(13), UINT64_C(0), UINT64_C(25), UINT64_C(64)
    };
    const uint64_t expected_off[GOLD_PLANES] = {
        UINT64_C(576), UINT64_C(640), UINT64_C(640), UINT64_C(768)
    };
    const size_t expected_total = 832u;

    size_t count_copy[GOLD_PLANES];
    size_t elem_copy[GOLD_PLANES];
    uint64_t layout_off_a[RES_BLOCK_PLANES_MAX];
    uint64_t layout_off_b[RES_BLOCK_PLANES_MAX];
    size_t total_a;
    size_t total_b;
    uint8_t *golden;
    uint8_t *golden_second;
    uint8_t *work;
    res_block_hdr hdr;

    CHECK(sizeof(res_block_hdr) == 544u);
    CHECK(offsetof(res_block_hdr, magic) == 0u);
    CHECK(offsetof(res_block_hdr, version) == 4u);
    CHECK(offsetof(res_block_hdr, layout_id) == 8u);
    CHECK(offsetof(res_block_hdr, block_hash) == 16u);
    CHECK(offsetof(res_block_hdr, plane_count) == 24u);
    CHECK(offsetof(res_block_hdr, _pad) == 28u);
    CHECK(offsetof(res_block_hdr, off) == 32u);
    CHECK(offsetof(res_block_hdr, len) == 288u);

    memcpy(count_copy, layout_count, sizeof(count_copy));
    memcpy(elem_copy, elem_size, sizeof(elem_copy));
    memset(layout_off_a, 0xa5, sizeof(layout_off_a));
    memset(layout_off_b, 0x5a, sizeof(layout_off_b));

    total_a = res_plane_layout(sizeof(res_block_hdr), layout_count,
                               elem_size, GOLD_PLANES, layout_off_a);
    total_b = res_plane_layout(sizeof(res_block_hdr), layout_count,
                               elem_size, GOLD_PLANES, layout_off_b);

    CHECK(total_a == expected_total);
    CHECK(total_b == expected_total);
    CHECK(memcmp(layout_off_a, expected_off, sizeof(expected_off)) == 0);
    CHECK(memcmp(layout_off_b, expected_off, sizeof(expected_off)) == 0);
    CHECK(memcmp(layout_off_a, layout_off_b,
                 GOLD_PLANES * sizeof(layout_off_a[0])) == 0);
    CHECK(memcmp(layout_count, count_copy, sizeof(count_copy)) == 0);
    CHECK(memcmp(elem_size, elem_copy, sizeof(elem_copy)) == 0);
    CHECK(layout_off_a[1] == layout_off_a[2]);
    CHECK((layout_off_a[0] & (RES_PLANE_GRAIN - 1u)) == 0);
    CHECK((layout_off_a[1] & (RES_PLANE_GRAIN - 1u)) == 0);
    CHECK((layout_off_a[2] & (RES_PLANE_GRAIN - 1u)) == 0);
    CHECK((layout_off_a[3] & (RES_PLANE_GRAIN - 1u)) == 0);

    CHECK(res_plane_layout(sizeof(res_block_hdr), NULL, NULL, 0, NULL) ==
          576u);

    {
        size_t count_33[RES_BLOCK_PLANES_MAX + 1u] = { 0 };
        size_t elem_33[RES_BLOCK_PLANES_MAX + 1u] = { 0 };
        uint64_t off_33[RES_BLOCK_PLANES_MAX + 1u] = { 0 };
        size_t one_count[1] = { 1u };
        size_t one_elem[1] = { 1u };
        uint64_t one_off[1] = { UINT64_C(0xfeedfacecafebeef) };

        CHECK(res_plane_layout(sizeof(res_block_hdr), count_33, elem_33,
                               RES_BLOCK_PLANES_MAX + 1u, off_33) == 0);
        CHECK(res_plane_layout(sizeof(res_block_hdr), NULL, one_elem,
                               1u, one_off) == 0);
        CHECK(res_plane_layout(sizeof(res_block_hdr), one_count, NULL,
                               1u, one_off) == 0);
        CHECK(res_plane_layout(sizeof(res_block_hdr), one_count, one_elem,
                               1u, NULL) == 0);
    }

    {
        size_t count[2];
        size_t elem[2];
        uint64_t off[2];

        count[0] = SIZE_MAX / 2u + 1u;
        elem[0] = 2u;
        CHECK(res_plane_layout(0, count, elem, 1u, off) == 0);

        count[0] = SIZE_MAX;
        elem[0] = 1u;
        CHECK(res_plane_layout(0, count, elem, 1u, off) == 0);

        count[0] = SIZE_MAX - 63u;
        elem[0] = 1u;
        CHECK(res_plane_layout(64u, count, elem, 1u, off) == 0);

        count[0] = 1u;
        elem[0] = 1u;
        CHECK(res_plane_layout(SIZE_MAX, count, elem, 1u, off) == 0);

        count[0] = 64u;
        count[1] = SIZE_MAX - 63u;
        elem[0] = 1u;
        elem[1] = 1u;
        CHECK(res_plane_layout(0, count, elem, 2u, off) == 0);

        /* count*elem WRAPS to a small nonzero product (2 * (SIZE_MAX/2 + 2) == 4 mod 2^64):
           the round-up and accumulation guards see the wrapped 4 and are happy, so ONLY a
           genuine count*elem overflow check refuses this and returns 0. */
        count[0] = 2u;
        elem[0] = SIZE_MAX / 2u + 2u;
        CHECK(res_plane_layout(0, count, elem, 1u, off) == 0);
    }

    golden = malloc(expected_total);
    golden_second = malloc(expected_total);
    work = malloc(expected_total);

    CHECK(golden != NULL);
    CHECK(golden_second != NULL);
    CHECK(work != NULL);
    if (golden == NULL || golden_second == NULL || work == NULL) {
        free(golden);
        free(golden_second);
        free(work);
        printf("FAILED: %d assertion(s)\n", failures);
        return 1;
    }

    fill_image(golden, expected_total);
    fill_image(golden_second, expected_total);

    CHECK(res_block_seal(golden, expected_total, TEST_MAGIC, TEST_VERSION,
                         TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                         seal_len) == 0);
    CHECK(res_block_seal(golden_second, expected_total, TEST_MAGIC,
                         TEST_VERSION, TEST_LAYOUT_ID, GOLD_PLANES,
                         layout_off_a, seal_len) == 0);

    CHECK(memcmp(golden, golden_second, expected_total) == 0);

    memcpy(&hdr, golden, sizeof(hdr));
    CHECK(hdr.magic == TEST_MAGIC);
    CHECK(hdr.version == TEST_VERSION);
    CHECK(hdr.layout_id == TEST_LAYOUT_ID);
    CHECK(hdr.plane_count == GOLD_PLANES);
    CHECK(hdr._pad == 0);
    CHECK(hdr.block_hash == ref_fnv1a64_zero_hash(golden, expected_total));

    for (size_t i = 0; i < GOLD_PLANES; ++i) {
        CHECK(hdr.off[i] == expected_off[i]);
        CHECK(hdr.len[i] == seal_len[i]);
    }

    for (size_t i = GOLD_PLANES; i < RES_BLOCK_PLANES_MAX; ++i) {
        CHECK(hdr.off[i] == 0);
        CHECK(hdr.len[i] == 0);
    }

    memcpy(work, golden, expected_total);
    CHECK(res_block_seal(golden, expected_total, TEST_MAGIC, TEST_VERSION,
                         TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                         seal_len) == 0);
    CHECK(memcmp(work, golden, expected_total) == 0);

    {
        uint8_t tiny[543];
        uint64_t off_33[RES_BLOCK_PLANES_MAX + 1u] = { 0 };
        uint64_t len_33[RES_BLOCK_PLANES_MAX + 1u] = { 0 };
        uint64_t bad_off[GOLD_PLANES];

        memcpy(bad_off, expected_off, sizeof(bad_off));
        bad_off[0] = 0;

        memset(tiny, 0, sizeof(tiny));

        CHECK(res_block_seal(NULL, expected_total, TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                             seal_len) == -1);
        CHECK(res_block_seal(tiny, sizeof(tiny), TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                             seal_len) == -1);
        CHECK(res_block_seal(work, expected_total, TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, RES_BLOCK_PLANES_MAX + 1u,
                             off_33, len_33) == -1);
        CHECK(res_block_seal(work, expected_total, TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, GOLD_PLANES, NULL,
                             seal_len) == -1);
        CHECK(res_block_seal(work, expected_total, TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                             NULL) == -1);

        memcpy(work, golden_second, expected_total);
        CHECK(res_block_seal(work, expected_total, TEST_MAGIC, TEST_VERSION,
                             TEST_LAYOUT_ID, GOLD_PLANES, bad_off,
                             seal_len) == -1);
    }

    CHECK(ref_block_accepts(golden, expected_total, TEST_MAGIC,
                            TEST_VERSION, TEST_LAYOUT_ID));
    open_exact_copy(golden, expected_total, TEST_MAGIC, TEST_VERSION,
                    TEST_LAYOUT_ID, 1);
    check_golden_payload(golden, expected_total, expected_off, seal_len,
                         elem_size, GOLD_PLANES);

    {
        /* Base alignment: the gate refuses a base below the header's own alignment (a typed
         * view of it is UB) and accepts any base at or above it -- ABSOLUTE plane alignment
         * belongs to the allocation, not the gate; the off[] grain rule is base-relative. */
        uint8_t *raw = malloc(expected_total + 128);
        res_block_view view;

        CHECK(raw != NULL);
        if (raw != NULL) {
            uint8_t *base = (uint8_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);

            memcpy(base, golden, expected_total);
            memset(&view, 0xa5, sizeof(view));
            CHECK(res_block_open(base, expected_total, TEST_MAGIC, TEST_VERSION,
                                 TEST_LAYOUT_ID, &view) == 0);

            memcpy(base + 1, golden, expected_total);
            memset(&view, 0xa5, sizeof(view));
            CHECK(res_block_open(base + 1, expected_total, TEST_MAGIC, TEST_VERSION,
                                 TEST_LAYOUT_ID, &view) == -1);
            CHECK(bytes_are_zero(&view, sizeof(view)));

            memcpy(base + 4, golden, expected_total);
            memset(&view, 0xa5, sizeof(view));
            CHECK(res_block_open(base + 4, expected_total, TEST_MAGIC, TEST_VERSION,
                                 TEST_LAYOUT_ID, &view) == -1);

            memcpy(base + 8, golden, expected_total);
            memset(&view, 0xa5, sizeof(view));
            CHECK(res_block_open(base + 8, expected_total, TEST_MAGIC, TEST_VERSION,
                                 TEST_LAYOUT_ID, &view) == 0);

            CHECK(res_block_seal(base + 1, expected_total, TEST_MAGIC, TEST_VERSION,
                                 TEST_LAYOUT_ID, GOLD_PLANES, layout_off_a,
                                 seal_len) == -1);
            free(raw);
        }
    }

    {
        const size_t header_only_size = 576u;
        uint8_t *header_only = malloc(header_only_size);
        uint64_t dummy_off[1] = { 0 };
        uint64_t dummy_len[1] = { 0 };

        CHECK(header_only != NULL);
        if (header_only != NULL) {
            fill_image(header_only, header_only_size);
            CHECK(res_block_seal(header_only, header_only_size, TEST_MAGIC,
                                 TEST_VERSION, TEST_LAYOUT_ID, 0,
                                 dummy_off, dummy_len) == 0);
            open_exact_copy(header_only, header_only_size, TEST_MAGIC,
                            TEST_VERSION, TEST_LAYOUT_ID, 1);
            free(header_only);
        }
    }

    {
        size_t truncation[64];
        size_t truncation_count = 0;
        const size_t fixed[] = {
            0u, 1u, 8u, 16u, 24u, 31u, 32u,
            543u, 544u, 545u, 575u, 576u, 577u
        };

        for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
            truncation[truncation_count++] = fixed[i];
        }

        for (size_t i = 0; i < GOLD_PLANES; ++i) {
            const size_t off = (size_t)expected_off[i];

            truncation[truncation_count++] = off - 1u;
            truncation[truncation_count++] = off;
            truncation[truncation_count++] = off + 1u;
        }

        truncation[truncation_count++] = expected_total - 1u;

        for (size_t i = 0; i < truncation_count; ++i) {
            const size_t n = truncation[i];
            uint8_t *prefix;

            CHECK(n < expected_total);
            if (n >= expected_total) {
                continue;
            }

            open_exact_copy(golden, n, TEST_MAGIC, TEST_VERSION,
                            TEST_LAYOUT_ID, 0);

            prefix = malloc(n);
            if (n != 0 && prefix == NULL) {
                CHECK(0);
                continue;
            }
            if (n != 0) {
                memcpy(prefix, golden, n);
            }
            if (n >= offsetof(res_block_hdr, block_hash) + sizeof(uint64_t)) {
                ref_repatch_hash(prefix, n);
            }

            open_exact_copy(prefix, n, TEST_MAGIC, TEST_VERSION,
                            TEST_LAYOUT_ID, 0);
            free(prefix);
        }
    }

    {
        res_block_hdr *mut;

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->magic ^= UINT32_C(0x01000000);
        ref_repatch_hash(work, expected_total);
        open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->version = TEST_VERSION + 1u;
        ref_repatch_hash(work, expected_total);
        open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->version = TEST_VERSION - 1u;
        ref_repatch_hash(work, expected_total);
        open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->layout_id ^= UINT64_C(0x8000000000000001);
        ref_repatch_hash(work, expected_total);
        open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, 0);

        open_exact_copy(golden, expected_total,
                        TEST_MAGIC ^ UINT32_C(0x01000000),
                        TEST_VERSION, TEST_LAYOUT_ID, 0);
        open_exact_copy(golden, expected_total, TEST_MAGIC,
                        TEST_VERSION + 1u, TEST_LAYOUT_ID, 0);
        open_exact_copy(golden, expected_total, TEST_MAGIC,
                        TEST_VERSION - 1u, TEST_LAYOUT_ID, 0);
        open_exact_copy(golden, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID ^ UINT64_C(0x8000000000000001), 0);
    }

    {
        const size_t flip_offsets[] = {
            offsetof(res_block_hdr, magic),
            offsetof(res_block_hdr, version),
            offsetof(res_block_hdr, layout_id),
            offsetof(res_block_hdr, block_hash),
            offsetof(res_block_hdr, block_hash) + 7u,
            offsetof(res_block_hdr, plane_count),
            offsetof(res_block_hdr, _pad),
            offsetof(res_block_hdr, off),
            offsetof(res_block_hdr, off) + 10u * sizeof(uint64_t),
            offsetof(res_block_hdr, len),
            offsetof(res_block_hdr, len) + 10u * sizeof(uint64_t),
            544u,
            575u,
            576u,
            677u,
            715u,
            767u,
            768u,
            799u,
            831u
        };

        for (size_t i = 0;
             i < sizeof(flip_offsets) / sizeof(flip_offsets[0]);
             ++i) {
            const size_t offset = flip_offsets[i];

            CHECK(offset < expected_total);
            memcpy(work, golden, expected_total);
            work[offset] ^= (uint8_t)(1u << (i & 7u));
            open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                            TEST_LAYOUT_ID, 0);
        }

        memcpy(work, golden, expected_total);
        store_u64(work + offsetof(res_block_hdr, block_hash), 0);
        open_exact_copy(work, expected_total, TEST_MAGIC, TEST_VERSION,
                        TEST_LAYOUT_ID, 0);

        memset(work, 0, 544u);
        open_exact_copy(work, 544u, 0, 0, 0, 0);
    }

    {
        res_block_hdr *mut;

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 0;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 512u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 544u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 577u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[3] = expected_total + RES_PLANE_GRAIN;
        mut->len[3] = 0;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[3] = UINT64_C(1) << 63u;
        mut->len[3] = 0;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[3] = UINT64_MAX & ~UINT64_C(63);
        mut->len[3] = 0;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 640u;
        mut->off[1] = 576u;
        mut->len[0] = 0;
        mut->len[1] = 0;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[0] = 576u;
        mut->off[1] = 576u;
        mut->len[0] = 1u;
        mut->len[1] = 1u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->len[0] = 65u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->len[3] = 65u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->len[0] = UINT64_MAX;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->plane_count = 33u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->plane_count = UINT32_MAX;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[3] = expected_total;
        mut->len[3] = 1u;
        run_repatched_structural_case(work, expected_total, 0);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->len[0] = 64u;
        mut->len[1] = 0u;
        mut->len[2] = 128u;
        mut->len[3] = 64u;
        run_repatched_structural_case(work, expected_total, 1);

        memcpy(work, golden, expected_total);
        mut = (res_block_hdr *)(void *)work;
        mut->off[3] = expected_total;
        mut->len[3] = 0u;
        run_repatched_structural_case(work, expected_total, 1);
    }

    {
        res_block_view view;
        uint8_t *copy;
        int rc;

        memset(&view, 0xa5, sizeof(view));
        rc = res_block_open(NULL, expected_total, TEST_MAGIC, TEST_VERSION,
                            TEST_LAYOUT_ID, &view);
        CHECK(rc == -1);
        CHECK(bytes_are_zero(&view, sizeof(view)));

        copy = malloc(expected_total);
        CHECK(copy != NULL);
        if (copy != NULL) {
            memcpy(copy, golden, expected_total);
            rc = res_block_open(copy, expected_total, TEST_MAGIC,
                                TEST_VERSION, TEST_LAYOUT_ID, NULL);
            CHECK(rc == -1);
            free(copy);
        }
    }

    {
        const uint64_t seeds[] = {
            UINT64_C(0x9e3779b97f4a7c15),
            UINT64_C(0xd1b54a32d192ed03),
            UINT64_C(0x94d049bb133111eb),
            UINT64_C(0x2545f4914f6cdd1d)
        };
        const size_t raw_boundaries[] = {
            0u, 1u, 8u, 16u, 24u, 31u, 32u,
            543u, 544u, 545u, 575u, 576u, 577u,
            639u, 640u, 641u, 767u, 768u, 769u,
            831u, 832u
        };
        size_t raw_accept = 0;
        size_t raw_refuse = 0;
        size_t structural_accept = 0;
        size_t structural_refuse = 0;

        for (size_t seed_index = 0;
             seed_index < sizeof(seeds) / sizeof(seeds[0]);
             ++seed_index) {
            uint64_t state = seeds[seed_index];

            for (size_t iteration = 0; iteration < 1500u; ++iteration) {
                size_t n;
                uint8_t *mutation;
                int expected;
                int accepted;

                if (iteration % 101u == 0) {
                    n = expected_total;
                } else if (iteration % 7u == 0) {
                    n = raw_boundaries[
                        xorshift64(&state) %
                        (sizeof(raw_boundaries) /
                         sizeof(raw_boundaries[0]))
                    ];
                } else {
                    n = (size_t)(xorshift64(&state) %
                                 (expected_total + 1u));
                }

                mutation = malloc(n);
                if (n != 0 && mutation == NULL) {
                    CHECK(0);
                    continue;
                }
                if (n != 0) {
                    memcpy(mutation, golden, n);
                }

                if (iteration % 101u != 0 && n != 0) {
                    const size_t changes =
                        1u + (size_t)(xorshift64(&state) % 8u);

                    for (size_t j = 0; j < changes; ++j) {
                        const size_t offset =
                            (size_t)(xorshift64(&state) % n);
                        const uint8_t mask =
                            (uint8_t)(1u <<
                                (unsigned)(xorshift64(&state) & 7u));

                        mutation[offset] ^= mask;
                    }
                }

                expected = n == expected_total &&
                           memcmp(mutation, golden, expected_total) == 0;
                CHECK(ref_block_accepts(mutation, n, TEST_MAGIC,
                                        TEST_VERSION,
                                        TEST_LAYOUT_ID) == expected);

                accepted = open_exact_copy(mutation, n, TEST_MAGIC,
                                           TEST_VERSION, TEST_LAYOUT_ID,
                                           expected);
                if (accepted) {
                    CHECK(ref_hash_valid(mutation, n));
                    ++raw_accept;
                } else {
                    ++raw_refuse;
                }

                free(mutation);
            }
        }

        for (size_t seed_index = 0;
             seed_index < sizeof(seeds) / sizeof(seeds[0]);
             ++seed_index) {
            uint64_t state = seeds[seed_index] ^
                             UINT64_C(0xa0761d6478bd642f);

            for (size_t iteration = 0; iteration < 1500u; ++iteration) {
                res_block_hdr *mut;
                const uint64_t mode = xorshift64(&state) % 10u;
                int accepted;

                memcpy(work, golden, expected_total);
                mut = (res_block_hdr *)(void *)work;

                switch (mode) {
                case 0:
                    mut->plane_count = (uint32_t)xorshift64(&state);
                    break;

                case 1: {
                    static const uint64_t hostile_off[] = {
                        UINT64_C(0),
                        UINT64_C(512),
                        UINT64_C(544),
                        UINT64_C(577),
                        UINT64_C(832),
                        UINT64_C(896),
                        UINT64_C(1) << 63u,
                        UINT64_MAX & ~UINT64_C(63)
                    };
                    const size_t index =
                        (size_t)(xorshift64(&state) % GOLD_PLANES);

                    mut->plane_count = GOLD_PLANES;
                    mut->off[index] = hostile_off[
                        xorshift64(&state) %
                        (sizeof(hostile_off) /
                         sizeof(hostile_off[0]))
                    ];
                    break;
                }

                case 2: {
                    const size_t index =
                        (size_t)(xorshift64(&state) % GOLD_PLANES);

                    mut->plane_count = GOLD_PLANES;
                    mut->len[index] = xorshift64(&state);
                    break;
                }

                case 3:
                    mut->plane_count = GOLD_PLANES;
                    mut->off[0] = 640u;
                    mut->off[1] = 576u;
                    mut->len[0] = 0;
                    mut->len[1] = 0;
                    break;

                case 4: {
                    const uint32_t planes =
                        (uint32_t)(xorshift64(&state) % 9u);
                    uint64_t slot = 9u;

                    mut->plane_count = planes;
                    for (size_t i = 0; i < RES_BLOCK_PLANES_MAX; ++i) {
                        mut->off[i] = 0;
                        mut->len[i] = 0;
                    }

                    for (uint32_t i = 0; i < planes; ++i) {
                        const uint64_t choices = 14u - slot;

                        slot += xorshift64(&state) % choices;
                        mut->off[i] = slot * RES_PLANE_GRAIN;
                    }

                    for (uint32_t i = 0; i < planes; ++i) {
                        const uint64_t next =
                            i + 1u < planes
                            ? mut->off[i + 1u]
                            : expected_total;
                        const uint64_t span = next - mut->off[i];

                        mut->len[i] = xorshift64(&state) % (span + 1u);
                    }
                    break;
                }

                case 5:
                    break;

                case 6:
                    mut->plane_count =
                        (xorshift64(&state) & 1u) != 0
                        ? 33u
                        : UINT32_MAX;
                    break;

                case 7:
                    mut->plane_count = GOLD_PLANES;
                    mut->off[3] =
                        (xorshift64(&state) % 20u) * RES_PLANE_GRAIN;
                    mut->len[3] = xorshift64(&state) % 200u;
                    break;

                case 8:
                    mut->plane_count = GOLD_PLANES;
                    for (size_t i = 0; i < 3u; ++i) {
                        const size_t index =
                            (size_t)(xorshift64(&state) % GOLD_PLANES);

                        mut->off[index] = xorshift64(&state);
                        mut->len[index] = xorshift64(&state);
                    }
                    break;

                case 9:
                    mut->plane_count = GOLD_PLANES;
                    mut->len[0] = 64u;
                    mut->len[1] = 0u;
                    mut->len[2] = 128u;
                    mut->len[3] = 64u;
                    break;
                }

                ref_repatch_hash(work, expected_total);
                CHECK(ref_hash_valid(work, expected_total));

                accepted = open_exact_copy(work, expected_total,
                                           TEST_MAGIC, TEST_VERSION,
                                           TEST_LAYOUT_ID, -1);
                if (accepted) {
                    CHECK(ref_block_accepts(work, expected_total,
                                            TEST_MAGIC, TEST_VERSION,
                                            TEST_LAYOUT_ID));
                    CHECK(ref_hash_valid(work, expected_total));
                    ++structural_accept;
                } else {
                    ++structural_refuse;
                }
            }
        }

        printf("raw accept=%zu refuse=%zu structural accept=%zu refuse=%zu\n",
               raw_accept, raw_refuse,
               structural_accept, structural_refuse);
    }

    free(work);
    free(golden_second);
    free(golden);

    if (failures != 0) {
        printf("FAILED: %d assertion(s)\n", failures);
        return 1;
    }
    printf("all block framing assertions passed\n");
=======
/* block framing: the hostile-input battery
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W5, M11):
 *   - res_plane_layout is PURE and its offsets are RES_PLANE_GRAIN-aligned;
 *   - res_block_open refuses: truncated, bad magic, bad version, bad layout_id, hash
 *     mismatch, offsets past end, count*sizeof overflow;
 *   - and every INTERIOR index: prim.material, node.mesh, node.parent, child spans,
 *     children[i], roots[i], indices[i]. ano_resgfx_scene checks array EXTENTS today and
 *     validates NOTHING INSIDE THEM -- a baked block from a pack turns each of those into an
 *     out-of-bounds read primitive handed straight to the renderer. Runs under ASan. */

#include <stdio.h>

int main(void)
{
    printf("block framing: the hostile-input battery\n");
    printf("  PENDING: no oracle yet -- owned by W5, M11.\n");
>>>>>>> block-b1-base
    return 0;
}
