/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Bench: find/replace_all shapes + cull/rune_sort. Sanity-checked; exits nonzero if wrong.
 * DISABLED in ctest; run from -O3. argv[1] scales reps. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings_utf.h"
#include "templates/bench.h"
#include "templates/rng.h"

enum { DOC_FIND = 4u << 20, DOC_REPL = 1u << 20, REPS_DEFAULT = 30 };

static int g_reps = REPS_DEFAULT;
static int g_wrong = 0;

#define WRONG(msg) do { printf("WRONG RESULT: %s\n", (msg)); g_wrong++; } while (0)

// Mixed-UTF-8 document of varied sentences.
static anostr_t make_doc(mi_heap_t *heap, size_t bytes, test_rng *rng)
{
    static const char *frags[] = {
        "the quick brown fox jumps over the lazy dog. ",
        "pack my box with five dozen liquor jugs; ",
        "M\xC3\xBCller pays \xE2\x82\xAC" "5 for r\xC3\xA9sum\xC3\xA9 advice, ",
        "\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn buys another Agda Gun! ",
        "sphinx of black quartz judge my vow: ",
    };
    anostr_builder_t b = anostr_builder_make(heap, (uint32_t)bytes + 64);
    while (b.len < bytes)
        anostr_builder_append_cstr(&b, frags[rng_below(rng, sizeof frags / sizeof frags[0])]);
    return anostr_freeze(&b);
}

// Timed series: op() x g_reps, print row, return p50 GB/s.
typedef size_t (*op_fn_t)(mi_heap_t *scratch, const void *ctx);

static double run_series(const char *label, size_t bytesPerOp, op_fn_t op, const void *ctx,
                         uint64_t *ticks)
{
    bench_lat lat;
    bench_lat_init(&lat, ticks, (size_t)g_reps);
    size_t sink = 0;
    for (int r = 0; r < g_reps; r++) {
        mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();   // outputs die per rep
        uint64_t t0 = bench_begin();
        sink += op(scratch, ctx);
        bench_lat_add(&lat, bench_end(t0));
    }
    bench_stats s = bench_lat_stats(&lat);
    bench_lat_row(label, s);
    if (sink == 1)  // consume the result so the timed call cannot be elided
        printf("!");
    return s.p50_ns ? (double)bytesPerOp / (double)s.p50_ns : 0.0;
}

/* find shapes. ctx = document; needles baked per function. */

static anostr_t g_docFind, g_docRepl;

static size_t op_find_hit_end(mi_heap_t *h, const void *ctx)
{
    (void)h; (void)ctx;
    size_t at = anostr_find(g_docFind, anostr_lit("XMARKSTHESPOT"), 0);
    if (at == ANOSTR_NPOS) WRONG("find: planted needle not found");
    return at;
}

static size_t op_find_miss(mi_heap_t *h, const void *ctx)
{
    (void)h; (void)ctx;
    size_t at = anostr_find(g_docFind, anostr_lit("QQWWZZYY"), 0);
    if (at != ANOSTR_NPOS) WRONG("find: absent needle found");
    return at;
}

static size_t op_find_common_first(mi_heap_t *h, const void *ctx)
{
    (void)h; (void)ctx;   // ' the ' first byte = space: densest candidate byte there is
    return anostr_find(g_docFind, anostr_lit(" the lazy dog. XMARKS"), 0);
}

static size_t op_find_rare_first(mi_heap_t *h, const void *ctx)
{
    (void)h; (void)ctx;   // 'X' appears only in the planted tail
    return anostr_find(g_docFind, anostr_lit("XMARKS"), 0);
}

static size_t op_find_long_needle(mi_heap_t *h, const void *ctx)
{
    (void)h; (void)ctx;
    return anostr_find(g_docFind, anostr_lit("sphinx of black quartz judge my vow: XMARKS"), 0);
}

/* replace shapes over the 1 MiB doc. */

static size_t op_repl_same_sparse(mi_heap_t *h, const void *ctx)
{
    (void)ctx;   // "sphinx" ~ 1 per 200 bytes
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("sphinx"), anostr_lit("SPHINX"));
    if (out.len != g_docRepl.len) WRONG("replace same-size changed length");
    return out.len;
}

static size_t op_repl_same_dense(mi_heap_t *h, const void *ctx)
{
    (void)ctx;   // 'e' is everywhere
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("e"), anostr_lit("3"));
    if (out.len != g_docRepl.len) WRONG("replace dense changed length");
    return out.len;
}

static size_t op_repl_grow(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("dog"), anostr_lit("direwolf"));
    if (out.len <= g_docRepl.len) WRONG("replace grow did not grow");
    return out.len;
}

static size_t op_repl_shrink(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("the "), anostr_lit("a "));
    if (out.len >= g_docRepl.len) WRONG("replace shrink did not shrink");
    return out.len;
}

static size_t op_repl_delete_spaces(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit(" "), anostr_empty());
    if (out.len >= g_docRepl.len) WRONG("delete spaces did not shrink");
    return out.len;
}

static size_t op_repl_utf8(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("\xC3\xA9"), anostr_lit("e"));
    if (!anostr_utf8_valid(out)) WRONG("UTF-8 needle replace produced invalid UTF-8");
    return out.len;
}

