/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for collation sort, replace_all, cull, rune_sort.
 * Exit 0 == pass; failures print what broke. argv[1] scales soak iterations. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings_utf.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static int sign(int v) { return v < 0 ? -1 : v > 0; }

// Corpus: shipped scripts plus fallback and degenerate shapes.

static const char *corpus_cstr[] = {
    "", "a", "A", "apple", "Apple", "applesauce", "Zebra",
    "\xC3\x84pfel",                                     // Äpfel
    "resume", "r\xC3\xA9sum\xC3\xA9",                   // résumé
    "M\xC3\xBCller", "Muster", "M\xC3\xBCnchen",
    "New York", "Newark",
    "\xC3\x85lesund", "alesund", "\xC3\x96rebro", "Oslo",
    "\xC8\x98tefan", "stefan",                          // Ștefan (s-comma decomposes)
    "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", // Москва
    "\xD0\xB5", "\xD1\x91", "\xD0\xB6",                 // е ё ж
    "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1",         // Αθήνα
    "\xE3\x81\x82", "\xE3\x82\xA2", "\xE3\x81\x84",     // あ ア い
    "\xE6\x9D\xB1\xE4\xBA\xAC", "\xE6\xBC\xA2",         // 東京 漢 (implicit weights)
    "\xE1\x9A\xA0\xE1\x9A\xA2",                         // ᚠᚢ runic
    "\xF0\x9D\x95\xA8",                                 // 𝕨 SMP, 2-CE implicit
    "\xF0\x9F\x98\x80",                                 // 😀
    "\x01", "\x01" "a",                                 // ignorables: zero prefix key
    "Potion of Healing", "Potion of Mana", "Potion of Might",
    "  spaced  ", "punct!!!", "\xEF\xBF\xBD",           // genuine U+FFFD
    "\x80" "broken",                                    // malformed lead byte
    // Mixed scripts: placed by first rune, tails break ties.
    "\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun", // あの Bjørn's Agda Gun
    "\xE3\x81\x82\xE3\x81\xAA",                         // あな
    "\xE3\x82\xA2\xE3\x83\x8E",                         // アノ
    "\xE3\x81\x82\xE3\x81\xAE Ansgar",                  // あの Ansgar
    "Bj\xC3\xB8rn", "Bj\xC3\xB8rn's", "Bj\xC3\xB8rns", "Bjorn", "Bjorna",
    "z\xD0\xB6", "\xD0\xB6z",                           // zж жz
    "\xE1\x9A\xA6Sword\xE1\x9A\xA6",                    // ᚦSwordᚦ decorative runes
};
enum { CORPUS_N = sizeof corpus_cstr / sizeof corpus_cstr[0] };

static anostr_t corpus[CORPUS_N];

static void corpus_init(void)
{
    for (size_t k = 0; k < CORPUS_N; k++)
        corpus[k] = anostr_view(corpus_cstr[k], strlen(corpus_cstr[k]));
}

// Random multi-script rune pool.
static anorune_t rng_rune(test_rng *rng)
{
    switch (rng_below(rng, 12)) {
    case 0: case 1: case 2: case 3:
            return 'a' + rng_below(rng, 26);
    case 4: return 'A' + rng_below(rng, 26);
    case 5: return (anorune_t[]){ 0xE9, 0xC4, 0xF6, 0xE5, 0x101, 0x219 }[rng_below(rng, 6)];
    case 6: return 0x430 + rng_below(rng, 32);          // Cyrillic а..я
    case 7: return 0x3B1 + rng_below(rng, 17);          // Greek α..ρ
    case 8: return 0x3042 + rng_below(rng, 20);         // hiragana
    case 9: return 0x4E00 + rng_below(rng, 64);         // Han (implicit)
    case 10: return (anorune_t[]){ ' ', '!', '.', 0x1, 0x301, 0x16A0 }[rng_below(rng, 6)];
    default: return (anorune_t[]){ 0x1D568, 0x1F600 }[rng_below(rng, 2)];
    }
}

