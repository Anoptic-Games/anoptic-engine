/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_strings_utf.h: encode/decode, case/class, collation, encoding conversion.
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

static bool rune_is_scalar(anorune_t r)
{
    return r <= ANORUNE_MAX && !(r >= 0xD800u && r <= 0xDFFFu);
}

static void test_encode_decode_roundtrip_exhaustive(void)
{
    for (anorune_t r = 0; r <= ANORUNE_MAX; r++) {
        if (!rune_is_scalar(r))
            continue;
        char buf[4];
        int n = anorune_encode(buf, r);
        int expect = r < 0x80u ? 1 : r < 0x800u ? 2 : r < 0x10000u ? 3 : 4;
        if (n != expect) {
            printf("FAIL: encode length of U+%04X: %d, expected %d\n", r, n, expect);
            failures++;
            return;
        }
        anostr_t s = anostr_view(buf, (size_t)n);
        size_t i = 0;
        anorune_t back = anostr_rune_next(s, &i);
        if (back != r || i != (size_t)n) {
            printf("FAIL: round-trip of U+%04X: got U+%04X, consumed %zu\n", r, back, i);
            failures++;
            return;
        }
    }

    // Exact bytes at length boundaries.
    char buf[4];
    CHECK(anorune_encode(buf, 0x7F) == 1 && (uint8_t)buf[0] == 0x7F, "U+007F is 1 byte");
    CHECK(anorune_encode(buf, 0x80) == 2 && memcmp(buf, "\xC2\x80", 2) == 0, "U+0080 is C2 80");
    CHECK(anorune_encode(buf, 0x7FF) == 2 && memcmp(buf, "\xDF\xBF", 2) == 0, "U+07FF is DF BF");
    CHECK(anorune_encode(buf, 0x800) == 3 && memcmp(buf, "\xE0\xA0\x80", 3) == 0, "U+0800 is E0 A0 80");
    CHECK(anorune_encode(buf, 0x20AC) == 3 && memcmp(buf, "\xE2\x82\xAC", 3) == 0, "euro sign is E2 82 AC");
    CHECK(anorune_encode(buf, 0xFFFF) == 3 && memcmp(buf, "\xEF\xBF\xBF", 3) == 0, "U+FFFF is EF BF BF");
    CHECK(anorune_encode(buf, 0x10000) == 4 && memcmp(buf, "\xF0\x90\x80\x80", 4) == 0, "U+10000 is F0 90 80 80");
    CHECK(anorune_encode(buf, ANORUNE_MAX) == 4 && memcmp(buf, "\xF4\x8F\xBF\xBF", 4) == 0, "U+10FFFF is F4 8F BF BF");

    // Invalid runes encode as U+FFFD.
    CHECK(anorune_encode(buf, 0xD800) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0, "surrogate encodes U+FFFD");
    CHECK(anorune_encode(buf, 0x110000) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0, "out-of-range encodes U+FFFD");
}

static void test_malformed_decode(void)
{
    // Strict decode: U+FFFD, advance 1, fails validation.
    static const struct { const char *bytes; size_t len; const char *what; } bad[] = {
        { "\x80",             1, "bare continuation byte" },
        { "\xBF\xBF",         2, "continuation run" },
        { "\xC3",             1, "truncated 2-byte sequence" },
        { "\xE2\x82",         2, "truncated 3-byte sequence" },
        { "\xF0\x90\x80",     3, "truncated 4-byte sequence" },
        { "\xC0\x80",         2, "overlong NUL (C0 80)" },
        { "\xC1\xBF",         2, "overlong 2-byte" },
        { "\xE0\x80\x80",     3, "overlong 3-byte" },
        { "\xF0\x80\x80\x80", 4, "overlong 4-byte" },
        { "\xED\xA0\x80",     3, "encoded surrogate U+D800" },
        { "\xED\xBF\xBF",     3, "encoded surrogate U+DFFF" },
        { "\xF4\x90\x80\x80", 4, "beyond U+10FFFF" },
        { "\xF8\x88\x80\x80", 4, "invalid lead 0xF8" },
        { "\xFF",             1, "invalid lead 0xFF" },
        { "\xE2\x41\x42",     3, "broken continuation (ASCII where 0x80.. expected)" },
    };
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        anostr_t s = anostr_view(bad[k].bytes, bad[k].len);
        size_t i = 0;
        anorune_t r = anostr_rune_next(s, &i);
        if (r != ANORUNE_REPLACEMENT || i != 1) {
            printf("FAIL: %s: got U+%04X, advanced to %zu (want U+FFFD, 1)\n", bad[k].what, r, i);
            failures++;
        }
        if (anostr_utf8_valid(s)) {
            printf("FAIL: %s passes anostr_utf8_valid\n", bad[k].what);
            failures++;
        }
    }

    // Encoded U+FFFD is valid, distinct from error return.
    anostr_t fffd = anostr_lit("\xEF\xBF\xBD");
    CHECK(anostr_utf8_valid(fffd), "encoded U+FFFD is valid UTF-8");

    // Malformed then good: resync after 1 byte.
    anostr_t mixed = anostr_lit("\x80" "ok");
    size_t i = 0;
    CHECK(anostr_rune_next(mixed, &i) == ANORUNE_REPLACEMENT && i == 1, "error consumes 1 byte");
    CHECK(anostr_rune_next(mixed, &i) == 'o' && anostr_rune_next(mixed, &i) == 'k',
          "decoding resynchronizes after the bad byte");

    // Iterator boundary conditions.
    anostr_t empty = anostr_empty();
    i = 7;  // deliberately out of range
    CHECK(anostr_rune_next(empty, &i) == ANORUNE_REPLACEMENT && i == 0, "next past end clamps");
    i = 0;
    CHECK(anostr_rune_prev(empty, &i) == ANORUNE_REPLACEMENT && i == 0, "prev at start stays put");
}