static size_t op_repl_nomatch(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_replace_all(h, g_docRepl, anostr_lit("QQWWZZYY"), anostr_lit("!"));
    if (anostr_bytes(&out) != anostr_bytes(&g_docRepl)) WRONG("no-match did not return the same backing");
    return out.len;
}

/* cull + rune_sort shapes. */

static size_t op_cull_ws_punct(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_cull(h, g_docRepl, ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT);
    if (out.len >= g_docRepl.len) WRONG("cull removed nothing from a spaced document");
    return out.len;
}

static anostr_t g_docClean;     // no whitespace/punct at all: the scan-only path

static size_t op_cull_noop(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_cull(h, g_docClean, ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT);
    if (anostr_bytes(&out) != anostr_bytes(&g_docClean)) WRONG("no-op cull did not return the same backing");
    return out.len;
}

static anostr_t g_page4k;

static size_t op_rune_sort_4k(mi_heap_t *h, const void *ctx)
{
    (void)ctx;
    anostr_t out = anostr_rune_sort(h, g_page4k);
    if (out.len < g_page4k.len) WRONG("rune_sort lost bytes");
    return out.len;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0) g_reps = v;
    }

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }
    uint64_t *ticks = mi_heap_malloc(heap, (size_t)g_reps * sizeof *ticks);
    if (ticks == NULL) { printf("FAIL: tick buffer\n"); return 1; }

    test_rng rng = rng_make(0x0B5E55EDu);

    // 4 MiB find haystack; unique needle in last fragment.
    {
        anostr_builder_t b = anostr_builder_make(heap, DOC_FIND + 128);
        anostr_t body = make_doc(heap, DOC_FIND, &rng);
        anostr_builder_append_str(&b, body);
        anostr_builder_append_cstr(&b, " the lazy dog. XMARKSTHESPOT");
        g_docFind = anostr_freeze(&b);
    }
    g_docRepl = make_doc(heap, DOC_REPL, &rng);

    // Clean doc + 4 KiB mixed page for rune_sort.
    {
        anostr_builder_t b = anostr_builder_make(heap, DOC_REPL + 64);
        while (b.len < DOC_REPL)
            anostr_builder_append_cstr(&b, "NothingCullableHereJustLettersAndM\xC3\xBCnchen");
        g_docClean = anostr_freeze(&b);
        anostr_t page = make_doc(heap, 4096, &rng);
        g_page4k = anostr_slice(page, 0, 4096);
    }

    printf("string ops: find over %u MiB, replace/cull over %u MiB, %d reps/series\n\n",
           DOC_FIND >> 20, DOC_REPL >> 20, g_reps);
    bench_lat_header();

    double gbps;
    gbps = run_series("find: hit at far end", g_docFind.len, op_find_hit_end, NULL, ticks);
    printf("%-28s scan %.2f GB/s\n", "", gbps);
    gbps = run_series("find: miss (full scan)", g_docFind.len, op_find_miss, NULL, ticks);
    printf("%-28s scan %.2f GB/s\n", "", gbps);
    run_series("find: common first byte", g_docFind.len, op_find_common_first, NULL, ticks);
    run_series("find: rare first byte", g_docFind.len, op_find_rare_first, NULL, ticks);
    run_series("find: 44-byte needle", g_docFind.len, op_find_long_needle, NULL, ticks);

    printf("\n");
    gbps = run_series("replace: same-size sparse", g_docRepl.len, op_repl_same_sparse, NULL, ticks);
    printf("%-28s %.2f GB/s\n", "", gbps);
    gbps = run_series("replace: same-size dense", g_docRepl.len, op_repl_same_dense, NULL, ticks);
    printf("%-28s %.2f GB/s\n", "", gbps);
    run_series("replace: grow (dog->direwolf)", g_docRepl.len, op_repl_grow, NULL, ticks);
    run_series("replace: shrink (the->a)", g_docRepl.len, op_repl_shrink, NULL, ticks);
    run_series("replace: delete all spaces", g_docRepl.len, op_repl_delete_spaces, NULL, ticks);
    run_series("replace: UTF-8 needle e-acute", g_docRepl.len, op_repl_utf8, NULL, ticks);
    gbps = run_series("replace: no match (identity)", g_docRepl.len, op_repl_nomatch, NULL, ticks);
    printf("%-28s %.2f GB/s (count pass only, zero alloc)\n", "", gbps);

    printf("\n");
    gbps = run_series("cull: ws+punct, 1 MiB", g_docRepl.len, op_cull_ws_punct, NULL, ticks);
    printf("%-28s %.2f GB/s\n", "", gbps);
    gbps = run_series("cull: no-op (clean doc)", g_docClean.len, op_cull_noop, NULL, ticks);
    printf("%-28s %.2f GB/s (scan only, same backing out)\n", "", gbps);
    run_series("rune_sort: 4 KiB mixed page", g_page4k.len, op_rune_sort_4k, NULL, ticks);

    if (g_wrong != 0) {
        printf("\n%d wrong result(s)\n", g_wrong);
        return 1;
    }
    return 0;
}