static anostr_t rng_str(test_rng *rng, mi_heap_t *heap, uint32_t maxRunes)
{
    uint32_t n = rng_below(rng, maxRunes + 1);
    anostr_builder_t b = anostr_builder_make(heap, 0);
    for (uint32_t k = 0; k < n; k++)
        anostr_builder_append_rune(&b, rng_rune(rng));
    return anostr_freeze(&b);
}

// Oracle: qsort(anostr_collate).

static int oracle_cmp(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

// Two sort paths must match elementwise; ties are byte-identical.
static bool check_against_oracle(const anostr_t *items, size_t n, const char *what,
                                 mi_heap_t *heap)
{
    anostr_t *mine = mi_heap_malloc(heap, n * sizeof *mine);
    anostr_t *ref  = mi_heap_malloc(heap, n * sizeof *ref);
    uint32_t *order = mi_heap_malloc(heap, n * sizeof *order);
    if (mine == NULL || ref == NULL || order == NULL) {
        printf("FAIL: %s: oracle scratch alloc\n", what);
        failures++;
        return false;
    }
    memcpy(mine, items, n * sizeof *mine);
    memcpy(ref, items, n * sizeof *ref);

    anostr_sort(mine, n);
    qsort(ref, n, sizeof ref[0], oracle_cmp);
    for (size_t i = 0; i < n; i++) {
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: anostr_sort[%zu] = \"%.*s\", oracle \"%.*s\"\n",
                   what, i, anostr_fmt(mine[i]), anostr_fmt(ref[i]));
            failures++;
            return false;
        }
    }

    // sort_idx: valid permutation, stable on ties.
    anostr_sort_idx(items, n, order);
    uint8_t *seen = mi_heap_zalloc(heap, n);
    for (size_t i = 0; i < n; i++) {
        if (order[i] >= n || seen[order[i]]) {
            printf("FAIL: %s: order is not a permutation at %zu\n", what, i);
            failures++;
            return false;
        }
        seen[order[i]] = 1;
        if (!anostr_eq(items[order[i]], ref[i])) {
            printf("FAIL: %s: sort_idx[%zu] gathers \"%.*s\", oracle \"%.*s\"\n",
                   what, i, anostr_fmt(items[order[i]]), anostr_fmt(ref[i]));
            failures++;
            return false;
        }
        if (i > 0 && anostr_collate(items[order[i - 1]], items[order[i]]) == 0 &&
            order[i - 1] >= order[i]) {
            printf("FAIL: %s: unstable on equal strings at %zu\n", what, i);
            failures++;
            return false;
        }
    }

    // Presorted early-out returns identical sequence.
    anostr_sort(mine, n);
    for (size_t i = 0; i < n; i++) {
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: re-sort of sorted input diverged at %zu\n", what, i);
            failures++;
            return false;
        }
    }
    return true;
}


static void test_collate_prefix(void)
{
    for (size_t a = 0; a < CORPUS_N; a++) {
        for (size_t b = 0; b < CORPUS_N; b++) {
            uint64_t ka = anostr_collate_prefix(corpus[a]);
            uint64_t kb = anostr_collate_prefix(corpus[b]);
            if (ka == kb)
                continue;   // equal keys promise nothing
            int kc = ka < kb ? -1 : 1;
            if (kc != sign(anostr_collate(corpus[a], corpus[b]))) {
                printf("FAIL: prefix key disagrees with collate: \"%.*s\" vs \"%.*s\"\n",
                       anostr_fmt(corpus[a]), anostr_fmt(corpus[b]));
                failures++;
            }
        }
    }

    CHECK(anostr_collate_prefix(anostr_empty()) == 0, "empty string keys to zero");
    CHECK(anostr_collate_prefix(anostr_lit("\x01")) == 0, "ignorable-only keys to zero");
    CHECK(anostr_collate_prefix(anostr_lit("a")) != 0, "a letter keys nonzero");
    // Case/accents secondary/tertiary: same prefix key.
    CHECK(anostr_collate_prefix(anostr_lit("apple")) == anostr_collate_prefix(anostr_lit("Apple")),
          "case does not move the primary prefix");
    CHECK(anostr_collate_prefix(anostr_lit("resume")) ==
          anostr_collate_prefix(anostr_lit("r\xC3\xA9sum\xC3\xA9")),
          "accents do not move the primary prefix");
    CHECK(anostr_collate_prefix(anostr_lit("apple")) < anostr_collate_prefix(anostr_lit("banana")),
          "primary order shows in the key");
}

