/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Property fuzzer + smoketest for anostr_t. Cross-kind agreement is the central property.
 * smoketest: every public symbol once. fuzz: soak on per-iter scratch heap.
 * SID is compile-time only. "splice" = slice + concat vs naive oracle.
 * Exit 0 == pass. argv[1] scales soak. */

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

/* Independent oracles. */

// FNV-1a reference: twin of anostr_hash / anostr_hash32.
static uint64_t fnv64(const void *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
static uint32_t fnv32(const void *p, size_t n)
{
    uint32_t h = 0x811c9dc5u;
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x01000193u; }
    return h;
}

// Naive memmem: first needle at/after from.
static size_t naive_find(const char *s, size_t sn, const char *nd, size_t nn, size_t from)
{
    if (from > sn) from = sn;
    if (nn == 0) return from;
    if (nn > sn) return ANOSTR_NPOS;
    for (size_t i = from; i + nn <= sn; i++)
        if (memcmp(s + i, nd, nn) == 0) return i;
    return ANOSTR_NPOS;
}

// Naive LTR non-overlapping replace; returns result length.
static size_t naive_replace(const char *s, size_t sn, const char *nd, size_t nn,
                            const char *rp, size_t rn, char *out)
{
    size_t o = 0;
    if (nn == 0) { memcpy(out, s, sn); return sn; }
    for (size_t i = 0; i < sn; ) {
        if (i + nn <= sn && memcmp(s + i, nd, nn) == 0) { memcpy(out + o, rp, rn); o += rn; i += nn; }
        else out[o++] = s[i++];
    }
    return o;
}

// Naive splice: remove [a,b), insert r. SUT = slice + concat.
static size_t naive_splice(const char *s, size_t sn, size_t a, size_t b,
                           const char *r, size_t rn, char *out)
{
    if (a > sn) a = sn;
    if (b > sn) b = sn;
    if (a > b) a = b;
    size_t o = 0;
    memcpy(out + o, s, a); o += a;
    memcpy(out + o, r, rn); o += rn;
    memcpy(out + o, s + b, sn - b); o += sn - b;
    return o;
}

// Base fold for constructed accented runes (hardcoded, not DUCET).
static anorune_t base_fold(anorune_t r)
{
    r = anorune_to_lower(r);
    switch (r) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0x101: return 'a';
    case 0xE7: return 'c';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xF1: return 'n';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return 'o';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0x219: case 0x161: return 's';
    default: return r;
    }
}

/* Random content generators. */

// Random rune: ASCII, accents, Cyrillic/Greek/kana, Han+SMP, ignorables.
static anorune_t rng_rune(test_rng *rng)
{
    switch (rng_below(rng, 12)) {
    case 0: case 1: case 2: case 3: return 'a' + rng_below(rng, 26);
    case 4: return 'A' + rng_below(rng, 26);
    case 5: return (anorune_t[]){ 0xE9, 0xC4, 0xF6, 0xE5, 0x101, 0x219 }[rng_below(rng, 6)];
    case 6: return 0x430 + rng_below(rng, 32);
    case 7: return 0x3B1 + rng_below(rng, 17);
    case 8: return 0x3042 + rng_below(rng, 20);
    case 9: return 0x4E00 + rng_below(rng, 64);
    case 10: return (anorune_t[]){ ' ', '!', '.', 0x1, 0x301, 0x16A0 }[rng_below(rng, 6)];
    default: return (anorune_t[]){ 0x1D568, 0x1F600 }[rng_below(rng, 2)];
    }
}

// Valid UTF-8 of up to maxRunes runes.
static anostr_t rng_str(test_rng *rng, mi_heap_t *heap, uint32_t maxRunes)
{
    uint32_t n = rng_below(rng, maxRunes + 1);
    anostr_builder_t b = anostr_builder_make(heap, 0);
    for (uint32_t k = 0; k < n; k++)
        anostr_builder_append_rune(&b, rng_rune(rng));
    return anostr_freeze(&b);
}

// Arbitrary bytes (NUL + malformed UTF-8) into buf.
static size_t rng_bytes(test_rng *rng, char *buf, size_t maxLen)
{
    size_t n = rng_below(rng, (uint32_t)maxLen + 1);
    for (size_t i = 0; i < n; i++) buf[i] = (char)rng_next(rng);
    return n;
}

/* Sort oracle, reused verbatim from the sort exemplar. */

static int rune_cmp(const void *a, const void *b)
{
    anorune_t x = *(const anorune_t *)a, y = *(const anorune_t *)b;
    return x < y ? -1 : (x > y);
}

static int oracle_cmp(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

static bool check_against_oracle(const anostr_t *items, size_t n, const char *what, mi_heap_t *heap)
{
    anostr_t *mine = mi_heap_malloc(heap, n * sizeof *mine);
    anostr_t *ref  = mi_heap_malloc(heap, n * sizeof *ref);
    uint32_t *order = mi_heap_malloc(heap, n * sizeof *order);
    if (mine == NULL || ref == NULL || order == NULL) {
        printf("FAIL: %s: oracle scratch alloc\n", what); failures++; return false;
    }
    memcpy(mine, items, n * sizeof *mine);
    memcpy(ref, items, n * sizeof *ref);

    anostr_sort(mine, n);
    qsort(ref, n, sizeof ref[0], oracle_cmp);
    for (size_t i = 0; i < n; i++)
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: anostr_sort[%zu] diverged from oracle\n", what, i); failures++; return false;
        }

    anostr_sort_idx(items, n, order);
    uint8_t *seen = mi_heap_zalloc(heap, n);
    for (size_t i = 0; i < n; i++) {
        if (order[i] >= n || seen[order[i]]) {
            printf("FAIL: %s: order is not a permutation at %zu\n", what, i); failures++; return false;
        }
        seen[order[i]] = 1;
        if (!anostr_eq(items[order[i]], ref[i])) {
            printf("FAIL: %s: sort_idx[%zu] gathers wrong element\n", what, i); failures++; return false;
        }
        if (i > 0 && anostr_collate(items[order[i - 1]], items[order[i]]) == 0 && order[i - 1] >= order[i]) {
            printf("FAIL: %s: unstable on equal strings at %zu\n", what, i); failures++; return false;
        }
    }

    anostr_sort(mine, n);   // presorted early-out returns the identical sequence (idempotent)
    for (size_t i = 0; i < n; i++)
        if (!anostr_eq(mine[i], ref[i])) {
            printf("FAIL: %s: re-sort diverged at %zu\n", what, i); failures++; return false;
        }
    return true;
}

/* Compile-time SID cross-checks (LITERAL inputs only; never a runtime operand). */