// Forward then backward; demand reverse sequence and matching rune_count.
static void check_iteration_agreement(anostr_t s, const anorune_t *expect, size_t count, const char *what)
{
    size_t i = 0, n = 0;
    while (i < anostr_len(s)) {
        anorune_t r = anostr_rune_next(s, &i);
        if (n >= count || r != expect[n]) {
            printf("FAIL: %s: forward rune %zu is U+%04X\n", what, n, r);
            failures++;
            return;
        }
        n++;
    }
    if (n != count) {
        printf("FAIL: %s: forward yielded %zu runes, want %zu\n", what, n, count);
        failures++;
        return;
    }
    i = anostr_len(s);
    while (i > 0) {
        anorune_t r = anostr_rune_prev(s, &i);
        if (n == 0 || r != expect[n - 1]) {
            printf("FAIL: %s: backward rune %zu is U+%04X\n", what, n - 1, r);
            failures++;
            return;
        }
        n--;
    }
    if (n != 0) {
        printf("FAIL: %s: backward stopped %zu runes early\n", what, n);
        failures++;
    }
    if (anostr_rune_count(s) != count) {
        printf("FAIL: %s: rune_count %zu, want %zu\n", what, anostr_rune_count(s), count);
        failures++;
    }
}

static void test_iteration(mi_heap_t *heap)
{
    // Mixed widths interleaved, then ASCII tail.
    static const anorune_t expect[] = { 'a', 0xE9, 0x416, 0x20AC, 0x1D568, 0x1F600, '!', ' ' };
    const char *text = "a\xC3\xA9\xD0\x96\xE2\x82\xAC\xF0\x9D\x95\xA8\xF0\x9F\x98\x80! ";

    anostr_t s = anostr_from(heap, text, strlen(text));
    CHECK(!anostr_is_inline(s), "mixed-width sample is heap-backed");
    CHECK(anostr_utf8_valid(s), "mixed-width sample is valid UTF-8");
    check_iteration_agreement(s, expect, sizeof expect / sizeof expect[0], "heap-backed mixed-width");

    // Same agreement on an inline value (<= 12 bytes).
    static const anorune_t expect_inline[] = { 0xE9, 0x20AC, 0x1D568 };
    anostr_t tiny = anostr_lit("\xC3\xA9\xE2\x82\xAC\xF0\x9D\x95\xA8");
    CHECK(anostr_is_inline(tiny), "9-byte sample is inline");
    check_iteration_agreement(tiny, expect_inline, 3, "inline mixed-width");
}