static void test_collate_key(mi_heap_t *heap)
{
    for (size_t a = 0; a < CORPUS_N; a++) {
        anostr_t ka = anostr_collate_key(heap, corpus[a]);
        for (size_t b = 0; b < CORPUS_N; b++) {
            anostr_t kb = anostr_collate_key(heap, corpus[b]);
            if (sign(anostr_compare(ka, kb)) != sign(anostr_collate(corpus[a], corpus[b]))) {
                printf("FAIL: full key order breaks on \"%.*s\" vs \"%.*s\"\n",
                       anostr_fmt(corpus[a]), anostr_fmt(corpus[b]));
                failures++;
                return;
            }
        }
    }
}

// Multi-script: first rune places the string; later runes break ties.
static void test_mixed_scripts(void)
{
    // Script ladder: one first-rune per band, ascending.
    static const struct { const char *s; const char *what; } ladder[] = {
        { " ",                 "space" },
        { "!",                 "ASCII punctuation" },
        { "5",                 "digit" },
        { "a",                 "Latin a" },
        { "z",                 "Latin z" },
        { "\xCE\xB1",          "Greek alpha" },
        { "\xD0\xB0",          "Cyrillic a" },
        { "\xE1\x9A\xA0",      "Runic fehu" },
        { "\xE3\x81\x82",      "hiragana a" },
        { "\xD7\x90",          "Hebrew alef (unlisted -> implicit, cp order)" },
        { "\xE6\xBC\xA2",      "Han (implicit)" },
        { "\xF0\x9F\x98\x80",  "emoji (SMP implicit)" },
    };
    for (size_t k = 1; k < sizeof ladder / sizeof ladder[0]; k++) {
        anostr_t lo = anostr_view(ladder[k - 1].s, strlen(ladder[k - 1].s));
        anostr_t hi = anostr_view(ladder[k].s, strlen(ladder[k].s));
        if (anostr_collate(lo, hi) >= 0) {
            printf("FAIL: script ladder: %s should sort before %s\n",
                   ladder[k - 1].what, ladder[k].what);
            failures++;
        }
    }

    // Showcase: あの Bjørn's Agda Gun (kana first).
    anostr_t mixed = anostr_lit("\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun");
    CHECK(anostr_collate(anostr_lit("Zebra"), mixed) < 0, "after every Latin string");
    CHECK(anostr_collate(anostr_lit("\xCE\xA9\xCE\xBC\xCE\xB5\xCE\xB3\xCE\xB1"), mixed) < 0,
          "after Greek (Ωμεγα)");
    CHECK(anostr_collate(anostr_lit("\xD0\xAF\xD1\x85\xD1\x82"), mixed) < 0,
          "after Cyrillic (Яхт)");
    CHECK(anostr_collate(anostr_lit("\xE1\x9A\xA6Sword\xE1\x9A\xA6"), mixed) < 0,
          "after Runic-decorated names");
    CHECK(anostr_collate(mixed, anostr_lit("\xE6\xBC\xA2")) < 0, "before Han");

    // Within kana band: な < の < は.
    CHECK(anostr_collate(anostr_lit("\xE3\x81\x82\xE3\x81\xAA"), mixed) < 0, "ana < ano...");
    CHECK(anostr_collate(mixed, anostr_lit("\xE3\x81\x82\xE3\x81\xAF")) < 0, "ano... < aha");
    // Katakana アノ primary-equal to あの, sorts before.
    CHECK(anostr_collate(anostr_lit("\xE3\x82\xA2\xE3\x83\x8E"), mixed) < 0,
          "katakana prefix sorts before the longer mixed string");

    // Past kana prefix, Latin rules mid-string.
    CHECK(anostr_collate(anostr_lit("\xE3\x81\x82\xE3\x81\xAE Ansgar"), mixed) < 0,
          "ano Ansgar < ano Bjorn's Agda Gun");
    CHECK(anostr_collate(mixed, anostr_lit("\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Sword")) < 0,
          "the last word still breaks the tie (Gun < Sword)");

    // ø groups with o (secondary), not after z.
    CHECK(anostr_collate(anostr_lit("Bjorn"), anostr_lit("Bj\xC3\xB8rn")) < 0,
          "Bjorn < Bjørn at the secondary level");
    CHECK(anostr_collate(anostr_lit("Bj\xC3\xB8rn"), anostr_lit("Bjorna")) < 0,
          "primary (length) outranks the stroke");
    CHECK(anostr_eq_base(anostr_lit("Bj\xC3\xB8rn"), anostr_lit("bjorn")), "Bjørn ==base bjorn");
    // Cross-script starts_base, ASCII keyboard.
    CHECK(anostr_starts_base(anostr_lit("\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun"),
                             anostr_lit("\xE3\x81\x82\xE3\x81\xAE bjorn")),
          "starts_base crosses scripts, case- and stroke-blind");
    // Mid-word punct significant: Bjørn's < Bjørns.
    CHECK(anostr_collate(anostr_lit("Bj\xC3\xB8rn's"), anostr_lit("Bj\xC3\xB8rns")) < 0,
          "apostrophe sorts before letters");

    // Exact expected mixed-script order.
    anostr_t items[] = {
        anostr_lit("\xE6\xBC\xA2\xE5\xAD\x97"),                             // 漢字
        anostr_lit("\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun"),
        anostr_lit("Agda Gun"),
        anostr_lit("\xE3\x82\xA2\xE3\x83\x8E"),                             // アノ
        anostr_lit("\xE1\x9A\xA0\xE1\x9A\xA2"),                             // ᚠᚢ
        anostr_lit("\xD0\x96\xD1\x83\xD0\xBA"),                             // Жук
        anostr_lit("\xE3\x81\x82\xE3\x81\xAA"),                             // あな
        anostr_lit("Bj\xC3\xB8rn"),
        anostr_lit("\xCE\xB1\xCE\xB2"),                                     // αβ
        anostr_lit("Zebra"),
    };
    static const char *expect[] = {
        "Agda Gun", "Bj\xC3\xB8rn", "Zebra",
        "\xCE\xB1\xCE\xB2",
        "\xD0\x96\xD1\x83\xD0\xBA",
        "\xE1\x9A\xA0\xE1\x9A\xA2",
        "\xE3\x81\x82\xE3\x81\xAA",
        "\xE3\x82\xA2\xE3\x83\x8E",
        "\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun",
        "\xE6\xBC\xA2\xE5\xAD\x97",
    };
    size_t n = sizeof items / sizeof items[0];
    anostr_sort(items, n);
    for (size_t k = 0; k < n; k++) {
        if (!anostr_eq(items[k], anostr_view(expect[k], strlen(expect[k])))) {
            printf("FAIL: mixed sort slot %zu is \"%.*s\"\n", k, anostr_fmt(items[k]));
            failures++;
        }
    }
}