// Published FNV-1a vectors vs anoptic_strings.h.
static_assert(ANOSTR_SID("") == 0xcbf29ce484222325ULL, "SID FNV64 empty");
static_assert(ANOSTR_SID("a") == 0xaf63dc4c8601ec8cULL, "SID FNV64 a");
static_assert(ANOSTR_SID("foobar") == 0x85944171f73967e8ULL, "SID FNV64 foobar");
static_assert(ANOSTR_SID32("") == 0x811c9dc5u, "SID FNV32 empty");
static_assert(ANOSTR_SID32("a") == 0xe40c292cu, "SID FNV32 a");
static_assert(ANOSTR_SID32("foobar") == 0xbf9cf968u, "SID FNV32 foobar");
static_assert(ANOSTR_SID("foo") != ANOSTR_SID("bar"), "distinct literals distinct ids");
static_assert(ANOSTR_SID("a\0b") == 0xe5d29919042666b2ULL, "SID counts the embedded NUL");

// 128-byte literal at ANOSTR_SID_MAX. Overlong is a negative build test (out of band).
#define SID128_LIT \
    "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" \
    "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa" "aaaaaaaaaaaaaaaa"
static_assert(sizeof(SID128_LIT) - 1 == 128, "128-byte literal");
static_assert(ANOSTR_SID(SID128_LIT) != 0, "128-byte SID is an ICE");

// SID as static init and enum.
static const anostr_sid   g_sid_init   = ANOSTR_SID("level_loaded");
static const anostr_sid32 g_sid32_init = ANOSTR_SID32("level_loaded");
enum { SID_ENUM = (int)(ANOSTR_SID32("tick") & 0x7FFFu) };

// SID as enum ICE and array size.
enum { SID_ARR_N = (int)(1u + (ANOSTR_SID32("player_spawn") % 13u)) };
static char g_sid_arr[SID_ARR_N];

// SID as a case label.
static uint32_t dispatch_sid(anostr_sid id)
{
    switch (id) {
    case ANOSTR_SID("tick"):         return 0;
    case ANOSTR_SID("player_spawn"): return 1;
    case ANOSTR_SID("level_loaded"): return 2;
    default:                         return UINT32_MAX;
    }
}

/* Cross-kind agreement: one logical content built through every backing must agree everywhere. */

// Decode the single rune in buf via rune_next.
static anorune_t decode1(const char *buf, size_t n)
{
    anostr_t s = anostr_view(buf, n);
    size_t i = 0;
    return anostr_rune_next(s, &i);
}

// nulfree gates from_cstr backing.
static void check_kinds(mi_heap_t *h, anostr_intern_t *it, const char *buf, size_t n,
                        bool nulfree, const char *what)
{
    anostr_t v[8];
    size_t k = 0;
    v[k++] = anostr_from(h, buf, n);                                   // arena
    v[k++] = anostr_view(buf, n);                                     // borrow
    v[k++] = anostr_keep(h, anostr_view(buf, n));                     // owned copy
    anostr_builder_t b = anostr_builder_make(h, 0);                  // builder-frozen
    anostr_builder_append(&b, buf, n);
    v[k++] = anostr_freeze(&b);
    size_t half = n / 2;                                             // concat of two halves
    v[k++] = anostr_concat(h, anostr_view(buf, half), anostr_view(buf + half, n - half));
    v[k++] = anostr_slice(anostr_keep(h, anostr_view(buf, n)), 0, n); // full-length slice-borrow
    v[k++] = anostr_dedupe(it, anostr_view(buf, n));                  // canonical
    if (nulfree) {
        char *cs = mi_heap_malloc(h, n + 1);
        memcpy(cs, buf, n); cs[n] = 0;
        v[k++] = anostr_from_cstr(h, cs);
    }

    for (size_t i = 0; i < k; i++) {
        CHECK(anostr_len(v[i]) == n, what);
        CHECK(anostr_is_empty(v[i]) == (n == 0), what);
        CHECK(anostr_is_inline(v[i]) == (n <= ANOSTR_INLINE_CAP), what);   // I2
        CHECK(anostr_eq(v[i], v[0]), what);
        CHECK(anostr_eq(v[0], v[i]), what);                                // symmetric
        CHECK(anostr_compare(v[i], v[0]) == 0, what);                      // eq <=> compare 0
        CHECK(anostr_collate(v[i], v[0]) == 0, what);
        CHECK(anostr_collate_prefix(v[i]) == anostr_collate_prefix(v[0]), what);
        CHECK(anostr_hash(v[i]) == anostr_hash(v[0]), what);
        CHECK(anostr_hash32(v[i]) == anostr_hash32(v[0]), what);
        CHECK(memcmp(anostr_bytes(&v[i]), buf, n) == 0, what);
        if (n <= ANOSTR_INLINE_CAP)                                        // Regime A: bit-identical
            CHECK(memcmp(&v[i], &v[0], sizeof(anostr_t)) == 0, what);
        // Result depends on content, not construction path.
        if (n > 0) {
            anostr_t nd = anostr_view(buf, 1);
            anostr_t tail = anostr_lit("|tail");
            CHECK(anostr_find(v[i], nd, 0) == anostr_find(v[0], nd, 0), what);
            CHECK(anostr_eq(anostr_slice(v[i], 1, n), anostr_slice(v[0], 1, n)), what);
            // split/replace/concat/join depend on content, not kind.
            CHECK(anostr_eq(anostr_concat(h, v[i], tail), anostr_concat(h, v[0], tail)), what);
            CHECK(anostr_eq(anostr_replace_all(h, v[i], nd, anostr_lit("Q")),
                            anostr_replace_all(h, v[0], nd, anostr_lit("Q"))), what);
            anostr_t jpi[2] = { v[i], tail }, jp0[2] = { v[0], tail };
            CHECK(anostr_eq(anostr_join(h, anostr_lit(","), jpi, 2),
                            anostr_join(h, anostr_lit(","), jp0, 2)), what);
            anostr_t pi, p0; bool ni, n0;                    // split sequences agree across kinds
            anostr_split_t si = anostr_split(v[i], nd), s0 = anostr_split(v[0], nd);
            do { ni = anostr_split_next(&si, &pi); n0 = anostr_split_next(&s0, &p0);
                 CHECK(ni == n0 && (!ni || anostr_eq(pi, p0)), what); } while (ni && n0);
        }
    }

    // Deduped twins share backing; ptr short-circuit and memcmp agree.
    anostr_t d1 = anostr_dedupe(it, anostr_view(buf, n));
    anostr_t d2 = anostr_dedupe(it, anostr_from(h, buf, n));
    CHECK(anostr_eq(d1, d2), what);
    if (n > ANOSTR_INLINE_CAP)
        CHECK(anostr_bytes(&d1) == anostr_bytes(&d2), what);               // shared canonical ptr
}

/* Smoketest: every public symbol once. */

