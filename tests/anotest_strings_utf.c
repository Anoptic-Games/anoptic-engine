/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_strings_utf.h -- UTF-8 iteration, encode, and the Unicode tables:
 *   - encode/decode round-trip of EVERY valid scalar value (0..0x10FFFF minus surrogates),
 *     with exact length-boundary bytes checked at 0x7F/0x80, 0x7FF/0x800, 0xFFFF/0x10000;
 *   - strict decode: overlongs, encoded surrogates, > U+10FFFF, truncated sequences, bare
 *     continuation bytes, and 0xF8..0xFF leads all yield U+FFFD advancing exactly 1 byte,
 *     and all fail anostr_utf8_valid;
 *   - forward/backward iteration agree (prev yields next's runes reversed) on mixed-width
 *     text, inline and heap-backed variants both;
 *   - rune_count == runes yielded by iteration on valid strings;
 *   - case mapping: ASCII, Cyrillic, Latin Extended-A odd/even pairs, final sigma (the
 *     documented non-round-trip), Deseret (SMP), identity on caseless (CJK, digits, emoji);
 *     every mapping of every code point stays a valid scalar value;
 *   - classification: letters across scripts (incl. Runic and CJK), Nd digits beyond ASCII,
 *     White_Space property quirks (NBSP yes, ZWSP no), combining marks (Mn/Mc/Me) vs
 *     precomposed letters;
 *   - builder_append_rune composes the exact expected byte sequence;
 *   - encoding conversion: UTF-16 surrogate pairs get exact expected units both ways,
 *     unpaired surrogates and out-of-range UTF-32 convert as U+FFFD, NUL termination and
 *     out-counts are right, and UTF-8 -> UTF-16/32 -> UTF-8 round-trips to anostr_eq;
 *   - a randomized iterate-forward/backward + convert-round-trip soak (fixed seed;
 *     argv[1] scales iterations).
 * Exit 0 == pass; failures print what broke. */

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

    // Exact bytes at the length boundaries.
    char buf[4];
    CHECK(anorune_encode(buf, 0x7F) == 1 && (uint8_t)buf[0] == 0x7F, "U+007F is 1 byte");
    CHECK(anorune_encode(buf, 0x80) == 2 && memcmp(buf, "\xC2\x80", 2) == 0, "U+0080 is C2 80");
    CHECK(anorune_encode(buf, 0x7FF) == 2 && memcmp(buf, "\xDF\xBF", 2) == 0, "U+07FF is DF BF");
    CHECK(anorune_encode(buf, 0x800) == 3 && memcmp(buf, "\xE0\xA0\x80", 3) == 0, "U+0800 is E0 A0 80");
    CHECK(anorune_encode(buf, 0x20AC) == 3 && memcmp(buf, "\xE2\x82\xAC", 3) == 0, "euro sign is E2 82 AC");
    CHECK(anorune_encode(buf, 0xFFFF) == 3 && memcmp(buf, "\xEF\xBF\xBF", 3) == 0, "U+FFFF is EF BF BF");
    CHECK(anorune_encode(buf, 0x10000) == 4 && memcmp(buf, "\xF0\x90\x80\x80", 4) == 0, "U+10000 is F0 90 80 80");
    CHECK(anorune_encode(buf, ANORUNE_MAX) == 4 && memcmp(buf, "\xF4\x8F\xBF\xBF", 4) == 0, "U+10FFFF is F4 8F BF BF");

    // Invalid runes encode as U+FFFD, never fail.
    CHECK(anorune_encode(buf, 0xD800) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0, "surrogate encodes U+FFFD");
    CHECK(anorune_encode(buf, 0x110000) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0, "out-of-range encodes U+FFFD");
}