static void test_sort_corpus(mi_heap_t *heap)
{
    // Corpus plus duplicates for stability.
    anostr_t items[CORPUS_N * 2];
    for (size_t k = 0; k < CORPUS_N; k++) {
        items[k] = corpus[k];
        items[CORPUS_N + k] = corpus[CORPUS_N - 1 - k];
    }
    check_against_oracle(items, CORPUS_N * 2, "corpus sort", heap);

    // Degenerate counts are no-ops.
    anostr_sort(NULL, 5);
    anostr_sort(items, 0);
    anostr_sort(items, 1);
    uint32_t one = 77;
    anostr_sort_idx(items, 1, &one);
    CHECK(one == 0, "count 1 order is identity");
    anostr_sort_idx(NULL, 3, (uint32_t[]){ 9, 9, 9 });
}

// "Potion of ..." family forces bulk full-key tie path.
static void test_tie_family(mi_heap_t *heap)
{
    enum { N = 64 };
    anostr_t items[N];
    static char names[N][32];
    test_rng rng = rng_make(0xF00DF00Du);
    for (size_t k = 0; k < N; k++) {
        snprintf(names[k], sizeof names[k], "Potion of %c%c%c",
                 'A' + (char)rng_below(&rng, 26), 'a' + (char)rng_below(&rng, 26),
                 'a' + (char)rng_below(&rng, 26));
        items[k] = anostr_view(names[k], strlen(names[k]));
    }
    uint64_t k0 = anostr_collate_prefix(items[0]);
    for (size_t k = 1; k < N; k++)
        if (anostr_collate_prefix(items[k]) != k0) {
            printf("FAIL: tie family does not share a prefix key\n");
            failures++;
            return;
        }
    check_against_oracle(items, N, "tie family", heap);
}