static void smoketest(mi_heap_t *h)
{
    const char *lit = "the quick brown fox";     // long
    anostr_t empty = anostr_empty();
    anostr_t sml   = anostr_view("abc", 3);       // inline
    anostr_t lng   = anostr_view(lit, strlen(lit));

    // Construction & accessors.
    CHECK(anostr_is_empty(empty) && anostr_len(empty) == 0, "empty is empty");
    CHECK(memcmp(&empty, &(anostr_t){0}, sizeof(anostr_t)) == 0, "empty is a zeroed value");
    CHECK(anostr_is_inline(sml) && !anostr_is_inline(lng), "inline rule follows length");
    anostr_t fr = anostr_from(h, lit, strlen(lit));
    anostr_t fc = anostr_from_cstr(h, lit);
    CHECK(anostr_eq(fr, lng) && anostr_eq(fc, lng), "from / from_cstr round-trip");
    CHECK(anostr_eq(anostr_lit("abc"), sml), "lit == view over sizeof-1");
    CHECK(memcmp(anostr_bytes(&sml), "abc", 3) == 0, "bytes readable");
    char fbuf[64];
    int fn = snprintf(fbuf, sizeof fbuf, "%.*s", anostr_fmt(lng));
    CHECK(fn == (int)strlen(lit) && memcmp(fbuf, lit, fn) == 0, "fmt feeds %.*s");

    // Totality: NULL bytes/oversized/NULL-heap long -> empty.
    CHECK(anostr_is_empty(anostr_from(h, NULL, 5)), "NULL bytes -> empty");
    CHECK(anostr_is_empty(anostr_from(h, lit, (size_t)UINT32_MAX + 1)), "len > UINT32_MAX -> empty");
    CHECK(anostr_is_empty(anostr_from_cstr(h, NULL)), "NULL cstr -> empty");
    CHECK(anostr_is_empty(anostr_from(NULL, lit, strlen(lit))), "NULL-heap long path -> empty");
    CHECK(anostr_eq(anostr_from(NULL, "abc", 3), sml), "NULL-heap inline path still works");

    // Comparison, hash, SID twin.
    CHECK(anostr_eq(sml, sml) && anostr_compare(sml, sml) == 0, "reflexive");
    CHECK(sign(anostr_compare(anostr_lit("apple"), anostr_lit("banana"))) < 0, "lexicographic");
    CHECK(anostr_hash(lng) == fnv64(lit, strlen(lit)), "hash == FNV-1a-64");
    CHECK(anostr_hash32(lng) == fnv32(lit, strlen(lit)), "hash32 == FNV-1a-32");
    CHECK(ANOSTR_SID("player_spawn") == anostr_hash(anostr_lit("player_spawn")), "SID twin of hash");
    CHECK(ANOSTR_SID32("player_spawn") == anostr_hash32(anostr_lit("player_spawn")), "SID32 twin");
    CHECK(dispatch_sid(anostr_hash(anostr_lit("level_loaded"))) == 2, "SID case label dispatch");
    CHECK(g_sid_init == anostr_hash(anostr_lit("level_loaded")), "SID static initializer");
    CHECK(g_sid32_init == anostr_hash32(anostr_lit("level_loaded")), "SID32 static initializer");
    g_sid_arr[0] = 1;
    CHECK(sizeof g_sid_arr == (size_t)SID_ARR_N && g_sid_arr[0] == 1, "SID array-size ICE");
    CHECK((int)(ANOSTR_SID32("tick") & 0x7FFFu) == SID_ENUM, "SID enum ICE");

    // Slice + splice (composed).
    CHECK(anostr_eq(anostr_slice(lng, 4, 9), anostr_lit("quick")), "slice substring");
    CHECK(anostr_eq(anostr_slice(lng, 100, 3), empty), "slice clamps to empty");
    anostr_t spliced = anostr_concat(h, anostr_slice(lng, 0, 4),
                                     anostr_concat(h, anostr_lit("slow "), anostr_slice(lng, 4, strlen(lit))));
    CHECK(anostr_eq(spliced, anostr_lit("the slow quick brown fox")), "splice = slice + concat");

    // Find / replace.
    CHECK(anostr_find(lng, anostr_lit("brown"), 0) == 10, "find index");
    CHECK(anostr_find(lng, anostr_lit("zzz"), 0) == ANOSTR_NPOS, "find absent -> NPOS");
    CHECK(anostr_find(anostr_lit("aaa"), anostr_lit("aa"), 0) == 0, "find overlap boundary");
    anostr_t rep = anostr_replace_all(h, lng, anostr_lit("quick"), anostr_lit("lazy"));
    CHECK(anostr_eq(rep, anostr_lit("the lazy brown fox")), "replace_all");
    anostr_t rnm = anostr_replace_all(h, lng, anostr_lit("zzz"), anostr_lit("!"));
    CHECK(anostr_eq(rnm, lng) && anostr_bytes(&rnm) == anostr_bytes(&lng), "no-match same backing");

    // Concat / join / split.
    CHECK(anostr_eq(anostr_concat(h, anostr_lit("foo"), anostr_lit("bar")), anostr_lit("foobar")), "concat");
    CHECK(anostr_eq(anostr_concat(h, lng, empty), lng), "empty is concat identity");
    anostr_t parts[3] = { anostr_lit("a"), anostr_lit("bb"), anostr_lit("ccc") };
    CHECK(anostr_eq(anostr_join(h, anostr_lit(","), parts, 3), anostr_lit("a,bb,ccc")), "join");
    CHECK(anostr_is_empty(anostr_join(h, anostr_lit(","), parts, 0)), "join count 0 -> empty");
    CHECK(anostr_is_empty(anostr_join(h, anostr_lit(","), NULL, 3)), "join NULL parts -> empty");
    anostr_split_t sp = anostr_split(anostr_lit("a,,b"), anostr_lit(","));
    anostr_t piece; int pc = 0;
    while (anostr_split_next(&sp, &piece)) pc++;
    CHECK(pc == 3, "split yields empty pieces");
    CHECK(!anostr_split_next(&sp, &piece), "split_next false once exhausted");

    // Intern / dedupe / keep / to_cstr.
    anostr_intern_t *t = anostr_intern_make(h);
    CHECK(t != NULL, "intern_make");
    anostr_sym s1 = anostr_intern(t, anostr_lit("alpha"));
    anostr_sym s2 = anostr_intern(t, anostr_from_cstr(h, "alpha"));
    CHECK(s1 == s2 && s1 != ANOSTR_SYM_NONE, "intern: equal content, same symbol");
    CHECK(anostr_intern_find(t, anostr_lit("alpha")) == s1, "intern_find hit");
    CHECK(anostr_intern_find(t, anostr_lit("missing")) == ANOSTR_SYM_NONE, "intern_find miss");
    CHECK(anostr_eq(anostr_sym_str(t, s1), anostr_lit("alpha")), "sym_str canonical value");
    CHECK(anostr_is_empty(anostr_sym_str(t, ANOSTR_SYM_NONE)), "sym_str NONE -> empty");
    CHECK(anostr_intern_count(t) == 1, "intern_count distinct");
    anostr_t dd = anostr_dedupe(t, anostr_lit("alpha"));
    CHECK(anostr_eq(dd, anostr_lit("alpha")), "dedupe canonical value");
    CHECK(anostr_eq(anostr_keep(h, lng), lng) && !anostr_is_inline(anostr_keep(h, lng)), "keep long");
    anostr_t kept_inline = anostr_keep(h, sml);
    CHECK(memcmp(&sml, &kept_inline, sizeof(anostr_t)) == 0, "keep inline bit-equal");
    char *cs = anostr_to_cstr(h, lng);
    CHECK(cs != NULL && strcmp(cs, lit) == 0, "to_cstr round-trip");
    CHECK(anostr_eq(anostr_from_cstr(h, anostr_to_cstr(h, sml)), sml), "to_cstr / from_cstr round-trip");

    // Builder: append family, appendf, append_rune, freeze, discard.
    anostr_builder_t bd = anostr_builder_make(h, 4);
    CHECK(anostr_builder_append(&bd, "he", 2) == 0, "append");
    CHECK(anostr_builder_append_str(&bd, anostr_lit("llo ")) == 0, "append_str");
    CHECK(anostr_builder_append_cstr(&bd, "wor") == 0, "append_cstr");
    CHECK(anostr_builder_append_rune(&bd, 'l') == 0, "append_rune");
    CHECK(anostr_builder_appendf(&bd, "d %d", 42) == 0, "appendf");
    anostr_t built = anostr_freeze(&bd);
    CHECK(anostr_eq(built, anostr_lit("hello world 42")), "freeze == accumulated bytes");
    CHECK(anostr_builder_append(&bd, "x", 1) == -1, "append after freeze -> -1");
    anostr_builder_t discardable = anostr_builder_make(h, 8);
    anostr_builder_append(&discardable, "junk", 4);
    anostr_builder_discard(&discardable);
    CHECK(anostr_builder_append(&discardable, "x", 1) == -1, "append after discard -> -1");
    CHECK(anostr_is_empty(anostr_freeze(&discardable)), "freeze after discard -> empty");

    // Builder I1: spoof len near ceiling, append fails, builder intact.
    anostr_builder_t ov = anostr_builder_make(h, 8);
    anostr_builder_append(&ov, "ab", 2);
    uint32_t saved = ov.len;
    ov.len = UINT32_MAX - 2;
    CHECK(anostr_builder_append(&ov, "abc", 3) == -1, "append overflow -> -1");
    CHECK(ov.len == UINT32_MAX - 2, "builder intact after overflow");
    ov.len = saved;
    anostr_builder_discard(&ov);

    // UTF: iteration, count, validity, encode.
    anostr_t uni = anostr_lit("a\xC3\xA9\xE4\xBA\xAC");      // a é 京
    size_t idx = 0;
    CHECK(anostr_rune_next(uni, &idx) == 'a', "rune_next ascii");
    CHECK(anostr_rune_next(uni, &idx) == 0xE9, "rune_next é");
    CHECK(anostr_rune_prev(uni, &idx) == 0xE9, "rune_prev é");
    CHECK(anostr_rune_count(uni) == 3, "rune_count");
    CHECK(anostr_utf8_valid(uni) && !anostr_utf8_valid(anostr_lit("\x80")), "utf8_valid");
    char eb[4];
    int el = anorune_encode(eb, 0x4EAC);
    CHECK(el == 3 && decode1(eb, 3) == 0x4EAC, "encode round-trip");
    CHECK(anorune_encode(eb, 0xD800) == 3 && decode1(eb, 3) == ANORUNE_REPLACEMENT, "surrogate -> U+FFFD");

    // Rune classification & case.
    CHECK(anorune_to_upper('a') == 'A' && anorune_to_lower('Z') == 'z', "ascii case");
    CHECK(anorune_to_upper(0x3B1) == 0x391, "Greek upper");
    CHECK(anorune_is_letter('a') && !anorune_is_letter('5'), "is_letter");
    CHECK(anorune_is_digit('7') && !anorune_is_digit('a'), "is_digit");
    CHECK(anorune_is_whitespace(' ') && anorune_is_whitespace(0x3000), "is_whitespace");
    CHECK(anorune_is_mark(0x301) && !anorune_is_mark(0xE9), "is_mark");
    CHECK(anorune_is_punct('!') && !anorune_is_punct('+'), "is_punct");

    // Collation, sorts.
    CHECK(sign(anostr_collate(anostr_lit("apple"), anostr_lit("Apple"))) < 0, "collate case");
    CHECK(anostr_collate_prefix(anostr_lit("apple")) == anostr_collate_prefix(anostr_lit("Apple")),
          "prefix key case-blind at primary");
    anostr_t ka = anostr_collate_key(h, anostr_lit("apple")), kb = anostr_collate_key(h, anostr_lit("Apple"));
    CHECK(sign(anostr_compare(ka, kb)) == sign(anostr_collate(anostr_lit("apple"), anostr_lit("Apple"))),
          "collate_key reproduces collate sign");
    anostr_t sitems[3] = { anostr_lit("banana"), anostr_lit("apple"), anostr_lit("cherry") };
    anostr_sort(sitems, 3);
    CHECK(anostr_eq(sitems[0], anostr_lit("apple")), "sort");
    uint32_t sord[3];
    anostr_sort_idx((anostr_t[]){ anostr_lit("b"), anostr_lit("a") }, 2, sord);
    CHECK(sord[0] == 1 && sord[1] == 0, "sort_idx permutation");
    anostr_sym syms[2] = { anostr_intern(t, anostr_lit("zeta")), anostr_intern(t, anostr_lit("beta")) };
    anostr_sym_sort(t, syms, 2);
    CHECK(anostr_eq(anostr_sym_str(t, syms[0]), anostr_lit("beta")), "sym_sort");

    // Base-insensitive.
    CHECK(anostr_eq_base(anostr_lit("\xC3\x85lesund"), anostr_lit("alesund")), "eq_base");
    CHECK(anostr_starts_base(anostr_lit("Bj\xC3\xB8rn"), anostr_lit("bjor")), "starts_base");
    CHECK(anostr_find_base(anostr_lit("caf\xC3\xA9 bar"), anostr_lit("CAFE"), 0) == 0, "find_base");

    // Cull, rune_sort.
    CHECK(anostr_eq(anostr_cull(h, anostr_lit("a b\tc"), ANOSTR_CULL_WHITESPACE), anostr_lit("abc")), "cull");
    CHECK(anostr_eq(anostr_rune_sort(h, anostr_lit("dcba")), anostr_lit("abcd")), "rune_sort");

    // Encoding conversion round-trips.
    size_t u16n = 0, u32n = 0;
    char16_t *u16 = anostr_to_utf16(h, uni, &u16n);
    CHECK(u16 != NULL && anostr_eq(anostr_from_utf16(h, u16, u16n), uni), "utf16 round-trip");
    CHECK(anostr_eq(anostr_from_utf16_cstr(h, u16), uni), "from_utf16_cstr");
    anorune_t *u32 = anostr_to_utf32(h, uni, &u32n);
    CHECK(u32 != NULL && u32n == 3 && anostr_eq(anostr_from_utf32(h, u32, u32n), uni), "utf32 round-trip");

    // Alloc-fail sentinels via NULL-heap long path.
    CHECK(anostr_is_empty(anostr_concat(NULL, lng, lng)), "concat OOM -> empty");
    CHECK(anostr_is_empty(anostr_keep(NULL, lng)), "keep OOM -> empty");
    CHECK(anostr_to_cstr(NULL, lng) == NULL, "to_cstr OOM -> NULL");
    CHECK(anostr_is_empty(anostr_collate_key(NULL, lng)), "collate_key OOM -> empty");
    CHECK(anostr_is_empty(anostr_cull(NULL, anostr_lit("a b c d e f g"), ANOSTR_CULL_WHITESPACE)),
          "cull OOM -> empty");
    CHECK(anostr_is_empty(anostr_rune_sort(NULL, lng)), "rune_sort OOM -> empty");
    CHECK(anostr_to_utf16(NULL, lng, NULL) == NULL, "to_utf16 OOM -> NULL");
    CHECK(anostr_to_utf32(NULL, lng, NULL) == NULL, "to_utf32 OOM -> NULL");
    CHECK(anostr_is_empty(anostr_from_utf16(h, NULL, 4)), "from_utf16 NULL src -> empty");
    CHECK(anostr_is_empty(anostr_from_utf32(h, NULL, 4)), "from_utf32 NULL src -> empty");
}

