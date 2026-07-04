/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Collation sort benchmark: the "player clicks sort by name on a 6000-item inventory"
 * scenario, multi-script names (Latin+accents, Cyrillic, Greek, kana, Han, Runic
 * decorations) with realistic duplicate stacks and "Potion of ..." tie families.
 *
 * Series (full-sort latency, percentiles over shuffled reps):
 *   - qsort+collate      : the streaming comparator loop (the old anostr_sort)
 *   - anostr_sort        : prefix keys + radix + tie resolution, keys rebuilt per call
 *   - anostr_sort presrt : the already-sorted early-out (re-click on a sorted list)
 *   - anostr_sort_idx    : permutation only, inventory structs never move
 *   - sym_sort warm      : interned symbols, cached keys -- zero string bytes read
 *   - qsort bytes        : anostr_compare byte order, the meaningless-order floor
 * plus one-shot rows (sym_sort cold = cache build) and a throughput section for
 * anostr_collate_prefix, anostr_collate_key, replace_all, cull, and rune_sort.
 *
 * Deterministic (fixed seeds). argv[1] overrides the item count (default 6000).
 * Built always so it cannot rot; DISABLED in ctest -- run by hand from a -O3 build
 * (build.bat 7 / build.sh 7). Prints a table, exits 0 (1 only if results are WRONG:
 * every timed sort is verified against the streaming comparator's order once). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings_utf.h"
#include "templates/bench.h"
#include "templates/rng.h"

enum { REPS = 40 };

// ---------------------------------------------------------------------------------------------
// Inventory name generator. Distinct pool ~ count/4, drawn with replacement, so stacks
// duplicate like a real inventory and the intern table has real work to collapse.

static const char *base_ascii[] = {
    "Sword", "Shield", "Potion", "Scroll", "Amulet", "Ring", "Helm", "Bow", "Dagger",
    "Staff", "Tome", "Gauntlet", "Cloak", "Boots", "Talisman", "Wand", "Orb", "Axe",
};
static const char *mod_ascii[] = {
    "Healing", "Might", "Frost", "Flame", "Shadows", "Light", "the Bear", "the Fox",
    "Venom", "Storms", "Ruin", "Glory", "Mana", "Speed", "Warding", "the Deep",
};
static const char *base_other[] = {
    "\xC3\x89p\xC3\xA9""e",                                     // Épée
    "M\xC3\xBCller", "\xC3\x85sgard", "\xC8\x98tefan",
    "\xD0\x9C\xD0\xB5\xD1\x87",                                 // Меч
    "\xD0\xA9\xD0\xB8\xD1\x82",                                 // Щит
    "\xCE\x9E\xCE\xAF\xCF\x86\xCE\xBF\xCF\x82",                 // Ξίφος
    "\xE3\x81\x8B\xE3\x81\x9F\xE3\x81\xAA",                     // かたな
    "\xE3\x82\xAB\xE3\x82\xBF\xE3\x83\x8A",                     // カタナ
    "\xE5\x89\xA3", "\xE6\xBC\xA2\xE5\x89\xA3",                 // 剣 漢剣
};

static anostr_t make_name(mi_heap_t *heap, test_rng *rng)
{
    char buf[96];
    uint32_t kind = rng_below(rng, 10);
    if (kind < 5) {         // "Potion of Healing" -- shared-prefix tie families
        snprintf(buf, sizeof buf, "%s of %s",
                 base_ascii[rng_below(rng, sizeof base_ascii / sizeof base_ascii[0])],
                 mod_ascii[rng_below(rng, sizeof mod_ascii / sizeof mod_ascii[0])]);
    } else if (kind < 7) {  // "+3 Frost Dagger"
        snprintf(buf, sizeof buf, "+%u %s %s", 1 + rng_below(rng, 9),
                 mod_ascii[rng_below(rng, sizeof mod_ascii / sizeof mod_ascii[0])],
                 base_ascii[rng_below(rng, sizeof base_ascii / sizeof base_ascii[0])]);
    } else if (kind < 9) {  // non-Latin scripts
        snprintf(buf, sizeof buf, "%s %s",
                 base_other[rng_below(rng, sizeof base_other / sizeof base_other[0])],
                 mod_ascii[rng_below(rng, sizeof mod_ascii / sizeof mod_ascii[0])]);
    } else {                // Diablo-style decorative runes around a Latin core
        snprintf(buf, sizeof buf, "\xE1\x9A\xA6%s\xE1\x9A\xA6",
                 base_ascii[rng_below(rng, sizeof base_ascii / sizeof base_ascii[0])]);
    }
    return anostr_from_cstr(heap, buf);
}

static void shuffle_items(anostr_t *v, size_t n, test_rng *rng)
{
    for (size_t k = n - 1; k > 0; k--) {
        size_t j = rng_below(rng, (uint32_t)k + 1);
        anostr_t tmp = v[k];
        v[k] = v[j];
        v[j] = tmp;
    }
}

static void shuffle_syms(anostr_sym *v, size_t n, test_rng *rng)
{
    for (size_t k = n - 1; k > 0; k--) {
        size_t j = rng_below(rng, (uint32_t)k + 1);
        anostr_sym tmp = v[k];
        v[k] = v[j];
        v[j] = tmp;
    }
}

static int cmp_collate(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

static int cmp_bytes(const void *a, const void *b)
{
    return anostr_compare(*(const anostr_t *)a, *(const anostr_t *)b);
}

static int verify_sorted(const anostr_t *v, size_t n, const char *what)
{
    for (size_t k = 1; k < n; k++) {
        if (anostr_collate(v[k - 1], v[k]) > 0) {
            printf("WRONG RESULT: %s out of order at %zu: \"%.*s\" > \"%.*s\"\n",
                   what, k, anostr_fmt(v[k - 1]), anostr_fmt(v[k]));
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    size_t count = 6000;
    if (argc > 1) count = strtoul(argv[1], NULL, 10);
    if (count < 2) count = 2;

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    // Build the inventory: pool of distinct names, drawn with replacement.
    test_rng rng = rng_make(0x1BADB002u);
    size_t poolN = count / 4 > 0 ? count / 4 : 1;
    anostr_t *pool  = mi_heap_malloc(heap, poolN * sizeof *pool);
    anostr_t *items = mi_heap_malloc(heap, count * sizeof *items);
    anostr_t *work  = mi_heap_malloc(heap, count * sizeof *work);
    uint32_t *order = mi_heap_malloc(heap, count * sizeof *order);
    uint64_t *ticks = mi_heap_malloc(heap, REPS * sizeof *ticks);
    if (pool == NULL || items == NULL || work == NULL || order == NULL || ticks == NULL) {
        printf("FAIL: corpus alloc\n");
        return 1;
    }
    for (size_t k = 0; k < poolN; k++)
        pool[k] = make_name(heap, &rng);
    for (size_t k = 0; k < count; k++)
        items[k] = pool[rng_below(&rng, (uint32_t)poolN)];

    size_t totalBytes = 0;
    for (size_t k = 0; k < count; k++)
        totalBytes += anostr_len(items[k]);
    printf("inventory: %zu items, %zu distinct names, %zu bytes of text, %d reps/series\n\n",
           count, poolN, totalBytes, REPS);

    int wrong = 0;
    bench_lat lat;
    bench_stats s;
    bench_lat_header();

    // qsort + streaming collate: the comparator-loop baseline.
    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        memcpy(work, items, count * sizeof *work);
        shuffle_items(work, count, &rng);
        uint64_t t0 = bench_begin();
        qsort(work, count, sizeof work[0], cmp_collate);
        bench_lat_add(&lat, bench_end(t0));
    }
    wrong += verify_sorted(work, count, "qsort+collate");
    s = bench_lat_stats(&lat);
    bench_lat_row("qsort+collate (baseline)", s);

    // anostr_sort: prefix keys + radix + ties, keys rebuilt every call.
    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        memcpy(work, items, count * sizeof *work);
        shuffle_items(work, count, &rng);
        uint64_t t0 = bench_begin();
        anostr_sort(work, count);
        bench_lat_add(&lat, bench_end(t0));
    }
    wrong += verify_sorted(work, count, "anostr_sort");
    s = bench_lat_stats(&lat);
    bench_lat_row("anostr_sort", s);

    // Re-sorting an already sorted list: the early-out path.
    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        uint64_t t0 = bench_begin();
        anostr_sort(work, count);
        bench_lat_add(&lat, bench_end(t0));
    }
    wrong += verify_sorted(work, count, "anostr_sort presorted");
    s = bench_lat_stats(&lat);
    bench_lat_row("anostr_sort (presorted)", s);

    // Permutation only: inventory structs would never move.
    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        memcpy(work, items, count * sizeof *work);
        shuffle_items(work, count, &rng);
        uint64_t t0 = bench_begin();
        anostr_sort_idx(work, count, order);
        bench_lat_add(&lat, bench_end(t0));
    }
    for (size_t k = 1; k < count && wrong == 0; k++)
        if (anostr_collate(work[order[k - 1]], work[order[k]]) > 0) {
            printf("WRONG RESULT: sort_idx out of order at %zu\n", k);
            wrong++;
        }
    s = bench_lat_stats(&lat);
    bench_lat_row("anostr_sort_idx", s);

    // Interned symbols: cold call builds the per-symbol key cache, warm calls sort
    // integers without ever touching string bytes.
    anostr_intern_t *tbl = anostr_intern_make(heap);
    anostr_sym *syms = mi_heap_malloc(heap, count * sizeof *syms);
    if (tbl == NULL || syms == NULL) { printf("FAIL: intern alloc\n"); return 1; }
    for (size_t k = 0; k < count; k++)
        syms[k] = anostr_intern(tbl, items[k]);

    uint64_t t0 = bench_begin();
    anostr_sym_sort(tbl, syms, count);
    uint64_t coldNs = ano_ticks_to_ns(bench_end(t0));

    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        shuffle_syms(syms, count, &rng);
        uint64_t t1 = bench_begin();
        anostr_sym_sort(tbl, syms, count);
        bench_lat_add(&lat, bench_end(t1));
    }
    for (size_t k = 1; k < count && wrong == 0; k++)
        if (anostr_collate(anostr_sym_str(tbl, syms[k - 1]), anostr_sym_str(tbl, syms[k])) > 0) {
            printf("WRONG RESULT: sym_sort out of order at %zu\n", k);
            wrong++;
        }
    s = bench_lat_stats(&lat);
    bench_lat_row("anostr_sym_sort (warm)", s);

    // Byte-order floor: what a sort costs when the order means nothing to a human.
    bench_lat_init(&lat, ticks, REPS);
    for (int r = 0; r < REPS; r++) {
        memcpy(work, items, count * sizeof *work);
        shuffle_items(work, count, &rng);
        uint64_t t1 = bench_begin();
        qsort(work, count, sizeof work[0], cmp_bytes);
        bench_lat_add(&lat, bench_end(t1));
    }
    s = bench_lat_stats(&lat);
    bench_lat_row("qsort bytes (floor)", s);

    printf("\nanostr_sym_sort (cold, builds key cache): %llu ns once\n",
           (unsigned long long)coldNs);

    // Throughput: the per-string primitives and the transforms.
    t0 = bench_begin();
    uint64_t sink = 0;
    for (size_t k = 0; k < count; k++)
        sink += anostr_collate_prefix(items[k]);
    uint64_t ns = ano_ticks_to_ns(bench_end(t0));
    printf("collate_prefix: %.0f ns/string (%.1f M strings/s)%s\n",
           (double)ns / (double)count, bench_ops_per_sec(count, ns) / 1e6,
           sink == 42 ? "!" : "");   // keep the loop un-elidable

    t0 = bench_begin();
    for (size_t k = 0; k < count; k++)
        (void)anostr_collate_key(heap, items[k]);
    ns = ano_ticks_to_ns(bench_end(t0));
    printf("collate_key:    %.0f ns/string (full 3-level key + tiebreak)\n",
           (double)ns / (double)count);

    // A ~1 MB document for the byte transforms.
    anostr_builder_t db = anostr_builder_make(heap, 1u << 20);
    while (db.len < (1u << 20))
        anostr_builder_append_cstr(&db, "the quick brown fox, jumps over \xC3\xA9 lazy dogs; ");
    anostr_t doc = anostr_freeze(&db);

    t0 = bench_begin();
    anostr_t rep = anostr_replace_all(heap, doc, anostr_lit("the"), anostr_lit("THE"));
    ns = ano_ticks_to_ns(bench_end(t0));
    printf("replace_all:    %.2f GB/s over %u bytes (%zu-byte result)\n",
           (double)anostr_len(doc) / (double)ns, doc.len, anostr_len(rep));

    t0 = bench_begin();
    anostr_t culled = anostr_cull(heap, doc, ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT);
    ns = ano_ticks_to_ns(bench_end(t0));
    printf("cull ws+punct:  %.2f GB/s over %u bytes (%zu-byte result)\n",
           (double)anostr_len(doc) / (double)ns, doc.len, anostr_len(culled));

    t0 = bench_begin();
    for (size_t k = 0; k < count; k++)
        (void)anostr_rune_sort(heap, items[k]);
    ns = ano_ticks_to_ns(bench_end(t0));
    printf("rune_sort:      %.0f ns/string\n", (double)ns / (double)count);

    return wrong == 0 ? 0 : 1;
}