static void test_sym_sort(mi_heap_t *heap)
{
    anostr_intern_t *t = anostr_intern_make(heap);
    CHECK(t != NULL, "intern table");
    if (t == NULL)
        return;

    // Intern corpus with duplicates, shuffle symbols.
    enum { N = CORPUS_N * 2 };
    anostr_sym syms[N];
    anostr_t   strs[N];
    test_rng rng = rng_make(0x5EEDBA5Eu);
    for (size_t k = 0; k < N; k++) {
        anostr_t s = corpus[rng_below(&rng, CORPUS_N)];
        syms[k] = anostr_intern(t, s);
        CHECK(syms[k] != ANOSTR_SYM_NONE, "intern succeeds");
    }

    // Cold builds cache; warm must agree.
    for (int round = 0; round < 2; round++) {
        anostr_sym_sort(t, syms, N);
        for (size_t k = 0; k < N; k++)
            strs[k] = anostr_sym_str(t, syms[k]);
        for (size_t k = 1; k < N; k++) {
            if (anostr_collate(strs[k - 1], strs[k]) > 0) {
                printf("FAIL: sym_sort round %d out of order at %zu\n", round, k);
                failures++;
                return;
            }
        }
        // Reshuffle for the warm round.
        for (size_t k = N - 1; k > 0; k--) {
            size_t j = rng_below(&rng, (uint32_t)k + 1);
            anostr_sym tmp = syms[k];
            syms[k] = syms[j];
            syms[j] = tmp;
        }
    }

    // New symbols extend the cache watermark.
    anostr_sym late = anostr_intern(t, anostr_lit("zzz late entry"));
    anostr_sym mid = anostr_intern(t, anostr_lit("apple"));
    anostr_sym trio[3] = { late, mid, 0x7FFFFFFF };        // an out-of-range symbol
    anostr_sym_sort(t, trio, 3);
    CHECK(trio[0] == 0x7FFFFFFF, "out-of-range symbol sorts first (empty string)");
    CHECK(trio[1] == mid && trio[2] == late, "apple then zzz");
}