/* Curated collation, case, and classification cases. */

static void test_collation_cases(mi_heap_t *h)
{
    static const struct { const char *lo, *hi, *what; } pairs[] = {
        { "\xC3\x84pfel", "Zebra", "Apfel < Zebra" },
        { "resume", "r\xC3\xA9sum\xC3\xA9", "resume < resume-accented" },
        { "apple", "Apple", "apple < Apple (case tertiary)" },
        { "\xD0\xB5", "\xD1\x91", "e < yo (Cyrillic)" },
        { "Bjorn", "Bj\xC3\xB8rn", "o groups then orders before oe" },
        { "!", "5", "punct < digit" },
        { "5", "a", "digit < Latin" },
        { "z", "\xCE\xB1", "Latin < Greek" },
        { "\xCE\xB1", "\xD0\xB0", "Greek < Cyrillic" },
        { "\xD0\xB0", "\xE1\x9A\xA0", "Cyrillic < Runic" },
        { "\xE1\x9A\xA0", "\xE3\x81\x82", "Runic < kana" },
        { "\xE3\x81\x82", "\xE6\xBC\xA2", "kana < Han (implicit)" },
    };
    for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
        anostr_t lo = anostr_view(pairs[k].lo, strlen(pairs[k].lo));
        anostr_t hi = anostr_view(pairs[k].hi, strlen(pairs[k].hi));
        CHECK(anostr_collate(lo, hi) < 0, pairs[k].what);
        CHECK(anostr_collate(hi, lo) > 0, pairs[k].what);       // antisymmetric
        CHECK(anostr_collate(lo, lo) == 0, pairs[k].what);      // reflexive
        // collate_prefix/key agree in sign where keys differ.
        uint64_t pa = anostr_collate_prefix(lo), pb = anostr_collate_prefix(hi);
        if (pa != pb)
            CHECK(sign(pa < pb ? -1 : 1) == sign(anostr_collate(lo, hi)), pairs[k].what);
        anostr_t kla = anostr_collate_key(h, lo), klb = anostr_collate_key(h, hi);
        CHECK(sign(anostr_compare(kla, klb)) == sign(anostr_collate(lo, hi)), pairs[k].what);
    }
    CHECK(anostr_collate_prefix(anostr_empty()) == 0, "empty keys to zero");
    CHECK(anostr_collate_prefix(anostr_lit("\x01")) == 0, "ignorable-only keys to zero");

    // Case mapping identity + idempotence + round-trip.
    CHECK(anorune_to_upper('A') == 'A', "upper idempotent");
    CHECK(anorune_to_lower(anorune_to_upper(0x3B4)) == 0x3B4, "Greek to_lower(to_upper) round-trip");
    CHECK(anorune_to_upper(0x2665) == 0x2665, "caseless is identity");   // ♥
    CHECK(anorune_to_upper(0x430) == 0x410, "Cyrillic upper");

    // Classification edges: S* not punct; unlisted marks/letters false.
    CHECK(!anorune_is_punct(0x20AC) && !anorune_is_punct('$'), "currency symbols are not punct");
    CHECK(anorune_is_punct(0x3001), "CJK ideographic comma is punct");
    CHECK(anorune_is_digit(0xFF15) && !anorune_is_digit(0x0660), "fullwidth digit yes, Arabic-Indic no");
    CHECK(!anorune_is_mark(0x5B0), "unlisted-script mark reports false");
    CHECK(anorune_is_whitespace(0xA0) && !anorune_is_whitespace('a'), "NBSP whitespace, letter not");
}