static void test_malformed_decode(void)
{
    // Each case: strict decode yields U+FFFD, advances exactly 1, and fails validation.
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

    // A genuinely-encoded U+FFFD is valid text, distinct from the error return.
    anostr_t fffd = anostr_lit("\xEF\xBF\xBD");
    CHECK(anostr_utf8_valid(fffd), "encoded U+FFFD is valid UTF-8");

    // Malformed byte followed by good text: the decoder resynchronizes after 1 byte.
    anostr_t mixed = anostr_lit("\x80" "ok");
    size_t i = 0;
    CHECK(anostr_rune_next(mixed, &i) == ANORUNE_REPLACEMENT && i == 1, "error consumes 1 byte");
    CHECK(anostr_rune_next(mixed, &i) == 'o' && anostr_rune_next(mixed, &i) == 'k',
          "decoding resynchronizes after the bad byte");

    // Boundary conditions of the iterators themselves.
    anostr_t empty = anostr_empty();
    i = 7;  // deliberately out of range
    CHECK(anostr_rune_next(empty, &i) == ANORUNE_REPLACEMENT && i == 0, "next past end clamps");
    i = 0;
    CHECK(anostr_rune_prev(empty, &i) == ANORUNE_REPLACEMENT && i == 0, "prev at start stays put");
}

// Forward-iterate collecting runes, then backward-iterate and demand the reverse sequence,
// and that rune_count agrees. Exercises both anostr_t variants via the caller's string.
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
    // 1-, 2-, 3-, 4-byte widths interleaved: "aéЖ€𝕨😀" then ASCII tail.
    static const anorune_t expect[] = { 'a', 0xE9, 0x416, 0x20AC, 0x1D568, 0x1F600, '!', ' ' };
    const char *text = "a\xC3\xA9\xD0\x96\xE2\x82\xAC\xF0\x9D\x95\xA8\xF0\x9F\x98\x80! ";

    anostr_t s = anostr_from(heap, text, strlen(text));
    CHECK(!anostr_is_inline(s), "mixed-width sample is heap-backed");
    CHECK(anostr_utf8_valid(s), "mixed-width sample is valid UTF-8");
    check_iteration_agreement(s, expect, sizeof expect / sizeof expect[0], "heap-backed mixed-width");

    // Same agreement on an inline value (<= 12 bytes): "é€𝕨" = 2+3+4 = 9 bytes.
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
    // Final sigma: both lowercase sigmas uppercase to capital sigma; no round-trip promise.
    CHECK(anorune_to_upper(0x3C2) == 0x3A3 && anorune_to_upper(0x3C3) == 0x3A3, "sigma and final sigma");
    CHECK(anorune_to_lower(0x3A3) == 0x3C3, "capital sigma lowers to medial sigma");
    // SMP casing: Deseret long I.
    CHECK(anorune_to_lower(0x10400) == 0x10428 && anorune_to_upper(0x10428) == 0x10400, "Deseret (SMP)");
    // Romanian: comma-below forms (correct) and the legacy cedilla forms both case.
    CHECK(anorune_to_lower(0x218) == 0x219 && anorune_to_upper(0x219) == 0x218, "Romanian S-comma");
    CHECK(anorune_to_lower(0x21A) == 0x21B && anorune_to_upper(0x21B) == 0x21A, "Romanian T-comma");
    CHECK(anorune_to_lower(0x15E) == 0x15F && anorune_to_upper(0x103) == 0x102,
          "legacy cedilla + a-breve pair");

    // Identity on caseless: CJK, kana, digits, emoji, unassigned, out of range.
    static const anorune_t caseless[] = { 0x6F22, 0x3042, '7', 0x1F600, 0xE000, 0x10FFFE, 0x110000 };
    for (size_t k = 0; k < sizeof caseless / sizeof caseless[0]; k++) {
        anorune_t r = caseless[k];
        if (anorune_to_upper(r) != r || anorune_to_lower(r) != r) {
            printf("FAIL: U+%04X should be caseless identity\n", r);
            failures++;
        }
    }

    // Every mapping of every scalar value lands on a valid scalar value. (Surrogates are
    // not scalar values -- decode never yields them -- so they are not part of the domain.)
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
    // Letters across scripts: Latin, Cyrillic, Greek, CJK, kana, Runic (BMP), Deseret (SMP).
    static const anorune_t letters[] = { 'A', 'z', 0x416, 0x3B1, 0x6F22, 0x3042, 0x16A0, 0x10400 };
    for (size_t k = 0; k < sizeof letters / sizeof letters[0]; k++)
        if (!anorune_is_letter(letters[k])) {
            printf("FAIL: U+%04X should be a letter\n", letters[k]);
            failures++;
        }
    CHECK(!anorune_is_letter('!') && !anorune_is_letter('7') && !anorune_is_letter(0x1F600),
          "punctuation, digits, emoji are not letters");

    // Nd digits: ASCII, Devanagari, Arabic-Indic. Roman numeral twelve is Nl, not Nd.
    CHECK(anorune_is_digit('0') && anorune_is_digit('9'), "ASCII digits");
    CHECK(anorune_is_digit(0x966) && anorune_is_digit(0x96F), "Devanagari digits");
    CHECK(anorune_is_digit(0x663), "Arabic-Indic digit three");
    CHECK(!anorune_is_digit(0x216B), "Roman numeral XII is not Nd");
    CHECK(!anorune_is_digit('a'), "letters are not digits");

    // White_Space property: NBSP and ideographic space are in; ZWSP (format char) is out.
    static const anorune_t spaces[] = { ' ', '\t', '\n', '\r', 0xA0, 0x3000 };
    for (size_t k = 0; k < sizeof spaces / sizeof spaces[0]; k++)
        if (!anorune_is_whitespace(spaces[k])) {
            printf("FAIL: U+%04X should be whitespace\n", spaces[k]);
            failures++;
        }
    CHECK(!anorune_is_whitespace(0x200B), "zero-width space is NOT White_Space (it is Cf)");
    CHECK(!anorune_is_whitespace('a'), "letters are not whitespace");

    // Combining marks: Mn (acute, niqqud), Mc (Devanagari matra), Me (enclosing circle).
    static const anorune_t marks[] = { 0x301, 0x5B4, 0x93E, 0x20DD };
    for (size_t k = 0; k < sizeof marks / sizeof marks[0]; k++)
        if (!anorune_is_mark(marks[k])) {
            printf("FAIL: U+%04X should be a combining mark\n", marks[k]);
            failures++;
        }
    CHECK(!anorune_is_mark('e') && !anorune_is_mark(0xE9) && !anorune_is_mark(0x219),
          "base and precomposed letters are not marks");
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