static void test_case_mapping(void)
{
    CHECK(anorune_to_upper('a') == 'A' && anorune_to_lower('A') == 'a', "ASCII case");
    CHECK(anorune_to_upper(0x436) == 0x416 && anorune_to_lower(0x416) == 0x436, "Cyrillic zhe");
    CHECK(anorune_to_lower(0x100) == 0x101 && anorune_to_upper(0x101) == 0x100,
          "Latin Extended-A odd/even pair (A-macron)");
    // Final sigma: both lowercase -> capital sigma.
    CHECK(anorune_to_upper(0x3C2) == 0x3A3 && anorune_to_upper(0x3C3) == 0x3A3, "sigma and final sigma");
    CHECK(anorune_to_lower(0x3A3) == 0x3C3, "capital sigma lowers to medial sigma");
    // Romanian: comma-below and legacy cedilla both case.
    CHECK(anorune_to_lower(0x218) == 0x219 && anorune_to_upper(0x219) == 0x218, "Romanian S-comma");
    CHECK(anorune_to_lower(0x21A) == 0x21B && anorune_to_upper(0x21B) == 0x21A, "Romanian T-comma");
    CHECK(anorune_to_lower(0x15E) == 0x15F && anorune_to_upper(0x103) == 0x102,
          "legacy cedilla + a-breve pair");

    // Identity on caseless and unlisted scripts.
    static const anorune_t caseless[] = { 0x6F22, 0x3042, '7', 0x1F600, 0xE000, 0x10400,
                                          0x10FFFE, 0x110000 };
    for (size_t k = 0; k < sizeof caseless / sizeof caseless[0]; k++) {
        anorune_t r = caseless[k];
        if (anorune_to_upper(r) != r || anorune_to_lower(r) != r) {
            printf("FAIL: U+%04X should be caseless identity\n", r);
            failures++;
        }
    }

    // Every mapping of every scalar lands on a valid scalar.
    for (anorune_t r = 0; r <= ANORUNE_MAX; r++) {
        if (!rune_is_scalar(r))
            continue;
        anorune_t u = anorune_to_upper(r), l = anorune_to_lower(r);
        if (!rune_is_scalar(u) || !rune_is_scalar(l)) {
            printf("FAIL: case map of U+%04X leaves the scalar range (U+%04X / U+%04X)\n", r, u, l);
            failures++;
            return;
        }
    }
}

static void test_classification(void)
{
    // Letters across shipped scripts.
    static const anorune_t letters[] = { 'A', 'z', 0x416, 0x3B1, 0x6F22, 0x3042, 0x16A0 };
    for (size_t k = 0; k < sizeof letters / sizeof letters[0]; k++)
        if (!anorune_is_letter(letters[k])) {
            printf("FAIL: U+%04X should be a letter\n", letters[k]);
            failures++;
        }
    CHECK(!anorune_is_letter('!') && !anorune_is_letter('7') && !anorune_is_letter(0x1F600),
          "punctuation, digits, emoji are not letters");
    CHECK(!anorune_is_letter(0x5D0) && !anorune_is_letter(0x10400),
          "unlisted scripts (Hebrew, Deseret) report false after the trim");

    // Nd digits: ASCII and fullwidth.
    CHECK(anorune_is_digit('0') && anorune_is_digit('9'), "ASCII digits");
    CHECK(anorune_is_digit(0xFF10) && anorune_is_digit(0xFF19), "fullwidth digits");
    CHECK(!anorune_is_digit(0x966) && !anorune_is_digit(0x663),
          "Devanagari and Arabic-Indic digits are outside the keep-list");
    CHECK(!anorune_is_digit(0x216B), "Roman numeral XII is not Nd");
    CHECK(!anorune_is_digit('a'), "letters are not digits");

    // White_Space: NBSP and U+3000 in, ZWSP out.
    static const anorune_t spaces[] = { ' ', '\t', '\n', '\r', 0xA0, 0x3000 };
    for (size_t k = 0; k < sizeof spaces / sizeof spaces[0]; k++)
        if (!anorune_is_whitespace(spaces[k])) {
            printf("FAIL: U+%04X should be whitespace\n", spaces[k]);
            failures++;
        }
    CHECK(!anorune_is_whitespace(0x200B), "zero-width space is NOT White_Space (it is Cf)");
    CHECK(!anorune_is_whitespace('a'), "letters are not whitespace");

    // Combining marks: 0300-036F covered.
    static const anorune_t marks[] = { 0x300, 0x301, 0x308, 0x30A, 0x342, 0x36F };
    for (size_t k = 0; k < sizeof marks / sizeof marks[0]; k++)
        if (!anorune_is_mark(marks[k])) {
            printf("FAIL: U+%04X should be a combining mark\n", marks[k]);
            failures++;
        }
    CHECK(!anorune_is_mark('e') && !anorune_is_mark(0xE9) && !anorune_is_mark(0x219),
          "base and precomposed letters are not marks");
    CHECK(!anorune_is_mark(0x5B4), "unlisted scripts' marks report false after the trim");
}