static void test_replace_all(mi_heap_t *heap)
{
    // Non-overlapping LTR: "aaa" has one "aa".
    CHECK(anostr_eq(anostr_replace_all(heap, anostr_lit("aaa"), anostr_lit("aa"), anostr_lit("b")),
                    anostr_lit("ba")), "non-overlapping matches");
    // Grow, shrink, same size.
    CHECK(anostr_eq(anostr_replace_all(heap, anostr_lit("a-b-c"), anostr_lit("-"), anostr_lit("--")),
                    anostr_lit("a--b--c")), "grow");
    CHECK(anostr_eq(anostr_replace_all(heap, anostr_lit("xaxbxc"), anostr_lit("x"), anostr_empty()),
                    anostr_lit("abc")), "shrink (delete)");
    CHECK(anostr_eq(anostr_replace_all(heap, anostr_lit("dog dog"), anostr_lit("dog"), anostr_lit("cat")),
                    anostr_lit("cat cat")), "same size");
    CHECK(anostr_is_empty(anostr_replace_all(heap, anostr_lit("xxx"), anostr_lit("x"), anostr_empty())),
          "everything deleted is the empty string");

    // UTF-8 needle: é -> e.
    CHECK(anostr_eq(anostr_replace_all(heap, anostr_lit("r\xC3\xA9sum\xC3\xA9"),
                                       anostr_lit("\xC3\xA9"), anostr_lit("e")),
                    anostr_lit("resume")), "UTF-8 needle");

    // No match: same value, same backing.
    anostr_t big = anostr_lit("a long string with no needle in it");
    anostr_t out = anostr_replace_all(heap, big, anostr_lit("zebra"), anostr_lit("!"));
    CHECK(anostr_eq(out, big) && anostr_bytes(&out) == anostr_bytes(&big),
          "no match returns the same backing");
    out = anostr_replace_all(heap, big, anostr_empty(), anostr_lit("!"));
    CHECK(anostr_eq(out, big) && anostr_bytes(&out) == anostr_bytes(&big),
          "empty needle is identity");

    // Long source shrink under inline cap -> fresh inline.
    out = anostr_replace_all(heap, anostr_lit("xxxxxxxxxxxxxxxxxxxxxxab"),
                             anostr_lit("x"), anostr_empty());
    CHECK(anostr_eq(out, anostr_lit("ab")) && anostr_is_inline(out), "shrinks to inline");

    // Randomized vs naive rebuild.
    test_rng rng = rng_make(0x5E91ACEDu);
    for (int it = 0; it < 300; it++) {
        char sb[64], nb[4], rb[6];
        size_t sn = rng_below(&rng, sizeof sb + 1);
        size_t nn = 1 + rng_below(&rng, sizeof nb);
        size_t rn = rng_below(&rng, sizeof rb + 1);
        for (size_t k = 0; k < sn; k++) sb[k] = (char)('a' + rng_below(&rng, 3));
        for (size_t k = 0; k < nn; k++) nb[k] = (char)('a' + rng_below(&rng, 3));
        for (size_t k = 0; k < rn; k++) rb[k] = (char)('a' + rng_below(&rng, 4));
        anostr_t s = anostr_view(sb, sn), a = anostr_view(nb, nn), r = anostr_view(rb, rn);

        anostr_builder_t bd = anostr_builder_make(heap, 0);
        for (size_t i = 0; i < sn; ) {
            if (i + nn <= sn && memcmp(sb + i, nb, nn) == 0) {
                anostr_builder_append(&bd, rb, rn);
                i += nn;
            } else {
                anostr_builder_append(&bd, sb + i, 1);
                i++;
            }
        }
        anostr_t want = anostr_freeze(&bd);
        anostr_t got = anostr_replace_all(heap, s, a, r);
        if (!anostr_eq(got, want)) {
            printf("FAIL: replace soak it=%d: \"%.*s\" [%.*s -> %.*s] gave \"%.*s\" want \"%.*s\"\n",
                   it, anostr_fmt(s), anostr_fmt(a), anostr_fmt(r),
                   anostr_fmt(got), anostr_fmt(want));
            failures++;
            return;
        }
    }
}