static void test_encoding_conversion(mi_heap_t *heap)
{
    // "a €𝕨" -- one surrogate pair. UTF-16: 0x0061 0x0020 0x20AC 0xD835 0xDD68.
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

    // Round-trips land on equal values (variant and backing independent).
    CHECK(anostr_eq(anostr_from_utf16(heap, w, 5), s), "utf16 round-trip");
    CHECK(anostr_eq(anostr_from_utf16_cstr(heap, w), s), "utf16 cstr round-trip");
    CHECK(anostr_eq(anostr_from_utf32(heap, u, 4), s), "utf32 round-trip");

    // Unpaired surrogates -> U+FFFD; a high+low pair split by anything is unpaired.
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

    // Invalid UTF-32 input -> U+FFFD.
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

// Random valid scalar values -> encode into a builder -> iterate forward and backward,
// demanding exact agreement with the source runes. Fixed seed, argv[1] scales.
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

        // Both conversions round-trip to an equal value.
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
    // One scratch heap for the whole run; everything long-lived dies with it at exit.
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    test_encode_decode_roundtrip_exhaustive();
    test_malformed_decode();
    test_iteration(heap);
    test_case_mapping();
    test_classification();
    test_builder_append_rune(heap);
    test_encoding_conversion(heap);

    uint32_t iterations = 500;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    soak(heap, iterations);

    if (failures == 0) { printf("anotest_strings_utf: all checks passed\n"); return 0; }
    printf("anotest_strings_utf: %d check(s) failed\n", failures);
    return 1;
}