static void test_builder_append_rune(mi_heap_t *heap)
{
    anostr_builder_t b = anostr_builder_make(heap, 0);
    CHECK(anostr_builder_append_rune(&b, 'a') == 0, "append ASCII rune");
    CHECK(anostr_builder_append_rune(&b, 0xE9) == 0, "append 2-byte rune");
    CHECK(anostr_builder_append_rune(&b, 0x20AC) == 0, "append 3-byte rune");
    CHECK(anostr_builder_append_rune(&b, 0x1F600) == 0, "append 4-byte rune");
    CHECK(anostr_builder_append_rune(&b, 0xD800) == 0, "append invalid rune (becomes U+FFFD)");
    anostr_t s = anostr_freeze(&b);

    const char *expect = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xEF\xBF\xBD";
    CHECK(anostr_len(s) == strlen(expect) && memcmp(anostr_bytes(&s), expect, strlen(expect)) == 0,
          "builder composed the exact expected bytes");
    CHECK(anostr_utf8_valid(s), "builder output is valid UTF-8");
    CHECK(anostr_rune_count(s) == 5, "builder output has 5 runes");
}

static void test_collation(void)
{
    // Level one: base letters (Ä before Z).
    CHECK(anostr_collate(anostr_lit("\xC3\x84pfel"), anostr_lit("Zebra")) < 0, "Aepfel < Zebra");
    // German umlauts group with base.
    CHECK(anostr_collate(anostr_lit("M\xC3\xBCller"), anostr_lit("Muster")) < 0, "Mueller < Muster");
    // Level two: accents.
    CHECK(anostr_collate(anostr_lit("resume"), anostr_lit("r\xC3\xA9sum\xC3\xA9")) < 0, "resume < resume-acute");
    // Level three: case.
    CHECK(anostr_collate(anostr_lit("apple"), anostr_lit("Apple")) < 0, "apple < Apple");
    CHECK(anostr_collate(anostr_lit("apple"), anostr_lit("apple")) == 0, "self-collation is 0");

    // Russian: ё between е and ж.
    CHECK(anostr_collate(anostr_lit("\xD0\xB5"), anostr_lit("\xD1\x91")) < 0, "e < yo");
    CHECK(anostr_collate(anostr_lit("\xD1\x91"), anostr_lit("\xD0\xB6")) < 0, "yo < zhe");
    // Romanian: ș groups with s.
    CHECK(anostr_collate(anostr_lit("s"), anostr_lit("\xC8\x99")) < 0, "s < s-comma");
    CHECK(anostr_collate(anostr_lit("\xC8\x99"), anostr_lit("t")) < 0, "s-comma < t");
    // Kana: あ < い; hira/kata differ at level three.
    CHECK(anostr_collate(anostr_lit("\xE3\x81\x82"), anostr_lit("\xE3\x81\x84")) < 0, "a < i (kana)");
    CHECK(anostr_eq_base(anostr_lit("\xE3\x81\x82"), anostr_lit("\xE3\x82\xA2")), "hira a ==base kata a");
    CHECK(anostr_collate(anostr_lit("\xE3\x81\x82"), anostr_lit("\xE3\x82\xA2")) != 0, "hira != kata full");
    // Han: implicit weights, code point order.
    CHECK(anostr_collate(anostr_lit("\xE6\xBC\xA2"), anostr_lit("\xE8\xAA\x9E")) < 0, "Han cp order");
    // Spaces are significant (non-ignorable).
    CHECK(anostr_collate(anostr_lit("New York"), anostr_lit("Newark")) < 0, "space before letters");
    // Empty sorts first; distinct bytes never equal.
    CHECK(anostr_collate(anostr_empty(), anostr_lit("a")) < 0, "empty sorts first");

    // Base-letter search.
    CHECK(anostr_eq_base(anostr_lit("M\xC3\x9CNCHEN"), anostr_lit("m\xC3\xBCnchen")), "MUENCHEN ==base muenchen");
    CHECK(anostr_eq_base(anostr_lit("\xC3\x85lesund"), anostr_lit("alesund")), "Aalesund ==base alesund");
    CHECK(anostr_eq_base(anostr_lit("\xC8\x98tefan"), anostr_lit("stefan")), "Stefan-comma ==base stefan");
    CHECK(!anostr_eq_base(anostr_lit("Oslo"), anostr_lit("Odin")), "different words differ");
    CHECK(anostr_starts_base(anostr_lit("\xC3\x96rebro kommun"), anostr_lit("orebro")), "starts_base Oerebro");
    CHECK(!anostr_starts_base(anostr_lit("Bergen"), anostr_lit("berge!")), "punctuation breaks a match");
    CHECK(anostr_starts_base(anostr_lit("anything"), anostr_empty()), "empty prefix always matches");

    // find_base: "Örebro" via ASCII "orebro".
    anostr_t line = anostr_lit("Visby \xC3\x96rebro");
    CHECK(anostr_find_base(line, anostr_lit("orebro"), 0) == 6, "find_base hits the umlaut form");
    CHECK(anostr_find_base(line, anostr_lit("kiruna"), 0) == ANOSTR_NPOS, "find_base misses cleanly");
    CHECK(anostr_find_base(line, anostr_empty(), 99) == anostr_len(line), "empty needle at min(from,len)");

    // Unlisted scripts after listed, by code point.
    CHECK(anostr_collate(anostr_lit("\xE1\x9A\xA0"), anostr_lit("\xE6\x9D\xB1")) < 0, "Runic before Han");
}