/* Embedded-NUL and malformed-UTF-8 byte transparency. */

static void test_edges(mi_heap_t *h)
{
    // Embedded NUL is a real byte; not a terminator.
    char ab[] = { 'a', 0, 'b' }, ac[] = { 'a', 0, 'c' };
    anostr_t nb = anostr_view(ab, 3), nc = anostr_view(ac, 3);
    CHECK(!anostr_eq(nb, nc), "embedded-NUL eq honors full len");
    CHECK(anostr_compare(nb, nc) < 0, "embedded-NUL compare");
    CHECK(anostr_hash(nb) != anostr_hash(nc), "embedded-NUL hash differs");
    CHECK(anostr_hash(nb) == fnv64(ab, 3), "hash counts the NUL");
    // to_cstr truncates at NUL; value keeps all 3 bytes.
    char *cs = anostr_to_cstr(h, nb);
    CHECK(cs != NULL && strlen(cs) == 1, "to_cstr truncates at embedded NUL (contract)");

    // Every empty spelling bit-identical to anostr_empty().
    anostr_builder_t eb = anostr_builder_make(h, 0);
    anostr_t empties[] = {
        anostr_empty(), anostr_view("", 0), anostr_from(h, "x", 0),
        anostr_from_cstr(h, NULL), anostr_from_cstr(h, ""), anostr_freeze(&eb),
        anostr_slice(anostr_lit("abc"), 1, 1), anostr_join(h, anostr_lit(","), NULL, 0),
        anostr_replace_all(h, anostr_lit("xx"), anostr_lit("x"), anostr_empty()),
    };
    for (size_t k = 0; k < sizeof empties / sizeof empties[0]; k++)
        CHECK(memcmp(&empties[k], &(anostr_t){0}, sizeof(anostr_t)) == 0, "empty is bit-identical");

    // Malformed: opaque to byte ops, U+FFFD for rune ops.
    static const char *bad[] = {
        "\x80",             // lone continuation
        "\xC3",             // truncated lead
        "\xC0\x80",         // overlong
        "\xED\xA0\x80",     // surrogate bytes
        "\xF4\x90\x80\x80", // > U+10FFFF
    };
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        anostr_t s = anostr_view(bad[k], strlen(bad[k]));
        CHECK(!anostr_utf8_valid(s), "malformed rejected by utf8_valid");
        // rune iteration is total within len.
        size_t i = 0, steps = 0;
        while (i < anostr_len(s)) { anostr_rune_next(s, &i); if (++steps > 64) break; }
        CHECK(i == anostr_len(s), "rune_next consumes malformed to exactly len");
        // Byte-level op agreement across kinds on malformed.
        anostr_t view = s, own = anostr_from(h, bad[k], strlen(bad[k]));
        CHECK(anostr_eq(view, own) && anostr_hash(view) == anostr_hash(own), "malformed eq across kinds");
    }

    // rune_next past end / rune_prev at 0 -> U+FFFD.
    anostr_t small = anostr_lit("hi");
    size_t at = anostr_len(small);
    CHECK(anostr_rune_next(small, &at) == ANORUNE_REPLACEMENT && at == anostr_len(small),
          "rune_next past end clamps");
    size_t zero = 0;
    CHECK(anostr_rune_prev(small, &zero) == ANORUNE_REPLACEMENT && zero == 0, "rune_prev at 0 stays");

    // anorune_encode boundaries + invalid -> U+FFFD.
    static const struct { anorune_t r; int len; } enc[] = {
        { 0x7F, 1 }, { 0x80, 2 }, { 0x7FF, 2 }, { 0x800, 3 }, { 0xFFFF, 3 },
        { 0x10000, 4 }, { 0x10FFFF, 4 },
    };
    for (size_t k = 0; k < sizeof enc / sizeof enc[0]; k++) {
        char buf[4];
        CHECK(anorune_encode(buf, enc[k].r) == enc[k].len, "encode byte length");
        CHECK(decode1(buf, enc[k].len) == enc[k].r, "encode/decode round-trip");
    }
    char buf[4];
    CHECK(anorune_encode(buf, 0x110000) == 3 && decode1(buf, 3) == ANORUNE_REPLACEMENT, ">MAX encodes FFFD");
}