static anostr_t naive_cull(mi_heap_t *heap, anostr_t s, uint32_t classes)
{
    anostr_builder_t b = anostr_builder_make(heap, 0);
    for (size_t i = 0; i < anostr_len(s); ) {
        size_t at = i;
        anorune_t r = anostr_rune_next(s, &i);
        bool cull = ((classes & ANOSTR_CULL_WHITESPACE) && anorune_is_whitespace(r)) ||
                    ((classes & ANOSTR_CULL_PUNCT) && anorune_is_punct(r)) ||
                    ((classes & ANOSTR_CULL_MARK) && anorune_is_mark(r));
        if (!cull)
            anostr_builder_append(&b, anostr_bytes(&s) + at, i - at);
    }
    return anostr_freeze(&b);
}

static void test_cull(mi_heap_t *heap)
{
    // Whitespace: ASCII, NBSP, U+3000.
    CHECK(anostr_eq(anostr_cull(heap, anostr_lit(" a\tb\nc \xC2\xA0 d \xE3\x80\x80 e "),
                                ANOSTR_CULL_WHITESPACE),
                    anostr_lit("abcde")), "whitespace cull across widths");

    // Punct is P* only; symbols/digits survive.
    CHECK(anostr_eq(anostr_cull(heap, anostr_lit("+5 Sword!, ($10) \xE2\x82\xAC. \xE3\x80\x81"),
                                ANOSTR_CULL_PUNCT),
                    anostr_lit("+5 Sword $10 \xE2\x82\xAC ")),
          "punct cull keeps symbols");

    // Marks: combining acute culled, precomposed é kept.
    CHECK(anostr_eq(anostr_cull(heap, anostr_lit("e\xCC\x81 \xC3\xA9"), ANOSTR_CULL_MARK),
                    anostr_lit("e \xC3\xA9")), "marks cull, precomposed stays");

    // Combined classes; total cull -> empty.
    CHECK(anostr_eq(anostr_cull(heap, anostr_lit(" !.\t,"), ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT),
                    anostr_empty()), "combined cull to empty");

    // Nothing culled: same value, no alloc.
    anostr_t clean = anostr_lit("NothingToCullInThisLongString");
    anostr_t out = anostr_cull(heap, clean, ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT);
    CHECK(anostr_eq(out, clean) && anostr_bytes(&out) == anostr_bytes(&clean),
          "no-op cull returns the same backing");
    out = anostr_cull(heap, anostr_lit(" x "), 0);
    CHECK(anostr_eq(out, anostr_lit(" x ")), "zero class mask is identity");

    // Malformed bytes never culled.
    CHECK(anostr_eq(anostr_cull(heap, anostr_lit("\x80 \x80"), ANOSTR_CULL_WHITESPACE),
                    anostr_lit("\x80\x80")), "malformed bytes pass through");

    // Randomized vs naive rune loop.
    test_rng rng = rng_make(0xCA11ED00u);
    for (int it = 0; it < 300; it++) {
        anostr_t s = rng_str(&rng, heap, 24);
        uint32_t classes = 1 + rng_below(&rng, 7);
        anostr_t got = anostr_cull(heap, s, classes);
        anostr_t want = naive_cull(heap, s, classes);
        if (!anostr_eq(got, want)) {
            printf("FAIL: cull soak it=%d classes=%u on \"%.*s\": got \"%.*s\" want \"%.*s\"\n",
                   it, classes, anostr_fmt(s), anostr_fmt(got), anostr_fmt(want));
            failures++;
            return;
        }
    }
}

static int rune_cmp(const void *a, const void *b)
{
    anorune_t x = *(const anorune_t *)a, y = *(const anorune_t *)b;
    return x < y ? -1 : (x > y);
}