static void test_sort(void)
{
    // Multi-script city list: Latin by base, then Greek, Cyrillic, Han.
    anostr_t cities[] = {
        anostr_lit("\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0"),  // Москва
        anostr_lit("Oslo"),
        anostr_lit("Z\xC3\xBCrich"),                                     // Zürich
        anostr_lit("\xE6\x9D\xB1\xE4\xBA\xAC"),                          // 東京
        anostr_lit("Athens"),
        anostr_lit("\xC3\x85lesund"),                                    // Ålesund
        anostr_lit("M\xC3\xBCnchen"),                                    // München
        anostr_lit("\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1"),          // Αθήνα
        anostr_lit("G\xC3\xB6teborg"),                                   // Göteborg
        anostr_lit("\xC3\x96rebro"),                                     // Örebro
        anostr_lit("Berlin"),
    };
    static const char *expect[] = {
        "\xC3\x85lesund", "Athens", "Berlin", "G\xC3\xB6teborg", "M\xC3\xBCnchen",
        "\xC3\x96rebro", "Oslo", "Z\xC3\xBCrich",
        "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1",
        "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0",
        "\xE6\x9D\xB1\xE4\xBA\xAC",
    };
    size_t n = sizeof cities / sizeof cities[0];
    anostr_sort(cities, n);
    for (size_t k = 0; k < n; k++) {
        if (!anostr_eq(cities[k], anostr_from_cstr(NULL, expect[k]))) {
            printf("FAIL: sorted city %zu is \"%.*s\"\n", k, anostr_fmt(cities[k]));
            failures++;
        }
    }
}

static void test_encoding_conversion(mi_heap_t *heap)
{
    // Surrogate pair round-trip.
    anostr_t s = anostr_lit("a \xE2\x82\xAC\xF0\x9D\x95\xA8");
    size_t n = 0;
    char16_t *w = anostr_to_utf16(heap, s, &n);
    static const char16_t expect16[] = { 0x61, 0x20, 0x20AC, 0xD835, 0xDD68, 0 };
    CHECK(w != NULL && n == 5, "to_utf16 unit count");
    CHECK(w != NULL && memcmp(w, expect16, sizeof expect16) == 0, "to_utf16 exact units + NUL");

    anorune_t *u = anostr_to_utf32(heap, s, &n);
    static const anorune_t expect32[] = { 0x61, 0x20, 0x20AC, 0x1D568, 0 };
    CHECK(u != NULL && n == 4, "to_utf32 rune count");
    CHECK(u != NULL && memcmp(u, expect32, sizeof expect32) == 0, "to_utf32 exact runes + NUL");

    // Round-trips land on equal values.
    CHECK(anostr_eq(anostr_from_utf16(heap, w, 5), s), "utf16 round-trip");
    CHECK(anostr_eq(anostr_from_utf16_cstr(heap, w), s), "utf16 cstr round-trip");
    CHECK(anostr_eq(anostr_from_utf32(heap, u, 4), s), "utf32 round-trip");

    // Unpaired surrogates -> U+FFFD.
    static const char16_t bad16[] = { 0xD800, 'a', 0xDC00, 0xDBFF };
    anostr_t fixed = anostr_from_utf16(heap, bad16, 4);
    static const anorune_t expect_fixed[] = { 0xFFFD, 'a', 0xFFFD, 0xFFFD };
    size_t i = 0, k = 0;
    while (i < anostr_len(fixed)) {
        anorune_t r = anostr_rune_next(fixed, &i);
        if (k >= 4 || r != expect_fixed[k]) {
            printf("FAIL: unpaired surrogate repair: rune %zu is U+%04X\n", k, r);
            failures++;
            break;
        }
        k++;
    }
    CHECK(anostr_utf8_valid(fixed), "repaired utf16 input is valid UTF-8");

    // Invalid UTF-32 -> U+FFFD.
    static const anorune_t bad32[] = { 0xD800, 0x110000, 'x' };
    anostr_t fixed32 = anostr_from_utf32(heap, bad32, 3);
    CHECK(anostr_eq(fixed32, anostr_lit("\xEF\xBF\xBD\xEF\xBF\xBD" "x")), "utf32 sanitizes to U+FFFD");

    // Totality corners.
    CHECK(anostr_is_empty(anostr_from_utf16(heap, NULL, 9)), "NULL utf16 src yields empty");
    CHECK(anostr_is_empty(anostr_from_utf32(heap, NULL, 9)), "NULL utf32 src yields empty");
    CHECK(anostr_is_empty(anostr_from_utf16(heap, expect16, 0)), "zero units yield empty");
    n = 77;
    CHECK(anostr_to_utf16(NULL, s, &n) == NULL && n == 0, "NULL heap fails with count 0");
}