/* keep survives source buffer teardown (I4). */

static void test_keep_lifetime(mi_heap_t *h)
{
    mi_heap_t *tmp = mi_heap_new();
    CHECK(tmp != NULL, "temp heap");
    if (tmp == NULL) return;
    const char *text = "a borrowed long string that outlives its source";
    char *buf = mi_heap_malloc(tmp, strlen(text));
    memcpy(buf, text, strlen(text));
    anostr_t kept = anostr_keep(h, anostr_view(buf, strlen(text)));   // copied into h
    CHECK(!anostr_is_inline(kept), "kept long value");
    mi_heap_destroy(tmp);                                             // source gone
    CHECK(anostr_len(kept) == strlen(text) && memcmp(anostr_bytes(&kept), text, strlen(text)) == 0,
          "kept value survives source teardown");
}

/* Intern rehash: force growth, re-resolve earlier symbols. */

static void test_intern_rehash(mi_heap_t *h)
{
    anostr_intern_t *t = anostr_intern_make(h);
    CHECK(t != NULL, "intern table");
    if (t == NULL) return;
    enum { N = 400 };
    anostr_sym syms[N];
    char nb[32];
    for (int i = 0; i < N; i++) {
        int n = snprintf(nb, sizeof nb, "symbol_number_%d", i);   // long, distinct
        syms[i] = anostr_intern(t, anostr_view(nb, n));
        CHECK(syms[i] == (anostr_sym)i, "dense symbols 0..count-1");
    }
    CHECK(anostr_intern_count(t) == N, "intern_count == distinct");
    for (int i = 0; i < N; i++) {
        int n = snprintf(nb, sizeof nb, "symbol_number_%d", i);
        CHECK(anostr_intern(t, anostr_view(nb, n)) == syms[i], "symbol stable across rehash");
        CHECK(anostr_eq(anostr_sym_str(t, syms[i]), anostr_view(nb, n)), "sym_str stable across rehash");
    }
    CHECK(anostr_intern_count(t) == N, "re-interning does not grow count");
    CHECK(anostr_is_empty(anostr_sym_str(t, N)), "out-of-range sym -> empty");
}

/* Mixed-kind sort: content-eq sequence across kinds. */

static void test_mixed_kind_sort(mi_heap_t *h, anostr_intern_t *it)
{
    static const char *words[] = { "delta", "alpha long enough to be heap", "charlie", "bravo",
                                   "alpha long enough to be heap", "echo", "bravo" };
    enum { N = sizeof words / sizeof words[0] };
    anostr_t items[N], ref[N];
    for (size_t k = 0; k < N; k++) {
        size_t n = strlen(words[k]);
        switch (k % 4) {                                 // spread the same content over kinds
        case 0: items[k] = anostr_from(h, words[k], n); break;
        case 1: items[k] = anostr_view(words[k], n); break;
        case 2: items[k] = anostr_keep(h, anostr_view(words[k], n)); break;
        default: items[k] = anostr_dedupe(it, anostr_view(words[k], n)); break;
        }
        ref[k] = anostr_view(words[k], n);
    }
    anostr_sort(ref, N);
    anostr_sort(items, N);
    for (size_t k = 0; k < N; k++)
        CHECK(anostr_eq(items[k], ref[k]), "mixed-kind sort matches content oracle");
}

/* anostr_sym_sort: cold vs warm cache; out-of-range sorts as empty. */

static void test_sym_sort(mi_heap_t *h)
{
    anostr_intern_t *t = anostr_intern_make(h);
    if (t == NULL) { CHECK(false, "sym_sort intern table"); return; }
    static const char *w[] = { "delta", "alpha", "charlie", "bravo", "alpha", "echo", "bravo", "foxtrot" };
    enum { N = sizeof w / sizeof w[0] };
    anostr_sym syms[N], cold[N];
    for (size_t k = 0; k < N; k++) syms[k] = anostr_intern(t, anostr_view(w[k], strlen(w[k])));

    anostr_sym_sort(t, syms, N);                        // cold: builds the per-symbol key cache
    memcpy(cold, syms, sizeof syms);
    for (size_t k = 1; k < N; k++)
        CHECK(anostr_collate(anostr_sym_str(t, syms[k - 1]), anostr_sym_str(t, syms[k])) <= 0,
              "sym_sort in collation order");

    test_rng rng = rng_make(0x51515151u);               // reshuffle, then warm-sort the cached keys
    for (size_t k = N - 1; k > 0; k--) {
        size_t j = rng_below(&rng, (uint32_t)k + 1);
        anostr_sym tmp = syms[k]; syms[k] = syms[j]; syms[j] = tmp;
    }
    anostr_sym_sort(t, syms, N);
    for (size_t k = 0; k < N; k++) CHECK(syms[k] == cold[k], "sym_sort warm == cold");

    anostr_sym trio[3] = { anostr_intern(t, anostr_lit("zzz")), anostr_intern(t, anostr_lit("aaa")), 0x7FFFFFFF };
    anostr_sym_sort(t, trio, 3);
    CHECK(trio[0] == 0x7FFFFFFF, "out-of-range symbol sorts first as empty");
    CHECK(anostr_eq(anostr_sym_str(t, trio[1]), anostr_lit("aaa")) &&
          anostr_eq(anostr_sym_str(t, trio[2]), anostr_lit("zzz")), "aaa before zzz");
}