static void test_rune_sort(mi_heap_t *heap)
{
    CHECK(anostr_eq(anostr_rune_sort(heap, anostr_lit("dcba")), anostr_lit("abcd")),
          "ASCII counting sort");
    CHECK(anostr_eq(anostr_rune_sort(heap, anostr_lit("b\xC3\xA9" "a")),
                    anostr_lit("ab\xC3\xA9")), "mixed width sorts by code point");
    CHECK(anostr_eq(anostr_rune_sort(heap, anostr_empty()), anostr_empty()), "empty in, empty out");
    // Malformed -> U+FFFD; output longer and valid.
    anostr_t fixed = anostr_rune_sort(heap, anostr_lit("\x80" "a"));
    CHECK(anostr_eq(fixed, anostr_lit("a\xEF\xBF\xBD")), "malformed sorts as U+FFFD");

    // Anagram key: code point order is case-sensitive.
    anostr_t k1 = anostr_rune_sort(heap, anostr_cull(heap, anostr_lit("s i l e n t!"),
                                                     ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT));
    anostr_t k2 = anostr_rune_sort(heap, anostr_cull(heap, anostr_lit("l.i.s.t.e.n"),
                                                     ANOSTR_CULL_WHITESPACE | ANOSTR_CULL_PUNCT));
    CHECK(anostr_eq(k1, k2), "anagram keys agree");

    // Randomized: multiset preserved vs qsort of runes.
    test_rng rng = rng_make(0x50F7ED00u);
    for (int it = 0; it < 300; it++) {
        anostr_t s = rng_str(&rng, heap, 32);
        anostr_t sorted = anostr_rune_sort(heap, s);
        size_t n = 0, m = 0;
        anorune_t *want = anostr_to_utf32(heap, s, &n);
        anorune_t *got  = anostr_to_utf32(heap, sorted, &m);
        qsort(want, n, sizeof want[0], rune_cmp);
        if (m != n || memcmp(want, got, n * sizeof want[0]) != 0) {
            printf("FAIL: rune_sort soak it=%d on \"%.*s\"\n", it, anostr_fmt(s));
            failures++;
            return;
        }
        if (!anostr_eq(anostr_rune_sort(heap, sorted), sorted)) {
            printf("FAIL: rune_sort not idempotent it=%d\n", it);
            failures++;
            return;
        }
        mi_free(want);
        mi_free(got);
    }
}

static void test_is_punct(void)
{
    static const anorune_t yes[] = { '!', ',', '.', ':', ';', '?', '(', '[', '_', '-',
                                     0x2014, 0x201C, 0x3001, 0x3002, 0xFF01 };
    for (size_t k = 0; k < sizeof yes / sizeof yes[0]; k++)
        if (!anorune_is_punct(yes[k])) {
            printf("FAIL: U+%04X should be punctuation\n", yes[k]);
            failures++;
        }
    static const anorune_t no[] = { '+', '$', '<', '=', '`', '^', '|', '~', 'a', '7',
                                    ' ', 0x20AC, 0x1F600, 0x5BE };    // Hebrew maqaf: unlisted
    for (size_t k = 0; k < sizeof no / sizeof no[0]; k++)
        if (anorune_is_punct(no[k])) {
            printf("FAIL: U+%04X should NOT be punctuation\n", no[k]);
            failures++;
        }
}

// Random multi-script lists must agree both ways. argv[1] scales.
static void soak(mi_heap_t *heap, uint32_t iterations)
{
    test_rng rng = rng_make(0xC0117A7Eu);
    enum { N = 200 };
    anostr_t items[N];
    for (uint32_t it = 0; it < iterations; it++) {
        mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
        if (scratch == NULL) {
            printf("FAIL: soak heap\n");
            failures++;
            return;
        }
        for (size_t k = 0; k < N; k++)
            items[k] = rng_str(&rng, scratch, 16);
        if (!check_against_oracle(items, N, "soak", scratch))
            return;
        (void)heap;
    }
}

int main(int argc, char **argv)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    corpus_init();
    test_collate_prefix();
    test_collate_key(heap);
    test_mixed_scripts();
    test_sort_corpus(heap);
    test_tie_family(heap);
    test_sym_sort(heap);
    test_replace_all(heap);
    test_cull(heap);
    test_rune_sort(heap);
    test_is_punct();

    uint32_t iterations = 30;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    soak(heap, iterations);

    if (failures == 0) { printf("anotest_strings_sort: all checks passed\n"); return 0; }
    printf("anotest_strings_sort: %d check(s) failed\n", failures);
    return 1;
}