// Random scalars: encode, iterate forward/backward vs source. argv[1] scales.
static void soak(mi_heap_t *heap, uint32_t iterations)
{
    test_rng rng = rng_make(0xC0DEC0DEu);
    enum { MAX_RUNES = 256 };
    anorune_t src[MAX_RUNES];

    for (uint32_t it = 0; it < iterations; it++) {
        uint32_t count = 1 + rng_below(&rng, MAX_RUNES);
        anostr_builder_t b = anostr_builder_make(heap, 0);
        for (uint32_t k = 0; k < count; k++) {
            anorune_t r;
            do {
                r = rng_below(&rng, ANORUNE_MAX + 1);
            } while (!rune_is_scalar(r));
            src[k] = r;
            if (anostr_builder_append_rune(&b, r) != 0) {
                printf("FAIL: soak append (it=%u)\n", it);
                failures++;
                anostr_builder_discard(&b);
                return;
            }
        }
        anostr_t s = anostr_freeze(&b);

        if (!anostr_utf8_valid(s) || anostr_rune_count(s) != count) {
            printf("FAIL: soak valid/count (it=%u)\n", it);
            failures++;
            return;
        }
        size_t i = 0, n = 0;
        while (i < anostr_len(s)) {
            if (anostr_rune_next(s, &i) != src[n++]) {
                printf("FAIL: soak forward mismatch (it=%u, rune %zu)\n", it, n - 1);
                failures++;
                return;
            }
        }
        i = anostr_len(s);
        while (i > 0) {
            if (anostr_rune_prev(s, &i) != src[--n]) {
                printf("FAIL: soak backward mismatch (it=%u, rune %zu)\n", it, n);
                failures++;
                return;
            }
        }

        // Both conversions round-trip.
        size_t units = 0, runes = 0;
        char16_t  *w = anostr_to_utf16(heap, s, &units);
        anorune_t *u = anostr_to_utf32(heap, s, &runes);
        if (w == NULL || u == NULL || runes != count ||
            !anostr_eq(anostr_from_utf16(heap, w, units), s) ||
            !anostr_eq(anostr_from_utf32(heap, u, runes), s)) {
            printf("FAIL: soak conversion round-trip (it=%u)\n", it);
            failures++;
            return;
        }
        mi_free(w);
        mi_free(u);
    }
}

int main(int argc, char **argv)
{
    // One scratch heap for the whole run.
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    test_encode_decode_roundtrip_exhaustive();
    test_malformed_decode();
    test_iteration(heap);
    test_case_mapping();
    test_classification();
    test_builder_append_rune(heap);
    test_collation();
    test_sort();
    test_encoding_conversion(heap);

    uint32_t iterations = 500;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    soak(heap, iterations);

    if (failures == 0) { printf("anotest_strings_utf: all checks passed\n"); return 0; }
    printf("anotest_strings_utf: %d check(s) failed\n", failures);
    return 1;
}