// Tie family sharing four primaries forces bulk full-key path.
static void test_tie_family(mi_heap_t *h)
{
    enum { N = 64 };
    static char names[N][32];
    anostr_t items[N];
    test_rng rng = rng_make(0xF00DF00Du);
    for (size_t k = 0; k < N; k++) {
        snprintf(names[k], sizeof names[k], "Potion of %c%c%c",
                 'A' + (char)rng_below(&rng, 26), 'a' + (char)rng_below(&rng, 26),
                 'a' + (char)rng_below(&rng, 26));
        items[k] = anostr_view(names[k], strlen(names[k]));
    }
    check_against_oracle(items, N, "tie family", h);
}

// Directed edges the soak reaches rarely.
static void test_directed(mi_heap_t *h)
{
    // append_rune on invalid -> U+FFFD, returns 0.
    anostr_builder_t br = anostr_builder_make(h, 0);
    CHECK(anostr_builder_append_rune(&br, 0xD800) == 0, "append_rune surrogate ok");
    CHECK(anostr_builder_append_rune(&br, 0x110000) == 0, "append_rune >MAX ok");
    CHECK(anostr_eq(anostr_freeze(&br), anostr_lit("\xEF\xBF\xBD\xEF\xBF\xBD")), "append_rune invalid -> U+FFFD");

    // appendf: empty fmt, grow past reserve, %.*s via anostr_fmt.
    anostr_builder_t bf = anostr_builder_make(h, 2);
    CHECK(anostr_builder_appendf(&bf, "%s", "") == 0, "appendf empty format");
    CHECK(anostr_builder_appendf(&bf, "%.*s", anostr_fmt(anostr_lit("payload well beyond the reserve"))) == 0,
          "appendf grows past reserve");
    CHECK(anostr_eq(anostr_freeze(&bf), anostr_lit("payload well beyond the reserve")), "appendf accumulates");

    // split: empty sep -> whole; trailing sep -> trailing empty.
    anostr_t pc;
    anostr_split_t whole = anostr_split(anostr_lit("a,b,c"), anostr_empty());
    CHECK(anostr_split_next(&whole, &pc) && anostr_eq(pc, anostr_lit("a,b,c")), "empty sep -> whole");
    CHECK(!anostr_split_next(&whole, &pc), "empty sep is one piece");
    anostr_split_t trail = anostr_split(anostr_lit("a,"), anostr_lit(","));
    CHECK(anostr_split_next(&trail, &pc) && anostr_eq(pc, anostr_lit("a")), "trailing sep piece 1");
    CHECK(anostr_split_next(&trail, &pc) && anostr_is_empty(pc), "trailing sep -> trailing empty piece");
    CHECK(!anostr_split_next(&trail, &pc), "trailing sep exhausted");

    // slice: >12 long-slice borrows; <=12 goes inline.
    anostr_t base = anostr_lit("a long backing string well over twelve bytes long");
    anostr_t sl = anostr_slice(base, 2, 34);
    CHECK(!anostr_is_inline(sl) && anostr_bytes(&sl) == anostr_bytes(&base) + 2, "long slice borrows backing");
    CHECK(anostr_is_inline(anostr_slice(base, 0, 5)), "short slice re-canonicalizes inline");

    // replace_all shrink below inline cap -> bit-identical inline.
    anostr_t shr = anostr_replace_all(h, anostr_lit("xxxxxxxxxxxxxxxxxxxxxxab"), anostr_lit("x"), anostr_empty());
    anostr_t ab = anostr_lit("ab");
    CHECK(anostr_is_inline(shr) && memcmp(&shr, &ab, sizeof(anostr_t)) == 0, "shrink-to-inline bit-identical");

    // UTF-16 unpaired surrogate -> U+FFFD.
    char16_t lone[] = { 0xD800, 0x0041 };
    CHECK(anostr_eq(anostr_from_utf16(h, lone, 2), anostr_lit("\xEF\xBF\xBD" "A")),
          "unpaired UTF-16 surrogate -> U+FFFD");
}

/* Randomized property soak. Per-iter scratch heap. */

static void fuzz(uint32_t iterations)
{
    test_rng rng = rng_make(0xF0FBEEF5u);
    for (uint32_t it = 0; it < iterations; it++) {
        mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
        if (scratch == NULL) { printf("FAIL: fuzz scratch heap\n"); failures++; return; }
        anostr_intern_t *tab = anostr_intern_make(scratch);
        if (tab == NULL) { printf("FAIL: fuzz intern table\n"); failures++; return; }

        // Cross-kind agreement across 12/13 boundary.
        char vbuf[128];                                  // 24 runes * up to 4 UTF-8 bytes = 96 worst case
        anostr_t vs = rng_str(&rng, scratch, 24);
        size_t vn = anostr_len(vs);
        memcpy(vbuf, anostr_bytes(&vs), vn);
        check_kinds(scratch, tab, vbuf, vn, true, "kinds: valid utf8");   // rng_str never emits a NUL byte

        char bbuf[80];
        size_t bn = rng_bytes(&rng, bbuf, 40);           // embedded NUL + malformed included
        check_kinds(scratch, tab, bbuf, bn, false, "kinds: arbitrary bytes");

        // Slice + splice vs naive oracle.
        anostr_t src = anostr_view(bbuf, bn);
        size_t a = bn ? rng_below(&rng, (uint32_t)bn + 1) : 0;
        size_t b = bn ? rng_below(&rng, (uint32_t)bn + 1) : 0;
        if (a > b) { size_t tmp = a; a = b; b = tmp; }
        char rb[8]; size_t rn = rng_bytes(&rng, rb, 6);
        anostr_t r = anostr_view(rb, rn);
        anostr_t sliced = anostr_slice(src, a, b);
        CHECK(anostr_len(sliced) == b - a, "slice width");
        CHECK(anostr_is_inline(sliced) == (b - a <= ANOSTR_INLINE_CAP), "slice canonical rule");
        anostr_t spl = anostr_concat(scratch, anostr_slice(src, 0, a),
                                     anostr_concat(scratch, r, anostr_slice(src, b, bn)));
        char soracle[128];
        size_t sn = naive_splice(bbuf, bn, a, b, rb, rn, soracle);
        CHECK(anostr_eq(spl, anostr_view(soracle, sn)), "splice = slice+concat vs naive bytes");

        // Concat vs naive buffer; empty is identity.
        char cbuf[80]; size_t cn = rng_bytes(&rng, cbuf, 20);
        anostr_t ca = anostr_view(bbuf, bn), cb = anostr_view(cbuf, cn);
        anostr_t cc = anostr_concat(scratch, ca, cb);
        char cor[160]; memcpy(cor, bbuf, bn); memcpy(cor + bn, cbuf, cn);
        CHECK(anostr_len(cc) == bn + cn && anostr_eq(cc, anostr_view(cor, bn + cn)), "concat bytes");
        CHECK(anostr_eq(anostr_concat(scratch, ca, anostr_empty()), ca), "concat empty identity");

        // Find + replace_all vs naive scans.
        if (bn > 0) {
            char nbuf[4]; size_t nn = 1 + rng_below(&rng, 3);
            for (size_t k = 0; k < nn; k++) nbuf[k] = bbuf[rng_below(&rng, (uint32_t)bn)];
            anostr_t nd = anostr_view(nbuf, nn);
            size_t from = rng_below(&rng, (uint32_t)bn + 2);
            CHECK(anostr_find(src, nd, from) == naive_find(bbuf, bn, nbuf, nn, from), "find vs naive");
            char reb[4]; size_t ren = rng_below(&rng, 3);
            for (size_t k = 0; k < ren; k++) reb[k] = (char)('A' + rng_below(&rng, 4));
            anostr_t got = anostr_replace_all(scratch, src, nd, anostr_view(reb, ren));
            char rout[512];
            size_t rlen = naive_replace(bbuf, bn, nbuf, nn, reb, ren, rout);
            CHECK(anostr_eq(got, anostr_view(rout, rlen)), "replace_all vs naive");
        }
        // Empty needle / no-op replace -> s bit-identical.
        anostr_t idrep = anostr_replace_all(scratch, src, anostr_empty(), anostr_lit("!"));
        CHECK(anostr_eq(idrep, src) && memcmp(&idrep, &src, sizeof(anostr_t)) == 0, "empty needle identity");

        // Split/join round-trip; count == finds+1.
        char sepb[2]; size_t sepn = 1 + rng_below(&rng, 2);
        for (size_t k = 0; k < sepn; k++) sepb[k] = bn ? bbuf[rng_below(&rng, (uint32_t)bn)] : ',';
        anostr_t sep = anostr_view(sepb, sepn);
        anostr_t pieces[96]; size_t np = 0;
        anostr_split_t sit = anostr_split(src, sep);
        anostr_t piece;
        while (np < 96 && anostr_split_next(&sit, &piece)) pieces[np++] = piece;
        size_t expect = 1;
        for (size_t i = 0; ; ) {
            size_t f = anostr_find(src, sep, i);
            if (f == ANOSTR_NPOS) break;
            expect++; i = f + sepn;
        }
        CHECK(np == expect, "split piece count == find-count + 1");
        CHECK(anostr_eq(anostr_join(scratch, sep, pieces, np), src), "split/join round-trip");

        // Sort vs collation oracle.
        enum { M = 32 };
        anostr_t list[M];
        for (size_t k = 0; k < M; k++) list[k] = rng_str(&rng, scratch, 12);
        check_against_oracle(list, M, "fuzz sort", scratch);

        // Cull vs naive; rune_sort multiset + idempotence.
        uint32_t classes = rng_below(&rng, 8);
        {
            anostr_builder_t nb = anostr_builder_make(scratch, 0);   // naive cull rebuild
            for (size_t i = 0; i < vn; ) {
                size_t start = i;
                anorune_t ru = anostr_rune_next(vs, &i);
                bool drop = ((classes & ANOSTR_CULL_WHITESPACE) && anorune_is_whitespace(ru)) ||
                            ((classes & ANOSTR_CULL_PUNCT) && anorune_is_punct(ru)) ||
                            ((classes & ANOSTR_CULL_MARK) && anorune_is_mark(ru));
                if (!drop) anostr_builder_append(&nb, anostr_bytes(&vs) + start, i - start);
            }
            CHECK(anostr_eq(anostr_cull(scratch, vs, classes), anostr_freeze(&nb)), "cull vs naive");
        }
        anostr_t rs = anostr_rune_sort(scratch, vs);
        size_t an = 0, gn = 0;
        anorune_t *awant = anostr_to_utf32(scratch, vs, &an);
        anorune_t *agot  = anostr_to_utf32(scratch, rs, &gn);
        CHECK(gn == an, "rune_sort preserves rune count");
        CHECK(anostr_eq(anostr_rune_sort(scratch, rs), rs), "rune_sort idempotent");

        // UTF round-trips; to_utf32 matches rune_next.
        size_t u16n = 0;
        char16_t *u16 = anostr_to_utf16(scratch, vs, &u16n);
        CHECK(u16 != NULL && anostr_eq(anostr_from_utf16(scratch, u16, u16n), vs), "utf16 round-trip");
        CHECK(anostr_eq(anostr_from_utf32(scratch, awant, an), vs), "utf32 round-trip");
        size_t di = 0, dk = 0;
        while (di < vn && dk < an) { CHECK(anostr_rune_next(vs, &di) == awant[dk++], "to_utf32 == rune_next"); }

        // rune_sort is a permutation of source runes.
        qsort(awant, an, sizeof awant[0], rune_cmp);
        for (size_t i = 0; i < an && i < gn; i++)
            CHECK(awant[i] == agot[i], "rune_sort is a permutation of the source runes");

        // collate_prefix/key agree with collate in sign.
        anostr_t p = rng_str(&rng, scratch, 10), q = rng_str(&rng, scratch, 10);
        uint64_t pk = anostr_collate_prefix(p), qk = anostr_collate_prefix(q);
        if (pk != qk)
            CHECK(sign(pk < qk ? -1 : 1) == sign(anostr_collate(p, q)), "prefix key agrees with collate");
        anostr_t kp = anostr_collate_key(scratch, p), kq = anostr_collate_key(scratch, q);
        CHECK(sign(anostr_compare(kp, kq)) == sign(anostr_collate(p, q)), "collate_key agrees with collate");

        // Base folds: ASCII case-only oracle for eq_base/find_base.
        char lo[24], up[24]; size_t ln = 1 + rng_below(&rng, 10);
        for (size_t k = 0; k < ln; k++) { char c = (char)('a' + rng_below(&rng, 26)); lo[k] = c; up[k] = (char)(c - 32); }
        CHECK(anostr_eq_base(anostr_view(lo, ln), anostr_view(up, ln)), "eq_base case-only");
        CHECK(anostr_starts_base(anostr_view(lo, ln), anostr_view(up, ln)), "starts_base case-only");
    }
}

int main(int argc, char **argv)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    smoketest(heap);
    test_collation_cases(heap);
    test_edges(heap);
    test_keep_lifetime(heap);
    test_intern_rehash(heap);
    anostr_intern_t *mk = anostr_intern_make(heap);
    if (mk != NULL) test_mixed_kind_sort(heap, mk);
    test_sym_sort(heap);
    test_tie_family(heap);
    test_directed(heap);

    uint32_t iterations = 200;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    fuzz(iterations);

    // base_fold used by accent oracle below.
    CHECK(base_fold(0xC5) == 'a', "base fold accents to base letter");

    if (failures == 0) { printf("anotest_strings_fuzz: all checks passed\n"); return 0; }
    printf("anotest_strings_fuzz: %d check(s) failed\n", failures);
    return 1;
}
